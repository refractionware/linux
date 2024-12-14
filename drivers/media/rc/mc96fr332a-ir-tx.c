// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for ABOV MC96FR332A MCU programmed as an IR transmitter, as seen
 * in the Samsung Galaxy Tab 3 8.0 and the Samsung Galaxy Note 10.1.
 *
 * Copyright (C) 2024 Artur Weber <aweber.kernel@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <media/rc-core.h>

#include "mc96fr332a-ir-tx.h"

#define DEVICE_NAME	"ABOV Semiconductor MC96FR332A IR TX"
#define DRIVER_NAME	"mc96fr332a-ir-tx"

struct mc96fr332a_ir_tx {
	struct i2c_client	 *client;
	struct device *dev;

	struct gpio_desc *wake_gpio;
	struct gpio_desc *status_gpio;
	struct regulator *ldo_regulator;
	struct regulator *vdd_regulator;

	u32 carrier;

	/* The I2C write buffer used during TX transfers is stored here
	 * due to stack size limits. */
	u8 i2c_buf[2048];
};

static int mc96fr332a_ir_tx_power_on(struct mc96fr332a_ir_tx *mc96)
{
	int ret;

	ret = regulator_enable(mc96->ldo_regulator);
	if (ret)
		return ret;

	ret = regulator_enable(mc96->vdd_regulator);
	if (ret)
		return ret;

	return 0;
}

static void mc96fr332a_ir_tx_power_off(struct mc96fr332a_ir_tx *mc96)
{
	regulator_disable(mc96->ldo_regulator);
	regulator_disable(mc96->vdd_regulator);
}

static void mc96fr332a_ir_tx_set_wake(struct mc96fr332a_ir_tx *mc96, int value)
{
	gpiod_set_value(mc96->wake_gpio, value);
}

/*
 * The MC96FR332A chip in the Samsung tablets is flashed with a custom
 * bootcode, presumably written by Samsung. The operation of this bootcode
 * can be described as follows:
 *
 * - An I2C client is exposed at address 0x50;
 * - On boot, the "firmware version" is set to 0xffff
 * - To enter flashing mode, the chip is restarted with `wake_en` switched OFF
 * - The code awaits a firmware upload; it is transmitted in 70-byte chunks
 *   via a series of I2C block writes (note: the last chunk is 6 bytes large);
 * - Once the firmware is written and the checksum is verified to be correct,
 *   the chip is rebooted, this time with wake_en enabled, and is ready to receive
 *   a signal.
 */

static int mc96fr332a_ir_tx_get_fw_version(struct mc96fr332a_ir_tx *mc96)
{
	struct i2c_client *client = mc96->client;
	int ret;
	u8 buf[8];

	ret = i2c_smbus_read_i2c_block_data(client, 0x00, 8, buf);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to get firmware version: %d\n", ret);
		return ret;
	}

	return buf[2] << 8 | buf[3];
}

static bool mc96fr332a_ir_tx_verify_fw_checksum(struct mc96fr332a_ir_tx *mc96)
{
	struct i2c_client *client = mc96->client;
	int ret, i, checksum_a, checksum_b;
	u8 buf[8];

	ret = i2c_smbus_read_i2c_block_data(client, 0x00, 8, buf);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to get firmware checksum: %d\n", ret);
		return ret;
	}

	checksum_a = buf[6] << 8 | buf[7];
	dev_info(mc96->dev, "checksum %d\n", checksum_a);

	checksum_b = 0;
	for (i = 0; i < 6; i++)
		checksum_b += buf[i];

	return (checksum_a == checksum_b);
}

/* TODO: debug function, remove this */
/*static void mc96fr332a_ir_tx_print_buffer(struct mc96fr332a_ir_tx *mc96)
{
	struct i2c_client *client = mc96->client;
	int ret;
	u8 buf[8];

	ret = i2c_smbus_read_i2c_block_data(client, 0x00, 8, buf);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to get buffer for printing: %d\n", ret);
		return;
	}

	dev_info(mc96->dev, "Current buffer: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
}*/

