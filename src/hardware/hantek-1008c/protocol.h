/*
 * This file is part of the libsigrok project.
 *
 * Experimental Hantek 1008C support, based on independently captured USB
 * transactions. The sample-value semantics are still under investigation.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef LIBSIGROK_HARDWARE_HANTEK_1008C_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HANTEK_1008C_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hantek-1008c"

#define H1008C_USB_VID          0x0783
#define H1008C_USB_PID          0x5725
#define H1008C_USB_INTERFACE    0
#define H1008C_EP_OUT           0x02
#define H1008C_EP_IN            0x81
#define H1008C_USB_PACKET       64
#define H1008C_USB_TIMEOUT_MS   1000
#define H1008C_CMD_DELAY_US     (30 * 1000)

#define H1008C_NUM_HW_CHANNELS  8
#define H1008C_MVP_CHANNELS     1
#define H1008C_SAMPLERATE       UINT64_C(2400000)
#define H1008C_A3_24MSPS        0x0f
#define H1008C_A2_RANGE_MVP     0x03

struct dev_context {
	struct sr_sw_limits limits;
	uint64_t samplerate;
	gboolean running;
	uint64_t burst_count;
};

SR_PRIV int h1008c_open(struct sr_dev_inst *sdi);
SR_PRIV int h1008c_close(struct sr_dev_inst *sdi);
SR_PRIV int h1008c_reopen(struct sr_dev_inst *sdi);
SR_PRIV int h1008c_startup(const struct sr_dev_inst *sdi);
SR_PRIV int h1008c_acquire_frame(const struct sr_dev_inst *sdi,
		float **samples, size_t *sample_count, float *delta_zero);

#endif
