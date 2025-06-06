// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include "synaptics_tcm_S3910.h"
#include <linux/platform_data/spi-mt65xx.h>

DECLARE_COMPLETION(response_complete);
DECLARE_COMPLETION(report_complete);

extern int tp_register_times;
static struct syna_tcm_data *g_tcm_info;

extern struct device_hcd *syna_remote_device_S3910_init(struct syna_tcm_data *tcm_info);
extern int syna_remote_device_S3910_destory(struct syna_tcm_data *tcm_info);
static void syna_main_register(struct seq_file *s, void *chip_data);

const struct mtk_chip_config st_spi_ctrdata_S3910 = {
	.sample_sel = 0,
	.cs_setuptime = 5000,
	.cs_holdtime = 3000,
	.cs_idletime = 0,
	.tick_delay = 0,
};
int block_delay_us = 0;
int byte_delay_us = 0;
static unsigned char *buf;
static unsigned int buf_size;
static struct spi_transfer *xfer;

static int syna_tcm_write_message(struct syna_tcm_data *tcm_info,
								  unsigned char command, unsigned char *payload,
								  unsigned int length, unsigned char **resp_buf,
								  unsigned int *resp_buf_size, unsigned int *resp_length,
								  unsigned int polling_delay_ms);
static void syna_tcm_test_report(struct syna_tcm_data *tcm_info);
static int syna_tcm_helper(struct syna_tcm_data *tcm_info);
static int syna_tcm_enable_report(struct syna_tcm_data *tcm_info, enum report_type report_type, bool enable);
static void syna_tcm_fw_update_in_bl(void);

static int syna_tcm_spi_alloc_mem(struct syna_tcm_data *tcm_hcd,
				  unsigned int count, unsigned int size)
{
	static unsigned int xfer_count;

	if (count > xfer_count) {
		kfree(xfer);
		xfer = kcalloc(count, sizeof(*xfer), GFP_KERNEL);
		if (!xfer) {
			TPD_INFO("Failed to allocate memory for xfer\n");
			xfer_count = 0;
			return -ENOMEM;
		}
		xfer_count = count;
	} else {
		memset(xfer, 0, count * sizeof(*xfer));
	}

	if (size > buf_size) {
		if (buf_size) {
			kfree(buf);
		}
		buf = kmalloc(size, GFP_KERNEL);
		if (!buf) {
			TPD_INFO("Failed to allocate memory for buf\n");
			buf_size = 0;
			return -ENOMEM;
		}
		buf_size = size;
	}

	return 0;
}

/**
 * syna_tcm_read - read data from TP IC
 *
 * @tcm_hcd: IC Information
 * @data: Store read data
 * @length: read data lenght
 * Write data to TP IC through SPI
 */
static inline int syna_tcm_read(struct syna_tcm_data *tcm_hcd,
					unsigned char *data, unsigned int length)
{
	int retval;
	unsigned int idx;
	struct spi_message msg;
	struct spi_device *spi = tcm_hcd->client;
	spi_message_init(&msg);

	if (byte_delay_us == 0) {
		retval = syna_tcm_spi_alloc_mem(tcm_hcd, 1, length);
	} else {
		retval = syna_tcm_spi_alloc_mem(tcm_hcd, length, 1);
	}
	if (retval < 0) {
		TPD_INFO("Failed to allocate memory\n");
		goto exit;
	}

	if (byte_delay_us == 0) {
		memset(buf, 0xff, length);
		xfer[0].len = length;
		xfer[0].tx_buf = buf;
		xfer[0].rx_buf = data;
		if (block_delay_us) {
			xfer[0].delay_usecs = block_delay_us;
		}
		spi_message_add_tail(&xfer[0], &msg);
	} else {
		buf[0] = 0xff;
		for (idx = 0; idx < length; idx++) {
			xfer[idx].len = 1;
			xfer[idx].tx_buf = buf;
			xfer[idx].rx_buf = &data[idx];
			xfer[idx].delay_usecs = byte_delay_us;
			if (block_delay_us && (idx == length - 1)) {
				xfer[idx].delay_usecs = block_delay_us;
			}
			spi_message_add_tail(&xfer[idx], &msg);
		}
	}
	retval = spi_sync(spi, &msg);
	if (retval == 0) {
		retval = length;
	} else {
		TPD_INFO("Failed to complete SPI transfer, error = %d\n", retval);
	}
exit:
	return retval;
}

/**
 * syna_tcm_write - write data to TP IC
 *
 * @tcm_hcd: IC Information
 * @data: Store write data
 * @length: write data lenght
 * Write data to TP IC through SPI
 */
static inline int syna_tcm_write(struct syna_tcm_data *tcm_hcd,
	unsigned char *data, unsigned int length)
{
	int retval;
	unsigned int idx;
	struct spi_message msg;
	struct spi_device *spi = tcm_hcd->client;
	spi_message_init(&msg);

	if (byte_delay_us == 0) {
		retval = syna_tcm_spi_alloc_mem(tcm_hcd, 1, 0);
	} else {
		retval = syna_tcm_spi_alloc_mem(tcm_hcd, length, 0);
	}
	if (retval < 0) {
		TPD_INFO("Failed to allocate memory\n");
		goto exit;
	}

	if (byte_delay_us == 0) {
		xfer[0].len = length;
		xfer[0].tx_buf = data;
		if (block_delay_us) {
			xfer[0].delay_usecs = block_delay_us;
		}
		spi_message_add_tail(&xfer[0], &msg);
	} else {
		for (idx = 0; idx < length; idx++) {
			xfer[idx].len = 1;
			xfer[idx].tx_buf = &data[idx];
			xfer[idx].delay_usecs = byte_delay_us;
			if (block_delay_us && (idx == length - 1)) {
					  xfer[idx].delay_usecs = block_delay_us;
			}
			spi_message_add_tail(&xfer[idx], &msg);
		}
	}
	retval = spi_sync(spi, &msg);
	if (retval == 0) {
		retval = length;
	} else {
		TPD_INFO("Failed to complete SPI transfer, error = %d\n", retval);
	}
exit:
	return retval;
}

/**
 * syna_get_report_data - Retrieve data from touch report
 *
 * @tcm_info: handle of tcm module
 * @offset: start bit of retrieved data
 * @bits: total bits of retrieved data
 * @data: pointer of data, at most 4 byte
 * Retrieve data from the touch report based on the bit offset and bit length
 * information from the touch report configuration.
 */
static int syna_get_report_data(struct syna_tcm_data *tcm_info, unsigned int offset,
								unsigned int bits, unsigned int *data)
{
	unsigned char mask = 0;
	unsigned char byte_data = 0;
	unsigned int output_data = 0;
	unsigned int bit_offset = offset % 8;
	unsigned int byte_offset = offset / 8;
	unsigned int data_bits = 0;
	unsigned int available_bits = 0;
	unsigned int remaining_bits = bits;
	unsigned char *touch_report = tcm_info->report.buffer.buf;
	int retval = 0;

	if (bits == 0 || bits > 32) {
		TPD_DEBUG("larger than 32 bits:%d\n", bits);
		retval = secure_memcpy((unsigned char *)data, bits / 8, &touch_report[byte_offset], bits / 8, bits / 8);
		if (retval < 0) {
			TPD_INFO("Failed to copy report data\n");
			return retval;
		}
		return 0;
	}

	if (offset + bits > tcm_info->report.buffer.data_length * 8) {
		TPD_DEBUG("offset and bits beyond total read length\n");
		*data = 0;
		return 0;
	}

	while (remaining_bits) {
		byte_data = touch_report[byte_offset];
		byte_data >>= bit_offset;

		available_bits = 8 - bit_offset;
		data_bits = MIN(available_bits, remaining_bits);
		mask = 0xff >> (8 - data_bits);

		byte_data &= mask;

		output_data |= byte_data << (bits - remaining_bits);

		bit_offset = 0;
		byte_offset += 1;
		remaining_bits -= data_bits;
	}

	*data = output_data;

	return 0;
}

/**
 * touch_parse_report() - Parse touch report
 *
 * Traverse through the touch report configuration and parse the touch report
 * generated by the device accordingly to retrieve the touch data.
 */
static int syna_parse_report(struct syna_tcm_data *tcm_info)
{
	int i = 0;
	int retval = 0;
	bool active_only = false, num_of_active_objects = false;
	unsigned char code;
	unsigned int size = 0, idx = 0, obj = 0;
	unsigned int next = 0, data = 0, bits = 0, offset = 0, objects = 0;
	unsigned char  grip_data[4];
	unsigned int active_objects = 0;
	unsigned int report_size = 0, config_size = 0;
	unsigned char *config_data = NULL;
	struct touch_hcd *touch_hcd = NULL;
	struct touch_data *touch_data = NULL;
	struct object_data *object_data = NULL;
	static unsigned int end_of_foreach = 0;

	touch_hcd = tcm_info->touch_hcd;
	touch_data = &touch_hcd->touch_data;
	object_data = touch_hcd->touch_data.object_data;
	config_data = tcm_info->config.buf;
	config_size = tcm_info->config.data_length;
	report_size = tcm_info->report.buffer.data_length;
	size = sizeof(*object_data) * touch_hcd->max_objects;
	memset(touch_hcd->touch_data.object_data, 0x00, size);

	while (idx < config_size) {
		code = config_data[idx++];
		switch (code) {
		case TOUCH_END:
			goto exit;
		case TOUCH_FOREACH_ACTIVE_OBJECT:
			obj = 0;
			next = idx;
			active_only = true;
			break;
		case TOUCH_FOREACH_OBJECT:
			obj = 0;
			next = idx;
			active_only = false;
			break;
		case TOUCH_FOREACH_END:
			end_of_foreach = idx;
			if (active_only) {
				if (num_of_active_objects) {
					objects++;
					if (objects < active_objects)
						idx = next;
				} else if (offset < report_size * 8) {
						idx = next;
				}
			} else {
				obj++;
				if (obj < touch_hcd->max_objects)
					idx = next;
			}
			break;
		case TOUCH_PAD_TO_NEXT_BYTE:
			offset = ceil_div(offset, 8) * 8;
			break;
		case TOUCH_TIMESTAMP:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get timestamp\n");
				return retval;
			}
			touch_data->timestamp = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_INDEX:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &obj);
			if (retval < 0) {
				TPD_INFO("Failed to get object index\n");
				return retval;
			}
			offset += bits;
			break;
		case TOUCH_OBJECT_N_CLASSIFICATION:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (tcm_info->ts->palm_to_sleep_enable
				&& data == PALM_FLAG
				&& !tcm_info->ts->is_suspended) {
				TPD_DEBUG("%s:detect palm,now to sleep\n", __func__);
				tcm_info->palm_to_sleep_state = PALM_TO_SLEEP;
			} else if (data == PALM_FLAG) {
				tcm_info->palm_hold_report = 1;
			} else if (data == 0) {
				tcm_info->palm_hold_report = 0;
			}
			if (retval < 0) {
				TPD_INFO("Failed to get object classification\n");
				return retval;
			}
			if(obj >= touch_hcd->max_objects) {
				TPD_INFO("obj> max_obj!! obj[%d]Report Data[%d]:", obj, report_size);
				if (tp_debug != 0) {
					for (i = 0; i < report_size; i++) {
						TPD_INFO("syna data:[0x%2x]", tcm_info->report.buffer.buf[i]);
					}
				}
				return -1;
			}
			object_data[obj].status = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_X_POSITION:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get object x position\n");
				return retval;
			}
			object_data[obj].x_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Y_POSITION:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get object y position\n");
				return retval;
			}
			object_data[obj].y_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Z:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get object z\n");
				return retval;
			}
			object_data[obj].z = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_X_WIDTH:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get object x width\n");
				return retval;
			}
			object_data[obj].x_width = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Y_WIDTH:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get object y width\n");
				return retval;
			}
			object_data[obj].y_width = data;
			offset += bits;
			break;
		case  TOUCH_REPORT_CUSTOMER_GRIP_INFO:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, (unsigned int *)(&grip_data[0]));
			if (retval < 0) {
				TPD_INFO("Failed to get Grip info\n");
				return retval;
			}
			object_data[obj].eywidth  = grip_data[0];
			object_data[obj].exwidth  = grip_data[1];
			object_data[obj].yeratio  = grip_data[2];
			object_data[obj].xeratio  = grip_data[3];
			offset += bits;
			break;
		case TOUCH_OBJECT_N_TX_POSITION_TIXELS:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get object tx position\n");
				return retval;
			}
			object_data[obj].tx_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_RX_POSITION_TIXELS:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get object rx position\n");
				return retval;
			}
			object_data[obj].rx_pos = data;
			offset += bits;
			break;
		case TOUCH_0D_BUTTONS_STATE:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get 0D buttons state\n");
				return retval;
			}
			touch_data->buttons_state = data;
			offset += bits;
			break;
		case TOUCH_GESTURE_DOUBLE_TAP:
		case TOUCH_REPORT_GESTURE_SWIPE:
		case TOUCH_REPORT_GESTURE_CIRCLE:
		case TOUCH_REPORT_GESTURE_UNICODE:
		case TOUCH_REPORT_GESTURE_VEE:
		case TOUCH_REPORT_GESTURE_TRIANGLE:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get gesture double tap\n");
				return retval;
			}
			touch_data->lpwg_gesture = data;
			offset += bits;
			break;
		case TOUCH_REPORT_GESTURE_INFO:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, (unsigned int *)(&touch_data->extra_gesture_info[0]));
			if (retval < 0) {
				TPD_INFO("Failed to get gesture double tap\n");
				return retval;
			}
			offset += bits;
			break;
		case TOUCH_REPORT_GESTURE_COORDINATE:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, (unsigned int *)(&touch_data->data_point[0]));
			if (retval < 0) {
				TPD_INFO("Failed to get gesture double tap\n");
				return retval;
			}
			offset += bits;
			break;
		case TOUCH_FRAME_RATE:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get frame rate\n");
				return retval;
			}
			touch_data->frame_rate = data;
			offset += bits;
			break;
		case TOUCH_POWER_IM:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get power IM\n");
				return retval;
			}
			touch_data->power_im = data;
			offset += bits;
			break;
		case TOUCH_CID_IM:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get CID IM\n");
				return retval;
			}
			touch_data->cid_im = data;
			offset += bits;
			break;
		case TOUCH_RAIL_IM:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get rail IM\n");
				return retval;
			}
			touch_data->rail_im = data;
			offset += bits;
			break;
		case TOUCH_CID_VARIANCE_IM:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get CID variance IM\n");
				return retval;
			}
			touch_data->cid_variance_im = data;
			offset += bits;
			break;
		case TOUCH_NSM_FREQUENCY:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get NSM frequency\n");
				return retval;
			}
			touch_data->nsm_frequency = data;
			offset += bits;
			break;
		case TOUCH_NSM_STATE:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get NSM state\n");
				return retval;
			}
			touch_data->nsm_state = data;
			offset += bits;
			break;
		case TOUCH_NUM_OF_ACTIVE_OBJECTS:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get number of active objects\n");
				return retval;
			}
			active_objects = data;
			num_of_active_objects = true;
			touch_data->num_of_active_objects = data;
			offset += bits;
			if (touch_data->num_of_active_objects == 0)
				idx = end_of_foreach;
			break;
		case TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (retval < 0) {
				TPD_INFO("Failed to get number of CPU cycles used since last frame\n");
				return retval;
			}
			touch_data->num_of_cpu_cycles = data;
			offset += bits;
			break;
		case TOUCH_TUNING_GAUSSIAN_WIDTHS:
			bits = config_data[idx++];
			offset += bits;
			break;
		case TOUCH_TUNING_SMALL_OBJECT_PARAMS:
			bits = config_data[idx++];
			offset += bits;
			break;
		case TOUCH_TUNING_0D_BUTTONS_VARIANCE:
			bits = config_data[idx++];
			offset += bits;
			break;
		}
	}

exit:
	return 0;
}

static void syna_get_diff_data_record(struct syna_tcm_data *tcm_info)
{
	int tx_num = tcm_info->hw_res->TX_NUM;
	int rx_num = tcm_info->hw_res->RX_NUM;
	int i = 0, j = 0;
	u8 *pdata_8;
	char buf[200];

	TPD_INFO("report size:%d, report length:%d,\n", tcm_info->report.buffer.buf_size, tcm_info->report.buffer.data_length);
	pdata_8 = &tcm_info->report.buffer.buf[0];

	if (tcm_info->report.buffer.data_length > SYNA_TCM_DIFF_BUF_LENGTH || tcm_info->report.buffer.data_length != (2 * (tx_num * rx_num + tx_num + rx_num)) || \
			(tx_num > 49) || (rx_num > 49)) {
		TPD_INFO("report length %d tx_num:%d rx_num:%d error\n", tcm_info->report.buffer.data_length, tx_num, rx_num);
	} else if (tcm_info->differ_read_every_frame) {
		memset(buf, 0, 200 * sizeof(char));
		TPD_DEBUG("diff data\n");
		for (i = 0; i < tx_num; i++) {
			for (j = 0; j < rx_num; j++) {
				snprintf(&buf[4 * j], 5, "%02x%02x", pdata_8[0], pdata_8[1]);
				pdata_8 += 2;
			}
			TPD_DEBUG("diff_record:[%2d]%s", i, buf);
		}
		TPD_DEBUG("sc_nomal diff data:\n");
		for (i = 0; i < rx_num; i++) {
			snprintf(&buf[4 * i], 5, "%02x%02x", pdata_8[0], pdata_8[1]);
			pdata_8 += 2;
		}
		TPD_DEBUG("diff_record:[RX]%s", buf);
		for (i = 0; i < tx_num; i++) {
			snprintf(&buf[4 * i], 5, "%02x%02x", pdata_8[0], pdata_8[1]);
			pdata_8 += 2;
		}
		TPD_DEBUG("diff_record:[TX]%s", buf);
		TPD_DEBUG("end\n");
	} else {
		TPD_DEBUG("differ_read_every_frame is false\n");
	}
	return;
}

static int syna_get_input_params(struct syna_tcm_data *tcm_info)
{
	int retval;

	LOCK_BUFFER(tcm_info->config);

	retval = syna_tcm_write_message(tcm_info, CMD_GET_TOUCH_REPORT_CONFIG,
									NULL, 0, &tcm_info->config.buf, &tcm_info->config.buf_size, &tcm_info->config.data_length, 0);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_GET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(tcm_info->config);
		return retval;
	}

	UNLOCK_BUFFER(tcm_info->config);

	return 0;
}

static int syna_set_default_report_config(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	int length = 0;

	LOCK_BUFFER(tcm_info->config);

	length = tcm_info->default_config.buf_size;

	if (tcm_info->default_config.buf) {
		retval = syna_tcm_alloc_mem(&tcm_info->config, length);
		if (retval < 0) {
			TPD_INFO("Failed to alloc mem\n");
			goto exit;
		}

		memcpy(tcm_info->config.buf, tcm_info->default_config.buf, length);
		tcm_info->config.buf_size = tcm_info->default_config.buf_size;
		tcm_info->config.data_length = tcm_info->default_config.data_length;
	}

exit:
	UNLOCK_BUFFER(tcm_info->config);

	return retval;
}

static int syna_get_default_report_config(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	unsigned int length;

	length = le2_to_uint(tcm_info->app_info.max_touch_report_config_size);

	LOCK_BUFFER(tcm_info->default_config);

	retval = syna_tcm_write_message(tcm_info,
									CMD_GET_TOUCH_REPORT_CONFIG,
									NULL,
									0,
									&tcm_info->default_config.buf,
									&tcm_info->default_config.buf_size,
									&tcm_info->default_config.data_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_GET_TOUCH_REPORT_CONFIG));
		goto exit;
	}

exit:
	UNLOCK_BUFFER(tcm_info->default_config);
	return retval;
}