/* TODO: move firmware out of the kernel */
static int mc96fr332a_ir_tx_update_fw(struct mc96fr332a_ir_tx *mc96) {
	bool checksum_correct;
	int fw_version, i, len;
	int ret = 0;

	// mc96fr332a_ir_tx_power_off(mc96);
	// msleep(20);

	mc96fr332a_ir_tx_power_on(mc96);
	mc96fr332a_ir_tx_set_wake(mc96, 1);
	msleep(100);

	fw_version = mc96fr332a_ir_tx_get_fw_version(mc96);
	if (fw_version == MC96FR332A_IR_TX_FIRMWARE_VERSION) {
		/* Already on latest firmware, return */
		goto out;
	}

	dev_info(mc96->dev, "Need to update firmware (current version: %d)\n", fw_version);

	mc96fr332a_ir_tx_power_off(mc96);
	mc96fr332a_ir_tx_set_wake(mc96, 0);
	msleep(20);

	mc96fr332a_ir_tx_power_on(mc96);
	msleep(100);

	checksum_correct = mc96fr332a_ir_tx_verify_fw_checksum(mc96);
	if (!checksum_correct) {
		dev_err(mc96->dev, "Firmware is out-of-date and bootrom checksum is broken\n");
		ret = -EINVAL;
		goto out;
	}

	msleep(30);

	/* Write new firmware */

	for (i = 0; i < MC96FR332A_IR_TX_FIRMWARE_FRAME_COUNT; i++) {
		if (i == MC96FR332A_IR_TX_FIRMWARE_FRAME_COUNT - 1)
			len = 6;
		else
			len = 70;
		ret = i2c_master_send(mc96->client, &IRDA_binary[i * 70], len);
		//ret = i2c_smbus_write_i2c_block_data(mc96->client, 0x00, len, &IRDA_binary[i * 70]);
		if (ret < 0) {
			dev_err(mc96->dev, "Failed to write firmware: %d\n", ret);
			goto out;
		} else {
			dev_info(mc96->dev, "Wrote firmware frame %d\n", i);
		}
		msleep(30);
	}

	mc96fr332a_ir_tx_power_off(mc96);
	msleep(20);

	mc96fr332a_ir_tx_power_on(mc96);
	mc96fr332a_ir_tx_set_wake(mc96, 1);
	msleep(100);

	checksum_correct = mc96fr332a_ir_tx_verify_fw_checksum(mc96);
	if (!checksum_correct) {
		dev_err(mc96->dev, "Post-firmware write checksum check failed\n");
		ret = -EINVAL;
		goto out;
	}

out:
	mc96fr332a_ir_tx_power_off(mc96);
	mc96fr332a_ir_tx_set_wake(mc96, 0);

	return ret;
}

/*
 * remcon_store:
 * - read one unsigned integer from char buffer `buf` and save it into `_data`
 *   - this read is null-terminated, i.e. a value of 0 is a break
 *   - the maximum loop count is 2048 (`MAX_SIZE`)
 *   - it would appear that these integers are 32-bit.
 * - an incrementing count of read integers is stored in `data->count` (henceforth `count`)
 * (first read refers to count = 0. as far as i can tell, this only happens on the first read ever;
 *  after that, the count is always reset to 2. weird bug or what?)
 * - on the first read and all reads past the second read:
 *   - increment `ir_sum` by `_data`
 *   - save 2 bytes into `signal`
 * - on the second read:
 *   - set `ir_freq` to `_data`
 *   - wake up the controller:
 *     - if it's off, turn vdd on, set wake_en on and wait 60ms
 *     - if it's on, turn wake_en off, wait 200us, turn it back on and wait 30ms
 *   - save 3 bytes into `signal`
 * once the read ends, pass off to ir_remocon_work.
 *
 * ir_remocon_work:
 * - the first 2 bytes of `signal` are set to the final count (irda_add_checksum_length)
 * - a checksum is calculated, containing the sum of all signals; it is saved at the end,
 *   right after all the signals (irda_add_checksum_length)
 * - the entirety of `signal` is sent out in one i2c block send
 * - the firmware verifies the checksum; if the irq pin is pulled up after 10ms,
 *   the checksum is assumed to be correct. (we should clear this pin, imo!)
 * - `ir_sum` and `ir_freq` are used to calculate the estimated emission time
 * - after the estimated emission time elapses, if the irq pin is pulled up,
 *   the ir is assumed to be sent correctly.
 *
 * in other words: the data transferred to the chip follows this format:
 * | cc | fff | ss | ss | .. | ss | mm |
 * (c - signal count, f - frequency, s - signal, m - checksum.)
 * (how many times the letters repeat dictates the amount of bytes)
 */

