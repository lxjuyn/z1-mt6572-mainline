/* SPDX-License-Identifier: GPL-2.0 */
/* STUB: MTK vendor cust_eint.h (board EINT table) not present on mainline.
 * Z1 mainline port: define all referenced CUST_EINT_* constants as 0 so
 * wmt_plat_alps.c compiles; runtime EINT ops are no-op stubs.
 * Definitions added on demand as .c references surface. */
#ifndef _CUST_EINT_H_STUB
#define _CUST_EINT_H_STUB

#define CUST_EINT_COMBO_BGF_NUM            0
#define CUST_EINT_COMBO_BGF_DEBOUNCE_CN    0
#define CUST_EINT_COMBO_BGF_DEBOUNCE_EN    0
#define CUST_EINT_COMBO_BGF_POLARITY       0
#define CUST_EINT_COMBO_BGF_SENSITIVE      0

#define CUST_EINT_COMBO_ALL_NUM            0
#define CUST_EINT_COMBO_ALL_DEBOUNCE_CN    0
#define CUST_EINT_COMBO_ALL_DEBOUNCE_EN    0
#define CUST_EINT_COMBO_ALL_POLARITY       0
#define CUST_EINT_COMBO_ALL_SENSITIVE      0

#define CUST_EINT_WIFI_NUM                 0

#define CUST_EINT_GPIO_USER_GPS 0

#endif /* _CUST_EINT_H_STUB */
