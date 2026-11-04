/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timelm: QC 4.19 ALSA/ASoC core helpers the Elite codecs/machine use that the
 * 5.15 GKI core doesn't have.  Same semantics as LG's 4.19 kernel.
 */
#ifndef _TIMELM_ASOC_COMPAT_H
#define _TIMELM_ASOC_COMPAT_H

#include <linux/stat.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/jack.h>

/* 4.19 sound/core/info.c (QC): create + register a subdir entry */
static inline struct snd_info_entry *
snd_info_create_subdir(struct module *mod, const char *name,
		       struct snd_info_entry *parent)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_module_entry(mod, name, parent);
	if (!entry)
		return NULL;
	entry->mode = S_IFDIR | 0555;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return NULL;
	}
	return entry;
}

/*
 * 4.19 include/sound/jack.h (QC) jack bits.  Unused in 5.15 (BTN bits start
 * at 0x4000); the 5.15 core has no input switch for them, so reporting them
 * only changes the jack status word, as with the ROM's HAL expectations.
 */
#ifndef SND_JACK_OC_HPHL
#define SND_JACK_OC_HPHL	0x0040
#define SND_JACK_OC_HPHR	0x0080
#define SND_JACK_UNSUPPORTED	0x0100
#endif

#endif /* _TIMELM_ASOC_COMPAT_H */
