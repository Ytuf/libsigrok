/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 bkidwell <ben.kidwell@gmail.com>
 * Copyright (C) 2024 lukenuc <lukenuculaj@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libsigrok/libsigrok.h>
#include "protocol.h"

static struct sr_dev_driver fwili_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static const char *channel_names[] = {"SPI_CS#/UART_RTS/GPIO_27",
	"SPI_CLK/UART_CTS/GPIO_26", "SPI_MOSI/UART_RX/I2C_SCL",
	"SPI_MISO/UART_TX/I2C_SDA"};

static const uint64_t samplerates[] = {
	SR_KHZ(122),
	SR_KHZ(244),
	SR_KHZ(488),
	SR_KHZ(977),
	SR_MHZ(1.95),
	SR_MHZ(3.9)
};

static gboolean is_plausible(const struct libusb_device_descriptor* des)
{

	if (des->idVendor != FWILI_VID)
		return FALSE;
	if (des->idProduct != FWILI_PID)
		return FALSE;

	return TRUE;
}

static void scan_device(struct ftdi_context* ftdic, struct libusb_device* dev,
			GSList** devices)
{
	static const int usb_str_maxlen = 32;

	struct libusb_device_descriptor usb_desc;
	struct dev_context *devc;
	char *vendor, *model, *serial_num;
	struct sr_dev_inst *sdi;
	int rv;
	size_t i;

	libusb_get_device_descriptor(dev, &usb_desc);

	if (!(is_plausible(&usb_desc))) {
		sr_spew("Unsupported FTDI device 0x%04x:0x%04x.",
			usb_desc.idVendor, usb_desc.idProduct);
		return;
	}

	devc = g_malloc0(sizeof(struct dev_context));

	/* Allocate memory for the incoming data. */
	devc->raw_data_buf = g_malloc0(RAW_DATA_BUF_SIZE);
	devc->decoded_data_buf = g_malloc0(RAW_DATA_BUF_SIZE * RLE_SIZE);

	vendor = g_malloc(usb_str_maxlen);
	model = g_malloc(usb_str_maxlen);
	serial_num = g_malloc(usb_str_maxlen);
	rv = ftdi_usb_get_strings(ftdic, dev, vendor, usb_str_maxlen, model,
		usb_str_maxlen, serial_num, usb_str_maxlen);
	switch (rv) {
	case 0:
		break;
	/* ftdi_usb_get_strings() fails on first miss, hence fall through. */
	case -7:
		sr_dbg("The device lacks a manufacturer descriptor.");
		g_snprintf(vendor, usb_str_maxlen, "Generic");
	/* FALLTHROUGH */
	case -8:
		sr_dbg("The device lacks a product descriptor.");
		g_snprintf(model, usb_str_maxlen, "Unknown");
	/* FALLTHROUGH */
	case -9:
		sr_dbg("The device lacks a serial number.");
		g_free(serial_num);
		serial_num = NULL;
		break;
	default:
		sr_err("Failed to get the FTDI strings: %d", rv);
		goto err_free_strings;
	}

	sr_dbg("Found an FTDI device: %s.", model);
	if (strcmp(vendor, FWILI_VENDOR) != 0) {
		sr_dbg("Device is not recognized as a FREE WiLi.");
		goto err_free_strings;
	}
	if (strcmp(model, FWILI_MODEL) != 0) {
		sr_dbg("Device is not recognized as a FREE WiLi.");
		goto err_free_strings;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = vendor;
	sdi->model = model;
	sdi->serial_num = serial_num;
	sdi->priv = devc;
	sdi->connection_id = g_strdup_printf("d:%u/%u",
		libusb_get_bus_number(dev), libusb_get_device_address(dev));

	for (i = 0; i < ARRAY_SIZE(channel_names); i++)
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE,
			channel_names[i]);

	*devices = g_slist_append(*devices, sdi);
	return;

err_free_strings:
	g_free(vendor);
	g_free(model);
	g_free(serial_num);
	g_free(devc->raw_data_buf);
	g_free(devc->decoded_data_buf);
	g_free(devc);
}

