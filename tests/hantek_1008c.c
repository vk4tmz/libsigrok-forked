/*
 * This file is part of the libsigrok project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <config.h>
#include <stdlib.h>
#include <check.h>

#include "../src/hardware/hantek-1008c/protocol.h"

START_TEST(test_acquisition_geometry)
{
	static const unsigned int triggered[] = { 1, 2, 4, 4, 6, 6, 8, 8 };
	unsigned int channels;

	ck_assert_uint_eq(h1008c_acquisition_width(0), 0);
	ck_assert_uint_eq(h1008c_acquisition_width(9), 0);
	for (channels = 1; channels <= H1008C_NUM_HW_CHANNELS; channels++) {
		ck_assert_uint_eq(h1008c_acquisition_width(channels),
			triggered[channels - 1]);
		ck_assert_uint_eq(h1008c_rate_divisor(H1008C_MODE_TRIGGERED,
			channels), triggered[channels - 1]);
		ck_assert_uint_eq(h1008c_rate_divisor(H1008C_MODE_SCAN,
			channels), channels);
		ck_assert_uint_eq(h1008c_rate_divisor(H1008C_MODE_ROLL,
			channels), channels + 1);
	}
}
END_TEST

START_TEST(test_range_names)
{
	ck_assert_str_eq(h1008c_range_name(1), "Narrow");
	ck_assert_str_eq(h1008c_range_name(2), "Medium");
	ck_assert_str_eq(h1008c_range_name(3), "Wide");
	ck_assert_ptr_null(h1008c_range_name(0));
	ck_assert_ptr_null(h1008c_range_name(4));
	ck_assert_int_eq(h1008c_range_id("Narrow"), 1);
	ck_assert_int_eq(h1008c_range_id("Medium"), 2);
	ck_assert_int_eq(h1008c_range_id("Wide"), 3);
	ck_assert_int_eq(h1008c_range_id("A2=03"), -1);
}
END_TEST

static void check_rates(enum h1008c_acquisition_mode mode,
		const uint64_t *rates, const uint8_t *a3, size_t count)
{
	const struct h1008c_rate *selected;
	struct h1008c_rate rate;
	unsigned int channels, divisor;
	size_t i;

	ck_assert_uint_eq(h1008c_rate_count(mode), count);
	for (i = 0; i < count; i++) {
		ck_assert_int_eq(h1008c_rate_get(mode, i, &rate), SR_OK);
		ck_assert_uint_eq(rate.samplerate, rates[i]);
		ck_assert_uint_eq(rate.a3, a3[i]);
		ck_assert_int_eq(rate.mode, mode);
		for (channels = 1; channels <= H1008C_NUM_HW_CHANNELS; channels++) {
			divisor = h1008c_rate_divisor(mode, channels);
			if (mode != H1008C_MODE_TRIGGERED && rates[i] < divisor)
				continue;
			selected = h1008c_find_effective_rate(
				rates[i] / divisor, divisor, mode);
			ck_assert_ptr_nonnull(selected);
			ck_assert_uint_eq(selected->samplerate, rates[i]);
			ck_assert_uint_eq(selected->a3, a3[i]);
		}
	}
	ck_assert_int_eq(h1008c_rate_get(mode, count, &rate), SR_ERR_ARG);
	ck_assert_ptr_null(h1008c_find_effective_rate(
		UINT64_C(123456789), 1, mode));
}

START_TEST(test_validated_rate_tables)
{
	static const uint64_t triggered_rates[] = {
		2000, 4000, 8000, 20000, 40000, 80000,
		200000, 400000, 800000, 2400000,
	};
	static const uint8_t triggered_a3[] = {
		0x19, 0x18, 0x17, 0x16, 0x15, 0x14,
		0x13, 0x12, 0x11, 0x0f,
	};
	static const uint64_t scan_rates[] = {
		2, 4, 8, 20, 40, 80, 200, 400, 800,
	};
	static const uint8_t scan_a3[] = {
		0x22, 0x21, 0x20, 0x1f, 0x1e, 0x1d, 0x1c, 0x1b, 0x1a,
	};
	static const uint64_t roll_rates[] = {
		2, 4, 10, 18, 46, 100, 200, 402, 802, 2006, 4012,
	};
	static const uint8_t roll_a3[] = {
		0x22, 0x21, 0x20, 0x1f, 0x1e, 0x1d,
		0x1c, 0x1b, 0x1a, 0x19, 0x18,
	};

	check_rates(H1008C_MODE_TRIGGERED, triggered_rates, triggered_a3,
		ARRAY_SIZE(triggered_rates));
	check_rates(H1008C_MODE_SCAN, scan_rates, scan_a3,
		ARRAY_SIZE(scan_rates));
	check_rates(H1008C_MODE_ROLL, roll_rates, roll_a3,
		ARRAY_SIZE(roll_rates));
}
END_TEST

int main(void)
{
	Suite *suite;
	TCase *tc;
	SRunner *runner;
	int failed;

	suite = suite_create("hantek-1008c");
	tc = tcase_create("model");
	tcase_add_test(tc, test_acquisition_geometry);
	tcase_add_test(tc, test_range_names);
	tcase_add_test(tc, test_validated_rate_tables);
	suite_add_tcase(suite, tc);
	runner = srunner_create(suite);
	srunner_run_all(runner, CK_VERBOSE);
	failed = srunner_ntests_failed(runner);
	srunner_free(runner);
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
