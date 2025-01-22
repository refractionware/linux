// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony ISX012 image sensor driver
 *
 * Copyright (C) 2024 Artur Weber <aweber.kernel@gmail.com>
 */

#include "linux/media-bus-format.h"
#include "linux/v4l2-subdev.h"
#include "media/v4l2-common.h"
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <media/v4l2-common.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define ISX012_DRIVER_NAME		"ISX012"

/* Status */
#define ISX012_REG_INTSTS0		0x000e
#define ISX012_REG_INTCLR0		0x0012

enum isx012_status_bit {
	ISX012_STS_OM_CHANGED		= (1 << 0),  /* Operating mode changed */
	ISX012_STS_CM_CHANGED		= (1 << 1),  /* Camera mode changed */
	ISX012_STS_JPEG_UPDATE		= (1 << 2),
	ISX012_STS_CAPNUM_END		= (1 << 3),
	ISX012_STS_AF_LOCK		= (1 << 4),
	ISX012_STS_VINT			= (1 << 5),
};

#define ISX012_REG_MODESEL		0x0081

/* Undocumented; name is assumed from function */
#define ISX012_REG_STREAMMODE		0x00BF

#define ISX012_REG_SENSMODE_MONI	0x0083
#define ISX012_REG_SENSMODE_CAP		0x0084
#define ISX012_REG_SENSMODE_MOVIE	0x0085

#define ISX012_REG_FPSTYPE_MONI		0x0086
#define ISX012_REG_FPSTYPE_CAP		0x0087
#define ISX012_REG_FPSTYPE_MOVIE	0x0088

#define ISX012_REG_OUTFMT_MONI		0x0089
#define ISX012_REG_OUTFMT_CAP		0x008A
#define ISX012_REG_OUTFMT_MOVIE		0x008B

#define ISX012_REG_HSIZE_MONI		0x0090
#define ISX012_REG_HSIZE_CAP		0x0092
#define ISX012_REG_HSIZE_MOVIE		0x0094

#define ISX012_HSIZE_MIN		96

#define ISX012_REG_VSIZE_MONI		0x0096
#define ISX012_REG_VSIZE_CAP		0x0098
#define ISX012_REG_VSIZE_MOVIE		0x009A

#define ISX012_VSIZE_MIN		64

#define ISX012_REG_VIFMODE		0x1E00

#define ISX012_REG_VADJ_SENS_1_1	0x018C
#define ISX012_REG_VADJ_SENS_1_2	0x018E
#define ISX012_REG_VADJ_SENS_1_4	0x0190
#define ISXO12_REG_VADJ_SENS_1_8	0x0192
#define ISX012_REG_VADJ_SENS_HD_1_1	0x0194
#define ISX012_REG_VADJ_SENS_HD_1_2	0x0196

#define ISX012_PIXEL_ARRAY_WIDTH	2592U
#define ISX012_PIXEL_ARRAY_HEIGHT	1944U

struct isx012_framesize {
	u32 width;
	u32 height;
};

/* The values in this enum match the MODESEL register values. */
enum isx012_mode {
	ISX012_MODE_MONITORING		= 0,
	ISX012_MODE_HALFRELEASE		= 1,
	ISX012_MODE_CAPTURE		= 2,
	ISX012_MODE_MOVIE		= 3,
	ISX012_MODE_MAX,
};

/* The values in this enum match the SENSMODE register values. */
enum isx012_sensmode {
	ISX012_SENSMODE_ALLPIX		= 0,
	ISX012_SENSMODE_1_2		= 1,
	ISX012_SENSMODE_1_4		= 2,
	ISX012_SENSMODE_1_8		= 4,
};

/* The values in this enum match the FPSTYPE register values. */
enum isx012_fpstype {
	ISX012_FPSTYPE_120FPS		= 0,
	ISX012_FPSTYPE_60FPS		= 1,
	ISX012_FPSTYPE_30FPS		= 2,
	ISX012_FPSTYPE_15FPS		= 3,
	ISX012_FPSTYPE_10FPS		= 4,
	ISX012_FPSTYPE_7_5FPS		= 5,
	ISX012_FPSTYPE_6FPS		= 6,
	ISX012_FPSTYPE_5FPS		= 7,
};