static int syna_set_normal_report_config(struct syna_tcm_data *tcm_info)
{
	int retval;
	unsigned int idx = 0;
	unsigned int length;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;

	TPD_DEBUG("%s:set normal report\n", __func__);
	length = le2_to_uint(tcm_info->app_info.max_touch_report_config_size);

	if (length < TOUCH_REPORT_CONFIG_SIZE) {
		TPD_INFO("Invalid maximum touch report config size\n");
		return -EINVAL;
	}

	LOCK_BUFFER(touch_hcd->out);

	retval = syna_tcm_alloc_mem(&touch_hcd->out, length);
	if (retval < 0) {
		TPD_INFO("Failed to allocate memory for touch_hcd->out.buf\n");
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	touch_hcd->out.buf[idx++] = TOUCH_GESTURE_DOUBLE_TAP;
	touch_hcd->out.buf[idx++] = 8;

	if (0 == tcm_info->normal_config_version) {
		touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_INFO;
		touch_hcd->out.buf[idx++] = 48;
	} else if (2 == tcm_info->normal_config_version) {
		touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_INFO;
		touch_hcd->out.buf[idx++] = 48;
		touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_COORDINATE;
		touch_hcd->out.buf[idx++] = 192;
	}
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_ACTIVE_OBJECT;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_INDEX;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_CLASSIFICATION;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_WIDTH;
	touch_hcd->out.buf[idx++] = 12;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_WIDTH;
	touch_hcd->out.buf[idx++] = 12;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_CUSTOMER_GRIP_INFO;
	touch_hcd->out.buf[idx++] = 32;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_END;
	touch_hcd->out.buf[idx++] = TOUCH_END;

	LOCK_BUFFER(touch_hcd->resp);

	retval = syna_tcm_write_message(tcm_info,
									CMD_SET_TOUCH_REPORT_CONFIG,
									touch_hcd->out.buf,
									length,
									&touch_hcd->resp.buf,
									&touch_hcd->resp.buf_size,
									&touch_hcd->resp.data_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_SET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(touch_hcd->resp);
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	UNLOCK_BUFFER(touch_hcd->resp);
	UNLOCK_BUFFER(touch_hcd->out);

	return retval;
}

static int syna_set_gesture_report_config(struct syna_tcm_data *tcm_info)
{
	int retval;
	unsigned int idx = 0;
	unsigned int length;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;

	TPD_DEBUG("%s: set gesture report\n", __func__);
	length = le2_to_uint(tcm_info->app_info.max_touch_report_config_size);

	if (length < TOUCH_REPORT_CONFIG_SIZE) {
		TPD_INFO("Invalid maximum touch report config size\n");
		return -EINVAL;
	}

	LOCK_BUFFER(touch_hcd->out);

	retval = syna_tcm_alloc_mem(&touch_hcd->out, length);
	if (retval < 0) {
		TPD_INFO("Failed to allocate memory for touch_hcd->out.buf\n");
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	touch_hcd->out.buf[idx++] = TOUCH_GESTURE_DOUBLE_TAP;
	touch_hcd->out.buf[idx++] = 8;
	/* touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_CIRCLE;*/
	/* touch_hcd->out.buf[idx++] = 1;
	// touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_SWIPE;
	// touch_hcd->out.buf[idx++] = 1;
	// touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_UNICODE;
	// touch_hcd->out.buf[idx++] = 1;
	// touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_VEE;
	// touch_hcd->out.buf[idx++] = 1;
	// touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_TRIANGLE;
	// touch_hcd->out.buf[idx++] = 1;
	// touch_hcd->out.buf[idx++] = TOUCH_PAD_TO_NEXT_BYTE;*/
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_INFO;
	touch_hcd->out.buf[idx++] = 48;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_COORDINATE;
	touch_hcd->out.buf[idx++] = 192;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_ACTIVE_OBJECT;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_INDEX;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_CLASSIFICATION;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_END;
	touch_hcd->out.buf[idx++] = TOUCH_END;

	LOCK_BUFFER(touch_hcd->resp);

	retval = syna_tcm_write_message(tcm_info,
									CMD_SET_TOUCH_REPORT_CONFIG,
									touch_hcd->out.buf,
									length,
									&touch_hcd->resp.buf,
									&touch_hcd->resp.buf_size,
									&touch_hcd->resp.data_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_SET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(touch_hcd->resp);
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	UNLOCK_BUFFER(touch_hcd->resp);
	UNLOCK_BUFFER(touch_hcd->out);

	return 0;
}

int syna_set_input_reporting(struct syna_tcm_data *tcm_info, bool suspend)
{
	int retval = 0;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;

	TPD_DEBUG("%s: mode 0x%x, state %d\n", __func__, tcm_info->id_info.mode, suspend);
	if (tcm_info->id_info.mode != MODE_APPLICATION || tcm_info->app_status != APP_STATUS_OK) {
		TPD_INFO("Application firmware not running\n");
		return 0;
	}

	touch_hcd->report_touch = false;

	mutex_lock(&touch_hcd->report_mutex);

	if (!suspend) {
		retval = syna_set_normal_report_config(tcm_info);
		if (retval < 0) {
			TPD_INFO("Failed to set report config\n");
			goto default_config;
		}
	} else {
		retval = syna_set_gesture_report_config(tcm_info);
		if (retval < 0) {
			TPD_INFO("Failed to set report config\n");
			goto default_config;
		}
	}

	retval = syna_get_input_params(tcm_info);
	if (retval < 0) {
		TPD_INFO("Failed to get input parameters\n");
	}

	goto exit;

default_config:
	/*if failed to set report config, use default report config */
	retval = syna_set_default_report_config(tcm_info);
	if (retval < 0) {
		TPD_INFO("Failed to set default report config");
	}

exit:
	mutex_unlock(&touch_hcd->report_mutex);

	touch_hcd->report_touch = retval < 0 ? false : true;

	return retval;
}

static void syna_set_trigger_reason(struct syna_tcm_data *tcm_info, irq_reason trigger_reason)
{
	SET_BIT(tcm_info->trigger_reason, trigger_reason);
	if (tcm_info->cb.invoke_common)
		tcm_info->cb.invoke_common();

	tcm_info->trigger_reason = 0;
}

static void syna_tcm_resize_chunk_size(struct syna_tcm_data *tcm_info)
{
	unsigned int max_write_size;

	max_write_size = le2_to_uint(tcm_info->id_info.max_write_size);
	tcm_info->wr_chunk_size = MIN(max_write_size, WR_CHUNK_SIZE);
	if (tcm_info->wr_chunk_size == 0)
		tcm_info->wr_chunk_size = max_write_size;
}

/**
 * syna_tcm_dispatch_report() - dispatch report received from device
 *
 * @tcm_info: handle of core module
 *
 * The report generated by the device is forwarded to the synchronous inbox of
 * each registered application module for further processing. In addition, the
 * report notifier thread is woken up for asynchronous notification of the
 * report occurrence.
 */
static void syna_tcm_dispatch_report(struct syna_tcm_data *tcm_info)
{
	int ret = 0;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	struct touch_data *touch_data = &touch_hcd->touch_data;

	LOCK_BUFFER(tcm_info->in);
	LOCK_BUFFER(tcm_info->report.buffer);

	tcm_info->report.buffer.buf = &tcm_info->in.buf[MESSAGE_HEADER_SIZE];
	tcm_info->report.buffer.buf_size = tcm_info->in.buf_size - MESSAGE_HEADER_SIZE;
	tcm_info->report.buffer.data_length = tcm_info->payload_length;
	tcm_info->report.id = tcm_info->report_code;

	TPD_DEBUG("%s: buf:%02x,%02x,%02x,%02x\n", __func__, tcm_info->in.buf[0], tcm_info->in.buf[1], tcm_info->in.buf[2], tcm_info->in.buf[3]);

	if (tcm_info->report.id == REPORT_TOUCH) {
		ret = syna_parse_report(tcm_info);
		if (ret < 0) {
			TPD_INFO("Failed to parse report\n");
			goto exit;
		}

		if (*tcm_info->in_suspend) {
			if ((touch_data->lpwg_gesture == TOUCH_HOLD_UP) || (touch_data->lpwg_gesture == TOUCH_HOLD_DOWN)) {
					syna_set_trigger_reason(tcm_info, IRQ_FINGERPRINT);
					goto exit;
			}
			syna_set_trigger_reason(tcm_info, IRQ_GESTURE);
		} else {
			syna_set_trigger_reason(tcm_info, IRQ_TOUCH);
			if (tcm_info->palm_to_sleep_state == PALM_TO_SLEEP) {
				syna_set_trigger_reason(tcm_info, IRQ_PALM);
				tcm_info->palm_to_sleep_state = PALM_TO_DEFAULT;
				TPD_DEBUG("%s:PALM_TO_DEFAULT\n", __func__);
			}
			if ((touch_data->lpwg_gesture == TOUCH_HOLD_UP) || (touch_data->lpwg_gesture == TOUCH_HOLD_DOWN)) {
				syna_set_trigger_reason(tcm_info, IRQ_FINGERPRINT);
				goto exit;
			}
		}
	} else if (tcm_info->report.id == REPORT_IDENTIFY) {
		if (tcm_info->cb.async_work && tcm_info->id_info.mode == MODE_APPLICATION)
			tcm_info->cb.async_work();
	} else if (tcm_info->report.id == REPORT_TOUCH_HOLD) {
		syna_set_trigger_reason(tcm_info, IRQ_FINGERPRINT);

	} else if (tcm_info->report.id == REPORT_DIFF) {
		syna_get_diff_data_record(tcm_info);

	} else if (tcm_info->report.id == REPORT_LOG) {
		syna_set_trigger_reason(tcm_info, IRQ_FW_HEALTH);
	} else {
		syna_tcm_test_report(tcm_info);
	}

exit:
	UNLOCK_BUFFER(tcm_info->report.buffer);
	UNLOCK_BUFFER(tcm_info->in);
	return;
}


/**
 * syna_tcm_dispatch_response() - dispatch response received from device
 *
 * @tcm_info: handle of core module
 *
 * The response to a command is forwarded to the sender of the command.
 */
static void syna_tcm_dispatch_response(struct syna_tcm_data *tcm_info)
{
	int retval = 0;

	if (atomic_read(&tcm_info->command_status) != CMD_BUSY)
		return;

	LOCK_BUFFER(tcm_info->resp);

	if (tcm_info->payload_length == 0) {
		UNLOCK_BUFFER(tcm_info->resp);
		atomic_set(&tcm_info->command_status, CMD_IDLE);
		goto exit;
	}

	retval = syna_tcm_alloc_mem(&tcm_info->resp, tcm_info->payload_length);
	if (retval < 0) {
		TPD_INFO("Failed to allocate memory for tcm_info->resp.buf\n");
		UNLOCK_BUFFER(tcm_info->resp);
		atomic_set(&tcm_info->command_status, CMD_ERROR);
		goto exit;
	}

	LOCK_BUFFER(tcm_info->in);

	retval = secure_memcpy(tcm_info->resp.buf, tcm_info->resp.buf_size,
						   &tcm_info->in.buf[MESSAGE_HEADER_SIZE], tcm_info->in.buf_size - MESSAGE_HEADER_SIZE, tcm_info->payload_length);
	if (retval < 0) {
		TPD_INFO("Failed to copy payload\n");
		UNLOCK_BUFFER(tcm_info->in);
		UNLOCK_BUFFER(tcm_info->resp);
		atomic_set(&tcm_info->command_status, CMD_ERROR);
		goto exit;
	}

	tcm_info->resp.data_length = tcm_info->payload_length;

	UNLOCK_BUFFER(tcm_info->in);
	UNLOCK_BUFFER(tcm_info->resp);

	atomic_set(&tcm_info->command_status, CMD_IDLE);

exit:
	complete(&response_complete);

	return;
}

/**
 * syna_tcm_dispatch_message() - dispatch message received from device
 *
 * @tcm_info: handle of core module
 *
 * The information received in the message read in from the device is dispatched
 * to the appropriate destination based on whether the information represents a
 * report or a response to a command.
 */
static void syna_tcm_dispatch_message(struct syna_tcm_data *tcm_info)
{
	int retval;
	unsigned int payload_length;

	if (tcm_info->report_code == REPORT_IDENTIFY) {
		tcm_info->identify_state = 1;
		payload_length = tcm_info->payload_length;

		LOCK_BUFFER(tcm_info->in);

		retval = secure_memcpy((unsigned char *)&tcm_info->id_info, sizeof(tcm_info->id_info),
								&tcm_info->in.buf[MESSAGE_HEADER_SIZE], tcm_info->in.buf_size - MESSAGE_HEADER_SIZE,
								MIN(sizeof(tcm_info->id_info), payload_length));
		if (retval < 0) {
			TPD_INFO("Failed to copy identification info\n");
			UNLOCK_BUFFER(tcm_info->in);
			return;
		}

		UNLOCK_BUFFER(tcm_info->in);

		syna_tcm_resize_chunk_size(tcm_info);
		TPD_INFO("Received identify report (firmware mode = 0x%02x)\n", tcm_info->id_info.mode);

		if (0x0b == tcm_info->id_info.mode) {
			tcm_info->firmware_mode_count++;
			if (!tcm_info->upload_flag && tcm_info->firmware_mode_count >= FIRMWARE_MODE_BL_MAX) {
				tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_REPORT, "firmware mode = 0x0b");
				tcm_info->upload_flag = 1;
			}
		}
		if (atomic_read(&tcm_info->command_status) == CMD_BUSY) {
			switch (tcm_info->command) {
			case CMD_RESET:
			case CMD_RUN_BOOTLOADER_FIRMWARE:
			case CMD_RUN_APPLICATION_FIRMWARE:
				atomic_set(&tcm_info->command_status, CMD_IDLE);
				complete(&response_complete);
				break;
			default:
				TPD_INFO("Device has been reset\n");
				atomic_set(&tcm_info->command_status, CMD_ERROR);
				complete(&response_complete);
				break;
			}
		}

		if (tcm_info->id_info.mode == MODE_HOST_DOWNLOAD)
			return;

		syna_tcm_helper(tcm_info);
	}

	if (tcm_info->report_code >= REPORT_IDENTIFY)
		syna_tcm_dispatch_report(tcm_info);
	else
		syna_tcm_dispatch_response(tcm_info);

	return;
}

/**
 * syna_tcm_continued_read() - retrieve entire payload from device
 *
 * @tcm_info: handle of core module
 *
 * Read transactions are carried out until the entire payload is retrieved from
 * the device and stored in the handle of the core module.
 */
static int syna_tcm_continued_read(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	unsigned char marker = 0, code = 0;
	unsigned int idx = 0, offset = 0, chunks = 0;
	unsigned int chunk_space = 0, xfer_length = 0, total_length = 0, remaining_length = 0;
#define RETRY_CNT_MAX (3)
	int retry_cnt = 0;

	total_length = MESSAGE_HEADER_SIZE + tcm_info->payload_length + 1;
	remaining_length = total_length - tcm_info->read_length;

	LOCK_BUFFER(tcm_info->in);

	retval = syna_tcm_realloc_mem(&tcm_info->in, total_length);
	if (retval < 0) {
		TPD_INFO("Failed to reallocate memory for tcm_info->in.buf\n");
		UNLOCK_BUFFER(tcm_info->in);
		return retval;
	}

	/* available chunk space for payload = total chunk size minus header
	 * marker byte and header code byte */
	if (tcm_info->rd_chunk_size == 0)
		chunk_space = remaining_length;
	else
		chunk_space = tcm_info->rd_chunk_size - 2;

	chunks = ceil_div(remaining_length, chunk_space);

	chunks = chunks == 0 ? 1 : chunks;

	offset = tcm_info->read_length;

	LOCK_BUFFER(tcm_info->temp);

	for (idx = 0; idx < chunks; idx++) {
		if (remaining_length > chunk_space)
			xfer_length = chunk_space;
		else
			xfer_length = remaining_length;

		if (xfer_length == 1) {
			tcm_info->in.buf[offset] = MESSAGE_PADDING;
			offset += xfer_length;
			remaining_length -= xfer_length;
			continue;
		}

		retval = syna_tcm_alloc_mem(&tcm_info->temp, xfer_length + 2);
		if (retval < 0) {
			TPD_INFO("Failed to allocate memory for tcm_info->temp.buf\n");
			UNLOCK_BUFFER(tcm_info->temp);
			UNLOCK_BUFFER(tcm_info->in);
			return retval;
		}
		udelay(30);
RETRY:
		retval = syna_tcm_read(tcm_info, tcm_info->temp.buf, xfer_length + 2);

		if (retval < 0) {
			TPD_INFO("Failed to read from device\n");
			UNLOCK_BUFFER(tcm_info->temp);
			UNLOCK_BUFFER(tcm_info->in);
			return retval;
		}

		marker = tcm_info->temp.buf[0];
		code = tcm_info->temp.buf[1];

		if (marker != MESSAGE_MARKER) {
			TPD_INFO("Incorrect header marker (0x%02x)\n", marker);
			if (marker == 0xFF) {
				if (retry_cnt < RETRY_CNT_MAX) {
					retry_cnt++;
					TPD_INFO("retry cnt[%d]\n", retry_cnt);
					goto RETRY;
				}
			}
			UNLOCK_BUFFER(tcm_info->temp);
			UNLOCK_BUFFER(tcm_info->in);
			return -EIO;
		}

		if (code != STATUS_CONTINUED_READ) {
			TPD_INFO("Incorrect header code (0x%02x)\n", code);
			UNLOCK_BUFFER(tcm_info->temp);
			UNLOCK_BUFFER(tcm_info->in);
			return -EIO;
		}

		retval = secure_memcpy(&tcm_info->in.buf[offset], total_length - offset, &tcm_info->temp.buf[2], xfer_length, xfer_length);
		if (retval < 0) {
			TPD_INFO("Failed to copy payload\n");
			UNLOCK_BUFFER(tcm_info->temp);
			UNLOCK_BUFFER(tcm_info->in);
			return retval;
		}

		offset += xfer_length;

		remaining_length -= xfer_length;
	}

	UNLOCK_BUFFER(tcm_info->temp);
	UNLOCK_BUFFER(tcm_info->in);

	return 0;
}

/**
 * syna_tcm_raw_read() - retrieve specific number of data bytes from device
 *
 * @tcm_info: handle of core module
 * @in_buf: buffer for storing data retrieved from device
 * @length: number of bytes to retrieve from device
 *
 * Read transactions are carried out until the specific number of data bytes are
 * retrieved from the device and stored in in_buf.
 */
static int syna_tcm_raw_read(struct syna_tcm_data *tcm_info, unsigned char *in_buf, unsigned int length)
{
	int retval = 0;
	unsigned char code = 0;
	unsigned int idx = 0, offset = 0;
	unsigned int chunks = 0, chunk_space = 0;
	unsigned int xfer_length = 0, remaining_length = 0;

	if (length < 2) {
		TPD_INFO("Invalid length information\n");
		return -EINVAL;
	}

	/* minus header marker byte and header code byte */
	remaining_length = length - 2;

	/* available chunk space for data = total chunk size minus header marker
	 * byte and header code byte */
	if (tcm_info->rd_chunk_size == 0)
		chunk_space = remaining_length;
	else
		chunk_space = tcm_info->rd_chunk_size - 2;

	chunks = ceil_div(remaining_length, chunk_space);

	chunks = chunks == 0 ? 1 : chunks;

	offset = 0;

	LOCK_BUFFER(tcm_info->temp);

	for (idx = 0; idx < chunks; idx++) {
		if (remaining_length > chunk_space)
			xfer_length = chunk_space;
		else
			xfer_length = remaining_length;

		if (xfer_length == 1) {
			in_buf[offset] = MESSAGE_PADDING;
			offset += xfer_length;
			remaining_length -= xfer_length;
			continue;
		}

		retval = syna_tcm_alloc_mem(&tcm_info->temp, xfer_length + 2);
		if (retval < 0) {
			TPD_INFO("Failed to allocate memory for tcm_info->temp.buf\n");
			UNLOCK_BUFFER(tcm_info->temp);
			return retval;
		}

		retval = syna_tcm_read(tcm_info, tcm_info->temp.buf, xfer_length + 2);
		if (retval < 0) {
			TPD_INFO("Failed to read from device\n");
			UNLOCK_BUFFER(tcm_info->temp);
			return retval;
		}

		code = tcm_info->temp.buf[1];

		if (idx == 0) {
			retval = secure_memcpy(&in_buf[0], length, &tcm_info->temp.buf[0], xfer_length + 2, xfer_length + 2);
		} else {
			if (code != STATUS_CONTINUED_READ) {
				TPD_INFO("Incorrect header code (0x%02x)\n", code);
				UNLOCK_BUFFER(tcm_info->temp);
				return -EIO;
			}

			retval = secure_memcpy(&in_buf[offset],
									length - offset, &tcm_info->temp.buf[2],
									xfer_length, xfer_length);
		}
		if (retval < 0) {
			TPD_INFO("Failed to copy data\n");
			UNLOCK_BUFFER(tcm_info->temp);
			return retval;
		}

		if (idx == 0)
			offset += (xfer_length + 2);
		else
			offset += xfer_length;

		remaining_length -= xfer_length;
	}

	UNLOCK_BUFFER(tcm_info->temp);

	return 0;
}

/**
 * syna_tcm_raw_write() - write command/data to device without receiving
 * response
 *
 * @tcm_info: handle of core module
 * @command: command to send to device
 * @data: data to send to device
 * @length: length of data in bytes
 *
 * A command and its data, if any, are sent to the device.
 */
static int syna_tcm_raw_write(struct syna_tcm_data *tcm_info, unsigned char command,
							  unsigned char *data, unsigned int length)
{
	int retval = 0;
	unsigned int idx = 0, chunks = 0, chunk_space = 0;
	unsigned int xfer_length = 0, remaining_length = length;

	/* available chunk space for data = total chunk size minus command byte */
	if (tcm_info->wr_chunk_size == 0)
		chunk_space = remaining_length;
	else
		chunk_space = tcm_info->wr_chunk_size - 1;

	chunks = ceil_div(remaining_length, chunk_space);

	chunks = chunks == 0 ? 1 : chunks;

	LOCK_BUFFER(tcm_info->out);

	for (idx = 0; idx < chunks; idx++) {
		if (remaining_length > chunk_space)
			xfer_length = chunk_space;
		else
			xfer_length = remaining_length;

		retval = syna_tcm_alloc_mem(&tcm_info->out, xfer_length + 1);
		if (retval < 0) {
			TPD_INFO("Failed to allocate memory for tcm_info->out.buf\n");
			UNLOCK_BUFFER(tcm_info->out);
			return retval;
		}

		if (idx == 0)
			tcm_info->out.buf[0] = command;
		else
			tcm_info->out.buf[0] = CMD_CONTINUE_WRITE;

		if (xfer_length) {
			retval = secure_memcpy(&tcm_info->out.buf[1],
									xfer_length,
									&data[idx * chunk_space],
									remaining_length,
									xfer_length);
			if (retval < 0) {
				TPD_INFO("Failed to copy data\n");
				UNLOCK_BUFFER(tcm_info->out);
				return retval;
			}
		}

		retval = syna_tcm_write(tcm_info, tcm_info->out.buf, xfer_length + 1);
		if (retval < 0) {
			TPD_INFO("Failed to write to device\n");
			UNLOCK_BUFFER(tcm_info->out);
			return retval;
		}

		remaining_length -= xfer_length;
	}

	UNLOCK_BUFFER(tcm_info->out);

	return 0;
}

/*add this for debug. remove before pvt*/
/*
static void syna_tcm_debug_message(char *buf, int len)
{
	int i = 0;
	char buffer[161] = {0};

	for (i = 0; i < len; i++) {
		if (i > 32)
			break;

		sprintf(&buffer[5 * i], "0x%02x ", buf[i]);
	}

	if (len > 0)
		TPD_INFO("payload data: %s\n", buffer);
}
*/

/**
 * syna_tcm_read_message() - read message from device
 *
 * @tcm_info: handle of core module
 * @in_buf: buffer for storing data in raw read mode
 * @length: length of data in bytes in raw read mode
 *
 * If in_buf is not NULL, raw read mode is used and syna_tcm_raw_read() is
 * called. Otherwise, a message including its entire payload is retrieved from
 * the device and dispatched to the appropriate destination.
 */
static int syna_tcm_read_message(struct syna_tcm_data *tcm_info, unsigned char *in_buf, unsigned int length)
{
	int retval = 0;
	unsigned int total_length = 0;
	struct syna_tcm_message_header *header = NULL;
	#define RETRY_CNT_MAX (3)
	int retry_cnt = 0;


	TPD_DEBUG("%s\n", __func__);
	mutex_lock(&tcm_info->rw_mutex);

	if (in_buf != NULL) {
		retval = syna_tcm_raw_read(tcm_info, in_buf, length);
		goto exit;
	}

	LOCK_BUFFER(tcm_info->in);

RETRY:

	retval = syna_tcm_read(tcm_info, tcm_info->in.buf,
					 tcm_info->read_length);
	if (retval < 0) {
		TPD_INFO("Failed to read from device\n");
		UNLOCK_BUFFER(tcm_info->in);
		goto exit;
	}

	header = (struct syna_tcm_message_header *)tcm_info->in.buf;
	if (header->marker != MESSAGE_MARKER) {
		TPD_INFO("wrong header marker:0x%02x\n", header->marker);
		if (header->marker == 0xFF) {
			if (retry_cnt < RETRY_CNT_MAX) {
				retry_cnt++;
				TPD_INFO("retry cnt[%d]\n", retry_cnt);
				goto RETRY;
			}
		}
		UNLOCK_BUFFER(tcm_info->in);
		retval = -ENXIO;
		goto exit;
	}

	tcm_info->report_code = header->code;
	tcm_info->payload_length = le2_to_uint(header->length);
	TPD_DEBUG("Header code = 0x%02x Payload len = %d\n", tcm_info->report_code, tcm_info->payload_length);

	if (tcm_info->report_code <= STATUS_ERROR || tcm_info->report_code == STATUS_INVALID) {
		switch (tcm_info->report_code) {
		case STATUS_OK:
			break;
		case STATUS_CONTINUED_READ:
		/*TPD_INFO("Out-of-sync continued read\n");*/
		case STATUS_IDLE:
		case STATUS_BUSY:
			tcm_info->payload_length = 0;
			UNLOCK_BUFFER(tcm_info->in);
			retval = 0;
			goto exit;
		default:
			TPD_INFO("Incorrect header code (0x%02x)\n", tcm_info->report_code);
			if (tcm_info->report_code != STATUS_ERROR) {
				UNLOCK_BUFFER(tcm_info->in);
				retval = -EIO;
				goto exit;
			}
		}
	}

	total_length = MESSAGE_HEADER_SIZE + tcm_info->payload_length + 1;

#ifdef PREDICTIVE_READING
	if (total_length <= tcm_info->read_length) {
		goto check_padding;
	} else if (total_length - 1 == tcm_info->read_length) {
		tcm_info->in.buf[total_length - 1] = MESSAGE_PADDING;
		goto check_padding;
	}
#else
	if (tcm_info->payload_length == 0) {
		tcm_info->in.buf[total_length - 1] = MESSAGE_PADDING;
		goto check_padding;
	}
#endif

	UNLOCK_BUFFER(tcm_info->in);

	retval = syna_tcm_continued_read(tcm_info);
	if (retval < 0) {
		TPD_INFO("Failed to do continued read\n");
		goto exit;
	}

	LOCK_BUFFER(tcm_info->in);

	tcm_info->in.buf[0] = MESSAGE_MARKER;
	tcm_info->in.buf[1] = tcm_info->report_code;
	tcm_info->in.buf[2] = (unsigned char)tcm_info->payload_length;
	tcm_info->in.buf[3] = (unsigned char)(tcm_info->payload_length >> 8);

check_padding:
	if (tcm_info->in.buf[total_length - 1] != MESSAGE_PADDING) {
		TPD_INFO("Incorrect message padding byte (0x%02x)\n", tcm_info->in.buf[total_length - 1]);
		UNLOCK_BUFFER(tcm_info->in);
		retval = -EIO;
		goto exit;
	}

	UNLOCK_BUFFER(tcm_info->in);

#ifdef PREDICTIVE_READING
	total_length = MAX(total_length, MIN_READ_LENGTH);
	tcm_info->read_length = MIN(total_length, tcm_info->rd_chunk_size);
	if (tcm_info->rd_chunk_size == 0)
		tcm_info->read_length = total_length;
#endif

	/*add for debug, remove before pvt*/
	/*if (LEVEL_BASIC != tp_debug) {
	//	syna_tcm_debug_message(&tcm_info->in.buf[4], tcm_info->payload_length);
	//}*/

	syna_tcm_dispatch_message(tcm_info);

	retval = 0;

exit:
	if ((retval < 0) && (atomic_read(&tcm_info->command_status) == CMD_BUSY)) {
		atomic_set(&tcm_info->command_status, CMD_ERROR);
		complete(&response_complete);
	}

	mutex_unlock(&tcm_info->rw_mutex);

	return retval;
}

/**
 * syna_tcm_write_message() - write message to device and receive response
 *
 * @tcm_info: handle of core module
 * @command: command to send to device
 * @payload: payload of command
 * @length: length of payload in bytes
 * @resp_buf: buffer for storing command response
 * @resp_buf_size: size of response buffer in bytes
 * @resp_length: length of command response in bytes
 * @polling_delay_ms: delay time after sending command before resuming polling
 *
 * If resp_buf is NULL, raw write mode is used and syna_tcm_raw_write() is
 * called. Otherwise, a command and its payload, if any, are sent to the device
 * and the response to the command generated by the device is read in.
 */
static int syna_tcm_write_message(struct syna_tcm_data *tcm_info,
								  unsigned char command, unsigned char *payload,
								  unsigned int length, unsigned char **resp_buf,
								  unsigned int *resp_buf_size, unsigned int *resp_length,
								  unsigned int timeout)
{
	int retval = 0;
	unsigned int idx = 0, chunks = 0, chunk_space = 0;
	unsigned int xfer_length = 0, remaining_length = 0;
	unsigned int command_status = 0;
	unsigned int timeout_ms = 0;

	mutex_lock(&tcm_info->command_mutex);
	mutex_lock(&tcm_info->rw_mutex);


	if (resp_buf == NULL) {
		retval = syna_tcm_raw_write(tcm_info, command, payload, length);
		mutex_unlock(&tcm_info->rw_mutex);
		goto exit;
	}

	atomic_set(&tcm_info->command_status, CMD_BUSY);
	reinit_completion(&response_complete);
	tcm_info->command = command;

	LOCK_BUFFER(tcm_info->resp);

	tcm_info->resp.buf = *resp_buf;
	tcm_info->resp.buf_size = *resp_buf_size;
	tcm_info->resp.data_length = 0;

	UNLOCK_BUFFER(tcm_info->resp);

	/* adding two length bytes as part of payload */
	remaining_length = length + 2;

	/* available chunk space for payload = total chunk size minus command
	 * byte */
	if (tcm_info->wr_chunk_size == 0)
		chunk_space = remaining_length;
	else
		chunk_space = tcm_info->wr_chunk_size - 1;

	chunks = ceil_div(remaining_length, chunk_space);

	chunks = chunks == 0 ? 1 : chunks;

	TPD_DEBUG("%s:Command = 0x%02x\n", __func__, command);

	LOCK_BUFFER(tcm_info->out);

	for (idx = 0; idx < chunks; idx++) {
		if (remaining_length > chunk_space)
			xfer_length = chunk_space;
		else
			xfer_length = remaining_length;

		retval = syna_tcm_alloc_mem(&tcm_info->out, xfer_length + 1);
		if (retval < 0) {
			TPD_INFO("Failed to allocate memory for tcm_info->out.buf\n");
			UNLOCK_BUFFER(tcm_info->out);
			mutex_unlock(&tcm_info->rw_mutex);
			goto exit;
		}

		if (idx == 0) {
			tcm_info->out.buf[0] = command;
			tcm_info->out.buf[1] = (unsigned char)length;
			tcm_info->out.buf[2] = (unsigned char)(length >> 8);

			if (xfer_length > 2) {
				retval = secure_memcpy(&tcm_info->out.buf[3],
											xfer_length - 2,
											payload,
											remaining_length - 2,
											xfer_length - 2);
				if (retval < 0) {
					TPD_INFO("Failed to copy payload\n");
					UNLOCK_BUFFER(tcm_info->out);
					mutex_unlock(&tcm_info->rw_mutex);
					goto exit;
				}
			}
		} else {
			tcm_info->out.buf[0] = CMD_CONTINUE_WRITE;

			retval = secure_memcpy(&tcm_info->out.buf[1],
									xfer_length,
									&payload[idx * chunk_space - 2],
									remaining_length,
									xfer_length);
			if (retval < 0) {
				TPD_INFO("Failed to copy payload\n");
				UNLOCK_BUFFER(tcm_info->out);
				mutex_unlock(&tcm_info->rw_mutex);
				goto exit;
			}
		}

		retval = syna_tcm_write(tcm_info, tcm_info->out.buf, xfer_length + 1);
		if (retval < 0) {
			TPD_INFO("Failed to write to device\n");
			UNLOCK_BUFFER(tcm_info->out);
			mutex_unlock(&tcm_info->rw_mutex);
			goto exit;
		}

		remaining_length -= xfer_length;
	}

	UNLOCK_BUFFER(tcm_info->out);

	mutex_unlock(&tcm_info->rw_mutex);

	if (timeout == 0) {
		timeout_ms = RESPONSE_TIMEOUT_MS_DEFAULT;
	} else {
		timeout_ms = timeout;
	}

	retval = wait_for_completion_timeout(&response_complete,
										 msecs_to_jiffies(timeout_ms));
	if (retval == 0) {
		TPD_INFO("Timed out waiting for response (command 0x%02x)\n",
			tcm_info->command);
		retval = -EIO;
	} else {
		command_status = atomic_read(&tcm_info->command_status);

		if (command_status != CMD_IDLE ||
			tcm_info->report_code == STATUS_ERROR) {
			TPD_INFO("Failed to get valid response\n");
			retval = -EIO;
			goto exit;
		}

		retval = 0;
	}

exit:
	if (command_status == CMD_IDLE) {
		LOCK_BUFFER(tcm_info->resp);

		if (tcm_info->report_code == STATUS_ERROR) {
			if (tcm_info->resp.data_length) {
				TPD_INFO("Error code = 0x%02x\n",
						tcm_info->resp.buf[0]);
			}
		}

		if (resp_buf != NULL) {
			*resp_buf = tcm_info->resp.buf;
			*resp_buf_size = tcm_info->resp.buf_size;
			*resp_length = tcm_info->resp.data_length;
		}

		UNLOCK_BUFFER(tcm_info->resp);
	}

	tcm_info->command = CMD_NONE;
	atomic_set(&tcm_info->command_status, CMD_IDLE);
	mutex_unlock(&tcm_info->command_mutex);

	return retval;
}

static int syna_tcm_get_app_info(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;
	unsigned int timeout = APP_STATUS_POLL_TIMEOUT_MS;

get_app_info:
	retval = syna_tcm_write_message(tcm_info,
									CMD_GET_APPLICATION_INFO,
									NULL,
									0,
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_GET_APPLICATION_INFO));
		goto exit;
	}

	retval = secure_memcpy((unsigned char *)&tcm_info->app_info,
							sizeof(tcm_info->app_info),
							resp_buf,
							resp_buf_size,
							MIN(sizeof(tcm_info->app_info), resp_length));
	if (retval < 0) {
		TPD_INFO("Failed to copy application info\n");
		goto exit;
	}

	tcm_info->app_status = le2_to_uint(tcm_info->app_info.status);

	if (tcm_info->app_status == APP_STATUS_BOOTING || tcm_info->app_status == APP_STATUS_UPDATING) {
		if (timeout > 0) {
			msleep(APP_STATUS_POLL_MS);
			timeout -= APP_STATUS_POLL_MS;
			goto get_app_info;
		}
	}

	retval = 0;

exit:
	kfree(resp_buf);

	return retval;
}

static int syna_tcm_get_boot_info(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;

	retval = syna_tcm_write_message(tcm_info,
									CMD_GET_BOOT_INFO,
									NULL,
									0,
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_GET_BOOT_INFO));
		goto exit;
	}

	retval = secure_memcpy((unsigned char *)&tcm_info->boot_info,
						   sizeof(tcm_info->boot_info),
						   resp_buf,
						   resp_buf_size,
						   MIN(sizeof(tcm_info->boot_info), resp_length));
	if (retval < 0) {
		TPD_INFO("Failed to copy boot info\n");
		goto exit;
	}

	retval = 0;

