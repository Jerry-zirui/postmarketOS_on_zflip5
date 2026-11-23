// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S6E3FAC (AMF670BS03) 6.7" 1080x2640 @ 120Hz Command-Mode DSC 1.1 AMOLED Panel
 * for Samsung Galaxy Z Flip 5 (SM-F7310 / samsung-b5q).
 *
 * DCS initialization, VRR 120Hz, DSC 1.1 PPS, 12-bit brightness, and sleep sequences
 * extracted directly from stock SM-F7310 /vendor/firmware/B5_S6E3FAC_AMF670BS03.dat.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct s6e3fac_b5q {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
	bool prepared;
};

static const struct regulator_bulk_data s6e3fac_supplies[] = {
	{ .supply = "vdd3" },
	{ .supply = "vci" },
	{ .supply = "vddr" },
};

static inline struct s6e3fac_b5q *to_s6e3fac_b5q(struct drm_panel *panel)
{
	return container_of(panel, struct s6e3fac_b5q, panel);
}

/* Exact stock DSC 1.1 128-byte PPS payload from B5_S6E3FAC_AMF670BS03.dat (1080x2640, 2x 540x40, 8bpc/8bpp) */
static const u8 s6e3fac_b5q_pps[128] = {
	0x11, 0x00, 0x00, 0x89, 0x30, 0x80, 0x0A, 0x50,
	0x04, 0x38, 0x00, 0x28, 0x02, 0x1C, 0x02, 0x1C,
	0x02, 0x00, 0x02, 0x0E, 0x00, 0x20, 0x03, 0xDD,
	0x00, 0x07, 0x00, 0x0C, 0x02, 0x77, 0x02, 0x8B,
	0x18, 0x00, 0x10, 0xF0, 0x03, 0x0C, 0x20, 0x00,
	0x06, 0x0B, 0x0B, 0x33, 0x0E, 0x1C, 0x2A, 0x38,
	0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7B,
	0x7D, 0x7E, 0x01, 0x02, 0x01, 0x00, 0x09, 0x40,
	0x09, 0xBE, 0x19, 0xFC, 0x19, 0xFA, 0x19, 0xF8,
	0x1A, 0x38, 0x1A, 0x78, 0x1A, 0xB6, 0x2A, 0xF6,
	0x2B, 0x34, 0x2B, 0x74, 0x3B, 0x74, 0x6B, 0xF4,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void s6e3fac_b5q_reset(struct s6e3fac_b5q *ctx)
{
	/* Match qcom,mdss-dsi-reset-sequence = <1 10> (active-low GPIO 125) */
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int s6e3fac_b5q_on(struct s6e3fac_b5q *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	struct drm_dsc_picture_parameter_set pps;
	u16 br = ctx->panel.backlight ? ctx->panel.backlight->props.brightness : 1024;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	/* 1. Sleep Out (0x11) + 10ms */
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 10);

	/* 2. SSR_SETTING */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0xA5, 0xA5);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0xAE, 0xF6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF6, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0xAC, 0xF6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF6, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x09, 0xF6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF6, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x0F, 0xF6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF6, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF4, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF7, 0x0F);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0xA5, 0xA5);
	mipi_dsi_msleep(&dsi_ctx, 110);

	/* 3. DSC_SETTING (Compression Mode ON + exact 128-byte PPS) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0x5A, 0x5A);

	mipi_dsi_compression_mode_multi(&dsi_ctx, true);
	memcpy(&pps, s6e3fac_b5q_pps, sizeof(pps));
	mipi_dsi_picture_parameter_set_multi(&dsi_ctx, &pps);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0xA5, 0xA5);

	/* 4. TE_ON */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE7, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0xA5, 0xA5);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0x5A, 0x5A);

	/* 5. ERR_FG_SETTING */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE5, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x3D, 0xF4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF4, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xED, 0x00, 0x40, 0x42, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0xA5, 0xA5);

	/* 6. PAGE_ADDRESS_SET (0..1079 x 0..2639) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0xA5, 0xA5);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0, 1080 - 1);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0, 2640 - 1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0x5A, 0x5A);

	/* 7. FFC_SET_0 (951 Mbps default) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x36, 0xC5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC5, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x3E, 0xC5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC5, 0x40, 0xF1, 0x3F, 0x99);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x56, 0xC5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC5, 0x40, 0xF1, 0x3F, 0x99);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0xA5, 0xA5);

	/* 8. ECC_SETTING */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x02, 0xF8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0xA5, 0xA5);

	/* 9. TSP_SYNC_ENABLE (120Hz) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB9, 0x61, 0x00, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x0C, 0xB9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB9, 0x05, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x28, 0xB9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB9, 0x59, 0x29);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0xA5, 0xA5);

	/* 10. VRR_SETTING (120Hz HS, LFDMAX=120, LFDMIN=120, RevC) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBD, 0xE1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0xAD, 0xBD);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBD, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF7, 0x0F);
	mipi_dsi_msleep(&dsi_ctx, 17);

	/* VFP = 0x0020 */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x08, 0xF2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF2, 0x00, 0x20);

	/* Rev C..Z timing table */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x9F, 0x98);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x98,
				     0x09, 0xA8, 0x09, 0xA8, 0x01, 0xAD,
				     0x01, 0xAD, 0x01, 0xAD, 0x01, 0xAD);

	/* FREQ 120Hz */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x0A, 0xBD);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBD, 0x40);

	/* LFD MAX = 120Hz (0x0000) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x1F, 0xBD);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBD, 0x00, 0x00);

	/* VRR TH */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x07, 0xBD);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBD, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x11, 0xBD);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBD, 0x10);

	/* LFD MIN = 120Hz (0x0000) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x14, 0xBD);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBD, 0x00, 0x00);

	/* Frame insertion (LFDMIN 120) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x7B, 0xBD);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBD,
				     0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01,
				     0x00, 0x01, 0x02, 0x00, 0x00, 0x00);

	/* TE_FRAME_SEL (120Hz) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB9, 0x61, 0x00, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x0C, 0xB9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB9, 0x05, 0x28, 0x00, 0x00);

	/* GLUT Offset off, Manual AOR off, Smooth Dim on */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x85, 0x93);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x93, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x4E, 0x98);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x98, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x0E, 0x94);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x0F);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF7, 0x0F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBD, 0xE3, 0x21);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0x5A, 0x5A);

	/* 11. DIA_SETTING (off), SMOOTH_DIMMING 1FRAME, VDDR_CLK_GATING, SANDSTORM */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x01, 0x95, 0x92);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x0E, 0x94);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF7, 0x0F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x19, 0xA3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xA3, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x33, 0xF2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF2, 0xF0);

	/* 12. Initial Brightness (12-bit 0x51 big-endian) */
	{
		u8 br_cmd[3] = { 0x51, (br >> 8) & 0x0F, br & 0xFF };

		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x0D, 0x94);
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x70);
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x28);
		mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, br_cmd, sizeof(br_cmd));
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF7, 0x0F);
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0xA5, 0xA5);
	}

	/* 13. AVC_MODE_SETTING */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x13, 0xEF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xEF, 0xA2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0xA5, 0xA5);

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	return dsi_ctx.accum_err;
}

