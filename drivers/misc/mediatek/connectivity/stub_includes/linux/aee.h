/* SPDX-License-Identifier: GPL-2.0 */
/* STUB: MediaTek AEE (Android Exception Engine) not present on mainline.
 * Provide common API no-ops surfaced when .c references them. */
#ifndef _LINUX_AEE_H_STUB
#define _LINUX_AEE_H_STUB

#include <linux/kernel.h>
#include <linux/types.h>

/* aee_kernel_warning / aee_kernel_exception et al. — no-op. */
static inline int aee_kernel_warning(const char *module, const char *msg, ...) { (void)module; (void)msg; return 0; }
static inline void aee_kernel_exception(const char *module, const char *msg, ...) { (void)module; (void)msg; }
static inline void aee_kernel_dal_show(const char *msg) { (void)msg; }
static inline void aee_kernel_reminder(const char *module, const char *msg, ...) { (void)module; (void)msg; }
static inline int aee_snprintf(char *buf, int len, const char *fmt, ...) { (void)fmt; if (buf && len>0) buf[0]='\0'; return 0; }

#define AE_START	0
#define AE_REP_SLEEP	1

#endif /* _LINUX_AEE_H_STUB */