static enum isx012_sensmode isx012_fpstype_to_sensmode(enum isx012_fpstype fpstype)
{
	switch (fpstype) {
	case ISX012_FPSTYPE_120FPS:
		return ISX012_SENSMODE_1_8;
	case ISX012_FPSTYPE_60FPS:
		return ISX012_SENSMODE_1_4;
	case ISX012_FPSTYPE_30FPS:
		return ISX012_SENSMODE_1_2;
	default:
		return ISX012_SENSMODE_ALLPIX;
	}
}

enum isx012_streammode {
	ISX012_STREAMMODE_ON		= 0,
	ISX012_STREAMMODE_OFF		= 1,
};

/* The values in this enum match the OUTFMT register values. */
enum isx012_outfmt {
	ISX012_OUTFMT_YUV		= 0,
	ISX012_OUTFMT_RGB		= 0x04,
	ISX012_OUTFMT_JPEG 		= 0x08,
};

#define ISX012_OUTFMT_COUNT		3

/*
 * The values in this enum match the VIFMODE register values.
 * The amount of VIFMODE values matches the amount of OUTFMT values.
 */
enum isx012_vifmode {
	ISX012_VIFMODE_YUV_PARALLEL	= 0x02,
	ISX012_VIFMODE_RGB_PARALLEL	= 0x06,
	ISX012_VIFMODE_JPEG_PARALLEL	= 0x0A,
};

static const u32 isx012_mbus_formats[ISX012_OUTFMT_COUNT] = {
	MEDIA_BUS_FMT_UYVY8_2X8,	/* ISX012_OUTFMT_YUV */
	MEDIA_BUS_FMT_RGB565_2X8_LE,	/* ISX012_OUTFMT_RGB */
	MEDIA_BUS_FMT_JPEG_1X8,		/* ISX012_OUTFMT_JPEG */
};

static u32 isx012_get_format_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ISX012_OUTFMT_COUNT; i++)
		if (isx012_mbus_formats[i] == code)
			return isx012_mbus_formats[i];

	return isx012_mbus_formats[0];
}

static int isx012_mbin_to_outfmt(u32 mbin)
{
	switch (mbin) {
	case MEDIA_BUS_FMT_UYVY8_2X8:
		return ISX012_OUTFMT_YUV;
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
		return ISX012_OUTFMT_RGB;
	case MEDIA_BUS_FMT_JPEG_1X8:
		return ISX012_OUTFMT_JPEG;
	default:
		return -EINVAL;
	}
}

static int isx012_outfmt_to_vifmode(enum isx012_outfmt outfmt)
{
	switch (outfmt) {
		case ISX012_OUTFMT_YUV:
			return ISX012_VIFMODE_YUV_PARALLEL;
		case ISX012_OUTFMT_RGB:
			return ISX012_VIFMODE_RGB_PARALLEL;
		case ISX012_OUTFMT_JPEG:
			return ISX012_VIFMODE_JPEG_PARALLEL;
		default:
			return -EINVAL;
	}
}

struct isx012_mode_desc {
	u16 fpstype_addr;
	u16 outfmt_addr;
	u16 sensmode_addr;
	u16 hsize_addr;
	u16 vsize_addr;
};

/*
 * The ISX012 has 3 main operating modes:
 *
 *  - Monitoring (used for capturing video <= 30fps in YUV/RGB modes);
 *  - Capture (used for capturing still images);
 *  - Movie (used for capturing video in JPEG mode or >= 30fps).
 *
 * The currently selected mode is written to the MODESEL register.
 *
 * V4L2 has no concept of a "still photo" vs "video" mode; as such, we only use
 * the Capture mode for "high quality" stills, and Monitoring/Movie for regular
 * streaming.
 */