static int s6e3fac_b5q_prepare(struct drm_panel *panel)
{
	struct s6e3fac_b5q *ctx = to_s6e3fac_b5q(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s6e3fac_supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	usleep_range(12000, 14000);
	s6e3fac_b5q_reset(ctx);

	ret = s6e3fac_b5q_on(ctx);
	if (ret < 0) {
		dev_err(&ctx->dsi->dev, "failed to initialize S6E3FAC panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(s6e3fac_supplies), ctx->supplies);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int s6e3fac_b5q_enable(struct drm_panel *panel)
{
	struct s6e3fac_b5q *ctx = to_s6e3fac_b5q(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	/* POWER_ON_POST_SETTING: Display On (0x29) + 20ms + smooth dimming */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF1, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0x5A, 0x5A);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB0, 0x00, 0x0E, 0x94);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x0F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF7, 0x0F);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF1, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0xA5, 0xA5);

	return dsi_ctx.accum_err;
}

static int s6e3fac_b5q_disable(struct drm_panel *panel)
{
	struct s6e3fac_b5q *ctx = to_s6e3fac_b5q(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	/* POWER_OFF_SETTING: Display Off (0x28) + Sleep In (0x10) + 120ms */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF7, 0x0F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0xA5, 0xA5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0x5A, 0x5A);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int s6e3fac_b5q_unprepare(struct drm_panel *panel)
{
	struct s6e3fac_b5q *ctx = to_s6e3fac_b5q(panel);

	ctx->prepared = false;
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(2000, 3000);

	return regulator_bulk_disable(ARRAY_SIZE(s6e3fac_supplies), ctx->supplies);
}

/*
 * Native 1080x2640 @ 120Hz (matches fhd120hs in SM-F7310 fdt.dts).
 * Horizontal porches are tuned so msm_dsi's compressed pclk calculation
 * yields ~951 Mbps bit clock, matching qcom,mdss-dsi-panel-clockrate
 * (951000000 Hz) and FFC_SET_0 (951 Mbps).
 */
static const struct drm_display_mode s6e3fac_b5q_mode = {
	.clock = (1080 + 42 + 42 + 41) * (2640 + 24 + 28 + 32) * 120 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 42,
	.hsync_end = 1080 + 42 + 42,
	.htotal = 1080 + 42 + 42 + 41,
	.vdisplay = 2640,
	.vsync_start = 2640 + 24,
	.vsync_end = 2640 + 24 + 28,
	.vtotal = 2640 + 24 + 28 + 32,
	.width_mm = 64,
	.height_mm = 158,
};

static int s6e3fac_b5q_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &s6e3fac_b5q_mode);
}

