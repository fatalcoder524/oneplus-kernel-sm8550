/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/firmware.h>

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
#include<mt-plat/mtk_boot_common.h>
#else
#include <soc/oplus/system/boot_mode.h>
#endif

#include "nvt_drivers_nt36532_noflash.h"

/*******Part0:LOG TAG Declear********************/

/*******Part1: Micro && function prototype Declear********************/
#define MESSAGE_SIZE			  (256)

static int8_t nvt_cmd_store(struct chip_data_nt36523 *chip_info, uint8_t u8Cmd);

static fw_update_state nvt_fw_update_sub(void *chip_data, const struct firmware *fw, bool force);
static fw_update_state nvt_fw_update(void *chip_data, const struct firmware *fw, bool force);
static int nvt_reset(void *chip_data);
static int nvt_get_chip_info(void *chip_data);
static int nvt_get_touch_points(void *chip_data, struct point_info *points, int max_num);
static int nvt_get_touch_points_high_reso(void *chip_data, struct point_info *points, int max_num);
static int32_t nvt_ts_point_data_checksum(uint8_t *buf, uint8_t length);

static void nvt_ts_read_history_log(void *chip_data);
static int32_t nvt_ts_pen_data_checksum(uint8_t *buf, uint8_t length);

/***************************** start of id map table******************************************/
/* hw info reg*/
static const struct nvt_ts_hw_reg_addr_info hw_reg_addr_info = {
	.chip_ver_trim_addr = 0x1FB104,
	.swrst_sif_addr = 0x1FB43E,
	.crc_err_flag_addr = 0x1FB535,
};

static const struct nvt_ts_hw_reg_addr_info hw_reg_addr_info_old_w_isp = {
	.chip_ver_trim_addr = 0x3F004,
	.swrst_sif_addr = 0x3F0FE,
	.crc_err_flag_addr = 0x3F135,
};

static const struct nvt_ts_mem_map NT36532_cascade_memory_map = {
	.EVENT_BUF_ADDR           = 0x125800,
	.RAW_PIPE0_ADDR           = 0x10B200,
	.RAW_PIPE1_ADDR           = 0x10B200,
	.BASELINE_ADDR            = 0x109E00,
	.BASELINE_BTN_ADDR        = 0,
	.DIFF_PIPE0_ADDR          = 0x128140,
	.DIFF_PIPE1_ADDR          = 0x129540,
	.RAW_BTN_PIPE0_ADDR       = 0,
	.RAW_BTN_PIPE1_ADDR       = 0,
	.DIFF_BTN_PIPE0_ADDR      = 0,
	.DIFF_BTN_PIPE1_ADDR      = 0,
	.PEN_2D_BL_TIP_X_ADDR     = 0x10F940,
	.PEN_2D_BL_TIP_Y_ADDR     = 0x10FD40,
	.PEN_2D_BL_RING_X_ADDR    = 0x110140,
	.PEN_2D_BL_RING_Y_ADDR    = 0x110540,
	.PEN_2D_DIFF_TIP_X_ADDR   = 0x111140,
	.PEN_2D_DIFF_TIP_Y_ADDR   = 0x111540,
	.PEN_2D_DIFF_RING_X_ADDR  = 0x111940,
	.PEN_2D_DIFF_RING_Y_ADDR  = 0x111D40,
	.PEN_2D_RAW_TIP_X_ADDR    = 0x126E00,
	.PEN_2D_RAW_TIP_Y_ADDR    = 0x127210,
	.PEN_2D_RAW_RING_X_ADDR   = 0x127620,
	.PEN_2D_RAW_RING_Y_ADDR   = 0x127A30,
	.PEN_1D_DIFF_TIP_X_ADDR   = 0x127E40,
	.PEN_1D_DIFF_TIP_Y_ADDR   = 0x127EC0,
	.PEN_1D_DIFF_RING_X_ADDR  = 0x127F40,
	.PEN_1D_DIFF_RING_Y_ADDR  = 0x127FC0,
	.ENB_CASC_REG             = {.addr = 0x1FB12C, .mask = 0x01},
	/* FW History */
	.MMAP_HISTORY_EVENT0      = 0x121AFC,
	.MMAP_HISTORY_EVENT1      = 0x121B3C,
	.MMAP_HISTORY_EVENT2      = 0x121B80,
	.MMAP_HISTORY_EVENT3      = 0x121BC0,
	.DOZE_GM_S1D_SCAN_RAW_ADDR = 0x10F940,
	.DOZE_GM_BTN_SCAN_RAW_ADDR = 0,
	/* Phase 2 Host Download */
	.BOOT_RDY_ADDR            = 0x1FB50D,
	.TX_AUTO_COPY_EN          = 0x1FC925,
	.SPI_DMA_TX_INFO          = 0x1FC914,
	/* BLD CRC */
	.BLD_LENGTH_ADDR          = 0x1FB538,	/* 0x1FB538 ~ 0x1FB53A (3 bytes) */
	.ILM_LENGTH_ADDR          = 0x1FB518,	/* 0x1FB518 ~ 0x1FB51A (3 bytes) */
	.DLM_LENGTH_ADDR          = 0x1FB530,	/* 0x1FB530 ~ 0x1FB532 (3 bytes) */
	.BLD_DES_ADDR             = 0x1FB514,	/* 0x1FB514 ~ 0x1FB516 (3 bytes) */
	.ILM_DES_ADDR             = 0x1FB528,	/* 0x1FB528 ~ 0x1FB52A (3 bytes) */
	.DLM_DES_ADDR             = 0x1FB52C,	/* 0x1FB52C ~ 0x1FB52E (3 bytes) */
	.G_ILM_CHECKSUM_ADDR      = 0x1FB500,   /* 0x1FB500 ~ 0x1FB503 (4 bytes) */
	.G_DLM_CHECKSUM_ADDR      = 0x1FB504,	/* 0x1FB504 ~ 0x1FB507 (4 bytes) */
	.R_ILM_CHECKSUM_ADDR      = 0x1FB520,	/* 0x1FB520 ~ 0x1FB523 (4 bytes) */
	.R_DLM_CHECKSUM_ADDR      = 0x1FB524,	/* 0x1FB524 ~ 0x1FB527 (4 bytes) */
	.DMA_CRC_EN_ADDR          = 0x1FB536,
	.BLD_ILM_DLM_CRC_ADDR     = 0x1FB533,
	.DMA_CRC_FLAG_ADDR        = 0x1FB534,
};

static const struct nvt_ts_mem_map NT36532_single_memory_map = {
	.EVENT_BUF_ADDR           = 0x125800,
	.RAW_PIPE0_ADDR           = 0x10C1EC,
	.RAW_PIPE1_ADDR           = 0x10C1EC,
	.BASELINE_ADDR            = 0x10B1EC,
	.BASELINE_BTN_ADDR        = 0,
	.DIFF_PIPE0_ADDR          = 0x11BFC4,
	.DIFF_PIPE1_ADDR          = 0x11CFC4,
	.RAW_BTN_PIPE0_ADDR       = 0,
	.RAW_BTN_PIPE1_ADDR       = 0,
	.DIFF_BTN_PIPE0_ADDR      = 0,
	.DIFF_BTN_PIPE1_ADDR      = 0,
	.PEN_2D_BL_TIP_X_ADDR     = 0x11E5EC,
	.PEN_2D_BL_TIP_Y_ADDR     = 0x126C00,
	.PEN_2D_BL_RING_X_ADDR    = 0x11E7EC,
	.PEN_2D_BL_RING_Y_ADDR    = 0x126E00,
	.PEN_2D_DIFF_TIP_X_ADDR   = 0x11E1EC,
	.PEN_2D_DIFF_TIP_Y_ADDR   = 0x11E9EC,
	.PEN_2D_DIFF_RING_X_ADDR  = 0x11E3EC,
	.PEN_2D_DIFF_RING_Y_ADDR  = 0x11EBEC,
	.PEN_2D_RAW_TIP_X_ADDR    = 0x120CEC,
	.PEN_2D_RAW_TIP_Y_ADDR    = 0x120EFC,
	.PEN_2D_RAW_RING_X_ADDR   = 0x12110C,
	.PEN_2D_RAW_RING_Y_ADDR   = 0x12131C,
	.PEN_1D_DIFF_TIP_X_ADDR   = 0x126A00,
	.PEN_1D_DIFF_TIP_Y_ADDR   = 0x126A80,
	.PEN_1D_DIFF_RING_X_ADDR  = 0x126B00,
	.PEN_1D_DIFF_RING_Y_ADDR  = 0x126B80,
	.ENB_CASC_REG             = {.addr = 0x1FB12C, .mask = 0x01},
	/* FW History */
	.MMAP_HISTORY_EVENT0      = 0x122930,
	.MMAP_HISTORY_EVENT1      = 0x122970,
	.DOZE_GM_S1D_SCAN_RAW_ADDR = 0x10ED2C,
	.DOZE_GM_BTN_SCAN_RAW_ADDR = 0,
	/* Phase 2 Host Download */
	.BOOT_RDY_ADDR            = 0x1FB50D,
	.TX_AUTO_COPY_EN          = 0x1FC925,
	.SPI_DMA_TX_INFO          = 0x1FC914,
	/* BLD CRC */
	.BLD_LENGTH_ADDR          = 0x1FB538,	/* 0x1FB538 ~ 0x1FB53A (3 bytes) */
	.ILM_LENGTH_ADDR          = 0x1FB518,	/* 0x1FB518 ~ 0x1FB51A (3 bytes) */
	.DLM_LENGTH_ADDR          = 0x1FB530,	/* 0x1FB530 ~ 0x1FB532 (3 bytes) */
	.BLD_DES_ADDR             = 0x1FB514,	/* 0x1FB514 ~ 0x1FB516 (3 bytes) */
	.ILM_DES_ADDR             = 0x1FB528,	/* 0x1FB528 ~ 0x1FB52A (3 bytes) */
	.DLM_DES_ADDR             = 0x1FB52C,	/* 0x1FB52C ~ 0x1FB52E (3 bytes) */
	.G_ILM_CHECKSUM_ADDR      = 0x1FB500,   /* 0x1FB500 ~ 0x1FB503 (4 bytes) */
	.G_DLM_CHECKSUM_ADDR      = 0x1FB504,	/* 0x1FB504 ~ 0x1FB507 (4 bytes) */
	.R_ILM_CHECKSUM_ADDR      = 0x1FB520,	/* 0x1FB520 ~ 0x1FB523 (4 bytes) */
	.R_DLM_CHECKSUM_ADDR      = 0x1FB524,	/* 0x1FB524 ~ 0x1FB527 (4 bytes) */
	.DMA_CRC_EN_ADDR          = 0x1FB536,
	.BLD_ILM_DLM_CRC_ADDR     = 0x1FB533,
	.DMA_CRC_FLAG_ADDR        = 0x1FB534,
};

static const struct nvt_ts_mem_map NT36523_memory_map = {
	.EVENT_BUF_ADDR		   = 0x2FE00,
	.RAW_PIPE0_ADDR		   = 0x30FA0,
	.RAW_PIPE1_ADDR		   = 0x30FA0,
	.BASELINE_ADDR			= 0x36510,
	.BASELINE_BTN_ADDR		= 0,
	.DIFF_PIPE0_ADDR		  = 0x373E8,
	.DIFF_PIPE1_ADDR		  = 0x38068,
	.RAW_BTN_PIPE0_ADDR	   = 0,
	.RAW_BTN_PIPE1_ADDR	   = 0,
	.DIFF_BTN_PIPE0_ADDR	  = 0,
	.DIFF_BTN_PIPE1_ADDR	  = 0,
	.PEN_2D_BL_TIP_X_ADDR	 = 0x2988A,
	.PEN_2D_BL_TIP_Y_ADDR	 = 0x29A1A,
	.PEN_2D_BL_RING_X_ADDR	= 0x29BAA,
	.PEN_2D_BL_RING_Y_ADDR	= 0x29D3A,
	.PEN_2D_DIFF_TIP_X_ADDR   = 0x29ECA,
	.PEN_2D_DIFF_TIP_Y_ADDR   = 0x2A05A,
	.PEN_2D_DIFF_RING_X_ADDR  = 0x2A1EA,
	.PEN_2D_DIFF_RING_Y_ADDR  = 0x2A37A,
	.PEN_2D_RAW_TIP_X_ADDR	= 0x2A50A,
	.PEN_2D_RAW_TIP_Y_ADDR	= 0x2A69A,
	.PEN_2D_RAW_RING_X_ADDR   = 0x2A82A,
	.PEN_2D_RAW_RING_Y_ADDR   = 0x2A9BA,
	.PEN_1D_DIFF_TIP_X_ADDR   = 0x2AB4A,
	.PEN_1D_DIFF_TIP_Y_ADDR   = 0x2ABAE,
	.PEN_1D_DIFF_RING_X_ADDR  = 0x2AC12,
	.PEN_1D_DIFF_RING_Y_ADDR  = 0x2AC76,
	/* FW History */
	.MMAP_HISTORY_EVENT0      = 0x38D54,
	.MMAP_HISTORY_EVENT1      = 0x38D94,
	.MMAP_HISTORY_EVENT2      = 0x39700,
	.MMAP_HISTORY_EVENT3      = 0x39740,
	.READ_FLASH_CHECKSUM_ADDR = 0x24000,
	.RW_FLASH_DATA_ADDR	   = 0x24002,
	.DOZE_GM_S1D_SCAN_RAW_ADDR = 0x31F40,
	.DOZE_GM_BTN_SCAN_RAW_ADDR = 0,
	/* Phase 2 Host Download */
	.BOOT_RDY_ADDR			= 0x3F10D,
	.TX_AUTO_COPY_EN		  = 0x3F7E8,
	.SPI_DMA_TX_INFO		  = 0x3F7F1,
	/* BLD CRC */
	.BLD_LENGTH_ADDR		  = 0x3F138,	/*0x3F138 ~ 0x3F13A	(3 bytes) */
	.ILM_LENGTH_ADDR		  = 0x3F118,	/*0x3F118 ~ 0x3F11A	(3 bytes) */
	.DLM_LENGTH_ADDR		  = 0x3F130,	/*0x3F130 ~ 0x3F132	(3 bytes) */
	.BLD_DES_ADDR			 = 0x3F114,	/*0x3F114 ~ 0x3F116	(3 bytes) */
	.ILM_DES_ADDR			 = 0x3F128,	/*0x3F128 ~ 0x3F12A	(3 bytes) */
	.DLM_DES_ADDR			 = 0x3F12C,	/*0x3F12C ~ 0x3F12E	(3 bytes) */
	.G_ILM_CHECKSUM_ADDR	  = 0x3F100,	/*0x3F100 ~ 0x3F103	(4 bytes) */
	.G_DLM_CHECKSUM_ADDR	  = 0x3F104,	/*0x3F104 ~ 0x3F107	(4 bytes) */
	.R_ILM_CHECKSUM_ADDR	  = 0x3F120,	/*0x3F120 ~ 0x3F123 (4 bytes) */
	.R_DLM_CHECKSUM_ADDR	  = 0x3F124,	/*0x3F124 ~ 0x3F127 (4 bytes) */
	.DMA_CRC_EN_ADDR		  = 0x3F136,
	.BLD_ILM_DLM_CRC_ADDR	 = 0x3F133,
	.DMA_CRC_FLAG_ADDR		= 0x3F134,
};

static struct nvt_ts_hw_info NT36532_hw_info = {
	.hw_crc    = HWCRC_LEN_3Bytes,
	.auto_copy = CHECK_TX_AUTO_COPY_EN,
	.hw_regs   = &hw_reg_addr_info,
};

static struct nvt_ts_hw_info NT36523_hw_info = {
	.hw_crc    = HWCRC_LEN_3Bytes,
	.auto_copy = CHECK_SPI_DMA_TX_INFO,
	.hw_regs   = &hw_reg_addr_info_old_w_isp,
};

static const struct nvt_ts_trim_id_table trim_id_table[] = {
	{
		.id = {0xFF, 0xFF, 0xFF, 0x32, 0x65, 0x03},
		.mask = {0, 0, 0, 1, 1, 1},
		.mmap = &NT36532_single_memory_map,
		.mmap_casc = &NT36532_cascade_memory_map,
		.hwinfo = &NT36532_hw_info
	},
	{
		.id = {0x20, 0xFF, 0xFF, 0x23, 0x65, 0x03},
		.mask = {1, 0, 0, 1, 1, 1},
		.mmap = &NT36523_memory_map,
		.hwinfo = &NT36523_hw_info
	},
	{
		.id = {0x0C, 0xFF, 0xFF, 0x23, 0x65, 0x03},
		.mask = {1, 0, 0, 1, 1, 1},
		.mmap = &NT36523_memory_map,
		.hwinfo = &NT36523_hw_info
	},
	{
		.id = {0x0B, 0xFF, 0xFF, 0x23, 0x65, 0x03},
		.mask = {1, 0, 0, 1, 1, 1},
		.mmap = &NT36523_memory_map,
		.hwinfo = &NT36523_hw_info
	},
	{
		.id = {0x0A, 0xFF, 0xFF, 0x23, 0x65, 0x03},
		.mask = {1, 0, 0, 1, 1, 1},
		.mmap = &NT36523_memory_map,
		.hwinfo = &NT36523_hw_info
	},
	{
		.id = {0xFF, 0xFF, 0xFF, 0x23, 0x65, 0x03},
		.mask = {0, 0, 0, 1, 1, 1},
		.mmap = &NT36523_memory_map,
		.hwinfo = &NT36523_hw_info
	},
};

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
static const struct mtk_chip_config spi_ctrdata = {
	.sample_sel = 0,
	.cs_setuptime = 200,
	.cs_holdtime = 200,
	.cs_idletime = 0,
	.tick_delay = 0,
};
#endif /* end of  CONFIG_TOUCHPANEL_MTK_PLATFORM */

/*******************************************************
Description:
	Novatek touchscreen write data to specify address.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
static int32_t nvt_write_addr(struct spi_device *client, uint32_t addr,
				  uint8_t data)
{
	int32_t ret = 0;
	uint8_t buf[4] = {0};

	/*---set xdata index--- */
	buf[0] = 0xFF;  /*set index/page/addr command */
	buf[1] = (addr >> 15) & 0xFF;
	buf[2] = (addr >> 7) & 0xFF;
	ret = CTP_SPI_WRITE(client, buf, 3);
	if (ret) {
	TPD_INFO("set page 0x%06X failed, ret = %d\n", addr, ret);
	return ret;
	}

	/*---write data to index--- */
	buf[0] = addr & (0x7F);
	buf[1] = data;
	ret = CTP_SPI_WRITE(client, buf, 2);
	if (ret) {
	TPD_INFO("write data to 0x%06X failed, ret = %d\n", addr, ret);
	return ret;
	}

	return ret;
}

static void nvt_sw_reset_idle(struct chip_data_nt36523 *chip_info)
{
	/*---MCU idle cmds to SWRST_SIF_ADDR--- */
	TPD_INFO("%s is called!\n", __func__);
	nvt_write_addr(chip_info->s_client, chip_info->swrst_sif_addr, 0xAA);

	msleep(15);
}

/*******************************************************
Description:
	Novatek touchscreen reset MCU (boot) function.

return:
	n.a.
*******************************************************/
static void nvt_bootloader_reset_noflash(struct chip_data_nt36523 *chip_info)
{
	/*---reset cmds to SWRST_SIF_ADDR--- */
	TPD_DEBUG("%s is called!\n", __func__);
	nvt_write_addr(chip_info->s_client, chip_info->swrst_sif_addr, 0x69);

	mdelay(5);  /*wait tBRST2FR after Bootload RST */
}

/*******************************************************
Description:
	Novatek touchscreen set index/page/addr address.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
static int32_t nvt_set_page(struct chip_data_nt36523 *chip_info, uint32_t addr)
{
	uint8_t buf[4] = {0};

	buf[0] = 0xFF;	  /*set index/page/addr command */
	buf[1] = (addr >> 15) & 0xFF;
	buf[2] = (addr >> 7) & 0xFF;

	return CTP_SPI_WRITE(chip_info->s_client, buf, 3);
}

/*******************************************************
Description:
	Novatek touchscreen read value to specific register.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
static int32_t nvt_read_reg(struct chip_data_nt36523 *chip_info, nvt_ts_reg_t reg, uint8_t *val)
{
	int32_t ret = 0;
	uint32_t addr = 0;
	uint8_t mask = 0;
	uint8_t shift = 0;
	uint8_t buf[8] = {0};
	uint8_t temp = 0;

	addr = reg.addr;
	mask = reg.mask;
	/* get shift */
	temp = reg.mask;
	shift = 0;
	while (1) {
		if ((temp >> shift) & 0x01)
			break;
		if (shift == 8) {
			TPD_INFO("mask all bits zero!\n");
			ret = -1;
			break;
		}
		shift++;
	}
	/* read the byte of the register is in */
	nvt_set_page(chip_info, addr);
	buf[0] = addr & 0xFF;
	buf[1] = 0x00;
	ret = CTP_SPI_READ(chip_info->s_client, buf, 2);
	if (ret < 0) {
		TPD_INFO("CTP_SPI_READ failed!(%d)\n", ret);
		goto nvt_read_register_exit;
	}
	/* get register's value in its field of the byte */
	*val = (buf[1] & mask) >> shift;

nvt_read_register_exit:
	return ret;
}

static void nvt_printk_fw_history(void *chip_data, uint32_t NVT_MMAP_HISTORY_ADDR)
{
	uint8_t i = 0;
	uint8_t buf[66];
	char str[128];
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	nvt_set_page(chip_info, NVT_MMAP_HISTORY_ADDR);

	buf[0] = (uint8_t) (NVT_MMAP_HISTORY_ADDR & 0x7F);
	CTP_SPI_READ(chip_info->s_client, buf, 65);	/*read 64bytes history */

	/*print all data */
	TPD_INFO("fw history 0x%x: \n", NVT_MMAP_HISTORY_ADDR);
	for (i = 0; i < 4; i++) {
		snprintf(str, sizeof(str), "%02x %02x %02x %02x %02x %02x %02x %02x   %02x %02x %02x %02x %02x %02x %02x %02x\n",
		buf[1+i*16], buf[2+i*16], buf[3+i*16], buf[4+i*16],
		buf[5+i*16], buf[6+i*16], buf[7+i*16], buf[8+i*16],
		buf[9+i*16], buf[10+i*16], buf[11+i*16], buf[12+i*16],
		buf[13+i*16], buf[14+i*16], buf[15+i*16], buf[16+i*16]);
		TPD_INFO("%s", str);
	}
}


static uint8_t nvt_wdt_fw_recovery(struct chip_data_nt36523 *chip_info,
				   uint8_t *point_data)
{
	uint32_t recovery_cnt_max = 3;
	uint8_t recovery_enable = false;
	uint8_t i = 0;

	chip_info->recovery_cnt++;

	/* Pattern Check */
	for (i = 1 ; i < 7 ; i++) {
		if ((point_data[i] != 0xFD) && (point_data[i] != 0xFE)) {
			chip_info->recovery_cnt = 0;
			break;
		}
	}

	if (chip_info->recovery_cnt > recovery_cnt_max) {
		recovery_enable = true;
		chip_info->recovery_cnt = 0;
		if (point_data[1] == 0xFE) {
			nvt_sw_reset_idle(chip_info);
		}
		nvt_ts_read_history_log(chip_info);
	}

	if (chip_info->recovery_cnt) {
		TPD_INFO("recovery_cnt is %d (0x%02X)\n", chip_info->recovery_cnt,
			 point_data[1]);
	}

	return recovery_enable;
}

/*********************************************************
Description:
	Novatek touchscreen host esd recovery function.

return:
	Executive outcomes. false-detect 0x77. true-not detect 0x77
**********************************************************/
static bool nvt_fw_recovery(uint8_t *point_data)
{
	uint8_t i = 0;
	bool detected = true;

	/* check pattern */
	for (i = 1 ; i < 7 ; i++) {
	if (point_data[i] != 0x77) {
		detected = false;
		break;
	}
	}

	return detected;
}

/*********************************************************
Description:
	out put sampled data into csv file

Input:
	buffer: where to store
	ts_data: chip_data
	nvt_testdata: auto_testdata
	limit_type: to determine the volume of data

return:
	not handled
**********************************************************/

static int nvt_output_data(int *buffer, struct chip_data_nt36523 *ts_data,
	struct auto_testdata *nvt_testdata, int num_each_line, int data_volumn)
{
	uint8_t data_buf[64] = {0};
	int i = 0;

	if (ts_data == NULL || nvt_testdata == NULL || nvt_testdata->pos == NULL) {
		pr_err("[TP]: %s in %d NULL point\n", __func__, __LINE__);
		return -1;
	}

	TPD_INFO("%s, enter pos[%ld] length[%ld]\n", __func__, *nvt_testdata->pos,
		 nvt_testdata->length);

	memset(data_buf, 0, sizeof(data_buf));
	for (i = 0; i < data_volumn; i += 1) {
		snprintf(data_buf, sizeof(data_buf), "%d,", buffer[i]);
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
			strlen(data_buf), nvt_testdata->pos);
		if (!((i + 1) % num_each_line)) {
			snprintf(data_buf, sizeof(data_buf), "\n");
			tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				strlen(data_buf), nvt_testdata->pos);
		}
	}

	TPD_INFO("%s, i[%d]\n", __func__, i);
	return 0;
}


static void nvt_esd_check_update_timer(struct chip_data_nt36523 *chip_info)
{
	TPD_DEBUG("%s\n", __func__);

	/* update interrupt timer */
	chip_info->irq_timer = jiffies;
}

static void nvt_esd_check_enable(struct chip_data_nt36523 *chip_info, bool enable)
{
	TPD_DEBUG("%s enable=%d\n", __func__, enable);

	/* update interrupt timer */
	chip_info->irq_timer = jiffies;
	/* enable/disable esd check flag */
	chip_info->esd_check_enabled = enable;
	/* clear esd_retry counter, if protect function is enabled */
	chip_info->esd_retry = enable ? 0 : chip_info->esd_retry;
}

static int nvt_esd_handle(void *chip_data)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	unsigned int timer = jiffies_to_msecs(jiffies - chip_info->irq_timer);

	if ((timer > NVT_TOUCH_ESD_CHECK_PERIOD) && chip_info->esd_check_enabled) {
		TPD_INFO("do ESD recovery, timer = %d, retry = %d\n", timer,
			 chip_info->esd_retry);
		/* do esd recovery, bootloader reset */
		nvt_reset(chip_info);
		tp_touch_btnkey_release(chip_info->tp_index);
	/* update interrupt timer */
	chip_info->irq_timer = jiffies;
	/* update esd_retry counter */
	chip_info->esd_retry++;
	}

	return 0;
}

static void nvt_ts_read_history_log(void *chip_data)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	uint8_t buf[65] = {0};
	int32_t i = 0;
	pr_err("[TP]: %s start in %d \n", __func__, __LINE__);

	/*---Read FW  History Event---*/
	nvt_printk_fw_history(chip_info, chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT0);
	nvt_printk_fw_history(chip_info, chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT1);
	if (chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT2 != 0)
		nvt_printk_fw_history(chip_info, chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT2);
	if (chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT3 != 0)
		nvt_printk_fw_history(chip_info, chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT3);
	/*---Read FW Status---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE);
	buf[0] = 0x60;
	for (i = 1; i <65; i++) { buf[i]=0; }
	CTP_SPI_READ(chip_info->s_client, buf, 2);
	TPD_INFO("FW Status:%02X\n", buf[1]);
	/*---Read FW Version---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_FWINFO);
	buf[0] = EVENT_MAP_FWINFO;
	for (i = 1; i <65; i++) { buf[i]=0; }
	CTP_SPI_READ(chip_info->s_client, buf, 2);
	TPD_INFO("FW Version:%02X\n", buf[1]);
	/*TPD_INFO("Read Finlish\n");*/
	pr_err("[TP]: %s end in %d \n", __func__, __LINE__);
}

/*******************************************************
Description:
	Novatek touchscreen check and stop crc reboot loop.

return:
	n.a.
*******************************************************/
static void nvt_esd_reverse_phase(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};

	nvt_set_page(chip_info, 0x3F035);

	buf[0] = 0x35;
	buf[1] = 0x00;
	CTP_SPI_READ(chip_info->s_client, buf, 2);

	if (buf[1] == 0xA5) {
		TPD_INFO("IC is in phase III.\n");

		buf[0] = 0x35;
		buf[1] = 0x00;
		CTP_SPI_WRITE(chip_info->s_client, buf, 2);

		buf[0] = 0x35;
		buf[1] = 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0x00)
			TPD_INFO("Force IC to switch back to phase II OK.\n");
		else
			TPD_INFO("Failed to switch back to phase II, buf[1]=0x%02X\n", buf[1]);
	}
}

static void nvt_stop_crc_reboot(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};
	int32_t retry = 0;

	/*IC is in CRC fail reboot loop, needs to be stopped! */
	for (retry = 5; retry > 0; retry--) {
		/*---reset idle : 1st--- */
		nvt_write_addr(chip_info->s_client, chip_info->swrst_sif_addr, 0xAA);

		/*---reset idle : 2rd--- */
		nvt_write_addr(chip_info->s_client, chip_info->swrst_sif_addr, 0xAA);

		msleep(1);

		/*---clear CRC_ERR_FLAG--- */
		nvt_set_page(chip_info, chip_info->crc_err_flag_addr);

		buf[0] = chip_info->crc_err_flag_addr & 0xFF;
		buf[1] = 0xA5;
		CTP_SPI_WRITE(chip_info->s_client, buf, 2);

		/*---check CRC_ERR_FLAG--- */
		nvt_set_page(chip_info, chip_info->crc_err_flag_addr);

		buf[0] = chip_info->crc_err_flag_addr & 0xFF;
		buf[1] = 0x00;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0xA5) {
			nvt_esd_reverse_phase(chip_info);
			break;
		}
	}

	if (retry == 0)
		TPD_INFO("CRC auto reboot is not able to be stopped! buf[1]=0x%02X\n", buf[1]);
}

/*******************************************************
Description:
	Novatek touchscreen check chip version trim function.

return:
	Executive outcomes. 0---NVT IC. -1---not NVT IC.
*******************************************************/
static int8_t nvt_ts_check_chip_ver_trim(struct chip_data_nt36523 *chip_info,
		struct nvt_ts_hw_reg_addr_info hw_regs)
{
	uint8_t buf[8] = {0};
	int32_t retry = 0;
	int32_t list = 0;
	int32_t i = 0;
	int32_t found_nvt_chip = 0;
	int32_t ret = -1;
	uint8_t enb_casc = 0;

	/* hw reg mapping */
	chip_info->chip_ver_trim_addr = hw_regs.chip_ver_trim_addr;
	chip_info->swrst_sif_addr = hw_regs.swrst_sif_addr;
	chip_info->crc_err_flag_addr = hw_regs.crc_err_flag_addr;

	TPD_INFO("check chip ver trim with chip_ver_trim_addr=0x%06x, "
			"swrst_sif_addr=0x%06x, crc_err_flag_addr=0x%06x\n",
			chip_info->chip_ver_trim_addr, chip_info->swrst_sif_addr, chip_info->crc_err_flag_addr);

