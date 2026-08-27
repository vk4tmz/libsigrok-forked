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
#include <math.h>
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
	g_usleep(H1008C_CMD_DELAY_US);
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
	/*
	 * Closing a stale libusb handle is harmless, and h1008c_open() locates
	 * the current device by sdi->connection_id (physical USB port path), not
	 * by the potentially changed USB address recorded during the first scan.
	 */
	h1008c_close(sdi);
	if (h1008c_open(sdi) != SR_OK)
		return SR_ERR;
	sr_dbg("Reacquired Hantek 1008C for new acquisition.");
	return SR_OK;
}

SR_PRIV int h1008c_startup(const struct sr_dev_inst *sdi)
{
	/* Exact known-good CH1/A2=03/2.4MS/s setup from the regression lab. */
	static const uint8_t b9[] = { 0xb9, 0x01, 0xbf, 0x04, 0x00, 0x00 };
	static const uint8_t b7[] = { 0xb7, 0x00 };
	static const uint8_t bb[] = { 0xbb, 0x08, 0x00 };
	static const uint8_t b0[] = { 0xb0 };
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t b5[] = { 0xb5 };
	static const uint8_t b6[] = { 0xb6 };
	static const uint8_t e5[] = { 0xe5 };
	static const uint8_t f7[] = { 0xf7 };
	static const uint8_t f8[] = { 0xf8 };
	static const uint8_t fa[] = { 0xfa };
	static const uint8_t f5[] = { 0xf5 };
	static const uint8_t a0[] = { 0xa0, 0x01 };
	static const uint8_t aa[] = { 0xaa, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const uint8_t a3[] = { 0xa3, H1008C_A3_24MSPS };
	static const uint8_t c1[] = { 0xc1, 0x00, 0x00 };
	static const uint8_t a7[] = { 0xa7, 0x00, 0x00 };
	static const uint8_t ac[] = { 0xac, 0x01, 0xf4, 0x00, 0x09, 0xc5, 0x00, 0x09, 0xc5 };
	struct cmd { const uint8_t *data; int len; };
	static const struct cmd cmds[] = {
		{ b9, sizeof(b9) }, { b7, sizeof(b7) }, { bb, sizeof(bb) },
		{ b0, sizeof(b0) }, { f3, sizeof(f3) }, { b5, sizeof(b5) },
		{ b6, sizeof(b6) }, { e5, sizeof(e5) }, { f7, sizeof(f7) },
		{ f8, sizeof(f8) }, { fa, sizeof(fa) }, { f5, sizeof(f5) },
		{ a0, sizeof(a0) }, { aa, sizeof(aa) }, { a3, sizeof(a3) },
		{ c1, sizeof(c1) }, { a7, sizeof(a7) }, { ac, sizeof(ac) },
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(cmds); i++) {
		if (command(sdi, cmds[i].data, cmds[i].len) != SR_OK)
			return SR_ERR;
	}
	return SR_OK;
}

static int prepare_capture(const struct sr_dev_inst *sdi)
{
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t a2[] = { 0xa2, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03 };
	static const uint8_t a4[] = { 0xa4, 0x01 };
	static const uint8_t c0[] = { 0xc0 };
	static const uint8_t c2[] = { 0xc2 };
	static const uint8_t a5[] = { 0xa5, 0x5a };
	struct cmd { const uint8_t *data; int len; };
	static const struct cmd cmds[] = {
		{ f3, sizeof(f3) }, { a2, sizeof(a2) }, { a4, sizeof(a4) },
		{ c0, sizeof(c0) }, { c2, sizeof(c2) },
		{ a5, sizeof(a5) }, { a5, sizeof(a5) },
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(cmds); i++) {
		if (command(sdi, cmds[i].data, cmds[i].len) != SR_OK)
			return SR_ERR;
	}
	return SR_OK;
}

static int get_buffer_size(const struct sr_dev_inst *sdi, uint8_t selector, uint16_t *size)
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
	if (!*size || (*size & 1)) {
		sr_err("C6 %02x returned invalid size %u.", selector, *size);
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

static float estimate_delta_zero(const uint8_t *buf, size_t len)
{
	/* Match the proven Python continuous reconstructor: acquisition median. */
	uint32_t hist[4096] = { 0 };
	size_t i, count, mid1, mid2, seen;
	uint16_t lo, hi;

	count = len / 2;
	for (i = 0; i + 1 < len; i += 2) {
		uint16_t v = ((uint16_t)buf[i] | ((uint16_t)buf[i + 1] << 8)) & 0x0fff;
		hist[v]++;
	}
	if (!count)
		return 0.0f;

	mid1 = (count - 1) / 2;
	mid2 = count / 2;
	seen = 0;
	lo = hi = 0;
	for (i = 0; i < ARRAY_SIZE(hist); i++) {
		if (!hist[i])
			continue;
		if (seen <= mid1 && mid1 < seen + hist[i])
			lo = (uint16_t)i;
		if (seen <= mid2 && mid2 < seen + hist[i]) {
			hi = (uint16_t)i;
			break;
		}
		seen += hist[i];
	}
	return ((float)lo + (float)hi) / 2.0f;
}

static float median_small(float *vals, size_t n)
{
	size_t i, j;
	float tmp;

	for (i = 1; i < n; i++) {
		tmp = vals[i];
		j = i;
		while (j > 0 && vals[j - 1] > tmp) {
			vals[j] = vals[j - 1];
			j--;
		}
		vals[j] = tmp;
	}
	if (n & 1)
		return vals[n / 2];
	return (vals[n / 2 - 1] + vals[n / 2]) / 2.0f;
}

static void reconstruct_relative_counts(const uint8_t *buf, size_t len,
		float zero, float *out)
{
	/*
	 * Port of hantek1008c.analysis.reconstruct_continuous_delta().
	 *
	 * 1. Subtract the acquisition-local median raw word.
	 * 2. Replace only implausibly large (>64 count) isolated deltas with the
	 *    local median of neighbouring non-discontinuous deltas.
	 * 3. Integrate the cleaned deltas.
	 * 4. Remove the best-fit linear drift caused by sub-count zero error.
	 *
	 * No voltage calibration, display centring, smoothing, or sample-rate
	 * normalisation is done here. Output remains experimental relative counts.
	 */
	const float discontinuity_limit = 64.0f;
	float *deltas, *cleaned;
	float acc, mx, my, num, den, slope, intercept;
	size_t i, j, n, lo, hi, good_n;

	n = len / 2;
	deltas = g_try_new(float, n);
	cleaned = g_try_new(float, n);
	if (!deltas || !cleaned) {
		g_free(deltas);
		g_free(cleaned);
		for (i = 0; i < n; i++)
			out[i] = 0.0f;
		return;
	}

	for (i = 0; i < n; i++) {
		uint16_t v = ((uint16_t)buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8)) & 0x0fff;
		deltas[i] = (float)v - zero;
		cleaned[i] = deltas[i];
	}

	for (i = 0; i < n; i++) {
		float local[9];
		if (fabsf(deltas[i]) <= discontinuity_limit)
			continue;
		lo = i > 4 ? i - 4 : 0;
		hi = MIN(n, i + 5);
		good_n = 0;
		for (j = lo; j < hi; j++) {
			if (fabsf(deltas[j]) <= discontinuity_limit)
				local[good_n++] = deltas[j];
		}
		cleaned[i] = good_n ? median_small(local, good_n) : 0.0f;
	}

	acc = 0.0f;
	for (i = 0; i < n; i++) {
		acc += cleaned[i];
		out[i] = acc;
	}

	if (n >= 2) {
		mx = ((float)n - 1.0f) / 2.0f;
		my = 0.0f;
		for (i = 0; i < n; i++)
			my += out[i];
		my /= (float)n;
		num = 0.0f;
		den = 0.0f;
		for (i = 0; i < n; i++) {
			float dx = (float)i - mx;
			num += dx * (out[i] - my);
			den += dx * dx;
		}
		slope = den ? num / den : 0.0f;
		intercept = my - slope * mx;
		for (i = 0; i < n; i++)
			out[i] -= intercept + slope * (float)i;
	}

	g_free(cleaned);
	g_free(deltas);
}

SR_PRIV int h1008c_acquire_frame(const struct sr_dev_inst *sdi,
		float **samples, size_t *sample_count, float *delta_zero)
{
	uint16_t size2, size3;
	float zero;
	uint8_t *raw;
	float *out;
	size_t total;

	*samples = NULL;
	*sample_count = 0;
	if (prepare_capture(sdi) != SR_OK)
		return SR_ERR;
	if (get_buffer_size(sdi, 0x02, &size2) != SR_OK ||
	    get_buffer_size(sdi, 0x03, &size3) != SR_OK)
		return SR_ERR;

	total = (size_t)size2 + size3;
	raw = g_try_malloc(total);
	if (!raw)
		return SR_ERR_MALLOC;
	if (read_buffer(sdi, 0x02, raw, size2) != SR_OK ||
	    read_buffer(sdi, 0x03, raw + size2, size3) != SR_OK) {
		g_free(raw);
		return SR_ERR;
	}

	zero = estimate_delta_zero(raw, total);
	out = g_try_malloc((total / 2) * sizeof(*out));
	if (!out) {
		g_free(raw);
		return SR_ERR_MALLOC;
	}
	reconstruct_relative_counts(raw, total, zero, out);
	g_free(raw);

	*samples = out;
	*sample_count = total / 2;
	if (delta_zero)
		*delta_zero = zero;
	return SR_OK;
}
