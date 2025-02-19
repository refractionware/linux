// SPDX-License-Identifier: GPL-2.0-only
/*
 * Broadcom BCM590XX PMU real-time clock driver
 *
 * Copyright (c) 2025 Artur Weber <aweber.kernel@gmail.com>
 */

#include "linux/regmap.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/mfd/bcm590xx.h>

/*
 * Offsets from time base reg/alarm base reg to registers for each component
 * of the date.
 */
enum bcm590xx_rtc_time_reg_offset {
	BCM590XX_RTC_SECOND_OFFSET = 0,
	BCM590XX_RTC_MINUTE_OFFSET,
	BCM590XX_RTC_HOUR_OFFSET,
	BCM590XX_RTC_DAY_OFFSET,
	BCM590XX_RTC_MONTH_OFFSET,
	BCM590XX_RTC_YEAR_OFFSET,
	BCM590XX_RTC_OFFSET_COUNT,
};

/*
 * Model-specific data struct
 *
 * @regmap		Which regmap to use for RTC data (primary/secondary)
 *
 * @time_base_reg	Base address for time data
 * @alarm_base_reg	Base address for alarm data
 *
 * @alarm_irq		PMU IRQ ID to use for alarm notifications
 */
struct bcm590xx_rtc_data {
	enum bcm590xx_regmap_type regmap;

	u8 time_base_reg;
	u8 alarm_base_reg;

	int alarm_irq;
	int sec_irq;
};

struct bcm590xx_rtc_data bcm59054_rtc_data = {
	.regmap = BCM590XX_REGMAP_SEC,

	.time_base_reg = 0xe0,
	.alarm_base_reg = 0xe7,

	.alarm_irq = BCM59054_IRQ_RTC_ALARM,
	.sec_irq = BCM59054_IRQ_RTC_SEC,
};

struct bcm590xx_rtc_data bcm59056_rtc_data = {
	.regmap = BCM590XX_REGMAP_SEC,

	.time_base_reg = 0xe0,
	.alarm_base_reg = 0xe7,

	.alarm_irq = BCM59056_IRQ_RTC_ALARM,
	.sec_irq = BCM59056_IRQ_RTC_SEC,
};

struct bcm590xx_rtc {
	struct bcm590xx *mfd;
	struct rtc_device	 *rtc_dev;
	struct regmap *regmap;
	const struct bcm590xx_rtc_data *data;

	int alarm_irq;
	int sec_irq;
};

static irqreturn_t bcm590xx_rtc_sec_irq_handler(int irq, void *data)
{
	struct bcm590xx_rtc *rtc = data;

	rtc_update_irq(rtc->rtc_dev, 1, RTC_UF);

	return IRQ_HANDLED;
}

static irqreturn_t bcm590xx_rtc_alarm_irq_handler(int irq, void *data)
{
	struct bcm590xx_rtc *rtc = data;

	rtc_update_irq(rtc->rtc_dev, 1, RTC_AF);

	return IRQ_HANDLED;
}

/*
 * Since both RTC time and alarm time are stored in the same way (just with a
 * different register offset), we can use the same function for reading/writing
 * both, with a custom offset.
 */

/* Read a time value into an rtc_time struct from the specified register. */
static int
_bcm590xx_read_time_from_reg(struct bcm590xx_rtc *rtc, u8 reg, struct rtc_time *tm)
{
	u8 time[BCM590XX_RTC_OFFSET_COUNT];
	int ret;

	ret = regmap_bulk_read(rtc->regmap, rtc->data->time_base_reg,
			       &time, BCM590XX_RTC_OFFSET_COUNT);
	if (ret)
		return ret;

	tm->tm_sec = time[BCM590XX_RTC_SECOND_OFFSET];
	tm->tm_min = time[BCM590XX_RTC_MINUTE_OFFSET];
	tm->tm_hour = time[BCM590XX_RTC_HOUR_OFFSET];
	tm->tm_mday = time[BCM590XX_RTC_DAY_OFFSET];
	tm->tm_mon = time[BCM590XX_RTC_MONTH_OFFSET] - 1;
	tm->tm_year = time[BCM590XX_RTC_YEAR_OFFSET] + 100;

	return 0;
}

static int
_bcm590xx_write_time_to_reg(struct bcm590xx_rtc *rtc, u8 reg, struct rtc_time *tm)
{
	u8 time[BCM590XX_RTC_OFFSET_COUNT];
	int ret;

	time[BCM590XX_RTC_SECOND_OFFSET] = tm->tm_sec;
	time[BCM590XX_RTC_MINUTE_OFFSET] = tm->tm_min;
	time[BCM590XX_RTC_HOUR_OFFSET] = tm->tm_hour;
	time[BCM590XX_RTC_DAY_OFFSET] = tm->tm_mday;
	time[BCM590XX_RTC_MONTH_OFFSET] = tm->tm_mon + 1;
	time[BCM590XX_RTC_YEAR_OFFSET] = tm->tm_year - 100;

	ret = regmap_bulk_write(rtc->regmap, rtc->data->time_base_reg,
			       &time, BCM590XX_RTC_OFFSET_COUNT);

	return ret;
}

