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

#ifndef LIBSIGROK_HARDWARE_FWILI_PROTOCOL_H
#define LIBSIGROK_HARDWARE_FWILI_PROTOCOL_H

#include <ftdi.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "fwili"

/* Device identification */
#define FWILI_VID (uint16_t)0x0403
#define FWILI_PID (uint16_t)0x6014
#define FWILI_VENDOR "Intrepid"
#define FWILI_MODEL "FreeWili"

/* 
 * Max number of samples that can be encoded
 * by hardware in a single byte (RLE)
 */
#define RLE_SIZE 16

/* Software data buffer */
#define RAW_DATA_BUF_SIZE (1024)

/* Hardware register definitions */
#define LA_ADDR 0x00
#define CONTROL_REG 0x80
#define SAMPLERATE_REG 0x81
#define START_BIT 0x01
#define STOP_BIT 0x02
#define CLR_BIT 0x04

/* Hardware command definitions */
static const unsigned char cmd_start[] = {LA_ADDR, CONTROL_REG, START_BIT};
static const unsigned char cmd_stop[] = {LA_ADDR, CONTROL_REG, STOP_BIT};
static unsigned char cmd_samplerate[] = {LA_ADDR, SAMPLERATE_REG, 0x00};

struct dev_context {
	struct ftdi_context* ftdic;

	unsigned char *raw_data_buf;
	unsigned char *decoded_data_buf;
	uint64_t samples_sent;
	uint64_t bytes_received;

	uint64_t cur_samplerate;
	uint64_t limit_samples;

	uint64_t capture_ratio;

	struct soft_trigger_logic *stl;
	gboolean trigger_fired;
};

SR_PRIV int fwili_dev_open(struct sr_dev_inst* sdi, struct sr_dev_driver* di);
SR_PRIV struct dev_context* fwili_dev_new(void);
SR_PRIV int fwili_start_acquisition(const struct sr_dev_inst* sdi);
SR_PRIV void fwili_abort_acquisition(struct dev_context* devc);
SR_PRIV int fwili_receive_data(int fd, int revents, void* cb_data);

#endif