	/*---Check for 5 times--- */
	for (retry = 5; retry > 0; retry--) {
		nvt_bootloader_reset_noflash(chip_info);

		nvt_set_page(chip_info, chip_info->chip_ver_trim_addr); /*read chip id */

		buf[0] = chip_info->chip_ver_trim_addr & 0x7F;  /*offset */
		buf[1] = 0x00;
		buf[2] = 0x00;
		buf[3] = 0x00;
		buf[4] = 0x00;
		buf[5] = 0x00;
		buf[6] = 0x00;
		CTP_SPI_WRITE(chip_info->s_client, buf, 7);

		buf[0] = chip_info->chip_ver_trim_addr & 0x7F;  /*offset */
		buf[1] = 0x00;
		buf[2] = 0x00;
		buf[3] = 0x00;
		buf[4] = 0x00;
		buf[5] = 0x00;
		buf[6] = 0x00;
		CTP_SPI_READ(chip_info->s_client, buf, 7);
		TPD_INFO("chip id:0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
				 buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

		/* compare read chip id on supported list*/
		for (list = 0;
				list < (sizeof(trim_id_table) / sizeof(struct nvt_ts_trim_id_table)); list++) {
			found_nvt_chip = 0;

			/* compare each byte */
			for (i = 0; i < NVT_ID_BYTE_MAX; i++) {
				if (trim_id_table[list].mask[i]) {
					if (buf[i + 1] != trim_id_table[list].id[i]) {  /*set parameter from chip id*/
						break;
					}
				}
			}

			if (i == NVT_ID_BYTE_MAX) {
				found_nvt_chip = 1;
			}

			if (found_nvt_chip) {
				TPD_INFO("This is NVT touch IC\n");
				if (trim_id_table[list].mmap->ENB_CASC_REG.addr) {
					/* check single or cascade */
					nvt_read_reg(chip_info, trim_id_table[list].mmap->ENB_CASC_REG, &enb_casc);
					/* TPD_INFO("ENB_CASC=0x%02X\n", enb_casc); */
					if (enb_casc & 0x01) {
						TPD_INFO("Single Chip\n");
						chip_info->trim_id_table.mmap = trim_id_table[list].mmap;
					} else {
						TPD_INFO("Cascade Chip\n");
						chip_info->trim_id_table.mmap = trim_id_table[list].mmap_casc;
					}
				} else {
					/* for chip that do not have ENB_CASC */
					chip_info->trim_id_table.mmap = trim_id_table[list].mmap;
				}

				chip_info->hw_crc = trim_id_table[list].hwinfo->hw_crc;
				chip_info->auto_copy = trim_id_table[list].hwinfo->auto_copy;

				/* hw reg re-mapping */
				chip_info->chip_ver_trim_addr = trim_id_table[list].hwinfo->hw_regs->chip_ver_trim_addr;
				chip_info->swrst_sif_addr = trim_id_table[list].hwinfo->hw_regs->swrst_sif_addr;
				chip_info->crc_err_flag_addr = trim_id_table[list].hwinfo->hw_regs->crc_err_flag_addr;

				TPD_INFO("set reg chip_ver_trim_addr=0x%06x, "
						"swrst_sif_addr=0x%06x, crc_err_flag_addr=0x%06x\n",
						chip_info->chip_ver_trim_addr, chip_info->swrst_sif_addr, chip_info->crc_err_flag_addr);

				ret = 0;
				goto out;
			} else {
				chip_info->trim_id_table.mmap = NULL;
				ret = -1;
			}
		}

		msleep(10);
	}

	if (chip_info->trim_id_table.mmap == NULL) {  /*set default value */
		chip_info->trim_id_table.mmap = &NT36523_memory_map;
		chip_info->hw_crc = HWCRC_LEN_3Bytes;
		chip_info->auto_copy = CHECK_SPI_DMA_TX_INFO;
	}

out:
	TPD_INFO("list = %d, hw_crc is %d, auto_copy is %d\n", list,
		 chip_info->hw_crc, chip_info->auto_copy);
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen check chip version trim loop
	function. Check chip version trim via hw regs table.

return:
	Executive outcomes. 0---NVT IC. -1---not NVT IC.
*******************************************************/
static int32_t nvt_ts_check_chip_ver_trim_loop(struct chip_data_nt36523 *chip_info) {
	uint8_t i = 0;
	int32_t ret = 0;

	struct nvt_ts_hw_reg_addr_info hw_regs_table[] = {
		hw_reg_addr_info,
		hw_reg_addr_info_old_w_isp
	};

	for (i = 0; i < (sizeof(hw_regs_table) / sizeof(struct nvt_ts_hw_reg_addr_info)); i++) {
	/* ---check chip version trim--- */
	ret = nvt_ts_check_chip_ver_trim(chip_info, hw_regs_table[i]);
		if (!ret) {
			break;
		}
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen check FW reset state function.

return:
	Executive outcomes. 0---succeed. -1---failed.
*******************************************************/
static int32_t nvt_check_fw_reset_state_noflash(
	struct chip_data_nt36523 *chip_info, RST_COMPLETE_STATE check_reset_state)
{
	uint8_t buf[8] = {0};
	int32_t ret = 0;
	int32_t retry = 0;
	int32_t retry_max = (check_reset_state == RESET_STATE_INIT) ? 10 : 100;

	while (1) {
	msleep(10);

	/* ---read reset state--- */
	buf[0] = EVENT_MAP_RESET_COMPLETE;
	buf[1] = 0x00;
	CTP_SPI_READ(chip_info->s_client, buf, 6);

	if ((buf[1] >= check_reset_state) && (buf[1] <= RESET_STATE_MAX)) {
		ret = 0;
		break;
	}

	retry++;
	if(unlikely(retry > retry_max)) {
			TPD_INFO("error, retry=%d, buf[1]=0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
				 retry, buf[1], buf[2], buf[3], buf[4], buf[5]);
		ret = -1;
		break;
	}
	}

	return ret;
}

static int nvt_enter_sleep(struct chip_data_nt36523 *chip_info, bool config)
{
	int ret = -1;
	struct touchpanel_data *ts = chip_info->ts;

	if (config) {
		ret = nvt_cmd_store(chip_info, CMD_ENTER_SLEEP);
		if (ret < 0) {
			TPD_INFO("%s: enter sleep mode failed!\n", __func__);
			return -1;
		} else {
			chip_info->is_sleep_writed = true;
			TPD_INFO("%s: enter sleep mode sucess!\n", __func__);
		}
		msleep(50);

		/*set spi cs to low while tp in sleep mode*/
		if (!(IS_ERR_OR_NULL(ts->hw_res.pinctrl) || IS_ERR_OR_NULL(ts->hw_res.pin_set_low)))
			pinctrl_select_state(ts->hw_res.pinctrl, ts->hw_res.pin_set_low);
	}

	return ret;
}

static int32_t nvt_get_fw_info_noflash(struct chip_data_nt36523 *chip_info)
{
	struct touchpanel_data *ts = spi_get_drvdata(chip_info->s_client);
	uint8_t buf[64] = {0};
	uint32_t retry_count = 0;
	int32_t ret = 0;

info_retry:
	/*---set xdata index to EVENT BUF ADDR---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR |
			 EVENT_MAP_FWINFO);

	/*---read fw info--- */
	buf[0] = EVENT_MAP_FWINFO;
	CTP_SPI_READ(chip_info->s_client, buf, 39);

	/*---clear x_num, y_num if fw info is broken--- */
	if ((buf[1] + buf[2]) != 0xFF) {
	TPD_INFO("FW info is broken! fw_ver=0x%02X, ~fw_ver=0x%02X\n", buf[1], buf[2]);
	chip_info->fw_ver = 0;

	if(retry_count < 3) {
		retry_count++;
		TPD_INFO("retry_count=%d\n", retry_count);
		goto info_retry;
	} else {
		TPD_INFO("Set default fw_ver=0!\n");
		ret = -1;
	}
	} else {
		chip_info->fw_ver = buf[1];
		/*chip_info->ts->chip_version = chip_info->fw_ver;*/
		chip_info->fw_eventbuf_prot = buf[13];
		chip_info->fw_sub_ver = buf[14];
		chip_info->nvt_pid = (uint16_t)((buf[36] << 8) | buf[35]);
		TPD_INFO("fw_ver = 0x%02X, fw_type=0x%02X, fw_eventbuf_prot=0x%02X, PID=0x%04X\n",
				chip_info->fw_ver, chip_info->fw_sub_ver,
				chip_info->fw_eventbuf_prot, chip_info->nvt_pid);
		ret = 0;
	}

	if (chip_info->fw_eventbuf_prot == NVT_EVENTBUF_PROT_HIGH_RESO) {
		ts->ts_ops->get_touch_points = nvt_get_touch_points_high_reso;
	} else {
		ts->ts_ops->get_touch_points = nvt_get_touch_points;
	}

	return ret;
}

static uint32_t byte_to_word(const uint8_t *data)
{
	return data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24);
}

static uint32_t CheckSum(const u8 *data, size_t len)
{
	uint32_t i = 0;
	uint32_t checksum = 0;

	for (i = 0; i < len + 1; i++) {
		checksum += data[i];
	}

	checksum += len;
	checksum = ~checksum + 1;

	return checksum;
}

static int32_t nvt_bin_header_parser(struct chip_data_nt36523 *chip_info,
					 const u8 *fwdata, size_t fwsize)
{
	uint32_t list = 0;
	uint32_t pos = 0x00;
	uint32_t tmp_end = 0x00;
	uint8_t info_sec_num = 0;
	uint8_t ovly_sec_num = 0;
	uint8_t ovly_info = 0;
	uint8_t find_bin_header = 0;

	/* Find the header size */
	tmp_end = fwdata[0] + (fwdata[1] << 8) + (fwdata[2] << 16) + (fwdata[3] << 24);

	/* check cascade next header */
	chip_info->cascade_2nd_header_info = (fwdata[0x20] & 0x02) >> 1;
	TPD_INFO("cascade_2nd_header_info = %d\n", chip_info->cascade_2nd_header_info);

	if (chip_info->cascade_2nd_header_info) {
		pos = 0x30;	/* info section start at 0x30 offset */
		while (pos < (tmp_end / 2)) {
			info_sec_num++;
			pos += 0x10;	/* each header info is 16 bytes */
		}

		info_sec_num = info_sec_num + 1; /*next header section */
	} else {
		pos = 0x30;	/* info section start at 0x30 offset */
		while (pos < tmp_end) {
			info_sec_num++;
			pos += 0x10;	/* each header info is 16 bytes */
		}
	}

	/*
	 * Find the DLM OVLY section
	 * [0:3] Overlay Section Number
	 * [4]   Overlay Info
	 */
	ovly_info = (fwdata[0x28] & 0x10) >> 4;
	ovly_sec_num = (ovly_info) ? (fwdata[0x28] & 0x0F) : 0;

	/*
	 * calculate all partition number
	 * ilm_dlm_num (ILM & DLM) + ovly_sec_num + info_sec_num
	 */
	chip_info->partition = chip_info->ilm_dlm_num + ovly_sec_num + info_sec_num;
	TPD_DEBUG("ovly_info = %d, ilm_dlm_num = %d, ovly_sec_num = %d, info_sec_num = %d, partition = %d\n",
		 ovly_info, chip_info->ilm_dlm_num, ovly_sec_num, info_sec_num,
		 chip_info->partition);

	/* allocated memory for header info */
	chip_info->bin_map = (struct nvt_ts_bin_map *)tp_devm_kzalloc(
					 &chip_info->s_client->dev,
					 (chip_info->partition + 1) * sizeof(struct nvt_ts_bin_map), GFP_KERNEL);

	if(chip_info->bin_map == NULL) {
	TPD_INFO("kzalloc for bin_map failed!\n");
	return -ENOMEM;
	}

	for (list = 0; list < chip_info->partition; list++) {
		/*
		 * [1] parsing ILM & DLM header info
		 * BIN_addr : SRAM_addr : size (12-bytes)
		 * crc located at 0x18 & 0x1C
		 */
		if (list < chip_info->ilm_dlm_num) {
			chip_info->bin_map[list].BIN_addr = byte_to_word(&fwdata[0 + list * 12]);
			chip_info->bin_map[list].SRAM_addr = byte_to_word(&fwdata[4 + list * 12]);
			chip_info->bin_map[list].size = byte_to_word(&fwdata[8 + list * 12]);
			chip_info->bin_map[list].crc = byte_to_word(&fwdata[0x18 + list * 4]);
			if (list == 0) {
				snprintf(chip_info->bin_map[list].name, 12, "ILM");

			} else if (list == 1) {
				snprintf(chip_info->bin_map[list].name, 12, "DLM");
			}
		}

		/*
		 * [2] parsing others header info
		 * SRAM_addr : size : BIN_addr : crc (16-bytes)
		 */
		if ((list >= chip_info->ilm_dlm_num)
				&& (list < (chip_info->ilm_dlm_num + info_sec_num))) {
			if (find_bin_header == 0) {
			/* others partition located at 0x30 offset */
			pos = 0x30 + (0x10 * (list - chip_info->ilm_dlm_num));
			} else if (find_bin_header && chip_info->cascade_2nd_header_info) {
				/* cascade 2nd header info */
				pos = tmp_end - 0x10;
			}

			chip_info->bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
			chip_info->bin_map[list].size = byte_to_word(&fwdata[pos + 4]);
			chip_info->bin_map[list].BIN_addr = byte_to_word(&fwdata[pos + 8]);
			chip_info->bin_map[list].crc = byte_to_word(&fwdata[pos + 12]);
			/* detect header end to protect parser function */
			if ((chip_info->bin_map[list].BIN_addr < tmp_end) && (chip_info->bin_map[list].size != 0)) {
				snprintf(chip_info->bin_map[list].name, 12, "Header");
				find_bin_header = 1;
			} else {
				snprintf(chip_info->bin_map[list].name, 12, "Info-%d",
					(list - chip_info->ilm_dlm_num));
			}
		}

		/*
		 * [3] parsing overlay section header info
		 * SRAM_addr : size : BIN_addr : crc (16-bytes)
		 */
		if (list >= (chip_info->ilm_dlm_num + info_sec_num)) {
			/* overlay info located at DLM (list = 1) start addr */
			pos = chip_info->bin_map[1].BIN_addr + (0x10 * (list - chip_info->ilm_dlm_num -
								info_sec_num));

			chip_info->bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
			chip_info->bin_map[list].size = byte_to_word(&fwdata[pos + 4]);
			chip_info->bin_map[list].BIN_addr = byte_to_word(&fwdata[pos + 8]);
			chip_info->bin_map[list].crc = byte_to_word(&fwdata[pos + 12]);
			snprintf(chip_info->bin_map[list].name, 12, "Overlay-%d",
				(list - chip_info->ilm_dlm_num - info_sec_num));
		}

		/* BIN size error detect */
		if ((chip_info->bin_map[list].BIN_addr + chip_info->bin_map[list].size) >
				fwsize) {
			TPD_INFO("access range (0x%08X to 0x%08X) is larger than bin size!\n",
				chip_info->bin_map[list].BIN_addr,
				chip_info->bin_map[list].BIN_addr + chip_info->bin_map[list].size);
			return -EINVAL;
		}
	}

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen init variable and allocate buffer
for download firmware function.

return:
	n.a.
*******************************************************/
static int32_t Download_Init(struct chip_data_nt36523 *chip_info)
{
	/* allocate buffer for transfer firmware */
	/* TPD_INFO("NVT_TRANSFER_LEN = %d\n", NVT_TRANSFER_LEN); */

	if (chip_info->fwbuf == NULL) {
		chip_info->fwbuf = (uint8_t *)tp_devm_kzalloc(&chip_info->s_client->dev,
				(NVT_TRANSFER_LEN + 1 + DUMMY_BYTES), GFP_KERNEL);

		if(chip_info->fwbuf == NULL) {
			TPD_INFO("kzalloc for fwbuf failed!\n");
			return -ENOMEM;
		}
	}

	return 0;
}

/*******************************************************
Description:
		Novatek touchscreen Write_Partition function to write
firmware into each partition.

return:
	n.a.
*******************************************************/
static int32_t Write_Partition(struct chip_data_nt36523 *chip_info,
				   const u8 *fwdata, size_t fwsize)
{
	uint32_t list = 0;
	char *name;
	uint32_t BIN_addr, SRAM_addr, size;
	uint32_t i = 0;
	uint16_t len = 0;
	int32_t count = 0;
	int32_t ret = 0;

	for (list = 0; list < chip_info->partition; list++) {
		/* initialize variable */
		SRAM_addr = chip_info->bin_map[list].SRAM_addr;
		size = chip_info->bin_map[list].size;
		BIN_addr = chip_info->bin_map[list].BIN_addr;
		name = chip_info->bin_map[list].name;

		/* TPD_INFO("[%d][%s] SRAM (0x%08X), SIZE (0x%08X), BIN (0x%08X)\n",
					list, name, SRAM_addr, size, BIN_addr); */

		/* Check data size */
		if ((BIN_addr + size) > fwsize) {
			TPD_INFO("access range (0x%08X to 0x%08X) is larger than bin size!\n",
					 BIN_addr, BIN_addr + size);
			ret = -1;
			goto out;
		}

		/* ignore reserved partition (Reserved Partition size is zero)*/
		if (!size) {
			continue;

		} else {
			size = size + 1;
		}

		/* write data to SRAM*/
		if (size % NVT_TRANSFER_LEN) {
			count = (size / NVT_TRANSFER_LEN) + 1;
		} else {
			count = (size / NVT_TRANSFER_LEN);
		}

		for (i = 0 ; i < count ; i++) {
			len = (size < NVT_TRANSFER_LEN) ? size : NVT_TRANSFER_LEN;

			/*---set xdata index to start address of SRAM--- */
			nvt_set_page(chip_info, SRAM_addr);

			/*---write data into SRAM--- */
			chip_info->fwbuf[0] = SRAM_addr & 0x7F; /*offset */
			memcpy(chip_info->fwbuf + 1, &fwdata[BIN_addr], len);   /*payload */
			CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, len + 1);

			SRAM_addr += NVT_TRANSFER_LEN;
			BIN_addr += NVT_TRANSFER_LEN;
			size -= NVT_TRANSFER_LEN;
		}
	}

out:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen clear status & enable fw crc function.

return:
	N/A.
*******************************************************/
static void nvt_fw_crc_enable(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[4] = {0};

	/*---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	/*---clear fw reset status--- */
	buf[0] = EVENT_MAP_RESET_COMPLETE & (0x7F);
	buf[1] = 0x00;
	CTP_SPI_WRITE(chip_info->s_client, buf, 2);

	/*---enable fw crc--- */
	buf[0] = EVENT_MAP_HOST_CMD & (0x7F);
	buf[1] = 0xAE;  /* enable fw crc command*/
	CTP_SPI_WRITE(chip_info->s_client, buf, 2);
}

/*******************************************************
Description:
	Novatek touchscreen set boot ready function.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
static void nvt_boot_ready(struct chip_data_nt36523 *chip_info, uint8_t ready)
{
	/*---write BOOT_RDY status cmds---*/
	nvt_write_addr(chip_info->s_client,
			   chip_info->trim_id_table.mmap->BOOT_RDY_ADDR, 1);

	mdelay(5);

	/*---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);
}

/*******************************************************
Description:
	Novatek touchscreen enable auto copy mode function.

return:
	N/A.
*******************************************************/
static void nvt_tx_auto_copy_mode(struct chip_data_nt36523 *chip_info)
{
	if (chip_info->auto_copy == CHECK_SPI_DMA_TX_INFO) {
		/* ---write TX_AUTO_COPY_EN cmds--- */
		nvt_write_addr(chip_info->s_client, chip_info->trim_id_table.mmap->TX_AUTO_COPY_EN, 0x69);
	} else if (chip_info->auto_copy == CHECK_TX_AUTO_COPY_EN) {
		/*---write SPI_MST_AUTO_COPY cmds--- */
		nvt_write_addr(chip_info->s_client, chip_info->trim_id_table.mmap->TX_AUTO_COPY_EN, 0x56);
	}

	TPD_INFO("tx auto copy mode %d enable\n", chip_info->auto_copy);
}

/*******************************************************
Description:
	Novatek touchscreen check spi dma tx info function.

return:
	N/A.
*******************************************************/
static int32_t nvt_check_spi_dma_tx_info(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 200;

	if (chip_info->trim_id_table.mmap->SPI_DMA_TX_INFO == 0) {
		TPD_INFO("error, SPI_DMA_TX_INFO = 0\n");
		return -1;
	}

	for (i = 0; i < retry; i++) {
		/*---set xdata index to SPI_DMA_TX_INFO--- */
		nvt_set_page(chip_info, chip_info->trim_id_table.mmap->SPI_DMA_TX_INFO);

		/*---read fw status--- */
		buf[0] = chip_info->trim_id_table.mmap->SPI_DMA_TX_INFO & 0x7F;
		buf[1] = 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(1000, 1000);
	}

	if (i >= retry) {
		TPD_INFO("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen check tx auto copy state function.

return:
	Executive outcomes. 0---succeed. -1---fail.
*******************************************************/
static int32_t nvt_check_tx_auto_copy(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 200;

	if (chip_info->trim_id_table.mmap->TX_AUTO_COPY_EN == 0) {
		TPD_INFO("error, TX_AUTO_COPY_EN = 0\n");
		return -1;
	}

	for (i = 0; i < retry; i++) {
		/* ---set xdata index to SPI_MST_AUTO_COPY--- */
		nvt_set_page(chip_info, chip_info->trim_id_table.mmap->TX_AUTO_COPY_EN);

		/* ---read auto copy status--- */
		buf[0] = chip_info->trim_id_table.mmap->TX_AUTO_COPY_EN & 0x7F;
		buf[1] = 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(1000, 1000);
	}

	if (i >= retry) {
		TPD_INFO("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen wait auto copy finished function.

return:
	Executive outcomes. 0---succeed. -1---fail.
*******************************************************/
static int32_t nvt_wait_auto_copy(struct chip_data_nt36523 *chip_info)
{
	if (chip_info->auto_copy == CHECK_SPI_DMA_TX_INFO) {
		return nvt_check_spi_dma_tx_info(chip_info);
	} else if (chip_info->auto_copy == CHECK_TX_AUTO_COPY_EN) {
		return nvt_check_tx_auto_copy(chip_info);
	} else {
		TPD_INFO("failed, not support mode %d!\n", chip_info->auto_copy);
		return -1;
	}
}

/*******************************************************
Description:
	Novatek touchscreen set bootload crc reg bank function.
This function will set hw crc reg before enable crc function.

return:
	n.a.
*******************************************************/
static void nvt_set_bld_crc_bank(struct chip_data_nt36523 *chip_info,
							 uint32_t DES_ADDR, uint32_t SRAM_ADDR,
							 uint32_t LENGTH_ADDR, uint32_t size,
							 uint32_t G_CHECKSUM_ADDR, uint32_t crc)
{
	/* write destination address */
	nvt_set_page(chip_info, DES_ADDR);
	chip_info->fwbuf[0] = DES_ADDR & 0x7F;
	chip_info->fwbuf[1] = (SRAM_ADDR) & 0xFF;
	chip_info->fwbuf[2] = (SRAM_ADDR >> 8) & 0xFF;
	chip_info->fwbuf[3] = (SRAM_ADDR >> 16) & 0xFF;
	CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, 4);

	/* write length */
	chip_info->fwbuf[0] = LENGTH_ADDR & 0x7F;
	chip_info->fwbuf[1] = (size) & 0xFF;
	chip_info->fwbuf[2] = (size >> 8) & 0xFF;
	chip_info->fwbuf[3] = (size >> 16) & 0xFF;
	if (chip_info->hw_crc == HWCRC_LEN_2Bytes) {
		CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, 3);
	} else if (chip_info->hw_crc >= HWCRC_LEN_3Bytes) {
		CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, 4);
	}

	/* write golden dlm checksum */
	chip_info->fwbuf[0] = G_CHECKSUM_ADDR & 0x7F;
	chip_info->fwbuf[1] = (crc) & 0xFF;
	chip_info->fwbuf[2] = (crc >> 8) & 0xFF;
	chip_info->fwbuf[3] = (crc >> 16) & 0xFF;
	chip_info->fwbuf[4] = (crc >> 24) & 0xFF;
	CTP_SPI_WRITE(chip_info->s_client, chip_info->fwbuf, 5);

	return;
}

/*******************************************************
Description:
	Novatek touchscreen check DMA hw crc function.
This function will check hw crc result is pass or not.

return:
	n.a.
*******************************************************/
static void nvt_set_bld_hw_crc(struct chip_data_nt36523 *chip_info)
{
	/* [0] ILM */
	/* write register bank */
	nvt_set_bld_crc_bank(chip_info,
					 chip_info->trim_id_table.mmap->ILM_DES_ADDR, chip_info->bin_map[0].SRAM_addr,
					 chip_info->trim_id_table.mmap->ILM_LENGTH_ADDR, chip_info->bin_map[0].size,
					 chip_info->trim_id_table.mmap->G_ILM_CHECKSUM_ADDR, chip_info->bin_map[0].crc);

	/* [1] DLM */
	/* write register bank */
	nvt_set_bld_crc_bank(chip_info,
					 chip_info->trim_id_table.mmap->DLM_DES_ADDR, chip_info->bin_map[1].SRAM_addr,
					 chip_info->trim_id_table.mmap->DLM_LENGTH_ADDR, chip_info->bin_map[1].size,
					 chip_info->trim_id_table.mmap->G_DLM_CHECKSUM_ADDR, chip_info->bin_map[1].crc);
}

/*******************************************************
Description:
	Novatek touchscreen read BLD hw crc info function.
This function will check crc results from register.

return:
	n.a.
*******************************************************/
static void nvt_read_bld_hw_crc(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};
	uint32_t g_crc = 0, r_crc = 0;

	/* CRC Flag */
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->BLD_ILM_DLM_CRC_ADDR);
	buf[0] = chip_info->trim_id_table.mmap->BLD_ILM_DLM_CRC_ADDR & 0x7F;
	buf[1] = 0x00;
	CTP_SPI_READ(chip_info->s_client, buf, 2);
	TPD_INFO("crc_done = %d, ilm_crc_flag = %d, dlm_crc_flag = %d\n",
		(buf[1] >> 2) & 0x01, (buf[1] >> 0) & 0x01, (buf[1] >> 1) & 0x01);

	/* ILM CRC */
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->G_ILM_CHECKSUM_ADDR);
	buf[0] = chip_info->trim_id_table.mmap->G_ILM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(chip_info->s_client, buf, 5);
	g_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->R_ILM_CHECKSUM_ADDR);
	buf[0] = chip_info->trim_id_table.mmap->R_ILM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(chip_info->s_client, buf, 5);
	r_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	TPD_INFO("ilm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X\n",
		chip_info->bin_map[0].crc, g_crc, r_crc);

	/* DLM CRC */
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->G_DLM_CHECKSUM_ADDR);
	buf[0] = chip_info->trim_id_table.mmap->G_DLM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(chip_info->s_client, buf, 5);
	g_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->R_DLM_CHECKSUM_ADDR);
	buf[0] = chip_info->trim_id_table.mmap->R_DLM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(chip_info->s_client, buf, 5);
	r_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	TPD_INFO("dlm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X\n",
		chip_info->bin_map[1].crc, g_crc, r_crc);

	return;
}

#if NVT_TOUCH_ESD_DISP_RECOVERY
static int32_t nvt_check_crc_done_ilm_err(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};

	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->BLD_ILM_DLM_CRC_ADDR);
	buf[0] = chip_info->trim_id_table.mmap->BLD_ILM_DLM_CRC_ADDR & 0x7F;
	buf[1] = 0x00;
	CTP_SPI_READ(chip_info->s_client, buf, 2);

	TPD_INFO("CRC DONE, ILM DLM FLAG = 0x%02X\n", buf[1]);
	if (((buf[1] & ILM_CRC_FLAG) && (buf[1] & CRC_DONE)) ||
			(buf[1] == 0xFE)) {
		return 1;
	} else {
		return 0;
	}
}

static int32_t nvt_f2c_read_write(struct chip_data_nt36523 *chip_info,
		uint8_t F2C_RW, uint32_t DDIC_REG_ADDR, uint16_t len, uint8_t *data)
{
	uint8_t buf[8] = {0};
	uint8_t retry = 0;
	uint8_t f2c_control = 0;
	uint32_t f2c_retry = 0;
	uint32_t retry_max = 1000;
	int32_t ret = 0;

	nvt_sw_reset_idle(chip_info);

	/*Setp1: Set REG CPU_IF_ADDR[15:0] */
	nvt_set_page(chip_info, CPU_IF_ADDR_LOW);
	buf[0] = CPU_IF_ADDR_LOW & 0x7F;
	buf[1] = (DDIC_REG_ADDR) & 0xFF;
	buf[2] = (DDIC_REG_ADDR >> 8) & 0xFF;
	CTP_SPI_WRITE(chip_info->s_client, buf, 3);


	/*Step2: Set REG FFM_ADDR[15:0] */
	nvt_set_page(chip_info, FFM_ADDR_LOW);
	buf[0] = FFM_ADDR_LOW & 0x7F;
	buf[1] = (TOUCH_DATA_ADDR) & 0xFF;
	buf[2] = (TOUCH_DATA_ADDR >> 8) & 0xFF;
	buf[3] = 0x00;
	if (chip_info->hw_crc == HWCRC_LEN_2Bytes) {
		CTP_SPI_WRITE(chip_info->s_client, buf, 3);
	} else if (chip_info->trim_id_table.hw_crc >= HWCRC_LEN_3Bytes) {
		CTP_SPI_WRITE(chip_info->s_client, buf, 4);
	}

	/*Step3: Write Data to TOUCH_DATA_ADDR */
	nvt_write_addr(chip_info->s_client, TOUCH_DATA_ADDR, *data);

	/*Step4: Set REG F2C_LENGT[H7:0] */
	nvt_write_addr(chip_info->s_client, F2C_LENGTH, len);

	/*Enable CP_TP_CPU_REQ */
	nvt_write_addr(chip_info->s_client, CP_TP_CPU_REQ, 1);

nvt_f2c_retry:
	/*Step5: Set REG CPU_Polling_En, F2C_RW, CPU_IF_ADDR_INC, F2C_EN */
	nvt_set_page(chip_info, FFM2CPU_CTL);
	buf[0] = FFM2CPU_CTL & 0x7F;
	buf[1] = 0xFF;
	ret = CTP_SPI_READ(chip_info->s_client, buf,  1 + len);/*1 is AddrL */
	if (ret) {
		TPD_INFO("Read FFM2CPU control failed!\n");
		return ret;
	}

	f2c_control = buf[1] |
		(0x01 << BIT_F2C_EN) |
		(0x01 << BIT_CPU_IF_ADDR_INC) |
		(0x01 << BIT_CPU_POLLING_EN);

	if (F2C_RW == F2C_RW_READ) {
		f2c_control = f2c_control & (~(1 << BIT_F2C_RW));
	} else if (F2C_RW == F2C_RW_WRITE) {
		f2c_control = f2c_control | (1 << BIT_F2C_RW);
	}

	nvt_write_addr(chip_info->s_client, FFM2CPU_CTL, f2c_control);

	/*Step6: wait F2C_EN = 0 */
	retry = 0;
	while (1) {
		nvt_set_page(chip_info, FFM2CPU_CTL);
		buf[0] = FFM2CPU_CTL & 0x7F;
		buf[1] = 0xFF;
		buf[2] = 0xFF;
		ret = CTP_SPI_READ(chip_info->s_client, buf,  3);
		if (ret) {
			TPD_INFO("Read FFM2CPU control failed!\n");
			return ret;
		}

		if ((buf[1] & 0x01) == 0x00) {
			break;
		}

		usleep_range(1000, 1000);
		retry++;

		if(unlikely(retry > 20)) {
			TPD_INFO("Wait F2C_EN = 0 failed! retry %d\n", retry);
			return -EIO;
		}
	}
	/*Step7: Check REG TH_CPU_CHK  status (1: Success,  0: Fail), if 0, can Retry Step5. */
	if (((buf[2] & 0x04) >> 2) != 0x01) {
		f2c_retry++;
		if (f2c_retry <= retry_max) {
			goto nvt_f2c_retry;
		} else {
			TPD_INFO("check TH_CPU_CHK failed!, buf[1]=0x%02X, buf[2]=0x%02X\n", buf[1],
				 buf[2]);
			return -EIO;
		}
	}

	if (F2C_RW == F2C_RW_READ) {
		nvt_set_page(chip_info, TOUCH_DATA_ADDR);
		buf[0] = TOUCH_DATA_ADDR & 0x7F;
		buf[1] = 0xFF;
		ret = CTP_SPI_READ(chip_info->s_client, buf,  1 + len);/*1 is AddrL */
		if (ret) {
			TPD_INFO("Read data failed!\n");
			return ret;
		}
		*data = buf[1];
	}

	return ret;
}

static int32_t nvt_f2c_disp_off(struct chip_data_nt36523 *chip_info)
{
	uint8_t data = 0x00;

	return nvt_f2c_read_write(chip_info, F2C_RW_WRITE, DISP_OFF_ADDR, 1, &data);
}
#endif /* NVT_TOUCH_ESD_DISP_RECOVERY */

/*******************************************************
Description:
	Novatek touchscreen Download_Firmware with HW CRC
function. It's complete download firmware flow.

return:
	n.a.
*******************************************************/
static int32_t Download_Firmware_HW_CRC(struct chip_data_nt36523 *chip_info,
					const struct firmware *fw)
{
	uint8_t retry = 0;
	int32_t ret = 0;
	uint8_t buf[8] = {0};

	TPD_DETAIL("Enter %s\n", __func__);
	chip_info->start = ktime_get();

	while (1) {
	nvt_esd_check_update_timer(chip_info);

	/* bootloader reset to reset MCU */
	nvt_bootloader_reset_noflash(chip_info);

	/* set ilm & dlm reg bank */
	nvt_set_bld_hw_crc(chip_info);

	/* Start Write Firmware Process */
	if (chip_info->cascade_2nd_header_info) {
		/* for cascade */
		nvt_tx_auto_copy_mode(chip_info);

		ret = Write_Partition(chip_info, fw->data, fw->size);
		if (ret) {
			TPD_INFO("Write_Firmware failed. (%d)\n", ret);
			goto fail;
		}

		ret = nvt_wait_auto_copy(chip_info);
		if (ret) {
			TPD_INFO("wait auto copy failed. (%d)\n", ret);
			goto fail;
		}
	} else {
		ret = Write_Partition(chip_info, fw->data, fw->size);
		if (ret) {
			TPD_INFO("Write_Firmware failed. (%d)\n", ret);
			goto fail;
		}
	}

	/* clear fw reset status & enable fw crc check */
	nvt_fw_crc_enable(chip_info);

	/* Set Boot Ready Bit */
	nvt_boot_ready(chip_info, true);

	ret = nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_INIT);
	if (ret) {
		TPD_INFO("nvt_check_fw_reset_state_noflash failed. (%d)\n", ret);
		goto fail;
	} else {
		break;
	}

fail:
		/* dummy read to check 0xFC */
		CTP_SPI_READ(chip_info->s_client, buf, 2);
		TPD_INFO("dummy read = 0x%x\n", buf[1]);
		if (buf[1] == 0xFC) {
			nvt_stop_crc_reboot(chip_info);
		}
	retry++;
	if(unlikely(retry > 2) || chip_info->using_headfile) {
		TPD_INFO("error, retry=%d\n", retry);
			nvt_read_bld_hw_crc(chip_info);
#if NVT_TOUCH_ESD_DISP_RECOVERY
			if (nvt_check_crc_done_ilm_err(chip_info)) {
				TPD_INFO("set display off to trigger display esd recovery.\n");
				nvt_f2c_disp_off(chip_info);
			}
#endif /* #if NVT_TOUCH_ESD_DISP_RECOVERY */
		break;
	}
	}

	chip_info->end = ktime_get();
	return ret;
}

static int32_t nvt_nf_detect_chip(struct chip_data_nt36523 *chip_info)
{
	int32_t ret = 0;
	int i;
	uint8_t buf[8] = {0};

	ret = nvt_set_page(chip_info, chip_info->chip_ver_trim_addr);

	buf[0] = chip_info->chip_ver_trim_addr & 0x7F;  /*offset */
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x00;
	ret = CTP_SPI_READ(chip_info->s_client, buf, 2);
	for(i = 1; i < 7; i++) {
		TPD_INFO("buf[%d] is 0x%02X\n", i, buf[i]);
		if(buf[i] != 0) {
			return 0;
		}
	}
	return -ENODEV;
}


/********* Start of implementation of oplus_touchpanel_operations callbacks********************/
/*extern int tp_util_get_vendor(struct hw_resource *hw_res, struct panel_info *panel_data);*/

static int nvt_ftm_process(void *chip_data)
{
	int ret = -1;
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	const struct firmware *fw = NULL;

	TPD_INFO("%s is called!\n", __func__);

	ret = nvt_get_chip_info(chip_info);
	if (ret) {
		TPD_INFO("chip is not identified\n");
		ret = -EINVAL;
	}

	ret = nvt_nf_detect_chip(chip_info);
	if(ret < 0) {
		TPD_INFO("no find novatek chip or spi fatal error\n");
		return -EINVAL;
	} else {
		ret = nvt_fw_update_sub(chip_info, fw, 0);
		if(ret > 0) {
			TPD_INFO("%s fw update failed!\n", __func__);
		} else {
			ret = nvt_enter_sleep(chip_info, true);
		}
	}

	return ret;
}

static int nvt_power_control(void *chip_data, bool enable)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
		gpio_direction_output(chip_info->hw_res->reset_gpio, 1);
	}

	return 0;
}

static int nvt_get_chip_info(void *chip_data)
{
	int ret = -1;
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	/*---check chip version trim--- */
	ret = nvt_ts_check_chip_ver_trim_loop(chip_info);
	if (ret) {
		TPD_INFO("chip is not identified\n");
		ret = -EINVAL;
	}

	return ret;
}

static int nvt_get_vendor(void *chip_data, struct panel_info *panel_data)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
/*
	int len = 0;

	len = strlen(panel_data->fw_name);
	if ((len > 3) && (panel_data->fw_name[len - 3] == 'i') && \
			(panel_data->fw_name[len - 2] == 'm')
			&& (panel_data->fw_name[len - 1] == 'g')) {
	panel_data->fw_name[len - 3] = 'b';
	panel_data->fw_name[len - 2] = 'i';
	panel_data->fw_name[len - 1] = 'n';
	}
*/
	chip_info->tp_type = panel_data->tp_type;
	TPD_INFO("chip_info->tp_type = %d, panel_data->fw_name = %s\n",
		 chip_info->tp_type, panel_data->fw_name);

	return 0;
}

static int nvt_reset_gpio_control(void *chip_data, bool enable)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
		TPD_INFO("%s: set reset state %d\n", __func__, enable);
		gpio_set_value(chip_info->hw_res->reset_gpio, enable);
	}

	return 0;
}

static unsigned int nvt_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int32_t ret = -1;
	uint32_t irq_reason = IRQ_IGNORE;

	memset(chip_info->point_data, 0, POINT_DATA_LEN);
	ret = CTP_SPI_READ(chip_info->s_client, chip_info->point_data, POINT_DATA_LEN + 1);
	if (ret < 0) {
		TPD_INFO("CTP_SPI_READ failed.(%d)\n", ret);
		return IRQ_IGNORE;
	}

	/*some kind of protect mechanism, after WDT firware redownload and try to save tp*/
	ret = nvt_wdt_fw_recovery(chip_info, chip_info->point_data);
	if (ret) {
		if ((gesture_enable == 1) && (is_suspended == 1)) {
			/* auto go back to wakeup gesture mode */
			TPD_INFO("Recover for fw reset %02X\n", chip_info->point_data[1]);
			nvt_reset(chip_info);
			ret = nvt_cmd_store(chip_info, 0x13);
			return IRQ_IGNORE;
		}
		TPD_INFO("Recover for fw reset %02X, IRQ_EXCEPTION\n", chip_info->point_data[1]);
		return IRQ_EXCEPTION;
	}

	if (nvt_fw_recovery(chip_info->point_data)) {  /* receive 0x77 */
		nvt_esd_check_enable(chip_info, true);
		return IRQ_IGNORE;
	}

	ret = nvt_ts_point_data_checksum(chip_info->point_data, POINT_DATA_CHECKSUM_LEN);
	if (ret) {
		irq_reason = irq_reason | IRQ_IGNORE;
	} else {
		irq_reason = irq_reason | IRQ_TOUCH;
	}

	if (chip_info->ts->pen_support) {
		ret = nvt_ts_pen_data_checksum(chip_info->point_data + 66, PEN_DATA_LEN);
		if (ret) {
			irq_reason = irq_reason | IRQ_IGNORE;
		} else {
			irq_reason = irq_reason | IRQ_PEN;
		}
	}

	if ((gesture_enable == 1) && (is_suspended == 1)) {
		if (CHK_BIT(irq_reason, IRQ_TOUCH)) {
			return IRQ_GESTURE;
		}
	} else if (is_suspended == 1) {
		return IRQ_IGNORE;
	}

	return irq_reason;
}

#ifdef CONFIG_OPLUS_TP_APK
static void nova_write_log_buf(struct chip_data_nt36523 *chip_info, u8 main_id,
				   u8 sec_id)
{
	log_buf_write(chip_info->ts, main_id);
	sec_id = sec_id | 0x80;
	log_buf_write(chip_info->ts, sec_id);
}


static void nova_debug_info(struct chip_data_nt36523 *chip_info, u8 *buf)
{
	static struct nvt_fw_debug_info debug_last;

	chip_info->nvt_fw_debug_info.rek_info = (uint8_t)(buf[0] >> 4) & 0x07;
	chip_info->nvt_fw_debug_info.rst_info = (uint8_t)(buf[0]) & 0x07;

	chip_info->nvt_fw_debug_info.esd	  = (uint8_t)(buf[1] >> 5) & 0x01;
	chip_info->nvt_fw_debug_info.palm	 = (uint8_t)(buf[1] >> 4) & 0x01;
	chip_info->nvt_fw_debug_info.bending  = (uint8_t)(buf[1] >> 3) & 0x01;
	chip_info->nvt_fw_debug_info.water	= (uint8_t)(buf[1] >> 2) & 0x01;
	chip_info->nvt_fw_debug_info.gnd	  = (uint8_t)(buf[1] >> 1) & 0x01;
	chip_info->nvt_fw_debug_info.er	   = (uint8_t)(buf[1]) & 0x01;

	chip_info->nvt_fw_debug_info.hopping  = (uint8_t)(buf[2] >> 4) & 0x0F;
	chip_info->nvt_fw_debug_info.fog	  = (uint8_t)(buf[2] >> 2) & 0x01;
	chip_info->nvt_fw_debug_info.film	 = (uint8_t)(buf[2] >> 1) & 0x01;
	chip_info->nvt_fw_debug_info.notch	= (uint8_t)(buf[2]) & 0x01;

	if (debug_last.rek_info != chip_info->nvt_fw_debug_info.rek_info) {
		nova_write_log_buf(chip_info, 1, chip_info->nvt_fw_debug_info.rek_info);
	}

	if (debug_last.rst_info != chip_info->nvt_fw_debug_info.rst_info) {
		nova_write_log_buf(chip_info, 2, chip_info->nvt_fw_debug_info.rst_info);
	}

	if (debug_last.esd != chip_info->nvt_fw_debug_info.esd) {
		log_buf_write(chip_info->ts, 3 + chip_info->nvt_fw_debug_info.esd);
	}

	if (debug_last.palm != chip_info->nvt_fw_debug_info.palm) {
		log_buf_write(chip_info->ts, 5 + chip_info->nvt_fw_debug_info.palm);
	}

	if (debug_last.bending != chip_info->nvt_fw_debug_info.bending) {
		log_buf_write(chip_info->ts, 7 + chip_info->nvt_fw_debug_info.bending);
	}

	if (debug_last.water != chip_info->nvt_fw_debug_info.water) {
		log_buf_write(chip_info->ts, 9 + chip_info->nvt_fw_debug_info.water);
	}

	if (debug_last.gnd != chip_info->nvt_fw_debug_info.gnd) {
		log_buf_write(chip_info->ts, 11 + chip_info->nvt_fw_debug_info.gnd);
	}

	if (debug_last.er != chip_info->nvt_fw_debug_info.er) {
		log_buf_write(chip_info->ts, 13 + chip_info->nvt_fw_debug_info.er);
	}

	if (debug_last.hopping != chip_info->nvt_fw_debug_info.hopping) {
		nova_write_log_buf(chip_info, 15, chip_info->nvt_fw_debug_info.hopping);
	}

	if (debug_last.fog != chip_info->nvt_fw_debug_info.fog) {
		log_buf_write(chip_info->ts, 17 + chip_info->nvt_fw_debug_info.fog);
	}

	if (debug_last.film != chip_info->nvt_fw_debug_info.film) {
		log_buf_write(chip_info->ts, 19 + chip_info->nvt_fw_debug_info.film);
	}

	if (debug_last.notch != chip_info->nvt_fw_debug_info.notch) {
		log_buf_write(chip_info->ts, 21 + chip_info->nvt_fw_debug_info.notch);
	}

	memcpy(&debug_last, &chip_info->nvt_fw_debug_info, sizeof(debug_last));

	/*msleep(2000);*/
	if (tp_debug > 0) {
		pr_err("REK_INFO:0x%02X, RST_INFO:0x%02X\n",
			   chip_info->nvt_fw_debug_info.rek_info,
			   chip_info->nvt_fw_debug_info.rst_info);

		pr_err("ESD:0x%02X, PALM:0x%02X, BENDING:0x%02X, WATER:0x%02X, GND:0x%02X, ER:0x%02X\n",
			   chip_info->nvt_fw_debug_info.esd,
			   chip_info->nvt_fw_debug_info.palm,
			   chip_info->nvt_fw_debug_info.bending,
			   chip_info->nvt_fw_debug_info.water,
			   chip_info->nvt_fw_debug_info.gnd,
			   chip_info->nvt_fw_debug_info.er);

		pr_err("HOPPING:0x%02X, FOG:0x%02X, FILM:0x%02X, NOTCH:0x%02X\n\n",
			   chip_info->nvt_fw_debug_info.hopping,
			   chip_info->nvt_fw_debug_info.fog,
			   chip_info->nvt_fw_debug_info.film,
			   chip_info->nvt_fw_debug_info.notch);
	}
}

