/*
 * This file is part of the libsigrok project.
 *
 * Experimental Hantek 1008C driver MVP.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <config.h>
#include "protocol.h"

static struct sr_dev_driver hantek_1008c_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint64_t samplerates[] = {
	H1008C_SAMPLERATE,
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc = di->context;
	struct libusb_device_descriptor des;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb_match;
	libusb_device **devlist;
	GSList *devices, *conn_devices, *l;
	const char *conn;
	char connection_id[64];
	int i;

	devices = NULL;
	conn_devices = NULL;
	conn = NULL;
	for (l = options; l; l = l->next) {
		struct sr_config *src = l->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn) {
			usb_match = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb_match = l->data;
				if (usb_match->bus == libusb_get_bus_number(devlist[i]) &&
				    usb_match->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				continue;
		}

		libusb_get_device_descriptor(devlist[i], &des);
		if (des.idVendor != H1008C_USB_VID || des.idProduct != H1008C_USB_PID)
			continue;
		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;

		sdi = g_malloc0(sizeof(*sdi));
		sdi->driver = &hantek_1008c_driver_info;
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup("Hantek");
		sdi->model = g_strdup("1008C (experimental)");
		sdi->connection_id = g_strdup(connection_id);
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
			libusb_get_device_address(devlist[i]), NULL);

		/* MVP intentionally exposes CH1 only. */
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "CH1");
		devc = g_malloc0(sizeof(*devc));
		sr_sw_limits_init(&devc->limits);
		devc->samplerate = H1008C_SAMPLERATE;
		sdi->priv = devc;
		devices = g_slist_append(devices, sdi);
	}

	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);
	libusb_free_device_list(devlist, 1);
	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	(void)cg;

	switch (key) {
	case SR_CONF_CONN: {
		struct sr_usb_dev_inst *usb = sdi->conn;
		if (!usb)
			return SR_ERR_ARG;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		return SR_OK;
	}
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->samplerate);
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
		const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	uint64_t rate;
	(void)cg;

	if (key == SR_CONF_LIMIT_SAMPLES)
		return sr_sw_limits_config_set(&devc->limits, key, data);
	if (key != SR_CONF_SAMPLERATE)
		return SR_ERR_NA;
	rate = g_variant_get_uint64(data);
	if (rate != H1008C_SAMPLERATE)
		return SR_ERR_SAMPLERATE;
	devc->samplerate = rate;
	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	if (key == SR_CONF_SAMPLERATE) {
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		return SR_OK;
	}
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	return h1008c_open(sdi);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return h1008c_close(sdi);
}

static int receive_frame(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	float *samples;
	size_t count;
	float zero;
	uint64_t remain;
	int ret;

	(void)fd;
	(void)revents;
	if (!devc->running)
		return TRUE;

	ret = h1008c_acquire_frame(sdi, &samples, &count, &zero);
	if (ret != SR_OK) {
		sr_err("Acquisition failed; stopping experimental Hantek 1008C stream.");
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	remain = count;
	if (devc->limits.limit_samples) {
		uint64_t left = devc->limits.limit_samples > devc->limits.samples_read ?
			devc->limits.limit_samples - devc->limits.samples_read : 0;
		remain = MIN((uint64_t)count, left);
	}
	if (!remain) {
		g_free(samples);
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.num_samples = remain;
	analog.data = samples;
	analog.meaning->mq = SR_MQ_COUNT;
	analog.meaning->unit = SR_UNIT_UNITLESS;
	analog.meaning->mqflags = 0;
	analog.meaning->channels = g_slist_append(NULL, sdi->channels->data);
	analog.encoding->digits = 0;
	analog.spec->spec_digits = 0;

	/*
	 * One physical 4000-sample acquisition burst is one truthful Sigrok
	 * frame. Do not concatenate bursts: the device has a substantial re-arm
	 * gap and each burst starts at an unrelated signal phase.
	 */
	std_session_send_df_frame_begin(sdi);
	sr_session_send(sdi, &packet);
	std_session_send_df_frame_end(sdi);
	g_slist_free(analog.meaning->channels);
	g_free(samples);

	devc->burst_count++;
	sr_sw_limits_update_samples_read(&devc->limits, remain);
	sr_sw_limits_update_frames_read(&devc->limits, 1);
	sr_dbg("burst=%" PRIu64 " frame=%" PRIu64 " samples=%" PRIu64
		" total=%" PRIu64 " delta-zero=%.3f",
		devc->burst_count, devc->limits.frames_read, remain,
		devc->limits.samples_read, zero);
	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);
	return TRUE;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_dev_inst *mutable_sdi = (struct sr_dev_inst *)sdi;

	/*
	 * The 1008C can logically disappear/re-enumerate after several seconds
	 * without protocol traffic. PulseView commonly keeps the device instance
	 * open between Run presses, leaving us with a stale libusb handle. Refresh
	 * the handle by physical USB port path at the start of every acquisition.
	 */
	if (h1008c_reopen(mutable_sdi) != SR_OK) {
		sr_err("Unable to reacquire Hantek 1008C USB connection.");
		return SR_ERR;
	}

	if (h1008c_startup(sdi) != SR_OK)
		return SR_ERR;
	devc->running = TRUE;
	devc->burst_count = 0;
	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);
	/* Synchronous MVP: each callback acquires one complete hardware burst. */
	return sr_session_source_add(sdi->session, -1, 0, 1,
		receive_frame, (struct sr_dev_inst *)sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (!devc->running)
		return SR_OK;
	devc->running = FALSE;
	sr_session_source_remove(sdi->session, -1);
	std_session_send_df_end(sdi);
	return SR_OK;
}

static struct sr_dev_driver hantek_1008c_driver_info = {
	.name = "hantek-1008c",
	.longname = "Hantek 1008C (experimental)",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(hantek_1008c_driver_info);
