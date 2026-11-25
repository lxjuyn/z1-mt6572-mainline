/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Z1_BATTERY_OAM_H
#define Z1_BATTERY_OAM_H

#ifdef __KERNEL__
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/types.h>
#else
#include <limits.h>
#include <stdbool.h>
#endif

#define Z1_OAM_QMAX_MAH		800
#define Z1_OAM_SWEEP_SECONDS	10
#define Z1_OAM_D5_SWEEPS	7
/* A new voltage sample cannot represent an arbitrarily long gap. */
#define Z1_OAM_MAX_ELAPSED_S	60

struct z1_oam_point {
	int mv;
	int dod;
	int mohms;
};

/* Immutable after initialization. Highest voltage and lowest DOD first. */
struct z1_oam_profile {
	const struct z1_oam_point *points;
	unsigned int count;
};

struct z1_oam_state {
	int d0, d1, d2, d3, d4, d5;
	int ocv1_mv, ocv2_mv;
	int r1_mohms, r2_mohms;
	/* Positive current discharges the cell. Current is in 0.1 mA. */
	int i1, i2;
	/* Integrated discharge since initialization, in 0.1 mAh. */
	int car1, car2;
	unsigned int sweeps;
	bool initialized;
};

static inline long long z1_oam_div(long long numerator, int denominator)
{
#ifdef __KERNEL__
	return div_s64(numerator, denominator);
#else
	return numerator / denominator;
#endif
}

static inline bool z1_oam_valid_mv(int mv)
{
	return mv > 0 && mv <= 10000;
}

static inline bool z1_oam_valid_profile(const struct z1_oam_profile *profile)
{
	const struct z1_oam_point *p;
	unsigned int i;

	if (!profile || !profile->points || profile->count < 2)
		return false;

	p = profile->points;
	if (p[0].dod != 0 || p[profile->count - 1].dod != 100)
		return false;

	for (i = 0; i < profile->count; i++) {
		if (!z1_oam_valid_mv(p[i].mv) ||
		    p[i].mohms <= 0 || p[i].mohms > 10000)
			return false;
		if (i && (p[i - 1].mv <= p[i].mv || p[i - 1].dod > p[i].dod))
			return false;
	}

	return true;
}

static inline int z1_oam_by_voltage(const struct z1_oam_profile *profile,
				    int mv, bool resistance)
{
	const struct z1_oam_point *p = profile->points;
	unsigned int i;
	int high, low;

	for (i = 0; i < profile->count - 1; i++) {
		if (mv >= p[i].mv)
			return resistance ? p[i].mohms : p[i].dod;
		if (mv >= p[i + 1].mv) {
			high = resistance ? p[i].mohms : p[i].dod;
			low = resistance ? p[i + 1].mohms : p[i + 1].dod;
			return high + (low - high) * (p[i].mv - mv) /
				(p[i].mv - p[i + 1].mv);
		}
	}

	return resistance ? p[i].mohms : p[i].dod;
}

static inline int z1_oam_ocv_from_dod(const struct z1_oam_profile *profile,
				      int dod)
{
	const struct z1_oam_point *p = profile->points;
	unsigned int i;

	/* A repeated DOD uses its first occurrence, as in fgauge_get_voltage(). */
	for (i = 0; i < profile->count - 1; i++) {
		if (dod <= p[i].dod)
			return p[i].mv;
		if (dod <= p[i + 1].dod)
			return p[i].mv - (p[i].mv - p[i + 1].mv) *
				(dod - p[i].dod) / (p[i + 1].dod - p[i].dod);
	}

	return p[i].mv;
}

static inline bool z1_oam_init_with_dod(struct z1_oam_state *state,
					const struct z1_oam_profile *profile,
					int ocv_mv, int dod)
{
	struct z1_oam_state next = {};

	next.d0 = dod;
	next.d1 = next.d2 = next.d3 = next.d4 = next.d5 = next.d0;
	next.ocv1_mv = next.ocv2_mv = z1_oam_ocv_from_dod(profile, next.d0);
	next.r1_mohms = next.r2_mohms = z1_oam_by_voltage(profile, ocv_mv, true);
	next.initialized = true;
	*state = next;
	return true;
}

