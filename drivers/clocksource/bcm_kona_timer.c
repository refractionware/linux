// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2012 Broadcom Corporation

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/clockchips.h>
#include <linux/types.h>
#include <linux/clk.h>

#include <linux/io.h>

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define KONA_GPTIMER_STCS_OFFSET			0x00000000
#define KONA_GPTIMER_STCLO_OFFSET			0x00000004
#define KONA_GPTIMER_STCHI_OFFSET			0x00000008
#define KONA_GPTIMER_STCM0_OFFSET			0x0000000C

#define KONA_GPTIMER_STCS_TIMER_MATCH_SHIFT		0
#define KONA_GPTIMER_STCS_COMPARE_ENABLE_SHIFT		4
#define KONA_GPTIMER_STCS_COMPARE_ENABLE_SYNC_SHIFT	8
#define KONA_GPTIMER_STCS_STCM0_SYNC_SHIFT		12

/*
 * There are 2 timers for Kona (AON and Peripheral), plus Core for the
 * BCM23550, adding up to a potential total of 3.
 */
#define MAX_NUM_TIMERS			3

/* Each timer has 4 channels, each with its own IRQ. */
#define MAX_NUM_CHANNELS		4

struct kona_bcm_timer_channel {
	/* Number of parent timer in the timers struct */
	unsigned int timer_id;
	/* Number of channel, from 0 to 3 */
	unsigned int id;
	/* IRQ of the channel */
	unsigned int irq;

	bool has_clockevent;
	struct clock_event_device clockevent;
};

struct kona_bcm_timer {
	char *name;

	unsigned int rate;
	void __iomem *base;

	struct kona_bcm_timer_channel channels[MAX_NUM_CHANNELS];
	unsigned int num_channels;
};

static struct kona_bcm_timer *timers[MAX_NUM_TIMERS];
static int num_timers = 0; /* Count of currently initialized timers */

static inline struct kona_bcm_timer *
channel_to_timer(struct kona_bcm_timer_channel *channel)
{
	return timers[channel->timer_id];
}

static inline struct kona_bcm_timer_channel *
clockevent_to_channel(struct clock_event_device *evt)
{
	return container_of(evt, struct kona_bcm_timer_channel, clockevent);
}

/* Wait for the new compare value to be loaded */
static void
kona_wait_for_compare_val_sync(void __iomem *base, int ch_num)
{
	int loop_limit = 1000;
	uint32_t reg;

	do {
		reg = readl(base + KONA_GPTIMER_STCS_OFFSET);
		if (reg & (1 << (KONA_GPTIMER_STCS_STCM0_SYNC_SHIFT + ch_num)))
			break;
	} while (--loop_limit);

	if (!loop_limit)
		pr_err("kona-timer: compare value sync timed out\n");
}

/* Wait for compare enable to be synced */
static void
kona_wait_for_compare_enable_sync(void __iomem *base, int ch_num, int target)
{
	u32 shift = KONA_GPTIMER_STCS_COMPARE_ENABLE_SYNC_SHIFT + ch_num;
	int loop_limit = 1000;
	uint32_t reg;

	do {
		reg = readl(base + KONA_GPTIMER_STCS_OFFSET);
		if (((reg & (1 << shift)) >> shift) == target)
			break;
	} while (--loop_limit);

	if (!loop_limit)
		pr_err("kona-timer: compare enable sync timed out\n");
}

/*
 * We use the peripheral timers for system tick, the cpu global timer for
 * profile tick
 */
static void kona_timer_disable_and_clear(void __iomem *base, int ch_num)
{
	uint32_t reg;

	reg = readl(base + KONA_GPTIMER_STCS_OFFSET);

	/* Make sure we're servicing the correct interrupt */
	if (!(reg & (1 << (KONA_GPTIMER_STCS_TIMER_MATCH_SHIFT + ch_num))))
		return;

	/* Clear compare (0) interrupt */
	reg |= 1 << (KONA_GPTIMER_STCS_TIMER_MATCH_SHIFT + ch_num);
	/* disable compare */
	reg &= ~(1 << (KONA_GPTIMER_STCS_COMPARE_ENABLE_SHIFT + ch_num));

	writel(reg, base + KONA_GPTIMER_STCS_OFFSET);

	kona_wait_for_compare_enable_sync(base, ch_num, 0);
}