#endif /* end of CONFIG_OPLUS_TP_APK*/

static int32_t nvt_ts_pen_data_checksum(uint8_t *buf, uint8_t length)
{
	uint8_t checksum = 0;
	int32_t i = 0;
	char str[128];

	/* Calculate checksum */
	for (i = 0; i < length - 1; i++) {
		checksum += buf[i];
	}
	checksum = (~checksum + 1);

	/* Compare ckecksum and dump fail data */
	if (checksum != buf[length - 1]) {
		TPD_INFO("pen packet checksum not match. (buf[%d]=0x%02X, checksum=0x%02X)\n",
			length - 1, buf[length - 1], checksum);
		/* --- dump pen buf --- */
		for (i = 0; i < length; i++) {
			sprintf(str + i*3, "%02X ", buf[i]);
		}
		TPD_INFO("%s\n", str);
		return -1;
	}

	return 0;
}

static int32_t nvt_ts_point_data_checksum(uint8_t *buf, uint8_t length)
{
	uint8_t checksum = 0;
	int32_t i = 0;
	char str[128];

	/* Generate checksum*/
	for (i = 0; i < length - 1; i++) {
		checksum += buf[i + 1];
	}

	checksum = (~checksum + 1);

	/* Compare ckecksum and dump fail data*/
	if (checksum != buf[length]) {
		TPD_INFO("i2c/spi packet checksum not match. (point_data[%d]=0x%02X, checksum=0x%02X)\n",
			 length, buf[length], checksum);

		for (i = 0; i < 5; i++) {
			snprintf(str, sizeof(str), "%02X %02X %02X %02X %02X %02X    %02X %02X %02X %02X %02X %02X\n",
					buf[1+i*12], buf[2+i*12], buf[3+i*12], buf[4+i*12], buf[5+i*12], buf[6+i*12],
					buf[7+i*12], buf[8+i*12], buf[9+i*12], buf[10+i*12], buf[11+i*12], buf[12+i*12]);
			TPD_INFO("%s", str);
		}

		for (i = 0; i < (length - 60); i++) {
			sprintf(str + i*3, "%02X ", buf[1 + 60 + i]);
		}
		TPD_INFO("%s\n", str);

		return -1;
	}

	return 0;
}


static int nvt_get_touch_points_high_reso(void *chip_data, struct point_info *points, int max_num)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int obj_attention = 0;
	int i = 0;
	uint32_t position = 0;
	uint32_t input_x = 0;
	uint32_t input_y = 0;
	uint32_t input_w = 0;
	uint32_t input_p = 0;
	uint8_t pointid = 0;
	uint8_t *point_data = chip_info->point_data;
	bool is_finger_hold = false;
	/*uint32_t tmp_data = 0;
	struct resolution_info *res_info = &chip_info->ts->resolution_info;*/

	for(i = 0; i < max_num; i++) {
		position = 1 + 6 * i;
		pointid = (uint8_t)(point_data[position + 0] >> 3) - 1;
		if (pointid >= max_num) {
			if ((point_data[position] & 0x07) == STATUS_FINGER_PALM) {
				if (chip_info->ts)
					chip_info->ts->pen_mode_tp_state = CANCLE_TP;
			}
			continue;
		}

		if (((point_data[position] & 0x07) == STATUS_FINGER_ENTER) ||
				((point_data[position] & 0x07) == STATUS_FINGER_MOVING)) {
			chip_info->irq_timer = jiffies;	/*reset esd check trigger base time*/

			input_x = (uint32_t)(point_data[position + 1] << 8) + (uint32_t) (point_data[position + 2]);
			input_y = (uint32_t)(point_data[position + 3] << 8) + (uint32_t) (point_data[position + 4]);

			/*switch (chip_info->ts->orientation) {
			case 90: {
				 	tmp_data = input_x;
					input_x = max(res_info->max_x, input_y);
					input_y = max(res_info->max_y, tmp_data);
				}
			break;
			case 180: {
					input_x = res_info->max_x - input_x - 1;
					input_y = res_info->max_y - input_y - 1;
				}
			break;
			case 270: {
				 	tmp_data = input_x;
					input_x = res_info->max_x - max(res_info->max_x - 1, input_y) - 1;
					input_y = res_info->max_x - max(res_info->max_y - 1, tmp_data) - 1;
				}
			break;
			default :break;
			}*/

			input_w = (uint32_t)(point_data[position + 5]);
			if (input_w == 0) {
				input_w = 1;
			}

			input_p = input_w;

			obj_attention = obj_attention | (1 << pointid);
			points[pointid].x = input_x;
			points[pointid].y = input_y;
			points[pointid].z = input_p;
			points[pointid].width_major = input_w;
			points[pointid].touch_major = input_w;
			points[pointid].status = 1;
		} else if ((point_data[position] & 0x07) == STATUS_FINGER_HOLD) {
			is_finger_hold = true;
		}
	}
	/*no valid point and finger hold state, we should report cancel */
	if ((0 == obj_attention) && is_finger_hold && chip_info->ts) {
		chip_info->ts->pen_mode_tp_state = CANCLE_TP;
	}

#ifdef CONFIG_OPLUS_TP_APK
	if (chip_info->debug_mode_sta) {
		nova_debug_info(chip_info, &point_data[109]);
	}
#endif /* end of CONFIG_OPLUS_TP_APK*/
	return obj_attention;
}

static int nvt_get_touch_points(void *chip_data, struct point_info *points,
				int max_num)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int obj_attention = 0;
	int i = 0;
	uint32_t position = 0;
	uint32_t input_x = 0;
	uint32_t input_y = 0;
	uint32_t input_w = 0;
	uint32_t input_p = 0;
	uint8_t pointid = 0;
	uint8_t *point_data = chip_info->point_data;
	/*uint32_t tmp_data = 0;
	struct resolution_info *res_info = &chip_info->ts->resolution_info;*/

	for (i = 0; i < max_num; i++) {
		position = 1 + 6 * i;
		pointid = (uint8_t)(point_data[position + 0] >> 3) - 1;

		if (pointid >= max_num) {
			continue;
		}

		if (((point_data[position] & 0x07) == STATUS_FINGER_ENTER) ||
				((point_data[position] & 0x07) == STATUS_FINGER_MOVING)) {
			chip_info->irq_timer = jiffies;	/*reset esd check trigger base time*/

			input_x = (uint32_t)(point_data[position + 1] << 4) +
					(uint32_t) (point_data[position + 3] >> 4);
			input_y = (uint32_t)(point_data[position + 2] << 4) +
					(uint32_t) (point_data[position + 3] & 0x0F);

			/*switch (chip_info->ts->orientation) {
			case 90: {
				 	tmp_data = input_x;
					input_x = max(res_info->max_x, input_y);
					input_y = max(res_info->max_y, tmp_data);
				}
			break;
			case 180: {
					input_x = res_info->max_x - input_x - 1;
					input_y = res_info->max_y - input_y - 1;
				}
			break;
			case 270: {
				 	tmp_data = input_x;
					input_x = res_info->max_x - max(res_info->max_x - 1, input_y) - 1;
					input_y = res_info->max_x - max(res_info->max_y - 1, tmp_data) - 1;
				}
			break;
			default :break;
			}*/

			input_w = (uint32_t)(point_data[position + 4]);
			if (input_w == 0) {
				input_w = 1;
			}

			if (i < 2) {
				input_p = (uint32_t)(point_data[position + 5]) + (uint32_t)(
						  point_data[i + 63] << 8);

				if (input_p > 1000) {
					input_p = 1000;
				}

			} else {
				input_p = (uint32_t)(point_data[position + 5]);
			}

			if (input_p == 0) {
				input_p = 1;
			}

			obj_attention = obj_attention | (1 << pointid);
			points[pointid].x = input_x;
			points[pointid].y = input_y;
			points[pointid].z = input_p;
			points[pointid].width_major = input_w;
			points[pointid].touch_major = input_w;
			points[pointid].status = 1;
		}
	}

#ifdef CONFIG_OPLUS_TP_APK

	if (chip_info->debug_mode_sta) {
		nova_debug_info(chip_info, &point_data[109]);
	}

#endif /* end of CONFIG_OPLUS_TP_APK*/
	return obj_attention;
}

static void nvt_get_pen_points(void *chip_data, struct pen_info *points)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	uint8_t pen_format_id = 0;
	uint8_t *point_data = chip_info->point_data;

	/* parse and handle pen report */
	pen_format_id = point_data[66];
	if (pen_format_id != 0xFF) {
		if (pen_format_id == 0x01) {
			/* report pen data */
			points->x = (uint32_t)(point_data[67] << 8) + (uint32_t)(point_data[68]);
			points->y = (uint32_t)(point_data[69] << 8) + (uint32_t)(point_data[70]);
			points->z = (uint32_t)(point_data[71] << 8) + (uint32_t)(point_data[72]);
			points->tilt_x = (int8_t)point_data[73];
			points->tilt_y = (int8_t)point_data[74];
			points->d = (uint32_t)(point_data[75] << 8) + (uint32_t)(point_data[76]);
			points->btn1 = (uint32_t)(point_data[77] & 0x01);
			points->btn2 = (uint32_t)((point_data[77] >> 1) & 0x01);
			points->battery = (uint32_t)point_data[78];
			points->status = 1;
		} else if (pen_format_id == 0xF0) {
			/* report Pen ID */
		} else {
			TPD_INFO("Unknown pen format id!\n");
		}
	}
	return;
}

static int8_t nvt_extend_cmd_store(struct chip_data_nt36523 *chip_info,
				   uint8_t u8Cmd, uint8_t u8SubCmd)
{
	int i, retry = 5;
	uint8_t buf[4] = {0};

	/*---set xdata index to EVENT BUF ADDR---(set page)*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	for (i = 0; i < retry; i++) {
		if (buf[1] != u8Cmd) {
			/*---set cmd status---*/
			buf[0] = EVENT_MAP_HOST_CMD;
			buf[1] = u8Cmd;
			buf[2] = u8SubCmd;
			CTP_SPI_WRITE(chip_info->s_client, buf, 3);
		}
		msleep(20);

		/*---read cmd status---*/
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, 3);

		if (buf[1] == 0x00) {
			break;
		}
	}

	if (unlikely(i == retry)) {
		TPD_INFO("send Cmd 0x%02X 0x%02X failed, buf[1]=0x%02X\n", u8Cmd, u8SubCmd,
			 buf[1]);
		return -1;
	} else {
		if (u8SubCmd == 0x16 || u8SubCmd == 0x17) {
			TPD_SPECIFIC_PRINT(chip_info->log_count,
			 "send Cmd 0x%02X 0x%02X success, tried %d times\n", u8Cmd, u8SubCmd, i);
		} else {
			TPD_INFO("send Cmd 0x%02X 0x%02X success, tried %d times\n", u8Cmd, u8SubCmd,
			 i);
		}
	}
	return 0;
}

static int8_t nvt_cmd_store(struct chip_data_nt36523 *chip_info, uint8_t u8Cmd)
{
	int i, retry = 5;
	uint8_t buf[3] = {0};

	/*---set xdata index to EVENT BUF ADDR---(set page)*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	for (i = 0; i < retry; i++) {
		if (buf[1] != u8Cmd) {
			/*---set cmd status---*/
			buf[0] = EVENT_MAP_HOST_CMD;
			buf[1] = u8Cmd;
			CTP_SPI_WRITE(chip_info->s_client, buf, 2);
		}

		msleep(20);

		/*---read cmd status---*/
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0x00) {
			break;
		}
	}

	if (unlikely(i == retry)) {
		TPD_INFO("send Cmd 0x%02X failed, buf[1]=0x%02X\n", u8Cmd, buf[1]);
		return -1;

	} else {
		TPD_INFO("send Cmd 0x%02X success, tried %d times\n", u8Cmd, i);
	}

	return 0;
}



static void nvt_ts_wakeup_gesture_coordinate(uint8_t *data, uint8_t max_num)
{
	uint32_t position = 0;
	uint32_t input_x = 0;
	uint32_t input_y = 0;
	int32_t i = 0;
	uint8_t input_id = 0;

	for (i = 0; i < max_num; i++) {
		position = 1 + 6 * i;
		input_id = (uint8_t)(data[position + 0] >> 3);

		if ((input_id == 0) || (input_id > max_num)) {
			continue;
		}

		if (((data[position] & 0x07) == 0x01) || ((data[position] & 0x07) == 0x02)) {
			input_x = (uint32_t)(data[position + 1] << 4) + (uint32_t)(
					  data[position + 3] >> 4);
			input_y = (uint32_t)(data[position + 2] << 4) + (uint32_t)(
					  data[position + 3] & 0x0F);
		}

		TPD_INFO("(%d: %d, %d)\n", i, input_x, input_y);
	}
}

#ifdef CONFIG_OPLUS_TP_APK

#define LEN_TEMP_GESTURE_BUF 2048
static void nvt_read_debug_gesture_coordinate_buffer(
							struct chip_data_nt36523 *chip_info,
							uint32_t xdata_addr, u8 *xdata, int32_t xdata_len)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	uint8_t buf[SPI_TANSFER_LENGTH + 2] = {0};
	uint32_t head_addr = 0;
	int32_t dummy_len = 0;
	/*int32_t data_len = 128;*/	/* max buffer size 1024 */
	int32_t residual_len = 0;
	uint8_t *xdata_tmp = NULL;

	/*---set xdata sector xdata_addr & length---*/
	head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
	dummy_len = xdata_addr - head_addr;
	residual_len = (head_addr + dummy_len + xdata_len) % XDATA_SECTOR_SIZE;


	/*malloc buffer space*/
	xdata_tmp = tp_devm_kzalloc(&chip_info->s_client->dev, LEN_TEMP_GESTURE_BUF,
					GFP_KERNEL);

	if (xdata_tmp == NULL) {
		TPD_INFO("%s malloc memory failed\n", __func__);
		return;
	}

	/*read xdata : step 1*/
	for (i = 0; i < ((dummy_len + xdata_len) / XDATA_SECTOR_SIZE); i++) {
		/*---read xdata by SPI_TANSFER_LENGTH*/

		for (j = 0; j < (XDATA_SECTOR_SIZE / SPI_TANSFER_LENGTH); j++) {
			/*---change xdata index---*/
			nvt_set_page(chip_info, head_addr + (XDATA_SECTOR_SIZE * i) +
					 (SPI_TANSFER_LENGTH * j));

			/*---read data---*/
			buf[0] = SPI_TANSFER_LENGTH * j;
			CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

			/*---copy buf to xdata_tmp---*/
			for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
				xdata_tmp[XDATA_SECTOR_SIZE * i + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
				/*printk("0x%02X, 0x%04X\n", buf[k+1], (XDATA_SECTOR_SIZE*i + SPI_TANSFER_LENGTH*j + k));*/
			}
		}

		/*printk("addr=0x%05X\n", (head_addr+XDATA_SECTOR_SIZE*i));*/
	}

	/*read xdata : step2*/
	if (residual_len != 0) {
		/*---read xdata by SPI_TANSFER_LENGTH*/

		for (j = 0; j < (residual_len / SPI_TANSFER_LENGTH + 1); j++) {
			/*---change xdata index---*/

			nvt_set_page(chip_info, xdata_addr + xdata_len - residual_len +
					 (SPI_TANSFER_LENGTH * j));

			/*---read data---*/
			buf[0] = SPI_TANSFER_LENGTH * j;
			CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

			/*---copy buf to xdata_tmp---*/
			for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
				xdata_tmp[(dummy_len + xdata_len - residual_len) + SPI_TANSFER_LENGTH * j + k] =
					buf[k + 1];
				/*printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + SPI_TANSFER_LENGTH*j + k));*/
			}
		}

		/*printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len));*/
	}

	/*---remove dummy data---*/
	pr_cont("nova read gesture xdata\n");

	for (i = 0; i < xdata_len; i++) {
		xdata[i] = xdata_tmp[dummy_len + i];
		pr_cont("0x%02X,", xdata[i]);
	}

	pr_cont("\n");
	/*---set xdata index to EVENT BUF ADDR---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&xdata_tmp,
			  LEN_TEMP_GESTURE_BUF);
}

static void nvt_dbg_gesture_record_coor_read(
				struct chip_data_nt36523 *chip_info, u8 pointdata)
{
	u8 *xdata = NULL;
	uint32_t buf_len = 0;
	uint8_t points_num[2] = {0};
	uint8_t data_len[2] = {0};

	buf_len = 512;
	xdata = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!xdata) {
		TPD_INFO("%s, malloc memory failed\n", __func__);
		return;
	}

	nvt_read_debug_gesture_coordinate_buffer(chip_info,
			NVT_MMAP_DEBUG_FINGER_DOWN_DIFFDATA,
			xdata, buf_len);
	points_num[0] = xdata[0];
	data_len[0] = 3 * points_num[0];
	memcpy(&chip_info->ts->gesture_buf[2], &xdata[1],
		   data_len[0] * sizeof(int32_t));

	nvt_read_debug_gesture_coordinate_buffer(chip_info,
			NVT_MMAP_DEBUG_STATUS_CHANGE_DIFFDATA,
			xdata, buf_len);
	points_num[1] = xdata[0];
	data_len[1] = 3 * points_num[1];
	memcpy(&chip_info->ts->gesture_buf[2 + data_len[0]], &xdata[1],
		   data_len[1] * sizeof(int32_t));

	chip_info->ts->gesture_buf[0] = pointdata;
	chip_info->ts->gesture_buf[1] = points_num[0] + points_num[1];

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&xdata, buf_len);
}
#endif /* end of CONFIG_OPLUS_TP_APK*/

static int nvt_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
	uint8_t gesture_id = 0;
	uint8_t func_type = 0;
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	uint8_t *point_data = chip_info->point_data;
	uint8_t max_num = 10;	/*TODO: define max_num by oplus common driver*/

	gesture_id = (uint8_t)(point_data[1] >> 3);
	func_type = (uint8_t)point_data[2];

	if ((gesture_id == 30) && (func_type == 1)) {
		gesture_id = (uint8_t)point_data[3];

	} else if (gesture_id > 30) {
		TPD_INFO("invalid gesture id= %d, no gesture event\n", gesture_id);
		return 0;
	}

#ifdef CONFIG_OPLUS_TP_APK
	TPD_INFO("gesture id is %d,data[1] %d,data[2] %d,data[3] %d\n",
		 gesture_id, point_data[1], point_data[2], point_data[3]);

	if (chip_info->debug_gesture_sta) {
		if (chip_info->ts->gesture_buf) {
			nvt_dbg_gesture_record_coor_read(chip_info, gesture_id);
		}
	}

#endif /* end of CONFIG_OPLUS_TP_APK*/

	if ((gesture_id > 0) && (gesture_id <= max_num)) {
		nvt_ts_wakeup_gesture_coordinate(point_data, max_num);
		return 0;
	}

	switch (gesture_id) {   /*judge gesture type*/
	case RIGHT_SLIDE_DETECT :
		gesture->gesture_type  = LEFT2RIGHT_SWIP;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_end.x   = point_data[8] | point_data[9] << 8;
		gesture->Point_end.y   = point_data[10] | point_data[11] << 8;
		break;

	case LEFT_SLIDE_DETECT :
		gesture->gesture_type  = RIGHT2LEFT_SWIP;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_end.x   = point_data[8] | point_data[9] << 8;
		gesture->Point_end.y   = point_data[10] | point_data[11] << 8;
		break;

	case DOWN_SLIDE_DETECT  :
		gesture->gesture_type  = UP2DOWN_SWIP;
		gesture->Point_start.x = point_data[4]  | point_data[5] << 8;
		gesture->Point_start.y = point_data[6]  | point_data[7] << 8;
		gesture->Point_end.x   = point_data[8]  | point_data[9] << 8;
		gesture->Point_end.y   = point_data[10] | point_data[11] << 8;
		break;

	case UP_SLIDE_DETECT :
		gesture->gesture_type  = DOWN2UP_SWIP;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_end.x   = point_data[8] | point_data[9] << 8;
		gesture->Point_end.y   = point_data[10] | point_data[11] << 8;
		break;

	case DTAP_DETECT:
		gesture->gesture_type  = DOU_TAP;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_end	 = gesture->Point_start;
		break;

	case UP_VEE_DETECT :
		gesture->gesture_type  = UP_VEE;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_end.x   = point_data[12] | point_data[13] << 8;
		gesture->Point_end.y   = point_data[14] | point_data[15] << 8;
		gesture->Point_1st.x   = point_data[8] | point_data[9] << 8;
		gesture->Point_1st.y   = point_data[10] | point_data[11] << 8;
		break;

	case DOWN_VEE_DETECT :
		gesture->gesture_type  = DOWN_VEE;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_end.x   = point_data[12] | point_data[13] << 8;
		gesture->Point_end.y   = point_data[14] | point_data[15] << 8;
		gesture->Point_1st.x   = point_data[8] | point_data[9] << 8;
		gesture->Point_1st.y   = point_data[10] | point_data[11] << 8;
		break;

	case LEFT_VEE_DETECT:
		gesture->gesture_type = LEFT_VEE;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_end.x   = point_data[12] | point_data[13] << 8;
		gesture->Point_end.y   = point_data[14] | point_data[15] << 8;
		gesture->Point_1st.x   = point_data[8] | point_data[9] << 8;
		gesture->Point_1st.y   = point_data[10] | point_data[11] << 8;
		break;

	case RIGHT_VEE_DETECT :
		gesture->gesture_type  = RIGHT_VEE;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_end.x   = point_data[12] | point_data[13] << 8;
		gesture->Point_end.y   = point_data[14] | point_data[15] << 8;
		gesture->Point_1st.x   = point_data[8] | point_data[9] << 8;
		gesture->Point_1st.y   = point_data[10] | point_data[11] << 8;
		break;

	case CIRCLE_DETECT  :
		gesture->gesture_type = CIRCLE_GESTURE;
		gesture->clockwise = (point_data[43] == 0x20) ? 1 : 0;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_1st.x   = point_data[8] | point_data[9] << 8;	/*ymin*/
		gesture->Point_1st.y   = point_data[10] | point_data[11] << 8;
		gesture->Point_2nd.x   = point_data[12] | point_data[13] << 8;  /*xmin*/
		gesture->Point_2nd.y   = point_data[14] | point_data[15] << 8;
		gesture->Point_3rd.x   = point_data[16] | point_data[17] << 8;  /*ymax*/
		gesture->Point_3rd.y   = point_data[18] | point_data[19] << 8;
		gesture->Point_4th.x   = point_data[20] | point_data[21] << 8;  /*xmax*/
		gesture->Point_4th.y   = point_data[22] | point_data[23] << 8;
		gesture->Point_end.x   = point_data[24] | point_data[25] << 8;
		gesture->Point_end.y   = point_data[26] | point_data[27] << 8;
		break;

	case DOUSWIP_DETECT  :
		gesture->gesture_type  = DOU_SWIP;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_end.x   = point_data[12] | point_data[13] << 8;
		gesture->Point_end.y   = point_data[14] | point_data[15] << 8;
		gesture->Point_1st.x   = point_data[8] | point_data[9] << 8;
		gesture->Point_1st.y   = point_data[10] | point_data[11] << 8;
		gesture->Point_2nd.x   = point_data[16] | point_data[17] << 8;
		gesture->Point_2nd.y   = point_data[18] | point_data[19] << 8;
		break;

	case M_DETECT  :
		gesture->gesture_type  = M_GESTRUE;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_1st.x   = point_data[8] | point_data[9] << 8;
		gesture->Point_1st.y   = point_data[10] | point_data[11] << 8;
		gesture->Point_2nd.x   = point_data[12] | point_data[13] << 8;
		gesture->Point_2nd.y   = point_data[14] | point_data[15] << 8;
		gesture->Point_3rd.x   = point_data[16] | point_data[17] << 8;
		gesture->Point_3rd.y   = point_data[18] | point_data[19] << 8;
		gesture->Point_end.x   = point_data[20] | point_data[21] << 8;
		gesture->Point_end.y   = point_data[22] | point_data[23] << 8;
		break;

	case W_DETECT :
		gesture->gesture_type  = W_GESTURE;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_1st.x   = point_data[8] | point_data[9] << 8;
		gesture->Point_1st.y   = point_data[10] | point_data[11] << 8;
		gesture->Point_2nd.x   = point_data[12] | point_data[13] << 8;
		gesture->Point_2nd.y   = point_data[14] | point_data[15] << 8;
		gesture->Point_3rd.x   = point_data[16] | point_data[17] << 8;
		gesture->Point_3rd.y   = point_data[18] | point_data[19] << 8;
		gesture->Point_end.x   = point_data[20] | point_data[21] << 8;
		gesture->Point_end.y   = point_data[22] | point_data[23] << 8;
		break;

	case S_DETECT:
		gesture->gesture_type  = S_GESTURE;
		gesture->Point_start.x = point_data[4] | point_data[5] << 8;
		gesture->Point_start.y = point_data[6] | point_data[7] << 8;
		gesture->Point_end.x   = point_data[8] | point_data[9] << 8;
		gesture->Point_end.y   = point_data[10] | point_data[11] << 8;
		gesture->Point_1st.x   = point_data[12] | point_data[13] << 8;
		gesture->Point_1st.y   = point_data[14] | point_data[15] << 8;
		gesture->Point_2nd.x   = point_data[16] | point_data[17] << 8;
		gesture->Point_2nd.y   = point_data[18] | point_data[19] << 8;
		gesture->Point_3rd.x   = point_data[20] | point_data[21] << 8;
		gesture->Point_3rd.y   = point_data[22] | point_data[23] << 8;
		break;

	case PEN_DETECT:
		gesture->gesture_type  = PENDETECT;
		break;

	default:
		gesture->gesture_type = UNKOWN_GESTURE;
		break;
	}

	TPD_INFO("%s, gesture_id: 0x%x, func_type: 0x%x, gesture_type: %d, clockwise: %d, points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n",
		__func__, gesture_id, func_type, gesture->gesture_type, gesture->clockwise,
		gesture->Point_start.x, gesture->Point_start.y,
		gesture->Point_end.x, gesture->Point_end.y,
		gesture->Point_1st.x, gesture->Point_1st.y,
		gesture->Point_2nd.x, gesture->Point_2nd.y,
		gesture->Point_3rd.x, gesture->Point_3rd.y,
		gesture->Point_4th.x, gesture->Point_4th.y);

	return 0;
}

static int nvt_reset(void *chip_data)
{
	int ret = -1;
	int val = 0;
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	struct touchpanel_data *ts = spi_get_drvdata(chip_info->s_client);
	/* const struct firmware *fw = NULL; */

	TPD_INFO("%s.\n", __func__);

	if(ts->in_test_process) {
		TPD_INFO("%s. TP in test mode, do not update firmware\n", __func__);
		return 0;
	}

	mutex_lock(&chip_info->mutex_testing);

	if(!ts->fw_update_app_support || chip_info->probe_done) {

		ret = nvt_fw_update(chip_info, NULL, 0);
		if(FW_NO_NEED_UPDATE == ret) {
			TPD_INFO("g_fw_buf update no need!\n");
		} else if (FW_UPDATE_ERROR == ret) {
			val = -1;
			TPD_INFO("g_fw_buf update failed!\n");
		}
	}
	/*
	if(chip_info->g_fw != NULL) {
	release_firmware(chip_info->g_fw);
	}
	*/
#ifdef CONFIG_OPLUS_TP_APK
	if(chip_info->debug_mode_sta) {
	if(ts->apk_op && ts->apk_op->apk_debug_set) {
		ts->apk_op->apk_debug_set(ts->chip_data, true);
		}
	}
#endif /* end of CONFIG_OPLUS_TP_APK*/

	chip_info->is_sleep_writed = false;
	mutex_unlock(&chip_info->mutex_testing);
	return val;
}

#ifdef CONFIG_OPLUS_TP_APK
static __maybe_unused int nvt_enable_debug_gesture_coordinate_record_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
	int8_t ret = -1;

	TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
		 enable, chip_info->is_sleep_writed);

	if (enable) {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_WKG_COORD_RECORD_ON);

	} else {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_WKG_COORD_RECORD_OFF);
	}

	return ret;
}

static __maybe_unused int nvt_enable_debug_gesture_coordinate_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
	int8_t ret = -1;

	TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
		 enable, chip_info->is_sleep_writed);

	if (enable) {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_WKG_COORD_ON);

	} else {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_DBG_WKG_COORD_OFF);
	}

	return ret;
}

#endif /* end of CONFIG_OPLUS_TP_APK*/

static int nvt_enable_pen_mode(struct chip_data_nt36523 *chip_info, bool enable);
static int nvt_enable_black_gesture(struct chip_data_nt36523 *chip_info, bool enable)
{
	int ret = -1;

	TPD_INFO("%s, enable = %d, chip_info->is_sleep_writed = %d\n", __func__, enable,
		 chip_info->is_sleep_writed);

	if (chip_info->is_sleep_writed) {
		chip_info->need_judge_irq_throw = true;
		nvt_reset(chip_info);
		chip_info->need_judge_irq_throw = false;
	}

	if (enable) {
#ifdef CONFIG_OPLUS_TP_APK

		if (chip_info->debug_gesture_sta) {
			nvt_enable_debug_gesture_coordinate_record_mode(chip_info, true);
		}

#endif /* end of CONFIG_OPLUS_TP_APK*/

		if ((chip_info->gesture_state & (1 << PENDETECT)) && *chip_info->is_pen_connected && !(*chip_info->is_pen_attracted)) {
			ret = nvt_enable_pen_mode(chip_info, true);
		} else {
			ret = nvt_enable_pen_mode(chip_info, false);
		}

		ret |= nvt_cmd_store(chip_info, CMD_OPEN_BLACK_GESTURE);
		msleep(50);
		TPD_INFO("%s: enable gesture %s !\n", __func__, (ret < 0) ? "failed" : "success");
	} else {
		ret = 0;
	}

	return ret;
}

#define NUM_OF_MONITOR_FRAMES 40
static int nvt_enable_edge_limit(struct chip_data_nt36523 *chip_info,
				int state)
{
	int8_t ret = -1;

	TPD_INFO("%s:state = %d, level = %d, chip_info->is_sleep_writed = %d\n",
		 __func__, state, NUM_OF_MONITOR_FRAMES, chip_info->is_sleep_writed);

	switch (state) {
	case VERTICAL_SCREEN:
		ret = nvt_extend_cmd_store(chip_info,
			EVENTBUFFER_EDGE_LIMIT_VERTICAL, NUM_OF_MONITOR_FRAMES);
		break;
	case LANDSCAPE_SCREEN_90:
		ret = nvt_extend_cmd_store(chip_info,
			EVENTBUFFER_EDGE_LIMIT_RIGHT_UP, NUM_OF_MONITOR_FRAMES);
		break;
	case LANDSCAPE_SCREEN_270:
		ret = nvt_extend_cmd_store(chip_info,
			EVENTBUFFER_EDGE_LIMIT_LEFT_UP, NUM_OF_MONITOR_FRAMES);
		break;
	}

	return ret;
}

static int nvt_enable_charge_mode(struct chip_data_nt36523 *chip_info,
				bool enable)
{
	int8_t ret = -1;

	TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__, enable,
		 chip_info->is_sleep_writed);

	if (enable) {
		ret = nvt_cmd_store(chip_info, EVENTBUFFER_PWR_PLUG_IN);

	} else {
		ret = nvt_cmd_store(chip_info, EVENTBUFFER_PWR_PLUG_OUT);
	}

	return ret;
}

static int nvt_enable_game_mode(struct chip_data_nt36523 *chip_info,
				bool enable)
{
	int8_t ret = -1;

	TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__, enable,
		 chip_info->is_sleep_writed);

	if (enable) {
		ret = nvt_cmd_store(chip_info, EVENTBUFFER_GAME_ON);
	} else {
		ret = nvt_cmd_store(chip_info, EVENTBUFFER_GAME_OFF);
	}

	return ret;
}

static int nvt_enable_headset_mode(struct chip_data_nt36523 *chip_info,
				bool enable)
{
	int8_t ret = -1;

	TPD_DEBUG("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
		  enable, chip_info->is_sleep_writed);

	if (enable) {
		ret = nvt_cmd_store(chip_info, EVENTBUFFER_HS_PLUG_IN);
		TPD_INFO("%s: EVENTBUFFER_HS_PLUG_IN\n", __func__);

	} else {
		ret = nvt_cmd_store(chip_info, EVENTBUFFER_HS_PLUG_OUT);
	}

	return ret;
}

static void nvt_notify_pencil_type(void *chip_data, uint8_t value)
{
	int i;
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	if (!chip_info) {
		return;
	}

	chip_info->current_pencil_type = value;

	if (chip_info->pen_id_map_num) {
		for (i = 0; i < chip_info->pen_id_map_num; i++) {
			if (chip_info->pen_id_map_array[i].src_id == value) {
				chip_info->current_pencil_type = chip_info->pen_id_map_array[i].dst_id;
				break;
			}
		}
	}

	TPD_INFO("value = %d, set pencil type to %d.\n", value, chip_info->current_pencil_type);

	return;
}

static int nvt_enable_pen_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
	int8_t ret = -1;

	if (enable) {
		switch (chip_info->current_pencil_type) {
		case TYPE_PENCIL_HAVON:
			ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_PEN_MODE_ON);
			break;
		case TYPE_PENCIL_MAXEYE:
			ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_PEN_MODE_2ND_ON);
			break;
		case TYPE_PENCIL_MAXEYE_2ND:
			ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_PEN_MODE_3RD_ON);
			break;
		case TYPE_PENCIL_SUNWODA:
			ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_PEN_MODE_4TH_ON);
			break;
		case TYPE_PENCIL_MAXEYE_3RD:
			ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_PEN_MODE_5TH_ON);
			break;
		default:
			ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_PEN_MODE_ON);
			TPD_INFO("error pencil type not defined !!!\n");
			break;
		}
	} else {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_PEN_MODE_OFF);
	}

	return ret;
}

static int nvt_enable_pen_vibrator(struct chip_data_nt36523 *chip_info, int ctl_cmd)
{
	int8_t ret = 0;


	chip_info->pen_ctl_para = ctl_cmd;

	if(ctl_cmd == 3) {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_PEN_VIBRATOR_ON);
	} else if (ctl_cmd == 2) {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD, EVENTBUFFER_EXT_PEN_VIBRATOR_OFF);
	} else {
		TPD_INFO("%s invaled cmd!!\n", __func__);
	}

	return ret;
}

static int nvt_pen_control(struct chip_data_nt36523 *chip_info, int ctl_cmd)
{
	if(ctl_cmd == PEN_CTL_FEEDBACK) {
		TPD_INFO("%s flag is feedback, return now control para\n", __func__);
		goto REPORT;
	}

	if(!ctl_cmd) {
		TPD_INFO("%s invaled data!!\n", __func__);
		goto ERR;
	}

	if(nvt_enable_pen_vibrator(chip_info, ctl_cmd)) {
		goto ERR;
	}

REPORT:
		return chip_info->pen_ctl_para;
ERR:
		return -1;
}

#ifdef CONFIG_OPLUS_TP_APK
static __maybe_unused int nvt_enable_hopping_polling_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
	int8_t ret = -1;

	TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
		 enable, chip_info->is_sleep_writed);

	nvt_esd_check_update_timer(chip_info);

	if (enable) {
		ret = nvt_cmd_store(chip_info, EVENTBUFFER_HOPPING_POLLING_ON);

	} else {
		ret = nvt_cmd_store(chip_info, EVENTBUFFER_HOPPING_POLLING_OFF);
	}

	return ret;
}

static __maybe_unused int nvt_enable_hopping_fix_freq_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
	int8_t ret = -1;

	TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
		 enable, chip_info->is_sleep_writed);

	nvt_esd_check_update_timer(chip_info);

	if (enable) {
		ret = nvt_cmd_store(chip_info, EVENTBUFFER_HOPPING_FIX_FREQ_ON);

	} else {
		ret = nvt_cmd_store(chip_info, EVENTBUFFER_HOPPING_FIX_FREQ_OFF);
	}

	return ret;
}

static __maybe_unused int nvt_enable_debug_msg_diff_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
	int8_t ret = -1;

	TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
		 enable, chip_info->is_sleep_writed);

	nvt_esd_check_update_timer(chip_info);

	if (enable) {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD,
					   EVENTBUFFER_EXT_DBG_MSG_DIFF_ON);

	} else {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD,
					   EVENTBUFFER_EXT_DBG_MSG_DIFF_OFF);
	}

	return ret;
}
#endif /* end of CONFIG_OPLUS_TP_APK*/

static int nvt_enable_water_polling_mode(struct chip_data_nt36523 *chip_info, bool enable)
{
	int8_t ret = -1;

	TPD_INFO("%s:enable = %d, chip_info->is_sleep_writed = %d\n", __func__,
		 enable, chip_info->is_sleep_writed);

	nvt_esd_check_update_timer(chip_info);

	if (enable) {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD,
					   EVENTBUFFER_EXT_DBG_WATER_POLLING_ON);
		if (!ret) {
			TPD_INFO("%s: send EVENTBUFFER_EXT_DBG_WATER_POLLING_ON\n", __func__);
		}
	} else {
		ret = nvt_extend_cmd_store(chip_info, EVENTBUFFER_EXT_CMD,
					   EVENTBUFFER_EXT_DBG_WATER_POLLING_OFF);
		if (!ret) {
			TPD_INFO("%s: send EVENTBUFFER_EXT_DBG_WATER_POLLING_OFF\n", __func__);
		}
	}

	return ret;
}