static int mc96fr332a_ir_tx(struct rc_dev *rc_dev, unsigned int *txbuf,
		      unsigned int count)
{
	struct mc96fr332a_ir_tx *mc96 = rc_dev->priv;
	unsigned int bufsize = (count * 2) + 7; /* 2 + 3 + (count * 2) + 2 */
	int checksum = 0;
	int i, ret;

	dev_info(&rc_dev->dev, "received transfer of count %d, freq %d\n", count, mc96->carrier);

	if ((bufsize > 2048)) {
		dev_err(&rc_dev->dev, "Transfer buffer too large\n");
		return -EINVAL;
	}

	/* Message length (including itself, excluding checksum): 2 bytes */
	mc96->i2c_buf[0] = ((bufsize - 2) >> 8) & 0xFF;
	mc96->i2c_buf[1] = (bufsize - 2) & 0xFF;

	/* Frequency: 3 bytes */
	mc96->i2c_buf[2] = (mc96->carrier >> 16) & 0xFF;
	mc96->i2c_buf[3] = (mc96->carrier >> 8) & 0xFF;
	mc96->i2c_buf[4] = mc96->carrier & 0xFF;

	/* Signal: 2 bytes each */
	for (i = 0; i < count; i++) {
		if (txbuf[i] > 0xFFFF) {
			dev_err(&rc_dev->dev, "Transfer value too large\n");
			return -EINVAL;
		}

		mc96->i2c_buf[5 + (i * 2)]     = (txbuf[i] >> 8) & 0xFF;
		mc96->i2c_buf[5 + (i * 2) + 1] = (txbuf[i]) & 0xFF;
	}

	/* Checksum: last 2 bytes */
	for (i = 0; i < (bufsize - 2); i++) {
		checksum += mc96->i2c_buf[i];
		dev_info(mc96->dev, "%d:%x\n", i, mc96->i2c_buf[i]);
	}

	mc96->i2c_buf[bufsize - 2] = (checksum >> 8) & 0xFF;
	mc96->i2c_buf[bufsize - 1] = checksum & 0xFF;

	dev_info(mc96->dev, "%d:%x\n", bufsize-2, mc96->i2c_buf[bufsize-2]);
	dev_info(mc96->dev, "%d:%x\n", bufsize-1, mc96->i2c_buf[bufsize-1]);

	/* Wake up the transmitter and send the prepared data */
	mc96fr332a_ir_tx_set_wake(mc96, 0);
	udelay(200);
	mc96fr332a_ir_tx_set_wake(mc96, 1);
	msleep(30);

	//ret = i2c_smbus_write_i2c_block_data(mc96->client, 0x00, bufsize, mc96->i2c_buf);
	ret = i2c_master_send(mc96->client, mc96->i2c_buf, bufsize);
	if (ret < 0) {
		dev_err(&rc_dev->dev, "Failed to write IR transfer data (%d)\n", ret);
		return ret;
	}

	dev_info(&rc_dev->dev, "IR transfer data written\n");

	mdelay(10);

	ret = gpiod_get_value(mc96->status_gpio);
	if (ret < 0) {
		dev_err(&rc_dev->dev, "Failed to get state of transfer\n");
		return -EINVAL;
	} else if (ret == 1) {
		dev_err(&rc_dev->dev, "Transfer checksum is not OK\n");
		return -EINVAL;
	}

	i = 5;
	while (i > 0) {
		msleep(1000 * (checksum / mc96->carrier));
		ret = gpiod_get_value(mc96->status_gpio);
		if (ret < 0) {
			dev_err(&rc_dev->dev, "Failed to get state of transfer\n");
			return -EINVAL;
		}

		dev_info(&rc_dev->dev, "Transfer status: %d\n", ret);

		if (ret == 1) {
			break;
		}
		i--;
	}

	if (ret == 0)
		dev_err(&rc_dev->dev, "Transfer failed");

	mc96fr332a_ir_tx_set_wake(mc96, 0);

	return 0;
}

static int mc96fr332a_ir_tx_set_carrier(struct rc_dev *rc_dev, u32 carrier)
{
	struct mc96fr332a_ir_tx *mc96 = rc_dev->priv;

	/* The frequency can be a maximum of 3 bytes (24 bits) */
	if (carrier > 0xFFFFFF)
		return -EINVAL;

	mc96->carrier = carrier;

	return 0;
}