static const struct drm_panel_funcs s6e3fac_b5q_funcs = {
	.prepare = s6e3fac_b5q_prepare,
	.enable = s6e3fac_b5q_enable,
	.disable = s6e3fac_b5q_disable,
	.unprepare = s6e3fac_b5q_unprepare,
	.get_modes = s6e3fac_b5q_get_modes,
};

static int s6e3fac_b5q_bl_update_status(struct backlight_device *bl)
{
	struct s6e3fac_b5q *ctx = bl_get_data(bl);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	u16 brightness = backlight_get_brightness(bl);
	u8 br_cmd[3] = { 0x51, (brightness >> 8) & 0x0F, brightness & 0xFF };

	if (!ctx->prepared)
		return 0;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x5A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x28);
	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, br_cmd, sizeof(br_cmd));
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF7, 0x0F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0xA5, 0xA5);

	return dsi_ctx.accum_err;
}

static const struct backlight_ops s6e3fac_b5q_bl_ops = {
	.update_status = s6e3fac_b5q_bl_update_status,
};

static int s6e3fac_b5q_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e3fac_b5q *ctx;
	const struct backlight_properties bl_props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1024,
		.max_brightness = 2047,
	};
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct s6e3fac_b5q, panel,
				   &s6e3fac_b5q_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(s6e3fac_supplies),
					    s6e3fac_supplies, &ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get panel regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	/* DSC 1.1: 1080x2640, 2 slices/line (540x40), 8bpc -> 8bpp (3:1) */
	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;
	ctx->dsc.slice_height = 40;
	ctx->dsc.slice_width = 540;
	ctx->dsc.slice_count = 2;
	ctx->dsc.bits_per_component = 8;
	ctx->dsc.bits_per_pixel = 8 << 4;
	ctx->dsc.block_pred_enable = true;
	dsi->dsc = &ctx->dsc;

	ctx->panel.prepare_prev_first = true;
	ctx->panel.backlight = devm_backlight_device_register(dev, dev_name(dev),
							      dev, ctx,
							      &s6e3fac_b5q_bl_ops,
							      &bl_props);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "failed to register backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	dev_info(dev, "Samsung S6E3FAC (AMF670BS03) 1080x2640@120Hz DSC panel probed\n");
	return 0;
}

static void s6e3fac_b5q_remove(struct mipi_dsi_device *dsi)
{
	struct s6e3fac_b5q *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6e3fac_b5q_of_match[] = {
	{ .compatible = "samsung,b5q-s6e3fac" },
	{ }
};
MODULE_DEVICE_TABLE(of, s6e3fac_b5q_of_match);

static struct mipi_dsi_driver s6e3fac_b5q_driver = {
	.probe = s6e3fac_b5q_probe,
	.remove = s6e3fac_b5q_remove,
	.driver = {
		.name = "panel-samsung-s6e3fac-b5q",
		.of_match_table = s6e3fac_b5q_of_match,
	},
};
module_mipi_dsi_driver(s6e3fac_b5q_driver);

MODULE_DESCRIPTION("Samsung S6E3FAC (AMF670BS03) 120Hz DSC AMOLED DRM Panel Driver (SM-F7310)");
MODULE_LICENSE("GPL");