exit:
	kfree(resp_buf);

	return retval;
}

static int syna_tcm_identify(struct syna_tcm_data *tcm_info, bool id)
{
	int retval;
	unsigned char *resp_buf;
	unsigned int resp_buf_size;
	unsigned int resp_length;

	resp_buf = NULL;
	resp_buf_size = 0;

	mutex_lock(&tcm_info->identify_mutex);

	if (!id)
		goto get_info;

	retval = syna_tcm_write_message(tcm_info,
									CMD_IDENTIFY,
									NULL,
									0,
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_IDENTIFY));
		goto exit;
	}

	retval = secure_memcpy((unsigned char *)&tcm_info->id_info,
						   sizeof(tcm_info->id_info),
						   resp_buf,
						   resp_buf_size,
						   MIN(sizeof(tcm_info->id_info), resp_length));
	if (retval < 0) {
		TPD_INFO("Failed to copy identification info\n");
		goto exit;
	}

	syna_tcm_resize_chunk_size(tcm_info);

get_info:
	if (tcm_info->id_info.mode == MODE_APPLICATION) {
		retval = syna_tcm_get_app_info(tcm_info);
		if (retval < 0) {
			TPD_INFO("Failed to get application info\n");
			goto exit;
		}
	} else {
		retval = syna_tcm_get_boot_info(tcm_info);
		if (retval < 0) {
			TPD_INFO("Failed to get boot info\n");
			goto exit;
		}
	}

	retval = 0;

exit:
	mutex_unlock(&tcm_info->identify_mutex);

	kfree(resp_buf);

	return retval;
}

static int syna_tcm_run_application_firmware(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	bool retry = true;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;

retry:
	retval = syna_tcm_write_message(tcm_info,
									CMD_RUN_APPLICATION_FIRMWARE,
									NULL,
									0,
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_RUN_APPLICATION_FIRMWARE));
		goto exit;
	}

	retval = syna_tcm_identify(tcm_info, false);
	if (retval < 0) {
		TPD_INFO("Failed to do identification\n");
		goto exit;
	}

	if (tcm_info->id_info.mode != MODE_APPLICATION) {
		TPD_INFO("Failed to run application firmware (boot status = 0x%02x)\n", tcm_info->boot_info.status);
		if (retry) {
			retry = false;
			goto retry;
		}
		retval = -EINVAL;
		goto exit;
	} else if (tcm_info->app_status != APP_STATUS_OK) {
		TPD_INFO("Application status = 0x%02x\n", tcm_info->app_status);
	}

	retval = 0;

exit:
	kfree(resp_buf);

	return retval;
}


static int syna_tcm_run_bootloader_firmware(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;

	retval = syna_tcm_write_message(tcm_info,
									CMD_RUN_BOOTLOADER_FIRMWARE,
									NULL,
									0,
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_RUN_BOOTLOADER_FIRMWARE));
		goto exit;
	}

	retval = syna_tcm_identify(tcm_info, false);
	if (retval < 0) {
		TPD_INFO("Failed to do identification\n");
		goto exit;
	}

	if (tcm_info->id_info.mode == MODE_APPLICATION) {
		TPD_INFO("Failed to enter bootloader mode\n");
		retval = -EINVAL;
		goto exit;
	}

	retval = 0;

exit:
	kfree(resp_buf);

	return retval;
}


static int syna_tcm_switch_mode(struct syna_tcm_data *tcm_info, enum firmware_mode mode)
{
	int retval = 0;

	mutex_lock(&tcm_info->reset_mutex);

	switch (mode) {
	case FW_MODE_BOOTLOADER:
		retval = syna_tcm_run_bootloader_firmware(tcm_info);
		if (retval < 0) {
			TPD_INFO("Failed to switch to bootloader mode\n");
			goto exit;
		}
		break;
	case FW_MODE_APPLICATION:
		retval = syna_tcm_run_application_firmware(tcm_info);
		if (retval < 0) {
			TPD_INFO("Failed to switch to application mode\n");
			goto exit;
		}
		break;
	default:
		TPD_INFO("Invalid firmware mode\n");
		retval = -EINVAL;
		goto exit;
	}

exit:
	mutex_unlock(&tcm_info->reset_mutex);

	return retval;
}

static int syna_tcm_get_dynamic_config(struct syna_tcm_data *tcm_info,
									   enum dynamic_config_id id, unsigned short *value)
{
	int retval = 0;
	unsigned char out_buf = (unsigned char)id;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;

	retval = syna_tcm_write_message(tcm_info,
									CMD_GET_DYNAMIC_CONFIG,
									&out_buf,
									sizeof(out_buf),
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									RESPONSE_TIMEOUT_MS_SHORT);
	if (retval < 0 || resp_length < 2) {
		retval = -EINVAL;
		TPD_INFO("Failed to read dynamic config\n");
		goto exit;
	}

	*value = (unsigned short)le2_to_uint(resp_buf);
exit:
	kfree(resp_buf);
	return retval;
}

static int syna_tcm_set_dynamic_config(struct syna_tcm_data *tcm_info,
									   enum dynamic_config_id id, unsigned short value)
{
	int retval = 0;
	unsigned char out_buf[3] = {0};
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;

	TPD_DEBUG("%s:config 0x%x, value %d\n", __func__, id, value);

	out_buf[0] = (unsigned char)id;
	out_buf[1] = (unsigned char)value;
	out_buf[2] = (unsigned char)(value >> 8);

	retval = syna_tcm_write_message(tcm_info,
									CMD_SET_DYNAMIC_CONFIG,
									out_buf,
									sizeof(out_buf),
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									RESPONSE_TIMEOUT_MS_SHORT);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_SET_DYNAMIC_CONFIG));
		goto exit;
	}

exit:
	kfree(resp_buf);

	return retval;
}

static int syna_tcm_sleep(struct syna_tcm_data *tcm_info, bool en)
{
	int retval = 0;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;
	unsigned char command = en ? CMD_ENTER_DEEP_SLEEP : CMD_EXIT_DEEP_SLEEP;

	TPD_INFO("%s: %s .\n", __func__, en ? "enter" : "exit");
	if (tcm_info->palm_hold_report == 1) {
		tcm_info->palm_hold_report = 0;
		TPD_INFO("%s:set palm_hold_report = %d\n", __func__, tcm_info->palm_hold_report);
	}

	retval = syna_tcm_write_message(tcm_info,
									command,
									NULL,
									0,
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", en ? STR(CMD_ENTER_DEEP_SLEEP) : STR(CMD_EXIT_DEEP_SLEEP));
		goto exit;
	}

exit:
	kfree(resp_buf);

	return retval;
}

void syna_tcm_refresh_switch(int fps)
{
	return;
}
EXPORT_SYMBOL(syna_tcm_refresh_switch);

static int syna_report_refresh_switch(void *chip_data, int fps)
{
	int retval = 0;
	unsigned short send_value = 0;
	int i = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	TPD_DEBUG("%s: refresh_switch: %d HZ!\n", __func__, fps);
	if (tcm_info == NULL) {
		return -1;
	}
	tcm_info->display_refresh_rate = fps;

	if (!*tcm_info->in_suspend && !tcm_info->game_mode) {
		for (i = 0; i < tcm_info->fps_report_rate_num; i = i + 2) {
			if (fps == tcm_info->fps_report_rate_array[i]) {
				send_value = tcm_info->fps_report_rate_array[i + 1];
			}
		}
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_SET_REPORT_FRE, send_value);
		if (retval < 0) {
			TPD_INFO("Failed to set dynamic report frequence config\n");
		}
		TPD_INFO("%s: refresh_switch: %d HZ %s!\n", __func__, fps, retval < 0 ? "failed" : "success");
	}
	return retval;
}

static void syna_rate_white_list_ctrl(void *chip_data, int value)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	unsigned short send_value = 1;
	int retval = 0;
	int i = 0;
	unsigned short fps = 60;

	if (tcm_info == NULL) {
		return;
	}

	if (*tcm_info->in_suspend || tcm_info->game_mode) {
		return;
	}
	/*0:ignore 1:120hz 2:90hz*/
	switch (value) {
	case 0: /* 60Hz*/
		fps = 60;
		break;
	case 1: /* 120Hz*/
		fps = 120;
		break;
	case 2: /* 90hz*/
		fps = 90;
		break;
	default:
		return;
	}
	for (i = 0; i < tcm_info->fps_report_rate_num; i = i + 2) {
		if (fps == tcm_info->fps_report_rate_array[i]) {
			send_value = tcm_info->fps_report_rate_array[i + 1];
		}
	}
	retval = syna_tcm_set_dynamic_config(tcm_info,
						 DC_SET_REPORT_FRE,
						 send_value);
	if (retval < 0) {
		TPD_INFO("Failed to set dynamic report frequence config\n");
	}
	TPD_INFO("%s: DC_SET_REPORT_FRE: %d  %s!\n",
		 __func__, send_value, retval < 0 ? "failed" : "success");
}


static int synaptics_resetgpio_set(struct hw_resource *hw_res, bool on)
{
	if (gpio_is_valid(hw_res->reset_gpio)) {
		TPD_DEBUG("Set the reset_gpio \n");
		gpio_direction_output(hw_res->reset_gpio, on);
	}

	return 0;
}

static int syna_tcm_reset(void *chip_data)
{
	int retval = 0;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0;
	unsigned int resp_length = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	tcm_info->palm_hold_report = 0;
	TPD_DEBUG("%s: clear palm_hold_report.\n", __func__);

	syna_tcm_fw_update_in_bl();

	mutex_lock(&tcm_info->reset_mutex);

	synaptics_resetgpio_set(tcm_info->hw_res, false);
	msleep(POWEWRUP_TO_RESET_TIME);
	synaptics_resetgpio_set(tcm_info->hw_res, true);
	msleep(RESET_TO_NORMAL_TIME);

	retval = syna_tcm_identify(tcm_info, false);
	if (retval < 0) {
		TPD_INFO("Failed to do identification\n");
		goto exit;
	}

	if (tcm_info->id_info.mode == MODE_APPLICATION)
		goto dispatch_reset;

	retval = syna_tcm_write_message(tcm_info,
									CMD_RUN_APPLICATION_FIRMWARE,
									NULL,
									0,
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_RUN_APPLICATION_FIRMWARE));
	}

	retval = syna_tcm_identify(tcm_info, false);
	if (retval < 0) {
		TPD_INFO("Failed to do identification\n");
		goto exit;
	}

dispatch_reset:
	TPD_INFO("Firmware mode = 0x%02x, boot status 0x%02x, app status 0x%02x\n",
			 tcm_info->id_info.mode,
			 tcm_info->boot_info.status,
			 tcm_info->app_status);

exit:
	mutex_unlock(&tcm_info->reset_mutex);

	kfree(resp_buf);
	if (tcm_info->tp_data_record_support && tcm_info->differ_read_every_frame) {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_SET_DIFFER_READ, 1);

		if (retval < 0) {
			TPD_INFO("Failed to set differ read true\n");
		}
	}

	return retval;
}

static int syna_get_chip_info(void *chip_data)
{
	int ret = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	TPD_INFO("%s: Enter\n", __func__);

	ret = syna_tcm_reset(tcm_info);  /* reset to get bootloader info or boot info*/
	if (ret < 0) {
		TPD_INFO("failed to reset device\n");
	}

	ret = syna_get_default_report_config(tcm_info);
	if (ret < 0) {
		TPD_INFO("failed to get default report config\n");
	}
	return 0;
}

static int syna_get_vendor(void *chip_data, struct panel_info *panel_data)
{
	char manu_temp[MAX_DEVICE_MANU_LENGTH] = SYNAPTICS_PREFIX;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	tcm_info->ihex_name = panel_data->extra;

	strlcat(manu_temp, panel_data->manufacture_info.manufacture, MAX_DEVICE_MANU_LENGTH);
	strncpy(panel_data->manufacture_info.manufacture, manu_temp, MAX_DEVICE_MANU_LENGTH);
	TPD_INFO("chip_info->tp_type = %d, panel_data->fw_name = %s\n", panel_data->tp_type, panel_data->fw_name);
	return 0;
}

static u32 syna_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	return tcm_info->trigger_reason;
}

static int syna_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
	unsigned int idx = 0, status = 0;
	struct object_data *object_data = NULL;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct touch_hcd *touch_hcd = NULL;
	static unsigned int obj_attention = 0x00;

	if (points == NULL || tcm_info == NULL)
		return obj_attention;

	touch_hcd = tcm_info->touch_hcd;

	if (!touch_hcd)
		return obj_attention;

	if (!touch_hcd->touch_data.object_data) {
		TPD_INFO("%s: object_data is null\n", __func__);
		return obj_attention;
	}

	if (tcm_info->palm_hold_report) {
		TPD_DEBUG("palm mode!\n");
		return 0;
	}

	object_data = touch_hcd->touch_data.object_data;

	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		status = object_data[idx].status;
		if (status != LIFT) {
			obj_attention |= (0x1 << idx);
		} else {
			if ((~obj_attention) & ((0x1) << idx))
				continue;
			else
				obj_attention &= (~(0x1 << idx));
		}

		points[idx].x = object_data[idx].x_pos;
		points[idx].y = object_data[idx].y_pos;
		points[idx].touch_major = max(object_data[idx].x_width, object_data[idx].y_width);
		points[idx].width_major = min(object_data[idx].x_width, object_data[idx].y_width);
		points[idx].tx_press = object_data[idx].exwidth;
		points[idx].rx_press = object_data[idx].eywidth;
		points[idx].tx_er = object_data[idx].xeratio;
		points[idx].rx_er = object_data[idx].yeratio;
		points[idx].status = 1;
	}

	return obj_attention;
}

static int syna_get_touch_points_auto(void *chip_data,
					  struct point_info *points,
					  int max_num,
					  struct resolution_info *resolution_info)
{
	unsigned int idx = 0, status = 0;
	struct object_data *object_data = NULL;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	static unsigned int obj_attention = 0x00;
	int max_x_inchip = 0;
	int max_y_inchip = 0;
	int max_x = 0;
	int max_y = 0;

	if (points == NULL) {
		return obj_attention;
	}

	if (tcm_info->palm_hold_report) {
		TPD_DEBUG("palm mode!\n");
		return 0;
	}

	object_data = touch_hcd->touch_data.object_data;

	max_x_inchip = le2_to_uint(tcm_info->app_info.max_x) + 1;
	max_y_inchip = le2_to_uint(tcm_info->app_info.max_y) + 1;
	max_x = resolution_info->max_x;
	max_y = resolution_info->max_y;


	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		status = object_data[idx].status;
		if (status != LIFT) {
			obj_attention |= (0x1 << idx);
		} else {
			if ((~obj_attention) & ((0x1) << idx)) {
				continue;
			} else {
				obj_attention &= (~(0x1 << idx));
			}
		}

		if (max_x_inchip == max_x) {
			points[idx].x = object_data[idx].x_pos;
		} else {
			points[idx].x = (object_data[idx].x_pos * max_x) / max_x_inchip;
		}
		if (max_y_inchip == max_y) {
			points[idx].y = object_data[idx].y_pos;
		} else {
			points[idx].y = (object_data[idx].y_pos * max_y) / max_y_inchip;
		}
		points[idx].touch_major = max(object_data[idx].x_width, object_data[idx].y_width);
		points[idx].width_major = min(object_data[idx].x_width, object_data[idx].y_width);
		points[idx].tx_press = object_data[idx].exwidth;
		points[idx].rx_press = object_data[idx].eywidth;
		points[idx].tx_er = object_data[idx].xeratio;
		points[idx].rx_er = object_data[idx].yeratio;
		points[idx].status = 1;
	}

	return obj_attention;
}