static GSList* scan_all(struct ftdi_context* ftdic, GSList* options)
{
	GSList *devices;
	struct ftdi_device_list *devlist = 0;
	struct ftdi_device_list *curdev;
	int ret;

	(void)options;

	devices = NULL;

	ret = ftdi_usb_find_all(ftdic, &devlist, 0, 0);
	if (ret < 0) {
		sr_err("Failed to list devices (%d): %s", ret,
			ftdi_get_error_string(ftdic));
		return NULL;
	}

	curdev = devlist;
	while (curdev) {
		scan_device(ftdic, curdev->dev, &devices);
		curdev = curdev->next;
	}

	ftdi_list_free(&devlist);

	return devices;
}

static GSList* scan(struct sr_dev_driver* di, GSList* options)
{
	struct ftdi_context *ftdic;
	struct sr_config *src;
	struct sr_usb_dev_inst *usb;
	const char *conn;
	GSList *l, *conn_devices;
	GSList *devices;
	struct drv_context *drvc;
	libusb_device **devlist;
	int i;

	drvc = di->context;
	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	ftdic = ftdi_new();
	if (!ftdic) {
		sr_err("Failed to initialize libftdi.");
		return NULL;
	}

	if (conn) {
		devices = NULL;
		libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
		for (i = 0; devlist[i]; i++) {
			conn_devices =
				sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus ==
				    libusb_get_bus_number(devlist[i])
				    && usb->address ==
				    libusb_get_device_address(devlist[i])) {
					scan_device(ftdic, devlist[i], &devices);
				}
			}
		}
		libusb_free_device_list(devlist, 1);
	} else {
		devices = scan_all(ftdic, options);
	}

	ftdi_free(ftdic);

	return std_scan_complete(di, devices);
}

static void clear_helper(struct dev_context *devc)
{
	g_free(devc->raw_data_buf);
	g_free(devc->decoded_data_buf);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di,
		(std_dev_clear_callback)clear_helper);
}

