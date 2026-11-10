// SPDX-License-Identifier: GPL-2.0-only
/*
 * timelm: LG panel features of the sw43103 (LG V60) on the msm-5.15 DSI
 * driver.  Port of the stock 4.19 techpack/display/msm/lge layer, reduced to
 * what this panel uses: cm/lge_color_manager.c, the irc_brighter and fp_lhbm
 * parts of brightness/lge_brightness.c, lge_ddic_ops_sw43103.c behind them,
 * and the /sys/class/panel nodes from lge_dsi_panel_create_sysfs().
 * The command sets are the panel-level lge,* properties of the panel DT node.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <video/mipi_display.h>

#include "dsi_panel.h"
#include "dsi_parser.h"
#include "timelm_panel_lge.h"

/*
 * timelm: LG fingerprint local HBM (the green circle under the UDFPS sensor).
 * Port of stock 4.19 lge_brightness.c fp_lhbm_store() and
 * lge_ddic_ops_sw43103.c lge_set_fp_lhbm_sw43103() without the AOD<->FPS
 * switch.  The DCS payloads are the panel-level lge,mdss-dsi-fp-lhbm-*
 * command sets; the HAL writes /sys/class/panel/brightness/fp_lhbm.
 */
enum timelm_lhbm_state {	/* stock enum fp_lhbm_state */
	TIMELM_LHBM_OFF = 0,
	TIMELM_LHBM_ON,
	TIMELM_LHBM_READY,
	TIMELM_LHBM_EXIT,
	TIMELM_LHBM_SM_OFF = 10,
	TIMELM_LHBM_SM_ON,
	TIMELM_LHBM_FORCED_OFF = 20,
	TIMELM_LHBM_FORCED_ON,
	TIMELM_LHBM_FORCED_READY,
	TIMELM_LHBM_FORCED_EXIT,
};

enum {
	TIMELM_LHBM_CMD_READY,
	TIMELM_LHBM_CMD_EXIT,
	TIMELM_LHBM_CMD_ON,
	TIMELM_LHBM_CMD_OFF,
	TIMELM_LHBM_CMD_MAX,
};

static struct {
	struct dsi_panel *panel;
	struct dsi_panel_cmd_set cmds[TIMELM_LHBM_CMD_MAX];
	int mode;	/* last request (stock fp_lhbm_mode) */
	int old_mode;	/* last applied (stock old_fp_lhbm_mode) */
	bool ready;	/* stock lhbm_ready_enable */
	bool pending;	/* stock need_fp_lhbm_set */
} timelm_lhbm;

static bool timelm_lhbm_is_on(int mode)
{
	return mode == TIMELM_LHBM_ON || mode == TIMELM_LHBM_SM_ON ||
		mode == TIMELM_LHBM_FORCED_ON;
}

static bool timelm_lhbm_is_off(int mode)
{
	return mode == TIMELM_LHBM_OFF || mode == TIMELM_LHBM_SM_OFF ||
		mode == TIMELM_LHBM_FORCED_OFF;
}

/* stock dsi_panel_set_backlight(): DBV stays at 0x6A9 while LHBM is lit */
bool timelm_lhbm_blocks_bl(struct dsi_panel *panel)
{
	if (timelm_lhbm.panel != panel)
		return false;

	if (timelm_lhbm.mode == TIMELM_LHBM_READY ||
	    timelm_lhbm_is_on(timelm_lhbm.mode))
		return true;

	return timelm_lhbm_is_off(timelm_lhbm.mode) && timelm_lhbm.ready;
}

/* timelm: a panel-level "lge,..." command set from the panel DT node */
struct timelm_cmd_prop {
	const char *cmd;
	const char *state;
};

static int timelm_parse_cmd(struct dsi_panel_cmd_set *cmd,
			    const struct timelm_cmd_prop *prop,
			    struct dsi_parser_utils *utils)
{
	const char *data, *state;
	u32 length = 0, packet_count = 0;
	int rc;

	data = utils->get_property(utils->data, prop->cmd, &length);
	if (!data)
		return -ENOTSUPP;

	rc = dsi_panel_get_cmd_pkt_count(data, length, &packet_count);
	if (rc)
		return rc;

	rc = dsi_panel_alloc_cmd_packets(cmd, packet_count);
	if (rc)
		return rc;

	rc = dsi_panel_create_cmd_packets(data, length, packet_count,
					  cmd->cmds);
	if (rc) {
		kfree(cmd->cmds);
		cmd->cmds = NULL;
		cmd->count = 0;
		return rc;
	}

	state = utils->get_property(utils->data, prop->state, NULL);
	cmd->state = (state && !strcmp(state, "dsi_hs_mode")) ?
		DSI_CMD_SET_STATE_HS : DSI_CMD_SET_STATE_LP;
	return 0;
}

static void timelm_free_cmds(struct dsi_panel_cmd_set *cmds, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		dsi_panel_destroy_cmd_packets(&cmds[i]);
		dsi_panel_dealloc_cmd_packets(&cmds[i]);
		cmds[i].cmds = NULL;
		cmds[i].count = 0;
	}
}

static int timelm_parse_cmds(struct dsi_panel_cmd_set *cmds,
			     const struct timelm_cmd_prop *props, int n,
			     struct dsi_parser_utils *utils)
{
	int i;

	for (i = 0; i < n; i++) {
		if (timelm_parse_cmd(&cmds[i], &props[i], utils)) {
			DSI_ERR("timelm: %s missing\n", props[i].cmd);
			timelm_free_cmds(cmds, i);
			return -EINVAL;
		}
	}
	return 0;
}