static int syna_tcm_set_gesture_mode(struct syna_tcm_data *tcm_info, bool enable)
{
	int retval = 0;
	int state = tcm_info->gesture_state;
	int config = 0xFFFF;
	/*this command may take too much time, if needed can add flag to skip this */
	TPD_INFO("%s: enable(%d) state(%d)\n", __func__, enable, state);

	if (tcm_info->black_gesture_indep) {
		if (enable) {
			SET_GESTURE_BIT(state, DouTap, config, 0)
			SET_GESTURE_BIT(state, UpVee, config, 2)
			SET_GESTURE_BIT(state, DownVee, config, 1)
			SET_GESTURE_BIT(state, LeftVee, config, 3)
			SET_GESTURE_BIT(state, RightVee, config, 4)
			SET_GESTURE_BIT(state, Circle, config, 5)
			SET_GESTURE_BIT(state, DouSwip, config, 6)
			SET_GESTURE_BIT(state, Left2RightSwip, config, 7)
			SET_GESTURE_BIT(state, Right2LeftSwip, config, 8)
			SET_GESTURE_BIT(state, Up2DownSwip, config, 9)
			SET_GESTURE_BIT(state, Down2UpSwip, config, 10)
			SET_GESTURE_BIT(state, Mgestrue, config, 11)
			SET_GESTURE_BIT(state, Wgestrue, config, 12)
			SET_GESTURE_BIT(state, SingleTap, config, 13)
			SET_GESTURE_BIT(state, Heart, config, 14)
		} else {
			config = 0x0;
		}
	}
	TPD_INFO("%s: gesture config:%x\n", __func__, config);

	if (enable) {
		retval = syna_tcm_sleep(tcm_info, false);
		if (retval < 0) {
			TPD_INFO("%s: Failed to exit sleep mode\n", __func__);
			return retval;
		}

		msleep(5); /* delay 5ms*/

		retval = syna_set_input_reporting(tcm_info, true);
		if (retval < 0) {
			TPD_INFO("%s: Failed to set input reporting\n", __func__);
			return retval;
		}

		retval = syna_tcm_set_dynamic_config(tcm_info, DC_IN_WAKEUP_GESTURE_MODE, true);
		if (retval < 0) {
			TPD_INFO("%s: Failed to set dynamic gesture config\n", __func__);
			return retval;
		}
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_GESTURE_MASK, config);
		if (retval < 0) {
			TPD_INFO("%s: Failed to set dynamic gesture mask config\n", __func__);
			return retval;
		}
	} else {
		retval = syna_tcm_sleep(tcm_info, true);
		if (retval < 0) {
			TPD_INFO("%s: Failed to enter sleep mode\n", __func__);
			return retval;
		}
	}

	return retval;
}

static void syna_tcm_enable_gesture_mask(void *chip_data, uint32_t enable)
{
	int retval = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	int state = tcm_info->gesture_state;
	int config = 0xFFFF;

	/*this command may take too much time, if needed can add flag to skip this */
	TPD_INFO("%s: enable(%d)\n", __func__, enable);
	if (tcm_info->black_gesture_indep) {
		if (enable) {
			SET_GESTURE_BIT(state, DouTap, config, 0)
			SET_GESTURE_BIT(state, UpVee, config, 2)
			SET_GESTURE_BIT(state, DownVee, config, 1)
			SET_GESTURE_BIT(state, LeftVee, config, 3)
			SET_GESTURE_BIT(state, RightVee, config, 4)
			SET_GESTURE_BIT(state, Circle, config, 5)
			SET_GESTURE_BIT(state, DouSwip, config, 6)
			SET_GESTURE_BIT(state, Left2RightSwip, config, 7)
			SET_GESTURE_BIT(state, Right2LeftSwip, config, 8)
			SET_GESTURE_BIT(state, Up2DownSwip, config, 9)
			SET_GESTURE_BIT(state, Down2UpSwip, config, 10)
			SET_GESTURE_BIT(state, Mgestrue, config, 11)
			SET_GESTURE_BIT(state, Wgestrue, config, 12)
			SET_GESTURE_BIT(state, SingleTap, config, 13)
			SET_GESTURE_BIT(state, Heart, config, 14)
		} else {
			config = 0x0;
		}
	}
	TPD_INFO("%s: gesture config:%x\n", __func__, config);

	if (enable) {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_GESTURE_MASK, config);
		if (retval < 0) {
			TPD_INFO("%s: Failed to set dynamic gesture mask config\n", __func__);
		}
	} else {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_GESTURE_MASK, 0x0000);
		if (retval < 0) {
			TPD_INFO("%s: Failed to set dynamic gesture mask config\n", __func__);
		}
	}
}

static int syna_tcm_set_game_mode(struct syna_tcm_data *tcm_info, bool enable)
{
	int retval = 0;
	unsigned short regval = 0;
	unsigned short noise_length = 0;

	tcm_info->game_mode = enable;

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_ERROR_PRIORITY, &regval);
	if (retval < 0) {
			TPD_INFO("Failed to get DC_ERROR_PRIORITY val\n");
			return retval;
	}
	TPD_INFO("%s: enable[%d], now reg status[0x%x]\n", __func__, tcm_info->game_mode, regval);

	if (enable) {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_ERROR_PRIORITY, regval|0x01);
		if (retval < 0) {
			TPD_INFO("Failed to set dynamic error priority config\n");
			return retval;
		}

		noise_length = 0x0A;

		retval = syna_tcm_set_dynamic_config(tcm_info, DC_NOISE_LENGTH, noise_length);
		if (retval < 0) {
			TPD_INFO("Failed to set dynamic noise length config\n");
			return retval;
		}
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_SET_REPORT_FRE, tcm_info->game_rate);
		if (retval < 0) {
			TPD_INFO("Failed to set dynamic report frequence config\n");
			return retval;
		}
	} else {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_ERROR_PRIORITY, regval&0xF0);
		if (retval < 0) {
			TPD_INFO("Failed to set dynamic error priority config\n");
			return retval;
		}

		retval = syna_tcm_set_dynamic_config(tcm_info, DC_NOISE_LENGTH, tcm_info->default_noise_length);
		if (retval < 0) {
			TPD_INFO("Failed to set dynamic noise length config\n");
			return retval;
		}
		syna_report_refresh_switch(tcm_info, tcm_info->display_refresh_rate);
		if (retval < 0) {
			TPD_INFO("Failed to set dynamic report frequence config\n");
			return retval;
		}
	}

	return retval;
}

static int syna_tcm_normal_mode(struct syna_tcm_data *tcm_info)
{
	int retval;

	retval = syna_set_input_reporting(tcm_info, false);
	if (retval < 0) {
		TPD_INFO("Failed to set input reporting\n");
		return retval;
	}

	retval = syna_tcm_set_dynamic_config(tcm_info, DC_IN_WAKEUP_GESTURE_MODE, false);
	if (retval < 0) {
		TPD_INFO("Failed to set dynamic gesture config\n");
		return retval;
	}

	syna_report_refresh_switch(tcm_info, tcm_info->display_refresh_rate);

	return retval;
}

static int syna_corner_limit_handle(struct syna_tcm_data *tcm_info)
{
	int ret = -1;

	if(LANDSCAPE_SCREEN_90 == tcm_info->touch_direction) {
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL, 0x01);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_SEL, 0x0F);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_SEL\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_X, 0x0A);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_X\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_Y, 0x0A);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_Y\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_DARK_ZONE_ENABLE, 0x03);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_DARK_ZONE_ENABLE\n", __func__);
			return ret;
		}
		if (tcm_info->fingerprint_and_grip_param_equal_19805) {
			ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_X, 0x88);
			if (ret < 0) {
				TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_X\n", __func__);
				return ret;
			}
		} else {
			ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_X, 0xFF);
			if (ret < 0) {
				TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_X\n", __func__);
				return ret;
			}
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_Y, 0x44);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_Y\n", __func__);
			return ret;
		}
	} else if (LANDSCAPE_SCREEN_270 == tcm_info->touch_direction) {
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL, 0x01);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_SEL, 0x0F);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_SEL\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_X, 0x0A);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_X\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_Y, 0x0A);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_Y\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_DARK_ZONE_ENABLE, 0x0C);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_DARK_ZONE_ENABLE\n", __func__);
			return ret;
		}
		if (tcm_info->fingerprint_and_grip_param_equal_19805) {
			ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_X, 0x88);
			if (ret < 0) {
				TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_X\n", __func__);
				return ret;
			}
		} else {
			ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_X, 0xFF);
			if (ret < 0) {
				TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_X\n", __func__);
				return ret;
			}
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_Y, 0x44);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_Y\n", __func__);
			return ret;
		}
	} else if (VERTICAL_SCREEN == tcm_info->touch_direction) {
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL, 0x00);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_SEL, 0x03);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_SEL\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_X, 0x0A);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_X\n", __func__);
			return ret;
		}
		/*ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_U, 0x32);
		//if (ret < 0) {
		//	TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_U\n", __func__);
		//	return ret;
		//}
		//ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_V, 0x64);
		//if (ret < 0) {
		//	TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_V\n", __func__);
		//	return ret;
		//}*/
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_DARK_ZONE_ENABLE, 0x05);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_DARK_ZONE_ENABLE\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_X, 0x24);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_X\n", __func__);
			return ret;
		}
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_Y, 0xF5);
		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_Y\n", __func__);
			return ret;
		}
	}

	return ret;
}

static int syna_enable_edge_limit(struct syna_tcm_data *tcm_info)
{
	int ret = 0;
	TPD_INFO("%s: enter\n", __func__);

	if (tcm_info->fw_edge_limit_support) {
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ENABLED, 0x03);
		TPD_INFO("%s: syna_tcm_set_dynamic_config 0x03\n", __func__);
	} else {
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ENABLED, 0x01);
	}

	if (ret < 0) {
		TPD_INFO("%s:failed to enable grip suppression\n", __func__);
		return ret;
	}

	ret = syna_corner_limit_handle(tcm_info);
	if (ret < 0) {
		TPD_INFO("%s:failed to set grip suppression para\n", __func__);
		return ret;
	}

	return ret;
}

static int syna_mode_switch(void *chip_data, work_mode mode, bool flag)
{
	int ret = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	switch(mode) {
	case MODE_NORMAL:
		ret = syna_tcm_normal_mode(tcm_info);
		if (ret < 0) {
			tcm_info->error_state_count++;
			TPD_INFO("normal mode switch failed\n");
			if (tcm_info->error_state_count >= ERROR_STATE_MAX) {
				syna_tcm_reset(tcm_info); /*ic state err, need to reset the IC*/
				tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_REPORT, "ic state err rest");
			}
		}
		tcm_info->error_state_count = 0;

		break;
	case MODE_GESTURE:
		ret = syna_tcm_set_gesture_mode(tcm_info, flag);
		if (ret < 0) {
			TPD_INFO("%s:Failed to set gesture mode\n", __func__);
		}
		break;
	case MODE_SLEEP:
		ret = syna_tcm_sleep(tcm_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: failed to switch to sleep", __func__);
		}
		break;
	case MODE_CHARGE:
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_CHARGER_CONNECTED, flag ? 1 : 0);
		if (ret < 0) {
			TPD_INFO("%s:failed to set charger mode\n", __func__);
			tcm_info->error_state_count++;
		}
		tcm_info->error_state_count = 0;
		break;
	case MODE_EDGE:
		ret = syna_enable_edge_limit(tcm_info);
		if (ret < 0) {
			TPD_INFO("%s: failed to enable edg limit.\n", __func__);
			tcm_info->error_state_count++;
		}
		tcm_info->error_state_count = 0;
		break;
	case MODE_GAME:
		ret = syna_tcm_set_game_mode(tcm_info, flag);
		if (ret < 0) {
			TPD_INFO("%s:failed to set game mode\n", __func__);
			tcm_info->error_state_count++;
		}
		tcm_info->error_state_count = 0;
		break;
	default:
		break;
	}
	return 0;
}

static int syna_ftm_process(void *chip_data)
{
	TPD_INFO("%s: go into sleep\n", __func__);
	syna_get_chip_info(chip_data);
	syna_mode_switch(chip_data, MODE_SLEEP, true);
	return 0;
}

static int syna_tcm_reinit_device(void *chip_data)
{
	complete_all(&response_complete);
	complete_all(&report_complete);

	return 0;
}

static int syna_power_control(void *chip_data, bool enable)
{
	int ret = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	TPD_DEBUG("%s: %d\n", __func__, enable);

	if (true == enable) {
		ret = tp_powercontrol_2v8(tcm_info->hw_res, true);
		if (ret) {
			return -1;
		}
		ret = tp_powercontrol_1v8(tcm_info->hw_res, true);
		if (ret) {
			return -1;
		}
		synaptics_resetgpio_set(tcm_info->hw_res, false);
		msleep(POWEWRUP_TO_RESET_TIME);
		synaptics_resetgpio_set(tcm_info->hw_res, true);
		msleep(RESET_TO_NORMAL_TIME);
	} else {
		synaptics_resetgpio_set(tcm_info->hw_res, false);
		ret = tp_powercontrol_1v8(tcm_info->hw_res, false);
		if (ret) {
			return -1;
		}
		ret = tp_powercontrol_2v8(tcm_info->hw_res, false);
		if (ret) {
			return -1;
		}
	}

	return ret;
}

static fw_check_state syna_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	u16 config = 0;
	int retval = 0;

	TPD_INFO("fw id %d, custom config id 0x%s\n", panel_data->TP_FW, tcm_info->app_info.customer_config_id);

	if (strlen(tcm_info->app_info.customer_config_id) == 0) {
		return FW_ABNORMAL;
	}

	sscanf(tcm_info->app_info.customer_config_id, "%x", &panel_data->TP_FW);
	if (panel_data->TP_FW == 0) {
		return FW_ABNORMAL;
	}

	if (panel_data->manufacture_info.version) {
		sprintf(panel_data->manufacture_info.version, "0x%s", tcm_info->app_info.customer_config_id);
	}

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_NOISE_LENGTH, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get default noise length\n");
		return FW_ABNORMAL;
	}

	tcm_info->default_noise_length = config;

	return FW_NORMAL;
}

static int syna_tcm_helper(struct syna_tcm_data *tcm_info)
{
	if (tcm_info->id_info.mode != MODE_APPLICATION && !mutex_is_locked(&tcm_info->reset_mutex)) {
		TPD_INFO("%s: use helper\n", __func__);
		queue_work(tcm_info->helper_workqueue, &tcm_info->helper_work);
	}

	return 0;
}

static void syna_tcm_helper_work(struct work_struct *work)
{
	int retval = 0;
	struct syna_tcm_data *tcm_info = container_of(work, struct syna_tcm_data, helper_work);

	mutex_lock(&tcm_info->reset_mutex);
	retval = syna_tcm_run_application_firmware(tcm_info);
	if (retval < 0) {
		TPD_INFO("Failed to switch to app mode\n");
	}

	mutex_unlock(&tcm_info->reset_mutex);
}

static int syna_tcm_async_work(void *chip_data)
{
	int retval = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	if (tcm_info->id_info.mode != MODE_APPLICATION) {
		return 0;
	}

	retval = syna_tcm_identify(tcm_info, false);
	if (retval < 0) {
		TPD_INFO("Failed to do identification\n");
		return retval;
	}

	syna_set_trigger_reason(tcm_info, IRQ_FW_AUTO_RESET);
	return 0;
}

static int syna_tcm_enable_report(struct syna_tcm_data *tcm_info, enum report_type report_type, bool enable)
{
	int retval;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char out[2] = {0};
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0;
	unsigned int resp_length = 0;

	test_hcd->report_index = 0;
	test_hcd->report_type = report_type;

	out[0] = test_hcd->report_type;

	retval = syna_tcm_write_message(tcm_info,
									enable ? CMD_ENABLE_REPORT : CMD_DISABLE_REPORT,
									out,
									1,
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write message %s\n", enable ? STR(CMD_ENABLE_REPORT) : STR(CMD_DISABLE_REPORT));
	}

	return retval;
}

static void syna_tcm_enable_fingerprint(void *chip_data, uint32_t enable)
{
	int retval = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	TPD_INFO("%s: enable(%d)\n", __func__, enable);

	if (enable) {
		if (tcm_info->fingerprint_and_grip_param_equal_19805) {
			retval = syna_tcm_set_dynamic_config(tcm_info, DC_TOUCH_HOLD, 0x07);
			if (retval < 0) {
				TPD_INFO("Failed to set dynamic touch and hold config\n");
				return;
			}
		} else {
			retval = syna_tcm_set_dynamic_config(tcm_info, DC_TOUCH_HOLD, *tcm_info->in_suspend ? 0x01 : 0x02);
			if (retval < 0) {
				TPD_INFO("Failed to set dynamic touch and hold config\n");
				return;
			}
			retval = syna_tcm_enable_report(tcm_info, REPORT_TOUCH_HOLD, *tcm_info->in_suspend ? false : true);
			if (retval < 0) {
				TPD_INFO("Failed to set enable touch and hold report\n");
				return;
			}
		}
	} else {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_TOUCH_HOLD, 0x00);
		if (retval < 0) {
			TPD_INFO("Failed to set dynamic touch and hold config\n");
			return;
		}
		if (!tcm_info->fingerprint_and_grip_param_equal_19805) {
			retval = syna_tcm_enable_report(tcm_info, REPORT_TOUCH_HOLD, false);
			if (retval < 0) {
			TPD_INFO("Failed to set disable touch and hold report\n");
			return;
		}
		}
	}

	return;
}

static void syna_tcm_fingerprint_info(void *chip_data, struct fp_underscreen_info *fp_tpinfo)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	struct touch_data *touch_data = &touch_hcd->touch_data;
	u8 *fp_buf = tcm_info->report.buffer.buf;

	if (!fp_tpinfo) {
		return;
	}

	if (tcm_info->report.buffer.data_length < 8 && touch_data->lpwg_gesture == TOUCH_HOLD_DOWN) {
		TPD_INFO("%s: invalid fingerprint buf length\n", __func__);
		return;
	}

	if (touch_data->lpwg_gesture == TOUCH_HOLD_DOWN) {
		fp_tpinfo->touch_state = FINGERPRINT_DOWN_DETECT;
		fp_tpinfo->x = fp_buf[0] | fp_buf[1] << 8;
		fp_tpinfo->y = fp_buf[2] | fp_buf[3] << 8;
		fp_tpinfo->area_rate = fp_buf[4] | fp_buf[5] << 8;
	} else if (touch_data->lpwg_gesture == TOUCH_HOLD_UP) {
		fp_tpinfo->touch_state = FINGERPRINT_UP_DETECT;
	}

	return;
}

static void syna_tcm_fingerprint_info_auto(void *chip_data,
					   struct fp_underscreen_info *fp_tpinfo,
					   struct resolution_info *resolution_info)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	struct touch_data *touch_data = &touch_hcd->touch_data;
	u8 *fp_buf = tcm_info->report.buffer.buf;

	int max_x_inchip = 0;
	int max_y_inchip = 0;
	int max_x = 0;
	int max_y = 0;

	if (!fp_tpinfo) {
		return;
	}

	if (tcm_info->report.buffer.data_length < 8
		&& touch_data->lpwg_gesture == TOUCH_HOLD_DOWN) {
		TPD_INFO("%s: invalid fingerprint buf length\n", __func__);
		return;
	}

	if (touch_data->lpwg_gesture != TOUCH_HOLD_DOWN
		&& touch_data->lpwg_gesture != TOUCH_HOLD_UP) {
		return;
	}

	max_x_inchip = le2_to_uint(tcm_info->app_info.max_x) + 1;
	max_y_inchip = le2_to_uint(tcm_info->app_info.max_y) + 1;
	max_x = resolution_info->LCD_WIDTH;
	max_y = resolution_info->LCD_HEIGHT;

	if (touch_data->lpwg_gesture == TOUCH_HOLD_DOWN) {
		fp_tpinfo->touch_state = FINGERPRINT_DOWN_DETECT;
		fp_tpinfo->x = fp_buf[0] | fp_buf[1] << 8;
		fp_tpinfo->y = fp_buf[2] | fp_buf[3] << 8;
		fp_tpinfo->area_rate = fp_buf[4] | fp_buf[5] << 8;
	} else if (touch_data->lpwg_gesture == TOUCH_HOLD_UP) {
		fp_tpinfo->touch_state = FINGERPRINT_UP_DETECT;
		fp_tpinfo->x = fp_buf[0] | fp_buf[1] << 8;
		fp_tpinfo->y = fp_buf[2] | fp_buf[3] << 8;
		fp_tpinfo->area_rate = fp_buf[4] | fp_buf[5] << 8;
	}

	if (max_x_inchip != max_x) {
		fp_tpinfo->x = (fp_tpinfo->x * max_x) / max_x_inchip;
	}

	if (max_y_inchip != max_y) {
		fp_tpinfo->y = (fp_tpinfo->y * max_y) / max_y_inchip;
	}

	return;
}

static void syna_tcm_get_health_info(void *chip_data, struct monitor_data *mon_data)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct health_info *health_info = (struct health_info *)tcm_info->report.buffer.buf;
	int data_length = tcm_info->report.buffer.data_length;
	struct health_info *health_local = &tcm_info->health_info;
	int i = 0;

	if (data_length < 20) {
		TPD_INFO("%s: invalid health debug buf length\n", __func__);
		return;
	}

	if (health_info->grip_count != 0 && health_local->grip_count != health_info->grip_count) {
		mon_data->grip_report++;
	}
	if (health_info->baseline_err != 0 && health_local->baseline_err != health_info->baseline_err) {
		switch(health_info->baseline_err) {
		case BASE_NEGATIVE_FINGER:
			mon_data->reserve1++;
			break;
		case BASE_MUTUAL_SELF_CAP:
			mon_data->reserve2++;
			break;
		case BASE_ENERGY_RATIO:
			mon_data->reserve3++;
			break;
		case BASE_RXABS_BASELINE:
			mon_data->reserve4++;
			break;
		case BASE_TXABS_BASELINE:
			mon_data->reserve5++;
			break;
		default:
			break;
		}
	}
	if (health_info->noise_state >= 2 && health_local->noise_state != health_info->noise_state) {
		mon_data->noise_count++;
	}
	if (health_info->shield_mode != 0 && health_local->shield_mode != health_info->shield_mode) {
		switch(health_info->shield_mode) {
		case SHIELD_PALM:
			mon_data->shield_palm++;
			break;
		case SHIELD_GRIP:
			mon_data->shield_edge++;
			break;
		case SHIELD_METAL:
			mon_data->shield_metal++;
			break;
		case SHIELD_MOISTURE:
			mon_data->shield_water++;
			break;
		case SHIELD_ESD:
			mon_data->shield_esd++;
			break;
		default:
			break;
		}
	}
	if (health_info->reset_reason != 0) {
		switch(health_info->reset_reason) {
		case RST_HARD:
			mon_data->hard_rst++;
			break;
		case RST_INST:
			mon_data->inst_rst++;
			break;
		case RST_PARITY:
			mon_data->parity_rst++;
			break;
		case RST_WD:
			mon_data->wd_rst++;
			break;
		case RST_OTHER:
			mon_data->other_rst++;
			break;
		}
	}
	memcpy(health_local, health_info, sizeof(struct health_info));
	if (tp_debug != 0) {
		for (i = 0; i < data_length; i++) {
			TPD_INFO("[0x%x], ", tcm_info->report.buffer.buf[i]);
		}
	}
}

static int syna_tcm_erase_flash(struct syna_tcm_data *tcm_info, unsigned int page_start, unsigned int page_count)
{
	int ret = 0;
	unsigned char out_buf[4] = {0};
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0;
	unsigned int resp_length = 0;
	unsigned int cmd_length = 0;

	TPD_INFO("start page %d, page count %d\n", page_start, page_count);
	if (page_start > 0xff || page_count > 0xff) {
		cmd_length = 4;
		out_buf[0] = (unsigned char)(page_start & 0xff);
		out_buf[1] = (unsigned char)((page_start >> 8) & 0xff);
		out_buf[2] = (unsigned char)(page_count & 0xff);
		out_buf[3] = (unsigned char)((page_count >> 8) & 0xff);
	} else {
		cmd_length = 2;
		out_buf[0] = (unsigned char)page_start;
		out_buf[1] = (unsigned char)page_count;
	}

	ret = syna_tcm_write_message(tcm_info,  CMD_ERASE_FLASH, out_buf, cmd_length,
								 &resp_buf, &resp_buf_size, &resp_length, ERASE_FLASH_DELAY_MS);
	if (ret < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_ERASE_FLASH));
	}

	kfree(resp_buf);
	return ret;
}

