/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timelm: the 4.19 kona synx kernel API had no sessions
 * (synx_import(obj, key, &new), synx_wait(obj, ms), ...).  The 5.15
 * CONFIG_MSM_GLOBAL_SYNX driver takes a struct synx_session first.  msm_cvp
 * uses one module-wide session, taken in msm_cvp_open() and dropped in
 * msm_cvp_destroy() (stock called synx_initialize()/synx_uninitialize()
 * there).  Implemented in timelm_synx.c.
 */
#ifndef __TIMELM_CVP_SYNX_H__
#define __TIMELM_CVP_SYNX_H__

#include <linux/types.h>
#include <synx_api.h>

int timelm_synx_get(void);
void timelm_synx_put(void);
int timelm_synx_import(s32 synx_obj, u32 secure_key, s32 *new_synx_obj);
int timelm_synx_wait(s32 synx_obj, u64 timeout_ms);
int timelm_synx_signal(s32 synx_obj, u32 status);
int timelm_synx_release(s32 synx_obj);

#endif
