// SPDX-License-Identifier: GPL-2.0-only
/*
 * timelm: 4.19 session-less synx API on the 5.15 session API, see
 * timelm_synx.h.
 */

#include <linux/mutex.h>
#include "msm_cvp_debug.h"
#include "timelm_synx.h"

static DEFINE_MUTEX(timelm_synx_lock);
static struct synx_session timelm_synx_session;
static unsigned int timelm_synx_users;

int timelm_synx_get(void)
{
	struct synx_initialization_params params = { .name = "msm_cvp" };
	int rc = 0;

	mutex_lock(&timelm_synx_lock);
	if (!timelm_synx_users) {
		rc = synx_initialize(&timelm_synx_session, &params);
		if (rc)
			dprintk(CVP_ERR, "%s: synx_initialize %d\n",
				__func__, rc);
	}
	if (!rc)
		timelm_synx_users++;
	mutex_unlock(&timelm_synx_lock);
	return rc;
}

void timelm_synx_put(void)
{
	mutex_lock(&timelm_synx_lock);
	if (timelm_synx_users && !--timelm_synx_users)
		synx_uninitialize(timelm_synx_session);
	mutex_unlock(&timelm_synx_lock);
}

int timelm_synx_import(s32 synx_obj, u32 secure_key, s32 *new_synx_obj)
{
	struct synx_import_params params = {
		.h_synx = synx_obj,
		.secure_key = secure_key,
		.new_h_synx = new_synx_obj,
	};

	return synx_import(timelm_synx_session, &params);
}

int timelm_synx_wait(s32 synx_obj, u64 timeout_ms)
{
	return synx_wait(timelm_synx_session, synx_obj, timeout_ms);
}

int timelm_synx_signal(s32 synx_obj, u32 status)
{
	return synx_signal(timelm_synx_session, synx_obj, status);
}

int timelm_synx_release(s32 synx_obj)
{
	return synx_release(timelm_synx_session, synx_obj);
}