/* dsi_panel_tx_cmd_set() for a set outside the per-mode cmd_sets[] */
static int timelm_tx(struct dsi_panel *panel, struct dsi_panel_cmd_set *set,
		     const char *name)
{
	struct dsi_cmd_desc *cmds = set->cmds;
	ssize_t len;
	int i;

	for (i = 0; i < set->count; i++, cmds++) {
		cmds->ctrl_flags = 0;
		if (set->state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		len = dsi_host_transfer_sub(panel->host, cmds);
		if (len < 0) {
			DSI_ERR("timelm: %s failed, rc=%zd\n", name, len);
			return len;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms * 1000,
				     (cmds->post_wait_ms * 1000) + 10);
	}

	return 0;
}

/* payload of packet @pos of a parsed set (stock get_payload_addr()) */
static u8 *timelm_payload(struct dsi_panel_cmd_set *set, int pos, int len)
{
	if (pos >= set->count || set->cmds[pos].msg.tx_len < len)
		return NULL;
	return (u8 *)set->cmds[pos].msg.tx_buf;
}

/*
 * timelm: LG colour manager / image quality for the sw43103 panel.
 * Port of stock 4.19 cm/lge_color_manager.c (screen_mode, screen_tune,
 * rgb_tune, hdr_mode, daylight_mode, video_enhancement),
 * brightness/lge_brightness.c irc_brighter_store() and the
 * lge_ddic_ops_sw43103.c ops behind them.  The sysfs nodes are at the end of
 * this file.  Everything ending in _locked runs
 * with panel->panel_lock held.  Unlike stock, a request made while the panel
 * is off is kept and applied by timelm_cm_reapply_locked() in
 * dsi_panel_enable(), as the panel on-command resets the DDIC to natural.
 */
enum {	/* stock screen_mode_oled_* */
	TIMELM_SCREEN_NATURAL = 0,
	TIMELM_SCREEN_CINEMA = 1,
	TIMELM_SCREEN_VIVID = 4,
	TIMELM_SCREEN_CUSTOM = 10,
};

enum {
	TIMELM_CM_NATURAL,
	TIMELM_CM_VIVID,
	TIMELM_CM_CINEMA,
	TIMELM_CM_SAT,
	TIMELM_CM_HUE,
	TIMELM_CM_SHA,
	TIMELM_CM_SAT_DEFAULT,
	TIMELM_CM_HUE_DEFAULT,
	TIMELM_CM_WB_DEFAULT,
	TIMELM_CM_DG,
	TIMELM_CM_DISP_CTRL,
	TIMELM_CM_IRC,
	TIMELM_CM_ACE_TUNE,
	TIMELM_CM_ACE_RESTORE,
	TIMELM_CM_VE_ON,
	TIMELM_CM_VE_OFF,
	TIMELM_CM_DAYLIGHT_ON,
	TIMELM_CM_DAYLIGHT_OFF,
	TIMELM_CM_MAX,
};

/* stock lge_ddic_cmd_set_prop_map / _state_map entries */
static const struct timelm_cmd_prop timelm_cm_props[TIMELM_CM_MAX] = {
	[TIMELM_CM_NATURAL] = { "lge,mdss-dsi-cm-natural",
		"lge,mdss-dsi-cm-natural-state" },
	[TIMELM_CM_VIVID] = { "lge,mdss-dsi-cm-vivid",
		"lge,mdss-dsi-cm-vivid-state" },
	[TIMELM_CM_CINEMA] = { "lge,mdss-dsi-cm-cinema",
		"lge,mdss-dsi-cm-cinema-state" },
	[TIMELM_CM_SAT] = { "lge,mdss-dsi-saturation-command",
		"lge,mdss-dsi-saturation-command-state" },
	[TIMELM_CM_HUE] = { "lge,mdss-dsi-hue-command",
		"lge,mdss-dsi-hue-command-state" },
	[TIMELM_CM_SHA] = { "lge,mdss-dsi-sharpness-command",
		"lge,mdss-dsi-sharpness-command-state" },
	/* the _DEFAULT copies keep the DT payload; the ones above get patched */
	[TIMELM_CM_SAT_DEFAULT] = { "lge,mdss-dsi-saturation-command",
		"lge,mdss-dsi-saturation-command-state" },
	[TIMELM_CM_HUE_DEFAULT] = { "lge,mdss-dsi-hue-command",
		"lge,mdss-dsi-hue-command-state" },
	[TIMELM_CM_WB_DEFAULT] = { "lge,mdss-dsi-wb-default-command",
		"lge,mdss-dsi-wb-default-command-state" },
	[TIMELM_CM_DG] = { "lge,digital-gamma-cmds-dummy",
		"lge,digital-gamma-cmds-dummy-state" },
	[TIMELM_CM_DISP_CTRL] = { "lge,mdss-dsi-disp-ctrl-command-1",
		"lge,mdss-dsi-disp-ctrl-command-1-state" },
	[TIMELM_CM_IRC] = { "lge,mdss-dsi-irc-command",
		"lge,mdss-dsi-irc-command-state" },
	[TIMELM_CM_ACE_TUNE] = { "lge,mdss-dsi-ace-tune-command",
		"lge,mdss-dsi-ace-command-state" },
	[TIMELM_CM_ACE_RESTORE] = { "lge,mdss-dsi-ace-restore-command",
		"lge,mdss-dsi-ace-command-state" },
	[TIMELM_CM_VE_ON] = { "lge,mdss-dsi-ve-on-command",
		"lge,mdss-dsi-ve-on-command-state" },
	[TIMELM_CM_VE_OFF] = { "lge,mdss-dsi-ve-off-command",
		"lge,mdss-dsi-ve-off-command-state" },
	[TIMELM_CM_DAYLIGHT_ON] = { "lge,mdss-dsi-daylight-on-command",
		"lge,mdss-dsi-daylight-on-command-state" },
	[TIMELM_CM_DAYLIGHT_OFF] = { "lge,mdss-dsi-daylight-off-command",
		"lge,mdss-dsi-daylight-off-command-state" },
};

static struct {
	struct dsi_panel *panel;
	struct dsi_panel_cmd_set cmds[TIMELM_CM_MAX];
	bool use_irc, use_ace;
	int screen_mode;
	int sat, hue, sha, color_filter;	/* screen_tune */
	int preset, red, green, blue;		/* rgb_tune */
	int hdr_mode;
	int daylight;
	bool cm_status, dgc_status, sharp_status;
	bool irc_off;		/* stock irc_current_state == LGE_IRC_OFF */
	bool irc_req_off;	/* last irc_brighter request */
	bool ace_hdr, ve_hdr;
} timelm_cm;

static int timelm_cm_tx(struct dsi_panel *panel, int idx)
{
	return timelm_tx(panel, &timelm_cm.cmds[idx], timelm_cm_props[idx].cmd);
}

/* stock lge_display_control_store_sw43103(): 0x53 CM_EN/GM_EN/SHARPEN */
static void timelm_cm_disp_ctrl_locked(struct dsi_panel *panel)
{
	u8 *p = timelm_payload(&timelm_cm.cmds[TIMELM_CM_DISP_CTRL], 1, 2);

	if (!p)
		return;

	p[1] = (timelm_cm.cm_status << 2) | (timelm_cm.dgc_status << 7) |
		(timelm_cm.sharp_status << 6);
	pr_info("timelm-cm: ctrl-command-1 0x%02x\n", p[1]);
	timelm_cm_tx(panel, TIMELM_CM_DISP_CTRL);
}

/* stock rgb_preset / dg_ctrl1_values / dg_ctrl2_values (sw43103) */
#define TIMELM_DG_PRESETS	35
static const int timelm_rgb_preset[5][3] = {
	{12,  1, 23},
	{ 6,  1, 17},
	{ 0,  1, 11},
	{ 0,  7, 11},
	{ 0, 13, 11}
};

static const u8 timelm_dg_ctrl1[TIMELM_DG_PRESETS][3] = {
	[0 ... 13] = {0x03, 0x05, 0xAF},
	[14 ... 33] = {0x03, 0x05, 0xAB},
	[34] = {0x03, 0x05, 0x6B},
};

static const u8 timelm_dg_ctrl2[TIMELM_DG_PRESETS][9] = {
	{0x00, 0xFF, 0x94, 0x1F, 0xAE, 0x37, 0xAE, 0x1F, 0x94},
	{0x00, 0xFB, 0x94, 0x1E, 0xAD, 0x35, 0xAB, 0x1C, 0x90},
	{0x00, 0xF8, 0x94, 0x1E, 0xAC, 0x34, 0xAA, 0x1A, 0x8E},
	{0x00, 0xF5, 0x93, 0x1D, 0xAA, 0x31, 0xA8, 0x17, 0x8B},
	{0x00, 0xF2, 0x93, 0x1C, 0xA8, 0x30, 0xA5, 0x15, 0x88},
	{0x00, 0xF0, 0x93, 0x1C, 0xA8, 0x2F, 0xA4, 0x14, 0x87},
	{0x00, 0xEC, 0x92, 0x1A, 0xA6, 0x2D, 0xA1, 0x10, 0x83},
	{0x00, 0xE9, 0x92, 0x19, 0xA5, 0x2B, 0x9F, 0x0D, 0x80},
	{0x00, 0xE6, 0x90, 0x18, 0xA4, 0x2A, 0x9D, 0x0B, 0x7D},
	{0x00, 0xE3, 0x90, 0x17, 0xA3, 0x27, 0x9B, 0x09, 0x7A},
	{0x00, 0xE0, 0x90, 0x17, 0xA2, 0x26, 0x9A, 0x07, 0x78},
	{0x00, 0xDD, 0x8F, 0x16, 0x9F, 0x24, 0x97, 0x04, 0x75},
	{0x00, 0xDA, 0x8F, 0x15, 0x9E, 0x23, 0x95, 0x02, 0x72},
	{0x00, 0xD7, 0x8E, 0x14, 0x9D, 0x21, 0x93, 0x00, 0x70},
	{0x00, 0xD4, 0x8E, 0x14, 0x9C, 0x20, 0x91, 0xFE, 0x6D},
	{0x00, 0xD1, 0x8E, 0x13, 0x9B, 0x1D, 0x8F, 0xFB, 0x6A},
	{0x00, 0xCE, 0x8D, 0x11, 0x9A, 0x1C, 0x8D, 0xF8, 0x68},
	{0x00, 0xCB, 0x8D, 0x10, 0x99, 0x1A, 0x8B, 0xF6, 0x65},
	{0x00, 0xC9, 0x8D, 0x10, 0x98, 0x19, 0x8A, 0xF4, 0x63},
	{0x00, 0xC6, 0x8C, 0x0F, 0x96, 0x18, 0x87, 0xF2, 0x61},
	{0x00, 0xC3, 0x8C, 0x0E, 0x95, 0x16, 0x85, 0xF0, 0x5E},
	{0x00, 0xC0, 0x8C, 0x0E, 0x94, 0x15, 0x84, 0xEE, 0x5C},
	{0x00, 0xBD, 0x8B, 0x0D, 0x92, 0x12, 0x82, 0xEB, 0x59},
	{0x00, 0xBA, 0x8B, 0x0C, 0x91, 0x11, 0x80, 0xE9, 0x56},
	{0x00, 0xB6, 0x8A, 0x0B, 0x90, 0x0F, 0x7D, 0xE6, 0x53},
	{0x00, 0xB3, 0x8A, 0x0A, 0x8F, 0x0D, 0x7B, 0xE4, 0x50},
	{0x00, 0xB0, 0x89, 0x0A, 0x8E, 0x0C, 0x7A, 0xE2, 0x4D},
	{0x00, 0xAD, 0x88, 0x09, 0x8B, 0x0A, 0x77, 0xDF, 0x4A},
	{0x00, 0xAA, 0x88, 0x07, 0x8A, 0x09, 0x75, 0xDC, 0x47},
	{0x00, 0xA7, 0x87, 0x06, 0x89, 0x06, 0x73, 0xDA, 0x45},
	{0x00, 0xA4, 0x87, 0x06, 0x88, 0x05, 0x71, 0xD8, 0x42},
	{0x00, 0xA1, 0x87, 0x05, 0x87, 0x03, 0x6F, 0xD5, 0x3F},
	{0x00, 0x9E, 0x86, 0x04, 0x86, 0x02, 0x6D, 0xD3, 0x3D},
	{0x00, 0x9B, 0x86, 0x03, 0x85, 0x00, 0x6B, 0xD1, 0x3A},
	{0x00, 0x98, 0x86, 0x03, 0x83, 0xFF, 0x69, 0xCF, 0x38}
};

/* stock lge_set_custom_rgb_sw43103(): B4h (upper) / B5h (lower) gamma */
static void timelm_cm_custom_rgb_locked(struct dsi_panel *panel)
{
	struct dsi_panel_cmd_set *dg = &timelm_cm.cmds[TIMELM_CM_DG];
	const int *pre = timelm_rgb_preset[timelm_cm.preset];
	u8 *ctl1 = timelm_payload(dg, 1, 11);
	u8 *ctl2 = timelm_payload(dg, 2, 28);
	int idx[3], c, i;

	if (!ctl1 || !ctl2)
		return;

	for (c = 0; c < 3; c++) {
		int step = 0;

		if (timelm_cm.screen_mode != TIMELM_SCREEN_NATURAL)
			step = c == 0 ? timelm_cm.red :
				c == 1 ? timelm_cm.green : timelm_cm.blue;
		/* stock does not bound this; keep UBSAN quiet on bad input */
		idx[c] = clamp(pre[c] + step, 0, TIMELM_DG_PRESETS - 1);
		for (i = 0; i < 3; i++)
			ctl1[2 + 3 * c + i] = timelm_dg_ctrl1[idx[c]][i];
		for (i = 0; i < 9; i++)
			ctl2[1 + 9 * c + i] = timelm_dg_ctrl2[idx[c]][i];
	}

	pr_info("timelm-cm: rgb index r=%d g=%d b=%d\n", idx[0], idx[1],
		idx[2]);
	timelm_cm_tx(panel, TIMELM_CM_DG);
}

/* stock sat/hue/sha_ctrl_values (sw43103) */
static const u8 timelm_sat_ctrl[5][12] = {
	{0x29, 0x21, 0x21, 0x2B, 0x28, 0x29, 0x2A, 0x2A, 0x2A, 0x21, 0x29, 0x27},
	{0x2F, 0x27, 0x27, 0x31, 0x2E, 0x2F, 0x30, 0x30, 0x30, 0x27, 0x2F, 0x2D},
	{0x35, 0x2D, 0x2D, 0x37, 0x34, 0x35, 0x36, 0x36, 0x36, 0x2D, 0x35, 0x33},
	{0x3B, 0x33, 0x33, 0x3D, 0x3A, 0x3B, 0x3C, 0x3C, 0x3C, 0x33, 0x3B, 0x39},
	{0x41, 0x39, 0x39, 0x43, 0x40, 0x41, 0x42, 0x42, 0x42, 0x39, 0x41, 0x3F}
};

static const u8 timelm_hue_ctrl[5][12] = {
	{0x70, 0x4A, 0x6B, 0x85, 0x55, 0x60, 0x86, 0x80, 0x80, 0x80, 0x73, 0x62},
	{0x78, 0x52, 0x73, 0x8D, 0x5D, 0x68, 0x8E, 0x88, 0x88, 0x88, 0x7B, 0x6A},
	{0x80, 0x5A, 0x7B, 0x95, 0x65, 0x70, 0x96, 0x90, 0x90, 0x90, 0x83, 0x72},
	{0x88, 0x62, 0x83, 0x9D, 0x6D, 0x78, 0x9E, 0x98, 0x98, 0x98, 0x8B, 0x7A},
	{0x90, 0x6A, 0x8B, 0xA5, 0x75, 0x80, 0xA6, 0xA0, 0xA0, 0xA0, 0x93, 0x82}
};

static const u8 timelm_sha_ctrl[5][11] = {
	{0x89, 0x01, 0x01, 0x88, 0x24, 0x68, 0x22, 0x20, 0x04, 0x00, 0x00},
	{0x89, 0x0F, 0x0F, 0x88, 0x24, 0x68, 0x22, 0x20, 0x04, 0x00, 0x00},
	{0x89, 0x1F, 0x1F, 0x88, 0x24, 0x68, 0x22, 0x20, 0x04, 0x00, 0x00},
	{0x89, 0x2F, 0x2F, 0x88, 0x24, 0x68, 0x22, 0x20, 0x04, 0x00, 0x00},
	{0x89, 0x2F, 0x3F, 0x88, 0x24, 0x68, 0x22, 0x20, 0x04, 0x00, 0x00}
};

/* stock lge_set_screen_tune_sw43103(): B9h / BBh / B2h */
static void timelm_cm_screen_tune_locked(struct dsi_panel *panel)
{
	u8 *sat = timelm_payload(&timelm_cm.cmds[TIMELM_CM_SAT], 1, 13);
	u8 *hue = timelm_payload(&timelm_cm.cmds[TIMELM_CM_HUE], 1, 13);
	u8 *sha = timelm_payload(&timelm_cm.cmds[TIMELM_CM_SHA], 1, 12);

	if (!sat || !hue || !sha)
		return;

	memcpy(sat + 1, timelm_sat_ctrl[timelm_cm.sat], 12);
	memcpy(hue + 1, timelm_hue_ctrl[timelm_cm.hue], 12);
	memcpy(sha + 1, timelm_sha_ctrl[timelm_cm.sha], 11);

	timelm_cm_tx(panel, TIMELM_CM_SAT);
	timelm_cm_tx(panel, TIMELM_CM_HUE);
	timelm_cm_tx(panel, TIMELM_CM_SHA);
}

/* stock lge_set_screen_mode_sw43103() */
static void timelm_cm_screen_mode_locked(struct dsi_panel *panel)
{
	pr_info("timelm-cm: screen_mode %d\n", timelm_cm.screen_mode);

	timelm_cm.dgc_status = true;
	timelm_cm.sharp_status = false;

	switch (timelm_cm.screen_mode) {
	case TIMELM_SCREEN_NATURAL:
		timelm_cm_tx(panel, TIMELM_CM_NATURAL);
		timelm_cm_custom_rgb_locked(panel);
		break;
	case TIMELM_SCREEN_VIVID:
		timelm_cm_tx(panel, TIMELM_CM_VIVID);
		break;
	case TIMELM_SCREEN_CINEMA:
		timelm_cm_tx(panel, TIMELM_CM_CINEMA);
		break;
	case TIMELM_SCREEN_CUSTOM:
		timelm_cm.sharp_status = true;
		timelm_cm_screen_tune_locked(panel);
		timelm_cm_custom_rgb_locked(panel);
		break;
	default:
		timelm_cm_tx(panel, TIMELM_CM_SAT_DEFAULT);
		timelm_cm_tx(panel, TIMELM_CM_HUE_DEFAULT);
		timelm_cm_tx(panel, TIMELM_CM_WB_DEFAULT);
		break;
	}

	timelm_cm_disp_ctrl_locked(panel);
}

/* stock lge_backlight_device_update_status(): resend the current DBV */
static void timelm_cm_resend_bl_locked(struct dsi_panel *panel)
{
	if (panel->bl_config.bl_level)
		dsi_panel_set_backlight(panel, panel->bl_config.bl_level);
}

static void timelm_cm_update_hdr(void)
{
	pr_info("timelm-cm: ddic hdr %d (ve %d ace %d)\n",
		timelm_cm.ve_hdr || timelm_cm.ace_hdr, timelm_cm.ve_hdr,
		timelm_cm.ace_hdr);
}

/* stock lge_set_irc_state_sw43103(); @hbm: LGE_GLOBAL_IRC_HBM vs _HDR */
static void timelm_cm_irc_locked(struct dsi_panel *panel, bool hbm, bool off)
{
	u8 *p;

	if (timelm_cm.use_irc && timelm_cm.irc_off != off) {
		p = timelm_payload(&timelm_cm.cmds[TIMELM_CM_IRC], 1, 2);
		if (p) {
			p[1] = off ? 0x60 : 0x61;
			pr_info("timelm-cm: irc-command 0x%02x\n", p[1]);
			timelm_cm_tx(panel, TIMELM_CM_IRC);
		}
	}
	timelm_cm.irc_off = off;

	if (hbm && timelm_cm.use_ace) {
		if (off) {
			timelm_cm.ace_hdr = true;
			timelm_cm_update_hdr();
			timelm_cm_tx(panel, TIMELM_CM_ACE_TUNE);
		} else {
			timelm_cm.ace_hdr = false;
			timelm_cm_update_hdr();
			timelm_cm_tx(panel, TIMELM_CM_ACE_RESTORE);
			timelm_cm_screen_mode_locked(panel);
		}
	}
}

/* stock lge_hdr_mode_set_sw43103() */
static void timelm_cm_hdr_mode_locked(struct dsi_panel *panel)
{
	bool hdr = timelm_cm.hdr_mode > 0;

	if (hdr) {
		timelm_cm.cm_status = false;
		timelm_cm.dgc_status = false;
	} else {
		timelm_cm.cm_status = true;
	}
	pr_info("timelm-cm: hdr %d cm %d dgc %d\n", hdr, timelm_cm.cm_status,
		timelm_cm.dgc_status);

	if (timelm_cm.use_irc)
		timelm_cm_irc_locked(panel, false, hdr);

	if (hdr)
		timelm_cm_disp_ctrl_locked(panel);
	else
		timelm_cm_screen_mode_locked(panel);

	timelm_cm_resend_bl_locked(panel);
}

/* stock lge_daylight_mode_set_sw43103() */
static void timelm_cm_daylight_locked(struct dsi_panel *panel)
{
	timelm_cm_tx(panel, timelm_cm.daylight ? TIMELM_CM_DAYLIGHT_ON :
		     TIMELM_CM_DAYLIGHT_OFF);

	if (timelm_cm.use_irc)
		timelm_cm_irc_locked(panel, false, timelm_cm.hdr_mode > 0);

	timelm_cm_resend_bl_locked(panel);
}

/* stock lge_set_video_enhancement_sw43103() */
static void timelm_cm_ve_locked(struct dsi_panel *panel)
{
	timelm_cm_update_hdr();
	if (timelm_cm.ve_hdr || timelm_cm.ace_hdr) {
		timelm_cm_tx(panel, TIMELM_CM_VE_ON);
	} else {
		timelm_cm_tx(panel, TIMELM_CM_VE_OFF);
		timelm_cm_screen_mode_locked(panel);
	}

	timelm_cm_resend_bl_locked(panel);
}

/* colour state for whatever the panel shows when nothing else overrides */
static void timelm_cm_restore_locked(struct dsi_panel *panel)
{
	if (timelm_cm.panel != panel)
		return;

	if (timelm_cm.hdr_mode > 0)
		timelm_cm_disp_ctrl_locked(panel);
	else
		timelm_cm_screen_mode_locked(panel);
}

/*
 * dsi_panel_enable(): the on-command leaves the DDIC in natural mode with
 * IRC on (stock set_irc_default_state()); bring back the requested state.
 */
static void timelm_cm_reapply_locked(struct dsi_panel *panel)
{
	if (timelm_cm.panel != panel)
		return;

	timelm_cm.irc_off = false;
	timelm_cm.ace_hdr = false;

	if (timelm_cm.daylight)
		timelm_cm_tx(panel, TIMELM_CM_DAYLIGHT_ON);

	if (timelm_cm.hdr_mode > 0)
		timelm_cm_hdr_mode_locked(panel);
	else if (timelm_cm.screen_mode != TIMELM_SCREEN_NATURAL ||
		 timelm_cm.preset != 2)
		timelm_cm_screen_mode_locked(panel);

	if (timelm_cm.ve_hdr)
		timelm_cm_ve_locked(panel);

	if (timelm_cm.irc_req_off)
		timelm_cm_irc_locked(panel, true, true);
}

/*
 * sysfs store handlers.  The state is always stored; the DCS go out only
 * while the panel is on (otherwise at the next dsi_panel_enable()).
 */
#define TIMELM_CM_OP(_name, _body)					\
static int timelm_cm_##_name(struct dsi_panel *panel, int v)		\
{									\
	if (timelm_cm.panel != panel)					\
		return -ENODEV;						\
									\
	mutex_lock(&panel->panel_lock);					\
	_body;								\
	mutex_unlock(&panel->panel_lock);				\
	return 0;							\
}

TIMELM_CM_OP(set_screen_mode, ({
	timelm_cm.screen_mode = v;
	if (panel->panel_initialized && timelm_cm.hdr_mode <= 0)
		timelm_cm_screen_mode_locked(panel);
}))

TIMELM_CM_OP(set_hdr_mode, ({
	timelm_cm.hdr_mode = v;
	if (panel->panel_initialized)
		timelm_cm_hdr_mode_locked(panel);
}))

TIMELM_CM_OP(set_irc_brighter, ({
	timelm_cm.irc_req_off = v == 1;
	if (panel->panel_initialized &&
	    (timelm_cm.use_irc || timelm_cm.use_ace))
		timelm_cm_irc_locked(panel, true, timelm_cm.irc_req_off);
}))

TIMELM_CM_OP(set_daylight_mode, ({
	timelm_cm.daylight = v;
	if (panel->panel_initialized)
		timelm_cm_daylight_locked(panel);
}))

TIMELM_CM_OP(set_video_enhancement, ({
	timelm_cm.ve_hdr = v > 0;
	if (panel->panel_initialized)
		timelm_cm_ve_locked(panel);
}))

/* stock screen_tune_set(): applies only in the custom screen mode */
static int timelm_cm_set_screen_tune(struct dsi_panel *panel, int sat,
				     int hue, int sha, int cf)
{
	if (timelm_cm.panel != panel)
		return -ENODEV;

	mutex_lock(&panel->panel_lock);
	timelm_cm.sat = clamp(abs(sat), 0, 4);
	timelm_cm.hue = clamp(abs(hue), 0, 4);
	timelm_cm.sha = clamp(abs(sha), 0, 4);
	timelm_cm.color_filter = abs(cf);
	if (panel->panel_initialized && timelm_cm.hdr_mode <= 0 &&
	    timelm_cm.screen_mode == TIMELM_SCREEN_CUSTOM) {
		timelm_cm_screen_tune_locked(panel);
		timelm_cm.sharp_status = true;
		timelm_cm_disp_ctrl_locked(panel);
	}
	mutex_unlock(&panel->panel_lock);
	return 0;
}

/* stock rgb_tune_set() with color_manager_default_status (DT: 1) */
static int timelm_cm_set_rgb_tune(struct dsi_panel *panel, int preset,
				  int red, int green, int blue)
{
	if (timelm_cm.panel != panel)
		return -ENODEV;

	mutex_lock(&panel->panel_lock);
	timelm_cm.preset = clamp(preset, 0, 4);
	timelm_cm.red = abs(red);
	timelm_cm.green = abs(green);
	timelm_cm.blue = abs(blue);
	if (panel->panel_initialized && timelm_cm.hdr_mode <= 0 &&
	    (timelm_cm.screen_mode == TIMELM_SCREEN_NATURAL ||
	     timelm_cm.screen_mode == TIMELM_SCREEN_CUSTOM)) {
		timelm_cm.cm_status = true;
		timelm_cm.dgc_status = true;
		timelm_cm_custom_rgb_locked(panel);
		timelm_cm_disp_ctrl_locked(panel);
	}
	mutex_unlock(&panel->panel_lock);
	return 0;
}

static void timelm_cm_init(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = &panel->utils;

	if (timelm_cm.panel ||
	    !utils->read_bool(utils->data, "lge,use-color-manager-oled"))
		return;

	if (timelm_parse_cmds(timelm_cm.cmds, timelm_cm_props, TIMELM_CM_MAX,
			      utils))
		return;

	timelm_cm.use_irc = utils->read_bool(utils->data, "lge,use-irc-ctrl");
	timelm_cm.use_ace = utils->read_bool(utils->data, "lge,use-ace-ctrl");
	/* = panel on-command "53 84": natural, CM_EN | GM_EN */
	timelm_cm.screen_mode = TIMELM_SCREEN_NATURAL;
	timelm_cm.preset = 2;
	timelm_cm.cm_status = true;
	timelm_cm.dgc_status = true;
	timelm_cm.panel = panel;
	pr_info("timelm-cm: %s colour manager ready (irc %d ace %d)\n",
		panel->name, timelm_cm.use_irc, timelm_cm.use_ace);
}

static const struct timelm_cmd_prop timelm_lhbm_props[TIMELM_LHBM_CMD_MAX] = {
	[TIMELM_LHBM_CMD_READY] = { "lge,mdss-dsi-fp-lhbm-ready-command",
		"lge,mdss-dsi-fp-lhbm-ready-command-state" },
	[TIMELM_LHBM_CMD_EXIT] = { "lge,mdss-dsi-fp-lhbm-exit-command",
		"lge,mdss-dsi-fp-lhbm-exit-command-state" },
	[TIMELM_LHBM_CMD_ON] = { "lge,mdss-dsi-fp-lhbm-on-command",
		"lge,mdss-dsi-fp-lhbm-on-command-state" },
	[TIMELM_LHBM_CMD_OFF] = { "lge,mdss-dsi-fp-lhbm-off-command",
		"lge,mdss-dsi-fp-lhbm-off-command-state" },
};

static int timelm_lhbm_tx(struct dsi_panel *panel, int idx)
{
	return timelm_tx(panel, &timelm_lhbm.cmds[idx],
			 timelm_lhbm_props[idx].cmd);
}

/* stock lge_set_fp_lhbm_sw43103(); called with panel_lock held */
static void timelm_lhbm_apply_locked(struct dsi_panel *panel, int mode)
{
	timelm_lhbm.mode = mode;
	if (mode == timelm_lhbm.old_mode)
		return;

	if (mode == TIMELM_LHBM_READY) {
		timelm_lhbm.ready = true;
		timelm_lhbm_tx(panel, TIMELM_LHBM_CMD_READY);
	} else if (timelm_lhbm_is_on(mode)) {
		timelm_lhbm_tx(panel, TIMELM_LHBM_CMD_ON);
	} else if (timelm_lhbm_is_off(mode)) {
		timelm_lhbm_tx(panel, TIMELM_LHBM_CMD_OFF);
		if (!timelm_lhbm.ready)
			dsi_panel_set_backlight(panel, panel->bl_config.bl_level);
	} else if (mode == TIMELM_LHBM_EXIT ||
		   mode == TIMELM_LHBM_FORCED_EXIT) {
		timelm_lhbm.ready = false;
		dsi_panel_set_backlight(panel, panel->bl_config.bl_level);
		timelm_lhbm_tx(panel, TIMELM_LHBM_CMD_OFF);
		timelm_lhbm_tx(panel, TIMELM_LHBM_CMD_EXIT);
		/* READY/EXIT clobber 0x53; stock calls lge_set_screen_mode() */
		timelm_cm_restore_locked(panel);
	} else {
		pr_info("timelm-lhbm: unsupported mode %d\n", mode);
		return;
	}

	pr_info("timelm-lhbm: mode %d -> %d, bl_level %u\n",
		timelm_lhbm.old_mode, mode, panel->bl_config.bl_level);
	timelm_lhbm.old_mode = mode;
}

/* stock fp_lhbm_store(): wait up to 400 ms for the panel to come on */
static int timelm_lhbm_set(struct dsi_panel *panel, int mode)
{
	int retry;

	if (timelm_lhbm.panel != panel)
		return -ENODEV;

	for (retry = 0; ; retry++) {
		mutex_lock(&panel->panel_lock);
		if (panel->panel_initialized)
			break;

		timelm_lhbm.mode = mode;
		/* the panel is off: nothing is lit, disable already exited */
		if (!timelm_lhbm_is_on(mode) && mode != TIMELM_LHBM_READY) {
			timelm_lhbm.pending = false;
			mutex_unlock(&panel->panel_lock);
			return 0;
		}
		if (retry >= 40) {
			timelm_lhbm.pending = true;
			mutex_unlock(&panel->panel_lock);
			pr_err("timelm-lhbm: panel off, mode %d deferred\n",
			       mode);
			return -EBUSY;
		}
		mutex_unlock(&panel->panel_lock);
		msleep(10);
	}

	timelm_lhbm.pending = false;
	timelm_lhbm_apply_locked(panel, mode);
	mutex_unlock(&panel->panel_lock);
	return 0;
}

static void timelm_lhbm_init(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = &panel->utils;

	if (timelm_lhbm.panel ||
	    !utils->read_bool(utils->data, "lge,use-fp-lhbm"))
		return;

	if (timelm_parse_cmds(timelm_lhbm.cmds, timelm_lhbm_props,
			      TIMELM_LHBM_CMD_MAX, utils))
		return;

	timelm_lhbm.panel = panel;
	timelm_lhbm.mode = TIMELM_LHBM_EXIT;
	timelm_lhbm.old_mode = TIMELM_LHBM_EXIT;
	pr_info("timelm-lhbm: %s fp_lhbm ready\n", panel->name);
}


/*
 * /sys/class/panel, as stock lge_dsi_panel_create_sysfs(): img_tune/ holds
 * the lge_color_manager.c nodes, brightness/ the lge_brightness.c ones, in
 * the stock formats.  Users on this ROM: the LiveDisplay HAL (screen_mode,
 * hdr_mode, irc_brighter), the fingerprint HAL (fp_lhbm) and the chown/chmod
 * in init.lge.rc and the HAL rc, which run at init, after the panel probe.
 * The HAL registers IDisplayModes, IColorEnhancement and
 * ISunlightEnhancement only if their node exists, but the vendor VINTF
 * manifest declares all of them, so system_server waits forever without a
 * node (img47).  The nodes are therefore created for an LG panel even if a
 * command set is missing; stores then fail with -ENODEV.
 */
static struct class *timelm_panel_class;
static struct device *timelm_img_tune_dev;
static struct device *timelm_brightness_dev;

#define TIMELM_PANEL_INT_ATTR(_name, _val, _set)			\
static ssize_t _name##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct dsi_panel *panel = dev_get_drvdata(dev);			\
	int v;								\
									\
	mutex_lock(&panel->panel_lock);					\
	v = (_val);							\
	mutex_unlock(&panel->panel_lock);				\
	return sysfs_emit(buf, "%d\n", v);				\
}									\
static ssize_t _name##_store(struct device *dev,			\
			     struct device_attribute *attr,		\
			     const char *buf, size_t size)		\
{									\
	struct dsi_panel *panel = dev_get_drvdata(dev);			\
	int v, rc;							\
									\
	if (sscanf(buf, "%d", &v) != 1)					\
		return -EINVAL;						\
									\
	rc = _set(panel, v);						\
	return rc ? rc : size;						\
}									\
static DEVICE_ATTR(_name, 0664, _name##_show, _name##_store)

/* stock "%d %d %d %d " (screen_tune, rgb_tune) */
#define TIMELM_PANEL_QUAD_ATTR(_name, _a, _b, _c, _d, _set)		\
static ssize_t _name##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct dsi_panel *panel = dev_get_drvdata(dev);			\
	int v[4];							\
									\
	mutex_lock(&panel->panel_lock);					\
	v[0] = (_a);							\
	v[1] = (_b);							\
	v[2] = (_c);							\
	v[3] = (_d);							\
	mutex_unlock(&panel->panel_lock);				\
	return sysfs_emit(buf, "%d %d %d %d \n", v[0], v[1], v[2], v[3]);\
}									\
static ssize_t _name##_store(struct device *dev,			\
			     struct device_attribute *attr,		\
			     const char *buf, size_t size)		\
{									\
	struct dsi_panel *panel = dev_get_drvdata(dev);			\
	int v[4], rc;							\
									\
	if (sscanf(buf, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) != 4)\
		return -EINVAL;						\
									\
	rc = _set(panel, v[0], v[1], v[2], v[3]);			\
	return rc ? rc : size;						\
}									\
static DEVICE_ATTR(_name, 0664, _name##_show, _name##_store)

TIMELM_PANEL_INT_ATTR(screen_mode, timelm_cm.screen_mode,
		      timelm_cm_set_screen_mode);
TIMELM_PANEL_INT_ATTR(hdr_mode, timelm_cm.hdr_mode, timelm_cm_set_hdr_mode);
TIMELM_PANEL_INT_ATTR(daylight_mode, timelm_cm.daylight,
		      timelm_cm_set_daylight_mode);
TIMELM_PANEL_INT_ATTR(video_enhancement, timelm_cm.ve_hdr,
		      timelm_cm_set_video_enhancement);
TIMELM_PANEL_QUAD_ATTR(screen_tune, timelm_cm.sat, timelm_cm.hue,
		       timelm_cm.sha, timelm_cm.color_filter,
		       timelm_cm_set_screen_tune);
TIMELM_PANEL_QUAD_ATTR(rgb_tune, timelm_cm.preset, timelm_cm.red,
		       timelm_cm.green, timelm_cm.blue,
		       timelm_cm_set_rgb_tune);
/* stock lge_get_irc_state_sw43103(): the IRC state on the panel, 1 = off */
TIMELM_PANEL_INT_ATTR(irc_brighter, timelm_cm.irc_off,
		      timelm_cm_set_irc_brighter);
TIMELM_PANEL_INT_ATTR(fp_lhbm, timelm_lhbm.mode, timelm_lhbm_set);

static ssize_t irc_support_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", timelm_cm.use_irc || timelm_cm.use_ace);
}
static DEVICE_ATTR_RO(irc_support);