static int syna_tcm_write_flash(struct syna_tcm_data *tcm_info, struct reflash_hcd *reflash_hcd,
								unsigned int address, const unsigned char *data, unsigned int datalen)
{
	int retval;
	unsigned int w_len, xfer_len, remaining_len;
	unsigned int flash_addr, block_addr;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0, offset = 0;
	struct syna_tcm_buffer out;

	memset(&out, 0, sizeof(out));
	INIT_BUFFER(out, false);

	w_len = tcm_info->wr_chunk_size - 5;
	w_len = w_len - (w_len % reflash_hcd->write_block_size);
	w_len = MIN(w_len, reflash_hcd->max_write_payload_size);

	remaining_len = datalen;

	while(remaining_len) {
		if (remaining_len > w_len) {
			xfer_len = w_len;
		} else {
			xfer_len = remaining_len;
		}

		retval = syna_tcm_alloc_mem(&out, xfer_len + 2);
		if (retval < 0) {
			TPD_INFO("Failed to alloc memory\n");
			break;
		}

		flash_addr = address + offset;
		block_addr = flash_addr / reflash_hcd->write_block_size;
		out.buf[0] = (unsigned char)block_addr;
		out.buf[1] = (unsigned char)(block_addr >> 8);

		retval = secure_memcpy(&out.buf[2],
							   xfer_len,
							   &data[offset],
							   datalen - offset,
							   xfer_len);
		if (retval < 0) {
			TPD_INFO("Failed to copy write data\n");
			break;
		}

		retval = syna_tcm_write_message(tcm_info, CMD_WRITE_FLASH,
										out.buf,
										xfer_len + 2,
										&resp_buf,
										&resp_buf_size,
										&resp_length,
										WRITE_FLASH_DELAY_MS);
		if (retval < 0) {
			TPD_INFO("Failed to write message %s, Addr 0x%08x, Len 0x%d\n",
					 STR(CMD_WRITE_FLASH), flash_addr, xfer_len);
			break;
		}

		offset += xfer_len;
		remaining_len -= xfer_len;
	}

	RELEASE_BUFFER(out);
	kfree(resp_buf);
	return 0;
}

static fw_update_state syna_tcm_fw_update(void *chip_data, const struct firmware *fw, bool force)
{
	int ret = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct image_info image_info;
	unsigned int image_fw_id, device_fw_id;
	unsigned char *device_config_id = NULL;
	unsigned char *image_config_id = NULL;
	struct app_config_header *header = NULL;
	int temp = 0, page_start = 0, page_count = 0;
	unsigned int size = 0, flash_addr = 0, device_addr = 0, device_size = 0;
	const unsigned char *data;
	struct reflash_hcd reflash_hcd;

	memset(&image_info, 0, sizeof(struct image_info));

	if (tcm_info->fwupdate_bootloader) {
		if (fw) {
			if (tcm_info->g_fw_buf && fw->size < FW_BUF_SIZE) {
				tcm_info->g_fw_len = fw->size;
				memcpy(tcm_info->g_fw_buf, fw->data, fw->size);
				tcm_info->g_fw_sta = true;
			} else {
				TPD_INFO("fw->size:%d is less than %d\n", fw->size, FW_BUF_SIZE);
				return FW_UPDATE_FATAL;
			}
		}
		if (tcm_info->g_fw_sta) {
			ret = synaptics_parse_header_v2(&image_info, tcm_info->g_fw_buf);
			if (ret < 0) {
				tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_FW_UPDATE, "synaptics_parse_header_v2 fail");
				TPD_INFO("Failed to parse fw image\n");
				return FW_UPDATE_FATAL;
			}
		} else {
			if (!fw) {
				TPD_INFO("fw is null\n");
				return FW_UPDATE_FATAL;
			} else {
				ret = synaptics_parse_header_v2(&image_info, fw->data);
				if (ret < 0) {
					tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_FW_UPDATE, "synaptics_parse_header_v2 fail");
					TPD_INFO("Failed to parse fw image\n");
					return FW_UPDATE_FATAL;
				}
			}
		}
	} else {
		ret = synaptics_parse_header_v2(&image_info, fw->data);
		if (ret < 0) {
			tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_FW_UPDATE, "synaptics_parse_header_v2 fail");
			TPD_INFO("Failed to parse fw image\n");
			return FW_UPDATE_FATAL;
		}
	}
	header = (struct app_config_header *)image_info.app_config.data;

	image_fw_id = le4_to_uint(header->build_id);
	device_fw_id = le4_to_uint(tcm_info->id_info.build_id);
	TPD_INFO("image build id %d, device build id %d\n", image_fw_id, device_fw_id);

	image_config_id = header->customer_config_id;
	device_config_id = tcm_info->app_info.customer_config_id;
	TPD_INFO("image config id 0x%s, device config id 0x%s\n", image_config_id, device_config_id);

	if (!force) {
		if ((image_fw_id == device_fw_id) && (strncmp(image_config_id, device_config_id, 16) == 0)) {
			TPD_INFO("same firmware/config id, no need to update\n");
			return FW_NO_NEED_UPDATE;
		}
	}

	ret = syna_tcm_identify(tcm_info, true);
	if (ret < 0) {
		return FW_UPDATE_ERROR;
	}

	if (tcm_info->id_info.mode == MODE_APPLICATION) {
		ret = syna_tcm_switch_mode(tcm_info, FW_MODE_BOOTLOADER);
		if (ret < 0) {
			tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_FW_UPDATE, "syna_tcm_switch_mode fail");
			TPD_INFO("Failed to switch to bootloader mode\n");
			return FW_UPDATE_ERROR;
		}
	}

	temp = tcm_info->boot_info.write_block_size_words;
	reflash_hcd.write_block_size = temp * 2;

	temp = le2_to_uint(tcm_info->boot_info.erase_page_size_words);
	reflash_hcd.page_size = temp * 2;

	temp = le2_to_uint(tcm_info->boot_info.max_write_payload_size);
	reflash_hcd.max_write_payload_size = temp;

	TPD_INFO("Write block size %d, page size %d, payload_size %d\n",
			 reflash_hcd.write_block_size,
			 reflash_hcd.page_size,
			 reflash_hcd.max_write_payload_size);

	if (reflash_hcd.write_block_size > (tcm_info->wr_chunk_size - 5)) {
		tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_FW_UPDATE, "write block size is exceed");
		TPD_INFO("write block size is exceed\n");
		return FW_UPDATE_ERROR;
	}

	if (image_info.app_firmware.size == 0) {
		tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_FW_UPDATE, "no application firmware in image");
		TPD_INFO("no application firmware in image\n\n");
		return FW_UPDATE_ERROR;
	}

	/* erase application firmware */
	page_start = image_info.app_firmware.flash_addr / reflash_hcd.page_size;
	page_count = ceil_div(image_info.app_firmware.size, reflash_hcd.page_size);
	ret = syna_tcm_erase_flash(tcm_info, page_start, page_count);
	if (ret < 0) {
		tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_FW_UPDATE, "Failed to erase firmware");
		TPD_INFO("Failed to erase firmware\n");
		return FW_UPDATE_ERROR;
	}

	/* write application firmware */
	data = image_info.app_firmware.data;
	size = image_info.app_firmware.size;
	flash_addr = image_info.app_firmware.flash_addr;

	ret = syna_tcm_write_flash(tcm_info, &reflash_hcd, flash_addr, data, size);
	if (ret < 0) {
		tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_FW_UPDATE, "Failed to write flash");
		TPD_INFO("Failed to write flash \n");
		return FW_UPDATE_ERROR;
	}

	/* update app config start */
	data = image_info.app_config.data;
	size = image_info.app_config.size;
	flash_addr = image_info.app_config.flash_addr;

	temp = le2_to_uint(tcm_info->app_info.app_config_start_write_block);
	device_addr = temp * reflash_hcd.write_block_size;
	device_size = le2_to_uint(tcm_info->app_info.app_config_size);

	TPD_INFO("Config Device addr/size 0x%x/%d, flash addr/size 0x%x/%d\n",
			 device_addr, device_size, flash_addr, size);

	page_start = image_info.app_config.flash_addr / reflash_hcd.page_size;
	page_count = ceil_div(image_info.app_config.size, reflash_hcd.page_size);

	ret = syna_tcm_erase_flash(tcm_info, page_start, page_count);
	if (ret < 0) {
		tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_FW_UPDATE, "Failed to erase config");
		TPD_INFO("Failed to erase config\n");
		return FW_UPDATE_ERROR;
	}

	ret = syna_tcm_write_flash(tcm_info, &reflash_hcd, flash_addr, data, size);
	if (ret < 0) {
		tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_FW_UPDATE, "Failed to write config");
		TPD_INFO("Failed to write config \n");
		return FW_UPDATE_ERROR;
	}

	TPD_INFO("end of config update\n");
	/* update app config end */

	return FW_UPDATE_SUCCESS;
}

static void syna_tcm_fw_update_in_bl(void)
{
	int ret = -1;
	struct syna_tcm_data *tcm_info = NULL;

	tcm_info = g_tcm_info;

	if (!tcm_info) {
		TPD_INFO("%s: tcm_info is null\n", __func__);
		return;
	}

	if (!tcm_info->fwupdate_bootloader) {
		TPD_INFO("%s not support.\n", __func__);
		return;
	}

	if (*tcm_info->loading_fw) {
		TPD_INFO("%s not support when TP loading fw.\n", __func__);
		return;
	}
	if (tcm_info->probe_done && tcm_info->firmware_mode_count % FWUPDATE_BL_MAX == 0 && tcm_info->firmware_mode_count) {
		tcm_info->firmware_mode_count = 0;
		ret = syna_tcm_fw_update(tcm_info, NULL, 0);
		if (ret > 0) {
			TPD_INFO("g_fw_buf update failed!\n");
		}
		tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_FW_UPDATE, "syna_tcm_fw_update_new");
	}
	return;
}

static int syna_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	struct touch_data *touch_data = &touch_hcd->touch_data;

	gesture->clockwise = 2;
	switch (touch_data->lpwg_gesture) {
	case DTAP_DETECT:
		gesture->gesture_type = DouTap;
		break;
	case CIRCLE_DETECT:
		gesture->gesture_type = Circle;
		if (touch_data->extra_gesture_info[2] == 0x10)
			gesture->clockwise = 1;
		else if (touch_data->extra_gesture_info[2] == 0x20)
			gesture->clockwise = 0;
		break;
	case SWIPE_DETECT:
		if (touch_data->extra_gesture_info[4] == 0x41) {/*x+*/
			gesture->gesture_type = Left2RightSwip;
		} else if (touch_data->extra_gesture_info[4] == 0x42) {/*x-*/
			gesture->gesture_type = Right2LeftSwip;
		} else if (touch_data->extra_gesture_info[4] == 0x44) {/*y+*/
			gesture->gesture_type = Up2DownSwip;
		} else if (touch_data->extra_gesture_info[4] == 0x48) {/*y-*/
			gesture->gesture_type = Down2UpSwip;
		} else if (touch_data->extra_gesture_info[4] == 0x81) {/*2x-*/
			gesture->gesture_type = DouSwip;
		} else if (touch_data->extra_gesture_info[4] == 0x82) {/*2x+*/
			gesture->gesture_type = DouSwip;
		} else if (touch_data->extra_gesture_info[4] == 0x84) {/*2y+*/
			gesture->gesture_type = DouSwip;
		} else if (touch_data->extra_gesture_info[4] == 0x88) {/*2y-*/
			gesture->gesture_type = DouSwip;
		}
		break;
	case M_UNICODE:
		gesture->gesture_type = Mgestrue;
		break;
	case W_UNICODE:
		gesture->gesture_type = Wgestrue;
		break;
	case VEE_DETECT:
		if (touch_data->extra_gesture_info[2] == 0x02) {/*up*/
			gesture->gesture_type = UpVee;
		} else if (touch_data->extra_gesture_info[2] == 0x01) {/*down*/
			gesture->gesture_type = DownVee;
		} else if (touch_data->extra_gesture_info[2] == 0x08) {/*left*/
			gesture->gesture_type = LeftVee;
		} else if (touch_data->extra_gesture_info[2] == 0x04) {/*right*/
			gesture->gesture_type = RightVee;
		}
		break;
	case TOUCH_HOLD_DOWN:
		gesture->gesture_type = FingerprintDown;
		break;
	case TOUCH_HOLD_UP:
		gesture->gesture_type = FingerprintUp;
		break;
	case HEART_DETECT:
		gesture->gesture_type = Heart;
		if (touch_data->extra_gesture_info[2] == 0x10) {
				gesture->clockwise = 1;
		} else if (touch_data->extra_gesture_info[2] == 0x20) {
				gesture->clockwise = 0;
		}
		break;
	case STAP_DETECT:
		gesture->gesture_type = SingleTap;
		break;
	case TRIANGLE_DETECT:
	default:
		TPD_DEBUG("not support\n");
		break;
	}
	if (gesture->gesture_type != UnkownGesture) {
		gesture->Point_start.x = (touch_data->data_point[0] | (touch_data->data_point[1] << 8));
		gesture->Point_start.y = (touch_data->data_point[2] | (touch_data->data_point[3] << 8));
		gesture->Point_end.x	= (touch_data->data_point[4] | (touch_data->data_point[5] << 8));
		gesture->Point_end.y	= (touch_data->data_point[6] | (touch_data->data_point[7] << 8));
		gesture->Point_1st.x	= (touch_data->data_point[8] | (touch_data->data_point[9] << 8));
		gesture->Point_1st.y	= (touch_data->data_point[10] | (touch_data->data_point[11] << 8));
		gesture->Point_2nd.x	= (touch_data->data_point[12] | (touch_data->data_point[13] << 8));
		gesture->Point_2nd.y	= (touch_data->data_point[14] | (touch_data->data_point[15] << 8));
		gesture->Point_3rd.x	= (touch_data->data_point[16] | (touch_data->data_point[17] << 8));
		gesture->Point_3rd.y	= (touch_data->data_point[18] | (touch_data->data_point[19] << 8));
		gesture->Point_4th.x	= (touch_data->data_point[20] | (touch_data->data_point[21] << 8));
		gesture->Point_4th.y	= (touch_data->data_point[22] | (touch_data->data_point[23] << 8));
	}

	if (gesture->gesture_type == SingleTap || gesture->gesture_type == DouTap) {
		gesture->Point_start.x = (touch_data->extra_gesture_info[0] | (touch_data->extra_gesture_info[1] << 8));
		gesture->Point_start.y = (touch_data->extra_gesture_info[2] | (touch_data->extra_gesture_info[3] << 8));
	}

	TPD_INFO("lpwg:0x%x, type:%d, clockwise: %d, points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n",
		touch_data->lpwg_gesture, gesture->gesture_type, gesture->clockwise, \
		gesture->Point_start.x, gesture->Point_start.y, \
		gesture->Point_end.x, gesture->Point_end.y, \
		gesture->Point_1st.x, gesture->Point_1st.y, \
		gesture->Point_2nd.x, gesture->Point_2nd.y, \
		gesture->Point_3rd.x, gesture->Point_3rd.y, \
		gesture->Point_4th.x, gesture->Point_4th.y);

	return 0;
}

static int syna_get_gesture_info_auto(void *chip_data,
					  struct gesture_info *gesture,
					  struct resolution_info *resolution_info)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	int max_x_inchip = 0;
	int max_y_inchip = 0;
	int max_x = 0;
	int max_y = 0;

	max_x_inchip = le2_to_uint(tcm_info->app_info.max_x) + 1;
	max_y_inchip = le2_to_uint(tcm_info->app_info.max_y) + 1;
	max_x = resolution_info->max_x;
	max_y = resolution_info->max_y;


	syna_get_gesture_info(chip_data, gesture);

	if (max_x_inchip == max_x && max_y_inchip == max_y) {
		return 0;
	}

	if (gesture->gesture_type == UnkownGesture) {
		return 0;
	}

	if (max_x_inchip != max_x) {
		gesture->Point_start.x = (gesture->Point_start.x * max_x) / max_x_inchip;
		gesture->Point_end.x	= (gesture->Point_end.x * max_x) / max_x_inchip;
		gesture->Point_1st.x	= (gesture->Point_1st.x * max_x) / max_x_inchip;
		gesture->Point_2nd.x	= (gesture->Point_2nd.x * max_x) / max_x_inchip;
		gesture->Point_3rd.x	= (gesture->Point_3rd.x * max_x) / max_x_inchip;
		gesture->Point_4th.x	= (gesture->Point_4th.x * max_x) / max_x_inchip;
	}

	if (max_y_inchip != max_y) {
		gesture->Point_start.y = (gesture->Point_start.y * max_y) / max_y_inchip;
		gesture->Point_end.y	= (gesture->Point_end.y * max_y) / max_y_inchip;
		gesture->Point_1st.y	= (gesture->Point_1st.y * max_y) / max_y_inchip;
		gesture->Point_2nd.y	= (gesture->Point_2nd.y * max_y) / max_y_inchip;
		gesture->Point_3rd.y	= (gesture->Point_3rd.y * max_y) / max_y_inchip;
		gesture->Point_4th.y	= (gesture->Point_4th.y * max_y) / max_y_inchip;
	}

	TPD_INFO("changed points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n",
		 gesture->Point_start.x, gesture->Point_start.y, \
		 gesture->Point_end.x, gesture->Point_end.y, \
		 gesture->Point_1st.x, gesture->Point_1st.y, \
		 gesture->Point_2nd.x, gesture->Point_2nd.y, \
		 gesture->Point_3rd.x, gesture->Point_3rd.y, \
		 gesture->Point_4th.x, gesture->Point_4th.y);

	return 0;
}

static void store_to_file(int fd, char *format, ...)
{
	va_list args;
	char buf[64] = {0};

	va_start(args, format);
	vsnprintf(buf, 64, format, args);
	va_end(args);

	if(fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
		ksys_write(fd, buf, strlen(buf));
#else
		sys_write(fd, buf, strlen(buf));
#endif
	}
}

int syna_tcm_get_frame_size_words(struct syna_tcm_data *tcm_info, bool image_only)
{
	unsigned int rows;
	unsigned int cols;
	unsigned int hybrid;
	unsigned int buttons;
	int size = 0;
	struct syna_tcm_app_info *app_info = &tcm_info->app_info;

	rows = le2_to_uint(app_info->num_of_image_rows);
	cols = le2_to_uint(app_info->num_of_image_cols);
	hybrid = le2_to_uint(app_info->has_hybrid_data);
	buttons = le2_to_uint(app_info->num_of_buttons);

	size = rows * cols;

	if (!image_only) {
		if (hybrid)
			size += rows + cols;
		size += buttons;
	}

	return size;
}

static int testing_run_prod_test_item(struct syna_tcm_data *tcm_info, enum test_item_bit test_code)
{
	int retval = 0;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;

	if (tcm_info->id_info.mode != MODE_APPLICATION || tcm_info->app_status != APP_STATUS_OK) {
		TPD_INFO("Application firmware not running\n");
		return -ENODEV;
	}

	LOCK_BUFFER(test_hcd->test_out);

	retval = syna_tcm_alloc_mem(&test_hcd->test_out,
								1);
	if (retval < 0) {
		TPD_INFO("Failed to allocate memory for test_hcd->test_out.buf\n");
		UNLOCK_BUFFER(test_hcd->test_out);
		return retval;
	}

	test_hcd->test_out.buf[0] = test_code;

	LOCK_BUFFER(test_hcd->test_resp);
	retval = syna_tcm_write_message(tcm_info,
									CMD_PRODUCTION_TEST,
									test_hcd->test_out.buf,
									1,
									&test_hcd->test_resp.buf,
									&test_hcd->test_resp.buf_size,
									&test_hcd->test_resp.data_length,
									RESPONSE_TIMEOUT_MS_LONG);
	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n",
				 STR(CMD_PRODUCTION_TEST));
		UNLOCK_BUFFER(test_hcd->test_resp);
		UNLOCK_BUFFER(test_hcd->test_out);
		return retval;
	}

	UNLOCK_BUFFER(test_hcd->test_resp);
	UNLOCK_BUFFER(test_hcd->test_out);

	return 0;
}

static uint32_t search_for_item(const struct firmware *fw, int item_cnt, uint8_t item_index)
{
	int i = 0;
	uint32_t item_offset = 0;
	struct syna_test_item_header *item_header = NULL;
	uint32_t *p_item_offset = (uint32_t *)(fw->data + sizeof(struct test_header_new));

	for (i = 0; i < item_cnt; i++) {
		item_header = (struct syna_test_item_header *)(fw->data + p_item_offset[i]);
		if (item_header->item_bit == item_index) {	  /*check the matched item offset*/
			item_offset = p_item_offset[i];
		}
	}

	return item_offset;
}

static int syna_int_pin_test(struct seq_file *s, struct syna_tcm_data *tcm_info,
							 struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	int eint_status, eint_count = 0, read_gpio_num = 10;

	TPD_INFO("%s start.\n", __func__);
	while(read_gpio_num--) {
		msleep(5);
		eint_status = gpio_get_value(syna_testdata->irq_gpio);
		if (eint_status == 1)
			eint_count--;
		else
			eint_count++;
		TPD_INFO("%s eint_count = %d  eint_status = %d\n", __func__, eint_count, eint_status);
	}
	if (eint_count == 10) {
		TPD_INFO("interrupt gpio is short to gnd.\n");
		if (!error_count)
			seq_printf(s, "interrupt gpio is short to gnd.\n");
		error_count++;
		return error_count;
	}

	return error_count;
}

static int syna_trx_short_test(struct seq_file *s, struct syna_tcm_data *tcm_info,
							   struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	uint8_t u_data8 = 0;
	int i = 0, j = 0, ret = 0;
	unsigned int checked_bits = 0, total_bits = 0;
	struct syna_test_item_header *item_header = NULL;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char *buf = NULL;

	total_bits = syna_testdata->TX_NUM + syna_testdata->RX_NUM;
	item_header = (struct syna_test_item_header *)(syna_testdata->fw->data + item_offset);
	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("trx short test magic number(%4x) is wrong.\n", item_header->item_magic);
		if (!error_count)
			seq_printf(s, "trx short test magic number(%4x) is wrong.\n", item_header->item_magic);
		error_count++;
		return error_count;
	}

	TPD_INFO("%s start.\n", __func__);
	ret = testing_run_prod_test_item(tcm_info, TYPE_TRX_SHORT);
	if (ret < 0) {
		TPD_INFO("run trx short test failed.\n");
		if (!error_count)
			seq_printf(s, "run trx short test failed.\n");
		error_count++;
		return error_count;
	}

	LOCK_BUFFER(test_hcd->test_resp);
	buf = test_hcd->test_resp.buf;
	TPD_INFO("%s read data size:%d\n", __func__, test_hcd->test_resp.data_length);
	store_to_file(syna_testdata->fd, "trx_short:\n");
	for (i = 0; i < test_hcd->test_resp.data_length;) {
		u_data8 = buf[i];
		store_to_file(syna_testdata->fd, "0x%02x, ", u_data8);
		for (j = 0; j < 8; j++) {
			if (u_data8 & (1 << j)) {
				TPD_INFO("trx short test failed at %d bits.\n", checked_bits + 1);
				if (!error_count)
					seq_printf(s, "trx short test failed at %d bits.\n", checked_bits + 1);
				error_count++;
			}
			checked_bits++;
			if (checked_bits >= total_bits)
				goto full_out;
		}

		i += 1;
	}

full_out:
	UNLOCK_BUFFER(test_hcd->test_resp);
	store_to_file(syna_testdata->fd, "\n");

	return error_count;
}