static int nvt_mode_switch(void *chip_data, work_mode mode, int flag)
{
	int ret = -1;
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	nvt_esd_check_update_timer(chip_info);

	switch (mode) {
	case MODE_NORMAL:
		ret = 0;
		break;

	case MODE_SLEEP:
		ret = nvt_enter_sleep(chip_info, true);

		if (ret < 0) {
			TPD_INFO("%s: nvt enter sleep failed\n", __func__);
		}

		nvt_esd_check_enable(chip_info, false);
		break;

	case MODE_GESTURE:
		ret = nvt_enable_black_gesture(chip_info, flag);

		if (ret < 0) {
			TPD_INFO("%s: nvt enable gesture failed.\n", __func__);
			return ret;
		}

		if (flag) {
			nvt_esd_check_enable(chip_info, false);
		}

		break;

	case MODE_CHARGE:
		ret = nvt_enable_charge_mode(chip_info, flag);

		if (ret < 0) {
			TPD_INFO("%s: enable charge mode : %d failed\n", __func__, flag);
		}

		break;

	case MODE_GAME:
		ret = nvt_enable_game_mode(chip_info, flag);

		if (ret < 0) {
			TPD_INFO("%s: enable game mode : %d failed\n", __func__, flag);
		}

		break;

	case MODE_HEADSET:
		ret = nvt_enable_headset_mode(chip_info, flag);

		if (ret < 0) {
			TPD_INFO("%s: enable headset mode : %d failed\n", __func__, flag);
		}

		break;

	case MODE_EDGE:
		ret = nvt_enable_edge_limit(chip_info, flag);

		if (ret < 0) {
			TPD_INFO("%s: limit switch: %d failed\n", __func__, flag);
		}

		break;

	case MODE_PEN_SCAN:
		ret = nvt_enable_pen_mode(chip_info, flag);

		if (ret < 0) {
			TPD_INFO("%s: enable pen mode : %d failed\n", __func__, flag);
		}

		break;

	case MODE_PEN_CTL:
		ret = nvt_pen_control(chip_info, flag);

		if (ret < 0) {
			TPD_INFO("%s: enable pencil control: %d failed\n", __func__, flag);
		}

		break;

	case MODE_WATERPROOF:
		ret = nvt_enable_water_polling_mode(chip_info, flag);

		if (ret < 0) {
			TPD_INFO("%s: enable water_polling_mode: %d failed\n", __func__, flag);
		}

		break;

	default:
		pr_err("[TP]: %s mode = %d Wrong mode\n", __func__, mode);
	}

	return ret;
}

static fw_check_state nvt_fw_check(void *chip_data,
				   struct resolution_info *resolution_info, struct panel_info *panel_data)
{
	uint8_t ver_len = 0;
	uint8_t fw_ver_len = 0;
	int ret = 0;
	char dev_version[MAX_DEVICE_VERSION_LENGTH] = {0};
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	nvt_esd_check_update_timer(chip_info);

	ret |= nvt_get_fw_info_noflash(chip_info);

	if (ret < 0) {
		TPD_INFO("%s: get fw info failed\n", __func__);
		return FW_ABNORMAL;

	} else {
		panel_data->tp_fw = chip_info->fw_ver;
		snprintf(dev_version, MAX_DEVICE_VERSION_LENGTH,
			 "%02X", panel_data->tp_fw);
		TPD_INFO("%s: dev_version = %s\n", __func__, dev_version);

		if (panel_data->manufacture_info.version) {
			ver_len = strlen(panel_data->manufacture_info.version);
			fw_ver_len = strlen(dev_version);
			snprintf(panel_data->manufacture_info.version + (ver_len - fw_ver_len),
				 sizeof(dev_version),
				 dev_version);
			TPD_DEBUG("%s: firmware_version = %s\n", __func__, panel_data->manufacture_info.version);
		}
	}

	return FW_NORMAL;
}

static int32_t nvt_clear_fw_status(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 20;

	for (i = 0; i < retry; i++) {
		/*---set xdata index to EVENT BUF ADDR---*/
		nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR |
				 EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

		/*---clear fw status---*/
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0x00;
		CTP_SPI_WRITE(chip_info->s_client, buf, 2);

		/*---read fw status---*/
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0x00) {
			break;
		}

		msleep(10);
	}

	if (i >= retry) {
		TPD_INFO("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;

	} else {
		return 0;
	}
}

static void nvt_enable_noise_collect(struct chip_data_nt36523 *chip_info,
					 int32_t frame_num)
{
	int ret = -1;
	uint8_t buf[8] = {0};

	/*---set xdata index to EVENT BUF ADDR---*/
	ret = nvt_set_page(chip_info,
			   chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

	/*---enable noise collect---*/
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0x47;
	buf[2] = 0xAA;
	buf[3] = frame_num;
	buf[4] = 0x00;
	ret |= CTP_SPI_WRITE(chip_info->s_client, buf, 5);

	if (ret < 0) {
		TPD_INFO("%s failed\n", __func__);
	}
}

static int8_t nvt_switch_FreqHopEnDis(struct chip_data_nt36523 *chip_info,
					  uint8_t FreqHopEnDis)
{
	uint8_t buf[8] = {0};
	uint8_t retry = 0;
	int8_t ret = 0;

	for (retry = 0; retry < 20; retry++) {
		/*---set xdata index to EVENT BUF ADDR---*/
		nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

		/*---switch FreqHopEnDis---*/
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = FreqHopEnDis;
		CTP_SPI_WRITE(chip_info->s_client, buf, 2);

		msleep(35);

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0x00) {
			break;
		}
	}

	if (unlikely(retry == 20)) {
		TPD_INFO("switch FreqHopEnDis 0x%02X failed, buf[1]=0x%02X\n", FreqHopEnDis,
			 buf[1]);
		ret = -1;
	}

	return ret;
}

static void nvt_change_mode(struct chip_data_nt36523 *chip_info, uint8_t mode)
{
	uint8_t buf[8] = {0};

	/*---set xdata index to EVENT BUF ADDR---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR |
			 EVENT_MAP_HOST_CMD);

	/*---set mode---*/
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = mode;
	CTP_SPI_WRITE(chip_info->s_client, buf, 2);

	if (mode == NORMAL_MODE) {
		msleep(20);
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = HANDSHAKING_HOST_READY;
		CTP_SPI_WRITE(chip_info->s_client, buf, 2);
		msleep(20);
	}
}

static uint8_t nvt_get_fw_pipe_noflash(struct chip_data_nt36523 *chip_info)
{
	int ret = -1;
	uint8_t buf[8] = {0};

	/*---set xdata index to EVENT BUF ADDR---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR |
			 	 EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

	/*---read fw status---*/
	buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
	buf[1] = 0x00;
	ret = CTP_SPI_READ(chip_info->s_client, buf, 2);

	if (ret) {
		TPD_INFO("%s: read or write failed\n", __func__);
	}

	return (buf[1] & 0x01);
}

#define LEN_TEMP_MDATA_BUF 8192
static void nvt_read_mdata(struct chip_data_nt36523 *chip_info,
			   uint32_t xdata_addr, int32_t *xdata, int32_t xdata_len)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	uint8_t buf[SPI_TANSFER_LENGTH + 2] = {0};
	uint32_t head_addr = 0;
	int32_t dummy_len = 0;
	int32_t data_len = 0;
	int32_t residual_len = 0;
	uint8_t *xdata_tmp = NULL;

	/*---set xdata sector address & length---*/
	head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
	dummy_len = xdata_addr - head_addr;
	data_len = chip_info->hw_res->tx_num * chip_info->hw_res->rx_num * 2;
	residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

	if (xdata_len / sizeof(int32_t) < data_len / 2) {
		TPD_INFO("xdata read buffer(%d) less than max data size(%d), return\n",
			 xdata_len, data_len);
		return;
	}

	/*malloc buffer space*/
	xdata_tmp = tp_devm_kzalloc(&chip_info->s_client->dev, LEN_TEMP_MDATA_BUF,
					GFP_KERNEL);

	if (xdata_tmp == NULL) {
		TPD_INFO("%s malloc memory failed\n", __func__);
		return;
	}

	/*read xdata : step 1*/
	for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
		/*---read xdata by SPI_TANSFER_LENGTH*/
		for (j = 0; j < (XDATA_SECTOR_SIZE / SPI_TANSFER_LENGTH); j++) {
			/*---change xdata index---*/
			nvt_set_page(chip_info, head_addr + (XDATA_SECTOR_SIZE * i) +
					 (SPI_TANSFER_LENGTH * j));

			/*---read data---*/
			buf[0] = SPI_TANSFER_LENGTH * j;
			CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

			/*---copy buf to xdata_tmp---*/
			for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
				xdata_tmp[XDATA_SECTOR_SIZE * i + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
				/*printk("0x%02X, 0x%04X\n", buf[k+1], (XDATA_SECTOR_SIZE*i + SPI_TANSFER_LENGTH*j + k));*/
			}
		}

		/*printk("addr=0x%05X\n", (head_addr+XDATA_SECTOR_SIZE*i));*/
	}

	/*read xdata : step2*/
	if (residual_len != 0) {
		/*---read xdata by SPI_TANSFER_LENGTH*/
		for (j = 0; j < (residual_len / SPI_TANSFER_LENGTH + 1); j++) {
			/*---change xdata index---*/
			nvt_set_page(chip_info, xdata_addr + data_len - residual_len +
					 (SPI_TANSFER_LENGTH * j));

			/*---read data---*/
			buf[0] = SPI_TANSFER_LENGTH * j;
			CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

			/*---copy buf to xdata_tmp---*/
			for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
				xdata_tmp[(dummy_len + data_len - residual_len) + SPI_TANSFER_LENGTH * j + k] =
					buf[k + 1];
				/*printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + SPI_TANSFER_LENGTH*j + k));*/
			}
		}

		/*printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len));*/
	}

	/*---remove dummy data and 2bytes-to-1data---*/
	for (i = 0; i < (data_len / 2); i++) {
		xdata[i] = (int16_t)(xdata_tmp[dummy_len + i * 2] + 256 * xdata_tmp[dummy_len +
					 i * 2 + 1]);
	}

	/*---set xdata index to EVENT BUF ADDR---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&xdata_tmp,
			  LEN_TEMP_MDATA_BUF);
}

static void nvt_read_get_num_mdata(struct chip_data_nt36523 *chip_info,
		uint32_t xdata_addr, int32_t *xdata, uint32_t num)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	uint8_t buf[SPI_TANSFER_LENGTH + 2] = {0};
	uint32_t head_addr = 0;
	int32_t dummy_len = 0;
	int32_t data_len = 0;
	int32_t residual_len = 0;
	uint8_t *xdata_tmp = NULL;

	/* ---set xdata sector address & length--- */
	head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
	dummy_len = xdata_addr - head_addr;
	data_len = num * 2;
	residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

	/*malloc buffer space*/
	xdata_tmp = tp_devm_kzalloc(&chip_info->s_client->dev, LEN_TEMP_MDATA_BUF,
					GFP_KERNEL);

	if (xdata_tmp == NULL) {
		TPD_INFO("%s malloc memory failed\n", __func__);
		return;
	}

	/* read xdata : step 1 */
	for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
		/* ---read xdata by SPI_TANSFER_LENGTH */
		for (j = 0; j < (XDATA_SECTOR_SIZE / SPI_TANSFER_LENGTH); j++) {
		/* ---change xdata index--- */
		nvt_set_page(chip_info, head_addr + (XDATA_SECTOR_SIZE * i) + (SPI_TANSFER_LENGTH * j));

		/* ---read data--- */
		buf[0] = SPI_TANSFER_LENGTH * j;
		CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

		/* ---copy buf to xdata_tmp--- */
		for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
				xdata_tmp[XDATA_SECTOR_SIZE * i + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
				/* printk("0x%02X, 0x%04X\n", buf[k+1], (XDATA_SECTOR_SIZE*i + SPI_TANSFER_LENGTH*j + k)); */
		}
		}
		/* printk("addr=0x%05X\n", (head_addr+XDATA_SECTOR_SIZE*i)); */
	}

	/* read xdata : step2 */
	if (residual_len != 0) {
		/* ---read xdata by SPI_TANSFER_LENGTH */
		for (j = 0; j < (residual_len / SPI_TANSFER_LENGTH + 1); j++) {
		/* ---change xdata index--- */
		nvt_set_page(chip_info, xdata_addr + data_len - residual_len + (SPI_TANSFER_LENGTH * j));

		/* ---read data--- */
		buf[0] = SPI_TANSFER_LENGTH * j;
		CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

		/* ---copy buf to xdata_tmp--- */
		for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
				xdata_tmp[(dummy_len + data_len - residual_len) + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
				/* printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + SPI_TANSFER_LENGTH*j + k)); */
		}
		}
		/* printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len)); */
	}

	/* ---remove dummy data and 2bytes-to-1data--- */
	for (i = 0; i < (data_len / 2); i++) {
		xdata[i] = (int16_t)(xdata_tmp[dummy_len + i * 2] + 256 * xdata_tmp[dummy_len + i * 2 + 1]);
	}

	/* ---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&xdata_tmp,
			  LEN_TEMP_MDATA_BUF);
}

#ifdef CONFIG_OPLUS_TP_APK
static void nvt_read_debug_mdata(struct chip_data_nt36523 *chip_info,
				 uint32_t xdata_addr, int32_t *xdata, int32_t xdata_len)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	uint8_t buf[SPI_TANSFER_LENGTH + 2] = {0};
	uint32_t head_addr = 0;
	int32_t dummy_len = 0;
	int32_t data_len = 0;
	int32_t residual_len = 0;
	uint8_t *xdata_tmp = NULL;

	/*---set xdata sector address & length---*/
	head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
	dummy_len = xdata_addr - head_addr;
	data_len = chip_info->hw_res->tx_num * chip_info->hw_res->rx_num;
	residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

	if (xdata_len / sizeof(int32_t) < data_len) {
		TPD_INFO("xdata read buffer(%d) less than max data size(%d), return\n",
			 xdata_len, data_len);
		return;
	}

	/*malloc buffer space*/
	xdata_tmp = tp_devm_kzalloc(&chip_info->s_client->dev, LEN_TEMP_MDATA_BUF,
					GFP_KERNEL);

	if (xdata_tmp == NULL) {
		TPD_INFO("%s malloc memory failed\n", __func__);
		return;
	}

	/*read xdata : step 1*/
	for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
		/*---read xdata by SPI_TANSFER_LENGTH*/
		for (j = 0; j < (XDATA_SECTOR_SIZE / SPI_TANSFER_LENGTH); j++) {
			/*---change xdata index---*/
			nvt_set_page(chip_info, head_addr + (XDATA_SECTOR_SIZE * i) +
					 (SPI_TANSFER_LENGTH * j));

			/*---read data---*/
			buf[0] = SPI_TANSFER_LENGTH * j;
			CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

			/*---copy buf to xdata_tmp---*/
			for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
				xdata_tmp[XDATA_SECTOR_SIZE * i + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
				/*printk("0x%02X, 0x%04X\n", buf[k+1], (XDATA_SECTOR_SIZE*i + SPI_TANSFER_LENGTH*j + k));*/
			}
		}

		/*printk("addr=0x%05X\n", (head_addr+XDATA_SECTOR_SIZE*i));*/
	}

	/*read xdata : step2*/
	if (residual_len != 0) {
		/*---read xdata by SPI_TANSFER_LENGTH*/
		for (j = 0; j < (residual_len / SPI_TANSFER_LENGTH + 1); j++) {
			/*---change xdata index---*/
			nvt_set_page(chip_info, xdata_addr + data_len - residual_len +
					 (SPI_TANSFER_LENGTH * j));

			/*---read data---*/
			buf[0] = SPI_TANSFER_LENGTH * j;
			CTP_SPI_READ(chip_info->s_client, buf, SPI_TANSFER_LENGTH + 1);

			/*---copy buf to xdata_tmp---*/
			for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
				xdata_tmp[(dummy_len + data_len - residual_len) + SPI_TANSFER_LENGTH * j + k] =
					buf[k + 1];
				/*printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + SPI_TANSFER_LENGTH*j + k));*/
			}
		}

		/*printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len));*/
	}

	/*---remove dummy data and scaling data (*8)---*/
	for (i = 0; i < (data_len); i++) {
		xdata[i] = (int16_t)(((int8_t) xdata_tmp[dummy_len + i]) * 8);
	}

	/*---set xdata index to EVENT BUF ADDR---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&xdata_tmp,
			  LEN_TEMP_MDATA_BUF);
}
#endif /* end of CONFIG_OPLUS_TP_APK*/

static int32_t nvt_polling_hand_shake_status(
								struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 250;

	msleep(20);

	for (i = 0; i < retry; i++) {
		/*---set xdata index to EVENT BUF ADDR---*/
		nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR |
				 EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

		/*---read fw status---*/
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0x00;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if ((buf[1] == 0xA0) || (buf[1] == 0xA1)) {
			break;
		}

		msleep(10);
	}

	if (i >= retry) {
		TPD_INFO("polling hand shake status failed, buf[1]=0x%02X\n", buf[1]);
		return -1;

	} else {
		return 0;
	}
}

static int32_t nvt_check_fw_status(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 50;

	msleep(20);

	for (i = 0; i < retry; i++) {
		/*---set xdata index to EVENT BUF ADDR---*/
		nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR |
				 EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

		/*---read fw status---*/
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0x00;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if ((buf[1] & 0xF0) == 0xA0) {
			break;
		}

		msleep(10);
	}

	if (i >= retry) {
		TPD_INFO("%s failed, i=%d, buf[1]=0x%02X\n", __func__, i, buf[1]);
		return -1;

	} else {
		return 0;
	}
}

static uint8_t nvt_get_fw_pipe(struct chip_data_nt36523 *chip_info)
{
	int ret = -1;
	uint8_t buf[8] = {0};

	/*---set xdata index to EVENT BUF ADDR---*/
	ret = nvt_set_page(chip_info,
					   chip_info->trim_id_table.mmap->EVENT_BUF_ADDR |
					   EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

	/*---read fw status---*/
	buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
	buf[1] = 0x00;
	ret = CTP_SPI_READ(chip_info->s_client, buf, 2);

	if (ret < 0) {
		TPD_INFO("%s: read or write failed\n", __func__);
	}

	return (buf[1] & 0x01);
}

static int32_t nvt_read_fw_noise(struct chip_data_nt36523 *chip_info,
				 int32_t config_diff_test_frame, int32_t *xdata, int32_t *xdata_n,
				 int32_t xdata_len)
{
	uint32_t x = 0;
	uint32_t y = 0;
	int32_t iArrayIndex = 0;
	int32_t frame_num = 0;

	if (xdata_len / sizeof(int32_t) < chip_info->hw_res->tx_num *
			chip_info->hw_res->rx_num) {
		TPD_INFO("read fw nosie buffer(%d) less than data size(%d)\n", xdata_len,
			 	 chip_info->hw_res->tx_num * chip_info->hw_res->rx_num);
		return -1;
	}

	/*---Enter Test Mode---*/
	if (nvt_clear_fw_status(chip_info)) {
		return -EAGAIN;
	}

	frame_num = config_diff_test_frame / 10;

	if (frame_num <= 0) {
		frame_num = 1;
	}

	TPD_INFO("%s: frame_num=%d\n", __func__, frame_num);
	nvt_enable_noise_collect(chip_info, frame_num);
	TPD_INFO("%s: going to do sleep for %dms \n", __func__, frame_num * 83);
	/* need wait PS_Config_Diff_Test_Frame * 8.3ms*/
	msleep(frame_num * 83);

	if (nvt_polling_hand_shake_status(chip_info)) {
		return -EAGAIN;
	}

	if (nvt_get_fw_info_noflash(chip_info)) {
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe(chip_info) == 0) {
		nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR, xdata,
				   xdata_len);

	} else {
		nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE1_ADDR, xdata,
				   xdata_len);
	}

	for (y = 0; y < chip_info->hw_res->rx_num; y++) {
		for (x = 0; x < chip_info->hw_res->tx_num; x++) {
			iArrayIndex = y * chip_info->hw_res->tx_num + x;
			xdata_n[iArrayIndex] = (int8_t)(xdata[iArrayIndex] & 0xFF);
			xdata[iArrayIndex] = (int8_t)((xdata[iArrayIndex] >> 8) & 0xFF);
		}
	}

	/*---Leave Test Mode---*/
	/*nvt_change_mode(chip_info, NORMAL_MODE);*/
	return 0;
}

static int32_t nvt_enter_digital_test(struct chip_data_nt36523 *chip_info,
					  uint8_t enter_digital_test)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 70;
	/*---set xdata index to EVENT BUF ADDR---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR |
			 EVENT_MAP_HOST_CMD);

	/*---set mode---*/
	if (enter_digital_test == true) {
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x32;
		buf[2] = 0x01;
		buf[3] = 0x08;
		buf[4] = 0x01;
		CTP_SPI_WRITE(chip_info->s_client, buf, 5);

	} else {
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x32;
		buf[2] = 0x00;
		buf[3] = 0x07;
		CTP_SPI_WRITE(chip_info->s_client, buf, 4);
	}

	/*---poling fw handshake*/
	for (i = 0; i < retry; i++) {
		/*---set xdata index to EVENT BUF ADDR---*/
		nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR |
				 EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);
		/*---read fw status---*/
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0x00;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0xAA) {
			break;
		}

		msleep(20);
	}

	if (i >= retry) {
		TPD_INFO("polling hand shake status failed, buf[1]=0x%02X\n", buf[1]);
		/* Read back 5 bytes from offset EVENT_MAP_HOST_CMD for debug check*/
		nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR |
				 EVENT_MAP_HOST_CMD);
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x00;
		buf[2] = 0x00;
		buf[3] = 0x00;
		buf[4] = 0x00;
		buf[5] = 0x00;
		CTP_SPI_READ(chip_info->s_client, buf, 6);
		TPD_INFO("Read back 5 bytes from offset EVENT_MAP_HOST_CMD: 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
			 buf[1], buf[2], buf[3], buf[4], buf[5]);
		return -1;

	} else {
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0xCC;
		CTP_SPI_WRITE(chip_info->s_client, buf, 2);
	}

	msleep(20);
	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen nvt_check_bin_checksum function.
Compare checksum from bin and calculated results to check
bin file is correct or not.
return:
	n.a.
*******************************************************/
static int32_t nvt_check_bin_checksum(const u8 *fwdata, size_t fwsize)
{
	uint32_t checksum_calculated = 0;
	uint32_t checksum_bin = 0;
	int32_t ret = 0;

	/* calculate the checksum reslut */
	checksum_calculated = CheckSum(fwdata, fwsize - FW_BIN_CHECKSUM_LEN - 1);
	/* get the checksum from file directly */
	checksum_bin = byte_to_word(fwdata + (fwsize - FW_BIN_CHECKSUM_LEN));
	if (checksum_calculated != checksum_bin) {
		TPD_INFO("%s checksum_calculated = 0x%08X\n", __func__, checksum_calculated);
		TPD_INFO("%s checksum_bin = 0x%08X\n", __func__, checksum_bin);
		ret = -EINVAL;
	}

	return ret;
}

static int32_t nvt_get_fw_need_write_size(const struct firmware *fw,
		struct chip_data_nt36523 *chip_info)
{
	int32_t i = 0;
	int32_t total_sectors_to_check = 0;

	total_sectors_to_check = fw->size / FLASH_SECTOR_SIZE;

	for (i = total_sectors_to_check; i > 0; i--) {
		/* check if there is end flag "NVT" at the end of this sector, firmware released*/
		if (strncmp(&fw->data[i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN], "NVT",
				NVT_FLASH_END_FLAG_LEN) == 0) {
			chip_info->fw_need_write_size = i * FLASH_SECTOR_SIZE;
			TPD_DEBUG("[NVT] fw_need_write_size = %zu(0x%zx), NVT end flag\n",
				 chip_info->fw_need_write_size, chip_info->fw_need_write_size);
			return 0;
		}

		/* check if there is end flag "MOD" at the end of this sector, firmware released to Panel Factory */
		if (strncmp(&fw->data[i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN], "MOD",
				NVT_FLASH_END_FLAG_LEN) == 0) {
			chip_info->fw_need_write_size = i * FLASH_SECTOR_SIZE;
			TPD_DEBUG("[MOD] fw_need_write_size = %zu(0x%zx), MOD end flag\n",
				 chip_info->fw_need_write_size, chip_info->fw_need_write_size);
			return 0;
		}
	}

	TPD_INFO("end flag \"NVT\" \"MOD\" not found!\n");
	return -EPERM;
}

static int check_fw_checksum_write_size(const struct firmware *fw,
		struct chip_data_nt36523 *chip_info)
{
	/* check FW need to write size */
	if (nvt_get_fw_need_write_size(fw, chip_info)) {
		TPD_INFO("get fw need to write size fail!\n");
		return -1;
	}

	/* check if FW version add FW version bar equals 0xFF */
	if (*(fw->data + (chip_info->fw_need_write_size - SIZE_4KB)) +
		*(fw->data + (chip_info->fw_need_write_size - SIZE_4KB + 1)) !=
			0xFF) {
		TPD_INFO("bin file FW_VER + FW_VER_BAR should be 0xFF!\n");
		TPD_INFO("FW_VER=0x%02X, FW_VER_BAR=0x%02X\n",
			*(fw->data + (chip_info->fw_need_write_size - SIZE_4KB)),
			 *(fw->data + (chip_info->fw_need_write_size - SIZE_4KB + 1)));
		return -1;
	}
	return 0;
}

static fw_update_state nvt_fw_update_sub(void *chip_data,
		const struct firmware *fw, bool force)
{
	int ret = 0;
	uint8_t point_data[POINT_DATA_LEN + 2] = {0};
	struct chip_data_nt36523 *chip_info = NULL;
	struct touchpanel_data *ts = NULL;
	struct firmware *request_fw_headfile = NULL;
	struct firmware *request_fw_tmp = NULL;
	struct monitor_data *monitor_data = NULL;

	/* request firmware failed, get from headfile */
	if (chip_data == NULL) {
		pr_err("[TP]: %s in %d NULL point\n", __func__, __LINE__);
		return FW_UPDATE_ERROR;
	}
	chip_info = (struct chip_data_nt36523 *)chip_data;
	if (chip_info->s_client == NULL || chip_info->monitor_data == NULL) {
		pr_err("[TP]: %s in %d NULL point\n", __func__, __LINE__);
		return FW_UPDATE_ERROR;
	}
	ts = spi_get_drvdata(chip_info->s_client);
	monitor_data =  chip_info->monitor_data;

	if (fw == NULL) {
		request_fw_headfile = tp_devm_kzalloc(&chip_info->s_client->dev,
							  sizeof(struct firmware), GFP_KERNEL);

		if (request_fw_headfile == NULL) {
			TPD_INFO("%s kzalloc failed!\n", __func__);
			goto out_fail;
		}

		if (chip_info->g_fw_sta) {
			TPD_DEBUG("request firmware failed, get from g_fw_buf\n");
			request_fw_headfile->size = chip_info->g_fw_len;
			request_fw_headfile->data = chip_info->g_fw_buf;
			fw = request_fw_headfile;

		} else {
			tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE,
						 "Request fw from headfile");
			TPD_DEBUG("request firmware failed, get from headfile\n");
			if (chip_info->ts->firmware_in_dts == NULL) {
				pr_err("[TP]: %s in %d NULL point\n", __func__, __LINE__);
				goto out_fail;
			}

			if (ts->firmware_in_dts && ts->firmware_in_dts->data) {
				request_fw_headfile->size = ts->firmware_in_dts->size;
				request_fw_headfile->data = ts->firmware_in_dts->data;
				fw = request_fw_headfile;

			} else {
				TPD_INFO("firmware_data is NULL! exit firmware update!\n");
				goto out_fail;
			}
		}
	}
	ret = check_fw_checksum_write_size(fw, chip_info);
	if (ret < 0) {
		pr_err("[TP]: %s try fw in dts\n", __func__);
		goto try_fw_in_dts;
	}
	/* fw checksum compare */
	ret = nvt_check_bin_checksum(fw->data, fw->size);

	if (ret) {
try_fw_in_dts:
		if (ts->firmware_in_dts && ts->firmware_in_dts->data && (fw->data != ts->firmware_in_dts->data)) {
			tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE,
						 "HW CHECKSUM CHECKING Failed");
			TPD_INFO("Image fw check checksum failed, reload fw from array\n");

			request_fw_tmp = tp_devm_kzalloc(&chip_info->s_client->dev,
				sizeof(struct firmware), GFP_KERNEL);

			if (request_fw_tmp == NULL) {
				TPD_INFO("%s kzalloc failed!\n", __func__);
				goto out_fail;
			}
			request_fw_tmp->size = ts->firmware_in_dts->size;
			request_fw_tmp->data = ts->firmware_in_dts->data;
			fw = request_fw_tmp;

			ret = check_fw_checksum_write_size(fw, chip_info);
			if (ret < 0)
				goto out_fail;
			ret = nvt_check_bin_checksum(fw->data, fw->size);
			if (ret < 0)
				pr_err("[TP]: %s retry update fw by dts failed ,but use still\n", __func__);
			TPD_DEBUG("%s retry update fw by dts success\n", __func__);
		} else {
			TPD_INFO("array fw check checksum failed, but use still\n");
		}

	} else {
		TPD_DEBUG("fw check checksum ok\n");
	}

	/* BIN Header Parser */
	ret = nvt_bin_header_parser(chip_info, fw->data, fw->size);

	if (ret) {
		tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE,
					 "HW HEADER PARSERING Failed");
		TPD_INFO("bin header parser failed\n");
		goto out_fail;
	}

	/* initial buffer and variable */
	ret = Download_Init(chip_info);

	if (ret) {
		TPD_INFO("Download Init failed. (%d)\n", ret);
		goto out_fail;
	}

	/* download firmware process */
	ret = Download_Firmware_HW_CRC(chip_info, fw);

	if (ret) {
		TPD_INFO("Download Firmware failed. (%d)\n", ret);
		goto out_fail;
	}

	TPD_INFO("Update firmware success! <%ld us>\n",
			 (long) ktime_us_delta(chip_info->end, chip_info->start));

	/* Get FW Info */
	ret = CTP_SPI_READ(chip_info->s_client, point_data, POINT_DATA_LEN + 1);

	if (ret < 0 || nvt_fw_recovery(point_data)) {
		nvt_esd_check_enable(chip_info, true);
	}

	ret = nvt_get_fw_info_noflash(chip_info);

	if (ret) {
		TPD_INFO("nvt_get_fw_info_noflash failed. (%d)\n", ret);
		goto out_fail;
	}

	ret = CTP_SPI_READ(chip_info->s_client, point_data, POINT_DATA_LEN + 1);

	if (ret < 0 || nvt_fw_recovery(point_data)) {
		nvt_esd_check_enable(chip_info, true);
	}

	nvt_fw_check(ts->chip_data, &ts->resolution_info, &ts->panel_data);

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&chip_info->bin_map,
			  (chip_info->partition + 1) * sizeof(struct nvt_ts_bin_map));

	if (request_fw_headfile != NULL) {
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&request_fw_headfile,
				  sizeof(struct firmware));
		request_fw_headfile = NULL;
	}

	if (request_fw_tmp != NULL) {
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&request_fw_tmp,
				  sizeof(struct firmware));
		request_fw_tmp = NULL;
	}

	tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "FW update Success");
	return FW_UPDATE_SUCCESS;

out_fail:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&chip_info->bin_map,
			  (chip_info->partition + 1) * sizeof(struct nvt_ts_bin_map));
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&request_fw_headfile,
			  sizeof(struct firmware));
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&request_fw_tmp,
		sizeof(struct firmware));
	if (ret) {
        	return FW_UPDATE_ERROR;
	} else {
        	return FW_NO_NEED_UPDATE;
	}
}



static fw_update_state nvt_fw_update(void *chip_data, const struct firmware *fw,
					 bool force)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	if (fw) {
		if (fw->size > FW_BUF_SIZE) {
			TPD_INFO("error size %ld (%d)\n", fw->size, FW_BUF_SIZE);
			return FW_UPDATE_FATAL;
		}
		if (chip_info->g_fw_buf) {
			chip_info->g_fw_len = fw->size;
			memcpy(chip_info->g_fw_buf, fw->data, fw->size);
			chip_info->g_fw_sta = true;
		}
	}

	return nvt_fw_update_sub(chip_data, fw, force);
}

static void nvt_enable_doze_noise_collect(struct chip_data_nt36523 *chip_info, int32_t frame_num)
{
	int ret = -1;
	uint8_t buf[8] = {0};

	/*---set xdata index to EVENT BUF ADDR---*/
	ret = nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	/*---enable noise collect---*/
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0x4B;
	buf[2] = 0xAA;
	buf[3] = frame_num;
	buf[4] = 0x00;
	ret |= CTP_SPI_WRITE(chip_info->s_client, buf, 5);

	if (ret < 0) {
		TPD_INFO("%s failed\n", __func__);
	}
}

static int32_t nvt_read_doze_fw_noise(struct chip_data_nt36523 *chip_info,
					  int32_t config_doze_noise_test_frame, int32_t *xdata,
					  int32_t *xdata_n, int32_t xdata_len)
{
	uint8_t buf[128] = {0};
	uint32_t x = 0;
	uint32_t y = 0;
	uint32_t doze_x_num = chip_info->p_nvt_test_para->doze_x_channel;
	uint32_t doze_y_num = chip_info->p_nvt_test_para->doze_y_channel;
	int32_t iArrayIndex = 0;
	int32_t frame_num = 0;

	if (xdata_len / sizeof(int32_t) < doze_y_num * doze_x_num) {
		TPD_INFO("read doze nosie buffer(%d) less than data size(%d)\n", xdata_len,
			 doze_y_num * doze_x_num);
		return -1;
	}

	/*---Enter Test Mode---*/
	if (nvt_clear_fw_status(chip_info)) {
		return -EAGAIN;
	}

	frame_num = config_doze_noise_test_frame / 10;

	if (frame_num <= 0) {
		frame_num = 1;
	}

	TPD_INFO("%s: frame_num=%d\n", __func__, frame_num);
	nvt_enable_doze_noise_collect(chip_info, frame_num);
	/* need wait PS_Config_Doze_Noise_Test_Frame * 8.3ms*/
	msleep(frame_num * 83);

	if (nvt_polling_hand_shake_status(chip_info)) {
		return -EAGAIN;
	}

	for (x = 0; x < doze_x_num; x++) {
		/*---change xdata index---*/
		nvt_set_page(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR + doze_y_num
				 * 2 * x);

		/*---read data---*/
		buf[0] = (chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR + doze_y_num *
			  2 * x) & 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, doze_y_num * 2 + 1);

		for (y = 0; y < doze_y_num; y++) {
			xdata[y * doze_x_num + x] = (uint16_t)(buf[y * 2 + 1] + 256 *
							buf[y * 2 + 2]);
		}
	}

	for (y = 0; y < doze_y_num; y++) {
		for (x = 0; x < doze_x_num; x++) {
			iArrayIndex = y * doze_x_num + x;
			xdata_n[iArrayIndex] = (int8_t)(xdata[iArrayIndex] & 0xFF) * 4;
			xdata[iArrayIndex] = (int8_t)((xdata[iArrayIndex] >> 8) & 0xFF) * 4;	/*scaling up*/
		}
	}

	/*---Leave Test Mode---*/
	/*nvt_change_mode(NORMAL_MODE);	No return to normal mode. Continuous to get doze rawdata*/

	return 0;
}

static int32_t nvt_read_doze_baseline(struct chip_data_nt36523 *chip_info,
					  int32_t *xdata, int32_t xdata_len)
{
	uint8_t buf[256] = {0};
	uint32_t x = 0;
	uint32_t y = 0;
	uint32_t doze_x_num = chip_info->p_nvt_test_para->doze_x_channel;
	uint32_t doze_y_num = chip_info->p_nvt_test_para->doze_y_channel;
	int32_t iArrayIndex = 0;

	/*---Enter Test Mode---*/

	if (xdata_len / sizeof(int32_t) < doze_y_num * doze_x_num) {
		TPD_INFO("read doze baseline buffer(%d) less than data size(%d)\n", xdata_len,
			 doze_y_num * doze_x_num);
		return -1;
	}

	for (x = 0; x < doze_x_num; x++) {
		/*---change xdata index---*/
		nvt_set_page(chip_info, chip_info->trim_id_table.mmap->DOZE_GM_S1D_SCAN_RAW_ADDR
				+ doze_y_num * 2 * x);

		/*---read data---*/
		buf[0] = (chip_info->trim_id_table.mmap->DOZE_GM_S1D_SCAN_RAW_ADDR + doze_y_num *
			2 * x) & 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, doze_y_num * 2 + 1);

		for (y = 0; y < doze_y_num; y++) {
			xdata[y * doze_x_num + x] = (uint16_t)(buf[y * 2 + 1] + 256 * buf[y * 2 + 2]);
		}
	}

	for (y = 0; y < doze_y_num; y++) {
		for (x = 0; x < doze_x_num; x++) {
			iArrayIndex = y * doze_x_num + x;
			xdata[iArrayIndex] = (int16_t)xdata[iArrayIndex];
		}
	}

	/*---Leave Test Mode---*/
	nvt_change_mode(chip_info, NORMAL_MODE);
	return 0;
}