static int bcm590xx_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct bcm590xx_rtc *rtc = dev_get_drvdata(dev);
	int ret;

	ret = _bcm590xx_read_time_from_reg(rtc, rtc->data->time_base_reg, tm);
	if (ret)
		dev_err(dev, "Failed to read time regs: %d\n", ret);

	return ret;
}

static int bcm590xx_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct bcm590xx_rtc *rtc = dev_get_drvdata(dev);
	int ret;

	ret = _bcm590xx_write_time_to_reg(rtc, rtc->data->time_base_reg, tm);
	if (ret)
		dev_err(dev, "Failed to write time regs: %d\n", ret);

	return ret;
}

static int bcm590xx_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct bcm590xx_rtc *rtc = dev_get_drvdata(dev);
	int ret;

	ret = _bcm590xx_read_time_from_reg(rtc, rtc->data->alarm_base_reg,
					   &alrm->time);
	if (ret)
		dev_err(dev, "Failed to read alarm time regs: %d\n", ret);

	return ret;
}

static int bcm590xx_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct bcm590xx_rtc *rtc = dev_get_drvdata(dev);
	int ret;

	ret = _bcm590xx_write_time_to_reg(rtc, rtc->data->alarm_base_reg,
					  &alrm->time);
	if (ret)
		dev_err(dev, "Failed to write alarm time regs: %d\n", ret);

	return ret;
}

static int bcm590xx_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct bcm590xx_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time reset_time = { .tm_year = 0xFF, .tm_mon = 0 };
	int ret;

	if (enabled) {
		ret = bcm590xx_devm_request_irq(dev, rtc->mfd,
						rtc->data->alarm_irq,
						bcm590xx_rtc_alarm_irq_handler,
						0, "rtc", rtc);

		if (rtc->alarm_irq < 0) {
			dev_err(dev, "Failed to request alarm IRQ: %d\n", ret);
			return ret;
		}

		rtc->alarm_irq = ret;
	} else {
		if (rtc->alarm_irq < 0)
			return 0;

		devm_free_irq(dev, rtc->alarm_irq, rtc);
		rtc->alarm_irq = -1;

		/* Clear the alarm */
		_bcm590xx_write_time_to_reg(rtc, rtc->data->alarm_base_reg,
					    &reset_time);
	}

	return 0;
}

static const struct rtc_class_ops bcm590xx_rtc_ops = {
	.read_time	= bcm590xx_rtc_read_time,
	.set_time	= bcm590xx_rtc_set_time,
	.read_alarm	= bcm590xx_rtc_read_alarm,
	.set_alarm	= bcm590xx_rtc_set_alarm,
	.alarm_irq_enable = bcm590xx_rtc_alarm_irq_enable,
};

static int bcm590xx_rtc_probe(struct platform_device *pdev)
{
	struct bcm590xx *bcm590xx = dev_get_drvdata(pdev->dev.parent);
	struct bcm590xx_rtc *rtc;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(struct bcm590xx_rtc),
			   GFP_KERNEL);
	if (!rtc) {
		dev_err(&pdev->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	rtc->mfd = bcm590xx;
	rtc->data = of_device_get_match_data(&pdev->dev);

	dev_set_drvdata(&pdev->dev, rtc);

	switch (rtc->data->regmap) {
	case BCM590XX_REGMAP_PRI:
		rtc->regmap = rtc->mfd->regmap_pri;
		break;
	case BCM590XX_REGMAP_SEC:
		rtc->regmap = rtc->mfd->regmap_sec;
		break;
	default:
		dev_err(&pdev->dev,
			"Invalid regmap value; this is a driver bug!\n");
		return -EINVAL;
	}

	/* Alarm IRQ is requested in bcm590xx_rtc_alarm_irq_enable */
	rtc->alarm_irq = -1;

	rtc->sec_irq = bcm590xx_devm_request_irq(&pdev->dev, bcm590xx,
						 rtc->data->sec_irq,
						 bcm590xx_rtc_sec_irq_handler,
						 0, "rtc-sec", rtc);
	if (rtc->sec_irq < 0) {
		dev_err(&pdev->dev, "Failed to request second update IRQ: %d\n",
			rtc->sec_irq);
		return rtc->sec_irq;
	}

	rtc->rtc_dev = devm_rtc_device_register(&pdev->dev, "bcm590xx-rtc",
						&bcm590xx_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rtc_dev)) {
		ret = PTR_ERR(rtc->rtc_dev);
		dev_err(&pdev->dev, "Failed to register RTC device: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id bcm590xx_rtc_match[] = {
	{ .compatible = "brcm,bcm59054-rtc", .data = &bcm59054_rtc_data, },
	{ .compatible = "brcm,bcm59056-rtc", .data = &bcm59056_rtc_data, },
	{}
};
MODULE_DEVICE_TABLE(of, bcm590xx_rtc_match);

static struct platform_driver bcm590xx_rtc_driver = {
	.driver		= {
		.name	= "bcm590xx-rtc",
		.of_match_table = of_match_ptr(bcm590xx_rtc_match),
	},
	.probe		= bcm590xx_rtc_probe,
};

module_platform_driver(bcm590xx_rtc_driver);

MODULE_DESCRIPTION("Broadcom BCM590XX PMU RTC driver");
MODULE_AUTHOR("Artur Weber <aweber.kernel@gmail.com>");
MODULE_LICENSE("GPL");