static struct attribute *img_tune_attrs[] = {
	&dev_attr_screen_mode.attr,
	&dev_attr_screen_tune.attr,
	&dev_attr_rgb_tune.attr,
	&dev_attr_hdr_mode.attr,
	&dev_attr_daylight_mode.attr,
	&dev_attr_video_enhancement.attr,
	NULL,
};
ATTRIBUTE_GROUPS(img_tune);

static struct attribute *brightness_attrs[] = {
	&dev_attr_irc_brighter.attr,
	&dev_attr_irc_support.attr,
	&dev_attr_fp_lhbm.attr,
	NULL,
};
ATTRIBUTE_GROUPS(brightness);

static void timelm_panel_sysfs_init(struct dsi_panel *panel)
{
	struct device *dev;

	if (timelm_img_tune_dev || timelm_brightness_dev)
		return;

	if (!timelm_panel_class) {
		timelm_panel_class = class_create(THIS_MODULE, "panel");
		if (IS_ERR(timelm_panel_class)) {
			DSI_ERR("timelm-panel: class_create failed %ld\n",
				PTR_ERR(timelm_panel_class));
			timelm_panel_class = NULL;
			return;
		}
	}

	dev = device_create_with_groups(timelm_panel_class, NULL, 0, panel,
					img_tune_groups, "img_tune");
	if (IS_ERR(dev))
		DSI_ERR("timelm-panel: img_tune failed %ld\n", PTR_ERR(dev));
	else
		timelm_img_tune_dev = dev;

	dev = device_create_with_groups(timelm_panel_class, NULL, 0, panel,
					brightness_groups, "brightness");
	if (IS_ERR(dev))
		DSI_ERR("timelm-panel: brightness failed %ld\n", PTR_ERR(dev));
	else
		timelm_brightness_dev = dev;
}