static int nvt_lpwg_rawdata_test(struct seq_file *s, void *chip_data,
				 struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int32_t *raw_data = NULL;
	int32_t iArrayIndex = 0;
	int8_t rawdata_result = -NVT_MP_UNKNOWN;
	uint8_t *raw_record = NULL;
	uint8_t data_buf[64] = {0};
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	int err_cnt = 0;
	int rx_num = chip_info->hw_res->rx_num;
	int tx_num = chip_info->hw_res->tx_num;
	record_len = rx_num * tx_num * sizeof(uint8_t);
	buf_len	= rx_num * tx_num * sizeof(int32_t);

	TPD_INFO("%s +\n", __func__);
	raw_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!raw_data) {
		TPD_INFO("raw_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	raw_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

	if (!raw_record) {
		TPD_INFO("raw_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	/*---LPWG Rawdata Test---*/
	TPD_INFO("LPWG mode FW Rawdata Test\n");
	rawdata_result = NVT_MP_PASS;

	if (nvt_get_fw_pipe(chip_info) == 0) {
		nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->RAW_PIPE0_ADDR,
				   raw_data, buf_len);

	} else {
		nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->RAW_PIPE1_ADDR,
				   raw_data, buf_len);
	}

	if (chip_info->p_nvt_autotest_offset->lpwg_rawdata_p
			&& chip_info->p_nvt_autotest_offset->lpwg_rawdata_n) {
		snprintf(data_buf, 64, "%s\n", "[NVT LPWG RAW DATA]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
			strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(raw_data, chip_info, nvt_testdata,
				tx_num, tx_num * rx_num);

		if ((chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_p != 0)
				&& (chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_n != 0)) {
			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;
					TPD_DEBUG_NTAG("%d, ", raw_data[iArrayIndex]);

					if ((raw_data[iArrayIndex] >
							chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_p) \
							|| (raw_data[iArrayIndex] <
								chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_n)) {
						rawdata_result = -NVT_MP_FAIL;
						raw_record[iArrayIndex] = 1;
						TPD_INFO("LPWG_Rawdata Test failed at rawdata[%d][%d] = %d[%d %d]\n",
							 i, j, raw_data[iArrayIndex],
							 chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_n,
							 chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_p);

						if (!err_cnt) {
							TPD_INFO(
								"LPWG Rawdata[%d][%d] = %d[%d %d]\n",
								i, j, raw_data[iArrayIndex],
								chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_n,
								chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_p);
						}

						err_cnt++;
					}
				}

				TPD_DEBUG_NTAG("\n");
			}

		} else {
			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;
					TPD_DEBUG_NTAG("%d, ", raw_data[iArrayIndex]);

					if ((raw_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->lpwg_rawdata_p[iArrayIndex]) \
							|| (raw_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->lpwg_rawdata_n[iArrayIndex])) {
						rawdata_result = -NVT_MP_FAIL;
						raw_record[iArrayIndex] = 1;
						TPD_INFO("LPWG_Rawdata Test failed at rawdata[%d][%d] = %d\n", i, j,
							 raw_data[iArrayIndex]);

						if (!err_cnt) {
							TPD_INFO(
								"LPWG Rawdata[%d][%d] = %d[%d %d]\n",
								i, j, raw_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->lpwg_rawdata_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->lpwg_rawdata_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}

				TPD_DEBUG_NTAG("\n");
			}
		}

	} else {
		TPD_INFO("lpwg_rawdata_p || lpwg_rawdata_n is NULL\n");
	}

MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&raw_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&raw_record, record_len);

	/*---Leave Test Mode---*/
	nvt_change_mode(chip_info, NORMAL_MODE);
	TPD_INFO("%s -\n", __func__);

	return rawdata_result;
}

static int nvt_lpwg_diff_rawdata_test(struct seq_file *s, void *chip_data,
					  struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int32_t *noise_p_data = NULL;
	int32_t *noise_n_data = NULL;
	int32_t iArrayIndex = 0;
	int8_t noise_p_result = -NVT_MP_UNKNOWN;
	int8_t noise_n_result = -NVT_MP_UNKNOWN;
	uint8_t *noise_p_record = NULL;
	uint8_t *noise_n_record = NULL;
	uint8_t data_buf[64] = {0};
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	int err_cnt = 0;
	int rx_num = chip_info->hw_res->rx_num;
	int tx_num = chip_info->hw_res->tx_num;
	record_len = rx_num * tx_num * sizeof(uint8_t);
	buf_len	= rx_num * tx_num * sizeof(int32_t);

	TPD_INFO("%s +\n", __func__);

	noise_p_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!noise_p_data) {
		TPD_INFO("noise_p_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	noise_n_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!noise_n_data) {
		TPD_INFO("noise_n_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	noise_p_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
					 GFP_KERNEL);

	if (!noise_p_record) {
		TPD_INFO("noise_p_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	noise_n_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
					 GFP_KERNEL);

	if (!noise_n_record) {
		TPD_INFO("noise_n_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	/*---LPWG DIFF RAWDATA TEST---*/
	noise_p_result = NVT_MP_PASS;
	noise_n_result = NVT_MP_PASS;

	if (nvt_read_fw_noise(chip_info,
				  chip_info->p_nvt_test_para->config_diff_test_frame, noise_p_data, noise_n_data,
				  buf_len) != 0) {
		TPD_INFO("LPWG mode read Noise data failed!\n");	/* 1: ERROR*/
		noise_p_result = -NVT_MP_FAIL_READ_DATA;
		noise_n_result = -NVT_MP_FAIL_READ_DATA;
		TPD_INFO("LPWG mode read Noise data failed!\n");
		err_cnt++;
		goto TEST_END;
	}

	if (chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_p &&
		chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_n) {
		snprintf(data_buf, 64, "%s\n", "[NVT LPWG DIFF RAW DATA POSITIVE]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
			strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(noise_p_data, chip_info, nvt_testdata,
				tx_num, tx_num * rx_num);

		snprintf(data_buf, 64, "%s\n", "[NVT LPWG DIFF RAW DATA NEGATIVE]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
			strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(noise_n_data, chip_info, nvt_testdata,
				tx_num, tx_num * rx_num);

		TPD_INFO("LPWG Noise RawData_Diff_Max:\n");

		if ((chip_info->p_nvt_test_para->config_lmt_lpwg_diff_p != 0)
				&& (chip_info->p_nvt_test_para->config_lmt_lpwg_diff_n != 0)) {
			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;
					TPD_DEBUG_NTAG("%d, ", noise_p_data[iArrayIndex]);

					if ((noise_p_data[iArrayIndex] >
							chip_info->p_nvt_test_para->config_lmt_lpwg_diff_p) \
							|| (noise_p_data[iArrayIndex] <
								chip_info->p_nvt_test_para->config_lmt_lpwg_diff_n)) {
						noise_p_result = -NVT_MP_FAIL;
						noise_p_record[iArrayIndex] = 1;
						TPD_INFO("LPWG Noise RawData_Diff_Max Test failed at rawdata[%d][%d] = %d[%d %d]\n",
							 i, j, noise_p_data[iArrayIndex],
							 chip_info->p_nvt_test_para->config_lmt_lpwg_diff_n,
							 chip_info->p_nvt_test_para->config_lmt_lpwg_diff_p);

						if (!err_cnt) {
							TPD_INFO(
								"LPWG Noise RawData_Diff_Max[%d][%d] = %d[%d %d]\n",
								i, j, noise_p_data[iArrayIndex],
								chip_info->p_nvt_test_para->config_lmt_lpwg_diff_n,
								chip_info->p_nvt_test_para->config_lmt_lpwg_diff_p);
						}

						err_cnt++;
					}
				}

				TPD_DEBUG_NTAG("\n");
			}

			TPD_INFO("LPWG Noise RawData_Diff_Min:\n");

			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;
					TPD_DEBUG_NTAG("%d, ", noise_n_data[iArrayIndex]);

					if ((noise_n_data[iArrayIndex] >
							chip_info->p_nvt_test_para->config_lmt_lpwg_diff_p) \
							|| (noise_n_data[iArrayIndex] <
								chip_info->p_nvt_test_para->config_lmt_lpwg_diff_n)) {
						noise_n_result = -NVT_MP_FAIL;
						noise_n_record[iArrayIndex] = 1;
						TPD_INFO("LPWG Noise RawData_Diff_Min Test failed at rawdata[%d][%d] = %d[%d %d]\n",
							 i, j, noise_n_data[iArrayIndex],
							 chip_info->p_nvt_test_para->config_lmt_lpwg_diff_n,
							 chip_info->p_nvt_test_para->config_lmt_lpwg_diff_p);

						if (!err_cnt) {
							TPD_INFO(
								"LPWG Noise RawData_Diff_Min[%d][%d] = %d[%d %d]\n",
								i, j, noise_n_data[iArrayIndex],
								chip_info->p_nvt_test_para->config_lmt_lpwg_diff_n,
								chip_info->p_nvt_test_para->config_lmt_lpwg_diff_p);
						}

						err_cnt++;
					}
				}

				TPD_DEBUG_NTAG("\n");
			}

		} else {
			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;
					TPD_DEBUG_NTAG("%d, ", noise_p_data[iArrayIndex]);

					if ((noise_p_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_p[iArrayIndex]) \
							|| (noise_p_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_n[iArrayIndex])) {
						noise_p_result = -NVT_MP_FAIL;
						noise_p_record[iArrayIndex] = 1;
						TPD_INFO("LPWG Noise RawData_Diff_Max Test failed at rawdata[%d][%d] = %d\n", i,
							 j, noise_p_data[iArrayIndex]);

						if (!err_cnt) {
							TPD_INFO(
								"LPWG Noise RawData_Diff_Max[%d][%d] = %d[%d %d]\n",
								i, j, noise_p_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}

				TPD_DEBUG_NTAG("\n");
			}

			TPD_INFO("LPWG Noise RawData_Diff_Min:\n");

			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;
					TPD_DEBUG_NTAG("%d, ", noise_n_data[iArrayIndex]);

					if ((noise_n_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_p[iArrayIndex]) \
							|| (noise_n_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_n[iArrayIndex])) {
						noise_n_result = -NVT_MP_FAIL;
						noise_n_record[iArrayIndex] = 1;
						TPD_INFO("LPWG Noise RawData_Diff_Min Test failed at rawdata[%d][%d] = %d\n", i,
							 j, noise_n_data[iArrayIndex]);

						if (!err_cnt) {
							TPD_INFO(
								"LPWG Noise RawData_Diff_Min[%d][%d] = %d[%d %d]\n",
								i, j, noise_n_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}

				TPD_DEBUG_NTAG("\n");
			}
		}

	} else {
		TPD_INFO("lpwg_diff_rawdata_n || lpwg_diff_rawdata_p is NULL\n");
	}

	/*---Leave Test Mode---*/
	nvt_change_mode(chip_info, NORMAL_MODE);

TEST_END:
MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&noise_p_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&noise_n_data, buf_len);

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&noise_p_record, record_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&noise_n_record, record_len);
	TPD_INFO("%s -\n", __func__);
	return (noise_p_result + noise_n_result);
}

static int nvt_fdm_diff_rawdata_test(struct seq_file *s, void *chip_data,
					 struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int32_t *fdm_noise_p_data = NULL;
	int32_t *fdm_noise_n_data = NULL;
	int32_t iArrayIndex = 0;
	uint8_t *fdm_noise_p_record = NULL;
	uint8_t *fdm_noise_n_record = NULL;
	int8_t fdm_noise_p_result = -NVT_MP_UNKNOWN;
	int8_t fdm_noise_n_result = -NVT_MP_UNKNOWN;
	uint8_t data_buf[64] = {0};
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	int err_cnt = 0;
	int doze_x_num = chip_info->p_nvt_test_para->doze_x_channel;
	int doze_y_num = chip_info->p_nvt_test_para->doze_y_channel;
	record_len = doze_x_num * doze_y_num * sizeof(uint8_t);
	buf_len	= doze_x_num * doze_y_num * sizeof(int32_t);

	TPD_INFO("%s +\n", __func__);
	fdm_noise_p_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len,
					   GFP_KERNEL);

	if (!fdm_noise_p_data) {
		TPD_INFO("fdm_noise_p_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	fdm_noise_n_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len,
					   GFP_KERNEL);

	if (!fdm_noise_n_data) {
		TPD_INFO("fdm_noise_n_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	fdm_noise_p_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
						 GFP_KERNEL);

	if (!fdm_noise_p_record) {
		TPD_INFO("fdm_noise_p_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	fdm_noise_n_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
						 GFP_KERNEL);

	if (!fdm_noise_n_record) {
		TPD_INFO("fdm_noise_n_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	/*---FDM DIFF RAWDATA Test---*/
	TPD_INFO("FDM FW Noise Test\n");
	fdm_noise_p_result = NVT_MP_PASS;
	fdm_noise_n_result = NVT_MP_PASS;

	if (nvt_read_doze_fw_noise(chip_info,
				   chip_info->p_nvt_test_para->config_fdm_noise_test_frame,
				   fdm_noise_p_data, fdm_noise_n_data, buf_len) != 0) {
		TPD_INFO("read FDM Noise data failed!\n");
		fdm_noise_p_result = -NVT_MP_FAIL_READ_DATA;
		fdm_noise_n_result = -NVT_MP_FAIL_READ_DATA;
		TPD_INFO("read FDM Noise data failed!\n");
		err_cnt++;
		goto TEST_END;
	}

	if (chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_p
			&& chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_n) {
		snprintf(data_buf, 64, "%s\n", "[NVT FDM DIFF RAW DATA POSITIVE]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
			strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(fdm_noise_p_data, chip_info, nvt_testdata,
				doze_x_num,
				doze_x_num * doze_y_num);

		snprintf(data_buf, 64, "%s\n", "[NVT FDM DIFF RAW DATA NEGATIVE]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
			strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(fdm_noise_n_data, chip_info, nvt_testdata,
				doze_x_num,
				doze_x_num * doze_y_num);

		if ((chip_info->p_nvt_test_para->config_lmt_fdm_diff_p != 0)
				&& (chip_info->p_nvt_test_para->config_lmt_fdm_diff_n != 0)) {
			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((fdm_noise_p_data[iArrayIndex] >
							chip_info->p_nvt_test_para->config_lmt_fdm_diff_p) \
							|| (fdm_noise_p_data[iArrayIndex] <
								chip_info->p_nvt_test_para->config_lmt_fdm_diff_n)) {
						fdm_noise_p_result = -NVT_MP_FAIL;
						fdm_noise_p_record[iArrayIndex] = 1;
						TPD_INFO("FDM Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n",
							 i, j, fdm_noise_p_data[iArrayIndex],
							 chip_info->p_nvt_test_para->config_lmt_fdm_diff_n,
							 chip_info->p_nvt_test_para->config_lmt_fdm_diff_p);

						if (!err_cnt) {
							TPD_INFO(
								"FDM Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								fdm_noise_p_data[iArrayIndex],
								chip_info->p_nvt_test_para->config_lmt_fdm_diff_n,
								chip_info->p_nvt_test_para->config_lmt_fdm_diff_p);
						}

						err_cnt++;
					}
				}
			}

			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((fdm_noise_n_data[iArrayIndex] >
							chip_info->p_nvt_test_para->config_lmt_fdm_diff_p) \
							|| (fdm_noise_n_data[iArrayIndex] <
								chip_info->p_nvt_test_para->config_lmt_fdm_diff_n)) {
						fdm_noise_n_result = -NVT_MP_FAIL;
						fdm_noise_n_record[iArrayIndex] = 1;
						TPD_INFO("FDM Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n",
							 i, j, fdm_noise_n_data[iArrayIndex],
							 chip_info->p_nvt_test_para->config_lmt_fdm_diff_n,
							 chip_info->p_nvt_test_para->config_lmt_fdm_diff_p);

						if (!err_cnt) {
							TPD_INFO(
								"FDM Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								fdm_noise_n_data[iArrayIndex],
								chip_info->p_nvt_test_para->config_lmt_fdm_diff_n,
								chip_info->p_nvt_test_para->config_lmt_fdm_diff_p);
						}

						err_cnt++;
					}
				}
			}

		} else {
			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((fdm_noise_n_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_p[iArrayIndex]) \
							|| (fdm_noise_n_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_n[iArrayIndex])) {
						fdm_noise_p_result = -NVT_MP_FAIL;
						fdm_noise_p_record[iArrayIndex] = 1;
						TPD_INFO("FDM Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n",
							 i, j, fdm_noise_n_data[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_n[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_p[iArrayIndex]);

						if (!err_cnt) {
							TPD_INFO(
								"FDM Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								fdm_noise_n_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}
			}

			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((fdm_noise_n_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_p[iArrayIndex]) \
							|| (fdm_noise_n_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_n[iArrayIndex])) {
						fdm_noise_n_result = -NVT_MP_FAIL;
						fdm_noise_n_record[iArrayIndex] = 1;
						TPD_INFO("FDM Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n",
							 i, j, fdm_noise_n_data[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_n[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_p[iArrayIndex]);

						if (!err_cnt) {
							TPD_INFO(
								"FDM Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								fdm_noise_n_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}
			}
		}

	} else {
		TPD_INFO("fdm_diff_rawdata_n || fdm_diff_rawdata_p is NULL\n");
	}

TEST_END:
MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&fdm_noise_p_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&fdm_noise_n_data, buf_len);

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&fdm_noise_n_record,
			  record_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&fdm_noise_p_record,
			  record_len);
	TPD_INFO("%s -\n", __func__);
	return (fdm_noise_p_result + fdm_noise_p_result);
}

static int nvt_fdm_rawdata_test(struct seq_file *s, void *chip_data,
				struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int32_t *fdm_raw_data = NULL;
	int32_t iArrayIndex = 0;
	int8_t fdm_rawdata_result = -NVT_MP_UNKNOWN;
	uint8_t data_buf[64] = {0};
	uint8_t *fdm_raw_record = NULL;
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	int err_cnt = 0;
	int doze_x_num = chip_info->p_nvt_test_para->doze_x_channel;
	int doze_y_num = chip_info->p_nvt_test_para->doze_y_channel;
	record_len = doze_x_num * doze_y_num * sizeof(uint8_t);
	buf_len	= doze_x_num * doze_y_num * sizeof(int32_t);

	TPD_INFO("%s +\n", __func__);
	fdm_raw_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!fdm_raw_data) {
		TPD_INFO("fdm_raw_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	fdm_raw_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
					 GFP_KERNEL);

	if (!fdm_raw_record) {
		TPD_INFO("fdm_raw_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}


	/*---FDM Rawdata Test---*/
	fdm_rawdata_result = NVT_MP_PASS;

	if (nvt_read_doze_baseline(chip_info, fdm_raw_data, buf_len) != 0) {
		TPD_INFO("read FDM FW Rawdata failed!\n");
		fdm_rawdata_result = -NVT_MP_FAIL_READ_DATA;
		err_cnt++;
		goto TEST_END;
	}


	if (chip_info->p_nvt_autotest_offset->fdm_rawdata_n
			&& chip_info->p_nvt_autotest_offset->fdm_rawdata_p) {
		snprintf(data_buf, 64, "%s\n", "[NVT FDM RAW DATA]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
			strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(fdm_raw_data, chip_info, nvt_testdata,
			doze_x_num,
			doze_x_num * doze_y_num);

		if ((chip_info->p_nvt_test_para->config_lmt_fdm_rawdata_p != 0)
				&& (chip_info->p_nvt_test_para->config_lmt_fdm_rawdata_n != 0)) {
			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((fdm_raw_data[iArrayIndex] >
							chip_info->p_nvt_test_para->config_lmt_fdm_rawdata_p) \
							|| (fdm_raw_data[iArrayIndex] <
								chip_info->p_nvt_test_para->config_lmt_fdm_rawdata_n)) {
						fdm_rawdata_result = -NVT_MP_FAIL;
						fdm_raw_record[iArrayIndex] = 1;
						TPD_INFO("FDM FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
							 fdm_raw_data[iArrayIndex], chip_info->p_nvt_test_para->config_lmt_fdm_rawdata_n,
							 chip_info->p_nvt_test_para->config_lmt_fdm_rawdata_p);

						if (!err_cnt) {
						}

						err_cnt++;
					}
				}
			}

		} else {
			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((fdm_raw_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->fdm_rawdata_p[iArrayIndex]) \
							|| (fdm_raw_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->fdm_rawdata_n[iArrayIndex])) {
						fdm_rawdata_result = -NVT_MP_FAIL;
						fdm_raw_record[iArrayIndex] = 1;
						TPD_INFO("FDM FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
							 fdm_raw_data[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->fdm_rawdata_n[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->fdm_rawdata_p[iArrayIndex]);

						if (!err_cnt) {
						}

						err_cnt++;
					}
				}
			}
		}

	} else {
		TPD_INFO("fdm_rawdata_n || fdm_rawdata_p is NULL\n");
	}

TEST_END:
MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&fdm_raw_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&fdm_raw_record, record_len);
	TPD_INFO("%s -\n", __func__);
	return fdm_rawdata_result;
}

static int nvt_black_screen_test_preoperation(struct seq_file *s,
		void *chip_data, struct auto_testdata *nvt_testdata,
		struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	struct touchpanel_data *ts = chip_info->ts;
	struct auto_test_header *ph = NULL;
	int i = 0;
	int ret = -1;
	char *p_node = NULL;
	char *fw_name_test = NULL;
	char *postfix = "_TEST.bin";
	uint8_t copy_len = 0;
	uint8_t  data_buf[128] = {0};
	uint32_t *p_item_offset = 0;
	int item_cnt = 0;
	struct auto_test_item_header *item_head = NULL;
	int m = 0;
	int32_t *p_data32 = NULL;

	TPD_INFO("%s +\n", __func__);
	nvt_esd_check_enable(chip_info, false);

	/*update test firmware*/
	fw_name_test = tp_devm_kzalloc(&chip_info->s_client->dev, MAX_FW_NAME_LENGTH,
					   GFP_KERNEL);

	if (fw_name_test == NULL) {
		TPD_INFO("fw_name_test kzalloc error!\n");
		return 0;
	}

	p_node  = strstr(chip_info->fw_name, ".");
	copy_len = p_node - chip_info->fw_name;
	memcpy(fw_name_test, chip_info->fw_name, copy_len);
	strlcat(fw_name_test, postfix, MAX_FW_NAME_LENGTH);
	TPD_INFO("%s : fw_name_test is %s\n", __func__, fw_name_test);

	/*update test firmware*/
	ret = request_firmware(&ts->com_test_data.black_test_fw, fw_name_test,
				   chip_info->dev);

	if (ret != 0) {
		TPD_INFO("request test firmware failed! ret = %d\n", ret);
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&fw_name_test,
				  MAX_FW_NAME_LENGTH);
		return 0;
	}

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&fw_name_test,
			  MAX_FW_NAME_LENGTH);

	chip_info->need_judge_irq_throw = true;

/*
	ret = nvt_fw_update_sub(chip_info, ts->com_test_data.black_test_fw, 0);

	if (ret > 0) {
		TPD_INFO("fw update failed!\n");
		goto RELEASE_FIRMWARE;
	}
*/
	TPD_INFO("%s : Don't update test firmware during suspend\n", __func__);

	/*---set xdata index to EVENT BUF ADDR---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);
/*	ret = nvt_cmd_store(chip_info, CMD_OPEN_BLACK_GESTURE);
	TPD_INFO("%s: enable gesture %s !\n", __func__, (ret < 0) ? "failed" : "success");
*/
	msleep(500);	/* for FDM (500ms) */


	if (nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_NORMAL_RUN)) {
		TPD_INFO("check fw reset state failed!\n");
		goto RELEASE_FIRMWARE;
	}

	if (nvt_switch_FreqHopEnDis(chip_info, FREQ_HOP_DISABLE)) {
		TPD_INFO("switch frequency hopping disable failed!\n");
		goto RELEASE_FIRMWARE;
	}

	if (nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_NORMAL_RUN)) {
		TPD_INFO("check fw reset state failed!\n");
		goto RELEASE_FIRMWARE;
	}

	msleep(100);

	/*---Enter Test Mode---*/
	if (nvt_clear_fw_status(chip_info)) {
		TPD_INFO("clear fw status failed!\n");
		goto RELEASE_FIRMWARE;
	}

	nvt_change_mode(chip_info, TEST_MODE_2);

	if (nvt_check_fw_status(chip_info)) {
		TPD_INFO("check fw status failed!\n");
		goto RELEASE_FIRMWARE;
	}

	if (nvt_get_fw_info_noflash(chip_info)) {
		TPD_INFO("get fw info failed!\n");
		goto RELEASE_FIRMWARE;
	}


	TPD_INFO("%s: mallocing nvt_test_para\n", __func__);
	chip_info->p_nvt_test_para = NULL;
	chip_info->p_nvt_test_para = tp_devm_kzalloc(&chip_info->s_client->dev,
					 sizeof(struct nvt_autotest_para), GFP_KERNEL);

	if (!chip_info->p_nvt_test_para) {
		pr_err("[TP]: %s p_nvt_test_para in NULL\n", __func__);
		goto RELEASE_DATA;
	}

	TPD_INFO("%s: mallocing nvt_autotest_offset\n", __func__);
	chip_info->p_nvt_autotest_offset = NULL;
	chip_info->p_nvt_autotest_offset = tp_devm_kzalloc(&chip_info->s_client->dev,
					   sizeof(struct nvt_autotest_offset), GFP_KERNEL);

	if (!chip_info->p_nvt_autotest_offset) {
		pr_err("[TP]: %s p_nvt_autotest_offset is NULL\n", __func__);
		goto RELEASE_DATA;
	}

	ret = request_firmware(&ts->com_test_data.limit_fw, chip_info->test_limit_name,
				   &chip_info->s_client->dev);
	TPD_INFO("Roland--->fw path is %s\n", chip_info->test_limit_name);

	if (ret < 0) {
		TPD_INFO("Request firmware failed - %s (%d)\n", chip_info->test_limit_name,
			 ret);
		goto RELEASE_DATA;
	}

	/*get test data*/
	ph = (struct auto_test_header *)(ts->com_test_data.limit_fw->data);

	p_item_offset = (uint32_t *)(ts->com_test_data.limit_fw->data + 16);

	for (i = 0; i < 8 * sizeof(ph->test_item); i++) {
		if ((ph->test_item >> i) & 0x01) {
			item_cnt++;
		}
	}
	memset(data_buf, 0, sizeof(data_buf));
	snprintf(data_buf, 128, "FW Version Name:%s\n total test item = %d\n",
			 ts->panel_data.manufacture_info.version, item_cnt);
	tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
			  strlen(data_buf), nvt_testdata->pos);

	TPD_INFO("%s: total test item = %d\n", __func__, item_cnt);

	TPD_INFO("%s: populating nvt_test_offset\n", __func__);

	for (m = 0; m < item_cnt; m++) {
		TPD_INFO("%s: m[%d]\n", __func__, m);
		item_head = (struct auto_test_item_header *)(ts->com_test_data.limit_fw->data +
				p_item_offset[m]);

		if (item_head->item_limit_type == LIMIT_TYPE_NO_DATA) {
			TPD_INFO("[%d] incorrect item type: LIMIT_TYPE_NO_DATA\n", item_head->item_bit);

		} else if (item_head->item_limit_type == LIMIT_TYPE_TOP_FLOOR_DATA) {
			TPD_INFO("test item bit [%d]\n", item_head->item_bit);

			if (item_head->item_bit == TYPE_LPWG_RAWDATA) {
				chip_info->p_nvt_autotest_offset->lpwg_rawdata_p = (int32_t *)(
							ts->com_test_data.limit_fw->data + item_head->top_limit_offset);
				chip_info->p_nvt_autotest_offset->lpwg_rawdata_n = (int32_t *)(
							ts->com_test_data.limit_fw->data + item_head->floor_limit_offset);

			} else if (item_head->item_bit == TYPE_LPWG_DIFF_RAWDATA) {
				chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_p = (int32_t *)(
							ts->com_test_data.limit_fw->data + item_head->top_limit_offset);
				chip_info->p_nvt_autotest_offset->lpwg_diff_rawdata_n = (int32_t *)(
							ts->com_test_data.limit_fw->data + item_head->floor_limit_offset);
			}

		} else if (item_head->item_limit_type == LIMIT_TYPE_DOZE_FDM_DATA) {
			TPD_INFO("test item bit [%d]\n", item_head->item_bit);

			if (item_head->item_bit == TYPE_FDM_RAWDATA) {
				chip_info->p_nvt_autotest_offset->fdm_rawdata_p = (int32_t *)(
							ts->com_test_data.limit_fw->data + item_head->top_limit_offset);
				chip_info->p_nvt_autotest_offset->fdm_rawdata_n = (int32_t *)(
							ts->com_test_data.limit_fw->data + item_head->floor_limit_offset);

			} else if (item_head->item_bit == TYPE_FDM_DIFF_RAWDATA) {
				chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_p = (int32_t *)(
							ts->com_test_data.limit_fw->data + item_head->top_limit_offset);
				chip_info->p_nvt_autotest_offset->fdm_diff_rawdata_n = (int32_t *)(
							ts->com_test_data.limit_fw->data + item_head->floor_limit_offset);
			}

			if (item_head->para_num) {
				if (item_head->para_num == 1) {
					p_data32 = (int32_t *)(ts->com_test_data.limit_fw->data + p_item_offset[m] +
							sizeof(struct auto_test_item_header));
					chip_info->p_nvt_test_para->doze_x_channel = p_data32[0];
					chip_info->p_nvt_test_para->doze_y_channel = chip_info->hw_res->rx_num;
				} else if (item_head->para_num == 2) {
					p_data32 = (int32_t *)(ts->com_test_data.limit_fw->data + p_item_offset[m] +
							sizeof(struct auto_test_item_header));
					chip_info->p_nvt_test_para->doze_x_channel = p_data32[0];
					chip_info->p_nvt_test_para->doze_y_channel = p_data32[1];
				}
				TPD_INFO("doze_x_channel = %d\n", chip_info->p_nvt_test_para->doze_x_channel);
				TPD_INFO("doze_y_channel = %d\n", chip_info->p_nvt_test_para->doze_y_channel);
			}
		}
	}

	TPD_INFO("%s: populating nvt_test_para\n", __func__);
	chip_info->p_nvt_test_para->config_lmt_short_rawdata_p		= 0;
	chip_info->p_nvt_test_para->config_lmt_short_rawdata_n		= 0;
	chip_info->p_nvt_test_para->config_diff_test_frame			= 50;
	chip_info->p_nvt_test_para->config_lmt_fw_diff_p			  = 0;
	chip_info->p_nvt_test_para->config_lmt_fw_diff_n			  = 0;
	chip_info->p_nvt_test_para->config_lmt_fw_cc_p				= 0;
	chip_info->p_nvt_test_para->config_lmt_fw_cc_n				= 0;
	chip_info->p_nvt_test_para->config_lmt_fw_digital_p		   = 0;
	chip_info->p_nvt_test_para->config_lmt_fw_digital_n		   = 0;
	chip_info->p_nvt_test_para->config_lmt_doze_rawdata_p		 = 0;
	chip_info->p_nvt_test_para->config_lmt_doze_rawdata_n		 = 0;
	chip_info->p_nvt_test_para->config_doze_noise_test_frame	  = 50;
	chip_info->p_nvt_test_para->config_lmt_doze_diff_p			= 0;
	chip_info->p_nvt_test_para->config_lmt_doze_diff_n			= 0;
	chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_p		 = 0;
	chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_n		 = 0;
	chip_info->p_nvt_test_para->config_lmt_lpwg_diff_p			= 0;
	chip_info->p_nvt_test_para->config_lmt_lpwg_diff_n			= 0;
	chip_info->p_nvt_test_para->config_lmt_fdm_rawdata_p		  = 0;
	chip_info->p_nvt_test_para->config_lmt_fdm_rawdata_n		  = 0;
	chip_info->p_nvt_test_para->config_fdm_noise_test_frame	   = 50;
	chip_info->p_nvt_test_para->config_lmt_fdm_diff_p			 = 0;
	chip_info->p_nvt_test_para->config_lmt_fdm_diff_n			 = 0;
	TPD_INFO("%s -\n", __func__);
	return 0;

RELEASE_DATA:
	tp_devm_kfree(&chip_info->s_client->dev,
			  (void **)&chip_info->p_nvt_autotest_offset, sizeof(struct nvt_autotest_offset));
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&chip_info->p_nvt_test_para,
			  sizeof(struct nvt_autotest_para));

RELEASE_FIRMWARE:
	release_firmware(ts->com_test_data.black_test_fw);
	ts->com_test_data.black_test_fw = NULL;
	release_firmware(ts->com_test_data.limit_fw);
	ts->com_test_data.limit_fw = NULL;
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&fw_name_test,
			  MAX_FW_NAME_LENGTH);
	chip_info->need_judge_irq_throw = false;
	TPD_INFO("%s - [abnormal]\n", __func__);

	return -1;
}

static int nvt_black_screen_test_endoperation(struct seq_file *s,
		void *chip_data, struct auto_testdata *nvt_testdata,
		struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	struct touchpanel_data *ts = chip_info->ts;

	tp_devm_kfree(&chip_info->s_client->dev,
			  (void **)&chip_info->p_nvt_autotest_offset, sizeof(struct nvt_autotest_offset));
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&chip_info->p_nvt_test_para,
			  sizeof(struct nvt_autotest_para));

	release_firmware(ts->com_test_data.black_test_fw);
	ts->com_test_data.black_test_fw = NULL;
	release_firmware(ts->com_test_data.limit_fw);
	ts->com_test_data.limit_fw = NULL;
	chip_info->need_judge_irq_throw = false;

	return 0;
}


static void nvt_set_gesture_state(void *chip_data, int state)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	chip_info->gesture_state = state;
}

static struct oplus_touchpanel_operations nvt_ops = {
	.ftm_process				= nvt_ftm_process,
	.reset					  = nvt_reset,
	.power_control			  = nvt_power_control,
	.get_chip_info			  = nvt_get_chip_info,
	.trigger_reason			 = nvt_trigger_reason,
	.get_touch_points		   = nvt_get_touch_points,
	.get_pen_points			 = nvt_get_pen_points,
	.get_gesture_info		   = nvt_get_gesture_info,
	.mode_switch				= nvt_mode_switch,
	.fw_check				   = nvt_fw_check,
	.fw_update				  = nvt_fw_update,
	.get_vendor				 = nvt_get_vendor,
	.esd_handle				 = nvt_esd_handle,
	.reset_gpio_control		 = nvt_reset_gpio_control,
	.set_gesture_state		  = nvt_set_gesture_state,
	.notify_pencil_type         = nvt_notify_pencil_type,
	.ftm_process_extra		  = NULL,
};

static void nvt_data_read(struct seq_file *s,
			struct chip_data_nt36523 *chip_info, DEBUG_READ_TYPE read_type)
{
	int ret = -1;
	int i, j;
	uint8_t pipe;
	int32_t *xdata = NULL;
	int32_t buf_len = 0;

	TPD_INFO("nvt clear fw status start\n");
	ret = nvt_clear_fw_status(chip_info);

	if (ret < 0) {
		TPD_INFO("clear_fw_status error, return\n");
		return;
	}

	nvt_change_mode(chip_info, TEST_MODE_2);
	TPD_INFO("nvt check fw status start\n");
	ret = nvt_check_fw_status(chip_info);

	if (ret < 0) {
		TPD_INFO("check_fw_status error, return\n");
		return;
	}

	TPD_INFO("nvt get fw info start");
	ret = nvt_get_fw_info_noflash(chip_info);

	if (ret < 0) {
		TPD_INFO("get_fw_info error, return\n");
		return;
	}

	buf_len = chip_info->hw_res->tx_num * chip_info->hw_res->rx_num *
			sizeof(int32_t);
	xdata = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!xdata) {
		TPD_INFO("%s, malloc memory failed\n", __func__);
		return;
	}

	pipe = nvt_get_fw_pipe_noflash(chip_info);
	TPD_INFO("nvt_get_fw_pipe:%d\n", pipe);

	switch (read_type) {
	case NVT_RAWDATA:
		seq_printf(s, "raw_data:\n");

		if (pipe == 0) {
			nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->RAW_PIPE0_ADDR, xdata,
					   buf_len);

		} else {
			nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->RAW_PIPE1_ADDR, xdata,
					   buf_len);
		}

		break;

	case NVT_DIFFDATA:
		seq_printf(s, "diff_data:\n");

		if (pipe == 0) {
			nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR, xdata,
					   buf_len);

		} else {
			nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE1_ADDR, xdata,
					   buf_len);
		}

		break;

	case NVT_BASEDATA:
		seq_printf(s, "basline_data:\n");
		nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->BASELINE_ADDR, xdata,
				   buf_len);
		break;

	default:
		seq_printf(s, "read type not support\n");
		break;
	}

	nvt_change_mode(chip_info, NORMAL_MODE);
	TPD_INFO("change normal mode end\n");

	/*print all data*/
	for (i = 0; i < chip_info->hw_res->rx_num; i++) {
		seq_printf(s, "[%2d]", i);

		for (j = 0; j < chip_info->hw_res->tx_num; j++) {
			seq_printf(s, "%5d, ", xdata[i * chip_info->hw_res->tx_num + j]);
		}

		seq_printf(s, "\n");
	}

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&xdata, buf_len);
}

int32_t nvt_set_pen_inband_mode_1(struct chip_data_nt36523 *chip_info, uint8_t freq_idx, uint8_t x_term)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 5;

	/*---set xdata index to EVENT BUF ADDR---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

	/*---set mode---*/
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0xC1;
	buf[2] = 0x02;
	buf[3] = freq_idx;
	buf[4] = x_term;
	CTP_SPI_WRITE(chip_info->s_client, buf, 5);

	for (i = 0; i < retry; i++) {
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0x00)
			break;

		msleep(20);
	}

	if (i >= retry) {
		TPD_INFO("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -EPERM;
	} else {
		return 0;
	}
}