static int syna_trx_open_test(struct seq_file *s, struct syna_tcm_data *tcm_info,
							  struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	uint8_t u_data8 = 0;
	int i = 0, j = 0, ret = 0;
	unsigned int checked_bits = 0, total_bits = 0;
	struct syna_test_item_header *item_header = NULL;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char *buf = NULL;

	total_bits = syna_testdata->TX_NUM + syna_testdata->RX_NUM;
	item_header = (struct syna_test_item_header *)(syna_testdata->fw->data + item_offset);
	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("trx open test magic number(%4x) is wrong.\n", item_header->item_magic);
		if (!error_count)
			seq_printf(s, "trx open test magic number(%4x) is wrong.\n", item_header->item_magic);
		error_count++;
		return error_count;
	}

	TPD_INFO("%s start.\n", __func__);
	ret = testing_run_prod_test_item(tcm_info, TYPE_TRX_OPEN);
	if (ret < 0) {
		TPD_INFO("run trx open test failed.\n");
		if (!error_count)
			seq_printf(s, "run trx open test failed.\n");
		error_count++;
		return error_count;
	}

	LOCK_BUFFER(test_hcd->test_resp);
	buf = test_hcd->test_resp.buf;
	TPD_INFO("%s read data size:%d\n", __func__, test_hcd->test_resp.data_length);
	store_to_file(syna_testdata->fd, "tx_tx_open:\n");
	for (i = 0; i < test_hcd->test_resp.data_length;) {
		u_data8 = buf[i];
		store_to_file(syna_testdata->fd, "0x%02x, ", u_data8);
		for (j = 0; j < 8; j++) {
			if (0 == (u_data8 & (1 << j))) {
				TPD_INFO("trx open test failed at %d bits.\n", checked_bits + 1);
				if (!error_count)
					seq_printf(s, "trx open test failed at %d bits.\n", checked_bits + 1);
				error_count++;
			}
			checked_bits++;
			if (checked_bits >= total_bits)
				goto full_out;
		}

		i += 1;
	}

full_out:
	UNLOCK_BUFFER(test_hcd->test_resp);
	store_to_file(syna_testdata->fd, "\n");

	return error_count;
}

static int syna_trx_gndshort_test(struct seq_file *s, struct syna_tcm_data *tcm_info,
								  struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	uint8_t u_data8 = 0;
	int i = 0, j = 0, ret = 0;
	unsigned int checked_bits = 0, total_bits = 0;
	struct syna_test_item_header *item_header = NULL;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char *buf = NULL;

	total_bits = syna_testdata->TX_NUM + syna_testdata->RX_NUM;
	item_header = (struct syna_test_item_header *)(syna_testdata->fw->data + item_offset);
	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("trx gndshort test magic number(%4x) is wrong.\n", item_header->item_magic);
		if (!error_count)
			seq_printf(s, "trx gndshort test magic number(%4x) is wrong.\n", item_header->item_magic);
		error_count++;
		return error_count;
	}

	TPD_INFO("%s start.\n", __func__);
	ret = testing_run_prod_test_item(tcm_info, TYPE_TRXGND_SHORT);
	if (ret < 0) {
		TPD_INFO("run trx gndshort test failed.\n");
		if (!error_count)
			seq_printf(s, "run trx gndshort test failed.\n");
		error_count++;
		return error_count;
	}

	LOCK_BUFFER(test_hcd->test_resp);
	buf = test_hcd->test_resp.buf;
	TPD_INFO("%s read data size:%d\n", __func__, test_hcd->test_resp.data_length);
	store_to_file(syna_testdata->fd, "tx_tx_gndshort:\n");
	for (i = 0; i < test_hcd->test_resp.data_length;) {
		u_data8 = buf[i];
		store_to_file(syna_testdata->fd, "0x%02x, ", u_data8);
		for (j = 0; j < 8; j++) {
			if (0 == (u_data8 & (1 << j))) {
				TPD_INFO("trx gndshort test failed at %d bits.\n", checked_bits + 1);
				if (!error_count)
					seq_printf(s, "trx gndshort test failed at %d bits.\n", checked_bits + 1);
				error_count++;
			}
			checked_bits++;
			if (checked_bits >= total_bits)
				goto full_out;
		}

		i += 1;
	}

full_out:
	UNLOCK_BUFFER(test_hcd->test_resp);
	store_to_file(syna_testdata->fd, "\n");

	return error_count;
}

static int syna_full_rawcap_test(struct seq_file *s, struct syna_tcm_data *tcm_info,
								 struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	uint16_t u_data16 = 0;
	int i = 0, ret = 0, index = 0, byte_cnt = 2;
	struct syna_test_item_header *item_header = NULL;
	int32_t *p_mutual_p = NULL, *p_mutual_n = NULL;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char *buf = NULL;

	item_header = (struct syna_test_item_header *)(syna_testdata->fw->data + item_offset);
	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("full rawcap test magic number(%4x) is wrong.\n", item_header->item_magic);
		if (!error_count)
			seq_printf(s, "full rawcap test magic number(%4x) is wrong.\n", item_header->item_magic);
		error_count++;
		return error_count;
	}
	if (item_header->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
		p_mutual_p = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset);
		p_mutual_n = (int32_t *)(syna_testdata->fw->data + item_header->floor_limit_offset);
	} else {
		TPD_INFO("full rawcap test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		if (!error_count)
			seq_printf(s, "full rawcap test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		error_count++;
		return error_count;
	}

	TPD_INFO("%s start.\n", __func__);
	ret = testing_run_prod_test_item(tcm_info, TYPE_FULLRAW_CAP);
	if (ret < 0) {
		TPD_INFO("run full rawcap test failed.\n");
		if (!error_count)
			seq_printf(s, "run full rawcap test failed.\n");
		error_count++;
		return error_count;
	}

	LOCK_BUFFER(test_hcd->test_resp);
	buf = test_hcd->test_resp.buf;
	TPD_INFO("%s read data size:%d\n", __func__, test_hcd->test_resp.data_length);
	store_to_file(syna_testdata->fd, "full_rawcap:");
	for (i = 0; i < test_hcd->test_resp.data_length;) {
		index = i / byte_cnt;
		u_data16 = (buf[i] | (buf[i + 1] << 8));
		if (0 == index % (syna_testdata->RX_NUM))
			store_to_file(syna_testdata->fd, "\n");
		store_to_file(syna_testdata->fd, "%04d, ", u_data16);
		if ((u_data16 < p_mutual_n[index]) || (u_data16 > p_mutual_p[index])) {
			TPD_INFO("full rawcap test failed at node[%d]=%d [%d %d].\n", index, u_data16, p_mutual_n[index], p_mutual_p[index]);
			if (!error_count)
				seq_printf(s, "full rawcap test failed at node[%d]=%d [%d %d].\n", index, u_data16, p_mutual_n[index], p_mutual_p[index]);
			error_count++;
		}

		i += byte_cnt;
	}
	UNLOCK_BUFFER(test_hcd->test_resp);
	store_to_file(syna_testdata->fd, "\n");

	return error_count;
}

static int syna_delta_noise_test(struct seq_file *s, struct syna_tcm_data *tcm_info,
								 struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	int16_t data16 = 0;
	int i = 0, ret = 0, index = 0, byte_cnt = 2;
	struct syna_test_item_header *item_header = NULL;
	int32_t *p_mutual_p = NULL, *p_mutual_n = NULL;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char *buf = NULL;

	item_header = (struct syna_test_item_header *)(syna_testdata->fw->data + item_offset);
	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("delta noise test magic number(%4x) is wrong.\n", item_header->item_magic);
		if (!error_count)
			seq_printf(s, "delta noise test magic number(%4x) is wrong.\n", item_header->item_magic);
		error_count++;
		return error_count;
	}
	if (item_header->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
		p_mutual_p = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset);
		p_mutual_n = (int32_t *)(syna_testdata->fw->data + item_header->floor_limit_offset);
	} else {
		TPD_INFO("delta noise test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		if (!error_count)
			seq_printf(s, "delta noise test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		error_count++;
		return error_count;
	}

	TPD_INFO("%s start.\n", __func__);
	ret = testing_run_prod_test_item(tcm_info, TYPE_DELTA_NOISE);
	if (ret < 0) {
		TPD_INFO("run delta noise rawcap test failed.\n");
		if (!error_count)
			seq_printf(s, "run delta noise test failed.\n");
		error_count++;
		return error_count;
	}

	LOCK_BUFFER(test_hcd->test_resp);
	buf = test_hcd->test_resp.buf;
	TPD_INFO("%s read data size:%d\n", __func__, test_hcd->test_resp.data_length);
	store_to_file(syna_testdata->fd, "delta_noise:");
	for (i = 0; i < test_hcd->test_resp.data_length;) {
		index = i / byte_cnt;
		data16 = (buf[i] | (buf[i + 1] << 8));
		if (0 == index % (syna_testdata->RX_NUM))
			store_to_file(syna_testdata->fd, "\n");
		store_to_file(syna_testdata->fd, "%04d, ", data16);
		if ((data16 < p_mutual_n[index]) || (data16 > p_mutual_p[index])) {
			TPD_INFO("delta noise test failed at node[%d]=%d [%d %d].\n", index, data16, p_mutual_n[index], p_mutual_p[index]);
			if (!error_count)
				seq_printf(s, "delta noise test failed at node[%d]=%d [%d %d].\n", index, data16, p_mutual_n[index], p_mutual_p[index]);
			error_count++;
		}

		i += byte_cnt;
	}
	UNLOCK_BUFFER(test_hcd->test_resp);
	store_to_file(syna_testdata->fd, "\n");

	return error_count;
}

static int syna_hybrid_rawcap_test(struct seq_file *s, struct syna_tcm_data *tcm_info,
								   struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	int32_t data32 = 0;
	int i = 0, ret = 0, index = 0, byte_cnt = 4;
	struct syna_test_item_header *item_header = NULL;
	int32_t *p_hybridcap_p = NULL, *p_hybridcap_n = NULL;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char *buf = NULL;

	item_header = (struct syna_test_item_header *)(syna_testdata->fw->data + item_offset);
	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("hybrid_rawcap test magic number(%4x) is wrong.\n", item_header->item_magic);
		if (!error_count)
			seq_printf(s, "hybrid_rawcap test magic number(%4x) is wrong.\n", item_header->item_magic);
		error_count++;
		return error_count;
	}
	if (item_header->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
		p_hybridcap_p = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset);
		p_hybridcap_n = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset + 4 * (syna_testdata->TX_NUM + syna_testdata->RX_NUM));
	} else {
		TPD_INFO("hybrid_rawcap test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		if (!error_count)
			seq_printf(s, "hybrid_rawcap test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		error_count++;
		return error_count;
	}

	TPD_INFO("%s start.\n", __func__);
	ret = testing_run_prod_test_item(tcm_info, TYPE_HYBRIDRAW_CAP);
	if (ret < 0) {
		TPD_INFO("run hybrid rawcap test failed.\n");
		if (!error_count)
			seq_printf(s, "run hybrid rawcap test failed.\n");
		error_count++;
		return error_count;
	}

	LOCK_BUFFER(test_hcd->test_resp);
	buf = test_hcd->test_resp.buf;
	TPD_INFO("%s read data size:%d\n", __func__, test_hcd->test_resp.data_length);
	store_to_file(syna_testdata->fd, "hybrid_rawcap:\n");
	for (i = 0; i < test_hcd->test_resp.data_length;) {
		index = i / byte_cnt;
		data32 = (buf[i] | (buf[i + 1] << 8) | (buf[i + 2] << 16) | (buf[i + 3] << 24));
		store_to_file(syna_testdata->fd, "%08d, ", data32);
		if ((data32 < p_hybridcap_n[index]) || (data32 > p_hybridcap_p[index])) {
			TPD_INFO("hybrid rawcap test failed at node[%d]=%d [%d %d].\n", index, data32, p_hybridcap_n[index], p_hybridcap_p[index]);
			if (!error_count)
				seq_printf(s, "hybrid rawcap test failed at node[%d]=%d [%d %d].\n", index, data32, p_hybridcap_n[index], p_hybridcap_p[index]);
			error_count++;
		}

		i += byte_cnt;
	}
	UNLOCK_BUFFER(test_hcd->test_resp);
	store_to_file(syna_testdata->fd, "\n");

	return error_count;
}

static int syna_hybrid_rawcap_test_ad(struct seq_file *s, struct syna_tcm_data *tcm_info,
								   struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	int32_t data32 = 0;
	int i = 0, ret = 0, index = 0, byte_cnt = 2;
	struct syna_test_item_header *item_header = NULL;
	int32_t *p_hybridcap_p = NULL, *p_hybridcap_n = NULL;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char *buf = NULL;

	item_header = (struct syna_test_item_header *)(syna_testdata->fw->data + item_offset);
	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("hybrid_rawcap_whth_ad test magic number(%4x) is wrong.\n", item_header->item_magic);
		if (!error_count)
			seq_printf(s, "hybrid_rawcap_whth_ad test magic number(%4x) is wrong.\n", item_header->item_magic);
		error_count++;
		return error_count;
	}
	if (item_header->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
		p_hybridcap_p = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset);
		p_hybridcap_n = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset + 4 * (syna_testdata->TX_NUM + syna_testdata->RX_NUM));
	} else {
		TPD_INFO("hybrid_rawcap_whth_ad test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		if (!error_count)
			seq_printf(s, "hybrid_rawcap_whth_ad test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		error_count++;
		return error_count;
	}

	TPD_INFO("%s start.\n", __func__);
	/*0x47 = 71 TYPE_HYBRIDRAW_CAP_WITH_AD + 24*/
	ret = testing_run_prod_test_item(tcm_info, TYPE_HYBRIDRAW_CAP_WITH_AD + 24);
	if (ret < 0) {
		TPD_INFO("run hybrid rawcap whth_ad test failed.\n");
		if (!error_count)
			seq_printf(s, "run hybrid rawcap whth_ad test failed.\n");
		error_count++;
		return error_count;
	}

	LOCK_BUFFER(test_hcd->test_resp);
	buf = test_hcd->test_resp.buf;
	TPD_INFO("%s read data size:%d\n", __func__, test_hcd->test_resp.data_length);
	store_to_file(syna_testdata->fd, "hybrid_rawcap_whth_ad:\n");
	for (i = 0; i < test_hcd->test_resp.data_length;) {
		index = i / byte_cnt;
		data32 = (buf[i] | (buf[i + 1] << 8));
		store_to_file(syna_testdata->fd, "%08d, ", data32);
		if ((data32 < p_hybridcap_n[index]) || (data32 > p_hybridcap_p[index])) {
			TPD_INFO("hybrid rawcap whth_ad test failed at node[%d]=%d [%d %d].\n", index, data32, p_hybridcap_n[index], p_hybridcap_p[index]);
			if (!error_count)
				seq_printf(s, "hybrid rawcap whth_ad test failed at node[%d]=%d [%d %d].\n", index, data32, p_hybridcap_n[index], p_hybridcap_p[index]);
			error_count++;
		}

		i += byte_cnt;
	}
	UNLOCK_BUFFER(test_hcd->test_resp);
	store_to_file(syna_testdata->fd, "\n");

	return error_count;
}

static int syna_rawcap_test(struct seq_file *s, struct syna_tcm_data *tcm_info,
							struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	int16_t data16 = 0;
	int i = 0, ret = 0, index = 0, byte_cnt = 2;
	struct syna_test_item_header *item_header = NULL;
	int32_t *p_mutual_p = NULL, *p_mutual_n = NULL;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char *buf = NULL;

	item_header = (struct syna_test_item_header *)(syna_testdata->fw->data + item_offset);
	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("raw cap test magic number(%4x) is wrong.\n", item_header->item_magic);
		if (!error_count)
			seq_printf(s, "raw cap test magic number(%4x) is wrong.\n", item_header->item_magic);
		error_count++;
		return error_count;
	}
	if (item_header->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
		p_mutual_p = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset);
		p_mutual_n = (int32_t *)(syna_testdata->fw->data + item_header->floor_limit_offset);
	} else {
		TPD_INFO("raw cap test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		if (!error_count)
			seq_printf(s, "raw cap test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		error_count++;
		return error_count;
	}

	TPD_INFO("%s start.\n", __func__);
	ret = testing_run_prod_test_item(tcm_info, TYPE_RAW_CAP);
	if (ret < 0) {
		TPD_INFO("run raw cap test failed.\n");
		if (!error_count)
			seq_printf(s, "run raw cap test failed.\n");
		error_count++;
		return error_count;
	}

	LOCK_BUFFER(test_hcd->test_resp);
	buf = test_hcd->test_resp.buf;
	TPD_INFO("%s read data size:%d\n", __func__, test_hcd->test_resp.data_length);
	store_to_file(syna_testdata->fd, "raw_cap:");
	for (i = 0; i < test_hcd->test_resp.data_length;) {
		index = i / byte_cnt;
		data16 = (buf[i] | (buf[i + 1] << 8));
		if (0 == index % (syna_testdata->RX_NUM))
			store_to_file(syna_testdata->fd, "\n");
		store_to_file(syna_testdata->fd, "%04d, ", data16);
		if ((data16 < p_mutual_n[index]) || (data16 > p_mutual_p[index])) {
			TPD_INFO("rawcap test failed at node[%d]=%d [%d %d].\n", index, data16, p_mutual_n[index], p_mutual_p[index]);
			if (!error_count)
				seq_printf(s, "rawcap test failed at node[%d]=%d [%d %d].\n", index, data16, p_mutual_n[index], p_mutual_p[index]);
			error_count++;
		}

		i += byte_cnt;
	}
	UNLOCK_BUFFER(test_hcd->test_resp);
	store_to_file(syna_testdata->fd, "\n");

	return error_count;
}

static int syna_trex_shortcustom_test(struct seq_file *s, struct syna_tcm_data *tcm_info,
									  struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	uint16_t u_data16 = 0;
	int i = 0, ret = 0, index = 0, byte_cnt = 2;
	struct syna_test_item_header *item_header = NULL;
	int32_t *p_tx_p = NULL, *p_tx_n = NULL;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char *buf = NULL;

	item_header = (struct syna_test_item_header *)(syna_testdata->fw->data + item_offset);
	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("trex short custom test magic number(%4x) is wrong.\n", item_header->item_magic);
		if (!error_count)
			seq_printf(s, "trex short custom test magic number(%4x) is wrong.\n", item_header->item_magic);
		error_count++;
		return error_count;
	}
	if (item_header->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
		p_tx_p = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset);
		p_tx_n = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset + 4 * (syna_testdata->TX_NUM + syna_testdata->RX_NUM));
	} else {
		TPD_INFO("trex short custom test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		if (!error_count)
			seq_printf(s, "trex short custom test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		error_count++;
		return error_count;
	}

	TPD_INFO("%s start.\n", __func__);
	ret = testing_run_prod_test_item(tcm_info, TYPE_TREXSHORT_CUSTOM);
	if (ret < 0) {
		TPD_INFO("run trex short custom test failed.\n");
		if (!error_count)
			seq_printf(s, "run trex short custom test failed.\n");
		error_count++;
		return error_count;
	}

	LOCK_BUFFER(test_hcd->test_resp);
	buf = test_hcd->test_resp.buf;
	TPD_INFO("%s read data size:%d\n", __func__, test_hcd->test_resp.data_length);
	store_to_file(syna_testdata->fd, "trex_shorcustom:\n");
	for (i = 0; i < test_hcd->test_resp.data_length;) {
		index = i / byte_cnt;
		u_data16 = (buf[i] | (buf[i + 1] << 8));
		store_to_file(syna_testdata->fd, "%04d, ", u_data16);
		if ((u_data16 < p_tx_n[index]) || (u_data16 > p_tx_p[index])) {
			TPD_INFO("trex_shorcustom test failed at node[%d]=%d [%d %d].\n", index, u_data16, p_tx_n[index], p_tx_p[index]);
			if (!error_count)
				seq_printf(s, "trex_shorcustom test failed at node[%d]=%d [%d %d].\n", index, u_data16, p_tx_n[index], p_tx_p[index]);
			error_count++;
		}

		i += byte_cnt;
	}
	UNLOCK_BUFFER(test_hcd->test_resp);
	store_to_file(syna_testdata->fd, "\n");

	return error_count;
}

static int syna_hybrid_diffcbc_test(struct seq_file *s, struct syna_tcm_data *tcm_info,
									struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	uint16_t u_data16 = 0;
	int i = 0, ret = 0, index = 0, byte_cnt = 2;
	struct syna_test_item_header *item_header = NULL;
	int32_t *p_selfdata_p = NULL, *p_selfdata_n = NULL;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char *buf = NULL;

	item_header = (struct syna_test_item_header *)(syna_testdata->fw->data + item_offset);
	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("hybrid diffcbc test magic number(%4x) is wrong.\n", item_header->item_magic);
		if (!error_count)
			seq_printf(s, "hybrid diffcbc test magic number(%4x) is wrong.\n", item_header->item_magic);
		error_count++;
		return error_count;
	}
	if (item_header->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
		p_selfdata_p = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset);
		p_selfdata_n = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset + 4 * (syna_testdata->TX_NUM + syna_testdata->RX_NUM));
	} else {
		TPD_INFO("hybrid diffcbc test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		if (!error_count)
			seq_printf(s, "hybrid diffcbc test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		error_count++;
		return error_count;
	}

	TPD_INFO("%s start.\n", __func__);
	ret = testing_run_prod_test_item(tcm_info, TYPE_HYBRIDABS_DIFF_CBC);
	if (ret < 0) {
		TPD_INFO("run hybrid diffcbc test failed.\n");
		if (!error_count)
			seq_printf(s, "run hybrid diffcbc test failed.\n");
		error_count++;
		return error_count;
	}

	LOCK_BUFFER(test_hcd->test_resp);
	buf = test_hcd->test_resp.buf;
	TPD_INFO("%s read data size:%d\n", __func__, test_hcd->test_resp.data_length);
	store_to_file(syna_testdata->fd, "hybrid_diffwithcbc:\n");
	for (i = 0; i < test_hcd->test_resp.data_length;) {
		index = i / byte_cnt;
		u_data16 = (buf[i] | (buf[i + 1] << 8));
		store_to_file(syna_testdata->fd, "%04d, ", u_data16);
		if ((u_data16 < p_selfdata_n[index]) || (u_data16 > p_selfdata_p[index])) {
			TPD_INFO("hybrid diffcbc test failed at node[%d]=%d [%d %d].\n", index, u_data16, p_selfdata_n[index], p_selfdata_p[index]);
			if (!error_count)
				seq_printf(s, "hybrid diffcbc test failed at node[%d]=%d [%d %d].\n", index, u_data16, p_selfdata_n[index], p_selfdata_p[index]);
			error_count++;
		}

		i += byte_cnt;
	}
	UNLOCK_BUFFER(test_hcd->test_resp);
	store_to_file(syna_testdata->fd, "\n");

	return error_count;
}

static int syna_hybrid_absnoise_test(struct seq_file *s, struct syna_tcm_data *tcm_info,
									 struct syna_testdata *syna_testdata, int item_offset, int error_count)
{
	int16_t data16 = 0;
	int i = 0, ret = 0, index = 0, byte_cnt = 2;
	struct syna_test_item_header *item_header = NULL;
	int32_t *p_selfdata_p = NULL, *p_selfdata_n = NULL;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char *buf = NULL;

	item_header = (struct syna_test_item_header *)(syna_testdata->fw->data + item_offset);
	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("hybrid abs noise test magic number(%4x) is wrong.\n", item_header->item_magic);
		if (!error_count)
			seq_printf(s, "hybrid abs noise test magic number(%4x) is wrong.\n", item_header->item_magic);
		error_count++;
		return error_count;
	}
	if (item_header->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
		p_selfdata_p = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset);
		p_selfdata_n = (int32_t *)(syna_testdata->fw->data + item_header->top_limit_offset + 4 * (syna_testdata->TX_NUM + syna_testdata->RX_NUM));
	} else {
		TPD_INFO("hybrid abs noise test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		if (!error_count)
			seq_printf(s, "hybrid abs noise test limit type(%2x) is wrong.\n", item_header->item_limit_type);
		error_count++;
		return error_count;
	}

	TPD_INFO("%s start.\n", __func__);
	ret = testing_run_prod_test_item(tcm_info, TYPE_HYBRIDABS_NOSIE);
	if (ret < 0) {
		TPD_INFO("run hybrid abs noise test failed.\n");
		if (!error_count)
			seq_printf(s, "run hybrid abs noise test failed.\n");
		error_count++;
		return error_count;
	}

	LOCK_BUFFER(test_hcd->test_resp);
	buf = test_hcd->test_resp.buf;
	TPD_INFO("%s read data size:%d\n", __func__, test_hcd->test_resp.data_length);
	store_to_file(syna_testdata->fd, "hybrid_absnoise:\n");
	for (i = 0; i < test_hcd->test_resp.data_length;) {
		index = i / byte_cnt;
		data16 = (buf[i] | (buf[i + 1] << 8));
		store_to_file(syna_testdata->fd, "%04d, ", data16);
		if ((data16 < p_selfdata_n[index]) || (data16 > p_selfdata_p[index])) {
			TPD_INFO("hybrid abs noise test failed at node[%d]=%d [%d %d].\n", index, data16, p_selfdata_n[index], p_selfdata_p[index]);
			if (!error_count)
				seq_printf(s, "hybrid abs noise test failed at node[%d]=%d [%d %d].\n", index, data16, p_selfdata_n[index], p_selfdata_p[index]);
			error_count++;
		}

		i += byte_cnt;
	}
	UNLOCK_BUFFER(test_hcd->test_resp);
	store_to_file(syna_testdata->fd, "\n");

	return error_count;
}