void timelm_panel_lge_init(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = &panel->utils;

	if (!utils->read_bool(utils->data, "lge,use-color-manager-oled") &&
	    !utils->read_bool(utils->data, "lge,use-fp-lhbm"))
		return;

	timelm_cm_init(panel);
	timelm_lhbm_init(panel);
	timelm_panel_sysfs_init(panel);
}

void timelm_panel_lge_put(struct dsi_panel *panel)
{
	if (timelm_img_tune_dev && dev_get_drvdata(timelm_img_tune_dev) == panel) {
		device_unregister(timelm_img_tune_dev);
		timelm_img_tune_dev = NULL;
	}
	if (timelm_brightness_dev &&
	    dev_get_drvdata(timelm_brightness_dev) == panel) {
		device_unregister(timelm_brightness_dev);
		timelm_brightness_dev = NULL;
	}

	if (timelm_cm.panel == panel) {
		timelm_free_cmds(timelm_cm.cmds, TIMELM_CM_MAX);
		memset(&timelm_cm, 0, sizeof(timelm_cm));
	}
	if (timelm_lhbm.panel == panel) {
		timelm_free_cmds(timelm_lhbm.cmds, TIMELM_LHBM_CMD_MAX);
		memset(&timelm_lhbm, 0, sizeof(timelm_lhbm));
	}
}

void timelm_panel_lge_enable_locked(struct dsi_panel *panel)
{
	/* stock lge_dsi_panel_mode_set(): colour state after the on-command */
	timelm_cm_reapply_locked(panel);

	/* stock dsi_display_set_power(): a request made while the panel was off */
	if (timelm_lhbm.panel == panel && timelm_lhbm.pending) {
		timelm_lhbm.pending = false;
		timelm_lhbm_apply_locked(panel, timelm_lhbm.mode);
	}
}

void timelm_panel_lge_disable_locked(struct dsi_panel *panel)
{
	/* stock is_need_lhbm_recovery(): do not leave the circle lit */
	if (timelm_lhbm.panel == panel &&
	    (timelm_lhbm.ready || timelm_lhbm_is_on(timelm_lhbm.old_mode)))
		timelm_lhbm_apply_locked(panel, TIMELM_LHBM_EXIT);
}