int32_t nvt_set_pen_normal_mode(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 5;

	/*---set xdata index to EVENT BUF ADDR---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

	/*---set mode---*/
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0xC1;
	buf[2] = 0x04;
	CTP_SPI_WRITE(chip_info->s_client, buf, 3);

	for (i = 0; i < retry; i++) {
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		CTP_SPI_READ(chip_info->s_client, buf, 2);

		if (buf[1] == 0x00)
			break;

		msleep(20);
	}

	if (i >= retry) {
		TPD_INFO("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -EPERM;
	} else {
		return 0;
	}
}

/*static void nvt_pen_data_read(struct seq_file *s, struct chip_data_nt36523 *chip_info, DEBUG_READ_TYPE read_type)
{
	int ret = -1;
	int i, j;
	int32_t *xdata_pen_tip_x = NULL;
	int32_t *xdata_pen_tip_y = NULL;
	int32_t *xdata_pen_ring_x = NULL;
	int32_t *xdata_pen_ring_y = NULL;
	int tx_num = chip_info->hw_res->tx_num;
	int rx_num = chip_info->hw_res->rx_num;
	int pen_tx_num = chip_info->hw_res->pen_tx_num;
	int pen_rx_num = chip_info->hw_res->pen_rx_num;

	TPD_INFO("nvt pen data read start\n");

	ret = nvt_cmd_store(chip_info, 0x61);
	if (ret < 0) {
		TPD_INFO("CMD 0x61 error, return\n");
		return;
	}

	ret = nvt_set_pen_inband_mode_1(chip_info, 0xFF, 0x00);
	if (ret < 0) {
		TPD_INFO("nvt_set_pen_inband_mode_1 error, return\n");
		return;
	}

	ret = nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_NORMAL_RUN);
	if (ret < 0) {
		TPD_INFO("nvt_check_fw_reset_state error, return\n");
		return;
	}

	ret = nvt_clear_fw_status(chip_info);
	if (ret < 0) {
		TPD_INFO("clear_fw_status error, return\n");
		return;
	}

	nvt_change_mode(chip_info, TEST_MODE_2);

	ret = nvt_check_fw_status(chip_info);
	if (ret < 0) {
		TPD_INFO("check_fw_status error, return\n");
		return;
	}

	TPD_INFO("nvt get fw info start");
	ret = nvt_get_fw_info_noflash(chip_info);
	if (ret < 0) {
		TPD_INFO("get_fw_info error, return\n");
		return;
	}

	xdata_pen_tip_x = tp_devm_kzalloc(&chip_info->s_client->dev, tx_num * pen_rx_num * sizeof(int32_t), GFP_KERNEL);
	if (!xdata_pen_tip_x) {
		TPD_INFO("%s, xdata_pen_tip_x malloc memory failed\n", __func__);
		return;
	}

	xdata_pen_tip_y = tp_devm_kzalloc(&chip_info->s_client->dev, pen_tx_num * rx_num * sizeof(int32_t), GFP_KERNEL);
	if (!xdata_pen_tip_y) {
		TPD_INFO("%s, xdata_pen_tip_y malloc memory failed\n", __func__);
		return;
	}

	xdata_pen_ring_x = tp_devm_kzalloc(&chip_info->s_client->dev, tx_num * pen_rx_num * sizeof(int32_t), GFP_KERNEL);
	if (!xdata_pen_ring_x) {
		TPD_INFO("%s, xdata_pen_ring_x malloc memory failed\n", __func__);
		return;
	}

	xdata_pen_ring_y = tp_devm_kzalloc(&chip_info->s_client->dev, pen_tx_num * rx_num * sizeof(int32_t), GFP_KERNEL);
	if (!xdata_pen_ring_y) {
		TPD_INFO("%s, xdata_pen_ring_y malloc memory failed\n", __func__);
		return;
	}

	switch (read_type) {
	case NVT_RAWDATA:
		seq_printf(s, "pen_raw_data:\n");
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_TIP_X_ADDR,
				xdata_pen_tip_x, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_TIP_Y_ADDR,
				xdata_pen_tip_y, pen_tx_num * rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_RING_X_ADDR,
				xdata_pen_ring_x, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_RING_Y_ADDR,
				xdata_pen_ring_y, pen_tx_num * rx_num);
		break;
	case NVT_DIFFDATA:
		seq_printf(s, "pen_diff_data:\n");
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_TIP_X_ADDR,
				xdata_pen_tip_x, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_TIP_Y_ADDR,
				xdata_pen_tip_y, pen_tx_num * rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_RING_X_ADDR,
				xdata_pen_ring_x, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_RING_Y_ADDR,
				xdata_pen_ring_y, pen_tx_num * rx_num);
		break;
	case NVT_BASEDATA:
		seq_printf(s, "pen_basline_data:\n");
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_TIP_X_ADDR,
				xdata_pen_tip_x, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_TIP_Y_ADDR,
				xdata_pen_tip_y, pen_tx_num * rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_RING_X_ADDR,
				xdata_pen_ring_x, tx_num * pen_rx_num);
		nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_RING_Y_ADDR,
				xdata_pen_ring_y, pen_tx_num * rx_num);
		break;
	default:
		seq_printf(s, "read type not support\n");
		break;
	}

	nvt_change_mode(chip_info, NORMAL_MODE);

	ret = nvt_set_pen_normal_mode(chip_info);
	if (ret < 0) {
		TPD_INFO("nvt_set_pen_inband_mode_1 error, return\n");
		return;
	}

	ret = nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_NORMAL_RUN);
	if (ret < 0) {
		TPD_INFO("nvt_check_fw_reset_state error, return\n");
		return;
	}

	ret = nvt_cmd_store(chip_info, 0x62);
	if (ret < 0) {
		TPD_INFO("CMD 0x62 error, return\n");
		return;
	}

	TPD_INFO("nvt pen data read end\n");

	seq_printf(s, "Tip X:\n");
	for (i = 0; i < pen_rx_num; i++) {
		seq_printf(s, "[%2d]", i);
		for (j = 0; j < tx_num; j++) {
			seq_printf(s, "%5d, ", xdata_pen_tip_x[i * tx_num + j]);
		}
		seq_printf(s, "\n");
	}

	seq_printf(s, "Tip Y:\n");
	for (i = 0; i < rx_num; i++) {
		seq_printf(s, "[%2d]", i);
		for (j = 0; j < pen_tx_num; j++) {
			seq_printf(s, "%5d, ", xdata_pen_tip_y[i * pen_tx_num + j]);
		}
		seq_printf(s, "\n");
	}

	seq_printf(s, "Ring X:\n");
	for (i = 0; i < pen_rx_num; i++) {
		seq_printf(s, "[%2d]", i);
		for (j = 0; j < tx_num; j++) {
			seq_printf(s, "%5d, ", xdata_pen_ring_x[i * tx_num + j]);
		}
		seq_printf(s, "\n");
	}

	seq_printf(s, "Ring Y:\n");
	for (i = 0; i < rx_num; i++) {
		seq_printf(s, "[%2d]", i);
		for (j = 0; j < pen_tx_num; j++) {
			seq_printf(s, "%5d, ", xdata_pen_ring_y[i * pen_tx_num + j]);
		}
		seq_printf(s, "\n");
	}

	tp_devm_kfree(&chip_info->s_client->dev, (void **)xdata_pen_tip_x, tx_num * pen_rx_num * sizeof(int32_t));
	tp_devm_kfree(&chip_info->s_client->dev, (void **)xdata_pen_tip_y, pen_tx_num * rx_num * sizeof(int32_t));
	tp_devm_kfree(&chip_info->s_client->dev, (void **)xdata_pen_ring_x, tx_num * pen_rx_num * sizeof(int32_t));
	tp_devm_kfree(&chip_info->s_client->dev, (void **)xdata_pen_ring_y, pen_tx_num * rx_num * sizeof(int32_t));
}*/

#ifdef CONFIG_OPLUS_TP_APK
static void nvt_debug_data_read(struct seq_file *s,
				struct chip_data_nt36523 *chip_info, DEBUG_READ_TYPE read_type)
{
	int ret = -1;
	int i;
	int j;
	int32_t *xdata = NULL;
	int32_t buf_len = 0;

	TPD_INFO("nvt get fw info start");
	ret = nvt_get_fw_info_noflash(chip_info);

	if (ret < 0) {
		TPD_INFO("get_fw_info error, return\n");
		return;
	}

	buf_len = chip_info->hw_res->tx_num * chip_info->hw_res->rx_num *
				sizeof(int32_t);
	xdata = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!xdata) {
		TPD_INFO("%s, malloc memory failed\n", __func__);
		return;
	}

	switch (read_type) {
	case NVT_DEBUG_FINGER_DOWN_DIFFDATA:
		seq_printf(s, "debug finger down diff data:\n");
		nvt_read_debug_mdata(chip_info, NVT_MMAP_DEBUG_FINGER_DOWN_DIFFDATA, xdata,
					 buf_len);
		break;

	case NVT_DEBUG_STATUS_CHANGE_DIFFDATA:
		seq_printf(s, "debug status change diff data:\n");
		nvt_read_debug_mdata(chip_info, NVT_MMAP_DEBUG_STATUS_CHANGE_DIFFDATA, xdata,
					 buf_len);
		break;

	default:
		break;
	}

	/*print all data*/
	for (i = 0; i < chip_info->hw_res->rx_num; i++) {
		seq_printf(s, "[%2d]", i);

		for (j = 0; j < chip_info->hw_res->tx_num; j++) {
			seq_printf(s, "%5d, ", xdata[i * chip_info->hw_res->tx_num + j]);
		}

		seq_printf(s, "\n");
	}

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&xdata, buf_len);
}
#endif /* end of CONFIG_OPLUS_TP_APK*/

static void nvt_delta_read(struct seq_file *s, void *chip_data)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	nvt_esd_check_update_timer(chip_info);

	nvt_data_read(s, chip_info, NVT_DIFFDATA);

#ifdef CONFIG_OPLUS_TP_APK

	if (chip_info->debug_mode_sta) {
		nvt_debug_data_read(s, chip_info, NVT_DEBUG_FINGER_DOWN_DIFFDATA);
	}

#endif /* end of CONFIG_OPLUS_TP_APK*/
}

static void nvt_baseline_read(struct seq_file *s, void *chip_data)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	nvt_esd_check_update_timer(chip_info);

	nvt_data_read(s, chip_info, NVT_BASEDATA);
	nvt_data_read(s, chip_info, NVT_RAWDATA);
}

/*static void nvt_pen_delta_read(struct seq_file *s, void *chip_data)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	nvt_esd_check_update_timer(chip_info);

	if (!chip_info->ts->pen_support) {
		TPD_INFO("not support\n");
		return;
	}

	nvt_pen_data_read(s, chip_info, NVT_DIFFDATA);
}

static void nvt_pen_baseline_read(struct seq_file *s, void *chip_data)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	nvt_esd_check_update_timer(chip_info);

	if (!chip_info->ts->pen_support) {
		TPD_INFO("not support\n");
		return;
	}

	nvt_pen_data_read(s, chip_info, NVT_BASEDATA);
	nvt_pen_data_read(s, chip_info, NVT_RAWDATA);
}*/

#ifdef CONFIG_OPLUS_TP_APK
static __maybe_unused void nvt_dbg_diff_finger_down_read(struct seq_file *s, void *chip_data)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	nvt_debug_data_read(s, chip_info, NVT_DEBUG_FINGER_DOWN_DIFFDATA);
}

static __maybe_unused void nvt_dbg_diff_status_change_read(struct seq_file *s, void *chip_data)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	nvt_debug_data_read(s, chip_info, NVT_DEBUG_STATUS_CHANGE_DIFFDATA);
}
#endif /* end of CONFIG_OPLUS_TP_APK*/

static void nvt_read_fw_history(struct seq_file *s, void *chip_data, uint32_t NVT_MMAP_HISTORY_ADDR)
{
	uint8_t i = 0;
	uint8_t buf[66];
	char str[128];
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	nvt_set_page(chip_info, NVT_MMAP_HISTORY_ADDR);

	buf[0] = (uint8_t) (NVT_MMAP_HISTORY_ADDR & 0x7F);
	CTP_SPI_READ(chip_info->s_client, buf, 65);	/*read 64bytes history*/

	/*print all data*/
	seq_printf(s, "fw history 0x%X: \n", NVT_MMAP_HISTORY_ADDR);
	for (i = 0; i < 4; i++) {
		snprintf(str, sizeof(str), "%02X %02X %02X %02X %02X %02X %02X %02X    %02X %02X %02X %02X %02X %02X %02X %02X\n",
		buf[1+i*16], buf[2+i*16], buf[3+i*16], buf[4+i*16],
		buf[5+i*16], buf[6+i*16], buf[7+i*16], buf[8+i*16],
		buf[9+i*16], buf[10+i*16], buf[11+i*16], buf[12+i*16],
		buf[13+i*16], buf[14+i*16], buf[15+i*16], buf[16+i*16]);
		seq_printf(s, "%s", str);
	}
}


static void nvt_main_register_read(struct seq_file *s, void *chip_data)
{
	uint8_t buf[4];
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;

	if (!chip_info) {
		return;
	}

	nvt_esd_check_update_timer(chip_info);

	/*read fw history*/
	nvt_read_fw_history(s, chip_data, chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT0);
	nvt_read_fw_history(s, chip_data, chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT1);
	if (chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT2 != 0)
		nvt_read_fw_history(s, chip_data, chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT2);
	if (chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT3 != 0)
		nvt_read_fw_history(s, chip_data, chip_info->trim_id_table.mmap->MMAP_HISTORY_EVENT3);

	/*---set xdata index to EVENT BUF ADDR---*/
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	/*---read cmd status---*/
	buf[0] = 0x5E;
	buf[1] = 0xFF;
	buf[2] = 0xFF;
	CTP_SPI_READ(chip_info->s_client, buf, 3);

	seq_printf(s, "PWR_FLAG:%d\n", (buf[1] >> PWR_FLAG) & 0x01);
	seq_printf(s, "EDGE_REJECT:%d\n", (buf[1] >> EDGE_REJECT_L) & 0x03);
	seq_printf(s, "JITTER_FLAG:%d\n", (buf[1] >> JITTER_FLAG) & 0x01);
	seq_printf(s, "HEADSET_FLAG:%d\n", (buf[1] >> HEADSET_FLAG) & 0x01);
	seq_printf(s, "HOPPING_FIX_FREQ_FLAG:%d\n",
		   (buf[1] >> HOPPING_FIX_FREQ_FLAG) & 0x01);
	seq_printf(s, "HOPPING_POLLING_FLAG:%d\n",
		   (buf[1] >> HOPPING_POLLING_FLAG) & 0x01);

	seq_printf(s, "DEBUG_DIFFDATA_FLAG:%d\n",
		   (buf[2] >> DEBUG_DIFFDATA_FLAG) & 0x01);
	seq_printf(s, "DEBUG_WKG_COORD_FLAG:%d\n",
		   (buf[2] >> DEBUG_WKG_COORD_FLAG) & 0x01);
	seq_printf(s, "DEBUG_WKG_COORD_RECORD_FLAG:%d\n",
		   (buf[2] >> DEBUG_WKG_COORD_RECORD_FLAG) & 0x01);
	seq_printf(s, "DEBUG_WATER_POLLING_FLAG:%d\n",
		   (buf[2] >> DEBUG_WATER_POLLING_FLAG) & 0x01);
}

static struct debug_info_proc_operations debug_info_proc_ops = {
	/*.limit_read		 = nvt_limit_read_std,*/
	.baseline_read	  = nvt_baseline_read,
	.delta_read		 = nvt_delta_read,
	/*.pen_delta_read = nvt_pen_delta_read,*/
	/*.pen_baseline_read = nvt_pen_baseline_read,*/
	.main_register_read = nvt_main_register_read,
};

static void nvt_enable_short_test(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};

	/*---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	/*---enable short test--- */
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0x43;
	buf[2] = 0xAA;
	buf[3] = 0x02;
	buf[4] = 0x00;
	CTP_SPI_WRITE(chip_info->s_client, buf, 5);
}

static int32_t nvt_read_fw_short(struct chip_data_nt36523 *chip_info,
				int32_t *xdata, int32_t xdata_len)
{
	uint32_t raw_pipe_addr = 0;
	uint8_t *rawdata_buf = NULL;
	uint32_t x = 0;
	uint32_t y = 0;
	uint8_t buf[128] = {0};
	int32_t iArrayIndex = 0;

	if (xdata_len / sizeof(int32_t) < chip_info->hw_res->tx_num *
			chip_info->hw_res->rx_num) {
		TPD_INFO("read fw short buffer(%d) less than data size(%d)\n", xdata_len,
			 chip_info->hw_res->tx_num * chip_info->hw_res->rx_num);
		return -1;
	}

	/* ---Enter Test Mode--- */
	if (nvt_clear_fw_status(chip_info)) {
	return -EAGAIN;
	}

	nvt_enable_short_test(chip_info);

	if (nvt_polling_hand_shake_status(chip_info)) {
	return -EAGAIN;
	}

	rawdata_buf = (uint8_t *)tp_devm_kzalloc(&chip_info->s_client->dev,
			chip_info->hw_res->tx_num * chip_info->hw_res->rx_num * 2, GFP_KERNEL);

	if (!rawdata_buf) {
	TPD_INFO("kzalloc for rawdata_buf failed!\n");
	return -ENOMEM;
	}

	if (nvt_get_fw_pipe(chip_info) == 0) {
		raw_pipe_addr = chip_info->trim_id_table.mmap->RAW_PIPE0_ADDR;

	} else {
		raw_pipe_addr = chip_info->trim_id_table.mmap->RAW_PIPE1_ADDR;
	}

	for (y = 0; y < chip_info->hw_res->rx_num; y++) {
		/*---change xdata index---*/
		nvt_set_page(chip_info, raw_pipe_addr + y * chip_info->hw_res->tx_num * 2);
		buf[0] = (uint8_t)((raw_pipe_addr + y * chip_info->hw_res->tx_num * 2) & 0xFF);
		CTP_SPI_READ(chip_info->s_client, buf, chip_info->hw_res->tx_num * 2 + 1);
		memcpy(rawdata_buf + y * chip_info->hw_res->tx_num * 2, buf + 1,
			   chip_info->hw_res->tx_num * 2);
	}

	for (y = 0; y < chip_info->hw_res->rx_num; y++) {
		for (x = 0; x < chip_info->hw_res->tx_num; x++) {
			iArrayIndex = y * chip_info->hw_res->tx_num + x;
			xdata[iArrayIndex] = (int16_t)(rawdata_buf[iArrayIndex * 2] + 256 *
							   rawdata_buf[iArrayIndex * 2 + 1]);
		}
	}

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&rawdata_buf,
			  chip_info->hw_res->tx_num * chip_info->hw_res->rx_num * 2);

	/*---Leave Test Mode--- */
	nvt_change_mode(chip_info, NORMAL_MODE);
	return 0;
}

static void nvt_enable_open_test(struct chip_data_nt36523 *chip_info)
{
	uint8_t buf[8] = {0};

	/*---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(chip_info, chip_info->trim_id_table.mmap->EVENT_BUF_ADDR);

	/*---enable open test--- */
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0x45;
	buf[2] = 0xAA;
	buf[3] = 0x02;
	buf[4] = 0x00;
	CTP_SPI_WRITE(chip_info->s_client, buf, 5);
}

static int32_t nvt_read_fw_open(struct chip_data_nt36523 *chip_info,
				int32_t *xdata, int32_t xdata_len)
{
	uint32_t raw_pipe_addr = 0;
	uint8_t *rawdata_buf = NULL;
	uint32_t x = 0;
	uint32_t y = 0;
	uint32_t tx_num = chip_info->hw_res->tx_num;
	uint32_t rx_num = chip_info->hw_res->rx_num;
	uint8_t buf[128] = {0};

	if (xdata_len / sizeof(int32_t) < chip_info->hw_res->tx_num *
			chip_info->hw_res->rx_num) {
		TPD_INFO("read fw open buffer(%d) less than data size(%d)\n", xdata_len,
			 chip_info->hw_res->tx_num * chip_info->hw_res->rx_num);
	return -1;
	}

	/* ---Enter Test Mode--- */
	if (nvt_clear_fw_status(chip_info)) {
	return -EAGAIN;
	}

	nvt_enable_open_test(chip_info);

	if (nvt_polling_hand_shake_status(chip_info)) {
	return -EAGAIN;
	}

	rawdata_buf = (uint8_t *)tp_devm_kzalloc(&chip_info->s_client->dev,
			tx_num * rx_num * 2, GFP_KERNEL);
	if (!rawdata_buf) {
	TPD_INFO("kzalloc for rawdata_buf failed!\n");
	return -ENOMEM;
	}

	if (nvt_get_fw_pipe(chip_info) == 0) {
		raw_pipe_addr = chip_info->trim_id_table.mmap->RAW_PIPE0_ADDR;

	} else {
		raw_pipe_addr = chip_info->trim_id_table.mmap->RAW_PIPE1_ADDR;
	}

	for (y = 0; y < rx_num; y++) {
	/* ---change xdata index--- */
	nvt_set_page(chip_info, raw_pipe_addr + y * tx_num * 2);
	buf[0] = (uint8_t)((raw_pipe_addr + y * tx_num * 2) & 0xFF);
	CTP_SPI_READ(chip_info->s_client, buf, tx_num * 2 + 1);
	memcpy(rawdata_buf + y * tx_num * 2, buf + 1, tx_num * 2);
	}

	for (y = 0; y < rx_num; y++) {
		for (x = 0; x < tx_num; x++) {
			/*xdata[(rx_num-y-1) * tx_num + (tx_num-x-1)] = (int16_t)((rawdata_buf[(y*tx_num + x)*2] + 256 * rawdata_buf[(y*tx_num + x)*2 + 1]));*/
			xdata[y * tx_num + x] = (int16_t)((rawdata_buf[(y * tx_num + x) * 2] + 256 *
							   rawdata_buf[(y * tx_num + x) * 2 + 1]));
		}
	}

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&rawdata_buf,
			  tx_num * rx_num * 2);

	/*---Leave Test Mode--- */
	nvt_change_mode(chip_info, NORMAL_MODE);
	return 0;
}