static void syna_auto_test(struct seq_file *s, void *chip_data, struct syna_testdata *syna_testdata)
{
	int i = 0, error_count = 0, item_offset = 0, item_cnt = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct hw_resource *hw_res = tcm_info->hw_res;
	int k, rst_val;

	/*calculate the total test item*/
	for (i = 0; i < 8 * sizeof(syna_testdata->test_item); i++) {
		if ((syna_testdata->test_item >> i) & 0x01) {
			item_cnt++;
		}
	}
	TPD_INFO("this test have %d test_item.\n", item_cnt);

	/*tp int short test*/
	syna_int_pin_test(s, tcm_info, syna_testdata, item_offset, error_count);

	/*check if it support tx and rx short test*/
	if (syna_testdata->test_item & (1 << TYPE_TRX_SHORT)) {
		item_offset = search_for_item(syna_testdata->fw, item_cnt, TYPE_TRX_SHORT);
		if (!item_offset) {
			error_count++;
			TPD_INFO("search for item(%d) limit offset failed.\n", TYPE_TRX_SHORT);
			seq_printf(s, "search for item(%d) limit offset failed.\n", TYPE_TRX_SHORT);
		} else {
			error_count = syna_trx_short_test(s, tcm_info, syna_testdata, item_offset, error_count);
		}
	}

	/*check if it support tx and rx open test*/
	if (syna_testdata->test_item & (1 << TYPE_TRX_OPEN)) {
		item_offset = search_for_item(syna_testdata->fw, item_cnt, TYPE_TRX_OPEN);
		if (!item_offset) {
			error_count++;
			TPD_INFO("search for item(%d) limit offset failed.\n", TYPE_TRX_OPEN);
			seq_printf(s, "search for item(%d) limit offset failed.\n", TYPE_TRX_OPEN);
		} else {
			error_count = syna_trx_open_test(s, tcm_info, syna_testdata, item_offset, error_count);
		}
	}

	/*check if it support tx and rx gnd short test*/
	if (syna_testdata->test_item & (1 << TYPE_TRXGND_SHORT)) {
		item_offset = search_for_item(syna_testdata->fw, item_cnt, TYPE_TRXGND_SHORT);
		if (!item_offset) {
			error_count++;
			TPD_INFO("search for item(%d) limit offset failed.\n", TYPE_TRXGND_SHORT);
			seq_printf(s, "search for item(%d) limit offset failed.\n", TYPE_TRXGND_SHORT);
		} else {
			error_count = syna_trx_gndshort_test(s, tcm_info, syna_testdata, item_offset, error_count);
		}
	}

	/*check if it support full rawcap test*/
	if (syna_testdata->test_item & (1 << TYPE_FULLRAW_CAP)) {
		item_offset = search_for_item(syna_testdata->fw, item_cnt, TYPE_FULLRAW_CAP);
		if (!item_offset) {
			error_count++;
			TPD_INFO("search for item(%d) limit offset failed.\n", TYPE_FULLRAW_CAP);
			seq_printf(s, "search for item(%d) limit offset failed.\n", TYPE_FULLRAW_CAP);
		} else {
			error_count = syna_full_rawcap_test(s, tcm_info, syna_testdata, item_offset, error_count);
		}
	}

	/*check if it support delta noise test*/
	if (syna_testdata->test_item & (1 << TYPE_DELTA_NOISE)) {
		item_offset = search_for_item(syna_testdata->fw, item_cnt, TYPE_DELTA_NOISE);
		if (!item_offset) {
			error_count++;
			TPD_INFO("search for item(%d) limit offset failed.\n", TYPE_DELTA_NOISE);
			seq_printf(s, "search for item(%d) limit offset failed.\n", TYPE_DELTA_NOISE);
		} else {
			error_count = syna_delta_noise_test(s, tcm_info, syna_testdata, item_offset, error_count);
		}
	}

	/*check if it support hybrid rawcap test*/
	if (syna_testdata->test_item & (1 << TYPE_HYBRIDRAW_CAP)) {
		item_offset = search_for_item(syna_testdata->fw, item_cnt, TYPE_HYBRIDRAW_CAP);
		if (!item_offset) {
			error_count++;
			TPD_INFO("search for item(%d) limit offset failed.\n", TYPE_HYBRIDRAW_CAP);
			seq_printf(s, "search for item(%d) limit offset failed.\n", TYPE_HYBRIDRAW_CAP);
		} else {
			error_count = syna_hybrid_rawcap_test(s, tcm_info, syna_testdata, item_offset, error_count);
		}
	}

	/*check if it support hybrid rawcap test with ad*/
	if (syna_testdata->test_item & (uint64_t) ((uint64_t) 1 << TYPE_HYBRIDRAW_CAP_WITH_AD)) {
		TPD_INFO("search for item(%d) limit offset .\n", TYPE_HYBRIDRAW_CAP_WITH_AD);
		item_offset = search_for_item(syna_testdata->fw, item_cnt, TYPE_HYBRIDRAW_CAP_WITH_AD);
		if (!item_offset) {
			error_count++;
			TPD_INFO("search for item(%d) limit offset failed.\n", TYPE_HYBRIDRAW_CAP_WITH_AD);
			seq_printf(s, "search for item(%d) limit offset failed.\n", TYPE_HYBRIDRAW_CAP_WITH_AD);
		} else {
			error_count = syna_hybrid_rawcap_test_ad(s, tcm_info, syna_testdata, item_offset, error_count);
		}
	}
	/*check if it support rawcap test*/
	if (syna_testdata->test_item & (1 << TYPE_RAW_CAP)) {
		item_offset = search_for_item(syna_testdata->fw, item_cnt, TYPE_RAW_CAP);
		if (!item_offset) {
			error_count++;
			TPD_INFO("search for item(%d) limit offset failed.\n", TYPE_RAW_CAP);
			seq_printf(s, "search for item(%d) limit offset failed.\n", TYPE_RAW_CAP);
		} else {
			error_count = syna_rawcap_test(s, tcm_info, syna_testdata, item_offset, error_count);
		}
	}

	/*check if it support trex shor custom test*/
	if (syna_testdata->test_item & (1 << TYPE_TREXSHORT_CUSTOM)) {
		item_offset = search_for_item(syna_testdata->fw, item_cnt, TYPE_TREXSHORT_CUSTOM);
		if (!item_offset) {
			error_count++;
			TPD_INFO("search for item(%d) limit offset failed.\n", TYPE_TREXSHORT_CUSTOM);
			seq_printf(s, "search for item(%d) limit offset failed.\n", TYPE_TREXSHORT_CUSTOM);
		} else {
			error_count = syna_trex_shortcustom_test(s, tcm_info, syna_testdata, item_offset, error_count);
		}
	}

	/*check if it support hybrid absdiff with cbc test*/
	if (syna_testdata->test_item & (1 << TYPE_HYBRIDABS_DIFF_CBC)) {
		item_offset = search_for_item(syna_testdata->fw, item_cnt, TYPE_HYBRIDABS_DIFF_CBC);
		if (!item_offset) {
			error_count++;
			TPD_INFO("search for item(%d) limit offset failed.\n", TYPE_HYBRIDABS_DIFF_CBC);
			seq_printf(s, "search for item(%d) limit offset failed.\n", TYPE_HYBRIDABS_DIFF_CBC);
		} else {
			error_count = syna_hybrid_diffcbc_test(s, tcm_info, syna_testdata, item_offset, error_count);
		}
	}

	tcm_info->identify_state = 0;
	syna_tcm_reset(tcm_info);

	/*check if it support hybrid noise test*/
	if (syna_testdata->test_item & (1 << TYPE_HYBRIDABS_NOSIE)) {
		item_offset = search_for_item(syna_testdata->fw, item_cnt, TYPE_HYBRIDABS_NOSIE);
		if (!item_offset) {
			error_count++;
			TPD_INFO("search for item(%d) limit offset failed.\n", TYPE_HYBRIDABS_NOSIE);
			seq_printf(s, "search for item(%d) limit offset failed.\n", TYPE_HYBRIDABS_NOSIE);
		} else {
			error_count = syna_hybrid_absnoise_test(s, tcm_info, syna_testdata, item_offset, error_count);
		}
	}
	/*check RST state*/
	if (gpio_is_valid(hw_res->reset_gpio)) {
		gpio_direction_input(hw_res->reset_gpio);
		for (k = 0; k < 10; k++) {
			msleep(2);
			rst_val = gpio_get_value(hw_res->reset_gpio);
			TPD_INFO("read rst gpio count = %d, rst_val = %d\n", k, rst_val);
			if (0 == rst_val) {
				TPD_INFO("check rst gpio state failed.\n");
				seq_printf(s, "check rst gpio state failed.\n");
				error_count++;
				break;
			}
		}

		gpio_direction_output(hw_res->reset_gpio, 1);
	} else {
		TPD_INFO("rst gpio is not valid!\n");
	}

	if (0 == tcm_info->identify_state) {
		TPD_INFO("receive REPORT_IDENTIFY failed!\n");
		seq_printf(s, "receive REPORT_IDENTIFY failed.\n");
		error_count++;
	}

	seq_printf(s, "imageid = 0x%llu customID 0x%s\n", syna_testdata->TP_FW, tcm_info->app_info.customer_config_id);
	seq_printf(s, "%d error(s). %s\n", error_count, error_count ? "" : "All test passed.");

	tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_TEST_AUTO, &error_count);

	TPD_INFO(" TP auto test %d error(s). %s\n", error_count, error_count ? "" : "All test passed.");
}

static int syna_tcm_collect_reports(struct syna_tcm_data *tcm_info, enum report_type report_type, unsigned int num_of_reports)
{
	int retval;
	bool completed = false;
	unsigned int timeout;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char out[2] = {0};
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0;
	unsigned int resp_length = 0;

	test_hcd->report_index = 0;
	test_hcd->report_type = report_type;
	test_hcd->num_of_reports = num_of_reports;

	reinit_completion(&report_complete);

	out[0] = test_hcd->report_type;

	retval = syna_tcm_write_message(tcm_info,
									CMD_ENABLE_REPORT,
									out,
									1,
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write message %s\n", STR(CMD_ENABLE_REPORT));
		completed = false;
		goto exit;
	}

	timeout = REPORT_TIMEOUT_MS * num_of_reports;

	retval = wait_for_completion_timeout(&report_complete,
										 msecs_to_jiffies(timeout));
	if (retval == 0) {
		TPD_INFO("Timed out waiting for report collection\n");
	} else {
		completed = true;
	}

	out[0] = test_hcd->report_type;

	retval = syna_tcm_write_message(tcm_info,
									CMD_DISABLE_REPORT,
									out,
									1,
									&resp_buf,
									&resp_buf_size,
									&resp_length,
									0);
	if (retval < 0) {
		TPD_INFO("Failed to write message %s\n", STR(CMD_DISABLE_REPORT));
	}

	if (!completed) {
		retval = -EIO;
	}
exit:

	return retval;
}

static void syna_tcm_test_report(struct syna_tcm_data *tcm_info)
{
	int retval;
	unsigned int offset, report_size;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;

	if (tcm_info->report.id != test_hcd->report_type) {
		TPD_INFO("Not request report type\n");
		return;
	}

	report_size = tcm_info->report.buffer.data_length;
	LOCK_BUFFER(test_hcd->report);

	if (test_hcd->report_index == 0) {
		retval = syna_tcm_alloc_mem(&test_hcd->report, report_size * test_hcd->num_of_reports);
		if (retval < 0) {
			TPD_INFO("Failed to allocate memory\n");
			UNLOCK_BUFFER(test_hcd->report);
			return;
		}
	}

	if (test_hcd->report_index < test_hcd->num_of_reports) {
		offset = report_size * test_hcd->report_index;
		retval = secure_memcpy(test_hcd->report.buf + offset,
							   test_hcd->report.buf_size - offset,
							   tcm_info->report.buffer.buf,
							   tcm_info->report.buffer.buf_size,
							   tcm_info->report.buffer.data_length);
		if (retval < 0) {
			TPD_INFO("Failed to copy report data\n");

			UNLOCK_BUFFER(test_hcd->report);
			return;
		}

		test_hcd->report_index++;
		test_hcd->report.data_length += report_size;
	}

	UNLOCK_BUFFER(test_hcd->report);

	if (test_hcd->report_index == test_hcd->num_of_reports) {
		complete(&report_complete);
	}
	return;
}

static void syna_tcm_format_print(struct seq_file *s, struct syna_tcm_data *tcm_info, char *buffer)
{
	unsigned int row, col;
	unsigned int rows, cols;
	unsigned int cnt = 0;
	short *pdata_16;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;

	rows = le2_to_uint(tcm_info->app_info.num_of_image_rows);
	cols = le2_to_uint(tcm_info->app_info.num_of_image_cols);

	TPD_INFO("report size:%d\n", test_hcd->report.data_length);
	if (buffer == NULL)
		pdata_16 = (short *)&test_hcd->report.buf[0];
	else
		pdata_16 = (short *)buffer;

	for (row = 0; row < rows; row++) {
		seq_printf(s, "[%02d] ", row);
		for (col = 0; col < cols; col++) {
			seq_printf(s, "%5d ", *pdata_16);
			pdata_16++;
		}
		seq_printf(s, "\n");
	}

	if (test_hcd->report.data_length == rows * cols * 2 + (rows + cols) * 2) {
		for (cnt = 0; cnt < rows + cols; cnt++) {
			seq_printf(s, "%5d ", *pdata_16);
			pdata_16++;
		}
	}
	seq_printf(s, "\n");

	return;
}

static void syna_tcm_format_unsigned_print(struct seq_file *s, struct syna_tcm_data *tcm_info, char *buffer)
{
	unsigned int row, col;
	unsigned int rows, cols;
	unsigned int cnt = 0;
	unsigned short *pdata_16;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;

	rows = le2_to_uint(tcm_info->app_info.num_of_image_rows);
	cols = le2_to_uint(tcm_info->app_info.num_of_image_cols);

	TPD_INFO("report size:%d\n", test_hcd->report.data_length);
	if (buffer == NULL)
		pdata_16 = (unsigned short *)&test_hcd->report.buf[0];
	else
		pdata_16 = (unsigned short *)buffer;

	for (row = 0; row < rows; row++) {
		seq_printf(s, "[%02d] ", row);
		for (col = 0; col < cols; col++) {
			seq_printf(s, "%5d ", *pdata_16);
			pdata_16++;
		}
		seq_printf(s, "\n");
	}

	if (test_hcd->report.data_length == rows * cols * 2 + (rows + cols) * 2) {
		for (cnt = 0; cnt < rows + cols; cnt++) {
			seq_printf(s, "%5d ", *pdata_16);
			pdata_16++;
		}
	}
	seq_printf(s, "\n");

	return;
}
static void syna_main_register(struct seq_file *s, void *chip_data)
{
	int retval = 0;
	unsigned short config = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_IN_WAKEUP_GESTURE_MODE, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("gesture mode : %d\n", config);
	seq_printf(s, "gesture mode : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_ERROR_PRIORITY, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("error priority (1:finger,0:error) : %d\n", config);
	seq_printf(s, "error priority (1:finger,0:error) : %d, 0x%x\n", config, config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_NOISE_LENGTH, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("noise length : %d\n", config);
	seq_printf(s, "noise length : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_SET_REPORT_FRE, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("report frequence (0x01:120HZ,0x02:180HZ,0x03:240HZ) : %d\n", config);
	seq_printf(s, "report frequence (0x01:120HZ,0x02:180HZ,0x03:240HZ) : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_CHARGER_CONNECTED, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("charger mode : %d\n", config);
	seq_printf(s, "charger mode : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_TOUCH_HOLD, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("touch and hold mode : %d\n", config);
	seq_printf(s, "touch and hold mode : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_GRIP_ENABLED, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("grip enable : %d\n", config);
	seq_printf(s, "grip enable : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("grip direction(1:ver 0:hor) : %d\n", config);
	seq_printf(s, "grip direction(0:ver 1:hor) : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_DARK_ZONE_ENABLE, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("dark zone enable : %d\n", config);
	seq_printf(s, "dark zone enable : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_X, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("dark zone x : %d\n", config);
	seq_printf(s, "dark zone x : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_Y, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("dark zone y : %d\n", config);
	seq_printf(s, "dark zone y : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_SEL, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("abs dark zone : %d\n", config);
	seq_printf(s, "abs dark zone : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_X, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("abs dark zone x : %d\n", config);
	seq_printf(s, "abs dark zone x : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_Y, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("abs dark zone y : %d\n", config);
	seq_printf(s, "abs dark zone y : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_U, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("abs dark zone U : %d\n", config);
	seq_printf(s, "abs dark zone U : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_V, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("abs dark zone V : %d\n", config);
	seq_printf(s, "abs dark zone V : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("low temperatue mode: %d\n", config);
	seq_printf(s, "low temperatue mode : %d\n", config);

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get dynamic config\n");
		seq_printf(s, "Failed to get dynamic config\n");
		return;
	}
	TPD_INFO("low temperatue mode: %d\n", config);
	seq_printf(s, "low temperatue mode : %d\n", config);

	TPD_INFO("Buid ID:%d, Custom ID:0x%s\n", le4_to_uint(tcm_info->id_info.build_id), tcm_info->app_info.customer_config_id);
	seq_printf(s, "Buid ID:%d, Custom ID:0x%s\n", le4_to_uint(tcm_info->id_info.build_id), tcm_info->app_info.customer_config_id);

	TPD_INFO("APP info : version:%d\n", le2_to_uint(tcm_info->app_info.version));
	TPD_INFO("APP info : status:%d\n", le2_to_uint(tcm_info->app_info.status));
	TPD_INFO("APP info : max_touch_report_config_size:%d\n", le2_to_uint(tcm_info->app_info.max_touch_report_config_size));
	TPD_INFO("APP info : max_touch_report_payload_size:%d\n", le2_to_uint(tcm_info->app_info.max_touch_report_payload_size));
	TPD_INFO("APP info : customer_config_id:%d\n", le2_to_uint(tcm_info->app_info.customer_config_id));
	TPD_INFO("APP info : max_x:%d\n", le2_to_uint(tcm_info->app_info.max_x));
	TPD_INFO("APP info : max_y:%d\n", le2_to_uint(tcm_info->app_info.max_y));
	TPD_INFO("APP info : num_of_image_rows:%d\n", le2_to_uint(tcm_info->app_info.num_of_image_rows));
	TPD_INFO("APP info : num_of_image_cols:%d\n", le2_to_uint(tcm_info->app_info.num_of_image_cols));

	seq_printf(s, "APP info : version:%d\n", le2_to_uint(tcm_info->app_info.version));
	seq_printf(s, "APP info : status:%d\n", le2_to_uint(tcm_info->app_info.status));
	seq_printf(s, "APP info : max_touch_report_config_size:%d\n", le2_to_uint(tcm_info->app_info.max_touch_report_config_size));
	seq_printf(s, "APP info : max_touch_report_payload_size:%d\n", le2_to_uint(tcm_info->app_info.max_touch_report_payload_size));
	seq_printf(s, "APP info : customer_config_id:%d\n", le2_to_uint(tcm_info->app_info.customer_config_id));
	seq_printf(s, "APP info : max_x:%d\n", le2_to_uint(tcm_info->app_info.max_x));
	seq_printf(s, "APP info : max_y:%d\n", le2_to_uint(tcm_info->app_info.max_y));
	seq_printf(s, "APP info : num_of_image_rows:%d\n", le2_to_uint(tcm_info->app_info.num_of_image_rows));
	seq_printf(s, "APP info : num_of_image_cols:%d\n", le2_to_uint(tcm_info->app_info.num_of_image_cols));
	if (tcm_info->default_config.data_length > 0) {
		seq_printf(s, "default_config:%*ph\n", tcm_info->default_config.data_length, tcm_info->default_config.buf);
		TPD_INFO("default_config:%*ph\n", tcm_info->default_config.data_length, tcm_info->default_config.buf);
	}

	if (tcm_info->config.data_length > 0) {
		seq_printf(s, "config:%*ph\n", tcm_info->config.data_length, tcm_info->config.buf);
		TPD_INFO("config:%*ph\n", tcm_info->config.data_length, tcm_info->config.buf);
	}

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, &config);
	if (retval < 0) {
		TPD_INFO("Failed to get temperature config\n");
	}
	seq_printf(s, "DC_LOW_TEMP_ENABLE:%d\n", config);
	TPD_INFO("DC_LOW_TEMP_ENABLE:%d\n", config);

	return;
}

static void syna_delta_read(struct seq_file *s, void *chip_data)
{
	int retval;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	retval = syna_tcm_set_dynamic_config(tcm_info, DC_NO_DOZE, 1);
	if (retval < 0) {
		TPD_INFO("Failed to exit doze\n");
	}

	msleep(20); /* delay 20ms*/

	retval = syna_tcm_collect_reports(tcm_info, REPORT_DELTA, 1);
	if (retval < 0) {
		seq_printf(s, "Failed to read delta data\n");
		return;
	}

	syna_tcm_format_print(s, tcm_info, NULL);

	/*set normal doze*/
	retval = syna_tcm_set_dynamic_config(tcm_info, DC_NO_DOZE, 0);
	if (retval < 0) {
		TPD_INFO("Failed to switch to normal\n");
	}

	return;
}

static void syna_baseline_read(struct seq_file *s, void *chip_data)
{
	int retval;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	retval = syna_tcm_set_dynamic_config(tcm_info, DC_NO_DOZE, 1);
	if (retval < 0) {
		TPD_INFO("Failed to exit doze\n");
	}

	msleep(20); /* delay 20ms*/

	retval = syna_tcm_collect_reports(tcm_info, REPORT_RAW, 1);
	if (retval < 0) {
		seq_printf(s, "Failed to read baseline data\n");
		return;
	}

	syna_tcm_format_unsigned_print(s, tcm_info, NULL);

	/*set normal doze*/
	retval = syna_tcm_set_dynamic_config(tcm_info, DC_NO_DOZE, 0);
	if (retval < 0) {
		TPD_INFO("Failed to switch to normal\n");
	}

	return;
}

static struct synaptics_proc_operations syna_proc_ops = {
	.auto_test	 = syna_auto_test,
};

static  void syna_reserve_read(struct seq_file *s, void *chip_data)
{
	int retval;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	retval = syna_tcm_set_dynamic_config(tcm_info, DC_NO_DOZE, 1);
	if (retval < 0) {
		TPD_INFO("Failed to exit doze\n");
	}

	msleep(20); /* delay 20ms*/

	retval = syna_tcm_collect_reports(tcm_info, REPORT_DEBUG, 1);
	if (retval < 0) {
		seq_printf(s, "Failed to read delta data\n");
		return;
	}

	syna_tcm_format_unsigned_print(s, tcm_info, NULL);

	/*set normal doze*/
	retval = syna_tcm_set_dynamic_config(tcm_info, DC_NO_DOZE, 0);
	if (retval < 0) {
		TPD_INFO("Failed to switch to normal\n");
	}

	return;
}

static void syna_tp_limit_data_write(void *chip_data, int count)
{
	int retval;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	if (!tcm_info->tp_data_record_support) {
		TPD_INFO("tp data record not support! \n");
		return;
	}
	if (count) {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_SET_DIFFER_READ, 1);

		if (retval < 0) {
			TPD_INFO("Failed to set differ read true\n");
		}
		tcm_info->differ_read_every_frame = true;
	} else {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_SET_DIFFER_READ, 0);

		if (retval < 0) {
			TPD_INFO("Failed to set differ read false\n");
		}
		tcm_info->differ_read_every_frame = false;
	}
	TPD_INFO("tp data record set to %u\n", count);
	return;
}
static struct debug_info_proc_operations syna_debug_proc_ops = {
	.delta_read	= syna_delta_read,
	.baseline_read = syna_baseline_read,
	.baseline_blackscreen_read = syna_baseline_read,
	.main_register_read = syna_main_register,
	.limit_read	= synaptics_limit_read,
	.reserve_read  = syna_reserve_read,
	.tp_limit_data_write = syna_tp_limit_data_write,
};

static int syna_device_report_touch(struct syna_tcm_data *tcm_info)
{
	int ret = syna_parse_report(tcm_info);
	if (ret < 0) {
		TPD_INFO("Failed to parse report\n");
		return -EINVAL;
	}
	syna_set_trigger_reason(tcm_info, IRQ_TOUCH);
	return 0;
}

static int syna_tcm_irq_handle(void *chip_data)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	return syna_tcm_read_message(tcm_info, NULL, 0);
}

static void syna_set_touch_direction(void *chip_data, uint8_t dir)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	tcm_info->touch_direction = dir;
}

static uint8_t syna_get_touch_direction(void *chip_data)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	return tcm_info->touch_direction;
}

static void syna_freq_hop_trigger(void *chip_data)
{
	static int freq_point = 0;
	int retval = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	TPD_INFO("%s : send cmd to tigger frequency hopping here!!!\n", __func__);

	switch (freq_point) {
	case 0:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING, freq_point);
		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}
		freq_point = 1;
		break;

	case 1:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING, freq_point);
		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}
		freq_point = 2;
		break;

	case 2:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING, freq_point);
		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}
		freq_point = 3;
		break;
	case 3:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING, freq_point);
		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}
		freq_point = 4;
		break;

	case 4:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING, freq_point);
		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}
		freq_point = 5;
		break;

	case 5:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING, freq_point);
		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}
		freq_point = 0;
		break;

	default:
		break;
	}
}

