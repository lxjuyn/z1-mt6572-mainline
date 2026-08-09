/* SPDX-License-Identifier: GPL-2.0 */
/* STUB: MTK vendor header eint.h not present on mainline.
 * Z1 mainline port: declare mt65xx_eint_* as inline no-ops so wmt_plat_alps.c
 * compiles. Runtime EINT enable/mask is a no-op here; real EINT must be wired
 * with mainline irq/gpio APIs + DT later.
 * Definitions added on demand as .c references surface. */
#ifndef _MACH_EINT_H_STUB
#define _MACH_EINT_H_STUB

#include <linux/types.h>

/* EINT polarity / sensitivity values (vendor defines) — inert 0 */
#ifndef EINT_INT_POL_NEG
#define EINT_INT_POL_NEG  0
#endif
#ifndef LEVEL_SENSITIVE
#define LEVEL_SENSITIVE   0
#endif
#ifndef EDGE_SENSITIVE
#define EDGE_SENSITIVE    1
#endif

/* mt65xx_eint_* — inert stubs.
 * Vendor signatures:
 *   void mt65xx_eint_mask(unsigned eint_num);
 *   void mt65xx_eint_unmask(unsigned eint_num);
 *   void mt65xx_eint_set_sens(unsigned eint_num, unsigned sens);
 *   void mt65xx_eint_set_hw_debounce(unsigned eint_num, unsigned clk_cnt);
 *   void mt65xx_eint_registration(unsigned eint_num, unsigned dbn_en,
 *                                 unsigned pol, void (void), unsigned autounmask);
 */
static inline void mt65xx_eint_mask(unsigned eint_num)             { (void)eint_num; }
static inline void mt65xx_eint_unmask(unsigned eint_num)           { (void)eint_num; }
static inline void mt65xx_eint_set_sens(unsigned eint_num, unsigned sens)
    { (void)eint_num; (void)sens; }
static inline void mt65xx_eint_set_hw_debounce(unsigned eint_num, unsigned clk_cnt)
    { (void)eint_num; (void)clk_cnt; }
/* eint_registration's callback arg is `void (void)`; let caller's cb match. */
static inline void mt65xx_eint_registration(unsigned eint_num, unsigned dbn_en,
                                            unsigned pol,
                                            void (*handler)(void),
                                            unsigned autounmask)
    { (void)eint_num; (void)dbn_en; (void)pol; (void)handler; (void)autounmask; }

#endif /* _MACH_EINT_H_STUB */
