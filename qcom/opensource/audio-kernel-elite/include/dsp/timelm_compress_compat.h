/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timelm: Qualcomm 4.19 additions the 5.15 core lacks. The QC compress uapi
 * itself now lives in the kernel's include/uapi/sound/compress_*.h.
 */
#ifndef _TIMELM_COMPRESS_COMPAT_H
#define _TIMELM_COMPRESS_COMPAT_H

#include <sound/compress_params.h>

/* QC 4.19 hwdep iface: next after SNDRV_HWDEP_IFACE_FW_FIREFACE in both trees */
#include <sound/asound.h>
#ifndef SNDRV_HWDEP_IFACE_AUDIO_BE
#define SNDRV_HWDEP_IFACE_AUDIO_BE	(SNDRV_HWDEP_IFACE_FW_FIREFACE + 1)
#define SNDRV_HWDEP_IFACE_AUDIO_CODEC	(SNDRV_HWDEP_IFACE_FW_FIREFACE + 2)
#endif

#endif /* _TIMELM_COMPRESS_COMPAT_H */