static int
kona_timer_get_counter(void __iomem *timer_base, uint32_t *msw, uint32_t *lsw)
{
	int loop_limit = 3;

	/*
	 * Read 64-bit free running counter
	 * 1. Read hi-word
	 * 2. Read low-word
	 * 3. Read hi-word again
	 * 4.1
	 *      if new hi-word is not equal to previously read hi-word, then
	 *      start from #1
	 * 4.2
	 *      if new hi-word is equal to previously read hi-word then stop.
	 */

	do {
		*msw = readl(timer_base + KONA_GPTIMER_STCHI_OFFSET);
		*lsw = readl(timer_base + KONA_GPTIMER_STCLO_OFFSET);
		if (*msw == readl(timer_base + KONA_GPTIMER_STCHI_OFFSET))
			break;
	} while (--loop_limit);
	if (!loop_limit) {
		pr_err("kona-timer: getting counter failed, timer will "
		       "be impacted\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int kona_timer_set_next_event(unsigned long clc,
				  struct clock_event_device *evt)
{
	/*
	 * timer (0) is disabled by the timer interrupt already
	 * so, here we reload the next event value and re-enable
	 * the timer.
	 *
	 * This way, we are potentially losing the time between
	 * timer-interrupt->set_next_event. CPU local timers, when
	 * they come in should get rid of skew.
	 */

	struct kona_bcm_timer_channel *channel = clockevent_to_channel(evt);
	struct kona_bcm_timer *timer = channel_to_timer(channel);
	uint32_t lsw, msw;
	uint32_t reg;
	int ret;

	ret = kona_timer_get_counter(timer->base, &msw, &lsw);
	if (ret)
		return ret;

	/* Load the "next" event tick value */
	writel(lsw + clc,
	       timer->base + KONA_GPTIMER_STCM0_OFFSET + channel->id * 4);

	/* Wait for the value to sync */
	kona_wait_for_compare_val_sync(timer->base, channel->id);

	/* Set timer match bit and enable compare. */
	reg = readl(timer->base + KONA_GPTIMER_STCS_OFFSET);
	reg |= (1 << (KONA_GPTIMER_STCS_TIMER_MATCH_SHIFT + channel->id));
	reg |= (1 << (KONA_GPTIMER_STCS_COMPARE_ENABLE_SHIFT + channel->id));
	writel(reg, timer->base + KONA_GPTIMER_STCS_OFFSET);

	return 0;
}

static int kona_timer_shutdown(struct clock_event_device *evt)
{
	struct kona_bcm_timer_channel *channel = clockevent_to_channel(evt);
	if (!channel) {
		pr_err("kona-timer: no channel for clockevent\n");
		return 0;
	}
	struct kona_bcm_timer *timer = channel_to_timer(channel);
	if (!timer) {
		pr_err("kona-timer: no timer for clockevent\n");
		return 0;
	}
	kona_timer_disable_and_clear(timer->base, channel->id);
	return 0;
}

static void __init
kona_timer_clockevents_init(struct kona_bcm_timer *timer,
				struct kona_bcm_timer_channel *channel)
{
	channel->clockevent.name = "system timer";
	channel->clockevent.features = CLOCK_EVT_FEAT_ONESHOT;
	channel->clockevent.set_next_event = kona_timer_set_next_event;
	channel->clockevent.set_state_shutdown = kona_timer_shutdown;
	channel->clockevent.tick_resume = kona_timer_shutdown;
	/*channel->clockevent.irq = channel->irq;*/
	channel->clockevent.cpumask = cpumask_of(0);

	channel->has_clockevent = true;

	clockevents_config_and_register(&channel->clockevent,
		timer->rate, 6, 0xffffffff);
}

static irqreturn_t kona_timer_interrupt(int irq, void *dev_id)
{
	if (!dev_id) {
		pr_err("kona-timer: no dev_id\n");
		return IRQ_HANDLED;
	}

	struct kona_bcm_timer_channel *channel = dev_id;
	struct kona_bcm_timer *timer = channel_to_timer(channel);

	if (!timer) {
		pr_err("kona-timer: no timer\n");
		return IRQ_HANDLED;
	}

	kona_timer_disable_and_clear(timer->base, channel->id);
	if (channel->has_clockevent)
		channel->clockevent.event_handler(&channel->clockevent);
	return IRQ_HANDLED;
}

static int __init kona_timer_init(struct device_node *node)
{
	struct kona_bcm_timer *timer;
	struct clk *external_clk;
	unsigned int i;
	u32 freq;

	if ((num_timers + 1) >= MAX_NUM_TIMERS) {
		pr_err("kona-timer: exceeded maximum number of timers (%d)\n",
			MAX_NUM_TIMERS);
		return -EINVAL;
	}

	timer = kzalloc(struct_size(timer, channels, MAX_NUM_CHANNELS),
			GFP_KERNEL);
	if (!timer)
		return -ENOMEM;

	external_clk = of_clk_get_by_name(node, NULL);

	if (!IS_ERR(external_clk)) {
		timer->rate = clk_get_rate(external_clk);
		clk_prepare_enable(external_clk);
	} else if (!of_property_read_u32(node, "clock-frequency", &freq)) {
		timer->rate = freq;
	} else {
		pr_err("kona-timer: unable to determine clock-frequency\n");
		goto err_free_timer;
	}

	/* Setup IO address */
	timer->base = of_iomap(node, 0);
	if (!timer->base) {
		pr_err("kona-timer: unable to map base\n");
		goto err_free_timer;
	}

	/*
	 * Each channel has one IRQ; the amount of channels is thus taken from
	 * the IRQ count.
	 */
	timer->num_channels = of_irq_count(node);
	if (timer->num_channels == 0) {
		pr_err("kona-timer: no interrupts provided\n");
		goto err_free_timer;
	} else if (timer->num_channels > MAX_NUM_CHANNELS) {
		pr_err("kona-timer: too many interrupts provided, capping out at %d",
			MAX_NUM_CHANNELS);
		timer->num_channels = MAX_NUM_CHANNELS;
	};

	pr_debug("kona-timer: initializing timer %d, %d channels\n",
		 num_timers, timer->num_channels);

	/* Add the timer pointer to the list of timers */
	timers[num_timers] = timer;

	/* Initialize the timer */
	for (i = 0; i < timer->num_channels; i++) {
		timer->channels[i].id = i;
		timer->channels[i].timer_id = num_timers;

		kona_timer_disable_and_clear(timer->base, i);
		kona_timer_clockevents_init(timer,
					&timer->channels[i]);
		kona_timer_set_next_event((timer->rate / HZ),
					&timer->channels[i].clockevent);

		timer->channels[i].irq = irq_of_parse_and_map(node, i);
		if (request_irq(timer->channels[i].irq, kona_timer_interrupt,
				IRQF_TIMER, "Kona Timer Tick",
				&timer->channels[i])) {
			pr_err("kona-timer: request_irq() failed\n");
			goto err_free_irqs;
		}
	};

	num_timers++;

	return 0;

err_free_irqs:
	while (i != 0) {
		i--;
		free_irq(timer->channels[i].irq, &timer->channels[i]);
	}

err_free_timer:
	kfree(timer);
	return -EINVAL;
}

TIMER_OF_DECLARE(brcm_kona, "brcm,kona-timer", kona_timer_init);
/*
 * bcm,kona-timer is deprecated by brcm,kona-timer
 * being kept here for driver compatibility
 */
TIMER_OF_DECLARE(bcm_kona, "bcm,kona-timer", kona_timer_init);