static const struct isx012_mode_desc isx012_mode_addrs[ISX012_MODE_MAX] = {
	[ISX012_MODE_MONITORING] = {
		.fpstype_addr	= ISX012_REG_FPSTYPE_MONI,
		.outfmt_addr	= ISX012_REG_OUTFMT_MONI,
		.sensmode_addr	= ISX012_REG_SENSMODE_MONI,
		.hsize_addr	= ISX012_REG_HSIZE_MONI,
		.vsize_addr	= ISX012_REG_VSIZE_MONI,
	},

	/* Half-relase is a variant of Monitoring mode */
	[ISX012_MODE_HALFRELEASE] = {
		.fpstype_addr	= ISX012_REG_FPSTYPE_MONI,
		.outfmt_addr	= ISX012_REG_OUTFMT_MONI,
		.sensmode_addr	= ISX012_REG_SENSMODE_MONI,
		.hsize_addr	= ISX012_REG_HSIZE_MONI,
		.vsize_addr	= ISX012_REG_VSIZE_MONI,
	},

	[ISX012_MODE_CAPTURE] = {
		.fpstype_addr	= ISX012_REG_FPSTYPE_CAP,
		.outfmt_addr	= ISX012_REG_OUTFMT_CAP,
		.sensmode_addr	= ISX012_REG_SENSMODE_CAP,
		.hsize_addr	= ISX012_REG_HSIZE_CAP,
		.vsize_addr	= ISX012_REG_VSIZE_CAP,
	},

	[ISX012_MODE_MOVIE] = {
		.fpstype_addr	= ISX012_REG_FPSTYPE_MOVIE,
		.outfmt_addr	= ISX012_REG_OUTFMT_MOVIE,
		.sensmode_addr	= ISX012_REG_SENSMODE_MOVIE,
		.hsize_addr	= ISX012_REG_HSIZE_MOVIE,
		.vsize_addr	= ISX012_REG_VSIZE_MOVIE,
	},
};

static const struct regmap_config isx012_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
};

static const char * const isx012_supply_name[] = {
	"avdd", /* Analog (2.8V) supply */
	"ovdd", /* Digital I/O (1.8V) supply */
	"dvdd", /* Digital Core (1.2V) supply */
};

#define ISX012_NUM_SUPPLIES 3

struct isx012 {
	struct device *dev;
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct regmap *regmap;
	struct regulator_bulk_data supplies[ISX012_NUM_SUPPLIES];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *standby_gpio;
	struct clk *clock;

	/* Hardware state cache */
	struct v4l2_mbus_framefmt format;
	enum isx012_mode mode;
	enum isx012_fpstype target_fps;
	u32 fmt_code;
};

static inline struct isx012 *subdev_to_isx012(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct isx012, subdev);
}

static int isx012_hw_clear_status(struct isx012 *isx012,
				  enum isx012_status_bit bit)
{
	int ret;

	ret = regmap_write(isx012->regmap, ISX012_REG_INTCLR0, bit);
	if (ret < 0) {
		dev_err(isx012->dev, "Failed to clear status bit 0x%u: %d\n",
			bit, ret);
		return ret;
	}

	return 0;
}

static int isx012_hw_wait_for_status(struct isx012 *isx012,
				     enum isx012_status_bit bit,
				     unsigned int on,
				     unsigned long wait,
				     u64 timeout)
{
	unsigned int val;
	int ret;

	ret = regmap_read_poll_timeout(isx012->regmap, ISX012_REG_INTCLR0, val,
				       ((val & bit) == (on ? bit : 0)),
				       wait, timeout);
	if (ret < 0) {
		dev_err(isx012->dev, "Status bit wait for 0x%u timed out: %d\n",
			bit, ret);
		return ret;
	}

	return 0;
}

