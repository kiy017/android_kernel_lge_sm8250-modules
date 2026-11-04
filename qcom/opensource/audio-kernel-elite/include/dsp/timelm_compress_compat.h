/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timelm: Qualcomm 4.19 extensions to <sound/compress_params.h> that the
 * 5.15 GKI uapi doesn't carry.  Values are taken verbatim from LG's 4.19
 * include/uapi/sound/compress_params.h, i.e. what the vendor audio HAL
 * (audio.primary.kona.so) was built against.
 *
 * Caveat: upstream 5.15 redefines SND_AUDIOCODEC_ALAC/APE (0x0F/0x10 vs
 * 4.19 0x20/0x21) and its union snd_codec_options is smaller, so the
 * compress-offload ioctls (struct snd_compr_params) do not match the 4.19 HAL.
 * Compress offload needs separate handling; PCM paths don't use any of this.
 */
#ifndef _TIMELM_COMPRESS_COMPAT_H
#define _TIMELM_COMPRESS_COMPAT_H

#include <sound/compress_params.h>

#ifndef SND_AUDIOCODEC_AC3
#define SND_AUDIOCODEC_DTS_PASS_THROUGH      ((__u32) 0x0000000F)
#define SND_AUDIOCODEC_DTS_LBR               ((__u32) 0x00000010)
#define SND_AUDIOCODEC_DTS_TRANSCODE_LOOPBACK ((__u32) 0x00000011)
#define SND_AUDIOCODEC_PASS_THROUGH          ((__u32) 0x00000012)
#define SND_AUDIOCODEC_MP2                   ((__u32) 0x00000013)
#define SND_AUDIOCODEC_DTS_LBR_PASS_THROUGH  ((__u32) 0x00000014)
#define SND_AUDIOCODEC_AC3                   ((__u32) 0x00000015)
#define SND_AUDIOCODEC_AC3_PASS_THROUGH      ((__u32) 0x00000016)
#define SND_AUDIOCODEC_WMA_PRO               ((__u32) 0x00000017)
#define SND_AUDIOCODEC_DTS                   ((__u32) 0x00000018)
#define SND_AUDIOCODEC_EAC3                  ((__u32) 0x00000019)
#define SND_AUDIOCODEC_DSD                   ((__u32) 0x00000022)
#define SND_AUDIOCODEC_APTX                  ((__u32) 0x00000023)
#define SND_AUDIOCODEC_TRUEHD                ((__u32) 0x00000024)
#define SND_AUDIOCODEC_MAT                   ((__u32) 0x00000025)
#define SND_AUDIOCODEC_THD                   ((__u32) 0x00000026)
#endif

#ifndef COMPRESSED_TIMESTAMP_FLAG
#define COMPRESSED_TIMESTAMP_FLAG 0x0001
#endif

#ifndef SND_CODEC_COMPRESS_PASSTHROUGH
#define SND_CODEC_COMPRESS_PASSTHROUGH
struct snd_codec_metadata {
	__u32 length;
	__u32 offset;
	__u64 timestamp;
	__u32 reserved[4];
};
#endif

/* QC 4.19 hwdep iface: next after SNDRV_HWDEP_IFACE_FW_FIREFACE in both trees */
#include <sound/asound.h>
#ifndef SNDRV_HWDEP_IFACE_AUDIO_BE
#define SNDRV_HWDEP_IFACE_AUDIO_BE	(SNDRV_HWDEP_IFACE_FW_FIREFACE + 1)
#define SNDRV_HWDEP_IFACE_AUDIO_CODEC	(SNDRV_HWDEP_IFACE_FW_FIREFACE + 2)
#endif

#endif /* _TIMELM_COMPRESS_COMPAT_H */
