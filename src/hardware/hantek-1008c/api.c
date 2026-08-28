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
#include <math.h>
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
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

struct h1008c_rate {
	uint64_t samplerate;
	uint8_t a3;
	enum h1008c_acquisition_mode mode;
};

/*
 * PulseView-facing CH1 rates.  Burst rates are hardware-validated.  Roll
 * rates are the public hantek1008py nominal rates multiplied by its documented
 * 4.56x one-active-channel factor, rounded to integer samples/s because
 * SR_CONF_SAMPLERATE is uint64.  Sub-1 sample/s settings are intentionally not
 * advertised.  Hardware validation of the roll rates remains required.
 */
static const struct h1008c_rate rate_table[] = {
	{ UINT64_C(1),       0x22, H1008C_MODE_ROLL },
	{ UINT64_C(2),       0x21, H1008C_MODE_ROLL },
	{ UINT64_C(5),       0x20, H1008C_MODE_ROLL },
	{ UINT64_C(9),       0x1f, H1008C_MODE_ROLL },
	{ UINT64_C(23),      0x1e, H1008C_MODE_ROLL },
	{ UINT64_C(50),      0x1d, H1008C_MODE_ROLL },
	{ UINT64_C(100),     0x1c, H1008C_MODE_ROLL },
	{ UINT64_C(201),     0x1b, H1008C_MODE_ROLL },
	{ UINT64_C(401),     0x1a, H1008C_MODE_ROLL },
	{ UINT64_C(1003),    0x19, H1008C_MODE_ROLL },
	{ UINT64_C(2006),    0x18, H1008C_MODE_ROLL },
	{ UINT64_C(800000),  0x11, H1008C_MODE_BURST },
	{ UINT64_C(2400000), 0x0f, H1008C_MODE_BURST },
};

static const uint64_t samplerates[] = {
	UINT64_C(1), UINT64_C(2), UINT64_C(5), UINT64_C(9), UINT64_C(23),
	UINT64_C(50), UINT64_C(100), UINT64_C(201), UINT64_C(401),
	UINT64_C(1003), UINT64_C(2006), UINT64_C(800000), UINT64_C(2400000),
};

static const struct h1008c_rate *find_rate(uint64_t samplerate)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(rate_table); i++) {
		if (rate_table[i].samplerate == samplerate)
			return &rate_table[i];
	}
	return NULL;
}


#define H1008C_BURST_FRAME_SAMPLES UINT64_C(4000)

static uint64_t effective_sample_limit(const struct dev_context *devc,
		uint64_t requested)
{
	uint64_t frames;

	if (!requested || devc->acquisition_mode != H1008C_MODE_BURST)
		return requested;

	/*
	 * BURST mode has an indivisible 4K hardware frame. Round the configured
	 * limit up here, at the configuration boundary, so frontends such as
	 * PulseView also learn the true capture size instead of allocating for
	 * (for example) 5000 samples and clipping the second 4K frame.
	 */
	frames = (requested + H1008C_BURST_FRAME_SAMPLES - 1) /
		H1008C_BURST_FRAME_SAMPLES;
	return frames * H1008C_BURST_FRAME_SAMPLES;
}