static int syna_tcm_smooth_lv_set(void *chip_data, int level)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	unsigned short regval = 0;
	int retval = 0;

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_ERROR_PRIORITY, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get smooth config\n");
		tcm_info->error_state_count++;
		return 0;
	}
	tcm_info->error_state_count = 0;

	retval = syna_tcm_set_dynamic_config(tcm_info, DC_ERROR_PRIORITY, (level << 4)|(regval&0x01));
	if (retval < 0) {
		TPD_INFO("Failed to set smooth config\n");
		return 0;
	}

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_ERROR_PRIORITY, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get smooth config\n");
		return 0;
	}
	TPD_INFO("OK synaptics smooth lv to %d, now reg_val:0x%x", level, regval);
	return 0;
}
static int syna_tcm_sensitive_lv_set(void *chip_data, int level)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	unsigned short regval = 0;
	int retval = 0;

	retval = syna_tcm_set_dynamic_config(tcm_info, DC_NOISE_LENGTH, level);
	if (retval < 0) {
		TPD_INFO("Failed to set sensitive config\n");
		tcm_info->error_state_count++;
		return 0;
	}
	tcm_info->error_state_count = 0;

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_NOISE_LENGTH, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get sensitive config\n");
		return 0;
	}
	TPD_INFO("OK synaptics sensitive lv to %d, now reg_val:%d", level, regval);

	return 0;
}

static int syna_tcm_set_high_frame_rate(void *chip_data, int level, int time)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	unsigned short regval = 0;
	int retval = 0;

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_ERROR_PRIORITY, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get high frame config\n");
		return 0;
	}

	if (level > 0) {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_ERROR_PRIORITY, regval|0x0c02);
		if (retval < 0) {
			TPD_INFO("Failed to enable high frame config\n");
			goto OUT;
		}
	} else {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_ERROR_PRIORITY, regval&0xfffd);
		if (retval < 0) {
			TPD_INFO("Failed to disable high frame config\n");
			goto OUT;
		}
	}

	TPD_INFO("synaptics %s high frame success lv to %d, time to %d",
		level > 0 ? "enable" : "disable", level, time);

OUT:
	return 0;
}

static void syna_s3908_set_gesture_state(void *chip_data, int state)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	TPD_INFO("%s:state:%d!\n", __func__, state);
	tcm_info->gesture_state = state;
}

static int syna_tcm_send_temperature(void *chip_data, int temp, bool status)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	unsigned short regval = 0;
	int retval = 0;
	unsigned short temp_mode = 0;

	/*enable low tempreratue*/
	if (temp <= tcm_info->syna_tempepratue[0]) {
		temp_mode = 1;
		tcm_info->syna_low_temp_enable++;
		tcm_info->syna_low_temp_disable = 0;
	/*disable low tempreratue*/
	} else if (temp > tcm_info->syna_tempepratue[1]) {
		temp_mode = 0;
		tcm_info->syna_low_temp_enable = 0;
		tcm_info->syna_low_temp_disable++;
	} else {
		if (tcm_info->syna_low_temp_enable > 0) {
			temp_mode = 1;
			tcm_info->syna_low_temp_enable++;
			tcm_info->syna_low_temp_disable = 0;
		} else {
			temp_mode = 0;
			tcm_info->syna_low_temp_enable = 0;
			tcm_info->syna_low_temp_disable++;
		}
	}

	if (tcm_info->syna_low_temp_enable > 1 || tcm_info->syna_low_temp_disable > 1) {
		TPD_DEBUG("enable or disable low temp mode is more than one time\n");
		return 0;
	}
	retval = syna_tcm_get_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get temperature config\n");
		return 0;
	}

	if (1 == temp_mode)  {
		tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_REPORT, "syna_low_temp_enable");
	} else {
		tp_healthinfo_report(tcm_info->monitor_data_v2, HEALTH_REPORT, "syna_low_temp_disable");
	}

	if (1 == temp_mode)  {
		temp_mode = regval | 0x02;
	} else {
		temp_mode = regval & 0xfd;
	}
	retval = syna_tcm_set_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, temp_mode);
	if (retval < 0) {
		TPD_INFO("Failed to set temperature config\n");
		return 0;
	}

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get temperature config\n");
		return 0;
	}
	TPD_INFO("OK synaptics temperature to %d, now reg_val:%d", temp_mode, regval);

	return retval;
}

static struct oplus_touchpanel_operations syna_tcm_ops = {
	.ftm_process			   = syna_ftm_process,
	.get_vendor				= syna_get_vendor,
	.get_chip_info			 = syna_get_chip_info,
	.get_touch_points		  = syna_get_touch_points,
	.get_gesture_info		  = syna_get_gesture_info,
	.power_control			 = syna_power_control,
	.reset					 = syna_tcm_reset,
	.u32_trigger_reason		= syna_trigger_reason,
	.mode_switch			   = syna_mode_switch,
	.irq_handle_unlock		 = syna_tcm_irq_handle,
	.fw_check				  = syna_fw_check,
	.fw_update				 = syna_tcm_fw_update,
	.async_work				= syna_tcm_async_work,
	.reinit_device			 = syna_tcm_reinit_device,
	.enable_fingerprint		= syna_tcm_enable_fingerprint,
	.screenon_fingerprint_info = syna_tcm_fingerprint_info,
	.health_report			 = syna_tcm_get_health_info,
	.set_touch_direction	   = syna_set_touch_direction,
	.get_touch_direction	   = syna_get_touch_direction,
	.freq_hop_trigger		  = syna_freq_hop_trigger,
	.enable_gesture_mask	   = syna_tcm_enable_gesture_mask,
	.smooth_lv_set			 = syna_tcm_smooth_lv_set,
	.sensitive_lv_set		  = syna_tcm_sensitive_lv_set,
	.tp_refresh_switch		= syna_report_refresh_switch,
	.rate_white_list_ctrl		= syna_rate_white_list_ctrl,
	.set_gesture_state		 = syna_s3908_set_gesture_state,
	.get_touch_points_auto	 = syna_get_touch_points_auto,
	.get_gesture_info_auto	 = syna_get_gesture_info_auto,
	.screenon_fingerprint_info_auto = syna_tcm_fingerprint_info_auto,
	.send_temperature			= syna_tcm_send_temperature,
	.set_high_frame_rate		= syna_tcm_set_high_frame_rate,
};

static void init_chip_dts(struct device *dev, void *chip_data)
{
	int rc;
	int ret = 0;
	int i = 0;
	int temp_array[FPS_REPORT_NUM];
	struct device_node *np;
	struct device_node *chip_np;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	np = dev->of_node;
	tcm_info->tp_data_record_support = of_property_read_bool(np, "tp_data_record_support");
	tcm_info->fw_edge_limit_support = of_property_read_bool(np, "fw_edge_limit_support");

	chip_np = of_get_child_by_name(np, "S3910");

	if (!chip_np) {
		tcm_info->display_refresh_rate = 60;
		tcm_info->game_rate = 1;
		/*default :1:120hz 2:180hz is for 19101 19191 20131*/
		tcm_info->fps_report_rate_num = 6;
		tcm_info->fps_report_rate_array[0] = 60;
		tcm_info->fps_report_rate_array[1] = 0;
		tcm_info->fps_report_rate_array[2] = 90;
		tcm_info->fps_report_rate_array[3] = 1;
		tcm_info->fps_report_rate_array[4] = 120;
		tcm_info->fps_report_rate_array[5] = 2;
		tcm_info->fingerprint_and_grip_param_equal_19805 = 0;
		tcm_info->syna_tempepratue[0] = 5;
		tcm_info->syna_tempepratue[1] = 15;
		tcm_info->syna_low_temp_enable = 0;
		tcm_info->syna_low_temp_disable = 1;
		tcm_info->fwupdate_bootloader = 0;
		tcm_info->normal_config_version = 0;
		return;
	}

	rc = of_property_read_u32(chip_np, "report_rate_default", &tcm_info->display_refresh_rate);
	if (rc < 0) {
		tcm_info->display_refresh_rate = 60;
		TPD_INFO("default rate %d\n", tcm_info->display_refresh_rate);
	}

	rc = of_property_read_u32(chip_np, "report_rate_game_value", &ret);
	if (rc < 0) {
		ret = 1;
		TPD_INFO("default rate %d\n", ret);
	}
	tcm_info->game_rate = ret;

	rc = of_property_count_u32_elems(chip_np, "fps_report_rate");
	tcm_info->fps_report_rate_num = rc;

	if (tcm_info->fps_report_rate_num > 0 && tcm_info->fps_report_rate_num <= FPS_REPORT_NUM && !(tcm_info->fps_report_rate_num % 2)) {
		rc = of_property_read_u32_array(chip_np, "fps_report_rate", temp_array, tcm_info->fps_report_rate_num);
		if (rc) {
			TPD_INFO("fps_report_rate not specified %d\n", rc);
		} else {
			for (i = 0; i < tcm_info->fps_report_rate_num; i++) {
				tcm_info->fps_report_rate_array[i] = temp_array[i];
				TPD_INFO("fps_report_rate is: %d\n", tcm_info->fps_report_rate_array[i]);
			}
		}
	} else {
		/*default :0:125hz 1:240hz 2:360HZ is for 21641*/
		tcm_info->fps_report_rate_num = 6;
		tcm_info->fps_report_rate_array[0] = 60;
		tcm_info->fps_report_rate_array[1] = 0;
		tcm_info->fps_report_rate_array[2] = 90;
		tcm_info->fps_report_rate_array[3] = 1;
		tcm_info->fps_report_rate_array[4] = 120;
		tcm_info->fps_report_rate_array[5] = 2;
		TPD_INFO("fps_report_rate is not dubole %d\n", tcm_info->fps_report_rate_num);
	}

	rc = of_property_read_u32(chip_np, "fingerprint_and_grip_param_equal_19805", &tcm_info->fingerprint_and_grip_param_equal_19805);
	if (rc < 0) {
			tcm_info->fingerprint_and_grip_param_equal_19805 = 0;
			TPD_INFO("default rate %d\n", tcm_info->fingerprint_and_grip_param_equal_19805);
	}
	tcm_info->syna_tempepratue[0] = 5;
	tcm_info->syna_tempepratue[1] = 15;
	tcm_info->syna_low_temp_enable = 0;
	tcm_info->syna_low_temp_disable = 1;
	rc = of_property_read_u32(chip_np, "normal_config_version", &tcm_info->normal_config_version);
	if (rc < 0) {
		/* latest projects' default normal config is version-2, old projects with version-0 need to config in dts */
		tcm_info->normal_config_version = 2;
		TPD_INFO("normal_config_version %d\n", tcm_info->normal_config_version);
	}

	rc = of_property_read_u32(chip_np, "fwupdate_bootloader", &tcm_info->fwupdate_bootloader);
	if (rc < 0) {
		tcm_info->fwupdate_bootloader = 0;
		TPD_INFO("fwupdate_bootloader %d\n", tcm_info->fwupdate_bootloader);
	}
}


static int syna_tcm_probe(struct spi_device *spi)
{
	int retval = 0;
	u64 time_counter = 0;
	struct syna_tcm_data *tcm_info = NULL;
	struct touchpanel_data *ts = NULL;
	struct device_hcd *device_hcd = NULL;

	TPD_INFO("%s: enter\n", __func__);

	if (tp_register_times > 0) {
		TPD_INFO("TP driver have success loaded %d times, exit\n", tp_register_times);
		return -1;
	}

	/*1. alloc mem for tcm_data*/
	tcm_info = kzalloc(sizeof(*tcm_info), GFP_KERNEL);
	if (!tcm_info) {
		TPD_INFO("no more memory\n");
		return -ENOMEM;
	}

	/*2. alloc mem for touchpanel_data*/
	ts = common_touch_data_alloc();
	if (ts == NULL) {
		TPD_INFO("failed to alloc common data\n");
		goto ts_alloc_failed;
	}

	reset_healthinfo_time_counter(&time_counter);

	/*3. init member of ts*/
	spi->controller_data = (void *)&st_spi_ctrdata_S3910;
	ts->dev = &spi->dev;
	ts->s_client  = spi;
	ts->irq = spi->irq;
	ts->irq_flags_cover = 0x2008;
	ts->chip_data = tcm_info;
	spi_set_drvdata(spi, ts);
	ts->has_callback = true;
	ts->use_resume_notify = true;
	ts->ts_ops = &syna_tcm_ops;
	ts->debug_info_ops = &syna_debug_proc_ops;
	ts->bus_type = TP_BUS_SPI;
	tcm_info->ts = ts;

	/*4. init member of tcm_info*/
	tcm_info->client = spi;
	tcm_info->hw_res = &ts->hw_res;
	tcm_info->ubl_addr = 0x2c;
	tcm_info->rd_chunk_size = RD_CHUNK_SIZE;
	tcm_info->wr_chunk_size = WR_CHUNK_SIZE;
	tcm_info->read_length = MIN_READ_LENGTH;
	tcm_info->in_suspend = &ts->is_suspended;
	tcm_info->syna_ops = &syna_proc_ops;
	/*tcm_info->display_refresh_rate = 90;*/
	tcm_info->game_mode = false;
	tcm_info->palm_hold_report = 0;
	atomic_set(&tcm_info->command_status, CMD_IDLE);

	mutex_init(&tcm_info->reset_mutex);
	mutex_init(&tcm_info->rw_mutex);
	mutex_init(&tcm_info->command_mutex);
	mutex_init(&tcm_info->identify_mutex);

	INIT_BUFFER(tcm_info->in, false);
	INIT_BUFFER(tcm_info->out, false);
	INIT_BUFFER(tcm_info->resp, true);
	INIT_BUFFER(tcm_info->temp, false);
	INIT_BUFFER(tcm_info->config, false);
	INIT_BUFFER(tcm_info->default_config, false);
	INIT_BUFFER(tcm_info->report.buffer, true);

	/*5. alloc mem for reading in buffer*/
	LOCK_BUFFER(tcm_info->in);
	retval = syna_tcm_alloc_mem(&tcm_info->in, MAX_READ_LENGTH);
	TPD_INFO("%s read_length:%d\n", __func__, tcm_info->read_length);
	if (retval < 0) {
		TPD_INFO("Failed to allocate memory for tcm_info->in.buf\n");
		goto err_malloc_inbuffer;
	}
	UNLOCK_BUFFER(tcm_info->in);

	/*6. create workqueue and init helper work*/
	tcm_info->helper_workqueue = create_singlethread_workqueue("syna_tcm_helper");
	INIT_WORK(&tcm_info->helper_work, syna_tcm_helper_work);

	/*7. alloc mem for touch_hcd and init it's member*/
	tcm_info->touch_hcd = (struct touch_hcd *)kzalloc(sizeof(struct touch_hcd), GFP_KERNEL);
	if (!tcm_info->touch_hcd) {
		retval = -ENOMEM;
		goto err_malloc_touchhcd;
	}
	INIT_BUFFER(tcm_info->touch_hcd->out, false);
	INIT_BUFFER(tcm_info->touch_hcd->resp, false);
	mutex_init(&tcm_info->touch_hcd->report_mutex);
	of_property_read_u32(ts->dev->of_node, "touchpanel,max-num-support", &tcm_info->touch_hcd->max_objects);
	tcm_info->touch_hcd->touch_data.object_data =
		(struct object_data *)kzalloc(sizeof(struct object_data) * tcm_info->touch_hcd->max_objects, GFP_KERNEL);
	if (!tcm_info->touch_hcd->touch_data.object_data) {
		retval = -ENOMEM;
		goto err_malloc_objdata;
	}

	/*8. alloc mem for test_hcd and it's member*/
	tcm_info->test_hcd = (struct syna_tcm_test *)kzalloc(sizeof(struct syna_tcm_test), GFP_KERNEL);
	if (!tcm_info->test_hcd) {
		retval = -ENOMEM;
		goto err_malloc_testhcd;
	}

	INIT_BUFFER(tcm_info->test_hcd->report, false);
	INIT_BUFFER(tcm_info->test_hcd->test_resp, false);
	INIT_BUFFER(tcm_info->test_hcd->test_out, false);

	/*9. register common part of touchpanel driver*/
	retval = register_common_touch_device(ts);
	if (retval < 0 && (retval != -EFTM)) {
		TPD_INFO("Failed to init device information\n");
		goto err_register_driver;
	}
	ts->mode_switch_type = SINGLE;
	init_chip_dts(ts->dev, tcm_info);
	tcm_info->black_gesture_indep = ts->black_gesture_indep_support;
	tcm_info->monitor_data_v2 = &ts->monitor_data_v2;
	ts->int_mode = 1;
	tcm_info->differ_read_every_frame = false;

	/*10. create synaptics common file*/
	synaptics_create_proc(ts, tcm_info->syna_ops);

	/*11. create remote device file and init it's callback*/
	device_hcd = syna_remote_device_S3910_init(tcm_info);
	if (device_hcd) {
		device_hcd->irq = tcm_info->client->irq;
		device_hcd->read_message = syna_tcm_read_message;
		device_hcd->write_message = syna_tcm_write_message;
		device_hcd->reset = syna_tcm_reset;
		device_hcd->report_touch = syna_device_report_touch;
	}
	tcm_info->fw_update_app_support = &ts->fw_update_app_support;
	tcm_info->loading_fw  = &ts->loading_fw;

	if (tcm_info->fwupdate_bootloader) {
		tcm_info->g_fw_buf = vmalloc(FW_BUF_SIZE);
		if (tcm_info->g_fw_buf == NULL) {
			TPD_INFO("fw buf vmalloc error\n");
			goto err_register_driver;
		}
		tcm_info->g_fw_sta = false;
		tcm_info->probe_done = 1;
	}

	g_tcm_info = tcm_info;

	if (ts->health_monitor_v2_support) {
		tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_PROBE, &time_counter);
	}

	return 0;

err_register_driver:
	if (tcm_info->test_hcd) {
		kfree(tcm_info->test_hcd);
		tcm_info->test_hcd = NULL;
	}

/*Destroy it to avoid double panel match cause kernel panic*/
	cancel_work_sync(&tcm_info->helper_work);
	flush_workqueue(tcm_info->helper_workqueue);
	destroy_workqueue(tcm_info->helper_workqueue);
/*end*/

err_malloc_testhcd:
	if (tcm_info->touch_hcd->touch_data.object_data) {
		kfree(tcm_info->touch_hcd->touch_data.object_data);
		tcm_info->touch_hcd->touch_data.object_data = NULL;
	}
err_malloc_objdata:
	if (tcm_info->touch_hcd) {
		kfree(tcm_info->touch_hcd);
		tcm_info->touch_hcd = NULL;
	}
err_malloc_touchhcd:
	RELEASE_BUFFER(tcm_info->in);
err_malloc_inbuffer:
	common_touch_data_free(ts);
	ts = NULL;

ts_alloc_failed:
	if (tcm_info) {
		kfree(tcm_info);
		tcm_info = NULL;
	}

	return retval;
}

static int syna_tcm_remove(struct spi_device *s_client)
{
	struct touchpanel_data *ts =  spi_get_drvdata(s_client);
	struct syna_tcm_data *tcm_info =  (struct syna_tcm_data *)ts->chip_data;

	RELEASE_BUFFER(tcm_info->report.buffer);
	RELEASE_BUFFER(tcm_info->config);
	RELEASE_BUFFER(tcm_info->temp);
	RELEASE_BUFFER(tcm_info->resp);
	RELEASE_BUFFER(tcm_info->out);
	RELEASE_BUFFER(tcm_info->in);

	if (tcm_info->fwupdate_bootloader && tcm_info->g_fw_buf) {
		vfree(tcm_info->g_fw_buf);
	}

	kfree(tcm_info);
	kfree(ts);

	return 0;
}

static int syna_spi_suspend(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s: is called\n", __func__);
	tp_i2c_suspend(ts);
	return 0;
}

static int syna_spi_resume(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s is called\n", __func__);
	tp_i2c_resume(ts);
	return 0;
}

static const struct dev_pm_ops syna_pm_ops = {
	.suspend = syna_spi_suspend,
	.resume = syna_spi_resume,
};

static const struct spi_device_id syna_tmc_id[] = {
	{ TPD_DEVICE, 0 },
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
		{ "oplus,tp_noflash", 0 },
#endif
	{ }
};

static struct of_device_id syna_match_table[] = {
	{ .compatible = TPD_DEVICE, },
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
		{ .compatible = "oplus,tp_noflash", },
#endif
	{ .compatible = "synaptics-s3910", },
	{ }
};

static struct spi_driver syna_spi_driver = {
	.probe	  = syna_tcm_probe,
	.remove	 = syna_tcm_remove,
	.id_table   = syna_tmc_id,
	.driver	 = {
		.name   = TPD_DEVICE,
		.owner = THIS_MODULE,
		.of_match_table =  syna_match_table,
		.pm = &syna_pm_ops,
	},
};


static int __init syna_tcm_module_init(void)
{
	TPD_INFO("%s is called\n", __func__);

	if (!tp_judge_ic_match(TPD_DEVICE)) {
		TPD_INFO("tp_judge_ic_match fail\n");
		return -1;
	}

	TPD_INFO("spi_register_driver\n");
	if (spi_register_driver(&syna_spi_driver) != 0) {
		TPD_INFO("unable to add spi driver.\n");
		return -1;
	}
	TPD_INFO("spi_register_driver OK\n");

	return 0;
}

static void __exit syna_tcm_module_exit(void)
{
	spi_unregister_driver(&syna_spi_driver);
	return;
}

#ifdef CONFIG_TOUCHPANEL_LATE_INIT
late_initcall(syna_tcm_module_init);
#else
module_init(syna_tcm_module_init);
#endif
module_exit(syna_tcm_module_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics TCM Touch Driver");
MODULE_LICENSE("GPL v2");
