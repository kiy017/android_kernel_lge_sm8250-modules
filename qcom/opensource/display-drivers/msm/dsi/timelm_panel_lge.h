/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timelm: LG panel features of the sw43103 (colour manager, IRC, fingerprint
 * LHBM) and their /sys/class/panel nodes, called from dsi_panel.c.
 */
#ifndef _TIMELM_PANEL_LGE_H_
#define _TIMELM_PANEL_LGE_H_

#include "dsi_panel.h"

/* dsi_panel_get(), after panel_lock is initialised */
void timelm_panel_lge_init(struct dsi_panel *panel);
/* dsi_panel_put() */
void timelm_panel_lge_put(struct dsi_panel *panel);
/* dsi_panel_enable(), after the on-command; panel_lock held */
void timelm_panel_lge_enable_locked(struct dsi_panel *panel);
/* dsi_panel_disable(), before the off-command; panel_lock held */
void timelm_panel_lge_disable_locked(struct dsi_panel *panel);
/* dsi_panel_enable(): true when the panel is being enabled into doze */
bool timelm_panel_lge_enable_to_lp(struct dsi_panel *panel);
/* dsi_panel_set_backlight(): DBV stays at 0x6A9 while LHBM is lit */
bool timelm_lhbm_blocks_bl(struct dsi_panel *panel);

#endif /* _TIMELM_PANEL_LGE_H_ */