/* Set up parameters for mode */
static int isx012_hw_set_mode_params(struct isx012 *isx012,
				     enum isx012_mode mode,
				     struct v4l2_mbus_framefmt *format,
				     u32 width, u32 height)
{
	enum isx012_outfmt outfmt = isx012_mbin_to_outfmt(format->code);
	enum isx012_sensmode sensmode;
	int ret;

	// TODO: allow other FPS values
	isx012->target_fps = ISX012_FPSTYPE_30FPS;

	sensmode = isx012_fpstype_to_sensmode(isx012->target_fps);

	/* Set FPSTYPE */
	ret = regmap_write(isx012->regmap,
			   isx012_mode_addrs[mode].fpstype_addr,
			   isx012->target_fps);
	if (ret < 0) {
		dev_err(isx012->dev, "Failed to set FPSTYPE: %d\n", ret);
		return ret;
	}

	/* Set SENSMODE */
	ret = regmap_write(isx012->regmap,
			   isx012_mode_addrs[mode].sensmode_addr,
			   isx012->target_fps);
	if (ret < 0) {
		dev_err(isx012->dev, "Failed to set SENSMODE: %d\n", ret);
		return ret;
	}

	/* Set OUTFMT */
	ret = regmap_write(isx012->regmap,
			   isx012_mode_addrs[mode].outfmt_addr,
			   outfmt);
	if (ret < 0) {
		dev_err(isx012->dev, "Failed to set OUTFMT: %d\n", ret);
		return ret;
	}

	/* Set HSIZE */
	ret = regmap_write(isx012->regmap,
			   isx012_mode_addrs[mode].hsize_addr,
			   width);
	if (ret < 0) {
		dev_err(isx012->dev, "Failed to set HSIZE: %d\n", ret);
		return ret;
	}

	/* Set VSIZE */
	ret = regmap_write(isx012->regmap,
			   isx012_mode_addrs[mode].vsize_addr,
			   height);
	if (ret < 0) {
		dev_err(isx012->dev, "Failed to set VSIZE: %d\n", ret);
		return ret;
	}

	return 0;
}

/* Set up mode, format, FPS. */
static int isx012_hw_set_format(struct isx012 *isx012,
				struct v4l2_mbus_framefmt *format)
{
	enum isx012_mode mode = ISX012_MODE_MOVIE;
	enum isx012_outfmt outfmt = isx012_mbin_to_outfmt(format->code);
	enum isx012_vifmode vifmode;
	int ret;

	if ((outfmt == ISX012_OUTFMT_YUV) || (outfmt == ISX012_OUTFMT_RGB))
		mode = ISX012_MODE_MONITORING;

	/* Set VIFMODE */
	vifmode = isx012_outfmt_to_vifmode(outfmt);
	if (vifmode == -EINVAL)
		return -EINVAL;

	ret = regmap_write(isx012->regmap, ISX012_REG_VIFMODE, vifmode);
	if (ret < 0) {
		dev_err(isx012->dev, "Failed to set VIFMODE: %d\n", ret);
		return ret;
	}

	/* Set mode selector */
	ret = isx012_hw_clear_status(isx012, ISX012_STS_CM_CHANGED);
	if (ret < 0)
		return ret;

	ret = regmap_write(isx012->regmap, ISX012_REG_MODESEL, mode);
	if (ret < 0) {
		dev_err(isx012->dev, "Failed to set mode: %d\n", ret);
		return ret;
	}

	ret = isx012_hw_wait_for_status(isx012, ISX012_STS_CM_CHANGED, 1,
					10, 100);
	if (ret < 0)
		return ret;

	isx012->mode = mode;

	return 0;
}

static int isx012_start_streaming(struct isx012 *isx012,
				  struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *format;
	int ret;

	format = v4l2_subdev_state_get_format(state, 0);

	ret = isx012_hw_set_mode_params(isx012, isx012->mode, format, 640, 480);
	if (ret)
		return ret;

	ret = isx012_hw_set_format(isx012, format);
	if (ret)
		return ret;

	/* Set STREAMMODE */
	ret = regmap_write(isx012->regmap, ISX012_REG_STREAMMODE,
			   ISX012_STREAMMODE_ON);
	if (ret < 0) {
		dev_err(isx012->dev, "Failed to set STREAMMODE: %d\n", ret);
		return ret;
	}

	return 0;
}

static int isx012_stop_streaming(struct isx012 *isx012,
				  struct v4l2_subdev_state *state)
{
	int ret;

	ret = regmap_write(isx012->regmap, ISX012_REG_STREAMMODE,
			   ISX012_STREAMMODE_OFF);
	if (ret < 0) {
		dev_err(isx012->dev, "Failed to set STREAMMODE: %d\n", ret);
		return ret;
	}

