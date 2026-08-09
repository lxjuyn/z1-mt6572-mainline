/* SPDX-License-Identifier: GPL-2.0 */
/* STUB: Android earlysuspend removed from mainline.
 * Provide empty structs/no-op register hooks so legacy code compiles. */
#ifndef _LINUX_EARLYSUSPEND_H_STUB
#define _LINUX_EARLYSUSPEND_H_STUB

#include <linux/list.h>

enum {
	EARLY_SUSPEND_LEVEL_BLANK_SCREEN = 0,
	EARLY_SUSPEND_LEVEL_STOP_DRAWING  = 1,
	EARLY_SUSPEND_LEVEL_DISABLE_FB    = 2,
};

struct early_suspend {
	int level;
	void (*suspend)(struct early_suspend *h);
	void (*resume)(struct early_suspend *h);
	struct list_head link;
};

static inline void register_early_suspend(struct early_suspend *h)  { (void)h; }
static inline void unregister_early_suspend(struct early_suspend *h) { (void)h; }

#endif /* _LINUX_EARLYSUSPEND_H_STUB */
