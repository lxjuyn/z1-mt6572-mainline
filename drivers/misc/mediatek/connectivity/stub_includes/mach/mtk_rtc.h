/* SPDX-License-Identifier: GPL-2.0 */
/* STUB: MTK vendor header mtk_rtc.h not present on mainline.
 * Z1 mainline port: declare rtc_gpio_* as inline no-ops so wmt_plat_alps.c
 * compiles. Real RTC/32k clock gating must be wired with mainline later.
 * Definitions added on demand as .c references surface. */
#ifndef _MACH_MTK_RTC_H_STUB
#define _MACH_MTK_RTC_H_STUB

#include <linux/types.h>

/* RTC GPIO user categories (vendor enum) — inert 0 */
#define RTC_GPIO_USER_GPS    0

/* Inert stubs. Vendor signatures:
 *   int  rtc_gpio_enable_32k(unsigned user);
 *   int  rtc_gpio_32k_status(void);
 */
static inline int rtc_gpio_enable_32k(unsigned user) { (void)user; return 0; }
static inline int rtc_gpio_32k_status(void)          { return 0; }

#endif /* _MACH_MTK_RTC_H_STUB */