	return 0;
}

static int isx012_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct isx012 *isx012 = subdev_to_isx012(sd);
	struct v4l2_subdev_state *state;
	int ret;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable) {
		ret = pm_runtime_resume_and_get(isx012->dev);
		if (ret)
			goto error_unlock;

		ret = isx012_start_streaming(isx012, state);
		if (ret)
			goto error_power_off;
	} else {
		ret = isx012_stop_streaming(isx012, state);
		pm_runtime_put(isx012->dev);
	}

	v4l2_subdev_unlock_state(state);

	return 0;

error_power_off:
	v4l2_subdev_unlock_state(state);
	pm_runtime_put(isx012->dev);
error_unlock:
	//mutex_unlock(&isx012->mutex);

	return ret;
}

static int isx012_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	dev_info(sd->dev, "isx012_enum_mbus_code called\n");

	if (code->index >= ISX012_OUTFMT_COUNT)
		return -EINVAL;

	code->code = isx012_mbus_formats[code->index];

	return 0;
}

static void isx012_update_pad_fmt(struct v4l2_mbus_framefmt *fmt,
				  u32 width, u32 height)
{
	fmt->code = isx012_get_format_code(fmt->code);
	fmt->width = width;
	fmt->height = height;
	//fmt->field = V4L2_FIELD_NONE;
	if (fmt->code == MEDIA_BUS_FMT_JPEG_1X8)
		fmt->colorspace = V4L2_COLORSPACE_JPEG;
	else
		fmt->colorspace = V4L2_COLORSPACE_RAW;
}

static struct v4l2_mbus_framefmt *__isx012_get_format(struct isx012 *isx012,
						    struct v4l2_subdev_state *state,
						    struct v4l2_subdev_format *fmt)
{
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return state ? v4l2_subdev_state_get_format(state, fmt->pad) : NULL;

	return &isx012->format;
}

static int isx012_set_pad_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *mf;
	struct isx012 *isx012 = subdev_to_isx012(sd);
	dev_info(sd->dev, "isx012_set_pad_fmt called\n");

	isx012_update_pad_fmt(&fmt->format, 640, 480);

	mf = __isx012_get_format(isx012, state, fmt);
	if (mf) {
		*mf = fmt->format;
	}
	return 0;
}

static int isx012_get_pad_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *mf;
	struct isx012 *isx012 = subdev_to_isx012(sd);
	dev_info(sd->dev, "isx012_get_pad_fmt called\n");

	mf = __isx012_get_format(isx012, state, fmt);
	fmt->format = *mf;

	return 0;
}

static int isx012_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	dev_info(sd->dev, "isx012_get_selection called\n");
	// TODO
	sel->r.top = 0;
	sel->r.left = 0;
	sel->r.width = 640; //ISX012_PIXEL_ARRAY_WIDTH;
	sel->r.height = 480; //ISX012_PIXEL_ARRAY_HEIGHT;

	return 0;
}

static int isx012_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	dev_info(sd->dev, "isx012_enum_frame_size called\n");
	fse->min_width = 640;
	fse->max_width = fse->min_width;
	fse->min_height = 480;
	fse->max_height = fse->min_height;

	return 0;
}

static const struct v4l2_subdev_video_ops isx012_video_ops = {
	.s_stream = isx012_set_stream,
};

static const struct v4l2_subdev_pad_ops isx012_pad_ops = {

	.enum_mbus_code	= isx012_enum_mbus_code,
	.get_fmt	= isx012_get_pad_fmt,
	.set_fmt	= isx012_set_pad_fmt,
	.get_selection	= isx012_get_selection,
	.set_selection	= isx012_get_selection,
	.enum_frame_size = isx012_enum_frame_size,
};

static const struct v4l2_subdev_ops isx012_subdev_ops = {
	.video = &isx012_video_ops,
	.pad = &isx012_pad_ops,
};

