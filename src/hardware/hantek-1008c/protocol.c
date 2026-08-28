/*
 * This file is part of the libsigrok project.
 *
 * Experimental Hantek 1008C USB protocol support.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <config.h>
#include <string.h>
#include "protocol.h"

static int usb_write(const struct sr_dev_inst *sdi, const uint8_t *buf, int len)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	int transferred, ret;

	transferred = 0;
	ret = libusb_bulk_transfer(usb->devhdl, H1008C_EP_OUT,
			(unsigned char *)buf, len, &transferred, H1008C_USB_TIMEOUT_MS);
	if (ret != LIBUSB_SUCCESS || transferred != len) {
		sr_err("USB write failed: %s (sent %d/%d).",
			libusb_error_name(ret), transferred, len);
		return SR_ERR;
	}
	return SR_OK;
}

static int usb_read(const struct sr_dev_inst *sdi, uint8_t *buf, int len, int *actual)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;

	*actual = 0;
	ret = libusb_bulk_transfer(usb->devhdl, H1008C_EP_IN,
			buf, len, actual, H1008C_USB_TIMEOUT_MS);
	if (ret != LIBUSB_SUCCESS) {
		sr_err("USB read failed: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	return SR_OK;
}

static int transact(const struct sr_dev_inst *sdi, const uint8_t *tx, int tx_len,
		uint8_t *rx, int rx_len, int *actual)
{
	int ret;

	ret = usb_write(sdi, tx, tx_len);
	if (ret != SR_OK)
		return ret;
	return usb_read(sdi, rx, rx_len, actual);
}

static int command(const struct sr_dev_inst *sdi, const uint8_t *tx, int tx_len)
{
	uint8_t rx[H1008C_USB_PACKET];
	int actual, ret;

	ret = transact(sdi, tx, tx_len, rx, sizeof(rx), &actual);
	if (ret != SR_OK)
		return ret;
	if (actual <= 0) {
		sr_err("Command 0x%02x returned no data.", tx[0]);
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int h1008c_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc = sdi->driver->context;
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	char connection_id[64];
	int count, i, ret;

	count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (count < 0)
		return SR_ERR;

	ret = SR_ERR;
	for (i = 0; i < count; i++) {
		libusb_get_device_descriptor(devlist[i], &des);
		if (des.idVendor != H1008C_USB_VID || des.idProduct != H1008C_USB_PID)
			continue;
		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;
		if (strcmp(sdi->connection_id, connection_id))
			continue;
		if (libusb_open(devlist[i], &usb->devhdl) != LIBUSB_SUCCESS)
			continue;
		usb->bus = libusb_get_bus_number(devlist[i]);
		usb->address = libusb_get_device_address(devlist[i]);
		ret = SR_OK;
		break;
	}
	libusb_free_device_list(devlist, 1);
	if (ret != SR_OK)
		return ret;

	ret = libusb_claim_interface(usb->devhdl, H1008C_USB_INTERFACE);
	if (ret != LIBUSB_SUCCESS) {
		sr_err("Unable to claim USB interface: %s.", libusb_error_name(ret));
		libusb_close(usb->devhdl);
		usb->devhdl = NULL;
		return SR_ERR;
	}
	sr_dbg("USB connection active at %u.%u (%s).", usb->bus, usb->address,
		sdi->connection_id ? sdi->connection_id : "unknown-port");
	return SR_OK;
}

SR_PRIV int h1008c_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;

	if (!usb || !usb->devhdl)
		return SR_OK;
	libusb_release_interface(usb->devhdl, H1008C_USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	return SR_OK;
}

SR_PRIV int h1008c_reopen(struct sr_dev_inst *sdi)
{
	h1008c_close(sdi);
	if (h1008c_open(sdi) != SR_OK)
		return SR_ERR;
	sr_dbg("Reacquired Hantek 1008C for new acquisition.");
	return SR_OK;
}

static int get_buffer_size(const struct sr_dev_inst *sdi, uint8_t selector,
		uint16_t *size)
{
	uint8_t tx[] = { 0xc6, selector };
	uint8_t rx[H1008C_USB_PACKET];
	int actual;

	if (transact(sdi, tx, sizeof(tx), rx, sizeof(rx), &actual) != SR_OK)
		return SR_ERR;
	if (actual != 2) {
		sr_err("C6 %02x returned %d bytes, expected 2.", selector, actual);
		return SR_ERR;
	}
	*size = ((uint16_t)rx[0] << 8) | rx[1];
	if (*size & 1) {
		sr_err("C6 %02x returned odd size %u.", selector, *size);
		return SR_ERR;
	}
	return SR_OK;
}

static int read_buffer(const struct sr_dev_inst *sdi, uint8_t selector,
		uint8_t *buf, uint16_t size)
{
	uint8_t tx[] = { 0xa6, selector };
	uint8_t pkt[H1008C_USB_PACKET];
	size_t done;
	int actual;

	done = 0;
	while (done < size) {
		if (usb_write(sdi, tx, sizeof(tx)) != SR_OK)
			return SR_ERR;
		if (usb_read(sdi, pkt, sizeof(pkt), &actual) != SR_OK)
			return SR_ERR;
		if (actual != H1008C_USB_PACKET) {
			sr_err("A6 %02x returned %d bytes, expected 64.", selector, actual);
			return SR_ERR;
		}
		memcpy(buf + done, pkt, MIN((size_t)actual, size - done));
		done += MIN((size_t)actual, size - done);
	}
	return SR_OK;
}

static int wait_ready(const struct sr_dev_inst *sdi)
{
	static const uint8_t a5[] = { 0xa5, 0x5a };
	uint8_t rx[H1008C_USB_PACKET];
	int actual, i;
	uint8_t state;

	for (i = 0; i < H1008C_A5_READY_POLLS; i++) {
		if (transact(sdi, a5, sizeof(a5), rx, sizeof(rx), &actual) != SR_OK)
			return SR_ERR;
		state = actual > 0 ? rx[actual - 1] : 0xff;
		if (state == 0x02 || state == 0x03) {
			sr_dbg("A5 ready state=%u after %d poll(s).", state, i + 1);
			return SR_OK;
		}
		g_usleep(H1008C_A5_POLL_DELAY_US);
	}

	sr_err("A5 never reached ready state 2/3 within readiness window.");
	return SR_ERR;
}

static int read_current_buffers(const struct sr_dev_inst *sdi,
		uint8_t **raw, size_t *total)
{
	uint16_t size2, size3;
	uint8_t *buf;
	size_t len;

	*raw = NULL;
	*total = 0;
	if (get_buffer_size(sdi, 0x02, &size2) != SR_OK ||
	    get_buffer_size(sdi, 0x03, &size3) != SR_OK)
		return SR_ERR;
	len = (size_t)size2 + size3;
	if (!len || (len & 1)) {
		sr_err("Invalid combined capture size %zu.", len);
		return SR_ERR;
	}
	buf = g_try_malloc(len);
	if (!buf)
		return SR_ERR_MALLOC;
	if ((size2 && read_buffer(sdi, 0x02, buf, size2) != SR_OK) ||
	    (size3 && read_buffer(sdi, 0x03, buf + size2, size3) != SR_OK)) {
		g_free(buf);
		return SR_ERR;
	}
	*raw = buf;
	*total = len;
	return SR_OK;
}

static int startup_range_pass(const struct sr_dev_inst *sdi, uint8_t range)
{
	uint8_t a2[] = { 0xa2, range, range, range, range, range, range, range, range };
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t a4[] = { 0xa4, 0x01 };
	static const uint8_t c0[] = { 0xc0 };
	static const uint8_t c2[] = { 0xc2 };
	uint8_t *raw;
	size_t total;

	if (command(sdi, f3, sizeof(f3)) != SR_OK ||
	    command(sdi, a2, sizeof(a2)) != SR_OK ||
	    command(sdi, a4, sizeof(a4)) != SR_OK ||
	    command(sdi, c0, sizeof(c0)) != SR_OK)
		return SR_ERR;
	g_usleep(H1008C_CAL_CAPTURE_DELAY_US);
	if (command(sdi, c2, sizeof(c2)) != SR_OK || wait_ready(sdi) != SR_OK)
		return SR_ERR;
	if (read_current_buffers(sdi, &raw, &total) != SR_OK)
		return SR_ERR;
	sr_dbg("Startup range pass %u consumed %zu bytes (%zu words).",
		range, total, total / 2);
	g_free(raw);
	return SR_OK;
}

SR_PRIV int h1008c_startup(const struct sr_dev_inst *sdi)
{
	/* Full direct-ADC initialization validated against mfg92/hantek1008py. */
	static const uint8_t b0[] = { 0xb0 };
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t b9[] = { 0xb9, 0x01, 0xb0, 0x04, 0x00, 0x00 };
	static const uint8_t b7[] = { 0xb7, 0x00 };
	static const uint8_t bb[] = { 0xbb, 0x08, 0x00 };
	static const uint8_t b5[] = { 0xb5 };
	static const uint8_t b6[] = { 0xb6 };
	static const uint8_t e5[] = { 0xe5 };
	static const uint8_t f7[] = { 0xf7 };
	static const uint8_t f8[] = { 0xf8 };
	static const uint8_t fa[] = { 0xfa };
	static const uint8_t f5[] = { 0xf5 };
	static const uint8_t a0_all[] = { 0xa0, 0x08 };
	static const uint8_t aa_all[] = { 0xaa, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 };
	static const uint8_t a3_default[] = { 0xa3, 0x11 };
	static const uint8_t c1[] = { 0xc1, 0x00, 0x00 };
	static const uint8_t a7[] = { 0xa7, 0x00, 0x00 };
	static const uint8_t ac_init[] = { 0xac, 0x01, 0xf4, 0x00, 0x09, 0xc5, 0x00, 0x09, 0xc5 };
	static const uint8_t f6[] = { 0xf6 };
	static const uint8_t a3_fast[] = { 0xa3, H1008C_A3_24MSPS };
	static const uint8_t ac_pre[] = { 0xac, 0x00, 0xc8, 0x00, 0x02, 0xbd, 0x00, 0x02, 0xbd };
	static const uint8_t e4[] = { 0xe4, 0x01 };
	static const uint8_t e6[] = { 0xe6, 0x01 };
	static const uint8_t a0_ch1[] = { 0xa0, 0x01 };
	static const uint8_t aa_ch1[] = { 0xaa, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const uint8_t a2_ch1[] = { 0xa2, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03 };
	static const uint8_t ac_final[] = { 0xac, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x05, 0x79 };
	static const uint8_t ab[] = { 0xab, 0x08, 0x00 };
	static const uint8_t e9[] = { 0xe9 };
	struct cmd { const uint8_t *data; int len; };
	static const struct cmd init1_tail[] = {
		{ f3, sizeof(f3) }, { b9, sizeof(b9) }, { b7, sizeof(b7) },
		{ bb, sizeof(bb) }, { b5, sizeof(b5) }, { b6, sizeof(b6) },
		{ e5, sizeof(e5) }, { f7, sizeof(f7) }, { f8, sizeof(f8) },
		{ fa, sizeof(fa) }, { f5, sizeof(f5) }, { a0_all, sizeof(a0_all) },
		{ aa_all, sizeof(aa_all) }, { a3_default, sizeof(a3_default) },
		{ c1, sizeof(c1) }, { a7, sizeof(a7) }, { ac_init, sizeof(ac_init) },
	};
	static const struct cmd init3_tail[] = {
		{ e5, sizeof(e5) }, { f7, sizeof(f7) }, { f8, sizeof(f8) },
		{ fa, sizeof(fa) }, { a3_fast, sizeof(a3_fast) },
		{ ac_pre, sizeof(ac_pre) }, { e4, sizeof(e4) }, { e6, sizeof(e6) },
		{ f3, sizeof(f3) }, { a0_ch1, sizeof(a0_ch1) },
		{ aa_ch1, sizeof(aa_ch1) }, { a2_ch1, sizeof(a2_ch1) },
		{ a3_fast, sizeof(a3_fast) }, { c1, sizeof(c1) }, { a7, sizeof(a7) },
		{ ac_final, sizeof(ac_final) }, { ab, sizeof(ab) }, { e9, sizeof(e9) },
	};
	size_t i;

	if (command(sdi, b0, sizeof(b0)) != SR_OK)
		return SR_ERR;
	g_usleep(H1008C_INIT_B0_DELAY_US);
	if (command(sdi, b0, sizeof(b0)) != SR_OK)
		return SR_ERR;
	for (i = 0; i < ARRAY_SIZE(init1_tail); i++) {
		if (command(sdi, init1_tail[i].data, init1_tail[i].len) != SR_OK)
			return SR_ERR;
	}

	for (i = 1; i <= 3; i++) {
		if (startup_range_pass(sdi, (uint8_t)i) != SR_OK)
			return SR_ERR;
	}

	if (command(sdi, f6, sizeof(f6)) != SR_OK)
		return SR_ERR;
	g_usleep(H1008C_F6_DELAY_US);
	for (i = 0; i < ARRAY_SIZE(init3_tail); i++) {
		if (command(sdi, init3_tail[i].data, init3_tail[i].len) != SR_OK)
			return SR_ERR;
	}
	sr_info("Hantek 1008C direct-ADC initialization complete (CH1, 2.4 MS/s).");
	return SR_OK;
}