static inline bool z1_oam_init_soc(struct z1_oam_state *state,
				   const struct z1_oam_profile *profile,
				   int ocv_mv, int soc)
{
	if (soc < 0 || soc > 100 || !z1_oam_valid_mv(ocv_mv) ||
	    !z1_oam_valid_profile(profile))
		return false;

	return z1_oam_init_with_dod(state, profile, ocv_mv, 100 - soc);
}

static inline bool z1_oam_init(struct z1_oam_state *state,
			       const struct z1_oam_profile *profile, int ocv_mv)
{
	if (!z1_oam_valid_mv(ocv_mv) || !z1_oam_valid_profile(profile))
		return false;

	return z1_oam_init_with_dod(state, profile, ocv_mv,
				    z1_oam_by_voltage(profile, ocv_mv, false));
}

static inline int z1_oam_accumulate(int car, int current_deci_ma, unsigned int elapsed_s)
{
	long long sum = car + z1_oam_div((long long)current_deci_ma * elapsed_s, 3600);

	/* Stock truncates the increment on each sweep, in either direction. */
	if (sum > INT_MAX)
		return INT_MAX;
	if (sum < INT_MIN)
		return INT_MIN;

	return sum;
}

static inline int z1_oam_dod_from_car(int initial_dod, int car)
{
	long long dod = initial_dod + z1_oam_div((long long)car * 10,
						Z1_OAM_QMAX_MAH);

	if (dod < 0)
		return 0;
	if (dod > 100)
		return 100;

	return dod;
}

static inline int z1_oam_compensated_ocv(const struct z1_oam_profile *profile,
					int mv, int current_deci_ma)
{
	int ocv = mv;
	int i;

	/* Stock iterates five times, then computes the final compensation. */
	for (i = 0; i < 6; i++) {
		int resistance = z1_oam_by_voltage(profile, ocv, true);
		long long drop = z1_oam_div((long long)current_deci_ma * resistance, 1000);

		ocv = mv + z1_oam_div(drop + 5, 10);
	}

	return ocv;
}

/* Requires the same profile used at initialization. */
static inline bool z1_oam_update(struct z1_oam_state *state,
				 const struct z1_oam_profile *profile,
				 int vbat_mv, unsigned int elapsed_s)
{
	struct z1_oam_state next = *state;
	long long i1;
	bool charging;

	if (!state->initialized || !z1_oam_valid_mv(vbat_mv) ||
	    !elapsed_s || elapsed_s > Z1_OAM_MAX_ELAPSED_S)
		return false;

	i1 = z1_oam_div(((long long)next.ocv1_mv - vbat_mv) * 10000,
			next.r1_mohms);
	if (i1 < INT_MIN || i1 > INT_MAX)
		return false;

	next.i1 = i1;
	next.i2 = (next.ocv2_mv - vbat_mv) * 10000 / next.r2_mohms;
	next.car1 = z1_oam_accumulate(next.car1, next.i1, elapsed_s);
	next.car2 = z1_oam_accumulate(next.car2, next.i2, elapsed_s);
	next.d1 = z1_oam_dod_from_car(next.d0, next.car1);
	next.d2 = z1_oam_dod_from_car(next.d0, next.car2);

	next.ocv1_mv = z1_oam_compensated_ocv(profile, vbat_mv, next.i2);
	next.d3 = next.d4 = z1_oam_by_voltage(profile, next.ocv1_mv, false);
	next.r1_mohms = z1_oam_by_voltage(profile, next.ocv1_mv, true);
	next.ocv2_mv = z1_oam_ocv_from_dod(profile, next.d2);
	next.r2_mohms = z1_oam_by_voltage(profile, next.ocv2_mv, true);

	if (++next.sweeps == Z1_OAM_D5_SWEEPS) {
		next.sweeps = 0;
		charging = next.i1 < 0 || next.i2 < 0;
		if (charging && next.d3 < next.d5)
			next.d5--;
		else if (!charging && next.d3 > next.d5)
			next.d5++;
	}

	*state = next;
	return true;
}

#endif