static int isx012_power_on(struct device *dev)
{
	struct v4l2_subdev *subdev = dev_get_drvdata(dev);
	struct isx012 *isx012 = subdev_to_isx012(subdev);
	int ret;

	ret = regulator_bulk_enable(ISX012_NUM_SUPPLIES, isx012->supplies);
	if (ret) {
		dev_err(isx012->dev, "Failed to enable supplies (%d)\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(isx012->reset_gpio, 0);
	// TODO: handle non-standby devices (see spresense driver)
	gpiod_set_value_cansleep(isx012->standby_gpio, 0);

	return 0;
};

static int isx012_power_off(struct device *dev)
{
	struct v4l2_subdev *subdev = dev_get_drvdata(dev);
	struct isx012 *isx012 = subdev_to_isx012(subdev);
	//int ret;

	//ret = regulator_bulk_disable(ISX012_NUM_SUPPLIES, isx012->supplies);
	//if (ret)
	//	dev_warn(isx012->dev, "Failed to disable supplies (%d)\n", ret);

	gpiod_set_value_cansleep(isx012->reset_gpio, 1);
	gpiod_set_value_cansleep(isx012->standby_gpio, 0);

	return 0;
}

static int isx012_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct isx012 *isx012;
	unsigned int i;
	int ret;

	isx012 = devm_kzalloc(dev, sizeof(*isx012), GFP_KERNEL);
	if (!isx012)
		return -ENOMEM;

	isx012->dev = dev;

	isx012->regmap = devm_regmap_init_i2c(client, &isx012_regmap_config);
	if (IS_ERR(isx012->regmap))
		return dev_err_probe(dev, PTR_ERR(isx012->regmap),
				     "Failed to register regmap\n");

	/* Get regulators */
	for (i = 0; i < ARRAY_SIZE(isx012_supply_name); i++)
			isx012->supplies[i].supply = isx012_supply_name[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(isx012_supply_name),
				      isx012->supplies);
	if (ret) {
		return dev_err_probe(dev, ret, "Failed to get regulators\n");
	}

	/* Get clock */
	isx012->clock = devm_clk_get(dev, NULL);
	if (IS_ERR(isx012->clock))
		return dev_err_probe(dev, PTR_ERR(isx012->clock),
				     "Failed to get clock\n");

	/* Get GPIOs */
	isx012->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(isx012->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(isx012->reset_gpio),
				     "Failed to get reset GPIO\n");

	isx012->standby_gpio = devm_gpiod_get(dev, "standby", GPIOD_OUT_HIGH);
	if (IS_ERR(isx012->standby_gpio))
		return dev_err_probe(dev, PTR_ERR(isx012->standby_gpio),
				     "Failed to get standby GPIO\n");

	/* Initialize V4L2 subdev */
	v4l2_i2c_subdev_init(&isx012->subdev, client, &isx012_subdev_ops);
	isx012->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	isx012->subdev.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	isx012->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&isx012->subdev.entity, 1, &isx012->pad);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to init media pad\n");

	ret = v4l2_async_register_subdev_sensor(&isx012->subdev);
	if (ret < 0) {
		return dev_err_probe(dev, ret,
				     "failed to register sensor sub-device\n");
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	dev_info(dev, "isx012 probe finished\n");

	return 0;
}

static void isx012_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		isx012_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct dev_pm_ops isx012_pm_ops = {
	SET_RUNTIME_PM_OPS(isx012_power_off, isx012_power_on, NULL)
};


static const struct i2c_device_id isx012_ids[] = {
	{ }
};
MODULE_DEVICE_TABLE(i2c, isx012_ids);

#ifdef CONFIG_OF
static const struct of_device_id isx012_of_match[] = {
	{ .compatible = "sony,isx012" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, isx012_of_match);
#endif

static struct i2c_driver isx012_driver = {
	.driver = {
		.of_match_table	= of_match_ptr(isx012_of_match),
		.pm		= &isx012_pm_ops,
		.name		= ISX012_DRIVER_NAME,
	},
	.probe		= isx012_probe,
	.remove		= isx012_remove,
	.id_table	= isx012_ids,
};

module_i2c_driver(isx012_driver);

MODULE_DESCRIPTION("Sony ISX012 image sensor driver");
MODULE_AUTHOR("Artur Weber <aweber.kernel@gmail.com>");
MODULE_LICENSE("GPL v2");
