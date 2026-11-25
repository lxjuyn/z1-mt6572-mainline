/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Z1_BATTERY_SEED_H
#define Z1_BATTERY_SEED_H

#include "z1_battery_ui.h"

#define Z1_RTC_SOC_MAX_DELTA	39
#define Z1_RTC_SOC_UNPLUGGED_MIN	5

/* Match the factory RTC filter, with an independent terminal-voltage guard. */
static inline int z1_battery_seed_soc(int voltage_soc, int rtc_soc,
				      int batsns_mv, bool supplied)
{
	if (rtc_soc < 1 || rtc_soc > 100 || batsns_mv <= Z1_UI_LOW_MV)
		return voltage_soc;

	if (!supplied && voltage_soc <= Z1_RTC_SOC_UNPLUGGED_MIN)
		return voltage_soc;

	if (rtc_soc < voltage_soc - Z1_RTC_SOC_MAX_DELTA ||
	    rtc_soc > voltage_soc + Z1_RTC_SOC_MAX_DELTA)
		return voltage_soc;

	return rtc_soc;
}

#endif