static int dev_open(struct sr_dev_inst* sdi)
{
	struct dev_context *devc;
	int ret;

	ret = SR_OK;
	devc = sdi->priv;

	devc->ftdic = ftdi_new();
	if (!devc->ftdic)
		return SR_ERR;

	ftdi_init(devc->ftdic);
	if (!devc->ftdic)
		return SR_ERR;

	ret = ftdi_usb_open_string(devc->ftdic, sdi->connection_id);
	if (ret < 0) {
		sr_dbg("Failed ftdi_usb_open_string");
		/* Log errors, except for -3 ("device not found"). */
		if (ret != -3)
			sr_err("Failed to open device (%d): %s", ret,
				ftdi_get_error_string(devc->ftdic));
		goto err_ftdi_free;
	}

	ret = PURGE_FTDI_BOTH(devc->ftdic);
	if (ret < 0) {
		sr_err("Failed to purge FTDI RX/TX buffers (%d): %s.", ret,
			ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}

	if (devc->cur_samplerate == 0) {
		/* Samplerate hasn't been set; default to the fastest one. */
		devc->cur_samplerate = samplerates[5];
	}

	if (devc->capture_ratio == 0) {
		/* Capture ratio hasn't been set yet; default to 15%. */
		devc->capture_ratio = 15;
	}

	return SR_OK;

err_dev_open_close_ftdic:
	ftdi_usb_close(devc->ftdic);

err_ftdi_free:
	ftdi_free(devc->ftdic);

	return SR_ERR;
}

static int dev_close(struct sr_dev_inst* sdi)
{
	struct dev_context* devc;

	devc = sdi->priv;

	if (!devc->ftdic)
		return SR_ERR_BUG;

	ftdi_usb_close(devc->ftdic);
	ftdi_free(devc->ftdic);
	devc->ftdic = NULL;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant** data,
	const struct sr_dev_inst* sdi, const struct sr_channel_group* cg)
{
	struct dev_context* devc;
	struct sr_usb_dev_inst* usb;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_ENABLED:
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant* data,
	const struct sr_dev_inst* sdi, const struct sr_channel_group* cg)
{
	struct dev_context* devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_SAMPLERATE:
		devc->cur_samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_ENABLED:
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant** data,
	const struct sr_dev_inst* sdi, const struct sr_channel_group* cg)
{
	struct dev_context* devc;

	devc = (sdi) ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		if (cg)
			return SR_ERR_NA;
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts,
			drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		if (!devc)
			return SR_ERR_NA;
		*data = std_gvar_samplerates(samplerates,
			ARRAY_SIZE(samplerates));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst* sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	int res;

	devc = sdi->priv;

	if (!devc->ftdic)
		return SR_ERR_BUG;

	/* Properly reset internal variables before every new acquisition. */
	devc->samples_sent = 0;
	devc->bytes_received = 0;

	/* Trigger logic */
	if ((trigger = sr_session_trigger_get(sdi->session))) {
		int pre_trigger_samples = 0;
		if (devc->limit_samples > 0)
			pre_trigger_samples =
				(devc->capture_ratio * devc->limit_samples) /
				100;
		devc->stl = soft_trigger_logic_new(
			sdi, trigger, pre_trigger_samples);
		devc->stl->unitsize = 1;
		if (!devc->stl)
			return SR_ERR_MALLOC;
		devc->trigger_fired = FALSE;
	} else
		devc->trigger_fired = TRUE;

	/* Initialize the FTDI chip */
	res = ftdi_usb_reset(devc->ftdic);
	if (res != 0)
		sr_err("ftdi_usb_reset failed! Res = %d", res);
	res = ftdi_tcioflush(devc->ftdic);
	if (res != 0)
		sr_err("ftdi_tcioflush failed! Res = %d", res);
	res = ftdi_disable_bitbang(devc->ftdic);
	if (res != 0)
		sr_err("ftdi_disable_bitbang failed! Res = %d", res);
	ftdi_set_latency_timer(devc->ftdic, 255);
	if (res != 0)
		sr_err("ftdi_set_latency_timer failed! Res = %d", res);
	res = ftdi_set_bitmode(devc->ftdic, 0, BITMODE_RESET);
	if (res != 0)
		sr_err("ftdi_set_bitmode BITMODE_RESET failed! Res = %d", res);
	res = ftdi_set_bitmode(devc->ftdic, 0, BITMODE_FT1284);
	if (res != 0)
		sr_err("ftdi_set_bitmode BITMODE_FT1248 failed! Res = %d", res);
	res = ftdi_write_data_set_chunksize(devc->ftdic, 1024);
	if (res != 0)
		sr_err("ftdi_write_data_set_chunksize failed! Res = %d", res);
	res = ftdi_read_data_set_chunksize(devc->ftdic, 1024);
	if (res != 0)
		sr_err("ftdi_write_data_set_chunksize failed! Res = %d", res);

  /* Set and send the samplerate */
	if (devc->cur_samplerate == samplerates[5])
		cmd_samplerate[2] = 0x04;
	else if (devc->cur_samplerate == samplerates[4])
		cmd_samplerate[2] = 0x08;
	else if (devc->cur_samplerate == samplerates[3])
		cmd_samplerate[2] = 0x10;
	else if (devc->cur_samplerate == samplerates[2])
		cmd_samplerate[2] = 0x20;
	else if (devc->cur_samplerate == samplerates[1])
		cmd_samplerate[2] = 0x40;
	else if (devc->cur_samplerate == samplerates[0])
		cmd_samplerate[2] = 0x80;
	else
		cmd_samplerate[2] = 0x04;

	res = ftdi_write_data(devc->ftdic, cmd_samplerate, sizeof(cmd_samplerate));
	if (res < 0) {
		sr_err("Failed to write samplerate to fwili. Plese ensure "
			"the FPGA is programmed with the default logic "
			"analyzer application.");
		sr_dev_acquisition_stop(sdi);
	}

	/* Start the FPGA */
	res = ftdi_write_data(devc->ftdic, cmd_start, sizeof(cmd_start));
	if (res < 0) {
		sr_err("Failed to write start bit to fwili. Plese ensure "
			"the FPGA is programmed with the default logic "
			"analyzer application.");
		sr_dev_acquisition_stop(sdi);
	}

	/* Clear read buffer */
	ftdi_read_data(devc->ftdic, devc->raw_data_buf, RAW_DATA_BUF_SIZE);

	std_session_send_df_header(sdi);

	/* Hook up a dummy handler to receive data from the device. */
	sr_session_source_add(
		sdi->session, -1, G_IO_IN, 0, fwili_receive_data, (void*)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst* sdi)
{
	struct dev_context* devc;
	int res;

	devc = sdi->priv;

	/* Stop the FPGA */
	res = ftdi_write_data(devc->ftdic, cmd_stop, sizeof(cmd_stop));
	if (res < 0) {
		sr_err("Failed to stop hardware. Hardware must have power "
			"cycled.");
	}

	sr_session_source_remove(sdi->session, -1);

	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver fwili_driver_info = {
	.name = "fwili",
	.longname = "FREE WILi",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(fwili_driver_info);
