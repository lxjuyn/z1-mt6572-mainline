/* SPDX-License-Identifier: GPL-2.0 */
/* STUB: MTK vendor cust_gpio_usage.h (board GPIO table) not present on mainline.
 * Z1 mainline port: define all referenced GPIO_COMBO / GPIO / _M_ constants as
 * inert 0 so wmt_plat_alps.c compiles; runtime GPIO ops are no-op stubs.
 * Definitions added on demand as .c references surface. */
#ifndef _CUST_GPIO_USAGE_H_STUB
#define _CUST_GPIO_USAGE_H_STUB

/* ---- GPIO pin ids (board-level). 0 = unmapped, inert. ---- */
#define GPIO_COMBO_RST_PIN             0
#define GPIO_COMBO_RST_PIN_M_GPIO      0
#define GPIO_COMBO_6620_LDO_EN_PIN     0
#define GPIO_COMBO_6620_LDO_EN_PIN_M_GPIO 0
#define GPIO_COMBO_PMU_EN_PIN          0
#define GPIO_COMBO_PMU_EN_PIN_M_GPIO   0
#define GPIO_COMBO_PMUV28_EN_PIN       0
#define GPIO_COMBO_PMUV28_EN_PIN_M_GPIO 0
#define GPIO_COMBO_URXD_PIN            0
#define GPIO_COMBO_URXD_PIN_M_GPIO     0
#define GPIO_COMBO_URXD_PIN_M_URXD     0
#define GPIO_COMBO_UTXD_PIN            0
#define GPIO_COMBO_UTXD_PIN_M_GPIO      0
#define GPIO_COMBO_UTXD_PIN_M_UTXD     0
#define GPIO_COMBO_BGF_EINT_PIN        0
#define GPIO_COMBO_BGF_EINT_PIN_M_EINT 0
#define GPIO_COMBO_BGF_EINT_PIN_M_GPIO 0
#define GPIO_COMBO_ALL_EINT_PIN        0
#define GPIO_COMBO_ALL_EINT_PIN_M_EINT 0
#define GPIO_COMBO_ALL_EINT_PIN_M_GPIO 0
#define GPIO_WIFI_EINT_PIN             0
#define GPIO_WIFI_EINT_PIN_M_EINT      0
#define GPIO_WIFI_EINT_PIN_M_GPIO      0
#define GPIO_COMBO_I2S_CK_PIN          0
#define GPIO_COMBO_I2S_CK_PIN_M_GPIO   0
#define GPIO_COMBO_I2S_CK_PIN_M_CLK    0
#define GPIO_COMBO_I2S_CK_PIN_M_I2S0_CK   0
#define GPIO_COMBO_I2S_CK_PIN_M_I2SIN_CK 0
#define GPIO_COMBO_I2S_DAT_PIN         0
#define GPIO_COMBO_I2S_DAT_PIN_M_GPIO  0
#define GPIO_COMBO_I2S_DAT_PIN_M_I2S0_DAT   0
#define GPIO_COMBO_I2S_DAT_PIN_M_I2SIN_DAT 0
#define GPIO_COMBO_I2S_DAT_PIN_M_MRG_I2S_PCM_RX 0
#define GPIO_COMBO_I2S_WS_PIN          0
#define GPIO_COMBO_I2S_WS_PIN_M_GPIO    0
#define GPIO_COMBO_I2S_WS_PIN_M_I2S0_WS 0
#define GPIO_COMBO_I2S_WS_PIN_M_I2SIN_WS 0
#define GPIO_COMBO_I2S_WS_PIN_M_MRG_I2S_PCM_SYNC 0
#define GPIO_GPS_LNA_PIN               0
#define GPIO_GPS_LNA_PIN_M_GPIO        0
#define GPIO_GPS_SYNC_PIN              0
#define GPIO_GPS_SYNC_PIN_M_GPIO       0
#define GPIO_GPS_SYNC_PIN_M_GPS_SYNC  0
#define GPIO_GPS_SYNC_PIN_M_MD1_GPS_SYNC 0
#define GPIO_GPS_SYNC_PIN_M_MD2_GPS_SYNC 0
#define GPIO_PCM_DAICLK_PIN            0
#define GPIO_PCM_DAICLK_PIN_M_GPIO     0
#define GPIO_PCM_DAICLK_PIN_M_CLK      0
#define GPIO_PCM_DAICLK_PIN_M_PCM0_CK  0
#define GPIO_PCM_DAIPCMIN_PIN          0
#define GPIO_PCM_DAIPCMIN_PIN_M_GPIO   0
#define GPIO_PCM_DAIPCMIN_PIN_M_DAIPCMIN     0
#define GPIO_PCM_DAIPCMIN_PIN_M_MRG_I2S_PCM_RX 0
#define GPIO_PCM_DAIPCMIN_PIN_M_PCM0_DI 0
#define GPIO_PCM_DAIPCMOUT_PIN         0
#define GPIO_PCM_DAIPCMOUT_PIN_M_GPIO  0
#define GPIO_PCM_DAIPCMOUT_PIN_M_DAIPCMOUT    0
#define GPIO_PCM_DAIPCMOUT_PIN_M_MRG_I2S_PCM_TX 0
#define GPIO_PCM_DAIPCMOUT_PIN_M_PCM0_DO 0
#define GPIO_PCM_DAISYNC_PIN           0
#define GPIO_PCM_DAISYNC_PIN_M_GPIO    0
#define GPIO_PCM_DAISYNC_PIN_M_BTSYNC  0
#define GPIO_PCM_DAISYNC_PIN_M_MRG_I2S_PCM_SYNC 0
#define GPIO_PCM_DAISYNC_PIN_M_PCM0_WS 0

/* user categories referenced by wmt_plat */
#define GPIO_USER_GPS   0

/* legacy alias some vendor files use */
#define GPIO_I2S        0

#endif /* _CUST_GPIO_USAGE_H_STUB */