static int apply_sample_limit(struct dev_context *devc)
{
	GVariant *value;
	uint64_t effective;
	int ret;

	effective = effective_sample_limit(devc, devc->requested_limit_samples);
	value = g_variant_new_uint64(effective);
	ret = sr_sw_limits_config_set(&devc->limits, SR_CONF_LIMIT_SAMPLES, value);
	if (ret == SR_OK && effective != devc->requested_limit_samples)
		sr_info("Burst sample limit rounded from %" PRIu64
			" to %" PRIu64 " to preserve complete 4K frames.",
			devc->requested_limit_samples, effective);
	return ret;
}

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
		devc->a3 = H1008C_A3_24MSPS;
		devc->acquisition_mode = H1008C_MODE_BURST;
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
	case SR_CONF_LIMIT_FRAMES:
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
	const struct h1008c_rate *rate;
	uint64_t samplerate;
	(void)cg;

	if (key == SR_CONF_LIMIT_SAMPLES) {
		devc->requested_limit_samples = g_variant_get_uint64(data);
		return apply_sample_limit(devc);
	}
	if (key == SR_CONF_LIMIT_FRAMES)
		return sr_sw_limits_config_set(&devc->limits, key, data);
	if (key != SR_CONF_SAMPLERATE)
		return SR_ERR_NA;
	samplerate = g_variant_get_uint64(data);
	rate = find_rate(samplerate);
	if (!rate)
		return SR_ERR_SAMPLERATE;
	devc->samplerate = rate->samplerate;
	devc->a3 = rate->a3;
	devc->acquisition_mode = rate->mode;
	/*
	 * PulseView may set sample count and samplerate in either order. Re-apply
	 * the user's requested sample limit after mode selection so BURST always
	 * advertises an integral number of 4K frames while ROLL remains exact.
	 */
	if (apply_sample_limit(devc) != SR_OK)
		return SR_ERR;
	sr_info("Selected %" PRIu64 " samples/s: %s mode, A3=%02x.",
		devc->samplerate,
		devc->acquisition_mode == H1008C_MODE_ROLL ? "roll" : "burst",
		devc->a3);
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

static void load_persistent_calibration(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	GKeyFile *keyfile;
	GError *error;
	gchar *path, *section;
	gint version;
	double zero_adc, volts_per_count;

	devc->calibration_valid = FALSE;
	devc->calibration_zero_adc = 0.0;
	devc->calibration_volts_per_count = 0.0;

	path = g_build_filename(g_get_user_data_dir(), "hantek-1008c",
		"calibration.ini", NULL);
	keyfile = g_key_file_new();
	error = NULL;
	if (!g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, &error)) {
		if (error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			sr_warn("Unable to read calibration file %s: %s.", path, error->message);
		g_clear_error(&error);
		g_key_file_free(keyfile);
		g_free(path);
		return;
	}

	error = NULL;
	version = g_key_file_get_integer(keyfile, "format", "version", &error);
	if (error || version != 1) {
		sr_warn("Ignoring unsupported Hantek calibration format in %s.", path);
		g_clear_error(&error);
		g_key_file_free(keyfile);
		g_free(path);
		return;
	}

	section = g_strdup_printf("device %s channel CH1 range %02X",
		sdi->connection_id ? sdi->connection_id : "", H1008C_A2_RANGE_MVP);
	if (!g_key_file_has_group(keyfile, section)) {
		sr_warn("No persisted calibration for %s CH1 A2=%02X; using raw ADC counts.",
			sdi->connection_id ? sdi->connection_id : "unknown-port",
			H1008C_A2_RANGE_MVP);
		g_free(section);
		g_key_file_free(keyfile);
		g_free(path);
		return;
	}

	error = NULL;
	zero_adc = g_key_file_get_double(keyfile, section, "zero_adc", &error);
	if (error) {
		sr_warn("Invalid zero_adc in Hantek calibration section %s.", section);
		g_clear_error(&error);
		goto out;
	}
	volts_per_count = g_key_file_get_double(keyfile, section,
		"volts_per_count", &error);
	if (error || !isfinite(zero_adc) || !isfinite(volts_per_count) ||
		volts_per_count <= 0.0) {
		sr_warn("Invalid Hantek calibration values in section %s.", section);
		g_clear_error(&error);
		goto out;
	}

	devc->calibration_zero_adc = zero_adc;
	devc->calibration_volts_per_count = volts_per_count;
	devc->calibration_valid = TRUE;
	sr_info("Loaded calibration for CH1 A2=%02X: zero=%.3f, scale=%.9g V/count.",
		H1008C_A2_RANGE_MVP, zero_adc, volts_per_count);

out:
	g_free(section);
	g_key_file_free(keyfile);
	g_free(path);
}

static void calibrate_samples(const struct dev_context *devc,
		float *samples, size_t count)
{
	size_t i;

	if (!devc->calibration_valid)
		return;
	for (i = 0; i < count; i++)
		samples[i] = (float)((samples[i] - devc->calibration_zero_adc) *
			devc->calibration_volts_per_count);
}

