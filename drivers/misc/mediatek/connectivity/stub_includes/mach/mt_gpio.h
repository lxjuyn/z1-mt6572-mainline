/* SPDX-License-Identifier: GPL-2.0 */
/* STUB: MTK vendor header mt_gpio.h not present on mainline.
 * Z1 mainline port: declare all mt_set/get_gpio_* as inline no-ops returning 0
 * so wmt_plat_alps.c compiles. Runtime GPIO level control is a no-op here;
 * real power sequencing must be rewritten with mainline gpio_desc + DT later.
 * Definitions added on demand as .c references surface. */
#ifndef _MACH_MT_GPIO_H_STUB
#define _MACH_MT_GPIO_H_STUB

#include <linux/types.h>  /* for int */

/* GPIO mode constants */
#define GPIO_MODE_GPIO      0
#define GPIO_MODE_00        0
#define GPIO_MODE_01        1
#define GPIO_MODE_02        2
#define GPIO_MODE_03        3
#define GPIO_MODE_04        4
#define GPIO_MODE_05        5
#define GPIO_MODE_06        6
#define GPIO_MODE_07        7

/* GPIO direction */
#define GPIO_DIR_IN         0
#define GPIO_DIR_OUT        1

/* GPIO output level */
#define GPIO_OUT_ZERO       0
#define GPIO_OUT_ONE        1

/* GPIO pull */
#define GPIO_PULL_DISABLE   0
#define GPIO_PULL_ENABLE    1
#define GPIO_PULL_UP        1
#define GPIO_PULL_DOWN      2

/* Dummy base pin number used by some vendor macros */
#ifndef GPIO_DEFAULT
#define GPIO_DEFAULT        0
#endif

/* mt_set_gpio_* / mt_get_gpio_* — inert stubs.
 * MTK vendor signature: int mt_set_gpio_*(unsigned pin, unsigned val) */
static inline int mt_set_gpio_dir(unsigned pin, unsigned dir)        { (void)pin; (void)dir; return 0; }
static inline int mt_set_gpio_mode(unsigned pin, unsigned mode)       { (void)pin; (void)mode; return 0; }
static inline int mt_set_gpio_out(unsigned pin, unsigned out)        { (void)pin; (void)out; return 0; }
static inline int mt_set_gpio_pull_enable(unsigned pin, unsigned en)  { (void)pin; (void)en; return 0; }
static inline int mt_set_gpio_pull_select(unsigned pin, unsigned sel) { (void)pin; (void)sel; return 0; }
static inline int mt_set_gpio_smt(unsigned pin, unsigned arg)         { (void)pin; (void)arg; return 0; }
static inline int mt_set_gpio_invent(unsigned pin, unsigned arg)      { (void)pin; (void)arg; return 0; }

static inline int mt_get_gpio_dir(unsigned pin)        { (void)pin; return GPIO_DIR_OUT; }
static inline int mt_get_gpio_mode(unsigned pin)       { (void)pin; return GPIO_MODE_GPIO; }
static inline int mt_get_gpio_out(unsigned pin)        { (void)pin; return GPIO_OUT_ZERO; }
static inline int mt_get_gpio_pull_enable(unsigned pin) { (void)pin; return GPIO_PULL_DISABLE; }
static inline int mt_get_gpio_pull_select(unsigned pin) { (void)pin; return GPIO_PULL_DOWN; }
static inline int mt_get_gpio_in(unsigned pin)         { (void)pin; return 0; }

/* Chip ECO version detection (vendor: mt_boot/mt6573). Z1 MT6572 不需区分, 桩成非 E1. */
#ifndef CHIP_E1
#define CHIP_E1  1
#endif
#ifndef CHIP_E2
#define CHIP_E2  2
#endif
static inline unsigned int get_chip_eco_ver(void) { return CHIP_E2; }  /* 非 E1, 走 pin 分离分支 */

#endif /* _MACH_MT_GPIO_H_STUB */