static int nvt_fw_rawdata_test(struct seq_file *s, void *chip_data,
				   struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	int32_t *raw_data = NULL;
	int32_t *pen_tip_x_data = NULL;
	int32_t *pen_tip_y_data = NULL;
	int32_t *pen_ring_x_data = NULL;
	int32_t *pen_ring_y_data = NULL;
	uint8_t *raw_record = NULL;
	uint8_t *pen_tip_x_record = NULL;
	uint8_t *pen_tip_y_record = NULL;
	uint8_t *pen_ring_x_record = NULL;
	uint8_t *pen_ring_y_record = NULL;
	uint8_t  data_buf[64] = {0};
	int i = 0, j = 0, buf_len = 0, record_len = 0;
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int8_t rawdata_result = -NVT_MP_UNKNOWN;
	int8_t pen_tip_x_result = -NVT_MP_UNKNOWN;
	int8_t pen_tip_y_result = -NVT_MP_UNKNOWN;
	int8_t pen_ring_x_result = -NVT_MP_UNKNOWN;
	int8_t pen_ring_y_result = -NVT_MP_UNKNOWN;
	int32_t iArrayIndex = 0;
	int err_cnt = 0;
	int rx_num = chip_info->hw_res->rx_num;
	int tx_num = chip_info->hw_res->tx_num;
	int pen_tx_num = chip_info->hw_res->pen_tx_num;
	int pen_rx_num = chip_info->hw_res->pen_rx_num;

	record_len = rx_num * tx_num * sizeof(uint8_t);
	buf_len	= rx_num * tx_num * sizeof(int32_t);

	TPD_INFO("%s +\n", __func__);

	if (chip_info->p_nvt_autotest_offset == NULL) {
		TPD_INFO("%s NULL point, return error\n", __func__);
		return -1;
	}

	raw_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!raw_data) {
		TPD_INFO("raw_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	raw_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

	if (!raw_record) {
		TPD_INFO("raw_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	/*---FW Rawdata Test---*/
	TPD_INFO("FW Rawdata Test\n");
	memset(data_buf, 0, sizeof(data_buf));
	snprintf(data_buf, 64, "%s\n", "[NVT RAW DATA]");
	tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
			  strlen(data_buf), nvt_testdata->pos);

	if (chip_info->p_nvt_autotest_offset->fw_rawdata_p
			&& chip_info->p_nvt_autotest_offset->fw_rawdata_n) {
		rawdata_result = NVT_MP_PASS;
		nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->BASELINE_ADDR,
				   raw_data, buf_len);
		nvt_output_data(raw_data, chip_info, nvt_testdata,
				tx_num, tx_num * rx_num);

		for (j = 0; j < rx_num; j++) {
			for (i = 0; i < tx_num; i++) {
				iArrayIndex = j * tx_num + i;

				if ((raw_data[iArrayIndex] >
						chip_info->p_nvt_autotest_offset->fw_rawdata_p[iArrayIndex]) \
						|| (raw_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->fw_rawdata_n[iArrayIndex])) {
					rawdata_result = -NVT_MP_FAIL;
					raw_record[iArrayIndex] = 1;
					TPD_INFO("rawdata Test failed at rawdata[%d][%d] = %d [%d,%d]\n",
						i, j, raw_data[iArrayIndex],
						chip_info->p_nvt_autotest_offset->fw_rawdata_n[iArrayIndex],
						chip_info->p_nvt_autotest_offset->fw_rawdata_p[iArrayIndex]);

					if (!err_cnt) {
						seq_printf(s, "rawdata Test failed at rawdata[%d][%d] = %d [%d,%d]\n",
								i, j, raw_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->fw_rawdata_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->fw_rawdata_p[iArrayIndex]);
					}

					err_cnt++;
				}
			}
		}

	} else {
		TPD_INFO("fw_rawdata_p || fw_rawdata_n is NULL\n");
	}

	if (chip_info->ts->pen_support) {
		TPD_INFO("Pen Rawdata Test \n");

		pen_tip_x_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_tip_x_data) {
			TPD_INFO("pen_tip_x_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_tip_y_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_tip_y_data) {
			TPD_INFO("pen_tip_y_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_x_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_ring_x_data) {
			TPD_INFO("pen_ring_x_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_y_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_ring_y_data) {
			TPD_INFO("pen_ring_y_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_tip_x_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_tip_x_record) {
			TPD_INFO("pen_tip_x_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_tip_y_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_tip_y_record) {
			TPD_INFO("pen_tip_y_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_x_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_ring_x_record) {
			TPD_INFO("pen_ring_x_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_y_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_ring_y_record) {
			TPD_INFO("pen_ring_y_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		/* pen tip x */
		memset(data_buf, 0, sizeof(data_buf));
		snprintf(data_buf, 64, "%s\n", "[NVT PEN TIP X RAW DATA]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				strlen(data_buf), nvt_testdata->pos);

		if (chip_info->p_nvt_autotest_offset->pen_tip_x_data_p
				&& chip_info->p_nvt_autotest_offset->pen_tip_x_data_n) {
			pen_tip_x_result = NVT_MP_PASS;

			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_TIP_X_ADDR,
					pen_tip_x_data, tx_num * pen_rx_num);
			nvt_output_data(pen_tip_x_data, chip_info, nvt_testdata,
					tx_num, tx_num * pen_rx_num);

			for (j = 0; j < pen_rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;

					if((pen_tip_x_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_tip_x_data_p[iArrayIndex]) \
							|| (pen_tip_x_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_tip_x_data_n[iArrayIndex])) {
						pen_tip_x_result = -NVT_MP_FAIL;
						pen_tip_x_record[iArrayIndex] = 1;
						TPD_INFO("pen tip x data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_x_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_x_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_x_data_p[iArrayIndex]);
						if (!err_cnt) {
							seq_printf(s, "pen tip x data Test failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_tip_x_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_x_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_x_data_p[iArrayIndex]);
						}
						err_cnt++;
					}
				}
			}

		} else {
			TPD_INFO("pen_tip_x_data_p || pen_tip_x_data_n is NULL\n");
		}

		/* pen tip y */
		memset(data_buf, 0, sizeof(data_buf));
		snprintf(data_buf, 64, "%s\n", "[NVT PEN TIP Y RAW DATA]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				strlen(data_buf), nvt_testdata->pos);

		if (chip_info->p_nvt_autotest_offset->pen_tip_y_data_p
				&& chip_info->p_nvt_autotest_offset->pen_tip_y_data_n) {
			pen_tip_y_result = NVT_MP_PASS;

			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_TIP_Y_ADDR,
					pen_tip_y_data, pen_tx_num * rx_num);
			nvt_output_data(pen_tip_y_data, chip_info, nvt_testdata,
					pen_tx_num, pen_tx_num * rx_num);

			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < pen_tx_num; i++) {
					iArrayIndex = j * pen_tx_num + i;
					if((pen_tip_y_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_tip_y_data_p[iArrayIndex]) \
							|| (pen_tip_y_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_tip_y_data_n[iArrayIndex])) {
						pen_tip_y_result = -NVT_MP_FAIL;
						pen_tip_y_record[iArrayIndex] = 1;
						TPD_INFO("pen tip y data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_y_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_y_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_y_data_p[iArrayIndex]);
						if (!err_cnt) {
							seq_printf(s, "pen tip y data Test failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_tip_y_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_y_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_y_data_p[iArrayIndex]);
						}
						err_cnt++;
					}
				}
			}

		} else {
			TPD_INFO("pen_tip_y_data_p || pen_tip_y_data_n is NULL\n");
		}

		/* pen ring x */
		memset(data_buf, 0, sizeof(data_buf));
		snprintf(data_buf, 64, "%s\n", "[NVT PEN RING X RAW DATA]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				strlen(data_buf), nvt_testdata->pos);

		if (chip_info->p_nvt_autotest_offset->pen_ring_x_data_p
				&& chip_info->p_nvt_autotest_offset->pen_ring_x_data_n) {
			pen_ring_x_result = NVT_MP_PASS;

			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_RING_X_ADDR,
					pen_ring_x_data, tx_num * pen_rx_num);
			nvt_output_data(pen_ring_x_data, chip_info, nvt_testdata,
					tx_num, tx_num * pen_rx_num);

			for (j = 0; j < pen_rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;

					if((pen_ring_x_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_ring_x_data_p[iArrayIndex]) \
							|| (pen_ring_x_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_ring_x_data_n[iArrayIndex])) {
						pen_ring_x_result = -NVT_MP_FAIL;
						pen_ring_x_record[iArrayIndex] = 1;
						TPD_INFO("pen ring x data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_x_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_x_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_x_data_p[iArrayIndex]);
						if (!err_cnt) {
							seq_printf(s, "pen ring x data Test failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_ring_x_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_x_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_x_data_p[iArrayIndex]);
						}
						err_cnt++;
					}
				}
			}

		} else {
			TPD_INFO("pen_ring_x_data_p || pen_ring_x_data_n is NULL\n");
		}

		/* pen ring y */
		memset(data_buf, 0, sizeof(data_buf));
		snprintf(data_buf, 64, "%s\n", "[NVT PEN RING Y RAW DATA]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				strlen(data_buf), nvt_testdata->pos);

		if (chip_info->p_nvt_autotest_offset->pen_ring_y_data_p
				&& chip_info->p_nvt_autotest_offset->pen_ring_y_data_n) {
			pen_ring_y_result = NVT_MP_PASS;

			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_BL_RING_Y_ADDR,
					pen_ring_y_data, pen_tx_num * rx_num);
			nvt_output_data(pen_ring_y_data, chip_info, nvt_testdata,
					pen_tx_num, pen_tx_num * rx_num);

			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < pen_tx_num; i++) {
					iArrayIndex = j * pen_tx_num + i;
					if((pen_ring_y_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_ring_y_data_p[iArrayIndex]) \
							|| (pen_ring_y_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_ring_y_data_n[iArrayIndex])) {
						pen_ring_y_result = -NVT_MP_FAIL;
						pen_ring_y_record[iArrayIndex] = 1;
						TPD_INFO("pen ring y data Test failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_y_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_y_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_y_data_p[iArrayIndex]);
						if (!err_cnt) {
							seq_printf(s, "pen ring y data Test failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_ring_y_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_y_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_y_data_p[iArrayIndex]);
						}
						err_cnt++;
					}
				}
			}

		} else {
			TPD_INFO("pen_ring_y_data_p || pen_ring_y_data_n is NULL\n");
		}
	} /* ts->pen_support */

MEM_ALLOC_ERR:
	if (chip_info->ts->pen_support) {
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_x_data, buf_len);
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_y_data, buf_len);
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_x_data, buf_len);
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_y_data, buf_len);
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_x_record, buf_len);
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_y_record, buf_len);
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_x_record, buf_len);
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_y_record, buf_len);
	}
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&raw_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&raw_record, buf_len);
	TPD_INFO("%s -\n", __func__);

	if (chip_info->ts->pen_support) {
		return (rawdata_result +
				pen_tip_x_result + pen_tip_y_result +
				pen_ring_x_result + pen_ring_y_result);
	} else {
		return rawdata_result;
	}
}


static int nvt_cc_rawdata_test(struct seq_file *s, void *chip_data,
				   struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	int32_t *cc_data = NULL;
	uint8_t *cc_record = NULL;
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int8_t cc_result = -NVT_MP_UNKNOWN;
	uint8_t data_buf[64] = {0};
	int32_t iArrayIndex = 0;
	int err_cnt = 0;
	int rx_num = chip_info->hw_res->rx_num;
	int tx_num = chip_info->hw_res->tx_num;
	buf_len = rx_num * tx_num * sizeof(int32_t);
	record_len = rx_num * tx_num * sizeof(uint8_t);

	TPD_INFO("%s +\n", __func__);

	if (chip_info->p_nvt_autotest_offset == NULL) {
		TPD_INFO("%s NULL point, return error\n", __func__);
		return -1;
	}
	cc_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!cc_data) {
		TPD_INFO("cc_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	cc_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

	if (!cc_record) {
		TPD_INFO("cc_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	if (chip_info->p_nvt_autotest_offset->cc_data_p
			&& chip_info->p_nvt_autotest_offset->cc_data_n) {
		cc_result = NVT_MP_PASS;

		if (nvt_get_fw_pipe(chip_info) == 0) {
			nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE1_ADDR,
					   cc_data, buf_len);

		} else {
			nvt_read_mdata(chip_info, chip_info->trim_id_table.mmap->DIFF_PIPE0_ADDR,
					   cc_data, buf_len);
		}

		snprintf(data_buf, 64, "%s\n", "[NVT CC RAW DATA]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				  strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(cc_data, chip_info, nvt_testdata,
				tx_num, tx_num * rx_num);

		if ((chip_info->p_nvt_test_para->config_lmt_fw_cc_p != 0)
				&& (chip_info->p_nvt_test_para->config_lmt_fw_cc_n != 0)) {
			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;

					if ((cc_data[iArrayIndex] > chip_info->p_nvt_test_para->config_lmt_fw_cc_p) \
							|| (cc_data[iArrayIndex] < chip_info->p_nvt_test_para->config_lmt_fw_cc_n)) {
						cc_result = -NVT_MP_FAIL;
						cc_record[iArrayIndex] = 1;
						TPD_INFO("cc data Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j,
							 cc_data[iArrayIndex], chip_info->p_nvt_test_para->config_lmt_fw_cc_n,
							 chip_info->p_nvt_test_para->config_lmt_fw_cc_p);

						if (!err_cnt) {
							seq_printf(s, "cc data Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j,
								   cc_data[iArrayIndex], chip_info->p_nvt_test_para->config_lmt_fw_cc_n,
								   chip_info->p_nvt_test_para->config_lmt_fw_cc_p);
						}

						err_cnt++;
					}
				}
			}

		} else {
			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;

					if ((cc_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->cc_data_p[iArrayIndex]) \
							|| (cc_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->cc_data_n[iArrayIndex])) {
						cc_result = -NVT_MP_FAIL;
						cc_record[iArrayIndex] = 1;
						TPD_INFO("cc data Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j,
							 cc_data[iArrayIndex], chip_info->p_nvt_autotest_offset->cc_data_n[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->cc_data_p[iArrayIndex]);

						if (!err_cnt) {
							seq_printf(s, "cc data Test failed at rawdata[%d][%d] = %d [%d,%d]\n", i, j,
								   cc_data[iArrayIndex], chip_info->p_nvt_autotest_offset->cc_data_n[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->cc_data_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}
			}
		}

	} else {
		TPD_INFO("cc_data_p || cc_data_n is NULL\n");
	}

	/*---Leave Test Mode---*/
	TPD_INFO("need to go back to Normal after cc rawdate test\n");
	nvt_change_mode(chip_info, NORMAL_MODE);

MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&cc_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&cc_record, record_len);
	TPD_INFO("%s -\n", __func__);
	return cc_result;
}

static int nvt_fw_pen_noise_test(struct seq_file *s, void *chip_data,
				 struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info, int *err_cnt)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	uint8_t data_buf[64] = {0};
	uint8_t *pen_tip_x_noise_p_record = NULL;
	uint8_t *pen_tip_x_noise_n_record = NULL;
	uint8_t *pen_tip_y_noise_p_record = NULL;
	uint8_t *pen_tip_y_noise_n_record = NULL;
	uint8_t *pen_ring_x_noise_p_record = NULL;
	uint8_t *pen_ring_x_noise_n_record = NULL;
	uint8_t *pen_ring_y_noise_p_record = NULL;
	uint8_t *pen_ring_y_noise_n_record = NULL;
	int8_t pen_tip_x_noise_p_result = -NVT_MP_UNKNOWN;
	int8_t pen_tip_x_noise_n_result = -NVT_MP_UNKNOWN;
	int8_t pen_tip_y_noise_p_result = -NVT_MP_UNKNOWN;
	int8_t pen_tip_y_noise_n_result = -NVT_MP_UNKNOWN;
	int8_t pen_ring_x_noise_p_result = -NVT_MP_UNKNOWN;
	int8_t pen_ring_x_noise_n_result = -NVT_MP_UNKNOWN;
	int8_t pen_ring_y_noise_p_result = -NVT_MP_UNKNOWN;
	int8_t pen_ring_y_noise_n_result = -NVT_MP_UNKNOWN;
	int32_t *pen_tip_x_noise_p_data = NULL;
	int32_t *pen_tip_x_noise_n_data = NULL;
	int32_t *pen_tip_y_noise_p_data = NULL;
	int32_t *pen_tip_y_noise_n_data = NULL;
	int32_t *pen_ring_x_noise_p_data = NULL;
	int32_t *pen_ring_x_noise_n_data = NULL;
	int32_t *pen_ring_y_noise_p_data = NULL;
	int32_t *pen_ring_y_noise_n_data = NULL;
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	int rx_num = chip_info->hw_res->rx_num;
	int tx_num = chip_info->hw_res->tx_num;
	int pen_tx_num = chip_info->hw_res->pen_tx_num;
	int pen_rx_num = chip_info->hw_res->pen_rx_num;
	int32_t iArrayIndex = 0;
	record_len = rx_num * tx_num * sizeof(uint8_t);
	buf_len	= rx_num * tx_num * sizeof(int32_t);
	TPD_INFO("Pen Noise Test \n");

		pen_tip_x_noise_p_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_tip_x_noise_p_data) {
			TPD_INFO("pen_tip_x_noise_p_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_tip_x_noise_n_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_tip_x_noise_n_data) {
			TPD_INFO("pen_tip_x_noise_n_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_tip_y_noise_p_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_tip_y_noise_p_data) {
			TPD_INFO("pen_tip_y_noise_p_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_tip_y_noise_n_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_tip_y_noise_n_data) {
			TPD_INFO("pen_tip_y_noise_n_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_x_noise_p_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_ring_x_noise_p_data) {
			TPD_INFO("pen_ring_x_noise_p_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_x_noise_n_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_ring_x_noise_n_data) {
			TPD_INFO("pen_ring_x_noise_n_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_y_noise_p_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_ring_y_noise_p_data) {
			TPD_INFO("pen_ring_y_noise_p_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_y_noise_n_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

		if (!pen_ring_y_noise_n_data) {
			TPD_INFO("pen_ring_y_noise_n_data malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_tip_x_noise_p_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_tip_x_noise_p_record) {
			TPD_INFO("pen_tip_x_noise_p_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_tip_x_noise_n_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_tip_x_noise_n_record) {
			TPD_INFO("pen_tip_x_noise_n_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_tip_y_noise_p_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_tip_y_noise_p_record) {
			TPD_INFO("pen_tip_y_noise_p_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_tip_y_noise_n_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_tip_y_noise_n_record) {
			TPD_INFO("pen_tip_y_noise_n_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_x_noise_p_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_ring_x_noise_p_record) {
			TPD_INFO("pen_ring_x_noise_p_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_x_noise_n_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_ring_x_noise_n_record) {
			TPD_INFO("pen_ring_x_noise_n_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_y_noise_p_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_ring_y_noise_p_record) {
			TPD_INFO("pen_ring_y_noise_p_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		pen_ring_y_noise_n_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len, GFP_KERNEL);

		if (!pen_ring_y_noise_n_record) {
			TPD_INFO("pen_ring_y_noise_n_record malloc memory failed!\n");
			goto MEM_ALLOC_ERR;
		}

		/* pen tip x noise */
		if (chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_p
				&& chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_n) {
			pen_tip_x_noise_p_result = NVT_MP_PASS;
			pen_tip_x_noise_n_result = NVT_MP_PASS;

			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_TIP_X_ADDR,
					pen_tip_x_noise_p_data, tx_num * pen_rx_num);
			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_TIP_X_ADDR,
					pen_tip_x_noise_n_data, tx_num * pen_rx_num);

			memset(data_buf, 0, sizeof(data_buf));
			snprintf(data_buf, 64, "%s\n", "[NVT PEN TIP X NOISE DATA POSITIVE]");
			tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
					strlen(data_buf), nvt_testdata->pos);
			nvt_output_data(pen_tip_x_noise_p_data, chip_info, nvt_testdata,
					tx_num, tx_num * pen_rx_num);

			memset(data_buf, 0, sizeof(data_buf));
			snprintf(data_buf, 64, "%s\n", "[NVT PEN TIP X NOISE DATA NEGATIVE]");
			tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
					strlen(data_buf), nvt_testdata->pos);
			nvt_output_data(pen_tip_x_noise_n_data, chip_info, nvt_testdata,
					tx_num, tx_num * pen_rx_num);

			for (j = 0; j < pen_rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;

					if((pen_tip_x_noise_p_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_p[iArrayIndex]) \
							|| (pen_tip_x_noise_p_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_n[iArrayIndex])) {
						pen_tip_x_noise_p_result = -NVT_MP_FAIL;
						pen_tip_x_noise_p_record[iArrayIndex] = 1;
						TPD_INFO("pen tip x noise p data failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_x_noise_p_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_p[iArrayIndex]);
						if (!(*err_cnt)) {
							seq_printf(s, "pen tip x noise p data failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_tip_x_noise_p_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_p[iArrayIndex]);
						}
						(*err_cnt)++;
					}
				}
			}

			for (j = 0; j < pen_rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;

					if((pen_tip_x_noise_n_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_p[iArrayIndex]) \
							|| (pen_tip_x_noise_n_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_n[iArrayIndex])) {
						pen_tip_x_noise_n_result = -NVT_MP_FAIL;
						pen_tip_x_noise_n_record[iArrayIndex] = 1;
						TPD_INFO("pen tip x noise n data failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_x_noise_n_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_p[iArrayIndex]);
						if (!(*err_cnt)) {
							seq_printf(s, "pen tip x noise n data failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_tip_x_noise_n_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_x_noise_data_p[iArrayIndex]);
						}
						(*err_cnt)++;
					}
				}
			}

		} else {
			TPD_INFO("pen_tip_x_noise_data_p || pen_tip_x_noise_data_n is NULL\n");
		}

		/* pen tip y */
		if (chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_p
				&& chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_n) {
			pen_tip_y_noise_p_result = NVT_MP_PASS;
			pen_tip_y_noise_n_result = NVT_MP_PASS;

			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_TIP_Y_ADDR,
					pen_tip_y_noise_p_data, pen_tx_num * rx_num);
			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_TIP_Y_ADDR,
					pen_tip_y_noise_n_data, pen_tx_num * rx_num);

			memset(data_buf, 0, sizeof(data_buf));
			snprintf(data_buf, 64, "%s\n", "[NVT PEN TIP Y NOISE DATA POSITIVE]");
			tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
					strlen(data_buf), nvt_testdata->pos);
			nvt_output_data(pen_tip_y_noise_p_data, chip_info, nvt_testdata,
					pen_tx_num, pen_tx_num * rx_num);

			memset(data_buf, 0, sizeof(data_buf));
			snprintf(data_buf, 64, "%s\n", "[NVT PEN TIP Y NOISE DATA NEGATIVE]");
			tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
					strlen(data_buf), nvt_testdata->pos);
			nvt_output_data(pen_tip_y_noise_n_data, chip_info, nvt_testdata,
					pen_tx_num, pen_tx_num * rx_num);

			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < pen_tx_num; i++) {
					iArrayIndex = j * pen_tx_num + i;

					if((pen_tip_y_noise_p_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_p[iArrayIndex]) \
							|| (pen_tip_y_noise_p_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_n[iArrayIndex])) {
						pen_tip_y_noise_p_result = -NVT_MP_FAIL;
						pen_tip_y_noise_p_record[iArrayIndex] = 1;
						TPD_INFO("pen tip y noise p data failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_y_noise_p_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_p[iArrayIndex]);
						if (!(*err_cnt)) {
							seq_printf(s, "pen tip y noise p data failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_tip_y_noise_p_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_p[iArrayIndex]);
						}
						(*err_cnt)++;
					}
				}
			}

			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < pen_tx_num; i++) {
					iArrayIndex = j * pen_tx_num + i;

					if((pen_tip_y_noise_n_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_p[iArrayIndex]) \
							|| (pen_tip_y_noise_n_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_n[iArrayIndex])) {
						pen_tip_y_noise_n_result = -NVT_MP_FAIL;
						pen_tip_y_noise_n_record[iArrayIndex] = 1;
						TPD_INFO("pen tip y noise n data failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_tip_y_noise_n_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_p[iArrayIndex]);
						if (!(*err_cnt)) {
							seq_printf(s, "pen tip y noise n data failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_tip_y_noise_n_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_tip_y_noise_data_p[iArrayIndex]);
						}
						(*err_cnt)++;
					}
				}
			}

		} else {
			TPD_INFO("pen_tip_y_noise_data_p || pen_tip_y_noise_data_n is NULL\n");
		}

		/* pen ring x */
		if (chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_p
				&& chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_n) {
			pen_ring_x_noise_p_result = NVT_MP_PASS;
			pen_ring_x_noise_n_result = NVT_MP_PASS;

			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_RING_X_ADDR,
					pen_ring_x_noise_p_data, tx_num * pen_rx_num);
			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_RING_X_ADDR,
					pen_ring_x_noise_n_data, tx_num * pen_rx_num);

			memset(data_buf, 0, sizeof(data_buf));
			snprintf(data_buf, 64, "%s\n", "[NVT PEN RING X NOISE DATA POSITIVE]");
			tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
					strlen(data_buf), nvt_testdata->pos);
			nvt_output_data(pen_ring_x_noise_p_data, chip_info, nvt_testdata,
					tx_num, tx_num * pen_rx_num);

			memset(data_buf, 0, sizeof(data_buf));
			snprintf(data_buf, 64, "%s\n", "[NVT PEN RING X NOISE DATA NEGATIVE]");
			tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
					strlen(data_buf), nvt_testdata->pos);
			nvt_output_data(pen_ring_x_noise_n_data, chip_info, nvt_testdata,
					tx_num, tx_num * pen_rx_num);

			for (j = 0; j < pen_rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;

					if((pen_ring_x_noise_p_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_p[iArrayIndex]) \
							|| (pen_ring_x_noise_p_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_n[iArrayIndex])) {
						pen_ring_x_noise_p_result = -NVT_MP_FAIL;
						pen_ring_x_noise_p_record[iArrayIndex] = 1;
						TPD_INFO("pen ring x noise p data failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_x_noise_p_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_p[iArrayIndex]);
						if (!(*err_cnt)) {
							seq_printf(s, "pen ring x noise p data failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_ring_x_noise_p_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_p[iArrayIndex]);
						}
						(*err_cnt)++;
					}
				}
			}

			for (j = 0; j < pen_rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;

					if((pen_ring_x_noise_n_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_p[iArrayIndex]) \
							|| (pen_ring_x_noise_n_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_n[iArrayIndex])) {
						pen_ring_x_noise_n_result = -NVT_MP_FAIL;
						pen_ring_x_noise_n_record[iArrayIndex] = 1;
						TPD_INFO("pen ring x noise n data failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_x_noise_n_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_p[iArrayIndex]);
						if (!(*err_cnt)) {
							seq_printf(s, "pen ring x noise n data failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_ring_x_noise_n_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_x_noise_data_p[iArrayIndex]);
						}
						(*err_cnt)++;
					}
				}
			}

		} else {
			TPD_INFO("pen_ring_x_noise_data_p || pen_ring_x_noise_data_n is NULL\n");
		}

		/* pen ring y */
		if (chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_p
				&& chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_n) {
			pen_ring_y_noise_p_result = NVT_MP_PASS;
			pen_ring_y_noise_n_result = NVT_MP_PASS;

			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_DIFF_RING_Y_ADDR,
					pen_ring_y_noise_p_data, pen_tx_num * rx_num);
			nvt_read_get_num_mdata(chip_info, chip_info->trim_id_table.mmap->PEN_2D_RAW_RING_Y_ADDR,
					pen_ring_y_noise_n_data, pen_tx_num * rx_num);

			memset(data_buf, 0, sizeof(data_buf));
			snprintf(data_buf, 64, "%s\n", "[NVT PEN RING Y NOISE DATA POSITIVE]");
			tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
					strlen(data_buf), nvt_testdata->pos);
			nvt_output_data(pen_ring_y_noise_p_data, chip_info, nvt_testdata,
					pen_tx_num, pen_tx_num * rx_num);

			memset(data_buf, 0, sizeof(data_buf));
			snprintf(data_buf, 64, "%s\n", "[NVT PEN RING Y NOISE DATA NEGATIVE]");
			tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
					strlen(data_buf), nvt_testdata->pos);
			nvt_output_data(pen_ring_y_noise_n_data, chip_info, nvt_testdata,
					pen_tx_num, pen_tx_num * rx_num);

			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < pen_tx_num; i++) {
					iArrayIndex = j * pen_tx_num + i;

					if((pen_ring_y_noise_p_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_p[iArrayIndex]) \
							|| (pen_ring_y_noise_p_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_n[iArrayIndex])) {
						pen_ring_y_noise_p_result = -NVT_MP_FAIL;
						pen_ring_y_noise_p_record[iArrayIndex] = 1;
						TPD_INFO("pen ring y noise p data failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_y_noise_p_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_p[iArrayIndex]);
						if (!(*err_cnt)) {
							seq_printf(s, "pen ring y noise p data failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_ring_y_noise_p_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_p[iArrayIndex]);
						}
						(*err_cnt)++;
					}
				}
			}

			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < pen_tx_num; i++) {
					iArrayIndex = j * pen_tx_num + i;

					if((pen_ring_y_noise_n_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_p[iArrayIndex]) \
							|| (pen_ring_y_noise_n_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_n[iArrayIndex])) {
						pen_ring_y_noise_n_result = -NVT_MP_FAIL;
						pen_ring_y_noise_n_record[iArrayIndex] = 1;
						TPD_INFO("pen ring y noise n data failed at data[%d][%d] = %d [%d,%d]\n",
								i, j, pen_ring_y_noise_n_data[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_n[iArrayIndex],
								chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_p[iArrayIndex]);
						if (!(*err_cnt)) {
							seq_printf(s, "pen ring y noise n data failed at data[%d][%d] = %d [%d,%d]\n",
									i, j, pen_ring_y_noise_n_data[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_n[iArrayIndex],
									chip_info->p_nvt_autotest_offset->pen_ring_y_noise_data_p[iArrayIndex]);
						}
						(*err_cnt)++;
					}
				}
			}

	} else {
		TPD_INFO("pen_ring_y_noise_data_p || pen_ring_y_noise_data_n is NULL\n");
	}

	/*---Leave Test Mode---*/
MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_x_noise_p_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_x_noise_n_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_y_noise_p_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_y_noise_n_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_x_noise_p_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_x_noise_n_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_y_noise_p_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_y_noise_n_data, buf_len);

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_x_noise_p_record, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_x_noise_n_record, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_y_noise_p_record, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_tip_y_noise_n_record, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_x_noise_p_record, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_x_noise_n_record, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_y_noise_p_record, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&pen_ring_y_noise_n_record, buf_len);

	return (pen_tip_x_noise_p_result + pen_tip_x_noise_n_result +
		pen_tip_y_noise_p_result + pen_tip_y_noise_n_result +
		pen_ring_x_noise_p_result + pen_ring_x_noise_n_result +
		pen_ring_y_noise_p_result + pen_ring_y_noise_n_result);
}

static int nvt_fw_noise_test(struct seq_file *s, void *chip_data,
				 struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	uint8_t *noise_p_record = NULL;
	uint8_t *noise_n_record = NULL;
	int8_t noise_p_result = -NVT_MP_UNKNOWN;
	int8_t noise_n_result = -NVT_MP_UNKNOWN;
	int8_t pen_noise_result = -NVT_MP_UNKNOWN;
	uint8_t data_buf[64] = {0};
	int32_t *noise_p_data = NULL;
	int32_t *noise_n_data = NULL;
	int32_t iArrayIndex = 0;
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	int err_cnt = 0;
	int rx_num = chip_info->hw_res->rx_num;
	int tx_num = chip_info->hw_res->tx_num;


	record_len = rx_num * tx_num * sizeof(uint8_t);
	buf_len	= rx_num * tx_num * sizeof(int32_t);

	TPD_INFO("%s +\n", __func__);

	if (chip_info->p_nvt_autotest_offset == NULL) {
		TPD_INFO("%s NULL point, return error\n", __func__);
		return -1;
	}
	noise_p_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!noise_p_data) {
		TPD_INFO("noise_p_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	noise_n_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!noise_n_data) {
		TPD_INFO("noise_n_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	noise_p_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
					 GFP_KERNEL);

	if (!noise_p_record) {
		TPD_INFO("noise_p_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	noise_n_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
					 GFP_KERNEL);

	if (!noise_n_record) {
		TPD_INFO("noise_n_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	/*---Noise Test---*/
	TPD_INFO("Noise Test \n");
	if (chip_info->p_nvt_autotest_offset->diff_rawdata_p
			&& chip_info->p_nvt_autotest_offset->diff_rawdata_n) {
		noise_p_result = NVT_MP_PASS;
		noise_n_result = NVT_MP_PASS;

		if (nvt_read_fw_noise(chip_info,
					  chip_info->p_nvt_test_para->config_diff_test_frame, noise_p_data, noise_n_data,
					  buf_len) != 0) {
			TPD_INFO("read Noise data failed!\n");
			noise_p_result = -NVT_MP_FAIL_READ_DATA;
			noise_n_result = -NVT_MP_FAIL_READ_DATA;
			seq_printf(s, "read Noise data failed!\n");
			err_cnt++;
			goto TEST_END;
		}

		snprintf(data_buf, 64, "%s\n", "[NVT NOISE DATA POSITIVE]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				  strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(noise_p_data, chip_info, nvt_testdata,
				tx_num, tx_num * rx_num);

		snprintf(data_buf, 64, "%s\n", "[NVT NOISE DATA NEGATIVE]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				  strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(noise_n_data, chip_info, nvt_testdata,
				tx_num, tx_num * rx_num);

		for (j = 0; j < rx_num; j++) {
			for (i = 0; i < tx_num; i++) {
				iArrayIndex = j * tx_num + i;

					if ((noise_p_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->diff_rawdata_p[iArrayIndex]) \
							|| (noise_p_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->diff_rawdata_n[iArrayIndex])) {
						noise_p_result = -NVT_MP_FAIL;
						noise_p_record[iArrayIndex] = 1;
						TPD_INFO("Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i,
							 j, noise_p_data[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->diff_rawdata_n[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->diff_rawdata_p[iArrayIndex]);

						if (!err_cnt) {
							seq_printf(s,
								   "Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								   noise_p_data[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->diff_rawdata_n[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->diff_rawdata_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}
			}

			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;

					if ((noise_n_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->diff_rawdata_p[iArrayIndex]) \
							|| (noise_n_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->diff_rawdata_n[iArrayIndex])) {
						noise_n_result = -NVT_MP_FAIL;
						noise_n_record[iArrayIndex] = 1;
						TPD_INFO("Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i,
							 j, noise_n_data[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->diff_rawdata_n[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->diff_rawdata_p[iArrayIndex]);

						if (!err_cnt) {
							seq_printf(s,
								   "Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								   noise_n_data[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->diff_rawdata_n[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->diff_rawdata_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}
			}

	} else {
		TPD_INFO("diff_rawdata_p || diff_rawdata_n is NULL\n");
	}

	if (chip_info->ts->pen_support) {
		pen_noise_result = nvt_fw_pen_noise_test(s, chip_data, nvt_testdata, p_test_item_info, &err_cnt);
	} /* ts->pen_support */

	/*---Leave Test Mode---*/
	nvt_change_mode(chip_info, NORMAL_MODE);

TEST_END:
MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&noise_p_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&noise_n_data, buf_len);

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&noise_n_record, record_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&noise_p_record, record_len);
	TPD_INFO("%s -\n", __func__);

	if (chip_info->ts->pen_support) {
		return (noise_p_result + noise_n_result + pen_noise_result);
	} else {
		return noise_p_result + noise_n_result;
	}
}

static int nvt_doze_noise_test(struct seq_file *s, void *chip_data,
				   struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int32_t *doze_noise_p_data = NULL;
	int32_t *doze_noise_n_data = NULL;
	int32_t iArrayIndex = 0;
	uint8_t *doze_noise_p_record = NULL;
	uint8_t *doze_noise_n_record = NULL;
	int8_t doze_noise_p_result = -NVT_MP_UNKNOWN;
	int8_t doze_noise_n_result = -NVT_MP_UNKNOWN;
	uint8_t data_buf[64] = {0};
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	int err_cnt = 0;
	int rx_num = chip_info->hw_res->rx_num;
	int tx_num = chip_info->hw_res->tx_num;
	int doze_x_num = chip_info->p_nvt_test_para->doze_x_channel;
	int doze_y_num = chip_info->p_nvt_test_para->doze_y_channel;

	record_len = rx_num * tx_num * sizeof(uint8_t);
	buf_len	= rx_num * tx_num * sizeof(int32_t);

	TPD_INFO("%s +\n", __func__);

	if (chip_info->p_nvt_autotest_offset == NULL || chip_info->p_nvt_test_para == NULL) {
		TPD_INFO("%s NULL point, return error\n", __func__);
		return -1;
	}
	doze_noise_p_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);
	if (!doze_noise_p_data) {
		TPD_INFO("doze_noise_p_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	doze_noise_n_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len,
						GFP_KERNEL);

	if (!doze_noise_n_data) {
		TPD_INFO("doze_noise_n_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	doze_noise_p_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
						  GFP_KERNEL);

	if (!doze_noise_p_record) {
		TPD_INFO("doze_noise_p_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	doze_noise_n_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
						  GFP_KERNEL);

	if (!doze_noise_n_record) {
		TPD_INFO("doze_noise_n_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	/*---Doze Noise Test---*/
	if (chip_info->p_nvt_autotest_offset->doze_diff_rawdata_p
			&& chip_info->p_nvt_autotest_offset->doze_diff_rawdata_n) {
		doze_noise_p_result = NVT_MP_PASS;
		doze_noise_n_result = NVT_MP_PASS;

		if (nvt_read_doze_fw_noise(chip_info,
					   chip_info->p_nvt_test_para->config_doze_noise_test_frame,
					   doze_noise_p_data, doze_noise_n_data, buf_len) != 0) {
			TPD_INFO("read Doze Noise data failed!\n");
			doze_noise_p_result = -NVT_MP_FAIL_READ_DATA;
			doze_noise_n_result = -NVT_MP_FAIL_READ_DATA;
			seq_printf(s, "read Doze Noise data failed!\n");
			err_cnt++;
			goto TEST_END;
		}

		snprintf(data_buf, 64, "%s\n", "[NVT DOZE NOISE DATA POSITIVE]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				  strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(doze_noise_p_data, chip_info, nvt_testdata,
				doze_x_num,
				doze_x_num * doze_y_num);

		snprintf(data_buf, 64, "%s\n", "[NVT DOZE NOISE DATA NEGATIVE]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				  strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(doze_noise_n_data, chip_info, nvt_testdata,
				doze_x_num,
				doze_x_num * doze_y_num);

		if ((chip_info->p_nvt_test_para->config_lmt_doze_diff_p != 0)
				&& (chip_info->p_nvt_test_para->config_lmt_doze_diff_n != 0)) {
			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((doze_noise_p_data[iArrayIndex] >
							chip_info->p_nvt_test_para->config_lmt_doze_diff_p) \
							|| (doze_noise_p_data[iArrayIndex] <
								chip_info->p_nvt_test_para->config_lmt_doze_diff_n)) {
						doze_noise_p_result = -NVT_MP_FAIL;
						doze_noise_p_record[iArrayIndex] = 1;
						TPD_INFO("Doze Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n",
							 i, j, doze_noise_p_data[iArrayIndex],
							 chip_info->p_nvt_test_para->config_lmt_doze_diff_n,
							 chip_info->p_nvt_test_para->config_lmt_doze_diff_p);

						if (!err_cnt) {
							seq_printf(s,
								   "Doze Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								   doze_noise_p_data[iArrayIndex],
								   chip_info->p_nvt_test_para->config_lmt_doze_diff_n,
								   chip_info->p_nvt_test_para->config_lmt_doze_diff_p);
						}

						err_cnt++;
					}
				}
			}

			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((doze_noise_n_data[iArrayIndex] >
							chip_info->p_nvt_test_para->config_lmt_doze_diff_p) \
							|| (doze_noise_n_data[iArrayIndex] <
								chip_info->p_nvt_test_para->config_lmt_doze_diff_n)) {
						doze_noise_n_result = -NVT_MP_FAIL;
						doze_noise_n_record[iArrayIndex] = 1;
						TPD_INFO("Doze Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n",
							 i, j, doze_noise_n_data[iArrayIndex],
							 chip_info->p_nvt_test_para->config_lmt_doze_diff_n,
							 chip_info->p_nvt_test_para->config_lmt_doze_diff_p);

						if (!err_cnt) {
							seq_printf(s,
								   "Doze Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								   doze_noise_n_data[iArrayIndex],
								   chip_info->p_nvt_test_para->config_lmt_doze_diff_n,
								   chip_info->p_nvt_test_para->config_lmt_doze_diff_p);
						}

						err_cnt++;
					}
				}
			}

		} else {
			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((doze_noise_n_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->doze_diff_rawdata_p[iArrayIndex]) \
							|| (doze_noise_n_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->doze_diff_rawdata_n[iArrayIndex])) {
						doze_noise_p_result = -NVT_MP_FAIL;
						doze_noise_p_record[iArrayIndex] = 1;
						TPD_INFO("Doze Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n",
							 i, j, doze_noise_n_data[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->doze_diff_rawdata_n[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->doze_diff_rawdata_p[iArrayIndex]);

						if (!err_cnt) {
							seq_printf(s,
								   "Doze Noise RawData_Diff_Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								   doze_noise_n_data[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->doze_diff_rawdata_n[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->doze_diff_rawdata_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}
			}

			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((doze_noise_n_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->doze_diff_rawdata_p[iArrayIndex]) \
							|| (doze_noise_n_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->doze_diff_rawdata_n[iArrayIndex])) {
						doze_noise_n_result = -NVT_MP_FAIL;
						doze_noise_n_record[iArrayIndex] = 1;
						TPD_INFO("Doze Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n",
							 i, j, doze_noise_n_data[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->doze_diff_rawdata_n[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->doze_diff_rawdata_p[iArrayIndex]);

						if (!err_cnt) {
							seq_printf(s,
								   "Doze Noise RawData_Diff_Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								   doze_noise_n_data[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->doze_diff_rawdata_n[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->doze_diff_rawdata_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}
			}
		}

	} else {
		TPD_INFO("doze_diff_rawdata_p || doze_diff_rawdata_n is NULL\n");
	}

TEST_END:
MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&doze_noise_p_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&doze_noise_n_data, buf_len);

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&doze_noise_p_record,
			  record_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&doze_noise_n_record,
			  record_len);
	TPD_INFO("%s -\n", __func__);
	return (doze_noise_n_result + doze_noise_p_result);
}

static int nvt_doze_fw_rawdata_test(struct seq_file *s, void *chip_data,
					struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int32_t *doze_raw_data = NULL;
	int32_t iArrayIndex = 0;
	int8_t doze_rawdata_result = -NVT_MP_UNKNOWN;
	int8_t *doze_raw_record = NULL;
	uint8_t data_buf[64] = {0};
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	int err_cnt = 0;
	int rx_num = chip_info->hw_res->rx_num;
	int tx_num = chip_info->hw_res->tx_num;
	int doze_x_num = chip_info->p_nvt_test_para->doze_x_channel;
	int doze_y_num = chip_info->p_nvt_test_para->doze_y_channel;
	record_len = rx_num * tx_num * sizeof(uint8_t);
	buf_len	= rx_num * tx_num * sizeof(int32_t);

	TPD_INFO("%s +\n", __func__);

	if (chip_info->p_nvt_autotest_offset == NULL || chip_info->p_nvt_test_para == NULL) {
		TPD_INFO("%s NULL point, return error\n", __func__);
		return -1;
	}
	doze_raw_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!doze_raw_data) {
		TPD_INFO("doze_raw_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	doze_raw_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
					  GFP_KERNEL);

	if (!doze_raw_record) {
		TPD_INFO("doze_raw_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	/*---Doze FW Rawdata Test---*/
	if (chip_info->p_nvt_autotest_offset->doze_rawdata_p
			&& chip_info->p_nvt_autotest_offset->doze_rawdata_n) {
		doze_rawdata_result = NVT_MP_PASS;

		if (nvt_read_doze_baseline(chip_info, doze_raw_data, buf_len) != 0) {
			TPD_INFO("read Doze FW Rawdata failed!\n");
			doze_rawdata_result = -NVT_MP_FAIL_READ_DATA;
			seq_printf(s, "read Doze FW Rawdata failed!\n");
			err_cnt++;
			goto TEST_END;
		}

		snprintf(data_buf, 64, "%s\n", "[NVT DOZE FW RAW DATA]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				  strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(doze_raw_data, chip_info, nvt_testdata,
				doze_x_num,
				doze_x_num * doze_y_num);

		if ((chip_info->p_nvt_test_para->config_lmt_doze_rawdata_p != 0)
				&& (chip_info->p_nvt_test_para->config_lmt_doze_rawdata_n != 0)) {
			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((doze_raw_data[iArrayIndex] >
							chip_info->p_nvt_test_para->config_lmt_doze_rawdata_p) \
							|| (doze_raw_data[iArrayIndex] <
								chip_info->p_nvt_test_para->config_lmt_doze_rawdata_n)) {
						doze_rawdata_result = -NVT_MP_FAIL;
						doze_raw_record[iArrayIndex] = 1;
						TPD_INFO("Doze FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
							 doze_raw_data[iArrayIndex],
							 chip_info->p_nvt_test_para->config_lmt_doze_rawdata_n,
							 chip_info->p_nvt_test_para->config_lmt_doze_rawdata_p);

						if (!err_cnt) {
							seq_printf(s, "Doze FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i,
								   j, doze_raw_data[iArrayIndex],
								   chip_info->p_nvt_test_para->config_lmt_doze_rawdata_n,
								   chip_info->p_nvt_test_para->config_lmt_doze_rawdata_p);
						}

						err_cnt++;
					}
				}
			}

		} else {
			for (j = 0; j < doze_y_num; j++) {
				for (i = 0; i < doze_x_num; i++) {
					iArrayIndex = j * doze_x_num + i;

					if ((doze_raw_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->doze_rawdata_p[iArrayIndex]) \
							|| (doze_raw_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->doze_rawdata_n[iArrayIndex])) {
						doze_rawdata_result = -NVT_MP_FAIL;
						doze_raw_record[iArrayIndex] = 1;
						TPD_INFO("Doze FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
							 doze_raw_data[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->doze_rawdata_n[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->doze_rawdata_p[iArrayIndex]);

						if (!err_cnt) {
							seq_printf(s, "Doze FW Rawdata Test failed at data[%d][%d] = %d [%d,%d]\n", i,
								   j, doze_raw_data[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->doze_rawdata_n[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->doze_rawdata_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}
			}
		}

	} else {
		TPD_INFO("doze_rawdata_p || doze_rawdata_n is NULL \n");
	}

TEST_END:
MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&doze_raw_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&doze_raw_record, record_len);
	TPD_INFO("%s -\n", __func__);
	return doze_rawdata_result;
}

static int nvt_short_test(struct seq_file *s, void *chip_data,
			  struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int32_t *short_data = NULL;
	int32_t iArrayIndex = 0;
	uint8_t *short_record = NULL;
	int8_t short_result = -NVT_MP_UNKNOWN;
	uint8_t data_buf[64] = {0};
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	int err_cnt = 0;
	int rx_num = chip_info->hw_res->rx_num;
	int tx_num = chip_info->hw_res->tx_num;
	record_len = rx_num * tx_num * sizeof(uint8_t);
	buf_len	= rx_num * tx_num * sizeof(int32_t);

	TPD_INFO("%s +\n", __func__);

	if (chip_info->p_nvt_autotest_offset == NULL ||
		chip_info->p_nvt_test_para == NULL) {
		TPD_INFO("%s NULL point, return error\n", __func__);
		return -1;
	}
	short_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!short_data) {
		TPD_INFO("short_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	short_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
					   GFP_KERNEL);

	if (!short_record) {
		TPD_INFO("short_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	/*--Short Test---*/
	if (chip_info->p_nvt_autotest_offset->short_rawdata_p
			&& chip_info->p_nvt_autotest_offset->short_rawdata_n) {
		short_result = NVT_MP_PASS;

		if (nvt_read_fw_short(chip_info, short_data, buf_len) != 0) {
			TPD_INFO("read Short test data failed!\n");
			short_result = -NVT_MP_FAIL_READ_DATA;
			seq_printf(s, "read Short test data failed!\n");
			err_cnt++;
			goto TEST_END;
		}

		snprintf(data_buf, 64, "%s\n", "[NVT SHORT]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				  strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(short_data, chip_info, nvt_testdata,
				tx_num, tx_num * rx_num);

		if ((chip_info->p_nvt_test_para->config_lmt_short_rawdata_p != 0)
				&& (chip_info->p_nvt_test_para->config_lmt_short_rawdata_n != 0)) {
			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;

					if ((short_data[iArrayIndex] >
							chip_info->p_nvt_test_para->config_lmt_short_rawdata_p) \
							|| (short_data[iArrayIndex] <
								chip_info->p_nvt_test_para->config_lmt_short_rawdata_n)) {
						short_result = -NVT_MP_FAIL;
						short_record[iArrayIndex] = 1;
						TPD_INFO("Short Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
							 short_data[iArrayIndex], chip_info->p_nvt_test_para->config_lmt_short_rawdata_n,
							 chip_info->p_nvt_test_para->config_lmt_short_rawdata_p);

						if (!err_cnt) {
							seq_printf(s, "Short Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								   short_data[iArrayIndex], chip_info->p_nvt_test_para->config_lmt_short_rawdata_n,
								   chip_info->p_nvt_test_para->config_lmt_short_rawdata_p);
						}

						err_cnt++;
					}
				}
			}

		} else {
			for (j = 0; j < rx_num; j++) {
				for (i = 0; i < tx_num; i++) {
					iArrayIndex = j * tx_num + i;

					if ((short_data[iArrayIndex] >
							chip_info->p_nvt_autotest_offset->short_rawdata_p[iArrayIndex]) \
							|| (short_data[iArrayIndex] <
								chip_info->p_nvt_autotest_offset->short_rawdata_n[iArrayIndex])) {
						short_result = -NVT_MP_FAIL;
						short_record[iArrayIndex] = 1;
						TPD_INFO("Short Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
							 short_data[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->short_rawdata_n[iArrayIndex],
							 chip_info->p_nvt_autotest_offset->short_rawdata_p[iArrayIndex]);

						if (!err_cnt) {
							seq_printf(s, "Short Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
								   short_data[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->short_rawdata_n[iArrayIndex],
								   chip_info->p_nvt_autotest_offset->short_rawdata_p[iArrayIndex]);
						}

						err_cnt++;
					}
				}
			}
		}

	} else {
		TPD_INFO("short_rawdata_p || short_rawdata_n is NULL\n");
	}

TEST_END:
MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&short_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&short_record, record_len);
	TPD_INFO("%s -\n", __func__);
	return short_result;
}

static int nvt_open_test(struct seq_file *s, void *chip_data,
			 struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int32_t *open_data = NULL;
	int32_t iArrayIndex = 0;
	uint8_t *open_record = NULL;
	int8_t open_result = -NVT_MP_UNKNOWN;
	uint8_t data_buf[64] = {0};
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	int err_cnt = 0;
	int rx_num = chip_info->hw_res->rx_num;
	int tx_num = chip_info->hw_res->tx_num;
	record_len = rx_num * tx_num * sizeof(uint8_t);
	buf_len	= rx_num * tx_num * sizeof(int32_t);

	TPD_INFO("%s +\n", __func__);

	if (chip_info->p_nvt_autotest_offset == NULL) {
		TPD_INFO("%s NULL point, return error\n", __func__);
		return -1;
	}
	open_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len, GFP_KERNEL);

	if (!open_data) {
		TPD_INFO("open_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	open_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
					  GFP_KERNEL);

	if (!open_record) {
		TPD_INFO("open_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	/*---Open Test---*/
	TPD_INFO("FW Open Test\n");

	if (chip_info->p_nvt_autotest_offset->open_rawdata_p
			&& chip_info->p_nvt_autotest_offset->open_rawdata_n) {
		open_result = NVT_MP_PASS;

		if (nvt_read_fw_open(chip_info, open_data, buf_len) != 0) {
			TPD_INFO("read Open test data failed!\n");
			open_result = -NVT_MP_FAIL_READ_DATA;
			seq_printf(s, "read Open test data failed!\n");
			err_cnt++;
			goto TEST_END;
		}

		snprintf(data_buf, 64, "%s\n", "[NVT OPEN TEST]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				  strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(open_data, chip_info, nvt_testdata,
				tx_num, tx_num * rx_num);

		for (j = 0; j < rx_num; j++) {
			for (i = 0; i < tx_num; i++) {
				iArrayIndex = j * tx_num + i;

				if ((open_data[iArrayIndex] >
						chip_info->p_nvt_autotest_offset->open_rawdata_p[iArrayIndex]) \
						|| (open_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->open_rawdata_n[iArrayIndex])) {
					open_result = -NVT_MP_FAIL;
					open_record[iArrayIndex] = 1;
					TPD_INFO("Open Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
						 open_data[iArrayIndex],
						 chip_info->p_nvt_autotest_offset->open_rawdata_n[iArrayIndex],
						 chip_info->p_nvt_autotest_offset->open_rawdata_p[iArrayIndex]);

					if (!err_cnt) {
						seq_printf(s, "Open Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
							   open_data[iArrayIndex],
							   chip_info->p_nvt_autotest_offset->open_rawdata_n[iArrayIndex],
							   chip_info->p_nvt_autotest_offset->open_rawdata_p[iArrayIndex]);
					}

					err_cnt++;
				}
			}
		}

	} else {
		TPD_INFO("open_rawdata_p || open_rawdata_n is NULL\n");
	}

TEST_END:
MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&open_data, buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&open_record, record_len);
	TPD_INFO("%s -\n", __func__);
	return open_result;
}

static int nvt_digital_noise_test(struct seq_file *s, void *chip_data,
				  struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	int32_t *digital_noise_p_data = NULL;
	int32_t *digital_noise_n_data = NULL;
	int32_t iArrayIndex = 0;
	uint8_t *digital_noise_p_record = NULL;
	uint8_t *digital_noise_n_record = NULL;
	int8_t digital_noise_p_result = -NVT_MP_UNKNOWN;
	int8_t digital_noise_n_result = -NVT_MP_UNKNOWN;
	uint8_t data_buf[64] = {0};
	int i = 0;
	int j = 0;
	int buf_len = 0;
	int record_len = 0;
	int err_cnt = 0;
	int rx_num = chip_info->hw_res->rx_num;
	int tx_num = chip_info->hw_res->tx_num;
	record_len = rx_num * tx_num * sizeof(uint8_t);
	buf_len	= rx_num * tx_num * sizeof(int32_t);

	TPD_INFO("%s +\n", __func__);

	if (chip_info->p_nvt_autotest_offset == NULL || chip_info->p_nvt_test_para == NULL) {
		TPD_INFO("%s NULL point, return error\n", __func__);
		return -1;
	}
	digital_noise_p_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len,
						   GFP_KERNEL);

	if (!digital_noise_p_data) {
		TPD_INFO("digital_noise_p_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	digital_noise_n_data = tp_devm_kzalloc(&chip_info->s_client->dev, buf_len,
						   GFP_KERNEL);

	if (!digital_noise_n_data) {
		TPD_INFO("digital_noise_n_data malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	digital_noise_p_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
				 GFP_KERNEL);

	if (!digital_noise_p_record) {
		TPD_INFO("digital_noise_p_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	digital_noise_n_record = tp_devm_kzalloc(&chip_info->s_client->dev, record_len,
				 GFP_KERNEL);

	if (!digital_noise_n_record) {
		TPD_INFO("digital_noise_n_record malloc memory failed!\n");
		goto MEM_ALLOC_ERR;
	}

	/*---Digital Noise Test---*/
	TPD_INFO("FW Digital Noise Test\n");

	if (chip_info->p_nvt_autotest_offset->digital_diff_p
			&& chip_info->p_nvt_autotest_offset->digital_diff_n) {
		digital_noise_p_result = NVT_MP_PASS;
		digital_noise_n_result = NVT_MP_PASS;
		nvt_enter_digital_test(chip_info, true);

		if (nvt_read_fw_noise(chip_info,
					  chip_info->p_nvt_test_para->config_diff_test_frame,
					  digital_noise_p_data, digital_noise_n_data, buf_len) != 0) {
			TPD_INFO("read Digital Noise data failed!\n");
			digital_noise_p_result = -NVT_MP_FAIL_READ_DATA;
			digital_noise_n_result = -NVT_MP_FAIL_READ_DATA;
			seq_printf(s, "read Digital Noise data failed!\n");
			err_cnt++;
			goto TEST_END;
		}

		/*---Leave Test Mode---*/
		nvt_change_mode(chip_info, NORMAL_MODE);

		nvt_enter_digital_test(chip_info, false);

		snprintf(data_buf, 64, "%s\n", "[NVT DIGITAL NOISE DATA POSITIVE]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				  strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(digital_noise_p_data, chip_info, nvt_testdata,
				tx_num, tx_num * rx_num);

		snprintf(data_buf, 64, "%s\n", "[NVT DIGITAL NOISE DATA NEGATIVE]");
		tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
				  strlen(data_buf), nvt_testdata->pos);
		nvt_output_data(digital_noise_n_data, chip_info, nvt_testdata,
				tx_num, tx_num * rx_num);

		for (j = 0; j < rx_num; j++) {
			for (i = 0; i < tx_num; i++) {
				iArrayIndex = j * tx_num + i;

				if ((digital_noise_p_data[iArrayIndex] >
						chip_info->p_nvt_autotest_offset->digital_diff_p[iArrayIndex]) \
						|| (digital_noise_p_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->digital_diff_n[iArrayIndex])) {
					digital_noise_p_result = -NVT_MP_FAIL;
					digital_noise_p_record[iArrayIndex] = 1;
					TPD_INFO("Digital Noise Max Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
						 digital_noise_p_data[iArrayIndex],
						 chip_info->p_nvt_autotest_offset->digital_diff_n[iArrayIndex],
						 chip_info->p_nvt_autotest_offset->digital_diff_p[iArrayIndex]);

					if (!err_cnt) {
						seq_printf(s, "Digital Noise Max Test failed at data[%d][%d] = %d [%d,%d]\n", i,
							   j,
							   digital_noise_p_data[iArrayIndex],
							   chip_info->p_nvt_autotest_offset->digital_diff_n[iArrayIndex],
							   chip_info->p_nvt_autotest_offset->digital_diff_p[iArrayIndex]);
					}

					err_cnt++;
				}
			}
		}

		for (j = 0; j < rx_num; j++) {
			for (i = 0; i < tx_num; i++) {
				iArrayIndex = j * tx_num + i;

				if ((digital_noise_n_data[iArrayIndex] >
						chip_info->p_nvt_autotest_offset->digital_diff_p[iArrayIndex]) \
						|| (digital_noise_n_data[iArrayIndex] <
							chip_info->p_nvt_autotest_offset->digital_diff_n[iArrayIndex])) {
					digital_noise_n_result = -NVT_MP_FAIL;
					digital_noise_n_record[iArrayIndex] = 1;
					TPD_INFO("Digital Noise Min Test failed at data[%d][%d] = %d [%d,%d]\n", i, j,
						 digital_noise_n_data[iArrayIndex],
						 chip_info->p_nvt_autotest_offset->digital_diff_n[iArrayIndex],
						 chip_info->p_nvt_autotest_offset->digital_diff_p[iArrayIndex]);

					if (!err_cnt) {
						seq_printf(s, "Digital Noise Min Test failed at data[%d][%d] = %d [%d,%d]\n", i,
							   j,
							   digital_noise_n_data[iArrayIndex],
							   chip_info->p_nvt_autotest_offset->digital_diff_n[iArrayIndex],
							   chip_info->p_nvt_autotest_offset->digital_diff_p[iArrayIndex]);
					}

					err_cnt++;
				}
			}
		}

	} else {
		TPD_INFO("digital_noise_P || digital_noise_N is NULL\n");
	}

TEST_END:
MEM_ALLOC_ERR:
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&digital_noise_p_data,
			  buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&digital_noise_n_data,
			  buf_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&digital_noise_n_record,
			  record_len);
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&digital_noise_p_record,
			  record_len);
	TPD_INFO("%s -\n", __func__);
	return (digital_noise_p_result + digital_noise_n_result);
}

#define NVT_LIMIT_LOADING(limit_option) { \
	chip_info->p_nvt_autotest_offset->limit_option##_p = \
	(int32_t *)(ts->com_test_data.limit_fw->data + item_head->top_limit_offset); \
	chip_info->p_nvt_autotest_offset->limit_option##_n = \
	(int32_t *)(ts->com_test_data.limit_fw->data + item_head->floor_limit_offset); \
}

static int nvt_autotest_preoperation(struct seq_file *s, void *chip_data,
					 struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	int ret = -1;
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	struct touchpanel_data *ts = chip_info->ts;
	struct auto_test_header *ph = NULL;

	char *fw_name_test = NULL;
	char *postfix = "_TEST.bin";
	uint32_t *p_item_offset = NULL;
	int item_cnt = 0;
	struct auto_test_item_header *item_head = NULL;
	int m = 0;
	uint8_t *p_print = NULL;
	int i = 0;
	char *p_node = NULL;
	uint8_t copy_len = 0;
	uint8_t  data_buf[128] = {0};
	int32_t *p_data32 = NULL;

	TPD_INFO("%s +\n", __func__);
	/* request test limit data from userspace*/
	TPD_INFO("panel_data.test_limit_name - %s\n", ts->panel_data.test_limit_name);
	ret = request_firmware(&ts->com_test_data.limit_fw,
				   ts->panel_data.test_limit_name, ts->dev);

	if (ret < 0) {
		TPD_INFO("Request firmware failed - %s (%d)\n", ts->panel_data.test_limit_name,
			 ret);
		return -1;
	}


	nvt_esd_check_enable(chip_info, false);

	fw_name_test = tp_devm_kzalloc(&chip_info->s_client->dev, MAX_FW_NAME_LENGTH,
					   GFP_KERNEL);

	if (fw_name_test == NULL) {
		TPD_INFO("fw_name_test kzalloc error!\n");
		return -1;
	}

	p_node  = strstr(chip_info->fw_name, ".");
	copy_len = p_node - chip_info->fw_name;
	memcpy(fw_name_test, chip_info->fw_name, copy_len);
	strlcat(fw_name_test, postfix, MAX_FW_NAME_LENGTH);
	TPD_INFO("fw_name_test is %s\n", fw_name_test);

	/*update test firmware*/
	ret = request_firmware(&ts->com_test_data.black_test_fw, fw_name_test, ts->dev);

	if (ret != 0) {
		TPD_INFO("request test firmware failed! ret = %d\n", ret);
		tp_devm_kfree(&chip_info->s_client->dev, (void **)&fw_name_test,
				  MAX_FW_NAME_LENGTH);
		return -1;
	}

	ret = nvt_fw_update_sub(chip_info, ts->com_test_data.black_test_fw, 0);

	if (ret > 0) {
		TPD_INFO("fw update failed!\n");
		goto RELEASE_FIRMWARE;
	}

	TPD_INFO("update test firmware successed!\n");

	if (nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_REK)) {
		TPD_INFO("check fw reset state failed!\n");
		seq_printf(s, "check fw reset state failed!\n");
		goto RELEASE_FIRMWARE;
	}

	if (nvt_switch_FreqHopEnDis(chip_info, FREQ_HOP_DISABLE)) {
		TPD_INFO("switch frequency hopping disable failed!\n");
		goto RELEASE_FIRMWARE;
	}

	if (nvt_check_fw_reset_state_noflash(chip_info, RESET_STATE_NORMAL_RUN)) {
		TPD_INFO("check fw reset state failed!\n");
		goto RELEASE_FIRMWARE;
	}

	msleep(100);

	/*---Enter Test Mode---*/
	TPD_INFO("enter test mode\n");

	if (nvt_clear_fw_status(chip_info)) {
		TPD_INFO("clear fw status failed!\n");
		goto RELEASE_FIRMWARE;
	}

	nvt_change_mode(chip_info, MP_MODE_CC);

	if (nvt_check_fw_status(chip_info)) {
		TPD_INFO("check fw status failed!\n");
		goto RELEASE_FIRMWARE;
	}

	if (nvt_get_fw_info_noflash(chip_info)) {
		TPD_INFO("get fw info failed!\n");
		goto RELEASE_FIRMWARE;
	}

	TPD_INFO("%s: mallocing nvt_test_para\n", __func__);
	chip_info->p_nvt_test_para = NULL;
	chip_info->p_nvt_test_para = tp_devm_kzalloc(&chip_info->s_client->dev,
					 sizeof(struct nvt_autotest_para), GFP_KERNEL);

	if (!chip_info->p_nvt_test_para) {
		goto RELEASE_DATA;
	}

	TPD_INFO("%s: mallocing nvt_autotest_offset \n", __func__);
	chip_info->p_nvt_autotest_offset = NULL;
	chip_info->p_nvt_autotest_offset = tp_devm_kzalloc(&chip_info->s_client->dev,
					   sizeof(struct nvt_autotest_offset), GFP_KERNEL);

	if (!chip_info->p_nvt_autotest_offset) {
		goto RELEASE_DATA;
	}

	ph = (struct auto_test_header *)(ts->com_test_data.limit_fw->data);

	TPD_INFO("start to dump img \n");
	p_print = (uint8_t *)ph;
	for (i = 0; i < 16 * 8; i++) {
		if (i % 16 == 0) {
			TPD_INFO("current line [%d]: \n", i / 16);
		}
		TPD_INFO("0x%x \n", *(p_print + i * sizeof(uint8_t)));
	}
	TPD_INFO("end of dump img \n");

	p_item_offset = (uint32_t *)(ts->com_test_data.limit_fw->data + 16);

	for (i = 0; i < 8 * sizeof(ph->test_item); i++) {
		if ((ph->test_item >> i) & 0x01) {
			item_cnt++;
		}
	}
	memset(data_buf, 0, sizeof(data_buf));
	snprintf(data_buf, 128, "FW Version Name:%s\nTotal test item = %d\n",
		ts->panel_data.manufacture_info.version, item_cnt);
	tp_test_write(nvt_testdata->fp, nvt_testdata->length, data_buf,
		strlen(data_buf), nvt_testdata->pos);

	TPD_INFO("%s: total test item = %d\n", __func__, item_cnt);

	for (m = 0; m < item_cnt; m++) {
		item_head = (struct auto_test_item_header *)(ts->com_test_data.limit_fw->data +
				p_item_offset[m]);

		if (item_head->item_limit_type == LIMIT_TYPE_NO_DATA) {
			TPD_INFO("[%d] incorrect item type: LIMIT_TYPE_NO_DATA\n", item_head->item_bit);

		} else if (item_head->item_limit_type == LIMIT_TYPE_TOP_FLOOR_DATA) {
			TPD_INFO("test item bit [%d]\n", item_head->item_bit);

			switch (item_head->item_bit) {
			case TYPE_FW_RAWDATA   : NVT_LIMIT_LOADING(fw_rawdata)    ; break;
			case TYPE_OPEN_RAWDATA : NVT_LIMIT_LOADING(open_rawdata)  ; break;
			case TYPE_SHORT_RAWDATA: NVT_LIMIT_LOADING(short_rawdata) ; break;
			case TYPE_CC_DATA      : NVT_LIMIT_LOADING(cc_data)       ; break;
			case TYPE_DIFF_RAWDATA : NVT_LIMIT_LOADING(diff_rawdata)  ; break;
			case TYPE_DIGITAL_DIFF : NVT_LIMIT_LOADING(digital_diff)  ; break;
			default:
				TPD_INFO("[%d] unknown autotest item bit \n", item_head->item_bit);
			break;
			}

		} else if (item_head->item_limit_type == LIMIT_TYPE_DOZE_FDM_DATA) {
			TPD_INFO("test item bit [%d]\n", item_head->item_bit);

			switch (item_head->item_bit) {
			case TYPE_DOZE_DIFF_RAWDATA: NVT_LIMIT_LOADING(doze_diff_rawdata); break;
			case TYPE_DOZE_RAWDATA     : NVT_LIMIT_LOADING(doze_rawdata)     ; break;
			default:
				TPD_INFO("[%d] unknown doze rawdata item bit \n", item_head->item_bit);
			break;
			}

			if (item_head->para_num) {
				if (item_head->para_num == 1) {
					p_data32 = (int32_t *)(
							ts->com_test_data.limit_fw->data + p_item_offset[m] +
							sizeof(struct auto_test_item_header));
					chip_info->p_nvt_test_para->doze_x_channel = p_data32[0];
					chip_info->p_nvt_test_para->doze_y_channel = chip_info->hw_res->rx_num;
				} else if (item_head->para_num == 2) {
					p_data32 = (int32_t *)(ts->com_test_data.limit_fw->data + p_item_offset[m] +
							sizeof(struct auto_test_item_header));
					chip_info->p_nvt_test_para->doze_x_channel = p_data32[0];
					chip_info->p_nvt_test_para->doze_y_channel = p_data32[1];
				}
				TPD_INFO("doze_x_channel = %d\n", chip_info->p_nvt_test_para->doze_x_channel);
				TPD_INFO("doze_y_channel = %d\n", chip_info->p_nvt_test_para->doze_y_channel);
			}
		} else if ((item_head->item_limit_type == LIMIT_TYPE_PEN_X_DATA) ||
				   (item_head->item_limit_type == LIMIT_TYPE_PEN_Y_DATA)) {
			TPD_INFO("test item bit [%d]\n", item_head->item_bit);

			switch (item_head->item_bit) {
			case TYPE_PEN_X_TIP       : NVT_LIMIT_LOADING(pen_tip_x_data)       ; break;
			case TYPE_PEN_Y_TIP       : NVT_LIMIT_LOADING(pen_tip_y_data)       ; break;
			case TYPE_PEN_X_RING      : NVT_LIMIT_LOADING(pen_ring_x_data)      ; break;
			case TYPE_PEN_Y_RING      : NVT_LIMIT_LOADING(pen_ring_y_data)      ; break;
			case TYPE_PEN_X_TIP_NOISE : NVT_LIMIT_LOADING(pen_tip_x_noise_data) ; break;
			case TYPE_PEN_Y_TIP_NOISE : NVT_LIMIT_LOADING(pen_tip_y_noise_data) ; break;
			case TYPE_PEN_X_RING_NOISE: NVT_LIMIT_LOADING(pen_ring_x_noise_data); break;
			case TYPE_PEN_Y_RING_NOISE: NVT_LIMIT_LOADING(pen_ring_y_noise_data); break;
			default:
				TPD_INFO("[%d] unknown pen item bit \n", item_head->item_bit);
			break;
			}
		} else {
			TPD_INFO("[%d] unknown item type\n", item_head->item_bit);
		}
	}

	TPD_INFO("%s: populating nvt_test_para\n", __func__);
	chip_info->p_nvt_test_para->config_lmt_short_rawdata_p		= 0;
	chip_info->p_nvt_test_para->config_lmt_short_rawdata_n		= 0;
	chip_info->p_nvt_test_para->config_diff_test_frame			= 50;
	chip_info->p_nvt_test_para->config_lmt_fw_diff_p			  = 0;
	chip_info->p_nvt_test_para->config_lmt_fw_diff_n			  = 0;
	chip_info->p_nvt_test_para->config_lmt_fw_cc_p				= 0;
	chip_info->p_nvt_test_para->config_lmt_fw_cc_n				= 0;
	chip_info->p_nvt_test_para->config_lmt_fw_digital_p		   = 0;
	chip_info->p_nvt_test_para->config_lmt_fw_digital_n		   = 0;
	chip_info->p_nvt_test_para->config_lmt_doze_rawdata_p		 = 0;
	chip_info->p_nvt_test_para->config_lmt_doze_rawdata_n		 = 0;
	chip_info->p_nvt_test_para->config_doze_noise_test_frame	  = 50;
	chip_info->p_nvt_test_para->config_lmt_doze_diff_p			= 0;
	chip_info->p_nvt_test_para->config_lmt_doze_diff_n			= 0;
	chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_p		 = 0;
	chip_info->p_nvt_test_para->config_lmt_lpwg_rawdata_n		 = 0;
	chip_info->p_nvt_test_para->config_lmt_lpwg_diff_p			= 0;
	chip_info->p_nvt_test_para->config_lmt_lpwg_diff_n			= 0;
	chip_info->p_nvt_test_para->config_lmt_fdm_rawdata_p		  = 0;
	chip_info->p_nvt_test_para->config_lmt_fdm_rawdata_n		  = 0;
	chip_info->p_nvt_test_para->config_fdm_noise_test_frame	   = 50;
	chip_info->p_nvt_test_para->config_lmt_fdm_diff_p			 = 0;
	chip_info->p_nvt_test_para->config_lmt_fdm_diff_n			 = 0;
	TPD_INFO("%s -\n", __func__);

	return 0;

RELEASE_DATA:
	tp_devm_kfree(&chip_info->s_client->dev,
			  (void **)&chip_info->p_nvt_autotest_offset, sizeof(struct nvt_autotest_offset));
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&chip_info->p_nvt_test_para,
			  sizeof(struct nvt_autotest_para));

RELEASE_FIRMWARE:
	if (ts->com_test_data.black_test_fw) {
		release_firmware(ts->com_test_data.black_test_fw);
		ts->com_test_data.black_test_fw = NULL;
	}

	if (ts->com_test_data.limit_fw) {
		release_firmware(ts->com_test_data.limit_fw);
		ts->com_test_data.limit_fw = NULL;
	}

	tp_devm_kfree(&chip_info->s_client->dev, (void **)&fw_name_test,
			  MAX_FW_NAME_LENGTH);

	TPD_INFO("%s - [abnormal]\n", __func__);
	return -1;
}

static int nvt_autotest_endoperation(struct seq_file *s, void *chip_data,
					 struct auto_testdata *nvt_testdata, struct test_item_info *p_test_item_info)
{
	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	/* struct touchpanel_data *ts = chip_info->ts; */
	struct touchpanel_data *ts = spi_get_drvdata(chip_info->s_client);

	TPD_INFO("%s +\n", __func__);
	tp_devm_kfree(&chip_info->s_client->dev,
			  (void **)&chip_info->p_nvt_autotest_offset, sizeof(struct nvt_autotest_offset));
	tp_devm_kfree(&chip_info->s_client->dev, (void **)&chip_info->p_nvt_test_para,
			  sizeof(struct nvt_autotest_para));

	if (ts->com_test_data.black_test_fw) {
		release_firmware(ts->com_test_data.black_test_fw);
		ts->com_test_data.black_test_fw = NULL;
	}

	if (ts->com_test_data.limit_fw) {
		release_firmware(ts->com_test_data.limit_fw);
		ts->com_test_data.limit_fw = NULL;
	}

	TPD_INFO("%s -\n", __func__);

	return 0;
}


#ifdef CONFIG_OPLUS_TP_APK

static void nova_apk_game_set(void *chip_data, bool on_off)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;
	nvt_mode_switch(chip_data, MODE_GAME, on_off);
	chip_info->lock_point_status = on_off;
}

static bool nova_apk_game_get(void *chip_data)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;
	return chip_info->lock_point_status;
}

static void nova_apk_debug_set(void *chip_data, bool on_off)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;

	nvt_enable_debug_msg_diff_mode(chip_info, on_off);
	chip_info->debug_mode_sta = on_off;
}

static bool nova_apk_debug_get(void *chip_data)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;

	return chip_info->debug_mode_sta;
}

static void nova_apk_gesture_debug(void *chip_data, bool on_off)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;
	chip_info->debug_gesture_sta = on_off;
}

static bool  nova_apk_gesture_get(void *chip_data)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;
	return chip_info->debug_gesture_sta;
}

static int  nova_apk_gesture_info(void *chip_data, char *buf, int len)
{
	int ret = 0;
	int i;
	int num;
	u8 temp;
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;

	if (len < 2) {
		return 0;
	}

	buf[0] = 255;

	temp = chip_info->ts->gesture_buf[0];

	switch (temp) {   /*judge gesture type*/
	case RIGHT_SLIDE_DETECT :
		buf[0]  = LEFT2RIGHT_SWIP;
		break;

	case LEFT_SLIDE_DETECT :
		buf[0]  = RIGHT2LEFT_SWIP;
		break;

	case DOWN_SLIDE_DETECT  :
		buf[0]  = UP2DOWN_SWIP;
		break;

	case UP_SLIDE_DETECT :
		buf[0]  = DOWN2UP_SWIP;
		break;

	case DTAP_DETECT:
		buf[0]  = DOU_TAP;
		break;

	case UP_VEE_DETECT :
		buf[0]  = UP_VEE;
		break;

	case DOWN_VEE_DETECT :
		buf[0]  = DOWN_VEE;
		break;

	case LEFT_VEE_DETECT:
		buf[0] = LEFT_VEE;
		break;

	case RIGHT_VEE_DETECT :
		buf[0]  = RIGHT_VEE;
		break;

	case CIRCLE_DETECT  :
		buf[0] = CIRCLE_GESTURE;
		break;

	case DOUSWIP_DETECT  :
		buf[0]  = DOU_SWIP;
		break;

	case M_DETECT  :
		buf[0]  = M_GESTRUE;
		break;

	case W_DETECT :
		buf[0]  = W_GESTURE;
		break;

	case PEN_DETECT:
		buf[0] = PenDetect;
		break;

	default:
		buf[0] = temp | 0x80;
		break;
	}

	num = chip_info->ts->gesture_buf[1];

	if (num > 208) {
		num = 208;
	}

	ret = 2;

	buf[1] = num;

	/*print all data*/
	for (i = 0; i < num; i++) {
		int x;
		int y;
		x = chip_info->ts->gesture_buf[i * 3 + 2] << 4;
		x = x + (chip_info->ts->gesture_buf[i * 3 + 4] >> 4);

		y = chip_info->ts->gesture_buf[i * 3 + 3] << 4;
		y = y + (chip_info->ts->gesture_buf[i * 3 + 4] & 0x0F);

		/*TPD_INFO("nova_apk_gesture_info:gesture x is %d,y is %d.\n", x, y);*/

		if (len < i * 4 + 2) {
			break;
		}

		buf[i * 4 + 2] = x & 0xFF;
		buf[i * 4 + 3] = (x >> 8) & 0xFF;
		buf[i * 4 + 4] = y & 0xFF;
		buf[i * 4 + 5] = (y >> 8) & 0xFF;
		ret += 4;
	}

	return ret;
}


static void nova_apk_earphone_set(void *chip_data, bool on_off)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;
	nvt_mode_switch(chip_data, MODE_HEADSET, on_off);
	chip_info->earphone_sta = on_off;
}

static bool nova_apk_earphone_get(void *chip_data)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;
	return chip_info->earphone_sta;
}

static void nova_apk_charger_set(void *chip_data, bool on_off)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;
	nvt_mode_switch(chip_data, MODE_CHARGE, on_off);
	chip_info->plug_status = on_off;
}

static bool nova_apk_charger_get(void *chip_data)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;

	return chip_info->plug_status;
}

static void nova_apk_noise_set(void *chip_data, bool on_off)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;
	nvt_enable_hopping_polling_mode(chip_info, on_off);

	chip_info->noise_sta = on_off;
}

static bool nova_apk_noise_get(void *chip_data)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;

	return chip_info->noise_sta;
}

static void nova_apk_water_set(void *chip_data, int type)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;

	if (type > 0) {
		nvt_enable_water_polling_mode(chip_info, true);

	} else {
		nvt_enable_water_polling_mode(chip_info, false);
	}

	chip_info->water_sta = type;
}

static int nova_apk_water_get(void *chip_data)
{
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;

	return chip_info->water_sta;
}

static int  nova_apk_tp_info_get(void *chip_data, char *buf, int len)
{
	int ret;
	struct chip_data_nt36523 *chip_info;
	chip_info = (struct chip_data_nt36523 *)chip_data;
	ret = snprintf(buf, len, "IC:NOVA%04X\nFW_VER:0x%02X\nCH:%dX%d\n",
					0x523,
					chip_info->fw_ver,
					chip_info->hw_res->tx_num,
					chip_info->hw_res->rx_num);

	if (ret > len) {
		ret = len;
	}

	return ret;
}

static void nova_init_oplus_apk_op(struct touchpanel_data *ts)
{
	ts->apk_op = tp_devm_kzalloc(&chip_info->s_client->dev, sizeof(APK_OPERATION), GFP_KERNEL);

	if (ts->apk_op) {
		ts->apk_op->apk_game_set = nova_apk_game_set;
		ts->apk_op->apk_game_get = nova_apk_game_get;
		ts->apk_op->apk_debug_set = nova_apk_debug_set;
		ts->apk_op->apk_debug_get = nova_apk_debug_get;
		ts->apk_op->apk_noise_set = nova_apk_noise_set;
		ts->apk_op->apk_noise_get = nova_apk_noise_get;
		ts->apk_op->apk_gesture_debug = nova_apk_gesture_debug;
		ts->apk_op->apk_gesture_get = nova_apk_gesture_get;
		ts->apk_op->apk_gesture_info = nova_apk_gesture_info;
		ts->apk_op->apk_earphone_set = nova_apk_earphone_set;
		ts->apk_op->apk_earphone_get = nova_apk_earphone_get;
		ts->apk_op->apk_charger_set = nova_apk_charger_set;
		ts->apk_op->apk_charger_get = nova_apk_charger_get;
		ts->apk_op->apk_tp_info_get = nova_apk_tp_info_get;
		ts->apk_op->apk_water_set = nova_apk_water_set;
		ts->apk_op->apk_water_get = nova_apk_water_get;

	} else {
		TPD_INFO("Can not kzalloc apk op.\n");
	}
}
#endif /* end of CONFIG_OPLUS_TP_APK*/

static struct nvt_auto_test_operations nvt_test_ops = {
	.auto_test_preoperation  = nvt_autotest_preoperation,
	.test1				   = nvt_fw_rawdata_test,
	.test2				   = nvt_cc_rawdata_test,
	.test3				   = nvt_fw_noise_test,
	.test4				   = nvt_doze_noise_test,
	.test5				   = nvt_doze_fw_rawdata_test,
	.test6				   = nvt_short_test,
	.test7				   = nvt_open_test,
	.test8				   = nvt_digital_noise_test,
	.auto_test_endoperation  = nvt_autotest_endoperation,

	.black_screen_test_preoperation = nvt_black_screen_test_preoperation,
	.black_screen_test1	  = nvt_lpwg_rawdata_test,
	.black_screen_test2	  = nvt_lpwg_diff_rawdata_test,
	.black_screen_test3	  = nvt_fdm_diff_rawdata_test,
	.black_screen_test4	  = nvt_fdm_rawdata_test,
	.black_screen_test_endoperation = nvt_black_screen_test_endoperation,
};

static struct engineer_test_operations nvt_engineer_test_ops = {
	.auto_test				  = nvt_auto_test,
	.black_screen_test		  = nvt_black_screen_autotest,
};

static void nvt_prase_dts(struct device *dev, void *chip_data)
{
	int rc;
	int i;
	int length;
	struct device_node *np;

	struct chip_data_nt36523 *chip_info = (struct chip_data_nt36523 *)chip_data;
	np = dev->of_node;

	rc = of_property_read_u32(np, "touchpanel,doze-tx-num", &chip_info->doze_x_num);
	if (rc < 0) {
		TPD_INFO("chip_info->doze_x_num not specified\n");
		chip_info->doze_x_num = 0;
	}

	rc = of_property_read_u32(np, "touchpanel,doze-rx-num", &chip_info->doze_y_num);
	if (rc < 0) {
		TPD_INFO("chip_info->doze_y_num not specified\n");
		chip_info->doze_y_num = 0;
	}

	TPD_INFO("chip_info->doze_x_num = %d,chip_info->doze_y_num = %d\n", chip_info->doze_x_num, chip_info->doze_y_num);

	rc = of_property_count_u32_elems(np, "touchpanel,pen-id-map");
	if (rc < 0) {
		TPD_INFO("chip_info->pen_id_map not specified\n");
		rc = 0;
	}
	length = rc;
	chip_info->pen_id_map_num = 0;

	if (length != 0 && length % 2 == 0) {
		chip_info->pen_id_map_num = length / 2;
		chip_info->pen_id_map_array = devm_kzalloc(dev, chip_info->pen_id_map_num * sizeof(struct pen_id_map), GFP_KERNEL);

		if (chip_info->pen_id_map_array) {
			rc = of_property_read_u32_array(np, "touchpanel,pen-id-map", (uint32_t *)chip_info->pen_id_map_array, length);
			if (rc < 0) {
				TPD_INFO("parse touchpanel,pen-id-map failed, rc=%d\n", rc);
				devm_kfree(dev, chip_info->pen_id_map_array);
				chip_info->pen_id_map_num = 0;
			} else {
				TPD_INFO("pen_id_map num = %d\n", chip_info->pen_id_map_num);
				for (i = 0; i < chip_info->pen_id_map_num; i++) {
					TPD_INFO("src_id: %d , dst_id: %d\n", chip_info->pen_id_map_array[i].src_id, chip_info->pen_id_map_array[i].dst_id);
				}
			}
		}
	} else {
		TPD_INFO("pen_id_map elements should be multiple of 2. length = %d\n", length);
	}
}

static int nvt_tp_probe(struct spi_device *client)
{
	struct chip_data_nt36523 *chip_info = NULL;
	struct touchpanel_data *ts = NULL;
	int ret = -1;
	u64 time_counter = 0;

	TPD_INFO("%s  is called\n", __func__);
	/* 0. healthinfo */
	reset_healthinfo_time_counter(&time_counter);

	/* 1. alloc chip info */
	chip_info = kzalloc(sizeof(struct chip_data_nt36523), GFP_KERNEL);

	if (chip_info == NULL) {
		TPD_INFO("chip info kzalloc error\n");
		ret = -ENOMEM;
		return ret;
	}

	chip_info->probe_done = 0;
	/* 2. Alloc common ts */
	ts = common_touch_data_alloc();

	if (ts == NULL) {
		TPD_INFO("ts kzalloc error\n");
		goto ts_malloc_failed;
	}

	chip_info->g_fw_buf = tp_vzalloc(FW_BUF_SIZE);

	if (chip_info->g_fw_buf == NULL) {
		TPD_INFO("fw buf tp_vzalloc error\n");
		goto err_g_fw_buf;
	}

	chip_info->g_fw_sta = false;

	chip_info->fw_buf_dma = kzalloc(FW_BUF_SIZE, GFP_KERNEL | GFP_DMA);

	if (chip_info->fw_buf_dma == NULL) {
		TPD_INFO("fw kzalloc error\n");
		goto err_fw_dma;
	}

	/* 3. bind client and dev for easy operate */
	chip_info->s_client = client;
	ts->debug_info_ops = &debug_info_proc_ops;
	ts->s_client = client;
	ts->irq = client->irq;
	spi_set_drvdata(client, ts);
	ts->dev = &client->dev;
	ts->chip_data = chip_info;
	chip_info->irq_num = ts->irq;
	chip_info->hw_res = &ts->hw_res;
	chip_info->ENG_RST_ADDR = 0x7FFF80;
	chip_info->recovery_cnt = 0;
	chip_info->partition = 0;
	chip_info->ilm_dlm_num = 2;
	chip_info->cascade_2nd_header_info = 0;
	chip_info->p_firmware_headfile = &ts->panel_data.firmware_headfile;
	chip_info->touch_direction = VERTICAL_SCREEN;
	mutex_init(&chip_info->mutex_testing);
	chip_info->using_headfile = false;
	chip_info->is_pen_attracted = &ts->is_pen_attracted;
	chip_info->is_pen_connected = &ts->is_pen_connected;

	/*---prepare for spi parameter---*/
	if (ts->s_client->master->flags & SPI_MASTER_HALF_DUPLEX) {
		TPD_INFO("Full duplex not supported by master\n");
		ret = -EIO;
		goto err_spi_setup;
	}

	ts->s_client->bits_per_word = 8;
	ts->s_client->mode = SPI_MODE_0;
	ts->s_client->chip_select = 0; /*modify reg=0 for more tp vendor share same spi interface*/

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
	/* new usage of MTK spi API */
	memcpy(&chip_info->spi_ctrl, &spi_ctrdata, sizeof(struct mtk_chip_config));
	ts->s_client->controller_data = (void *)&chip_info->spi_ctrl;
#else /* else of CONFIG_TOUCHPANEL_MTK_PLATFORM*/
	ret = spi_setup(ts->s_client);

	if (ret < 0) {
		TPD_INFO("Failed to perform SPI setup\n");
		goto err_spi_setup;
	}

#endif /* end of CONFIG_TOUCHPANEL_MTK_PLATFORM*/
	ts->bus_ready = true;
	TPD_INFO("mode=%d, max_speed_hz=%d\n", ts->s_client->mode,
		 ts->s_client->max_speed_hz);

	/* 4. file_operations callbacks binding */
	ts->ts_ops = &nvt_ops;
	ts->engineer_ops = &nvt_engineer_test_ops;
	ts->com_test_data.chip_test_ops = &nvt_test_ops;

	/* 5. register common touch device*/
	ret = register_common_touch_device(ts);

	if (ret < 0) {
		goto err_register_driver;
	}

	ts->tp_suspend_order = TP_LCD_SUSPEND;
	ts->tp_resume_order = LCD_TP_RESUME;
	chip_info->is_sleep_writed = false;
	chip_info->fw_name = ts->panel_data.fw_name;
	chip_info->dev = ts->dev;
	chip_info->test_limit_name = ts->panel_data.test_limit_name;
	chip_info->monitor_data = &ts->monitor_data;

	nvt_prase_dts(ts->dev, chip_info);


	/*reset esd handle time interval*/
	if (ts->esd_handle_support) {
		chip_info->esd_check_enabled = false;
		ts->esd_info.esd_work_time = msecs_to_jiffies(
							 NVT_TOUCH_ESD_CHECK_PERIOD); /* change esd check interval to 1.5s*/
		TPD_INFO("%s:change esd handle time to %d s\n", __func__,
			 ts->esd_info.esd_work_time / HZ);
	}

	/*6. create nvt test files*/
	nvt_flash_proc_init(ts, "NVTSPI");
	nvt_flash_proc_init(ts, "NVTModeSet");

	chip_info->ts = ts;
#ifdef CONFIG_OPLUS_TP_APK
	nova_init_oplus_apk_op(ts);
#endif /* end of CONFIG_OPLUS_TP_APK*/

	/* update fw in probe*/
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM

	if (ts->boot_mode == RECOVERY_BOOT || is_oem_unlocked()
			|| ts->fw_update_in_probe_with_headfile)
#else
	pr_err("[TP]:%s ts->boot_mode %d \n", __func__, ts->boot_mode);
	pr_err("[TP]:%s is_oem_unlocked() %d \n", __func__, is_oem_unlocked());
	pr_err("[TP]:%s ts->fw_update_in_probe_with_headfile %d \n", __func__, ts->fw_update_in_probe_with_headfile);
	if (ts->boot_mode == MSM_BOOT_MODE__RECOVERY || is_oem_unlocked()
			|| ts->fw_update_in_probe_with_headfile)
#endif
	 {
		TPD_INFO("In Recovery mode, no-flash download fw by headfile\n");
		ret = nvt_fw_update(chip_info, NULL, 0);

		if (ret > 0) {
			TPD_INFO("fw update failed!\n");
		}
	}

	chip_info->probe_done = 1;
	chip_info->current_pencil_type = TYPE_PENCIL_HAVON;
	TPD_INFO("%s, probe normal end\n", __func__);

	if (ts->health_monitor_support) {
		tp_healthinfo_report(&ts->monitor_data, HEALTH_PROBE, &time_counter);
	}

	return 0;

err_register_driver:
	common_touch_data_free(ts);
	ts = NULL;

err_spi_setup:

	if (chip_info->fw_buf_dma) {
		kfree(chip_info->fw_buf_dma);
		chip_info->fw_buf_dma = NULL;
	}

	spi_set_drvdata(client, NULL);
err_fw_dma:
	tp_vfree((void **)&chip_info->g_fw_buf);
err_g_fw_buf:
	tp_kfree((void **)&ts);

ts_malloc_failed:
	kfree(chip_info);
	chip_info = NULL;
	/* ret = -1; */

	TPD_INFO("%s, probe error\n", __func__);
	return ret;
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
static void nvt_tp_remove(struct spi_device *client)
#else
static int nvt_tp_remove(struct spi_device *client)
#endif
{
	struct touchpanel_data *ts = spi_get_drvdata(client);

	TPD_INFO("%s is called\n", __func__);
	if (ts) {
		unregister_common_touch_device(ts);
		common_touch_data_free(ts);
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
#else
	return 0;
#endif
}

static int nvt_spi_suspend(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s: is called\n", __func__);

	tp_pm_suspend(ts);

	return 0;
}

static int nvt_spi_resume(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s is called\n", __func__);

	tp_pm_resume(ts);

	return 0;
}

static const struct spi_device_id tp_id[] = {
#ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
	{ "oplus,tp_noflash", 0 },
#else
	{TPD_DEVICE, 0},
#endif
	{},
};

static struct of_device_id tp_match_table[] = {
#ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
	{ .compatible = "oplus,tp_noflash", },
#else
	{ .compatible = TPD_DEVICE, },
#endif
	{ },
};

static const struct dev_pm_ops tp_pm_ops = {
	.suspend = nvt_spi_suspend,
	.resume = nvt_spi_resume,
};

static struct spi_driver tp_spi_driver = {
	.probe	  = nvt_tp_probe,
	.remove	 = nvt_tp_remove,
	.id_table   = tp_id,
	.driver = {
		.name   = TPD_DEVICE,
		.owner  = THIS_MODULE,
		.of_match_table = tp_match_table,
		.pm = &tp_pm_ops,
	},
};


static int __init nvt_driver_init(void)
{
	TPD_INFO("%s is called\n", __func__);

#if IS_BUILTIN(CONFIG_TOUCHPANEL_OPLUS)
	if (!tp_judge_ic_match(DRIVER_NAME)) {
		goto OUT;
	}

	get_oem_verified_boot_state();
#endif
	if (spi_register_driver(&tp_spi_driver) != 0) {
		TPD_INFO("unable to add spi driver.\n");
		goto OUT;
	}
OUT:
	return 0;
}

static void __exit nvt_driver_exit(void)
{
	spi_unregister_driver(&tp_spi_driver);
	return;
}

#ifdef CONFIG_TOUCHPANEL_LATE_INIT
late_initcall(nvt_driver_init);
#else
module_init(nvt_driver_init);
#endif
module_exit(nvt_driver_exit);

MODULE_DESCRIPTION("Novatek nt36523 noflash Driver");
MODULE_LICENSE("GPL");