static int prepare_direct_capture(const struct sr_dev_inst *sdi, gint64 *a4_start_us)
{
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t e4[] = { 0xe4, 0x01 };
	static const uint8_t e6[] = { 0xe6, 0x01 };
	static const uint8_t a4[] = { 0xa4, 0x01 };
	static const uint8_t c0[] = { 0xc0 };
	static const uint8_t c2[] = { 0xc2 };

	if (command(sdi, f3, sizeof(f3)) != SR_OK ||
	    command(sdi, e4, sizeof(e4)) != SR_OK ||
	    command(sdi, e6, sizeof(e6)) != SR_OK)
		return SR_ERR;
	if (a4_start_us)
		*a4_start_us = g_get_monotonic_time();
	if (command(sdi, a4, sizeof(a4)) != SR_OK)
		return SR_ERR;
	if (H1008C_BURST_ARM_DELAY_US)
		g_usleep(H1008C_BURST_ARM_DELAY_US);
	if (command(sdi, c0, sizeof(c0)) != SR_OK ||
	    command(sdi, c2, sizeof(c2)) != SR_OK || wait_ready(sdi) != SR_OK)
		return SR_ERR;
	return SR_OK;
}

static int finish_direct_capture(const struct sr_dev_inst *sdi)
{
	static const uint8_t e4[] = { 0xe4, 0x01 };
	static const uint8_t e6[] = { 0xe6, 0x01 };

	if (command(sdi, e4, sizeof(e4)) != SR_OK ||
	    command(sdi, e6, sizeof(e6)) != SR_OK)
		return SR_ERR;
	return SR_OK;
}

SR_PRIV int h1008c_acquire_frame(const struct sr_dev_inst *sdi,
		float **samples, size_t *sample_count, gint64 *a4_start_us)
{
	uint8_t *raw;
	float *out;
	size_t total, i, n;
	int ret;

	*samples = NULL;
	*sample_count = 0;
	if (a4_start_us)
		*a4_start_us = 0;
	if (prepare_direct_capture(sdi, a4_start_us) != SR_OK)
		return SR_ERR;
	ret = read_current_buffers(sdi, &raw, &total);
	if (finish_direct_capture(sdi) != SR_OK) {
		g_free(raw);
		return SR_ERR;
	}
	if (ret != SR_OK)
		return ret;

	n = total / 2;
	out = g_try_new(float, n);
	if (!out) {
		g_free(raw);
		return SR_ERR_MALLOC;
	}
	for (i = 0; i < n; i++)
		out[i] = (float)(((uint16_t)raw[i * 2] |
			((uint16_t)raw[i * 2 + 1] << 8)) & 0x0fff);
	g_free(raw);

	*samples = out;
	*sample_count = n;
	return SR_OK;
}