static void send_analog_samples(struct sr_dev_inst *sdi, float *samples,
		size_t count, gboolean frame)
{
	struct dev_context *devc = sdi->priv;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.num_samples = count;
	analog.data = samples;
	if (devc->calibration_valid) {
		analog.meaning->mq = SR_MQ_VOLTAGE;
		analog.meaning->unit = SR_UNIT_VOLT;
		analog.encoding->digits = 3;
		analog.spec->spec_digits = 3;
	} else {
		analog.meaning->mq = SR_MQ_COUNT;
		analog.meaning->unit = SR_UNIT_PIECE;
		analog.encoding->digits = 0;
		analog.spec->spec_digits = 0;
	}
	analog.meaning->mqflags = 0;
	analog.meaning->channels = g_slist_append(NULL, sdi->channels->data);

	if (frame)
		std_session_send_df_frame_begin(sdi);
	sr_session_send(sdi, &packet);
	if (frame)
		std_session_send_df_frame_end(sdi);
	g_slist_free(analog.meaning->channels);
}

static int receive_samples(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;
	float *samples;
	size_t count;
	uint64_t remain;
	gboolean frame;
	int ret;

	(void)fd;
	(void)revents;
	if (!devc->running)
		return TRUE;

	frame = devc->acquisition_mode == H1008C_MODE_BURST;
	if (frame)
		ret = h1008c_acquire_frame(sdi, &samples, &count);
	else
		ret = h1008c_read_roll(sdi, &samples, &count);
	if (ret != SR_OK) {
		sr_err("Hantek 1008C %s acquisition failed; stopping.",
			frame ? "burst" : "roll");
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	/* C7 returning zero in roll mode simply means no samples are ready yet. */
	if (!count)
		return TRUE;

	remain = count;
	/*
	 * A burst is one physical acquisition frame. Never truncate a completed
	 * hardware frame merely to satisfy an aggregate sample limit: doing so
	 * makes the final sweep appear shortened in frontends such as PulseView.
	 * Let the software limit stop acquisition after the complete frame, even
	 * when that means samples_read exceeds the requested limit by < 4K.
	 *
	 * Roll mode is a genuine continuous stream, so it may stop exactly at the
	 * requested sample count.
	 */
	if (!frame && devc->limits.limit_samples) {
		uint64_t left = devc->limits.limit_samples > devc->limits.samples_read ?
			devc->limits.limit_samples - devc->limits.samples_read : 0;
		remain = MIN((uint64_t)count, left);
	}
	if (!remain) {
		g_free(samples);
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	calibrate_samples(devc, samples, remain);
	send_analog_samples(sdi, samples, remain, frame);
	g_free(samples);

	sr_sw_limits_update_samples_read(&devc->limits, remain);
	if (frame) {
		devc->burst_count++;
		sr_sw_limits_update_frames_read(&devc->limits, 1);
		sr_dbg("burst=%" PRIu64 " frame=%" PRIu64 " samples=%" PRIu64
			" total=%" PRIu64 " direct-adc",
			devc->burst_count, devc->limits.frames_read, remain,
			devc->limits.samples_read);
	} else {
		sr_dbg("roll samples=%" PRIu64 " total=%" PRIu64,
			remain, devc->limits.samples_read);
	}

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

	load_persistent_calibration(sdi);
	if (h1008c_startup(sdi,
		devc->acquisition_mode == H1008C_MODE_BURST ?
		devc->a3 : H1008C_A3_24MSPS) != SR_OK)
		return SR_ERR;
	devc->running = TRUE;
	devc->burst_count = 0;
	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);
	if (devc->acquisition_mode == H1008C_MODE_ROLL) {
		if (h1008c_start_roll(sdi, devc->a3) != SR_OK) {
			devc->running = FALSE;
			return SR_ERR;
		}
	}
	sr_info("Acquisition start: %" PRIu64 " samples/s, %s mode, A3=%02x.",
		devc->samplerate,
		devc->acquisition_mode == H1008C_MODE_ROLL ? "roll" : "burst",
		devc->a3);
	return sr_session_source_add(sdi->session, -1, 0,
		devc->acquisition_mode == H1008C_MODE_ROLL ? 10 : 1,
		receive_samples, (struct sr_dev_inst *)sdi);
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
