/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Z1_BATTERY_UI_H
#define Z1_BATTERY_UI_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#endif

#define Z1_UI_VBAT_SAMPLES	30
#define Z1_UI_LOW_MV		3450
#define Z1_UI_ZERO_MV		3400
#define Z1_UI_DOWN_HOLD_SWEEPS	6

struct z1_battery_ui {
	int samples[Z1_UI_VBAT_SAMPLES];
	unsigned int index;
	int sum;
	int capacity;
	unsigned int down_sweeps;
};

/* Call once per successful battery sweep with the terminal voltage. */
static inline int z1_battery_ui_update(struct z1_battery_ui *ui, int raw_soc,
				       int vbat_mv, bool supplied)
{
	int average_mv;
	unsigned int i;

	if (!ui->sum) {
		for (i = 0; i < Z1_UI_VBAT_SAMPLES; i++)
			ui->samples[i] = vbat_mv;
		ui->sum = vbat_mv * Z1_UI_VBAT_SAMPLES;
		ui->capacity = raw_soc;
	}

	ui->sum += vbat_mv - ui->samples[ui->index];
	ui->samples[ui->index] = vbat_mv;
	if (++ui->index == Z1_UI_VBAT_SAMPLES)
		ui->index = 0;
	average_mv = ui->sum / Z1_UI_VBAT_SAMPLES;

	/* The stock low-voltage branch allows 0% and bypasses normal sync. */
	if (average_mv <= Z1_UI_LOW_MV) {
		if (ui->capacity > 1 ||
		    (ui->capacity == 1 && average_mv <= Z1_UI_ZERO_MV))
			ui->capacity--;
		return ui->capacity;
	}

	/* Stock limits the previous UI value before normal sync when charging. */
	if (supplied && ui->capacity == 100)
		ui->capacity = 99;

	if (ui->capacity > raw_soc && ui->capacity > 1) {
		if (ui->down_sweeps == Z1_UI_DOWN_HOLD_SWEEPS) {
			ui->capacity--;
			ui->down_sweeps = 0;
		} else {
			ui->down_sweeps++;
		}
	} else {
		ui->capacity = raw_soc > 0 ? raw_soc : 1;
		ui->down_sweeps = 0;
	}

	return ui->capacity;
}

#endif