static int mc96fr332a_ir_tx_open(struct rc_dev *rc_dev)
{
	struct mc96fr332a_ir_tx *mc96 = rc_dev->priv;
	int ret;

	mc96fr332a_ir_tx_set_wake(mc96, 1);
	ret = mc96fr332a_ir_tx_power_on(mc96);
	if (ret < 0) {
		dev_err(mc96->dev, "Failed to power on: %d\n", ret);
		return ret;
	}

	msleep(30);

	dev_info(mc96->dev, "opened device\n");

	return 0;
}

static void mc96fr332a_ir_tx_close(struct rc_dev *rc_dev)
{
	struct mc96fr332a_ir_tx *mc96 = rc_dev->priv;

	//mc96fr332a_ir_tx_power_off(mc96);
	msleep(10);

	dev_info(mc96->dev, "closed device\n");
}

static int mc96fr332a_ir_tx_probe(struct i2c_client *client)
{
	struct mc96fr332a_ir_tx *mc96;
	struct device *dev = &client->dev;
	struct rc_dev *rcdev;
	int ret;

	mc96 = devm_kmalloc(dev, sizeof(*mc96), GFP_KERNEL);
	if (!mc96) {
		dev_err(dev, "Failed to allocate memory for driver data\n");
		return -ENOMEM;
	}

	rcdev = devm_rc_allocate_device(dev, RC_DRIVER_IR_RAW_TX);
	if (!rcdev)
		return -ENOMEM;

	i2c_set_clientdata(client, mc96);
	mc96->client = client;
	mc96->dev = dev;

	mc96->wake_gpio = devm_gpiod_get(dev, "wake", GPIOD_OUT_LOW);
	if (IS_ERR(mc96->wake_gpio))
		return dev_err_probe(dev, PTR_ERR(mc96->wake_gpio),
				     "Failed to get wake GPIO\n");

	mc96->status_gpio = devm_gpiod_get(dev, "status", GPIOD_IN);
	if (IS_ERR(mc96->status_gpio))
		return dev_err_probe(dev, PTR_ERR(mc96->status_gpio),
				     "Failed to get status GPIO\n");

	mc96->ldo_regulator = devm_regulator_get(dev, "ldo");
	if (IS_ERR(mc96->ldo_regulator))
		return dev_err_probe(dev, PTR_ERR(mc96->ldo_regulator),
				     "Failed to get LDO regulator\n");

	mc96->vdd_regulator = devm_regulator_get(dev, "vdd");
	if (IS_ERR(mc96->vdd_regulator))
		return dev_err_probe(dev, PTR_ERR(mc96->vdd_regulator),
				     "Failed to get VDD regulator\n");

	rcdev->priv = mc96;
	rcdev->driver_name = DRIVER_NAME;
	rcdev->device_name = DEVICE_NAME;
	rcdev->tx_ir = mc96fr332a_ir_tx;
	rcdev->s_tx_carrier = mc96fr332a_ir_tx_set_carrier;
	rcdev->open = mc96fr332a_ir_tx_open;
	rcdev->close = mc96fr332a_ir_tx_close;

	mc96->carrier = 38000;

	ret = devm_rc_register_device(dev, rcdev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to register RC device\n");

	ret = mc96fr332a_ir_tx_update_fw(mc96);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to update firmware\n");

	return 0;
}

static const struct of_device_id mc96fr332a_ir_tx_of_match[] = {
	{ .compatible = "abov,mc96fr332a-ir-tx", },
	{ },
};
MODULE_DEVICE_TABLE(of, mc96fr332a_ir_tx_of_match);

static const struct i2c_device_id foo_idtable[] = {
      { "mc96fr332a", 0 },
      { }
};
MODULE_DEVICE_TABLE(i2c, foo_idtable);

static struct i2c_driver mc96fr332a_ir_tx_driver = {
	.driver = {
		.name		= DRIVER_NAME,
		.of_match_table = mc96fr332a_ir_tx_of_match,
	},

	.probe	= mc96fr332a_ir_tx_probe,
};
module_i2c_driver(mc96fr332a_ir_tx_driver);

MODULE_DESCRIPTION("ABOV Semiconductor MC96FR332A IR TX");
MODULE_AUTHOR("Artur Weber <aweber.kernel@gmail.com>");
MODULE_LICENSE("GPL");
