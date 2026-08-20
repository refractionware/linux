// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung S6D7AA0 MIPI-DSI TFT LCD controller drm_panel driver.
 *
 * Copyright (C) 2022 Artur Weber <aweber.kernel@gmail.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>

#include <video/mipi_display.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct orise_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];
	const struct orise_panel_desc *desc;
};

struct orise_panel_desc {
	unsigned int panel_type;
	void (*init_func)(struct orise_panel *ctx, struct mipi_dsi_multi_context *dsi_ctx);
	void (*off_func)(struct mipi_dsi_multi_context *dsi_ctx);
	const struct drm_display_mode *drm_mode;
};

enum orise_panel_panels {
	ORISE_PANEL_CMI_V3 = 0x48
};

static inline struct orise_panel *panel_to_orise_panel(struct drm_panel *panel)
{
	return container_of(panel, struct orise_panel, panel);
}

static void orise_panel_reset(struct orise_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(50);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(50);
}

static int orise_panel_on(struct orise_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	ctx->desc->init_func(ctx, &dsi_ctx);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static void orise_panel_off(struct orise_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	ctx->desc->off_func(&dsi_ctx);

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 64);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 120);
}

static int orise_panel_prepare(struct drm_panel *panel)
{
	struct orise_panel *ctx = panel_to_orise_panel(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	orise_panel_reset(ctx);

	ret = orise_panel_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int orise_panel_disable(struct drm_panel *panel)
{
	struct orise_panel *ctx = panel_to_orise_panel(panel);

	orise_panel_off(ctx);

	return 0;
}

static int orise_panel_unprepare(struct drm_panel *panel)
{
	struct orise_panel *ctx = panel_to_orise_panel(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

/* Initialization code and structures for CMI V3 (ID 0x48) panel */

static void orise_panel_cmi_v3_init(struct orise_panel *ctx, struct mipi_dsi_multi_context *dsi_ctx)
{
	/* shift_addr00 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0x00);

	/* x47_cmi_extc LWRITE */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xFF, 0x80, 0x12, 0x01);

	/* shift_addr80 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0x80);

	/* x47_cmi_cmd2 LWRITE */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xFF, 0x80, 0x12);

	/* shift_addr80 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0x92);

	/* x47_set_A_data_latch_to_0x08 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xC4, 0x08);

	/* shift_addrA1 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0x92);

	/* x47_set_B_source_gate */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xB3, 0x00);

	mipi_dsi_dcs_exit_sleep_mode_multi(dsi_ctx);

	mipi_dsi_msleep(dsi_ctx, 110);

	/* shift_addrB2 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0xb2);

	/* x47_set_D_improve_tear */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xC0, 0x30);

	/* shift_addrA6 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0xA6);

	/* x47_set_E_improve_tear LWRITE */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xC1, 0x82, 0x00);

	/* shift_addrB0 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0xB0);

	/* x47_set_F_improve_tear */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xB3, 0x18);

	/* shift_addr80 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0x80);

	/* x47_osc_ref_80 LWRITE */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xC1, 0x25, 0x77);

	/* shift_addr83 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0x83);

	/* x47_osc_ref_83 comment says LWRITE but code does WRITE1, bug? */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xC1, 0x50);

	/* shift_addr00 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x00, 0x00);

	mipi_dsi_dcs_set_display_on_multi(dsi_ctx);
	mipi_dsi_msleep(dsi_ctx, 10);
}

static void orise_panel_cmi_v3_off(struct mipi_dsi_multi_context *dsi_ctx)
{
	return;
}

static const struct drm_display_mode orise_panel_mode = {
    .clock = (480 + 46 + 4 + 44) * (854 + 15 + 1 + 16) * 60 / 1000,
    .hdisplay = 480,
    .hsync_start = 480 + 46,
    .hsync_end = 480 + 46 + 4,
    .htotal = 480 + 46 + 4 + 44,
    .vdisplay = 854,
    .vsync_start = 854 + 15,
    .vsync_end = 854 + 15 + 1,
    .vtotal = 854 + 15 + 1 + 16,
    .width_mm = 50,
    .height_mm = 89,
    .type = DRM_MODE_TYPE_DRIVER,
};

static const struct orise_panel_desc orise_panel_cmi_v3_desc = {
	.panel_type = ORISE_PANEL_CMI_V3,
	.init_func = orise_panel_cmi_v3_init,
	.off_func = orise_panel_cmi_v3_off,
	.drm_mode = &orise_panel_mode,
};

static int orise_panel_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct orise_panel *ctx;

	ctx = container_of(panel, struct orise_panel, panel);
	if (!ctx)
		return -EINVAL;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->drm_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs orise_panel_funcs = {
	.disable = orise_panel_disable,
	.prepare = orise_panel_prepare,
	.unprepare = orise_panel_unprepare,
	.get_modes = orise_panel_get_modes,
};

static int orise_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct orise_panel *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct orise_panel, panel,
				   &orise_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->desc = of_device_get_match_data(dev);
	if (!ctx->desc)
		return -ENODEV;

	ctx->supplies[0].supply = "power";
	ctx->supplies[1].supply = "vmipi";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
					      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_VIDEO_HSE;

	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void orise_panel_remove(struct mipi_dsi_device *dsi)
{
	struct orise_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id orise_panel_of_match[] = {
	{
		.compatible = "sony,nicki-orise-cmi-v3",
		.data = &orise_panel_cmi_v3_desc
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, orise_panel_of_match);

static struct mipi_dsi_driver orise_panel_driver = {
	.probe = orise_panel_probe,
	.remove = orise_panel_remove,
	.driver = {
		.name = "panel-orise",
		.of_match_table = orise_panel_of_match,
	},
};
module_mipi_dsi_driver(orise_panel_driver);

MODULE_AUTHOR("Artur Weber <aweber.kernel@gmail.com>");
MODULE_DESCRIPTION("Orise MIPI-DSI LCD controller driver");
MODULE_LICENSE("GPL");
