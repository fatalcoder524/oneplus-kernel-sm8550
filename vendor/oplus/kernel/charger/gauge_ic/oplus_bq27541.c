// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/version.h>
#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#include <asm/unaligned.h>
#include <linux/module.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
#ifndef CONFIG_OPLUS_CHARGER_MTK6779R
#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include <mt-plat/charger_type.h>
#else
#include <mt-plat/v1/charger_type.h>
#endif
#endif
#endif
#include <linux/xlog.h>
#endif
#include <linux/power_supply.h>
#include <linux/gpio.h>
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/device_info.h>
#endif
#include <linux/proc_fs.h>
#include <linux/init.h>
#else
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/device_info.h>
#endif
#include <linux/proc_fs.h>
#include <linux/soc/qcom/smem.h>
#endif
#include <linux/gfp.h>
#include <linux/iio/consumer.h>

#ifdef OPLUS_SHA1_HMAC
#include <linux/random.h>
#endif
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include "../voocphy/oplus_voocphy.h"
#include "oplus_bq27541.h"
#include "../oplus_debug_info.h"
#include "../oplus_pps.h"
#include "../oplus_ufcs.h"
#include "oplus_sh366002.h"
#include "oplus_bqfs.h"
#include "oplus_nfg1000a.h"
#include "../oplus_chg_module.h"

#define GAUGE_ERROR -1
#define GAUGE_OK 0
#define BATT_FULL_ERROR 2
#define XBL_AUTH_DEBUG
#define VOLT_MIN		1000
#define VOLT_MAX		5000
#define CURR_MAX		25000
#define CURR_MIN		-20000
#define TEMP_MAX		1000 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN
#define TEMP_MIN		-400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN
#define SOH_MIN			0
#define SOH_MAX			100
#define FCC_MIN			10
#define FCC_MAX			12000
#define CC_MIN			0
#define CC_MAX			5000
#define QMAX_MIN		10
#define QMAX_MAX		12000
#define RETRY_CNT		3

static struct chip_bq27541 *gauge_ic = NULL;
static struct chip_bq27541 *sub_gauge_ic = NULL;
static struct oplus_external_auth_chip *external_auth_chip = NULL;

static oplus_gauge_auth_result auth_data;
static bool get_smem_batt_info(oplus_gauge_auth_result *auth, int kk);
static bool init_gauge_auth(oplus_gauge_auth_result *rst, struct bq27541_authenticate_data *authenticate_data);
static int bq27411_write_soc_smooth_parameter(struct chip_bq27541 *chip, bool is_powerup);
static int zy0603_unseal(struct chip_bq27541 *chip, u32 key);
static int zy0603_seal(struct chip_bq27541 *chip, u32 key);
static bool zy0603_afi_update_done(void);
static int bq27z561_unseal(struct chip_bq27541 *chip, u32 key);
static int bq27z561_seal(struct chip_bq27541 *chip, u32 key);
static bool bq27z561_deep_init(void);
static void bq27z561_deep_deinit(void);
static int gauge_get_info(u8 *info, int len);
static int sub_gauge_get_info(u8 *info, int len);
static int gauge_get_calib_time(int *dod_calib_time, int *qmax_calib_time, int gauge_index);
static int bq27z561_get_qmax_parameters(struct chip_bq27541 *chip, int * qmax);
static int bq27z561_get_dod0_parameters(
struct chip_bq27541 *chip, int *batt_dod0, int *batt_dod0_passed_q);
static bool init_sha256_gauge_auth(struct chip_bq27541 *chip);
static int bq27541_track_upload_mode_info(struct chip_bq27541 *chip);

#define GAUGE_READ_ERR 0x01
#define GAUGE_WRITE_ERR 0x02
static int gauge_i2c_err = 0;

int oplus_vooc_get_fast_chg_type(void);
bool oplus_vooc_get_fastchg_ing(void);
bool oplus_vooc_get_vooc_by_normal_path(void);
int oplus_battery_type_check_bqfs(struct chip_bq27541 *chip);

int __attribute__((weak)) oplus_get_fg_device_type(void)
{
	return 0;
}

void __attribute__((weak)) oplus_set_fg_device_type(int device_type)
{
	return;
}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FAULT_INJECT_CHG)
noinline s32 oplus_i2c_smbus_read_byte_data(const struct i2c_client *client, u8 command)
{
	return i2c_smbus_read_byte_data(client, command);
}

noinline s32 oplus_i2c_smbus_write_byte_data(const struct i2c_client *client, u8 command, u8 value)
{
	return i2c_smbus_write_byte_data(client, command, value);
}

noinline s32 oplus_i2c_smbus_read_word_data(const struct i2c_client *client, u8 command)
{
	return i2c_smbus_read_word_data(client, command);
}

noinline s32 oplus_i2c_smbus_write_word_data(const struct i2c_client *client, u8 command, u16 value)
{
	return i2c_smbus_write_word_data(client, command, value);
}
#else
#define oplus_i2c_smbus_read_byte_data i2c_smbus_read_byte_data
#define oplus_i2c_smbus_write_byte_data i2c_smbus_write_byte_data
#define oplus_i2c_smbus_read_word_data i2c_smbus_read_word_data
#define oplus_i2c_smbus_write_word_data i2c_smbus_write_word_data
#endif

/**********************************************************
  *
  *   [I2C Function For Read/Write bq27541]
  *
  *********************************************************/
#define I2C_RETRY_MAX_COUNT   2
int gauge_read_i2c(struct chip_bq27541 *chip, int cmd, int *returnData)
{
	int retry = I2C_RETRY_MAX_COUNT;
	int retry_c = I2C_RETRY_MAX_COUNT;
	if (!chip->client) {
		pr_err(" chip->client NULL, return\n");
		return 0;
	}
	if(oplus_is_rf_ftm_mode()) {
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}

	mutex_lock(&chip->chip_mutex);
	*returnData = oplus_i2c_smbus_read_word_data(chip->client, cmd);
	if (chip->device_type == DEVICE_ZY0602 || cmd == BQ27541_BQ27411_REG_CNTL) {
		if (*returnData < 0) {
			while (retry > 0) {
				usleep_range(5000, 5000);
				*returnData = oplus_i2c_smbus_read_word_data(chip->client, cmd);
				if (*returnData < 0) {
					retry--;
				} else {
					break;
				}
			}
		}
	} else {
		if (*returnData < 0) {
			while (retry_c > 0) {
				usleep_range(5000, 5000);
				*returnData = oplus_i2c_smbus_read_word_data(chip->client, cmd);
				if (*returnData < 0) {
					retry_c--;
				} else {
					break;
				}
			}
		}
	}

	mutex_unlock(&chip->chip_mutex);
	/* pr_err(" cmd = 0x%x, returnData = 0x%x\r\n", cmd, *returnData); */
	if (*returnData < 0) {
		pr_err("%s read err, rc = %d suspend = %d\n", __func__, *returnData, atomic_read(&chip->suspended));
		if (atomic_read(&chip->suspended) == 0) {
			gauge_i2c_err |= GAUGE_READ_ERR;
			atomic_set(&chip->gauge_i2c_status, 1);
		}
		return 1;
	} else {
		/*clear the i2c_status*/
		if (atomic_read(&chip->gauge_i2c_status))
			atomic_set(&chip->gauge_i2c_status, 0);
		return 0;
	}
}

int gauge_i2c_txsubcmd(struct chip_bq27541 *chip, int cmd, int writeData)
{
	int rc = 0;
	int retry = I2C_RETRY_MAX_COUNT;

	if (!chip->client) {
		pr_err(" chip->client NULL, return\n");
		return 0;
	}
	if(oplus_is_rf_ftm_mode()) {
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	mutex_lock(&chip->chip_mutex);
	rc = oplus_i2c_smbus_write_word_data(chip->client, cmd, writeData);

	if (rc < 0) {
		while (retry > 0) {
			usleep_range(5000, 5000);
			rc = oplus_i2c_smbus_write_word_data(chip->client, cmd, writeData);
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (rc < 0) {
		pr_err("%s write err, rc = %d suspend = %d\n", __func__, rc, atomic_read(&chip->suspended));
		if (atomic_read(&chip->suspended) == 0) {
			gauge_i2c_err |= GAUGE_WRITE_ERR;
			atomic_set(&chip->gauge_i2c_status, 1);
		}
	} else {
		/*clear the i2c_status*/
		if (atomic_read(&chip->gauge_i2c_status))
			atomic_set(&chip->gauge_i2c_status, 0);
	}
	mutex_unlock(&chip->chip_mutex);
	return 0;
}

int gauge_write_i2c_block(struct chip_bq27541 *chip, u8 cmd, u8 length, u8 *writeData)
{
	int rc = 0;
	int retry = I2C_RETRY_MAX_COUNT;

	if (!chip->client) {
		pr_err(" chip->client NULL, return\n");
		return 0;
	}
	if(oplus_is_rf_ftm_mode()) {
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	mutex_lock(&chip->chip_mutex);
	rc = i2c_smbus_write_i2c_block_data(chip->client, cmd, length, writeData);

	if (rc < 0) {
		while (retry > 0) {
			usleep_range(5000, 5000);
			rc = i2c_smbus_write_i2c_block_data(chip->client, cmd, length, writeData);
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (rc < 0) {
		pr_err("%s write err, rc = %d suspend = %d\n", __func__, rc, atomic_read(&chip->suspended));
		if (atomic_read(&chip->suspended) == 0) {
			gauge_i2c_err |= GAUGE_WRITE_ERR;
			atomic_set(&chip->gauge_i2c_status, 1);
		}
	} else {
		/*clear the i2c_status*/
		if (atomic_read(&chip->gauge_i2c_status))
			atomic_set(&chip->gauge_i2c_status, 0);
	}

	mutex_unlock(&chip->chip_mutex);
	return 0;
}

int gauge_read_i2c_block(struct chip_bq27541 *chip, u8 cmd, u8 length, u8 *returnData)
{
	int rc = 0;
	int retry = I2C_RETRY_MAX_COUNT;

	if (!chip->client) {
		pr_err(" chip->client NULL,return\n");
		return 0;
	}
	if(oplus_is_rf_ftm_mode()) {
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;
	mutex_lock(&chip->chip_mutex);
	rc = i2c_smbus_read_i2c_block_data(chip->client, cmd, length, returnData);

	if (rc < 0) {
		while (retry > 0) {
			usleep_range(5000, 5000);
			rc = i2c_smbus_read_i2c_block_data(chip->client, cmd, length, returnData);
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (rc < 0) {
		pr_err("%s read err, rc = %d, suspend = %d\n", __func__, rc, atomic_read(&chip->suspended));
		if (atomic_read(&chip->suspended) == 0) {
			gauge_i2c_err |= GAUGE_READ_ERR;
			atomic_set(&chip->gauge_i2c_status, 1);
		}
		mutex_unlock(&chip->chip_mutex);
		return rc;
	} else {
		/*clear the i2c_status*/
		if (atomic_read(&chip->gauge_i2c_status))
			atomic_set(&chip->gauge_i2c_status, 0);
	}
	mutex_unlock(&chip->chip_mutex);
	return 0;
}

int bq27541_read_i2c_onebyte(struct chip_bq27541 *chip, u8 cmd, u8 *returnData)
{
	int retry = I2C_RETRY_MAX_COUNT;
	s32 ret = 0;

	if (!chip->client) {
		pr_err(" chip->client NULL, return\n");
		return 0;
	}
	if(oplus_is_rf_ftm_mode()) {
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	mutex_lock(&chip->chip_mutex);
	ret = oplus_i2c_smbus_read_byte_data(chip->client, cmd);

	if (ret < 0) {
		while (retry > 0) {
			usleep_range(5000, 5000);
			ret = oplus_i2c_smbus_read_byte_data(chip->client, cmd);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	mutex_unlock(&chip->chip_mutex);
	/*pr_err(" cmd = 0x%x, returnData = 0x%x\r\n", cmd, *returnData) ; */
	if (ret < 0) {
		pr_err("%s read err, rc = %d suspend = %d\n", __func__, ret, atomic_read(&chip->suspended));
		if (atomic_read(&chip->suspended) == 0) {
			gauge_i2c_err |= GAUGE_READ_ERR;
			atomic_set(&chip->gauge_i2c_status, 1);
		}
		*returnData = (u8)ret;
		return 1;
	} else {
		*returnData = (u8)ret;

		if (atomic_read(&chip->gauge_i2c_status))
			atomic_set(&chip->gauge_i2c_status, 0);
		return 0;
	}
}

int gauge_i2c_txsubcmd_onebyte(struct chip_bq27541 *chip, u8 cmd, u8 writeData)
{
	int rc = 0;
	int retry = I2C_RETRY_MAX_COUNT;

	if (!chip->client) {
		pr_err(" chip->client NULL, return\n");
		return 0;
	}
	if(oplus_is_rf_ftm_mode()) {
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	mutex_lock(&chip->chip_mutex);
	rc = oplus_i2c_smbus_write_byte_data(chip->client, cmd, writeData);

	if (rc < 0) {
		while (retry > 0) {
			usleep_range(5000, 5000);
			rc = oplus_i2c_smbus_write_byte_data(chip->client, cmd, writeData);
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (rc < 0) {
		pr_err("%s write err, rc = %d supend = %d\n", __func__, rc, atomic_read(&chip->suspended));
		if (atomic_read(&chip->suspended) == 0) {
			gauge_i2c_err |= GAUGE_WRITE_ERR;
			atomic_set(&chip->gauge_i2c_status, 1);
		}
	} else {
		/*clear the i2c_status*/
		if (atomic_read(&chip->gauge_i2c_status))
			atomic_set(&chip->gauge_i2c_status, 0);
	}
	mutex_unlock(&chip->chip_mutex);
	return 0;
}

static int bq27541_get_gauge_i2c_err(void)
{
	if (!gauge_ic) {
		return 0;
	}
	pr_err("%s gauge_i2c_err = %d suspend = %d\n", __func__, gauge_i2c_err, atomic_read(&gauge_ic->suspended));

	return gauge_i2c_err;
}

static void bq27541_clear_gauge_i2c_err(void)
{
	if (!gauge_ic) {
		pr_err("%s, gauge_ic is null\n", __func__);
		return;
	}

	gauge_i2c_err = 0;

	return;
}

static bool normal_range_judge(int max, int min, int data)
{
	if (data > max || data < min)
		return false;

	return true;
}

static bool zy0603_fg_condition_check(void)
{
	if (gauge_ic == NULL || !gauge_ic->batt_zy0603) {
		chg_err("%s gauge_ic is null or no batt_zy0603\n", __func__);
		return false;
	}

	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		pr_info("%s gauge_ic->locked return\n", __func__);
		return false;
	}

	if (!zy0603_afi_update_done()) {
		pr_info("%s afi_update return\n", __func__);
		return false;
	}

	if (oplus_vooc_get_fastchg_ing() && oplus_vooc_get_vooc_by_normal_path() &&
	    oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC)
		return true;

	if (oplus_vooc_get_allow_reading() == false) {
		pr_info("%s allow reading false or no mcu vooc return\n", __func__);
		return false;
	}

	return true;
}
static int zy0603_read_altmac_block(struct chip_bq27541 *chip, u16 reg, u8 *buf, u8 offset, u8 length)
{
	if (!zy0603_fg_condition_check()) {
		pr_info("%s zy0603_fg_condition_check fail\n", __func__);
		return -1;
	}

	mutex_lock(&chip->gauge_alt_manufacturer_access);
	gauge_i2c_txsubcmd(chip, ZYO603_CMD_ALTMAC, reg);
	if (gauge_i2c_err)
		goto fg_read_altmac_block_end;

	gauge_read_i2c_block(chip, ZYO603_CMD_ALTBLOCK + offset, length, buf);
	if (gauge_i2c_err)
		goto fg_read_altmac_block_end;

fg_read_altmac_block_end:
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	if (gauge_i2c_err)
		return -1;

	return 0;
}
static int zy0603_static_checksum_check(struct chip_bq27541 *chip);
static int zy0603_QmaxFgStatus_check(struct chip_bq27541 *chip);
bool zy0603_check_rc_sfr(void)
{
	s16 rc_sfr = 0;
	s32 qmax1 = 0, qmax0 = 0;
	u8 qmax_buf[ZYO603_QMAX_LEN] = { 0 };
	u8 sfr_buf[ZYO603_SFRCC_LEN] = { 0 };
	s32 ret;
	bool is_need_reset_sfr;
	struct chip_bq27541 *chip = gauge_ic;

	if (!chip || !chip->b_soft_reset_for_zy)
		return false;

	if (chip && chip->batt_zy0603 && zy0603_fg_condition_check()) {
		if (zy0603_static_checksum_check(chip) || zy0603_QmaxFgStatus_check(chip))
			return false;
	}

	ret = zy0603_read_altmac_block(chip, ZY0603_READ_QMAX_CMD, qmax_buf, ZY0603_QMAX_DATA_OFFSET, ZYO603_QMAX_LEN);
	if (ret < 0) {
		pr_err("%s: could not read CMD 0x0075, ret = %d\n", __func__, ret);
		is_need_reset_sfr = false;
		goto check_end;
	}
	qmax0 = (qmax_buf[1] << 8) | qmax_buf[0];
	qmax1 = (qmax_buf[3] << 8) | qmax_buf[2];
	qmax0 = max(qmax0, qmax1) * 2;
	if (qmax0 > ZY0603_MAX_QMAX_THRESHOLD)
		qmax0 = ZY0603_MAX_QMAX_THRESHOLD;

	ret = zy0603_read_altmac_block(chip, ZY0603_READ_SFRCC_CMD, sfr_buf, ZY0603_SFRCC_DATA_OFFSET,
				       ZYO603_SFRCC_LEN);
	if (ret < 0) {
		pr_err("%s: could not read CMD 0x0073, ret = %d\n", __func__, ret);
		is_need_reset_sfr = false;
		goto check_end;
	}

	rc_sfr = (sfr_buf[1] << 8) | sfr_buf[0];
	is_need_reset_sfr = !!(abs(rc_sfr) > qmax0);

check_end:
	pr_info("%s: RC_SFR=%d, qmax=%d, ret=%d is_need_reset_sfr = %s", __func__, rc_sfr, qmax0, ret,
		is_need_reset_sfr == true ? "true" : "false");
	return is_need_reset_sfr;
}
EXPORT_SYMBOL(zy0603_check_rc_sfr);

static bool zy0603_check_modelratio(struct chip_bq27541 *chip)
{
	u8 r_buf[2] = { 0 };
	u8 modelratio[2] = { 0xF7, 0x1D };
	zy0603_read_altmac_block(chip, ZY0603_MODELRATIO_REG, r_buf, 0, 2);
	if (r_buf[0] != modelratio[0] || r_buf[1] != modelratio[1])
		return true;
	else
		return false;
}

static int zy0603_check_comm_gfconfig(struct chip_bq27541 *chip, int cmd, u8 target_val)
{
	u8 r_val;
	int retry_cnt = 0;
	int error_flag;

	do {
		error_flag = 0;
		zy0603_read_altmac_block(chip, cmd, &r_val, 0, 1);
		if (r_val != target_val) {
			retry_cnt++;
			error_flag = 1;
		}
	} while (error_flag == 1 && retry_cnt <= 3);

	if (error_flag == 1 && retry_cnt > 3) {
		pr_err("%s cmd %d FAILED, retried counts: %d \n", __func__, cmd, retry_cnt);
		return GAUGE_ERROR;
	}
	return GAUGE_OK;
}

static bool zy0603_get_fw_version(struct chip_bq27541 *chip, int *fwversion)
{
	u8 version_data[4] = { 0 };
	u8 version_reg[2] = { 0x02, 0x00 };
	bool ret;

	if (!zy0603_fg_condition_check()) {
		*fwversion = -1;
		pr_info("%s zy0603_fg_condition_check fail\n", __func__);
		return false;
	}

	gauge_write_i2c_block(chip, ZYO603_CMD_ALTMAC, 2, version_reg);
	if (gauge_i2c_err)
		goto fg_read_altmac_block_end;
	usleep_range(ZYO603_CMD_SBS_DELAY, ZYO603_CMD_SBS_DELAY);

	gauge_read_i2c_block(chip, ZYO603_CMD_ALTMAC, 4, version_data);
fg_read_altmac_block_end:
	if (gauge_i2c_err) {
		ret = false;
		*fwversion = -1;
	} else {
		*fwversion = (version_data[3] << 8) | version_data[2];
		ret = true;
	}
	pr_info("%s [0x%02x][0x%02x][0x%02x][0x%02x] fg_version %d %s\n", __func__, version_data[0], version_data[1],
		version_data[2], version_data[3], *fwversion, ret == true ? "i2c ok" : "i2c error");
	return ret;
}

static int zy0603_full_access_mode(struct chip_bq27541 *chip)
{
	u8 full_access_cmd_1[2] = { 0xEF, 0xCD };
	u8 full_access_cmd_2[2] = { 0xAB, 0x90 };
	u8 full_access_op_st[2] = { 0x54, 0x00 };
	u8 read_buf[4] = { 0 };
	int retry = 2;
	int rc = 0;

	if (!zy0603_fg_condition_check()) {
		pr_info("%s zy0603_fg_condition_check fail\n", __func__);
		return -1;
	}

	mutex_lock(&chip->gauge_alt_manufacturer_access);
	do {
		gauge_write_i2c_block(chip, ZYO603_CTRL_REG, 2, full_access_cmd_1);
		pr_err("%s write {0xEF, 0xCD} --> 0x00\n", __func__);
		usleep_range(1000, 1000);

		gauge_write_i2c_block(chip, ZYO603_CTRL_REG, 2, full_access_cmd_2);
		pr_err("%s write {0xAB, 0x90} --> 0x00\n", __func__);
		usleep_range(1000, 1000);

		gauge_write_i2c_block(chip, ZYO603_CMD_ALTMAC, 2, full_access_op_st);
		pr_err("%s write {0x54, 0x00} --> 0x3E\n", __func__);
		usleep_range(1000, 1000);

		gauge_read_i2c_block(chip, ZYO603_CMD_ALTMAC, 4, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1],
		       read_buf[2], read_buf[3]);
		if ((read_buf[3] & 0x02) == 0) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	pr_err("%s full_access flag [%d]\n", __func__, rc);

	return rc;
}

static bool zy0603_fg_softreset(struct chip_bq27541 *chip)
{
	bool is_reset_done = false;
	int ret = -1;

	if (!zy0603_fg_condition_check()) {
		pr_info("%s zy0603_fg_condition_check fail\n", __func__);
		return false;
	}

	pr_info("%s need soft  reset\n", __func__);
	mutex_lock(&chip->gauge_alt_manufacturer_access);
	ret = zy0603_unseal(chip, 0);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);

	if (ret != 0) {
		pr_info("%s zy0603_unseal fail\n", __func__);
		return false;
	}

	ret = zy0603_full_access_mode(chip);
	if (ret == 0) {
		gauge_i2c_txsubcmd(chip, ZYO603_CMD_ALTMAC, ZY0603_SOFT_RESET_CMD);
		pr_info("%s w 0x3E \n", __func__);
		if (gauge_i2c_err)
			pr_info("%s soft reset fail", __func__);
		else
			is_reset_done = true;
	}
	mutex_lock(&chip->gauge_alt_manufacturer_access);
	zy0603_seal(chip, 0);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);

	return is_reset_done;
}

int zy0603_reset_rc_sfr(void)
{
	struct chip_bq27541 *chip = gauge_ic;

	if (!chip || !chip->b_soft_reset_for_zy)
		return -1;

	if (!zy0603_fg_condition_check()) {
		pr_info("%s zy0603_fg_condition_check fail\n", __func__);
		return -1;
	}

	if (gauge_ic->fg_soft_version == -1) {
		mutex_lock(&chip->gauge_alt_manufacturer_access);
		zy0603_get_fw_version(chip, &gauge_ic->fg_soft_version);
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
	}

	if (gauge_ic->fg_soft_version == -1) {
		pr_info("%s check_fw_version fail!", __func__);
		return -1;
	}

	pr_info("%s fwversion = 0x%04X \n", __func__, gauge_ic->fg_soft_version);
	if (gauge_ic->fg_soft_version < ZY0603_SOFT_RESET_VERSION_THRESHOLD)
		return -1;

	if (zy0603_fg_softreset(chip))
		return 0;
	else
		return 1;
}
EXPORT_SYMBOL(zy0603_reset_rc_sfr);

static void zy0602_cal_model_check(bool ffc_state)
{
	struct chip_bq27541 *chip = gauge_ic;
	int ret;

	if (!chip || chip->device_type != DEVICE_ZY0602)
		return;
	if (atomic_read(&chip->shutdown) == 1)
		return;

	pr_info("zy0602_cal_model_check,ffc:%d\n", ffc_state);

	if (ffc_state == false)
		fg_gauge_calibrate_board(chip);
	ret = fg_gauge_check_cell_model(chip, DTSI_MODEL_NAME);
	if (ret > 0)
		ret = fg_gauge_restore_cell_model(chip, DTSI_MODEL_NAME);
	fg_gauge_check_por_soc(chip);

	bq27541_track_upload_mode_info(chip);
}

static void sub_zy0602_cal_model_check(bool ffc_state)
{
	struct chip_bq27541 *chip = sub_gauge_ic;
	int ret;

	if (!chip || chip->device_type != DEVICE_ZY0602)
		return;

	pr_info("zy0602_cal_model_check,ffc:%d\n", ffc_state);

	if (ffc_state == false)
		fg_gauge_calibrate_board(chip);
	ret = fg_gauge_check_cell_model(chip, DTSI_MODEL_NAME);
	if (ret > 0)
		ret = fg_gauge_restore_cell_model(chip, DTSI_MODEL_NAME);
	fg_gauge_check_por_soc(chip);

	bq27541_track_upload_mode_info(chip);
}

/* OPLUS 2021-06-20 Add begin for zy0603 bad battery. */
static int zy0603_start_checksum_cal(struct chip_bq27541 *chip)
{
	u8 update_checksum[2] = { 0x05, 0x00 };
	if (NULL == chip)
		return -1;
	if (chip->afi_count == 0)
		return -1;

	return gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, update_checksum);
}

static int zy0603_disable_ssdf_and_pf(struct chip_bq27541 *chip)
{
	u8 write_protectioncfgb[5] = { 0x0F, 0x41, 0x00, 0xAF, 0x05 }; /*zy 20211227*/
	u8 write_ssdftime[5] = { 0x29, 0x41, 0x00, 0x95, 0x05 };
	u8 write_static_df_cheksum[6] = { 0x7A, 0x42 };
	u8 w_modelratio[6] = { 0x14, 0x47, 0xF7, 0x1D, 0x90, 0x06 };
	u8 w_cfconfig_chgp[5] = { 0x52, 0x47, 0xF7, 0x6F, 0x05 };
	u8 w_cfconfig_r2d[5] = { 0x5A, 0x47, 0xBB, 0xA3, 0x05 };
	u8 w_FGMAXDELTA[5] = { 0x9B, 0x47, 0x9B, 0x82, 0x05 };
	u8 pf_en_cmd[2] = { 0x24, 0x00 };
	u8 mfg_status_cm[2] = { 0x57, 0x00 };
	u8 read_buf[4] = { 0 };
	int rc = 0;
	int retry_cnt = 0;

	if (NULL == chip || (chip && chip->afi_count == 0))
		return -1;

	write_static_df_cheksum[2] = chip->static_df_checksum_3e[0];
	write_static_df_cheksum[3] = chip->static_df_checksum_3e[1];
	write_static_df_cheksum[4] = chip->static_df_checksum_60[0];
	write_static_df_cheksum[5] = chip->static_df_checksum_60[1];
	if (!zy0603_unseal(chip, 0)) { /* ok */
		/* disable protectioncfgb[ssdf] = 0 */
		gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, write_protectioncfgb);
		pr_err("%s write {0x0F, 0x41, 0x00} --> 0x3e\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, write_protectioncfgb + 3);
		pr_err("%s write {0xAF, 0x05} --> 0x60\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0], read_buf[1], read_buf[2]);
		if (!((read_buf[0] == write_protectioncfgb[0]) && (read_buf[1] == write_protectioncfgb[1]) &&
		      (read_buf[2] == write_protectioncfgb[2]))) {
			rc = -1;
			return rc;
		}

		/* disable ssdf */
		gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, write_ssdftime);
		pr_err("%s write {0x29,0x41,0x00} --> 0x3e\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, write_ssdftime + 3);
		pr_err("%s write {0x95,0x05} --> 0x60\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0], read_buf[1], read_buf[2]);
		if (!((read_buf[0] == write_ssdftime[0]) && (read_buf[1] == write_ssdftime[1]) &&
		      (read_buf[2] == write_ssdftime[2]))) {
			rc = -1;
			return rc;
		}

		if (chip->b_soft_reset_for_zy) {
			/* modelratio */
			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, w_modelratio);
			pr_err("%s modelratio write {0x14, 0x47, 0xF7, 0x1D} --> 0x3e\n", __func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, w_modelratio + 4);
			pr_err("%s modelratio write {0x96, 0x06} --> 0x60\n", __func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);
			pr_err("%s modelratio 0x3E -->read [0x%02x][0x%02x] [0x%02x] [0x%02x]\n", __func__, read_buf[0],
			       read_buf[1], read_buf[2], read_buf[3]);
			if (!((read_buf[0] == w_modelratio[0]) && (read_buf[1] == w_modelratio[1]) &&
			      (read_buf[2] == w_modelratio[2]) && (read_buf[3] == w_modelratio[3]))) {
				rc = -1;
				return rc;
			}

			/* w_cfconfig_chgp */
			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, w_cfconfig_chgp);
			pr_err("%s w_cfconfig_chgp write {0x52, 0x47, 0xF7} --> 0x3e\n", __func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, w_cfconfig_chgp + 3);
			pr_err("%s  w_cfconfig_chgp write {0xAF, 0x05} --> 0x60\n", __func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
			pr_err("%s w_cfconfig_chgp 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0],
			       read_buf[1], read_buf[2]);
			if (!((read_buf[0] == w_cfconfig_chgp[0]) && (read_buf[1] == w_cfconfig_chgp[1]) &&
			      (read_buf[2] == w_cfconfig_chgp[2]))) {
				rc = -1;
				return rc;
			}

			/* w_cfconfig_r2d */
			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, w_cfconfig_r2d);
			pr_err("%s w_cfconfig_r2d write {0x5A, 0x47, 0xBB} --> 0x3e\n", __func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, w_cfconfig_r2d + 3);
			pr_err("%s w_cfconfig_r2d write {0xA3, 0x05} --> 0x60\n", __func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
			pr_err("%s w_cfconfig_r2d 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0],
			       read_buf[1], read_buf[2]);
			if (!((read_buf[0] == w_cfconfig_r2d[0]) && (read_buf[1] == w_cfconfig_r2d[1]) &&
			      (read_buf[2] == w_cfconfig_r2d[2]))) {
				rc = -1;
				return rc;
			}

			/* w_FGMAXDELTA */
			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, w_FGMAXDELTA);
			pr_err("%s w_FGMAXDELTA write {0x9B, 0x47, 0x9B} --> 0x3e\n", __func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, w_FGMAXDELTA + 3);
			pr_err("%s w_FGMAXDELTA write {0x82, 0x05} --> 0x60\n", __func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
			pr_err("%s w_FGMAXDELTA 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0],
			       read_buf[1], read_buf[2]);
			if (!((read_buf[0] == w_FGMAXDELTA[0]) && (read_buf[1] == w_FGMAXDELTA[1]) &&
			      (read_buf[2] == w_FGMAXDELTA[2]))) {
				rc = -1;
				return rc;
			}
		}

		/* update StaticDFChecksum */
		gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, write_static_df_cheksum);
		pr_err("%s write static_df_checksum_3e --> 0x3e\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, write_static_df_cheksum + 4);
		pr_err("%s write {0x4B,0x06} --> 0x60\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1],
		       read_buf[2], read_buf[3]);
		if (!((read_buf[0] == write_static_df_cheksum[0]) && (read_buf[1] == write_static_df_cheksum[1]) &&
		      (read_buf[2] == write_static_df_cheksum[2]) && (read_buf[3] == write_static_df_cheksum[3]))) {
			rc = -1;
			return rc;
		}
		/* disable PF_EN */
		do {
			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, pf_en_cmd);
			pr_err("%s write {0x24,0x00} --> 0x3e\n", __func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, mfg_status_cm);
			pr_err("%s write {0x57,0x00} --> 0x3e\n", __func__);

			gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);
			pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0],
			       read_buf[1], read_buf[2], read_buf[3]);
			if (read_buf[2] & (0x1 << 6)) {
				retry_cnt++;
				pr_err("retry_dis_pfen retry_cnt[%d]\n", retry_cnt);
			}
		} while ((read_buf[2] & (0x1 << 6)) && retry_cnt <= 3);
		if (retry_cnt > 3) {
			pr_err("retry_dis_pfen failed[%d]\n", retry_cnt);
		}

		if (!((read_buf[0] == mfg_status_cm[0]) && (read_buf[1] == mfg_status_cm[1]) &&
		      ((read_buf[3] & 0x40) == 0))) {
			rc = -1;
			pr_err("read_buf check retry_dis_pfen failed[%d]\n", retry_cnt);
			return rc;
		}
	}

	return rc;
}

static int zy0603_disable_ssdf_and_pf_check(struct chip_bq27541 *chip)
{
	u8 write_ssdftime[2] = { 0x29, 0x41 };
	u8 ProtectionCfgB[3] = { 0x0f, 0x41, 0x00 };
	u8 mfg_status_cm[2] = { 0x57, 0x00 };
	u8 read_buf[4] = { 0 };
	int comm_cfconfig_cmd[3] = { ZY0603_GFCONFIG_CHGP_REG, ZY0603_GFCONFIG_R2D_REG, ZY0603_GFMAXDELTA_REG };
	u8 comm_target_data[3] = { 0xF7, 0XBB, 0x9B };
	int rc = 0;
	int error_flag = 0;
	int retry_cnt = 0;
	int i_index;

	/* ssdf_en check in ProtectionCfgB */
	do {
		gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, ProtectionCfgB);
		pr_err("%s write {0x29,0x41} --> 0x3e\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0], read_buf[1], read_buf[2]);
		error_flag = 0;
		if (((read_buf[0] == ProtectionCfgB[0]) && (read_buf[1] == ProtectionCfgB[1]) &&
		     (read_buf[2] == ProtectionCfgB[2]))) {
			pr_err("%s ssdf_en in ProtectionCfgB have disabled\n", __func__);
			rc = GAUGE_OK;
		} else {
			pr_err("%s ssdf_en in ProtectionCfgB recheck %d\n", __func__, retry_cnt);
			retry_cnt++;
			error_flag = 1;
		}
	} while (error_flag == 1 && retry_cnt <= 3);
	if (error_flag == 1 && retry_cnt > 3) {
		pr_err("%s ssdf_en in ProtectionCfgB FAILED, retried counts: %d \n", __func__, retry_cnt);
		return GAUGE_ERROR;
	}

	if (chip->b_soft_reset_for_zy) {
		error_flag = 0;
		retry_cnt = 0;
		do {
			error_flag = 0;
			if (zy0603_check_modelratio(chip)) {
				retry_cnt++;
				error_flag = 1;
			} else {
				rc = GAUGE_OK;
			}
		} while (error_flag == 1 && retry_cnt <= 3);
		if (error_flag == 1 && retry_cnt > 3) {
			pr_err("%s modelratio FAILED, retried counts: %d \n", __func__, retry_cnt);
			return GAUGE_ERROR;
		}

		for (i_index = 0; i_index < 3; i_index++) {
			rc = zy0603_check_comm_gfconfig(chip, comm_cfconfig_cmd[i_index], comm_target_data[i_index]);
			if (rc == GAUGE_ERROR)
				return GAUGE_ERROR;
		}
	}

	/* ssdf check */
	error_flag = 0;
	retry_cnt = 0;
	do {
		gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, write_ssdftime);
		pr_err("%s write {0x29,0x41} --> 0x3e\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0], read_buf[1], read_buf[2]);
		error_flag = 0;
		if (((read_buf[0] == write_ssdftime[0]) && (read_buf[1] == write_ssdftime[1]) &&
		     (read_buf[2] == 0x00))) {
			pr_err("ssdf time have set 0\n");
			rc = GAUGE_OK;
		} else {
			pr_err("%s ssdf time recheck %d\n", __func__, retry_cnt);
			retry_cnt++;
			error_flag = 1;
		}
	} while (error_flag == 1 && retry_cnt <= 3);
	if (error_flag == 1 && retry_cnt > 3) {
		pr_err("%s ssdf_time checked FAILED, retried counts: %d \n", __func__, retry_cnt);
		return GAUGE_ERROR;
	}

	/* PF_EN check */
	error_flag = 0;
	retry_cnt = 0;
	do {
		gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, mfg_status_cm);
		pr_err("%s write {0x57,0x00} --> 0x3e\n", __func__);

		gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1],
		       read_buf[2], read_buf[3]);

		error_flag = 0;
		if ((read_buf[0] == mfg_status_cm[0] && read_buf[1] == mfg_status_cm[1] &&
		     !(read_buf[2] & (0x1 << 6)))) {
			pr_err("pf_en have disabled\n");
			rc = GAUGE_OK;
		} else {
			pr_err("%s pf en recheck %d\n", __func__, retry_cnt);
			retry_cnt++;
			error_flag = 1;
		}
	} while (error_flag == 1 && retry_cnt <= 3);
	if (retry_cnt > 3) {
		pr_err("%s retry_dis_pfen failed[%d]\n", __func__, retry_cnt);
		return GAUGE_ERROR;
	}

	return rc;
}

static int zy0603_check_ssdf_pf_checksum(struct chip_bq27541 *chip)
{
	u8 checksum_reg[2] = { 0x7A, 0x42 };
	u8 read_buf[4] = { 0 };
	int rc = 0;
	int retry = 2;

	if (!chip) {
		return GAUGE_ERROR;
	}
	do {
		gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, checksum_reg);
		pr_err("%s write {0x7A, 0x42} --> 0x3E\n", __func__);
		usleep_range(5000, 5000);

		gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x][0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1],
		       read_buf[2], read_buf[3]);
		if ((read_buf[2] == chip->static_df_checksum_3e[0]) &&
		    (read_buf[3] == chip->static_df_checksum_3e[1])) {
			retry = 0;
			rc = GAUGE_OK;
		} else {
			retry--;
			rc = GAUGE_ERROR;
		}
	} while (retry > 0);

	return rc;
}

static int zy0603_static_checksum_check(struct chip_bq27541 *chip)
{
	int rc = 0;
	u8 update_checksum[2] = { 0x05, 0x00 };
	u8 read_buf[4] = { 0 };

	/* w */
	gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, update_checksum);

	/* r */
	gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);

	/* check */
	if (read_buf[3] & (1 << 7)) {
		rc = 1; /* exception */
		pr_err("%s checksum exception [0x%x] [0x%x] [0x%x] [0x%x]\n", __func__, read_buf[0], read_buf[1],
		       read_buf[2], read_buf[3]);
	} else {
		rc = 0; /* normal */
	}

	return rc;
}

static int zy0603_QmaxFgStatus_check(struct chip_bq27541 *chip)
{
	int rc = 0;
	u8 QmaxFGstatus[2] = { 0x75, 0x00 };
	u8 read_buf[7] = { 0 };
	int qmax_cell1, qmax_cell2, fg_status;

	/* w */
	gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, QmaxFGstatus);

	/* r */
	gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 7, read_buf);

	/* check */
	fg_status = read_buf[6];
	qmax_cell1 = (read_buf[3] << 8) | read_buf[2];
	qmax_cell2 = (read_buf[5] << 8) | read_buf[4];
	if (!(qmax_cell1 >= chip->zy_dts_qmax_min && qmax_cell1 <= chip->zy_dts_qmax_max) ||
	    !(qmax_cell2 >= chip->zy_dts_qmax_min && qmax_cell2 <= chip->zy_dts_qmax_max) || !(fg_status == 0x33)) {
		rc = 1; /* exception */
		pr_err("Qmax or fg_status exception\n");
		pr_err("%s [0x%0x] [0x%0x] [0x%0x] [0x%0x] [0x%0x] [0x%0x] [0x%0x]\n", __func__, read_buf[0],
		       read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5], read_buf[6]);
	} else {
		rc = 0; /* normal */
	}

	return rc;
}

static int zy0603_update_afi_buf(struct chip_bq27541 *chip, u16 base_addr, u16 len, const u8 *afi_buf1, char *msg)
{
	u8 checksum = 0xFF;
	u8 len1 = len / 16;
	u8 len2 = len % 16;
	int i = 0;
	int j = 0;
	int k = 0;
	int retry = 0;
	int error_flag = 0;

	u8 write_buf[16 + 4] = { 0 };
	u8 read_buf[16 + 4] = { 0 };
	u16 addr;

	printk("update %s ...\n", msg);
	for (i = 0; i < len1; i++) {
		checksum = 0xFF;
		addr = base_addr + i * 16;
		write_buf[0] = (u8)addr;
		checksum -= write_buf[0];
		write_buf[1] = (u8)(addr >> 8);
		checksum -= write_buf[1];
		for (j = 0; j < 16; j++) {
			write_buf[2 + j] = afi_buf1[j + i * 16]; /* 0x4000 + 30 = 0x401E */
			checksum -= write_buf[2 + j];
		}
		write_buf[18] = checksum;
		write_buf[19] = 16 + 4; /* total len */

		retry = 0;
		do {
			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 18, write_buf); /* addr + valid data */
			usleep_range(U_DELAY_1_MS, U_DELAY_1_MS);
			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2,
						write_buf + 18); /* checksum + self (18 + 19) */
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);
			gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 18, read_buf); /* addr + valid data */
			for (k = 0; k < 18; k++) {
				error_flag = 0;
				if (write_buf[k] != read_buf[k]) {
					pr_err("%s 1 w r[%d][%d] = 0x%0x 0x%0x", msg, i, k, write_buf[k],
					       read_buf[k]); /* expect 0x78 */
					error_flag = 1;
					retry++;
					pr_err("%s 1 error retry[%d]\n", msg, retry);
					break;
				}
			}
		} while (error_flag == 1 && retry < 5);
		pr_err("\n");
	}

	if (len2 > 0) { /* tail: len2 < 16 */
		checksum = 0xFF;
		addr = base_addr + len1 * 16;
		write_buf[0] = (u8)addr;
		checksum -= write_buf[0];
		write_buf[1] = (u8)(addr >> 8);
		checksum -= write_buf[1];
		for (j = 0; j < len2; j++) {
			write_buf[2 + j] = afi_buf1[j + len1 * 16];
			checksum -= write_buf[2 + j];
		}
		write_buf[len2 + 2] = checksum;
		write_buf[len2 + 3] = len2 + 4;

		retry = 0;
		do {
			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, len2 + 2, write_buf);
			usleep_range(U_DELAY_1_MS, U_DELAY_1_MS);
			gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, write_buf + len2 + 2);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);
			gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, len2 + 2, read_buf);
			for (k = 0; k < len2 + 2; k++) {
				error_flag = 0;
				if (write_buf[k] != read_buf[k]) {
					pr_err("%s 2 [%d] = 0x%0x 0x%0x", msg, k, write_buf[k], read_buf[k]);
					error_flag = 1;
					retry++;
					pr_err("%s 2 error retry[%d]\n", msg, retry);
					break;
				}
			}
		} while (error_flag == 1 && retry < 5);
	}
	pr_err("%s update done\n", msg);

	return 0;
}

static int zy0603_afi_param_update(struct chip_bq27541 *chip)
{
	u16 base_addr;

	/* write afi_buf1 */
	base_addr = 0x4000;
	zy0603_update_afi_buf(chip, base_addr, chip->afi_buf_len[0], chip->afi_buf[0], "afi_buf1");

	/* write afi_buf2 */
	base_addr = 0x40FF;
	zy0603_update_afi_buf(chip, base_addr, chip->afi_buf_len[1], chip->afi_buf[1], "afi_buf2");

	/* write afi_buf3 */
	base_addr = 0x4200;
	zy0603_update_afi_buf(chip, base_addr, chip->afi_buf_len[2], chip->afi_buf[2], "afi_buf3");

	/* write afi_buf4 */
	base_addr = 0x4245;
	zy0603_update_afi_buf(chip, base_addr, chip->afi_buf_len[3], chip->afi_buf[3], "afi_buf4");

	/* write afi_buf5 */
	base_addr = 0x427A;
	zy0603_update_afi_buf(chip, base_addr, chip->afi_buf_len[4], chip->afi_buf[4], "afi_buf5");

	/* write afi_buf6 */
	base_addr = 0x4400;
	zy0603_update_afi_buf(chip, base_addr, chip->afi_buf_len[5], chip->afi_buf[5], "afi_buf6");

	/* write afi_buf7 */
	base_addr = 0x4600;
	zy0603_update_afi_buf(chip, base_addr, chip->afi_buf_len[6], chip->afi_buf[6], "afi_buf7");
	return 0;
}

#define OPLUS_AFI_UPDATE_INTERVAL_SEC 5
#define OPLUS_AFI_UPDATE_INTERVAL round_jiffies_relative(msecs_to_jiffies(OPLUS_AFI_UPDATE_INTERVAL_SEC * 1000))
extern bool oplus_check_afi_update_condition(void);
static void zy0603_afi_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_bq27541 *chip = container_of(dwork, struct chip_bq27541, afi_update);
	int ret = 0;
	int error_flag = 0;

	chip->need_check = false;
	if (chip->error_occured) {
		goto error_occured;
	}
error_occured:
	msleep(M_DELAY_10_S);
	if (oplus_check_afi_update_condition()) {
		printk(KERN_ERR "[%s] zy0603_afi_param_update [%d, %d]\n", __func__, oplus_vooc_get_fastchg_started(),
		       oplus_vooc_get_allow_reading());
		if (oplus_vooc_get_allow_reading()) {
			if (!zy0603_unseal(chip, 0)) {
				chip->afi_update_done = false;
				ret = zy0603_afi_param_update(chip);
				if (ret < 0) {
					printk(KERN_ERR "[%s] zy0603_afi_param_update fail\n", __func__);
				}

				do {
					error_flag = 0;
					if (!zy0603_seal(chip, 0)) {
						chip->afi_update_done = true;
						chip->error_occured = true;
						printk(KERN_ERR "[%s] zy0603_seal ok\n", __func__);
					} else {
						error_flag = 1;
					}
				} while (error_flag);
			}
			printk(KERN_ERR "[%s] zy0603_afi_param_update done\n", __func__);
		}
	}
	chip->need_check = true;
	/*	if (chip->error_occured) {
		schedule_delayed_work(&gauge_ic->afi_update, 0);
		printk(KERN_ERR "[%s] error_occured retrigger afi update\n", __func__);
	}
*/
}

static bool zy0603_afi_update_done(void)
{
	if (!gauge_ic) {
		return false;
	}

	if (gauge_ic->batt_zy0603 && gauge_ic->afi_count > 0 && !gauge_ic->afi_update_done) {
		printk(KERN_ERR "return for afi_update_done not finished\n");
		return false;
	} else {
		return true;
	}
}

#define CHECKSUM_ERROR_CNT 3
static int zy0603_protect_check(void)
{
	static int checksum_error = 0;
	static int qmax_fgstatus_error = 0;
	/* static bool error_occured = false; */
	int checksum_error_flag = 0, qmaxfg_error_flag = 0;

	/*printk(KERN_ERR "[%s] v12 unseal start\n", __func__);*/
	if (!gauge_ic || (gauge_ic && !gauge_ic->batt_zy0603)
		|| (gauge_ic && gauge_ic->afi_count == 0)
		|| atomic_read(&gauge_ic->gauge_i2c_status)) {
		/*printk(KERN_ERR "[%s] return for %s\n", __func__, !gauge_ic ? "guage is null" : "is not zy0603 gauge");*/
		return -1;
	}

	/*printk(KERN_ERR "[%s] ssdf and pf disable = %d\n", __func__, gauge_ic->disabled);*/
	/* if (oplus_vooc_get_allow_reading() && gauge_ic->disabled !error_occured) { */
	if (oplus_vooc_get_allow_reading() && gauge_ic->disabled && gauge_ic->need_check) {
		if (zy0603_static_checksum_check(gauge_ic)) {
			checksum_error_flag = 1;
			checksum_error++;
			printk(KERN_ERR "[%s] staticchecksum_error = %d\n", __func__, checksum_error);
			if (CHECKSUM_ERROR_CNT <= checksum_error) {
				checksum_error = 0;
				if (gauge_ic && gauge_ic->afi_update_done) {
					schedule_delayed_work(&gauge_ic->afi_update, 0);
				}
			}
		} else {
			checksum_error_flag = 0;
			checksum_error = 0;
		}
		if (zy0603_QmaxFgStatus_check(gauge_ic)) {
			qmaxfg_error_flag = 1;
			qmax_fgstatus_error++;

			printk(KERN_ERR "[%s] qmax_fgstatus_error = %d\n", __func__, qmax_fgstatus_error);
			if (CHECKSUM_ERROR_CNT <= qmax_fgstatus_error) {
				qmax_fgstatus_error = 0;
				if (gauge_ic && gauge_ic->afi_update_done) {
					schedule_delayed_work(&gauge_ic->afi_update, 0);
				}
			}
		} else {
			qmaxfg_error_flag = 0;
			qmax_fgstatus_error = 0;
		}
		/* error_occured = true; */
	}
	/*printk(KERN_ERR "[%s] checksum_error[%d %d] qmaxfg_error[%d, %d]\n",
		__func__, checksum_error_flag, checksum_error, qmaxfg_error_flag, qmax_fgstatus_error);*/

	return 0;
}

static int zy0603_parse_afi_buf(struct chip_bq27541 *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct device_node *afi_data_node = NULL;
	int rc = 0;
	const u8 *data;
	char dt_name[30];
	const char *battery_name;
	unsigned int len = 0;
	int i = 0;

	chip->afi_count = 0;
	afi_data_node = of_find_node_by_name(node, "zy,afi_data");
	if (afi_data_node == NULL) {
		dev_err(chip->dev, "%s, zy,afi_data failed\n", __func__);
		return -ENOMEM;
	}
	rc = of_property_read_string(afi_data_node, "battery,name", &battery_name);
	if (rc) {
		dev_err(chip->dev, "%s, battery-name failed\n", __func__);
		return -ENOMEM;
	}
	dev_err(chip->dev, "%s, battery-name = %s\n", __func__, battery_name);
	rc = of_property_read_u32(afi_data_node, "qmax_min", &chip->zy_dts_qmax_min);
	if (rc) {
		dev_err(chip->dev, "%s, qmax_min failed\n", __func__);
		return -ENOMEM;
	}
	dev_err(chip->dev, "%s, qmax_min = %d\n", __func__, chip->zy_dts_qmax_min);
	rc = of_property_read_u32(afi_data_node, "qmax_max", &chip->zy_dts_qmax_max);
	if (rc) {
		dev_err(chip->dev, "%s, qmax_max failed\n", __func__);
		return -ENOMEM;
	}
	dev_err(chip->dev, "%s, qmax_max = %d\n", __func__, chip->zy_dts_qmax_max);
	data = of_get_property(afi_data_node, "static_df_checksum_3e", &len);
	if (!data || len != 2) {
		dev_err(chip->dev, "%s, parse static_df_checksum_3e  failed\n", __func__);
		return -ENOMEM;
	}
	chip->static_df_checksum_3e = data;
	dev_err(chip->dev, "%s, static_df_checksum_3e = [%x,%x]\n", __func__, chip->static_df_checksum_3e[0],
		chip->static_df_checksum_3e[1]);
	data = of_get_property(afi_data_node, "static_df_checksum_60", &len);
	if (!data || len != 2) {
		dev_err(chip->dev, "%s, parse static_df_checksum_60  failed\n", __func__);
		return -ENOMEM;
	}
	chip->static_df_checksum_60 = data;
	dev_err(chip->dev, "%s, static_df_checksum_60 = [%x,%x]\n", __func__, chip->static_df_checksum_60[0],
		chip->static_df_checksum_60[1]);
	rc = of_property_read_u32(afi_data_node, "afi_buf_num", &chip->afi_count);
	if (rc || chip->afi_count == 0) {
		chip->afi_count = 0;
		return -ENOMEM;
	}
	dev_err(chip->dev, "zy0603_parse_afi_buf afi_count %d\n", chip->afi_count);
	chip->afi_buf = devm_kzalloc(&chip->client->dev, chip->afi_count * sizeof(u8 *), GFP_KERNEL);
	chip->afi_buf_len = devm_kzalloc(&chip->client->dev, chip->afi_count * sizeof(unsigned int), GFP_KERNEL);
	for (i = 0; i < chip->afi_count; i++) {
		sprintf(dt_name, "afi_buf_%d", i);
		dev_err(chip->dev, "zy0603_parse_afi_buf chwit dt_name= %s\n", dt_name);
		data = of_get_property(afi_data_node, dt_name, &len);
		if (!data) {
			dev_err(chip->dev, "%s, parse afi_buf %s failed\n", __func__, dt_name);
			chip->afi_count = 0;
			return -ENOMEM;
		}
		chip->afi_buf[i] = data;
		chip->afi_buf_len[i] = len;
		dev_err(chip->dev, "zy0603_parse_afi_buf chwit i = %d, len= %d  end [%x]\n", i, len,
			chip->afi_buf[i][chip->afi_buf_len[i] - 1]);
	}
	return rc;
}
/* OPLUS 2013-08-24 wangjc Add begin for add adc interface. */
static int bq27541_get_battery_cc(void) /*  sjc20150105  */
{
	int ret = 0;
	int cc = 0;
	int retry = RETRY_CNT;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->cmd_addr.reg_cc == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->cc_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_cc, &cc);
			if (ret) {
				dev_err(gauge_ic->dev, "error reading cc.\n");
				return 0;
			}
			if (normal_range_judge(CC_MAX, CC_MIN, cc))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("cc out of range,retry=%d,cc=%d", retry, cc);
		} while (retry--);
	} else {
		if (gauge_ic->cc_pre) {
			return gauge_ic->cc_pre;
		} else {
			return 0;
		}
	}
	gauge_ic->cc_pre = cc;
	return cc;
}

static int bq27541_get_sub_battery_cc(void)
{
	int ret = 0;
	int cc = 0;
	int retry = RETRY_CNT;

	if (!sub_gauge_ic) {
		return 0;
	}
	if (sub_gauge_ic->cmd_addr.reg_cc == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	if (atomic_read(&sub_gauge_ic->suspended) == 1 || atomic_read(&sub_gauge_ic->gauge_i2c_status)) {
		return sub_gauge_ic->cc_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(sub_gauge_ic, sub_gauge_ic->cmd_addr.reg_cc, &cc);
			if (ret) {
				dev_err(sub_gauge_ic->dev, "error reading cc.\n");
				return 0;
			}
			if (normal_range_judge(CC_MAX, CC_MIN, cc))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("cc out of range,retry=%d,cc=%d", retry, cc);
		} while (retry--);
	} else {
		if (sub_gauge_ic->cc_pre) {
			return sub_gauge_ic->cc_pre;
		} else {
			return 0;
		}
	}
	sub_gauge_ic->cc_pre = cc;
	return cc;
}

static int bq27541_get_battery_fcc(void) /*  sjc20150105  */
{
	int ret = 0;
	int fcc = 0;
	int retry = RETRY_CNT;

	if (!gauge_ic) {
		return 0;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->fcc_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_fcc, &fcc);
			if (ret) {
				dev_err(gauge_ic->dev, "error reading fcc.\n");
				return ret;
			}
			if (normal_range_judge(FCC_MAX, FCC_MIN, fcc))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("fcc out of range,retry=%d,fcc=%d", retry, fcc);
		} while (retry--);
	} else {
		if (gauge_ic->fcc_pre) {
			return gauge_ic->fcc_pre;
		} else {
			return 0;
		}
	}
	gauge_ic->fcc_pre = fcc;
	return fcc;
}

static int bq27541_get_sub_gauge_battery_fcc(void)
{
	int ret = 0;
	int fcc = 0;
	int retry = RETRY_CNT;

	if (!sub_gauge_ic) {
		return 0;
	}
	if (atomic_read(&sub_gauge_ic->suspended) == 1 || atomic_read(&sub_gauge_ic->gauge_i2c_status)) {
		return sub_gauge_ic->fcc_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(sub_gauge_ic, sub_gauge_ic->cmd_addr.reg_fcc, &fcc);
			if (ret) {
				dev_err(sub_gauge_ic->dev, "error reading fcc.\n");
				return ret;
			}
			if (normal_range_judge(FCC_MAX, FCC_MIN, fcc))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("fcc out of range,retry=%d,fcc=%d", retry, fcc);
		} while (retry--);
	} else {
		if (sub_gauge_ic->fcc_pre) {
			return sub_gauge_ic->fcc_pre;
		} else {
			return 0;
		}
	}
	sub_gauge_ic->fcc_pre = fcc;
	return fcc;
}

static int bq27541_get_prev_batt_fcc(void) /*  sjc20150105  */
{
	if (!gauge_ic) {
		return 0;
	}
	return gauge_ic->fcc_pre;
}
static int bq27541_get_sub_gauge_prev_batt_fcc(void)
{
	if (!sub_gauge_ic) {
		return 0;
	}
	return sub_gauge_ic->fcc_pre;
}

static int bq27541_soc_calibrate(struct chip_bq27541 *chip, int soc)
{
	unsigned int soc_calib;

	if (!chip) {
		return 0;
	}

	soc = soc & 0xff;
	soc_calib = soc;
	if (soc >= 100) {
		soc_calib = 100;
	} else if (soc < 0) {
		soc_calib = 0;
	}
	chip->soc_pre = soc_calib;

	return soc_calib;
}

static void bq27541_cntl_cmd(struct chip_bq27541 *chip, int subcmd)
{
	gauge_i2c_txsubcmd(chip, BQ27541_BQ27411_REG_CNTL, subcmd);
}

static int bq28z610_get_2cell_voltage(void);

static int bq27541_get_battery_mvolts(void)
{
	int ret = 0;
	int volt = 0;
	int retry = RETRY_CNT;

	if (!gauge_ic) {
		return 0;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->batt_vol_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_volt, &volt);
			if (ret) {
				dev_err(gauge_ic->dev, "error reading voltage, ret:%d\n", ret);
				gauge_ic->batt_cell_max_vol = gauge_ic->max_vol_pre;
				gauge_ic->batt_cell_min_vol = gauge_ic->min_vol_pre;
				return gauge_ic->batt_vol_pre;
			}
			if (normal_range_judge(VOLT_MAX, VOLT_MIN, volt))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("voltage out of range,retry=%d,voltage=%d", retry, volt);
		} while (retry--);

		if (gauge_ic->batt_bq28z610) {
			bq28z610_get_2cell_voltage();
			gauge_ic->max_vol_pre = gauge_ic->batt_cell_max_vol;
			gauge_ic->min_vol_pre = gauge_ic->batt_cell_min_vol;
			gauge_ic->batt_vol_pre = gauge_ic->batt_cell_max_vol;
			return gauge_ic->batt_cell_max_vol;
		} else {
			gauge_ic->batt_cell_max_vol = volt;
			gauge_ic->batt_cell_min_vol = volt;
			gauge_ic->batt_vol_pre = volt;
			gauge_ic->max_vol_pre = gauge_ic->batt_cell_max_vol;
			gauge_ic->min_vol_pre = gauge_ic->batt_cell_min_vol;
			return volt;
		}
	} else {
		return gauge_ic->batt_vol_pre;
	}
}
static int bq27541_get_sub_battery_mvolts(void)
{
	int ret = 0;
	int volt = 0;
	int retry = RETRY_CNT;

	if (!sub_gauge_ic) {
		return 0;
	}
	if (atomic_read(&sub_gauge_ic->suspended) == 1 || atomic_read(&sub_gauge_ic->gauge_i2c_status)) {
		return sub_gauge_ic->batt_vol_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(sub_gauge_ic, sub_gauge_ic->cmd_addr.reg_volt, &volt);
			if (ret) {
				dev_err(sub_gauge_ic->dev, "error reading voltage, ret:%d\n", ret);
				sub_gauge_ic->batt_cell_max_vol = sub_gauge_ic->max_vol_pre;
				sub_gauge_ic->batt_cell_min_vol = sub_gauge_ic->min_vol_pre;
				return sub_gauge_ic->batt_vol_pre;
			}
			if (normal_range_judge(VOLT_MAX, VOLT_MIN, volt))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("voltage out of range,retry=%d,voltage=%d", retry, volt);
		} while (retry--);

		if (sub_gauge_ic->batt_bq28z610) {
			bq28z610_get_2cell_voltage();
			sub_gauge_ic->max_vol_pre = sub_gauge_ic->batt_cell_max_vol;
			sub_gauge_ic->min_vol_pre = sub_gauge_ic->batt_cell_min_vol;
			sub_gauge_ic->batt_vol_pre = sub_gauge_ic->batt_cell_max_vol;
			return sub_gauge_ic->batt_cell_max_vol;
		} else {
			sub_gauge_ic->batt_cell_max_vol = volt;
			sub_gauge_ic->batt_cell_min_vol = volt;
			sub_gauge_ic->batt_vol_pre = volt;
			sub_gauge_ic->max_vol_pre = sub_gauge_ic->batt_cell_max_vol;
			sub_gauge_ic->min_vol_pre = sub_gauge_ic->batt_cell_min_vol;
			return volt;
		}
	} else {
		return sub_gauge_ic->batt_vol_pre;
	}
}
static int bq27541_get_battery_fc(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->fc_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_fc, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading fc, ret:%d\n", ret);
			return gauge_ic->fc_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->fc_pre = val;
			return val;
		} else {
			return gauge_ic->fc_pre;
		}
	} else {
		return gauge_ic->fc_pre;
	}
}

static int bq27541_get_battery_qm(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->qm_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_qm, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading qm, ret:%d\n", ret);
			return gauge_ic->qm_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->qm_pre = val;
			return val;
		} else {
			return gauge_ic->qm_pre;
		}
	} else {
		return gauge_ic->qm_pre;
	}
}

static int bq27541_get_sub_battery_qm(void)
{
	int ret = 0;
	int val = 0;

	if (!sub_gauge_ic) {
		return 0;
	}
	if (sub_gauge_ic->device_type == DEVICE_BQ27541 || sub_gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&sub_gauge_ic->suspended) == 1 || atomic_read(&sub_gauge_ic->gauge_i2c_status)) {
		return sub_gauge_ic->qm_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(sub_gauge_ic, sub_gauge_ic->cmd_addr.reg_qm, &val);
		if (ret) {
			dev_err(sub_gauge_ic->dev, "error reading qm, ret:%d\n", ret);
			return sub_gauge_ic->qm_pre;
		}
		if (!sub_gauge_ic->batt_bq28z610) {
			sub_gauge_ic->qm_pre = val;
			return val;
		} else {
			return sub_gauge_ic->qm_pre;
		}
	} else {
		return sub_gauge_ic->qm_pre;
	}
}

static int bq27541_get_battery_pd(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->pd_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_pd, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading pd, ret:%d\n", ret);
			return gauge_ic->pd_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->pd_pre = val;
			return val;
		} else {
			return gauge_ic->pd_pre;
		}
	} else {
		return gauge_ic->pd_pre;
	}
}

static int bq27541_get_battery_rcu(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->rcu_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_rcu, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading rcu, ret:%d\n", ret);
			return gauge_ic->rcu_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->rcu_pre = val;
			return val;
		} else {
			return gauge_ic->rcu_pre;
		}
	} else {
		return gauge_ic->rcu_pre;
	}
}

static int bq27541_get_battery_rcf(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->rcf_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_rcf, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading rcf, ret:%d\n", ret);
			return gauge_ic->rcf_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->rcf_pre = val;
			return val;
		} else {
			return gauge_ic->rcf_pre;
		}
	} else {
		return gauge_ic->rcf_pre;
	}
}

static int bq27541_get_battery_fcu(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->fcu_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_fcu, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading fcu, ret:%d\n", ret);
			return gauge_ic->fcu_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->fcu_pre = val;
			return val;
		} else {
			return gauge_ic->fcu_pre;
		}
	} else {
		return gauge_ic->fcu_pre;
	}
}

static int bq27541_get_battery_fcf(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->fcf_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_fcf, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading fcf, ret:%d\n", ret);
			return gauge_ic->fcf_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->fcf_pre = val;
			return val;
		} else {
			return gauge_ic->fcf_pre;
		}
	} else {
		return gauge_ic->fcf_pre;
	}
}

static int bq27541_get_battery_sou(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->sou_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_sou, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading sou, ret:%d\n", ret);
			return gauge_ic->sou_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->sou_pre = val;
			return val;
		} else {
			return gauge_ic->sou_pre;
		}
	} else {
		return gauge_ic->sou_pre;
	}
}

static int bq27541_get_battery_do0(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->do0_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_do0, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading do0, ret:%d\n", ret);
			return gauge_ic->do0_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->do0_pre = val;
			return val;
		} else {
			return gauge_ic->do0_pre;
		}
	} else {
		return gauge_ic->do0_pre;
	}
}

static int bq27541_get_sub_battery_do0(void)
{
	int ret = 0;
	int val = 0;

	if (!sub_gauge_ic)
		return 0;

	if (sub_gauge_ic->device_type == DEVICE_BQ27541 || sub_gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426)
		return -1;

	if (atomic_read(&sub_gauge_ic->suspended) == 1 || atomic_read(&sub_gauge_ic->gauge_i2c_status))
		return sub_gauge_ic->do0_pre;

	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(sub_gauge_ic, sub_gauge_ic->cmd_addr.reg_do0, &val);
		if (ret) {
			dev_err(sub_gauge_ic->dev, "error reading do0, ret:%d\n", ret);
			return sub_gauge_ic->do0_pre;
		}
		if (!sub_gauge_ic->batt_bq28z610) {
			sub_gauge_ic->do0_pre = val;
			return val;
		} else {
			return sub_gauge_ic->do0_pre;
		}
	} else {
		return sub_gauge_ic->do0_pre;
	}
}

static int bq27541_get_battery_doe(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->doe_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_doe, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading doe, ret:%d\n", ret);
			return gauge_ic->doe_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->doe_pre = val;
			return val;
		} else {
			return gauge_ic->doe_pre;
		}
	} else {
		return gauge_ic->doe_pre;
	}
}

static int bq27541_get_battery_trm(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->trm_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_trm, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading trm, ret:%d\n", ret);
			return gauge_ic->trm_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->trm_pre = val;
			return val;
		} else {
			return gauge_ic->trm_pre;
		}
	} else {
		return gauge_ic->trm_pre;
	}
}

static int bq27541_get_battery_pc(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->pc_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_pc, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading pc, ret:%d\n", ret);
			return gauge_ic->pc_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->pc_pre = val;
			return val;
		} else {
			return gauge_ic->pc_pre;
		}
	} else {
		return gauge_ic->pc_pre;
	}
}

static int bq27541_get_battery_qs(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602 || gauge_ic->device_type == DEVICE_BQ27426) {
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->qs_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_qs, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading qs, ret:%d\n", ret);
			return gauge_ic->qs_pre;
		}
		if (!gauge_ic->batt_bq28z610) {
			gauge_ic->qs_pre = val;
			return val;
		} else {
			return gauge_ic->qs_pre;
		}
	} else {
		return gauge_ic->qs_pre;
	}
}

static int bq27541_get_battery_mvolts_2cell_max(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return gauge_ic->batt_cell_max_vol;
}

static int bq27541_get_battery_mvolts_2cell_min(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return gauge_ic->batt_cell_min_vol;
}

#define DEVICE_CHEMISTRY_LION 1
#define DEVICE_CHEMISTRY_C2A1 2
#define DEVICE_CHEMISTRY_C2A2 3
#define DEVICE_CHEMISTRY_UNKOWN 99
static int bq28z610_get_device_chemistry(struct chip_bq27541 *chip)
{
	u8 data[4] = { 0, 0, 0, 0 };
	int ret = 0;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->suspended) == 1 || atomic_read(&chip->gauge_i2c_status)) {
		return 0;
	}

	if (chip->batt_bq28z610) {
		if (oplus_vooc_get_allow_reading() == true) {
			mutex_lock(&chip->gauge_alt_manufacturer_access);
			gauge_i2c_txsubcmd(chip, BQ28Z610_DEVICE_CHEMISTRY_EN_ADDR, BQ28Z610_DEVICE_CHEMISTRY_CMD);
			usleep_range(1000, 1000);
			ret = gauge_read_i2c_block(chip, BQ28Z610_DEVICE_CHEMISTRY_ADDR,
						     BQ28Z610_DEVICE_CHEMISTRY_SIZE, data);
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			if (ret) {
				dev_err(chip->dev, "error reading operation status.\n");
				return 0;
			}
			dev_info(chip->dev, "device chemistry: [%c%c%c%c]\n", data[0], data[1], data[2], data[3]);
			if (data[0] == 0x4C && data[1] == 0x49 && data[2] == 0x4F && data[3] == 0x4E) {
				return DEVICE_CHEMISTRY_LION;
			} else if (data[0] == 0x43 && data[1] == 0x32 && data[2] == 0x41 && data[3] == 0x31) {
				return DEVICE_CHEMISTRY_C2A1;
			} else if (data[0] == 0x43 && data[1] == 0x32 && data[2] == 0x41 && data[3] == 0x32) {
				return DEVICE_CHEMISTRY_C2A2;
			} else {
				return DEVICE_CHEMISTRY_UNKOWN;
			}
		}
	}
	return 0;
}

static int bq28z610_get_balancing_config(struct chip_bq27541 *chip)
{
	u8 data[4] = { 0, 0, 0, 0 };
	int ret = 0;
	int balancing_config = 0;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->suspended) == 1 || atomic_read(&chip->gauge_i2c_status)) {
		return 0;
	}

	if (chip->batt_bq28z610) {
		if (oplus_vooc_get_allow_reading() == true) {
			mutex_lock(&chip->gauge_alt_manufacturer_access);
			gauge_i2c_txsubcmd(chip, BQ28Z610_OPERATION_STATUS_EN_ADDR, BQ28Z610_OPERATION_STATUS_CMD);
			usleep_range(1000, 1000);
			ret = gauge_read_i2c_block(chip, BQ28Z610_OPERATION_STATUS_ADDR,
						     BQ28Z610_OPERATION_STATUS_SIZE, data);
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			if (ret) {
				dev_err(chip->dev, "error reading operation status.\n");
				return chip->pre_balancing_config;
			}
			balancing_config =
				(u32)(((u32)data[3] << 24 | (u32)data[2] << 16 | (u32)data[1] << 8 | data[0]) &
				      BQ28Z610_BALANCING_CONFIG_BIT) >>
				28;
			chip->pre_balancing_count++;
			if (balancing_config ^ chip->pre_balancing_config || chip->pre_balancing_count >= 10) {
				chip->pre_balancing_count = 0;
				dev_info(chip->dev, "operation status[0x%x], cb28[%d]\n",
					 data[3] << 24 | data[2] << 16 | data[1] << 8 | data[0], balancing_config);
			}
			chip->pre_balancing_config = balancing_config;
			return balancing_config;
		} else {
			return chip->pre_balancing_config;
		}
	}
	return 0;
}

#define TEMP_LT_16C 1 /* -2-3-4-5-6-7 */
#define TEMP_LT_39C 2 /* -1-2-3-4-5 */
#define TEMP_HT_39C 3 /* -1-2-3-4 */
#define BATT_TEMP_16C 160
#define BATT_TEMP_39C 390
static int batt_balancing_config = 0;
static int bq28z610_get_battery_balancing_status(void)
{
	return batt_balancing_config;
}
static int bq27541_get_battery_temperature(void)
{
	int ret = 0;
	int temp = 0;
	static int pre_batt_balancing_config = 0;
	static int count = 0;
	static int cb_count = 0;
	static int cb_flag = 0;
	static int temp_status = 0;
	static int delta_temp = 0;
	struct task_struct *t = NULL;
	int retry = RETRY_CNT;

	if (!gauge_ic) {
		return 0;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_temp, &temp);
			if (ret || normal_range_judge(TEMP_MAX, TEMP_MIN, temp))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("temperature out of range,retry=%d,temperature=%d", retry, temp);
		} while (retry--);
		if (ret) {
			count++;
			dev_err(gauge_ic->dev, "error reading temperature\n");
			if (count > 1) {
				count = 0;
				gauge_ic->temp_pre = -400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
				return -400;
			} else {
				return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
			}
		}
		count = 0;
	} else {
		return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
	}

	if (gauge_ic->bq28z610_need_balancing) {
		t = get_current();
		if (t != NULL && !strncmp(t->comm, "tbatt_pwroff", 12))
			goto cb_exit;

		batt_balancing_config = bq28z610_get_balancing_config(gauge_ic);
		if (pre_batt_balancing_config == 0 && batt_balancing_config == 1 && temp_status == 0) {
			if (gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN <= BATT_TEMP_16C) {
				temp_status = TEMP_LT_16C;
				delta_temp = 10;
			} else if (gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN <= BATT_TEMP_39C) {
				temp_status = TEMP_LT_39C;
				delta_temp = 0;
			} else {
				temp_status = TEMP_HT_39C;
				delta_temp = 0;
			}
			printk(KERN_ERR "SJC-TEST: temp_status[%d], delta_temp[%d]\n", temp_status, delta_temp);
		}
		pre_batt_balancing_config = batt_balancing_config;

		if (batt_balancing_config == 1) {
			cb_flag = 1;
			cb_count++;

			if (gauge_ic->bq28z610_device_chem == DEVICE_CHEMISTRY_C2A1 ||
			    gauge_ic->bq28z610_device_chem == DEVICE_CHEMISTRY_C2A2) {
				if (cb_count >= 4 && temp_status == TEMP_LT_16C) {
					temp = temp - 20;
					printk(KERN_ERR "SJC-TEST C2A1: - 20\n");
				} else if (cb_count >= 3 && temp_status != TEMP_HT_39C) {
					temp = temp - 15;
					printk(KERN_ERR "SJC-TEST C2A1: - 15\n");
				} else if (cb_count >= 2) {
					temp = temp - 10;
					printk(KERN_ERR "SJC-TEST C2A1: - 10\n");
				} else if (cb_count >= 1) {
					temp = temp - 5;
					printk(KERN_ERR "SJC-TEST C2A1: - 5\n");
				}

				if (temp_status == TEMP_LT_16C) {
					if (cb_count >= 4)
						cb_count = 4;
				} else if (temp_status != TEMP_HT_39C) {
					if (cb_count >= 3)
						cb_count = 3;
				} else {
					if (cb_count >= 2)
						cb_count = 2;
				}
			} else {
				if (cb_count >= 6 && temp_status == TEMP_LT_16C) {
					temp = temp - (60 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 60 + delta_temp);
				} else if (cb_count >= 5 && temp_status != TEMP_HT_39C) {
					temp = temp - (50 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 50 + delta_temp);
				} else if (cb_count >= 4) {
					temp = temp - (40 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 40 + delta_temp);
				} else if (cb_count >= 3) {
					temp = temp - (30 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 30 + delta_temp);
				} else if (cb_count >= 2) {
					temp = temp - (20 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 20 + delta_temp);
				} else if (cb_count >= 1) {
					temp = temp - (10 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 10 + delta_temp);
				}

				if (temp_status == TEMP_LT_16C) {
					if (cb_count >= 6)
						cb_count = 6;
				} else if (temp_status != TEMP_HT_39C) {
					if (cb_count >= 5)
						cb_count = 5;
				} else {
					if (cb_count >= 4)
						cb_count = 4;
				}
			}
		} else if (cb_flag == 1 && cb_count > 0) {
			if (gauge_ic->bq28z610_device_chem == DEVICE_CHEMISTRY_C2A1 ||
			    gauge_ic->bq28z610_device_chem == DEVICE_CHEMISTRY_C2A2) {
				if (cb_count >= 4 && temp_status == TEMP_LT_16C) {
					temp = temp - 20;
					printk(KERN_ERR "SJC-TEST 4 C2A1: - 20\n");
				} else if (cb_count >= 3 && temp_status != TEMP_HT_39C) {
					temp = temp - 15;
					printk(KERN_ERR "SJC-TEST 3 C2A1: - 15\n");
				} else if (cb_count >= 2) {
					temp = temp - 10;
					printk(KERN_ERR "SJC-TEST 2 C2A1: - 10\n");
				} else if (cb_count >= 1) {
					temp = temp - 5;
					printk(KERN_ERR "SJC-TEST 1 C2A1: - 5\n");
				}
			} else {
				if (cb_count >= 6 && temp_status == TEMP_LT_16C) {
					temp = temp - (60 + delta_temp);
					printk(KERN_ERR "SJC-TEST 6: - %d\n", 60 + delta_temp);
				} else if (cb_count >= 5 && temp_status != TEMP_HT_39C) {
					temp = temp - (50 + delta_temp);
					printk(KERN_ERR "SJC-TEST 5: - %d\n", 50 + delta_temp);
				} else if (cb_count >= 4) {
					temp = temp - (40 + delta_temp);
					printk(KERN_ERR "SJC-TEST 4: - %d\n", 40 + delta_temp);
				} else if (cb_count >= 3) {
					temp = temp - (30 + delta_temp);
					printk(KERN_ERR "SJC-TEST 3: - %d\n", 30 + delta_temp);
				} else if (cb_count >= 2) {
					temp = temp - (20 + delta_temp);
					printk(KERN_ERR "SJC-TEST 2: - %d\n", 20 + delta_temp);
				} else if (cb_count >= 1) {
					temp = temp - (10 + delta_temp);
					printk(KERN_ERR "SJC-TEST 1: - %d\n", 10 + delta_temp);
				}
			}

			cb_count--;
		} else {
			cb_count = 0;
			cb_flag = 0;
			temp_status = 0;
			delta_temp = 0;
		}
	}
cb_exit:
	if ((temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN) > 1000) {
		temp = gauge_ic->temp_pre;
	}
	gauge_ic->temp_pre = temp;
	return temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

static int bq27541_get_sub_battery_temperature(void)
{
	int ret = 0;
	int temp = 0;
	static int count = 0;
	int retry = RETRY_CNT;

	if (!sub_gauge_ic) {
		return 0;
	}
	if (atomic_read(&sub_gauge_ic->suspended) == 1) {
		return sub_gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(sub_gauge_ic, sub_gauge_ic->cmd_addr.reg_temp, &temp);
			if (ret || normal_range_judge(TEMP_MAX, TEMP_MIN, temp))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("temperature out of range,retry=%d,temperature=%d", retry, temp);
		} while (retry--);
		if (ret) {
			count++;
			dev_err(sub_gauge_ic->dev, "error reading temperature\n");
			if (count > 1) {
				count = 0;
				sub_gauge_ic->temp_pre = -400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
				return -400;
			} else {
				return sub_gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
			}
		}
		count = 0;
	} else {
		return sub_gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
	}

	sub_gauge_ic->temp_pre = temp;
	return temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

static bool bq27541_is_battery_present(void)
{
	int flags = 0, ret = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return 0;
	}
	ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.subcmd_chem_id, &flags);
	if (ret < 0) {
		pr_err("read bq27541  fail\n");
		mdelay(100);
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.subcmd_chem_id, &flags);
		if (ret < 0) {
			pr_err("read bq27541  fail again\n");
			return true;
		}
	}
	return true;
}

static int bq27541_get_batt_remaining_capacity(void)
{
	int ret;
	int cap = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->rm_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_rm, &cap);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading capacity.\n");
			return ret;
		}
		gauge_ic->rm_pre = cap;
		return gauge_ic->rm_pre;
	} else {
		return gauge_ic->rm_pre;
	}
}

static int bq27541_get_sub_gauge_batt_remaining_capacity(void)
{
	int ret;
	int cap = 0;

	if (!sub_gauge_ic) {
		return 0;
	}
	if (atomic_read(&sub_gauge_ic->suspended) == 1 || atomic_read(&sub_gauge_ic->gauge_i2c_status)) {
		return sub_gauge_ic->rm_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(sub_gauge_ic, sub_gauge_ic->cmd_addr.reg_rm, &cap);
		if (ret) {
			dev_err(sub_gauge_ic->dev, "error reading capacity.\n");
			return ret;
		}
		sub_gauge_ic->rm_pre = cap;
		return sub_gauge_ic->rm_pre;
	} else {
		return sub_gauge_ic->rm_pre;
	}
}

static int zy602_get_dod0_parameters(int *batt_dod0_1)
{
	unsigned char dod0_buffer[ZY602_MAC_CELL_DOD0_SIZE] = { 0 };

	if (!gauge_ic || (DEVICE_ZY0602 != gauge_ic->device_type)
		|| !oplus_vooc_get_allow_reading()
		|| atomic_read(&gauge_ic->gauge_i2c_status)) {
		dev_err(gauge_ic->dev, "gauge ic is NULL or is not zy602 or not allow_reading!\n");
		return -1;
	}

	mutex_lock(&gauge_ic->gauge_alt_manufacturer_access);
	gauge_i2c_txsubcmd(gauge_ic, ZY602_MAC_CELL_DOD0_EN_ADDR, ZY602_MAC_CELL_DOD0_CMD);

	usleep_range(1000, 1000);
	gauge_read_i2c_block(gauge_ic, ZY602_MAC_CELL_DOD0_ADDR, ZY602_MAC_CELL_DOD0_SIZE, dod0_buffer);
	mutex_unlock(&gauge_ic->gauge_alt_manufacturer_access);
	*batt_dod0_1 = (dod0_buffer[11] << 8) | dod0_buffer[10];

	dev_err(gauge_ic->dev, "!!![zy602_get_dod0_parameters] dod0 1: %d", *batt_dod0_1);

	return 0;
}

static int zy602_get_sub_dod0_parameters(int *batt_dod0_1)
{
	unsigned char dod0_buffer[ZY602_MAC_CELL_DOD0_SIZE] = { 0 };

	if (!sub_gauge_ic || (DEVICE_ZY0602 != sub_gauge_ic->device_type)
		|| !oplus_vooc_get_allow_reading()
		|| atomic_read(&sub_gauge_ic->gauge_i2c_status)) {
		dev_err(sub_gauge_ic->dev, "gauge ic is NULL or is not zy602 or not allow_reading!\n");
		return -1;
	}

	mutex_lock(&sub_gauge_ic->gauge_alt_manufacturer_access);
	gauge_i2c_txsubcmd(sub_gauge_ic, ZY602_MAC_CELL_DOD0_EN_ADDR, ZY602_MAC_CELL_DOD0_CMD);

	usleep_range(1000, 1000);
	gauge_read_i2c_block(sub_gauge_ic, ZY602_MAC_CELL_DOD0_ADDR, ZY602_MAC_CELL_DOD0_SIZE, dod0_buffer);
	mutex_unlock(&sub_gauge_ic->gauge_alt_manufacturer_access);
	*batt_dod0_1 = (dod0_buffer[11] << 8) | dod0_buffer[10];

	dev_err(sub_gauge_ic->dev, "!!![zy602_get_dod0_parameters] dod0 1: %d", *batt_dod0_1);

	return 0;
}

static int bq28z610_get_dod0_parameters(int *batt_dod0_1, int *batt_dod0_2, int *batt_dod0_passed_q)
{
	unsigned char dod0_buffer[BQ28Z610_MAC_CELL_DOD0_SIZE] = { 0, 0, 0, 0, 0, 0 };
	short dod0_passed_q_temp;

	if (!gauge_ic || !gauge_ic->batt_bq28z610
		|| !oplus_vooc_get_allow_reading()
		|| atomic_read(&gauge_ic->gauge_i2c_status)) {
		dev_err(gauge_ic->dev, "gauge ic is NULL or is not 28z610 or not allow_reading!\n");
		return -1;
	}

	mutex_lock(&gauge_ic->gauge_alt_manufacturer_access);
	gauge_i2c_txsubcmd(gauge_ic, BQ28Z610_MAC_CELL_DOD0_EN_ADDR, BQ28Z610_MAC_CELL_DOD0_CMD);
	usleep_range(1000, 1000);
	if (gauge_ic->batt_zy0603) {
		gauge_read_i2c_block(gauge_ic, ZY0603_MAC_CELL_SOCCAL0, BQ28Z610_MAC_CELL_DOD0_SIZE, dod0_buffer);
	} else {
		gauge_read_i2c_block(gauge_ic, BQ28Z610_MAC_CELL_DOD0_ADDR, BQ28Z610_MAC_CELL_DOD0_SIZE, dod0_buffer);
	}
	mutex_unlock(&gauge_ic->gauge_alt_manufacturer_access);
	dod0_passed_q_temp = (dod0_buffer[5] << 8) | dod0_buffer[4];
	*batt_dod0_1 = (dod0_buffer[1] << 8) | dod0_buffer[0];
	*batt_dod0_2 = (dod0_buffer[3] << 8) | dod0_buffer[2];
	if (dod0_passed_q_temp & 0x8000) {
		dod0_passed_q_temp = -((~(dod0_passed_q_temp - 1)) & 0xFFFF);
	}
	*batt_dod0_passed_q = dod0_passed_q_temp;
	dev_err(gauge_ic->dev, "!!![bq28z610_get_dod0_parameters]dod0 passed q: %d, dod0 1: %d, dod0 2: %d",
		*batt_dod0_passed_q, *batt_dod0_1, *batt_dod0_2);

	return 0;
}

static int zy602_get_qmax_parameters(int *batt_qmax_1)
{
	unsigned char qmax_buffer_1[ZY602_MAC_CELL_QMAX_SIZE_A] = { 0 };

	if (!gauge_ic || (DEVICE_ZY0602 != gauge_ic->device_type)
		|| !oplus_vooc_get_allow_reading()
		|| atomic_read(&gauge_ic->gauge_i2c_status)) {
		dev_err(gauge_ic->dev, "gauge ic is NULL or is not zy602 or not allow_reading!\n");
		return -1;
	}

	mutex_lock(&gauge_ic->gauge_alt_manufacturer_access);
	gauge_i2c_txsubcmd(gauge_ic, ZY602_MAC_CELL_QMAX_EN_ADDR, ZY602_MAC_CELL_QMAX_CMD);
	usleep_range(1000, 1000);
	gauge_read_i2c_block(gauge_ic, ZY602_MAC_CELL_QMAX_ADDR_A, ZY602_MAC_CELL_QMAX_SIZE_A, qmax_buffer_1);

	mutex_unlock(&gauge_ic->gauge_alt_manufacturer_access);
	*batt_qmax_1 = (qmax_buffer_1[17] << 8) | qmax_buffer_1[16];

	return 0;
}

static int zy602_get_sub_qmax_parameters(int *batt_qmax_1)
{
	unsigned char qmax_buffer_1[ZY602_MAC_CELL_QMAX_SIZE_A] = { 0 };

	if (!sub_gauge_ic || (DEVICE_ZY0602 != sub_gauge_ic->device_type)
			|| !oplus_vooc_get_allow_reading()
			|| atomic_read(&sub_gauge_ic->gauge_i2c_status)) {
		dev_err(sub_gauge_ic->dev, "gauge ic is NULL or is not zy602 or not allow_reading!\n");
		return -1;
	}

	mutex_lock(&sub_gauge_ic->gauge_alt_manufacturer_access);
	gauge_i2c_txsubcmd(sub_gauge_ic, ZY602_MAC_CELL_QMAX_EN_ADDR, ZY602_MAC_CELL_QMAX_CMD);
	usleep_range(1000, 1000);
	gauge_read_i2c_block(sub_gauge_ic, ZY602_MAC_CELL_QMAX_ADDR_A, ZY602_MAC_CELL_QMAX_SIZE_A, qmax_buffer_1);

	mutex_unlock(&sub_gauge_ic->gauge_alt_manufacturer_access);
	*batt_qmax_1 = (qmax_buffer_1[17] << 8) | qmax_buffer_1[16];

	return 0;
}

static int bq28z610_get_qmax_parameters(int *batt_qmax_1, int *batt_qmax_2, int *batt_qmax_passed_q)
{
	unsigned char qmax_buffer_1[BQ28Z610_MAC_CELL_QMAX_SIZE_A] = { 0, 0, 0, 0 };
	unsigned char qmax_buffer_2[BQ28Z610_MAC_CELL_QMAX_SIZE_B] = { 0, 0 };
	short qmax_passed_q_temp;

	if (!gauge_ic || !gauge_ic->batt_bq28z610
		|| !oplus_vooc_get_allow_reading()
		|| atomic_read(&gauge_ic->gauge_i2c_status)) {
		dev_err(gauge_ic->dev, "gauge ic is NULL or is not 28z610 or not allow_reading!\n");
		return -1;
	}

	mutex_lock(&gauge_ic->gauge_alt_manufacturer_access);
	gauge_i2c_txsubcmd(gauge_ic, BQ28Z610_MAC_CELL_QMAX_EN_ADDR, BQ28Z610_MAC_CELL_QMAX_CMD);
	usleep_range(1000, 1000);
	gauge_read_i2c_block(gauge_ic, BQ28Z610_MAC_CELL_QMAX_ADDR_A, BQ28Z610_MAC_CELL_QMAX_SIZE_A, qmax_buffer_1);

	gauge_i2c_txsubcmd(gauge_ic, BQ28Z610_MAC_CELL_QMAX_EN_ADDR, BQ28Z610_MAC_CELL_QMAX_CMD);
	usleep_range(1000, 1000);
	gauge_read_i2c_block(gauge_ic, BQ28Z610_MAC_CELL_QMAX_ADDR_B, BQ28Z610_MAC_CELL_QMAX_SIZE_B, qmax_buffer_2);
	mutex_unlock(&gauge_ic->gauge_alt_manufacturer_access);
	*batt_qmax_1 = (qmax_buffer_1[1] << 8) | qmax_buffer_1[0];
	*batt_qmax_2 = (qmax_buffer_1[3] << 8) | qmax_buffer_1[2];
	qmax_passed_q_temp = (qmax_buffer_2[1] << 8) | qmax_buffer_2[0];
	if (qmax_passed_q_temp & 0x8000) {
		qmax_passed_q_temp = -((~(qmax_passed_q_temp - 1)) & 0xFFFF);
	}
	*batt_qmax_passed_q = qmax_passed_q_temp;

	return 0;
}

static int zy602_soh_hardware_defect_avoidance_scheme(struct chip_bq27541 *chip, int *soh)
{
	int batt_capacity_mah = 0;
	int capacity_pct_s = 0;
	int batt_qmax_1 = 0;
	int batt_qmax_2 = 0;
	int batt_qmax_passed_q = 0;
	int ret_q = 0;

	if (!chip) {
		return -1;
	}

	if (0 == chip->capacity_pct) {
		capacity_pct_s = 100;
	} else {
		capacity_pct_s = chip->capacity_pct;
	}
	batt_capacity_mah = oplus_chg_get_design_capacity() * capacity_pct_s / 100;

	if (batt_capacity_mah <= 0) {
		return -1;
	}

	if (DEVICE_ZY0602 == chip->device_type) {
		if (chip == gauge_ic) {
			ret_q = zy602_get_qmax_parameters(&batt_qmax_1);
		} else if (chip == sub_gauge_ic) {
			ret_q = zy602_get_sub_qmax_parameters(&batt_qmax_1);
		}
	} else if (true == chip->batt_zy0603) {
		if (chip == gauge_ic) {
			ret_q = bq28z610_get_qmax_parameters(&batt_qmax_1, &batt_qmax_2, &batt_qmax_passed_q);
		} else if (chip == sub_gauge_ic) {
			ret_q = bq28z610_get_qmax_parameters(&batt_qmax_2, &batt_qmax_1, &batt_qmax_passed_q);
		}
		batt_qmax_1 = batt_qmax_1 + batt_qmax_2;
	}

	*soh = batt_qmax_1 * 100 / batt_capacity_mah;

	return ret_q;
}

static int bq27541_get_battery_soh(void) /*  sjc20150105  */
{
	int ret = 0;
	int soh = 0;
	int ret_q = 0;
	int retry = RETRY_CNT;

	if (!gauge_ic) {
		return 0;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->soh_pre;
	}

	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_soh, &soh);

			if ((DEVICE_ZY0602 == gauge_ic->device_type) || (true == gauge_ic->batt_zy0603))
				ret_q = zy602_soh_hardware_defect_avoidance_scheme(gauge_ic, &soh);
			if (ret || (ret_q < 0)) {
				dev_err(gauge_ic->dev, "error reading soh.\n");
				return 0;
			}
			if (normal_range_judge(SOH_MAX, SOH_MIN, soh))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("soh out of range,retry=%d,soh=%d", retry, soh);
		} while (retry--);

		if (soh > 100)
			soh = 100;

		sh366002_read_gaugeinfo_block(gauge_ic);
	} else {
		if (gauge_ic->soh_pre) {
			return gauge_ic->soh_pre;
		} else {
			return 0;
		}
	}
	gauge_ic->soh_pre = soh;
	return soh;
}

static int bq27541_get_sub_battery_soh(void)
{
	int ret = 0;
	int soh = 0;
	int ret_q = 0;
	int retry = RETRY_CNT;

	if (!sub_gauge_ic) {
		return 0;
	}
	if (atomic_read(&sub_gauge_ic->suspended) == 1 || atomic_read(&sub_gauge_ic->gauge_i2c_status)) {
		return sub_gauge_ic->soh_pre;
	}

	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(sub_gauge_ic, sub_gauge_ic->cmd_addr.reg_soh, &soh);

			if ((DEVICE_ZY0602 == sub_gauge_ic->device_type) || (true == sub_gauge_ic->batt_zy0603))
				ret_q = zy602_soh_hardware_defect_avoidance_scheme(sub_gauge_ic, &soh);
			if (ret || (ret_q < 0)) {
				dev_err(sub_gauge_ic->dev, "error reading soh.\n");
				return 0;
			}

			if (normal_range_judge(SOH_MAX, SOH_MIN, soh))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("soh out of range,retry=%d,soh=%d", retry, soh);
		} while (retry--);

		if (soh > 100)
			soh = 100;

		sh366002_read_gaugeinfo_block(sub_gauge_ic);
	} else {
		if (sub_gauge_ic->soh_pre) {
			return sub_gauge_ic->soh_pre;
		} else {
			return 0;
		}
	}
	sub_gauge_ic->soh_pre = soh;
	return soh;
}

static int bq28z610_get_firstcell_voltage(void)
{
	if (!gauge_ic || !gauge_ic->batt_bq28z610 || !oplus_vooc_get_allow_reading()) {
		dev_err(gauge_ic->dev, "gauge ic is NULL or is not 28z610 or not allow_reading!\n");
		return 0;
	}

	bq28z610_get_2cell_voltage();
	return gauge_ic->batt_cell_1_vol;
}

static int bq28z610_get_secondcell_voltage(void)
{
	if (!gauge_ic || !gauge_ic->batt_bq28z610 || !oplus_vooc_get_allow_reading()) {
		dev_err(gauge_ic->dev, "gauge ic is NULL or is not 28z610 or not allow_reading!\n");
		return 0;
	}

	bq28z610_get_2cell_voltage();
	return gauge_ic->batt_cell_2_vol;
}

static int bq27541_get_prev_batt_remaining_capacity(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return gauge_ic->rm_pre;
}
static int bq27541_get_sub_gauge_prev_batt_remaining_capacity(void)
{
	if (!sub_gauge_ic) {
		return 0;
	}
	return sub_gauge_ic->rm_pre;
}

static int zy602_get_battery_passed_q(void)
{
	int ret = 0;
	int passed_q = 0;

	if ((!gauge_ic) || (DEVICE_ZY0602 != gauge_ic->device_type)) {
		return 0;
	}

	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->passed_q_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_logbuf, &passed_q);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading passed_q.\n");
			return ret;
		}
	} else {
		if (gauge_ic->passed_q_pre) {
			return gauge_ic->passed_q_pre;
		} else {
			return 0;
		}
	}
	gauge_ic->passed_q_pre = passed_q;
	return passed_q;
}

static int bq27541_get_battery_soc(void)
{
	int ret;
	int soc = 0, soc_check = 0;

	if (!gauge_ic) {
		return 50;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->soc_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_soc, &soc);
		if (ret) {
			chg_err("error reading soc.ret:%d\n", ret);
			goto read_soc_err;
		}
		msleep(10);
		ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_soc, &soc_check);
		if (ret) {
			chg_err("error reading soc.ret:%d\n", ret);
			goto read_soc_err;
		}
		if ((soc > 100) || ((soc & 0xff) > 100) || (abs((soc_check - soc)) > 3)) {
			msleep(50);
			chg_err("[bq27541_get_battery_soc]soc:%d\n", soc);
			ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_soc, &soc);
			if (ret) {
				chg_err("error reading fcc.\n");
				return ret;
			}
		}
	} else {
		if (gauge_ic->soc_pre) {
			return gauge_ic->soc_pre;
		} else {
			return 0;
		}
	}
	soc = bq27541_soc_calibrate(gauge_ic, soc);
	return soc;

read_soc_err:
	if (gauge_ic->soc_pre) {
		return gauge_ic->soc_pre;
	} else {
		return 0;
	}
}

static int bq27541_get_sub_battery_soc(void)
{
	int ret;
	int soc = 0;

	if (!sub_gauge_ic) {
		return 50;
	}
	if (atomic_read(&sub_gauge_ic->suspended) == 1 || atomic_read(&sub_gauge_ic->gauge_i2c_status)) {
		return sub_gauge_ic->soc_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(sub_gauge_ic, sub_gauge_ic->cmd_addr.reg_soc, &soc);
		if (ret) {
			dev_err(sub_gauge_ic->dev, "error reading soc.ret:%d\n", ret);
			goto read_soc_err;
		}
	} else {
		if (sub_gauge_ic->soc_pre) {
			return sub_gauge_ic->soc_pre;
		} else {
			return 0;
		}
	}
	soc = bq27541_soc_calibrate(sub_gauge_ic, soc);
	return soc;

read_soc_err:
	if (sub_gauge_ic->soc_pre) {
		return sub_gauge_ic->soc_pre;
	} else {
		return 0;
	}
}

static int bq27541_get_average_current(void)
{
	int ret;
	int curr = 0;
	int retry = 5;
	int data_retry = RETRY_CNT;
	int temp_curr = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return -gauge_ic->current_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_ai, &curr);
			if (curr & 0x8000)
				temp_curr = ((~(curr - 1)) & 0xFFFF);
			else
				temp_curr = -curr;
			if (ret || normal_range_judge(CURR_MAX, CURR_MIN, temp_curr))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("current out of range,retry=%d,current=%d", data_retry, temp_curr);
		} while (data_retry--);

		if (ret) {
			while (retry > 0) {
				usleep_range(5000, 5000);
				ret = gauge_read_i2c(gauge_ic, gauge_ic->cmd_addr.reg_ai, &curr);
				if (ret < 0) {
					retry--;
				} else {
					break;
				}
			}
			dev_err(gauge_ic->dev, "error reading current.\n");
			return -gauge_ic->current_pre;
		}
	} else {
		return -gauge_ic->current_pre;
	}
	/* negative current */
	if (curr & 0x8000) {
		curr = -((~(curr - 1)) & 0xFFFF);
	}
	gauge_ic->current_pre = curr;
	return -curr;
}

static int bq27541_get_sub_battery_average_current(void)
{
	int ret;
	int curr = 0;
	int retry = 5;
	int data_retry = RETRY_CNT;
	int temp_curr = 0;

	if (!sub_gauge_ic) {
		return 0;
	}
	if (atomic_read(&sub_gauge_ic->suspended) == 1 || atomic_read(&sub_gauge_ic->gauge_i2c_status)) {
		return -sub_gauge_ic->current_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		do {
			ret = gauge_read_i2c(sub_gauge_ic, sub_gauge_ic->cmd_addr.reg_ai, &curr);
			if (curr & 0x8000)
				temp_curr = ((~(curr - 1)) & 0xFFFF);
			else
				temp_curr = -curr;
			if (ret || normal_range_judge(CURR_MAX, CURR_MIN, temp_curr))
				break;
			else
				usleep_range(10000, 10000);
			chg_err("current out of range,retry=%d,current=%d", data_retry, temp_curr);
		} while (data_retry--);
		if (ret) {
			while (retry > 0) {
				usleep_range(5000, 5000);
				ret = gauge_read_i2c(sub_gauge_ic, sub_gauge_ic->cmd_addr.reg_ai, &curr);
				if (ret < 0) {
					retry--;
				} else {
					break;
				}
			}
			dev_err(sub_gauge_ic->dev, "error reading current.\n");
			return sub_gauge_ic->current_pre;
		}
	} else {
		return -sub_gauge_ic->current_pre;
	}
	/* negative current */
	if (curr & 0x8000) {
		curr = -((~(curr - 1)) & 0xFFFF);
	}
	sub_gauge_ic->current_pre = curr;
	return -curr;
}

static int bq27541_sha1_hmac_authenticate(struct bq27541_authenticate_data *authenticate_data);

#define ZY0602_RETRY_MAX_COUNT (3)
static bool bq27541_get_battery_hmac(void)
{
	int i;
	bool result = false;

	if (!gauge_ic) {
		return true;
	}

	if (oplus_is_rf_ftm_mode()) {
		return true;
	}

	if (gauge_ic->batt_bq28z610) {
		/*return bq27541_is_authenticate_OK(gauge_ic);*/
		get_smem_batt_info(&auth_data, 1);
		if (init_gauge_auth(&auth_data, gauge_ic->authenticate_data))
			return true;
		pr_info("%s:gauge authenticate failed, try again\n", __func__);
		get_smem_batt_info(&auth_data, 0);
		return init_gauge_auth(&auth_data, gauge_ic->authenticate_data);
	} else if (gauge_ic->sha1_key_index == ZY0602_KEY_INDEX) {
		for (i = 0; i < ZY0602_RETRY_MAX_COUNT; i++) {
			if (get_smem_batt_info(&auth_data, ZY0602_KEY_INDEX)) {
				if (init_gauge_auth(&auth_data, gauge_ic->authenticate_data)) {
					result = true;
					break;
				}
			}
			msleep(10);
		}
	} else if (gauge_ic->support_sha256_hmac) {
		result = init_sha256_gauge_auth(gauge_ic);
	} else {
		return true;
	}

	return result;
}

#define GET_BATTERY_AUTH_RETRY_COUNT (5)
static bool bq27541_get_battery_authenticate(void)
{
	static int get_temp = 0;

	if (!gauge_ic) {
		return true;
	}

	pr_info("%s: temp_pre = %d, get_temp = %d.\n", __func__, gauge_ic->temp_pre, get_temp);

	if (gauge_ic->temp_pre == 0 || get_temp < GET_BATTERY_AUTH_RETRY_COUNT) {
		get_temp++;
		bq27541_get_battery_temperature();
		msleep(10);
		bq27541_get_battery_temperature();
	}

	if ((gauge_ic->temp_pre == (-400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN))
			|| (oplus_battery_type_check_bqfs(gauge_ic) == false)) {
		return false;
	} else {
		return true;
	}
}

static bool bq27541_get_sub_battery_authenticate(void)
{
	static bool get_temp = false;

	if (!sub_gauge_ic) {
		return true;
	}

	if (sub_gauge_ic->temp_pre == 0 && get_temp == false) {
		bq27541_get_sub_battery_temperature();
		msleep(10);
		bq27541_get_sub_battery_temperature();
	}
	get_temp = true;
	if (sub_gauge_ic->temp_pre == (-400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN)) {
		return false;
	} else {
		return true;
	}
}

static int bq27541_get_prev_battery_mvolts(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return gauge_ic->batt_vol_pre;
}

static int bq27541_get_sub_prev_battery_mvolts(void)
{
	if (!sub_gauge_ic) {
		return 0;
	}
	return sub_gauge_ic->batt_vol_pre;
}

static int bq27541_get_prev_battery_mvolts_2cell_max(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return gauge_ic->max_vol_pre;
}

static int bq27541_get_prev_battery_mvolts_2cell_min(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return gauge_ic->min_vol_pre;
}

static int bq27541_get_prev_battery_temperature(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

static int bq27541_get_prev_battery_soc(void)
{
	if (!gauge_ic) {
		return 50;
	}
	return gauge_ic->soc_pre;
}

static int bq27541_get_prev_average_current(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return -gauge_ic->current_pre;
}
static void bq27541_set_battery_full(bool full)
{
	/* Do nothing */
}

static void bq28z610_modify_dod0_parameter(struct chip_bq27541 *chip);

static int bq28z610_modify_dod0(void)
{
	if (!gauge_ic || gauge_ic->batt_zy0603) {
		return 0;
	}
	if (gauge_ic->batt_bq28z610) {
		bq28z610_modify_dod0_parameter(gauge_ic);
	}
	return 0;
}

static void bq28z610_modify_soc_smooth_parameter(struct chip_bq27541 *chip);
static int zy0603_write_data_cmd(struct chip_bq27541 *chip, bool enable);
static int bq27411_write_soc_smooth_parameter_for_zy(struct chip_bq27541 *chip, bool is_powerup);
static int bq28z610_update_soc_smooth_parameter(void)
{
	if (!gauge_ic) {
		return -1;
	}
	if (gauge_ic->batt_zy0603) {
		pr_err("%s batt_zy0603 begin\n", __func__);
		zy0603_write_data_cmd(gauge_ic, true);
		return GAUGE_OK;
	}

	if (gauge_ic->device_type == DEVICE_ZY0602) {
		bq27411_write_soc_smooth_parameter_for_zy(gauge_ic, false);
	}

	if (gauge_ic->batt_bq28z610) {
		bq28z610_modify_soc_smooth_parameter(gauge_ic);
	}
	if (gauge_ic->device_type == DEVICE_BQ27426 && gauge_ic->enable_sleep_mode) {
		bq27426_modify_soc_smooth_parameter(gauge_ic, true);
	}

	return 0;
}

static int bq28z610_update_sub_soc_smooth_parameter(void)
{
	if (!sub_gauge_ic) {
		return -1;
	}
	if (sub_gauge_ic->batt_zy0603) {
		pr_err("%s batt_zy0603 begin\n", __func__);
		zy0603_write_data_cmd(sub_gauge_ic, true);
		return GAUGE_OK;
	}

	if (sub_gauge_ic->device_type == DEVICE_ZY0602) {
		bq27411_write_soc_smooth_parameter_for_zy(sub_gauge_ic, false);
	}

	if (sub_gauge_ic->batt_bq28z610) {
		bq28z610_modify_soc_smooth_parameter(sub_gauge_ic);
	}
	return 0;
}

int bq27541_get_passedchg(int *val)
{
	int rc = -1;
	struct chip_bq27541 *chip;

	u8 value;
	s16 v = 0;

	if (gauge_ic == NULL)
		return rc;

	chip = gauge_ic;

	if (atomic_read(&gauge_ic->suspended) == 1)
		return rc;

	/*enable block data control */
	if (gauge_ic->batt_bq28z610) {
		rc = gauge_i2c_txsubcmd_onebyte(gauge_ic, BQ27411_BLOCK_DATA_CONTROL, 0x00);
		if (rc) {
			pr_err("%s enable block data control fail\n", __func__);
			goto out;
		}
		usleep_range(5000, 5000);

		gauge_i2c_txsubcmd(gauge_ic, BQ27411_DATA_CLASS_ACCESS, 0x0074);
		usleep_range(10000, 10000);
		rc = bq27541_read_i2c_onebyte(gauge_ic, 0x4E, &value);
		if (rc) {
			pr_err("%s read 0x4E fail\n", __func__);
			goto out;
		}
		v |= value;
		rc = bq27541_read_i2c_onebyte(gauge_ic, 0x4F, &value);
		if (rc) {
			pr_err("%s read 0x4F fail\n", __func__);
			goto out;
		}
		v |= (value << 8);
	} else {
		rc = bq27541_read_i2c_onebyte(gauge_ic, 0x6C, &value);
		if (rc) {
			pr_err("%s read 0x6c fail\n", __func__);
			goto out;
		}
		v |= value;
		rc = bq27541_read_i2c_onebyte(gauge_ic, 0x6D, &value);
		if (rc) {
			pr_err("%s read 0x6d fail\n", __func__);
			goto out;
		}
		v |= (value << 8);
	}

	*val = v;

	if (atomic_read(&gauge_ic->suspended) == 1)
		return -1;
out:
	return rc;
}

static int bq8z610_unseal(struct chip_bq27541 *chip);
static int bq8z610_seal(struct chip_bq27541 *chip);
static int bq8z610_sealed(struct chip_bq27541 *chip);

static int bq28z610_set_sleep_mode_withoutunseal(struct chip_bq27541 *chip, bool val)
{
	int rc = -1;
	u8 disable_data[BQ28Z610_REG_I2C_SIZE] = { 0xfd, 0x46, 0x01 };
	u8 enable_data[BQ28Z610_REG_I2C_SIZE] = { 0xfd, 0x46, 0x11 };
	u8 read_data[BQ28Z610_REG_I2C_SIZE] = { 0, 0, 0 };
	bool read_sleep_mode = 0;
	pr_err("%s enter, val=%d\n", __func__, val);
	if (chip == NULL)
		return rc;

	if (chip->batt_zy0603) {
		return rc;
	}

	if (!chip->batt_bq28z610) {
		return rc;
	}
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_CNTL1_DA_CONFIG);
	usleep_range(10000, 10000);
	gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, read_data);
	read_sleep_mode = (read_data[2] & BIT(4)) >> 4 ? true : false;
	pr_err("%s read_data=[%x],[%x],[%x] read_sleep_mode = %d\n", __func__, read_data[0], read_data[1], read_data[2],
	       read_sleep_mode);
	if (read_sleep_mode == val)
		return 0;
	if (val) {
		pr_err("%s enable sleep_mode, val=%d\n", __func__, val);
		rc = gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, &enable_data[0]);
		msleep(10);
		gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x05ab);
		msleep(10);
	} else {
		pr_err("%s disable sleep_mode, val=%d\n", __func__, val);
		rc = gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, &disable_data[0]);
		msleep(10);
		gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x05bb);
		msleep(10);
	}
	return rc;
}

static int bq28z610_set_sleep_mode(struct chip_bq27541 *chip, bool val)
{
	int rc = -1;

	pr_err("%s enter, val=%d\n", __func__, val);
	if (chip == NULL)
		return rc;

	if (atomic_read(&chip->suspended) == 1)
		return rc;

	/*enable block data control */
	if (chip->batt_bq28z610) {
		if (!bq8z610_unseal(chip)) {
			return rc;
		} else {
			msleep(50);
		}
		rc = bq28z610_set_sleep_mode_withoutunseal(chip, val);
		if (bq8z610_sealed(chip) == 0) {
			usleep_range(1000, 1000);
			bq8z610_seal(chip);
		}
	}

	return rc;
}

#define BCC_SET_DEBUG_PARMS 1
#define BCC_PAGE_SIZE 256
#define BCC_N_DEBUG 0
#define BCC_Y_DEBUG 1
static int bcc_debug_mode = BCC_N_DEBUG;
static char bcc_debug_buf[BCC_PAGE_SIZE] = { 0 };

static void bq28z610_bcc_set_buffer(int *buffer)
{
	int batt_qmax_1 = 0;
	int batt_qmax_2 = 0;
	int batt_qmax_passed_q = 0;
	int batt_dod0_1 = 0;
	int batt_dod0_2 = 0;
	int batt_dod0_passed_q = 0;
	int btemp = 0;
	int batt_current = 0, batt_current2 = 0;
	int voltage_cell1 = 0, voltage_cell2 = 0;
	int soc_ext_1 = 0, soc_ext_2 = 0;
	int bcc_current_max = 0, bcc_current_min = 0;
	int atl_last_gear_current = 0;

	if (oplus_is_pps_charging()) {
		bcc_current_max = oplus_pps_get_bcc_max_curr();
		bcc_current_min = oplus_pps_get_bcc_min_curr();
		atl_last_gear_current = oplus_pps_get_bcc_exit_curr();
	} else if (oplus_is_ufcs_charging()) {
		bcc_current_max = oplus_ufcs_get_bcc_max_curr();
		bcc_current_min = oplus_ufcs_get_bcc_min_curr();
		atl_last_gear_current = oplus_ufcs_get_bcc_exit_curr();
	} else if (true == oplus_is_voocphy_charging()) {
		bcc_current_max = oplus_voocphy_get_bcc_max_curr();
		bcc_current_min = oplus_voocphy_get_bcc_min_curr();
		atl_last_gear_current = oplus_voocphy_get_bcc_exit_curr();
	} else {
		bcc_current_max = oplus_vooc_check_bcc_max_curr();
		bcc_current_min = oplus_vooc_check_bcc_min_curr();
		atl_last_gear_current = oplus_vooc_get_bcc_exit_curr();
	}

	if ((true == gauge_ic->batt_zy0603) || (DEVICE_ZY0602 == gauge_ic->device_type)) {
		buffer[17] = SW_GAUGE;
	} else if  (gauge_ic->batt_nfg1000a) {
		buffer[17] = NFG_GAUGE;
	} else if ((DEVICE_BQ27411 == gauge_ic->device_type) || (DEVICE_BQ27541 == gauge_ic->device_type) ||
		   (true == gauge_ic->batt_bq28z610) || gauge_ic->batt_bq27z561 || (DEVICE_BQ27426== gauge_ic->device_type)) {
		buffer[17] = TI_GAUGE;
	} else {
		buffer[17] = UNKNOWN_GAUGE_TYPE;
	}

	if (sub_gauge_ic) {
		buffer[18] = DOUBLE_PARALLEL_WOUND_CELLS;
	} else if ((DEVICE_ZY0602 == gauge_ic->device_type) ||
		(DEVICE_BQ27411 == gauge_ic->device_type) || gauge_ic->batt_bq27z561 ||
		(DEVICE_BQ27426 == gauge_ic->device_type) || gauge_ic->batt_nfg1000a) {
		buffer[18] = SINGLE_CELL;
	} else {
		buffer[18] = DOUBLE_SERIES_WOUND_CELLS;
	}

	if (DOUBLE_SERIES_WOUND_CELLS == buffer[18]) {
		bq28z610_get_qmax_parameters(&batt_qmax_1, &batt_qmax_2, &batt_qmax_passed_q);
		bq28z610_get_dod0_parameters(&batt_dod0_1, &batt_dod0_2, &batt_dod0_passed_q);
		voltage_cell1 = bq28z610_get_firstcell_voltage();
		voltage_cell2 = bq28z610_get_secondcell_voltage();
		batt_current = bq27541_get_average_current();
		btemp = oplus_chg_match_temp_for_chging();
	} else if (SINGLE_CELL == buffer[18]) {
		if (DEVICE_ZY0602 == gauge_ic->device_type) {
			zy602_get_qmax_parameters(&batt_qmax_1);
			batt_qmax_2 = batt_qmax_1;
			batt_qmax_passed_q = 0;
			zy602_get_dod0_parameters(&batt_dod0_1);
			batt_dod0_2 = batt_dod0_1;
			batt_dod0_passed_q = zy602_get_battery_passed_q();
		}  else if (gauge_ic->batt_bq27z561) {
			bq27z561_get_qmax_parameters(gauge_ic, &batt_qmax_1);
			batt_qmax_2 = batt_qmax_1;
			batt_qmax_passed_q = 0;
			bq27z561_get_dod0_parameters(gauge_ic, &batt_dod0_1, &batt_dod0_passed_q);
			batt_dod0_2 = batt_dod0_1;
		} else if (gauge_ic->batt_nfg1000a) {
			nfg1000a_bcc_set_buffer(gauge_ic, buffer);
		} else {
			batt_qmax_1 = bq27541_get_battery_qm();
			batt_qmax_2 = batt_qmax_1;
			batt_qmax_passed_q = 0;
			batt_dod0_1 = bq27541_get_battery_do0();
			batt_dod0_2 = batt_dod0_1;
			batt_dod0_passed_q = 0;
		}
		voltage_cell1 = bq27541_get_battery_mvolts();
		voltage_cell2 = voltage_cell1;
		batt_current = bq27541_get_average_current();
		btemp = oplus_chg_match_temp_for_chging();
	} else if (DOUBLE_PARALLEL_WOUND_CELLS == buffer[18]) {
		if (DEVICE_ZY0602 == gauge_ic->device_type) {
			zy602_get_qmax_parameters(&batt_qmax_1);
			zy602_get_sub_qmax_parameters(&batt_qmax_2);
			batt_qmax_passed_q = 0;
			zy602_get_dod0_parameters(&batt_dod0_1);
			zy602_get_sub_dod0_parameters(&batt_dod0_2);
			batt_dod0_passed_q = 0;
		}  else if (gauge_ic->batt_bq27z561) {
			bq27z561_get_qmax_parameters(gauge_ic, &batt_qmax_1);
			bq27z561_get_qmax_parameters(sub_gauge_ic, &batt_qmax_2);
			batt_qmax_passed_q = 0;
			bq27z561_get_dod0_parameters(gauge_ic, &batt_dod0_1, &batt_dod0_passed_q);
			bq27z561_get_dod0_parameters(sub_gauge_ic, &batt_dod0_2, &batt_dod0_passed_q);
			batt_dod0_passed_q = 0;
		} else if (gauge_ic->batt_nfg1000a) {
			nfg1000a_bcc_set_buffer(gauge_ic, buffer);
			nfg1000a_sub_bcc_set_buffer(sub_gauge_ic, buffer);
		} else {
			batt_qmax_1 = bq27541_get_battery_qm();
			batt_qmax_2 = bq27541_get_sub_battery_qm();
			batt_qmax_passed_q = 0;
			batt_dod0_1 = bq27541_get_battery_do0();
			batt_dod0_2 = bq27541_get_sub_battery_do0();
			batt_dod0_passed_q = 0;
		}
		voltage_cell1 = bq27541_get_battery_mvolts();
		voltage_cell2 = bq27541_get_sub_battery_mvolts();
		batt_current = bq27541_get_average_current();
		batt_current2 = bq27541_get_sub_battery_average_current();
		btemp = oplus_chg_match_temp_for_chging();
	}

	if (gauge_ic->batt_zy0603) {
		batt_dod0_passed_q = 0;
		buffer[17] = 1;
	}

	if (!gauge_ic->batt_nfg1000a) {
		buffer[0] = batt_dod0_1;
		buffer[1] = batt_dod0_2;
		buffer[2] = batt_dod0_passed_q;
		buffer[3] = batt_qmax_1;
		buffer[4] = batt_qmax_2;
		buffer[5] = batt_qmax_passed_q;
		buffer[12] = soc_ext_1;
		buffer[13] = soc_ext_2;
	}

	buffer[6] = voltage_cell1;
	buffer[7] = btemp;
	buffer[8] = batt_current;
	buffer[9] = bcc_current_max;
	buffer[10] = bcc_current_min;
	buffer[11] = voltage_cell2;
	buffer[14] = atl_last_gear_current;
}

static int bq27z561_get_calib_time(
	struct chip_bq27541 *chip, int *dod_time, int *qmax_time)
{
	int i;
	int ret;
	int data_check;
	int try_count;
	u8 extend_data[14] = {0};
	int check_args[CALIB_TIME_CHECK_ARGS] = {0};
	static bool init_flag = false;
	struct gauge_track_info_reg extend[] = {
		{ BQ27Z561_SUBCMD_IT_STATUS2 , 12 },
		{ BQ27Z561_SUBCMD_IT_STATUS3 , 4 },
	};

	if (!chip || !dod_time || !qmax_time)
		return -1;

	for (i = 0; i < ARRAY_SIZE(extend); i++) {
		try_count = BQ27Z561_SUBCMD_TRY_COUNT;
try:
		mutex_lock(&chip->gauge_alt_manufacturer_access);
		ret = gauge_i2c_txsubcmd(chip, BQ27Z561_DATAFLASHBLOCK, extend[i].addr);
		if (ret < 0) {
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			return -1;
		}

		if (sizeof(extend_data) >= extend[i].len + 2) {
			usleep_range(1000, 1000);
			ret = gauge_read_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, (extend[i].len + 2), extend_data);
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			if (ret < 0)
				return -1;
			data_check = (extend_data[1] << 0x8) | extend_data[0];
			if (try_count-- > 0 && data_check != extend[i].addr) {
				pr_info("not match. add=0x%4x, count=%d, extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
					extend[i].addr, try_count, extend_data[0], extend_data[1]);
				usleep_range(2000, 2000);
				goto try;
			}
			if (!try_count)
				return -1;
			if (extend[i].addr == BQ27Z561_SUBCMD_IT_STATUS2) {
				check_args[0] = (extend_data[13] << 0x08) | extend_data[12];
			} else {
				check_args[1] = (extend_data[3] << 0x08) | extend_data[2];
				check_args[2] = (extend_data[5] << 0x08) | extend_data[4];
			}
			if (i < ARRAY_SIZE(extend) - 1)
				usleep_range(2000, 2000);
		} else {
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
		}
	}

	if (!init_flag || check_args[0] != chip->calib_check_args_pre[0])
		chip->dod_time = 0;
	else
		chip->dod_time++;

	if (!init_flag || check_args[1] != chip->calib_check_args_pre[1] ||
		check_args[2] != chip->calib_check_args_pre[2])
		chip->qmax_time = 0;
	else
		chip->qmax_time++;

	init_flag = true;
	memcpy(chip->calib_check_args_pre, check_args, sizeof(check_args));
	*dod_time = chip->dod_time;
	*qmax_time = chip->qmax_time;

	return ret;
}

static int bq27z561_get_qmax_parameters(struct chip_bq27541 *chip, int * qmax)
{
	int ret;
	int data_check;
	int try_count = BQ27Z561_SUBCMD_TRY_COUNT;
	u8 extend_data[4] = {0};
	struct gauge_track_info_reg extend = { BQ27Z561_SUBCMD_IT_STATUS3 , 2 };

	if (!chip || !qmax)
		return -1;
try:
	mutex_lock(&chip->gauge_alt_manufacturer_access);
	ret = gauge_i2c_txsubcmd(chip, BQ27Z561_DATAFLASHBLOCK, extend.addr);
	if (ret < 0) {
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		return ret;
	}
	usleep_range(1000, 1000);
	ret = gauge_read_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, (extend.len + 2), extend_data);
	if (ret < 0) {
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		return ret;
	}

	data_check = (extend_data[1] << 0x8) | extend_data[0];
	if (try_count-- > 0 && data_check != extend.addr) {
		pr_err("0x%4x not match. try_count=%d, extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
		extend.addr, try_count, extend_data[0], extend_data[1]);
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		usleep_range(2000, 2000);
		goto try;
	}
	if (!try_count) {
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		return -1;
	}
	*qmax = (extend_data[3] << 0x08) | extend_data[2];
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	pr_info("bq27z561_get_qmax_parameters qmax:%d\n", *qmax);

	return 0;
}

static int bq27z561_get_dod0_parameters(
	struct chip_bq27541 *chip, int *batt_dod0, int *batt_dod0_passed_q)
{
	int ret;
	int data_check;
	int try_count = BQ27Z561_SUBCMD_TRY_COUNT;
	u8 extend_data[16] = {0};
	struct gauge_track_info_reg extend = { BQ27Z561_SUBCMD_IT_STATUS2 , 14 };

	if (!chip || !batt_dod0 || !batt_dod0_passed_q)
		return -1;

try:
	mutex_lock(&chip->gauge_alt_manufacturer_access);
	ret = gauge_i2c_txsubcmd(chip, BQ27Z561_DATAFLASHBLOCK, extend.addr);
	if (ret < 0)
		goto error;
	usleep_range(1000, 1000);
	ret = gauge_read_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, (extend.len + 2), extend_data);
	if (ret < 0)
		goto error;

	data_check = (extend_data[1] << 0x8) | extend_data[0];
	if (try_count-- > 0 && data_check != extend.addr) {
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		pr_err("not match. extend_data[0]=0x%2x, extend_data[1]=0x%2x\n", extend_data[0], extend_data[1]);
		usleep_range(2000, 2000);
		goto try;
	}
	if (!try_count)
		goto error;
	*batt_dod0_passed_q = (extend_data[15] << 0x08) | extend_data[14];
	if (*batt_dod0_passed_q & 0x8000)
		*batt_dod0_passed_q = -((~(*batt_dod0_passed_q - 1)) & 0xFFFF);
	*batt_dod0 = (extend_data[13] << 0x08) | extend_data[12];
	pr_info("bq27z561_get_dod0_parameters dod0:%d, dod0_passed_q:%d\n", *batt_dod0, *batt_dod0_passed_q);

	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return 0;
error:
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return ret;
}

static int bq27426_get_qmax_parameters(int * qmax)
{
	int ret = 0;

	if (!gauge_ic) {
		return 0;
	}
	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		return gauge_ic->batt_vol_pre;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = gauge_read_i2c(gauge_ic, BQ27426_QMAX_16, qmax);
		if (ret) {
			chg_err("error reading qmax, ret:%d\n", ret);
			return ret;
		}
		chg_err(" reading qmax, qmax:%d\n", *qmax);
	}
	return 0;
}

bool bqfs_get_init_status(void)
{
	struct chip_bq27541 *chip = gauge_ic;

	if (!chip)
		return false;

	return chip->bqfs_info.bqfs_status;
}

int bqfs_fw_check(void)
{
	struct chip_bq27541 *chip = gauge_ic;
	int rc = 0;

	if (!chip)
		return false;
	if (chip->device_type == DEVICE_BQ27426)
		rc = bqfs_fw_upgrade(chip, false);

	return rc;
}

int bqfs_data_check(void)
{
	struct chip_bq27541 *chip = gauge_ic;

	if (!chip)
		return false;
	if (chip->device_type == DEVICE_BQ27426)
		oplus_bqfs_data_check(chip);

	return true;
}

int bq27541_get_batt_manu_date_by_alt_manufacturer_access(void)
{
	int ret = 0;
	int data_check = 0;
	int try_count = BQ27Z561_SUBCMD_TRY_COUNT;
	u8 extend_data[BQ27Z561_SUBCMD_MANU_DATE_READ_BUF_LEN + 2] = {0};
	struct gauge_track_info_reg extend =
		{ BQ27Z561_SUBCMD_MANU_DATE, BQ27Z561_SUBCMD_MANU_DATE_READ_BUF_LEN };

	if (!gauge_ic)
		return -EINVAL;

	if (!gauge_ic->batt_bq27z561)
		return -EINVAL;

try:
	mutex_lock(&gauge_ic->gauge_alt_manufacturer_access);
	ret = gauge_i2c_txsubcmd(gauge_ic, BQ27Z561_DATAFLASHBLOCK, extend.addr);
	if (ret < 0)
		goto error;
	usleep_range(1000, 1000);
	ret = gauge_read_i2c_block(gauge_ic, BQ27Z561_DATAFLASHBLOCK,
		(extend.len + 2), extend_data);
	if (ret < 0)
		goto error;

	data_check = (extend_data[1] << 8) | extend_data[0];
	if (try_count-- > 0 && data_check != extend.addr) {
		mutex_unlock(&gauge_ic->gauge_alt_manufacturer_access);
		chg_err("not match. extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
			extend_data[0], extend_data[1]);
		usleep_range(2000, 2000);
		goto try;
	}
	if (!try_count)
		goto error;

	gauge_ic->manu_date = (extend_data[3] << 8) | extend_data[2];
	pr_info("bq27541 get batt manu date by alt manufacturer access:0x%04x\n",
		gauge_ic->manu_date);

error:
	mutex_unlock(&gauge_ic->gauge_alt_manufacturer_access);
	return ret;
}

int bq27541_get_batt_manu_date(char *info, int len)
{
	int date_len = 0;

	if (!gauge_ic)
		return -EINVAL;

	if (!gauge_ic->batt_bq27z561)
		return -EINVAL;

	date_len = snprintf(info, len, "%d-%02d-%02d", (((gauge_ic->manu_date >> 9) & 0x7F) + 1980),
			(gauge_ic->manu_date >> 5) & 0xF, gauge_ic->manu_date & 0x1F);

	return date_len;
}

static int bq27541_get_batt_manu_info_by_alt_manufacturer_access(void)
{
	int ret = 0;
	u8 buf[BQ27Z561_BATT_VDM_DATA_READ_BUF_LEN] = {0x00};
	u8 ui_soh = 0x00;
	u16 ui_cycle_count = 0x00;
	u8 used_flag = 0x00;
	u16 date_temp = 0x00;
	u8 check_sum = BQ27Z561_BATTINFO_DEFAULT_CHECKSUM;
	int retry_id = 0;

	if (!gauge_ic)
		return -EINVAL;

	if (!gauge_ic->batt_bq27z561)
		return -EINVAL;

retry:
	while (retry_id < BQ27Z561_BATT_INFO_RETRY_MAX) {
		retry_id++;

		mutex_lock(&gauge_ic->gauge_alt_manufacturer_access);
		ret = gauge_i2c_txsubcmd(gauge_ic, BQ27Z561_DATAFLASHBLOCK,
			BQ27Z561_BATTINFO_VDMDATA_CMD);
		if (ret < 0)
			goto error;
		usleep_range(1000, 1000);
		ret = gauge_read_i2c_block(gauge_ic, BQ27Z561_DATAFLASHBLOCK,
			BQ27Z561_BATT_VDM_DATA_READ_BUF_LEN, buf);
		if (ret < 0)
			goto error;

		mutex_unlock(&gauge_ic->gauge_alt_manufacturer_access);

		if ((buf[0] == (BQ27Z561_BATTINFO_VDMDATA_CMD & 0xFF))
			&& (buf[1] == (BQ27Z561_BATTINFO_VDMDATA_CMD >> 8))) {
			date_temp = buf[BQ27Z561_BATT_FIRST_USAGE_DATE_L] |
				buf[BQ27Z561_BATT_FIRST_USAGE_DATE_H] << 8;
			ui_soh = buf[BQ27Z561_BATT_UI_SOH];
			ui_cycle_count = buf[BQ27Z561_BATT_UI_CYCLE_COUNT_L] |
				buf[BQ27Z561_BATT_UI_CYCLE_COUNT_H] << 8;
			used_flag = buf[BQ27Z561_BATT_USED_FLAG];
			break;
		}
	}

	if (retry_id < BQ27Z561_BATT_INFO_RETRY_MAX) {
		if (buf[BQ27Z561_BATT_FIRST_USAGE_DATE_CHECK] != BQ27Z561_BATTINFO_NO_CHECKSUM) {
			check_sum = (BQ27Z561_BATTINFO_DEFAULT_CHECKSUM -
				buf[BQ27Z561_BATT_FIRST_USAGE_DATE_L] -
				buf[BQ27Z561_BATT_FIRST_USAGE_DATE_H]) & 0xFF;
			if (check_sum != buf[BQ27Z561_BATT_FIRST_USAGE_DATE_CHECK]) {
				chg_err("fud_check fail calc[0x%02x] read[0x%02x], retry\n",
					check_sum, buf[BQ27Z561_BATT_FIRST_USAGE_DATE_CHECK]);
				goto retry;
			}
		}

		gauge_ic->first_usage_date = date_temp;

		if (buf[BQ27Z561_BATT_UI_SOH_CHECK] != BQ27Z561_BATTINFO_NO_CHECKSUM) {
			check_sum = (BQ27Z561_BATTINFO_DEFAULT_CHECKSUM -
				buf[BQ27Z561_BATT_UI_SOH]) & 0xFF;
			if (check_sum != buf[BQ27Z561_BATT_UI_SOH_CHECK]) {
				chg_err("uisoc_check fail calc[0x%02x] read[0x%02x], retry\n",
					check_sum, buf[BQ27Z561_BATT_UI_SOH_CHECK]);
				goto retry;
			}
		}

		gauge_ic->ui_soh = ui_soh;

		if (buf[BQ27Z561_BATT_UI_CYCLE_COUNT_CHECK] != BQ27Z561_BATTINFO_NO_CHECKSUM) {
			check_sum = (BQ27Z561_BATTINFO_DEFAULT_CHECKSUM -
				buf[BQ27Z561_BATT_UI_CYCLE_COUNT_L] -
				buf[BQ27Z561_BATT_UI_CYCLE_COUNT_H]) & 0xFF;
			if (check_sum != buf[BQ27Z561_BATT_UI_CYCLE_COUNT_CHECK]) {
				chg_err("uicc_check fail calc[0x%02x] read[0x%02x], retry.\n",
					check_sum, buf[BQ27Z561_BATT_UI_CYCLE_COUNT_CHECK]);
				goto retry;
			}
		}

		gauge_ic->ui_cycle_count = ui_cycle_count;

		if (buf[BQ27Z561_BATT_USED_FLAG_CHECK] != BQ27Z561_BATTINFO_NO_CHECKSUM) {
			check_sum = (BQ27Z561_BATTINFO_DEFAULT_CHECKSUM -
				buf[BQ27Z561_BATT_USED_FLAG]) & 0xFF;
			if (check_sum != buf[BQ27Z561_BATT_USED_FLAG_CHECK]) {
				chg_err("used_flag fail calc[0x%02x] read[0x%02x], retry.\n",
					check_sum, buf[BQ27Z561_BATT_USED_FLAG_CHECK]);
				goto retry;
			}
		}
		gauge_ic->used_flag = used_flag;
	}

	return 0;
error:
	mutex_unlock(&gauge_ic->gauge_alt_manufacturer_access);
	return ret;
}

static void oplus_get_manu_battinfo_work(struct work_struct *work)
{
	int ret = 0;

	if (!gauge_ic->batt_bq27z561)
		return;

	ret = bq27541_get_batt_manu_date_by_alt_manufacturer_access();
	if (ret < 0) {
		chg_err("oplus_bq27541_get_batt_manufact_info get manu-date failed\n");
	}
	ret = bq27541_get_batt_manu_info_by_alt_manufacturer_access();
	if (ret < 0) {
		chg_err("oplus_bq27541_get_batt_manufact_info get manu_info failed\n");
	}

	return;
}

static int bq27z561_sealed(void)
{
	struct chip_bq27541 *chip = gauge_ic;
	int rc = 0;
	u8 seal_op_st[2] = { 0x54, 0x00 };
	u8 read_buf[6] = { 0 };

	if (!chip)
		return -EINVAL;

	gauge_write_i2c_block(chip, 0x3E, 2, seal_op_st);
	usleep_range(50000, 50000);
	gauge_read_i2c_block(chip, 0x3E, 6, read_buf);

	if ((read_buf[3] & 0x01) == 0) {
		rc = 0;
		chg_info("bq27z561 is unseal\n");
	} else {
		rc = -1;
		chg_info("bq27z561 is seal\n");
	}
	return rc;
}

static bool bq27z561_deep_init(void)
{
	struct chip_bq27541 *chip = gauge_ic;
	int ret = 0;

	if (!chip) {
		return false;
	}

	ret = bq27z561_sealed();
	if (ret == 0) {
		chg_info("bq27z561 is already unseal\n");
		return true;
	}

	mutex_lock(&chip->gauge_alt_manufacturer_access);
	ret = bq27z561_unseal(chip, 0);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	usleep_range(10000, 10000);

	if (ret < 0) {
		chg_err("bq27z561 unseal failed\n");
		return false;
	}

	chg_err("bq27z561_deep_init\n");

	return true;
}

static void bq27z561_deep_deinit(void)
{
	struct chip_bq27541 *chip = gauge_ic;

	usleep_range(1000, 1000);
	mutex_lock(&chip->gauge_alt_manufacturer_access);
	bq27z561_seal(chip, 0);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	usleep_range(100000, 100000);

	chg_err("bq27z561_deep_deinit\n");
}

int bq27541_get_batt_first_usage_date(char *info, int len)
{
	int date_len = 0;

	if (!gauge_ic)
		return -EINVAL;

	if (!gauge_ic->batt_bq27z561)
		return -EINVAL;

	cancel_delayed_work_sync(&gauge_ic->get_manu_battinfo_work);
	schedule_delayed_work(&gauge_ic->get_manu_battinfo_work,
		round_jiffies_relative(msecs_to_jiffies(100)));

	date_len = snprintf(info, len, "%d-%02d-%02d",
		(((gauge_ic->first_usage_date >> 9) & 0x7F) + 1980),
		(gauge_ic->first_usage_date >> 5) & 0xF,
		gauge_ic->first_usage_date & 0x1F);

	return date_len;
}

int bq27541_set_batt_first_usage_date(const char *info)
{
	u16 first_usage_date = 0x0000;
	u8 check_sum = 0x00;
	u8 write_data[BQ27Z561_BATT_FIRST_USAGE_DATE_WLEN] =
		{BQ27Z561_SUBCMD_FIRST_USAGE_DATE_WADDR & 0xFF,
		BQ27Z561_SUBCMD_FIRST_USAGE_DATE_WADDR >> 8, 0x00, 0x00, 0x00};
	u8 check_data[2] = {BQ27Z561_BATTINFO_DEFAULT_CHECKSUM,
		BQ27Z561_BATT_FIRST_USAGE_DATE_WLEN + 2};
	int i = 0;
	int ret = 0;
	int year = 0;
	int month = 0;
	int day = 0;

	if (!gauge_ic)
		return -EINVAL;

	if (!gauge_ic->batt_bq27z561)
		return -EINVAL;

	if (!info)
		return -EINVAL;

	ret = sscanf(info, "%d-%d-%d", &year, &month, &day);
	if (ret <= 0) {
		return -EINVAL;
	}

	first_usage_date = (((year - 1980) & 0x7F) << 9) | ((month & 0xF) << 5) | (day & 0x1F);
	check_sum = 0xFF - ((first_usage_date >> 8) & 0xFF) - (first_usage_date & 0xFF);
	chg_info("first_usage_date:%d-%d-%d\n", year, month, day);
	chg_info("first_usage_date data=0x%04x\n", first_usage_date);

	write_data[2] = first_usage_date & 0xFF;
	write_data[3] = first_usage_date >> 8;
	check_sum = (0xFF - write_data[2] - write_data[3]) & 0xFF;
	write_data[4] = check_sum;

	if (!bq27z561_deep_init()) {
		chg_err("bq27z561 deep init failed\n");
		return -EINVAL;
	}
	ret = gauge_write_i2c_block(gauge_ic, BQ27Z561_DATAFLASHBLOCK,
		BQ27Z561_BATT_FIRST_USAGE_DATE_WLEN, write_data);
	if (ret < 0)
		return -EINVAL;
	usleep_range(1000, 1000);

	for (i = 0; i < BQ27Z561_BATT_FIRST_USAGE_DATE_WLEN; i++) {
		check_data[0] -= write_data[i];
	}
	ret = gauge_write_i2c_block(gauge_ic, BQ27Z561_AUTHENCHECKSUM, 2, check_data);
	if (ret < 0) {
		return -EINVAL;
	}

	if (!gauge_ic->bq27z561_seal_flag) {
		usleep_range(1000, 1000);
		bq27z561_deep_deinit();
	}
	return ret;
}

int bq27z561_get_bq27z561_seal_flag(void)
{
	if (!gauge_ic)
		return -EINVAL;

	if (!gauge_ic->batt_bq27z561)
		return -EINVAL;

	chg_err("bq27z561_get_bq27z561_seal_flag = %d\n", gauge_ic->bq27z561_seal_flag);
	return gauge_ic->bq27z561_seal_flag;
}

int bq27z561_set_bq27z561_seal_flag(int seal_flag)
{
	u8 bq27z561_seal_flag = seal_flag & 0xFF;

	gauge_ic->bq27z561_seal_flag = bq27z561_seal_flag;

	if (seal_flag) {
		if (!bq27z561_deep_init()) {
			chg_err("bq27z561 deep init failed\n");
			return -EINVAL;
		}
	} else {
		usleep_range(1000, 1000);
		bq27z561_deep_deinit();
	}

	chg_err("bq27z561_set_bq27z561_seal_flag = %d\n", gauge_ic->bq27z561_seal_flag);
	return 0;
}

int bq27541_get_batt_ui_cc(void)
{
	if (!gauge_ic)
		return -EINVAL;

	if (!gauge_ic->batt_bq27z561)
		return -EINVAL;

	cancel_delayed_work_sync(&gauge_ic->get_manu_battinfo_work);
	schedule_delayed_work(&gauge_ic->get_manu_battinfo_work,
		round_jiffies_relative(msecs_to_jiffies(100)));

	return gauge_ic->ui_cycle_count;
}

int bq27541_set_batt_ui_cc(int ui_cycle_count)
{
	u16 ui_cycle_count_data = ui_cycle_count & 0xFFFF;
	u8 check_sum = 0x00;
	u8 write_data[BQ27Z561_BATT_UI_CC_WLEN] = {BQ27Z561_SUBCMD_UI_CC_WADDR & 0xFF,
		BQ27Z561_SUBCMD_UI_CC_WADDR >> 8, 0x00, 0x00, 0x00};
	u8 check_data[2] = {BQ27Z561_BATTINFO_DEFAULT_CHECKSUM, BQ27Z561_BATT_UI_CC_WLEN + 2};
	int i = 0;
	int ret = 0;

	check_sum = (BQ27Z561_BATTINFO_DEFAULT_CHECKSUM - write_data[2] - write_data[3]) & 0xFF;
	chg_info("ui_cycle_count_data=0x%04x\n", ui_cycle_count_data);

	write_data[2] = ui_cycle_count_data & 0xFF;
	write_data[3] = ui_cycle_count_data >> 8;
	check_sum = (BQ27Z561_BATTINFO_DEFAULT_CHECKSUM - write_data[2] - write_data[3]) & 0xFF;
	write_data[4] = check_sum;

	if (!bq27z561_deep_init()) {
		return -EINVAL;
	}
	ret = gauge_write_i2c_block(gauge_ic, BQ27Z561_DATAFLASHBLOCK,
		BQ27Z561_BATT_UI_CC_WLEN, write_data);
	if (ret < 0) {
		return -EINVAL;
	}
	usleep_range(1000, 1000);

	for (i = 0; i < BQ27Z561_BATT_UI_CC_WLEN; i++) {
		check_data[0] -= write_data[i];
	}
	ret = gauge_write_i2c_block(gauge_ic, BQ27Z561_AUTHENCHECKSUM, 2, check_data);
	if (ret < 0) {
		return -EINVAL;
	}

	if (!gauge_ic->bq27z561_seal_flag) {
		usleep_range(1000, 1000);
		bq27z561_deep_deinit();
	}

	return ret;
}

int bq27541_get_batt_ui_soh(void)
{
	if (!gauge_ic)
		return -EINVAL;

	if (!gauge_ic->batt_bq27z561)
		return -EINVAL;

	cancel_delayed_work_sync(&gauge_ic->get_manu_battinfo_work);
	schedule_delayed_work(&gauge_ic->get_manu_battinfo_work,
		round_jiffies_relative(msecs_to_jiffies(100)));

	return gauge_ic->ui_soh;
}

int bq27541_set_batt_ui_soh(int ui_soh)
{
	u8 ui_soh_data = ui_soh & 0xFF;
	u8 check_sum = 0x00;
	u8 write_data[BQ27Z561_BATT_UI_SOH_WLEN] = {BQ27Z561_SUBCMD_UI_SOH_WADDR & 0xFF,
		BQ27Z561_SUBCMD_UI_SOH_WADDR >> 8, 0x00, 0x00};
	u8 check_data[2] = {BQ27Z561_BATTINFO_DEFAULT_CHECKSUM, BQ27Z561_BATT_UI_SOH_WLEN + 2};
	int i = 0;
	int ret = 0;

	chg_info("set_batt_ui_soh data=0x%04x\n", ui_soh_data);
	write_data[2] = ui_soh_data;
	check_sum = (BQ27Z561_BATTINFO_DEFAULT_CHECKSUM - write_data[2]) & 0xFF;
	write_data[3] = check_sum;

	if (!bq27z561_deep_init()) {
		return -EINVAL;
	}
	ret = gauge_write_i2c_block(gauge_ic, BQ27Z561_DATAFLASHBLOCK,
		BQ27Z561_BATT_UI_SOH_WLEN, write_data);
	if (ret < 0) {
		return -EINVAL;
	}
	usleep_range(1000, 1000);

	for (i = 0; i < BQ27Z561_BATT_UI_SOH_WLEN; i++) {
		check_data[0] -= write_data[i];
	}
	ret = gauge_write_i2c_block(gauge_ic, BQ27Z561_AUTHENCHECKSUM, 2, check_data);
	if (ret < 0) {
		return -EINVAL;
	}

	if (!gauge_ic->bq27z561_seal_flag) {
		usleep_range(1000, 1000);
		bq27z561_deep_deinit();
	}

	return ret;
}


int bq27541_get_batt_used_flag(void)
{
	if (!gauge_ic)
		return -EINVAL;

	if (!gauge_ic->batt_bq27z561)
		return -EINVAL;

	cancel_delayed_work_sync(&gauge_ic->get_manu_battinfo_work);
	schedule_delayed_work(&gauge_ic->get_manu_battinfo_work,
		round_jiffies_relative(msecs_to_jiffies(100)));

	return gauge_ic->used_flag;
}

int bq27541_set_batt_used_flag(int used_flag)
{
	u8 used_flag_data = used_flag & 0xFF;
	u8 check_sum = 0x00;
	u8 write_data[BQ27Z561_BATT_USED_FLAG_WLEN] = {BQ27Z561_SUBCMD_USED_FLAG_WADDR & 0xFF,
					BQ27Z561_SUBCMD_USED_FLAG_WADDR >> 8, 0x00, 0x00};
	u8 check_data[2] = {BQ27Z561_BATTINFO_DEFAULT_CHECKSUM, BQ27Z561_BATT_USED_FLAG_WLEN + 2};
	int i = 0;
	int ret = 0;

	chg_info("set_batt_used_flag data=0x%04x\n", used_flag_data);
	write_data[2] = used_flag_data;
	check_sum = (BQ27Z561_BATTINFO_DEFAULT_CHECKSUM - write_data[2]) & 0xFF;
	write_data[3] = check_sum;

	if (!bq27z561_deep_init()) {
		return -EINVAL;
	}
	ret = gauge_write_i2c_block(gauge_ic, BQ27Z561_DATAFLASHBLOCK,
		BQ27Z561_BATT_USED_FLAG_WLEN, write_data);
	if (ret < 0) {
		return -EINVAL;
	}
	usleep_range(1000, 1000);

	for (i = 0; i < BQ27Z561_BATT_USED_FLAG_WLEN; i++) {
		check_data[0] -= write_data[i];
	}
	ret = gauge_write_i2c_block(gauge_ic, BQ27Z561_AUTHENCHECKSUM, 2, check_data);
	if (ret < 0) {
		return -EINVAL;
	}

	if (!gauge_ic->bq27z561_seal_flag) {
		usleep_range(1000, 1000);
		bq27z561_deep_deinit();
	}

	return ret;
}

static int oplus_bq27541_get_batt_sn(struct chip_bq27541 *chip)
{
	int i;
	int ret;
	int retry = 0;
	u8 check_sum = 0xFF;
	u8 buf[BQ28Z610_BATT_SN_READ_BUF_LEN] = {0x00};
	u8 batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE] = {0x00};

	if (!chip) {
		chg_err("chip fg_ic is not ready.\n");
		return -ENODEV;
	}

	if (!gauge_ic->batt_bq27z561) {
		return 0;
	}

	if (atomic_read(&chip->suspended) == 1) {
		return 0;
	}

retry:
	while (retry < BQ28Z610_BATT_SN_RETRY_MAX) {
		retry++;
		ret = gauge_i2c_txsubcmd(chip, BQ28Z610_BATT_SN_EN_ADDR, BQ28Z610_BATT_SN_CMD);
		if (ret < 0) {
			continue;
		}
		usleep_range(1000, 1000);
		ret = gauge_read_i2c_block(chip, BQ28Z610_BATT_SN_EN_ADDR,
			BQ28Z610_BATT_SN_READ_BUF_LEN, buf);
		if (ret < 0) {
			continue;
		}

		if ((buf[0] == (BQ28Z610_BATT_SN_CMD & 0xFF)) &&
		    (buf[1] == (BQ28Z610_BATT_SN_CMD >> 8))) {
			memcpy(batt_sn, &buf[2], OPLUS_BATT_SERIAL_NUM_SIZE);
			break;
		}
		chg_err("read sn fail, retry(%d)\n", retry);
	}


	if (retry < BQ28Z610_BATT_SN_RETRY_MAX) {
		if (batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1] != BQ28Z610_BATT_SN_NO_CHECKSUM) {
			check_sum = 0xFF;
			for (i = 0; i < OPLUS_BATT_SERIAL_NUM_SIZE - 1; i++) {
				check_sum -= batt_sn[i];
			}
			if (check_sum != batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1]) {
				chg_err("check_sum fail calc[%d] read[%d], retry.\n",
					check_sum, batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1]);
				goto retry;
			}
		}
		/* replace checksum byte by end of string '\0' */
		batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1] = '\0';
		strlcpy(chip->battinfo.bat_serial_num, (char*)batt_sn, OPLUS_BATT_SERIAL_NUM_SIZE);
	} else {
		chg_err("get sn failed\n");
	}
	return 0;
}

int oplus_bq27541_get_battinfo_sn(char buf[], int len)
{
	struct chip_bq27541 *chip = gauge_ic;
	int bsnlen = 0;

	chg_info("BattSN:%s\n", chip->battinfo.bat_serial_num);
	bsnlen = strlcpy(buf, chip->battinfo.bat_serial_num, OPLUS_BATT_SERIAL_NUM_SIZE);

	return bsnlen;
}

static int gauge_get_qmax(int *qmax1, int *qmax2)
{
	SCC_CELL_TYPE type;
	int batt_qmax_passed_q;
	static int qmax1_pre = 0;
	static int qmax2_pre = 0;
	int retry = RETRY_CNT;

	if (!qmax1 || !qmax2 || !gauge_ic)
		return -1;

	if (!oplus_vooc_get_allow_reading() || atomic_read(&gauge_ic->suspended) == 1 ||
	    atomic_read(&gauge_ic->gauge_i2c_status)) {
		*qmax1 = qmax1_pre;
		*qmax2 = qmax2_pre;
		return 0;
	}

	if (sub_gauge_ic && (atomic_read(&sub_gauge_ic->suspended) == 1 ||
	    atomic_read(&sub_gauge_ic->gauge_i2c_status))) {
		*qmax1 = qmax1_pre;
		*qmax2 = qmax2_pre;
		return 0;
	}

	if (sub_gauge_ic)
		type = DOUBLE_PARALLEL_WOUND_CELLS;
	else if ((DEVICE_ZY0602 == gauge_ic->device_type) || (DEVICE_BQ27411 == gauge_ic->device_type) ||
		(DEVICE_BQ27426 == gauge_ic->device_type) || gauge_ic->batt_bq27z561 || gauge_ic->batt_nfg1000a)
		type = SINGLE_CELL;
	else
		type = DOUBLE_SERIES_WOUND_CELLS;

	do {
		if (DOUBLE_SERIES_WOUND_CELLS == type) {
			bq28z610_get_qmax_parameters(qmax1, qmax2, &batt_qmax_passed_q);
		} else if (SINGLE_CELL == type) {
			if (gauge_ic->batt_bq27z561) {
				bq27z561_get_qmax_parameters(gauge_ic, qmax1);
				*qmax2 = *qmax1;
			} else if (gauge_ic->batt_nfg1000a) {
				nfg1000a_get_qmax_parameters(gauge_ic, qmax1);
				*qmax2 = *qmax1;
			} else if (DEVICE_ZY0602 == gauge_ic->device_type) {
				zy602_get_qmax_parameters(qmax1);
				*qmax2 = *qmax1;
			} else if (DEVICE_BQ27426 == gauge_ic->device_type) {
				bq27426_get_qmax_parameters(qmax1);
				*qmax2 = *qmax1;
			} else {
				*qmax1 = bq27541_get_battery_qm();
				*qmax2 = *qmax1;
			}
		} else if (DOUBLE_PARALLEL_WOUND_CELLS == type) {
			if (DEVICE_ZY0602 == gauge_ic->device_type) {
				zy602_get_qmax_parameters(qmax1);
				zy602_get_sub_qmax_parameters(qmax2);
			} else if (gauge_ic->batt_bq27z561 && sub_gauge_ic->batt_bq27z561) {
				bq27z561_get_qmax_parameters(gauge_ic, qmax1);
				bq27z561_get_qmax_parameters(sub_gauge_ic, qmax2);
			} else if (gauge_ic->batt_nfg1000a && sub_gauge_ic->batt_nfg1000a) {
				nfg1000a_get_qmax_parameters(gauge_ic, qmax1);
				nfg1000a_get_qmax_parameters(sub_gauge_ic, qmax2);
			} else {
				*qmax1 = bq27541_get_battery_qm();
				*qmax2 = bq27541_get_sub_battery_qm();
			}
		}
		if (normal_range_judge(QMAX_MAX, QMAX_MIN, *qmax1) && normal_range_judge(QMAX_MAX, QMAX_MIN, *qmax2))
			break;
		else
			usleep_range(10000, 10000);
		chg_err("qmax out of range,retry=%d,qmax1=%d,qmax2=%d", retry, *qmax1, *qmax2);
	} while (retry--);

	qmax1_pre = *qmax1;
	qmax2_pre = *qmax2;

	return 0;
}

static int gauge_get_fcc(int *fcc1, int *fcc2)
{
	if (!fcc1 || !fcc2)
		return -1;

	*fcc1 = bq27541_get_battery_fcc();
	*fcc2 = bq27541_get_sub_gauge_battery_fcc();

	return 0;
}

static int gauge_get_cc(int *cc1, int *cc2)
{
	if (!cc1 || !cc2)
		return -1;

	*cc1 = bq27541_get_battery_cc();
	*cc2 = bq27541_get_sub_battery_cc();

	return 0;
}

static int gauge_get_soh(int *soh1, int *soh2)
{
	if (!soh1 || !soh2)
		return -1;

	*soh1 = bq27541_get_battery_soh();
	*soh2 = bq27541_get_sub_battery_soh();

	return 0;
}

static void bq28z610_bcc_pre_calibration(int *buffer)
{
	if (!gauge_ic) {
		return;
	}

	memcpy(gauge_ic->bcc_buf, buffer, BCC_PARMS_COUNT_LEN);
}

static int bq28z610_get_battery_bcc_parameters(char *buf)
{
	unsigned char *tmpbuf;
	int buffer[BCC_PARMS_COUNT] = { 0 };
	int len = 0;
	int i = 0;
	int idx = 0;

	tmpbuf = (unsigned char *)get_zeroed_page(GFP_KERNEL);

	bq28z610_bcc_set_buffer(buffer);

	if ((oplus_vooc_get_fastchg_ing() && oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC &&
	     !oplus_vooc_get_fastchg_dummy_started()) ||
	    oplus_pps_get_pps_fastchg_started() ||
	    oplus_ufcs_get_ufcs_fastchg_started() ||
	    (true == oplus_voocphy_get_fastchg_ing())) {
		buffer[15] = 1;
	} else {
		buffer[15] = 0;
	}

	if (buffer[9] == 0) {
		buffer[15] = 0;
	}

	buffer[16] = oplus_chg_get_bcc_curr_done_status();

	if (oplus_pps_get_pps_fastchg_started()) {
		if (oplus_pps_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	} else if (oplus_ufcs_get_ufcs_fastchg_started()) {
		if (oplus_ufcs_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	} else if (true == oplus_voocphy_get_fastchg_ing()) {
		if (oplus_voocphy_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	} else {
		if (oplus_vooc_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	}


	printk(KERN_ERR
	       "%s : ----dod0_1[%d], dod0_2[%d], dod0_passed_q[%d], qmax_1[%d], qmax_2[%d], qmax_passed_q[%d] \
		voltage_cell1[%d], temperature[%d], batt_current[%d], max_current[%d], min_current[%d], voltage_cell2[%d], \
		soc_ext_1[%d], soc_ext_2[%d], atl_last_geat_current[%d], charging_flag[%d], bcc_curr_done[%d], is_zy0603[%d], batt_type[%d], \
		bcc_judge buf:%d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n", __func__, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4],
		buffer[5], buffer[6], buffer[7], buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13],
		buffer[14], buffer[15], buffer[16], buffer[17], buffer[18], oplus_vooc_get_fastchg_ing(),
		(oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC), (!oplus_vooc_get_fastchg_dummy_started()),
		oplus_vooc_bcc_get_temp_range(), oplus_pps_get_pps_fastchg_started(), oplus_pps_bcc_get_temp_range(),
		oplus_ufcs_get_ufcs_fastchg_started(), oplus_ufcs_bcc_get_temp_range(),
		oplus_voocphy_get_fastchg_ing(), oplus_voocphy_bcc_get_temp_range());

	for (i = 0; i < BCC_PARMS_COUNT - 1; i++) {
		len = snprintf(tmpbuf, BCC_PAGE_SIZE - idx, "%d,", buffer[i]);
		memcpy(&buf[idx], tmpbuf, len);
		idx += len;
	}
	len = snprintf(tmpbuf, BCC_PAGE_SIZE - idx, "%d", buffer[i]);
	memcpy(&buf[idx], tmpbuf, len);

	free_page((unsigned long)tmpbuf);

#ifdef BCC_SET_DEBUG_PARMS
	if (bcc_debug_mode & BCC_Y_DEBUG) {
		memcpy(&buf[0], bcc_debug_buf, BCC_PAGE_SIZE);
		printk(KERN_ERR "%s bcc_debug_buf:%s\n", __func__, bcc_debug_buf);
		return 0;
	}
#endif

	printk(KERN_ERR "%s buf:%s\n", __func__, buf);

	return 0;
}

static int bq28z610_get_fastchg_battery_bcc_parameters(char *buf)
{
	int buffer[BCC_PARMS_COUNT] = { 0 };

	bq28z610_bcc_set_buffer(buffer);

	if ((oplus_vooc_get_fastchg_ing() && oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC &&
	     !oplus_vooc_get_fastchg_dummy_started()) ||
	    oplus_pps_get_pps_fastchg_started() ||
	    oplus_ufcs_get_ufcs_fastchg_started() ||
	    (true == oplus_voocphy_get_fastchg_ing())) {
		buffer[15] = 1;
	} else {
		buffer[15] = 0;
	}

	if (buffer[9] == 0) {
		buffer[15] = 0;
	}

	buffer[16] = oplus_chg_get_bcc_curr_done_status();

	if (oplus_pps_get_pps_fastchg_started()) {
		if (oplus_pps_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	} else if (oplus_ufcs_get_ufcs_fastchg_started()) {
		if (oplus_ufcs_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	} else if (true == oplus_voocphy_get_fastchg_ing()) {
		if (oplus_voocphy_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	} else {
		if (oplus_vooc_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	}

	printk(KERN_ERR
	       "%s : ----dod0_1[%d], dod0_2[%d], dod0_passed_q[%d], qmax_1[%d], qmax_2[%d], qmax_passed_q[%d] \
		voltage_cell1[%d], temperature[%d], batt_current[%d], max_current[%d], min_current[%d], voltage_cell2[%d], \
		soc_ext_1[%d], soc_ext_2[%d], atl_last_geat_current[%d], charging_flag[%d], bcc_curr_done[%d], is_zy0603[%d], batt_type[%d], \
		bcc_judge buf:%d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n", __func__, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4],
		buffer[5], buffer[6], buffer[7], buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13],
		buffer[14], buffer[15], buffer[16], buffer[17], buffer[18], oplus_vooc_get_fastchg_ing(),
		(oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC), (!oplus_vooc_get_fastchg_dummy_started()),
		oplus_vooc_bcc_get_temp_range(), oplus_pps_get_pps_fastchg_started(), oplus_pps_bcc_get_temp_range(),
		oplus_ufcs_get_ufcs_fastchg_started(), oplus_ufcs_bcc_get_temp_range(),
		oplus_voocphy_get_fastchg_ing(), oplus_voocphy_bcc_get_temp_range());

	bq28z610_bcc_pre_calibration(buffer);

	printk(KERN_ERR "%s buf:%s\n", __func__, buf);

	return 0;
}

static int bq28z610_get_prev_battery_bcc_parameters(char *buf)
{
	int i = 0;
	int idx = 0;
	unsigned char *tmpbuf;
	int len = 0;
	int buffer[BCC_PARMS_COUNT] = { 0 };

	memcpy(buffer, gauge_ic->bcc_buf, BCC_PARMS_COUNT_LEN);
	tmpbuf = (unsigned char *)get_zeroed_page(GFP_KERNEL);

	/*modify for update in real time*/
	if (oplus_is_pps_charging()) {
		buffer[9] = oplus_pps_get_bcc_max_curr();
		buffer[10] = oplus_pps_get_bcc_min_curr();
	} else if (oplus_is_ufcs_charging()) {
		buffer[9] = oplus_ufcs_get_bcc_max_curr();
		buffer[10] = oplus_ufcs_get_bcc_min_curr();
	} else if (true == oplus_is_voocphy_charging()) {
		buffer[9] = oplus_voocphy_get_bcc_max_curr();
		buffer[10] = oplus_voocphy_get_bcc_min_curr();
	} else {
		buffer[9] = oplus_vooc_check_bcc_max_curr();
		buffer[10] = oplus_vooc_check_bcc_min_curr();
	}

	if ((oplus_vooc_get_fastchg_ing() && oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC) ||
	    oplus_pps_get_pps_fastchg_started() ||
	    oplus_ufcs_get_ufcs_fastchg_started() ||
	    (true == oplus_voocphy_get_fastchg_ing())) {
		buffer[15] = 1;
	} else {
		buffer[15] = 0;
	}

	if (buffer[9] == 0) {
		buffer[15] = 0;
	}

	buffer[16] = oplus_chg_get_bcc_curr_done_status();

	if (oplus_pps_get_pps_fastchg_started()) {
		if (oplus_pps_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	} else if (oplus_ufcs_get_ufcs_fastchg_started()) {
		if (oplus_ufcs_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	} else if (true == oplus_voocphy_get_fastchg_ing()) {
		if (oplus_voocphy_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	} else {
		if (oplus_vooc_bcc_get_temp_range() == false) {
			buffer[9] = 0;
			buffer[10] = 0;
			buffer[14] = 0;
			buffer[15] = 0;
		}
	}

	chg_err(KERN_ERR "%s bcc_voocphy buf:%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", __func__, oplus_vooc_get_fastchg_ing(),
		(oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC),
		(!oplus_vooc_get_fastchg_dummy_started()), oplus_vooc_bcc_get_temp_range(),
		oplus_pps_get_pps_fastchg_started(), oplus_pps_bcc_get_temp_range(),
		oplus_ufcs_get_ufcs_fastchg_started(), oplus_ufcs_bcc_get_temp_range(),
		oplus_voocphy_get_fastchg_ing(), oplus_voocphy_bcc_get_temp_range());

	for (i = 0; i < BCC_PARMS_COUNT - 1; i++) {
		len = snprintf(tmpbuf, BCC_PAGE_SIZE - idx, "%d,", buffer[i]);
		memcpy(&buf[idx], tmpbuf, len);
		idx += len;
	}
	len = snprintf(tmpbuf, BCC_PAGE_SIZE - idx, "%d", buffer[i]);
	memcpy(&buf[idx], tmpbuf, len);

	free_page((unsigned long)tmpbuf);

#ifdef BCC_SET_DEBUG_PARMS
	if (bcc_debug_mode & BCC_Y_DEBUG) {
		memcpy(&buf[0], bcc_debug_buf, BCC_PAGE_SIZE);
		printk(KERN_ERR "%s bcc_debug_buf:%s\n", __func__, bcc_debug_buf);
		return 0;
	}
#endif

	printk(KERN_ERR "%s bcc_buf:%s\n", __func__, buf);

	return 0;
}

#define BUFFER_OFFSET_ADDRESS 8
static int bq28z610_set_bcc_debug_parameters(const char *buf)
{
	int ret = 0;
#ifdef BCC_SET_DEBUG_PARMS
	char temp_buf[10] = { 0 };
#endif

#ifdef BCC_SET_DEBUG_PARMS
	if (strlen(buf) > 0 && strlen(buf) <= BCC_PAGE_SIZE) {
		if (strncpy(temp_buf, buf, 7)) {
			printk(KERN_ERR "%s temp_buf:%s\n", __func__, temp_buf);
		}
		if (!strncmp(temp_buf, "Y_DEBUG", 7)) {
			bcc_debug_mode = BCC_Y_DEBUG;
			printk(KERN_ERR "%s BCC_Y_DEBUG:%d\n", __func__, bcc_debug_mode);
		} else {
			bcc_debug_mode = BCC_N_DEBUG;
			printk(KERN_ERR "%s BCC_N_DEBUG:%d\n", __func__, bcc_debug_mode);
		}
		if (strlen(buf) > BUFFER_OFFSET_ADDRESS) {
			strncpy(bcc_debug_buf, buf + BUFFER_OFFSET_ADDRESS, BCC_PAGE_SIZE - BUFFER_OFFSET_ADDRESS);
			printk(KERN_ERR "%s bcc_debug_buf:%s, temp_buf:%s\n", __func__, bcc_debug_buf, temp_buf);
		}
		return ret;
	}
#endif

	printk(KERN_ERR "%s buf:%s\n", __func__, buf);
	return ret;
}

static int gauge_reg_dump(void);
static int bq27541_set_allow_reading(int enable)
{
	if (gauge_ic)
		gauge_ic->allow_reading = enable;

	return 0;
}
static int bq27541_set_wlchg_started_status(bool wlchg_started_status)
{
	if (!gauge_ic) {
		return 0;
	} else {
		gauge_ic->wlchg_started = wlchg_started_status;
	}

	return 0;
}

static struct oplus_gauge_operations bq27541_gauge_ops = {
	.get_battery_mvolts = bq27541_get_battery_mvolts,
	.get_battery_fc = bq27541_get_battery_fc,
	.get_battery_qm = bq27541_get_battery_qm,
	.get_battery_pd = bq27541_get_battery_pd,
	.get_battery_rcu = bq27541_get_battery_rcu,
	.get_battery_rcf = bq27541_get_battery_rcf,
	.get_battery_fcu = bq27541_get_battery_fcu,
	.get_battery_fcf = bq27541_get_battery_fcf,
	.get_battery_sou = bq27541_get_battery_sou,
	.get_battery_do0 = bq27541_get_battery_do0,
	.get_battery_doe = bq27541_get_battery_doe,
	.get_battery_trm = bq27541_get_battery_trm,
	.get_battery_pc = bq27541_get_battery_pc,
	.get_battery_qs = bq27541_get_battery_qs,
	.get_battery_temperature = bq27541_get_battery_temperature,
	.is_battery_present = bq27541_is_battery_present,
	.get_batt_remaining_capacity = bq27541_get_batt_remaining_capacity,
	.get_battery_soc = bq27541_get_battery_soc,
	.get_average_current = bq27541_get_average_current,
	.get_battery_fcc = bq27541_get_battery_fcc,
	.get_prev_batt_fcc = bq27541_get_prev_batt_fcc,
	.get_battery_cc = bq27541_get_battery_cc,
	.get_battery_soh = bq27541_get_battery_soh,
	.get_battery_authenticate = bq27541_get_battery_authenticate,
	.get_battery_hmac = bq27541_get_battery_hmac,
	.set_battery_full = bq27541_set_battery_full,
	.get_prev_battery_mvolts = bq27541_get_prev_battery_mvolts,
	.get_prev_battery_temperature = bq27541_get_prev_battery_temperature,
	.get_prev_battery_soc = bq27541_get_prev_battery_soc,
	.get_prev_average_current = bq27541_get_prev_average_current,
	.get_prev_batt_remaining_capacity = bq27541_get_prev_batt_remaining_capacity,
	.get_battery_mvolts_2cell_max = bq27541_get_battery_mvolts_2cell_max,
	.get_battery_mvolts_2cell_min = bq27541_get_battery_mvolts_2cell_min,
	.get_prev_battery_mvolts_2cell_max = bq27541_get_prev_battery_mvolts_2cell_max,
	.get_prev_battery_mvolts_2cell_min = bq27541_get_prev_battery_mvolts_2cell_min,
	.update_battery_dod0 = bq28z610_modify_dod0,
	.update_soc_smooth_parameter = bq28z610_update_soc_smooth_parameter,
	.get_battery_cb_status = bq28z610_get_battery_balancing_status,
	.get_gauge_i2c_err = bq27541_get_gauge_i2c_err,
	.clear_gauge_i2c_err = bq27541_clear_gauge_i2c_err,
	.get_passdchg = bq27541_get_passedchg,
	.dump_register = gauge_reg_dump,
	.protect_check = zy0603_protect_check,
	.afi_update_done = zy0603_afi_update_done,
	.set_allow_reading = bq27541_set_allow_reading,
	.wlchg_started_status = bq27541_set_wlchg_started_status,
	.get_bcc_parameters = bq28z610_get_battery_bcc_parameters,
	.get_update_bcc_parameters = bq28z610_get_fastchg_battery_bcc_parameters,
	.set_bcc_parameters = bq28z610_set_bcc_debug_parameters,
	.get_prev_bcc_parameters = bq28z610_get_prev_battery_bcc_parameters,
	.check_rc_sfr = zy0603_check_rc_sfr,
	.soft_reset_rc_sfr = zy0603_reset_rc_sfr,
	.get_gauge_info = gauge_get_info,
	.get_batt_qmax = gauge_get_qmax,
	.get_batt_fcc = gauge_get_fcc,
	.get_batt_cc = gauge_get_cc,
	.get_batt_soh = gauge_get_soh,
	.get_calib_time = gauge_get_calib_time,
	.cal_model_check = zy0602_cal_model_check,
	.get_bqfs_status = bqfs_get_init_status,
	.bqfs_fw_check = bqfs_fw_check,
	.bqfs_data_check = bqfs_data_check,
	.get_batt_manu_date = bq27541_get_batt_manu_date,
	.get_batt_first_usage_date = bq27541_get_batt_first_usage_date,
	.set_batt_first_usage_date = bq27541_set_batt_first_usage_date,
	.get_seal_flag = bq27z561_get_bq27z561_seal_flag,
	.set_seal_flag = bq27z561_set_bq27z561_seal_flag,
	.get_batt_ui_cc = bq27541_get_batt_ui_cc,
	.set_batt_ui_cc = bq27541_set_batt_ui_cc,
	.get_batt_ui_soh = bq27541_get_batt_ui_soh,
	.set_batt_ui_soh = bq27541_set_batt_ui_soh,
	.get_batt_used_flag = bq27541_get_batt_used_flag,
	.set_batt_used_flag = bq27541_set_batt_used_flag,
	.get_battinfo_sn = oplus_bq27541_get_battinfo_sn,
};

static struct oplus_gauge_operations bq27541_sub_gauge_ops = {
	.get_battery_mvolts = bq27541_get_sub_battery_mvolts,
	.get_prev_battery_mvolts = bq27541_get_sub_prev_battery_mvolts,
	.get_battery_qm = bq27541_get_sub_battery_qm,
	.get_battery_do0 = bq27541_get_sub_battery_do0,
	.get_battery_temperature = bq27541_get_sub_battery_temperature,
	.get_battery_soc = bq27541_get_sub_battery_soc,
	.get_average_current = bq27541_get_sub_battery_average_current,
	.get_battery_fcc = bq27541_get_sub_gauge_battery_fcc,
	.get_battery_cc = bq27541_get_sub_battery_cc,
	.get_battery_soh = bq27541_get_sub_battery_soh,
	.get_prev_batt_fcc = bq27541_get_sub_gauge_prev_batt_fcc,
	.get_batt_remaining_capacity = bq27541_get_sub_gauge_batt_remaining_capacity,
	.get_prev_batt_remaining_capacity = bq27541_get_sub_gauge_prev_batt_remaining_capacity,
	.get_battery_authenticate = bq27541_get_sub_battery_authenticate,
	.update_soc_smooth_parameter = bq28z610_update_sub_soc_smooth_parameter,
	.get_gauge_info = sub_gauge_get_info,
	.cal_model_check = sub_zy0602_cal_model_check,
	/*	.get_battery_hmac = bq27541_get_battery_hmac,
	.set_battery_full = bq27541_set_battery_full,
	.get_gauge_i2c_err = bq27541_get_gauge_i2c_err,
	.clear_gauge_i2c_err = bq27541_clear_gauge_i2c_err,*/
};

static void gauge_set_cmd_addr(struct chip_bq27541 *chip, int device_type)
{
	if (device_type == DEVICE_BQ27541 || device_type == DEVICE_ZY0602) {
		chip->cmd_addr.reg_cntl = BQ27541_BQ27411_REG_CNTL;
		chip->cmd_addr.reg_temp = BQ27541_REG_TEMP;
		chip->cmd_addr.reg_volt = BQ27541_REG_VOLT;
		chip->cmd_addr.reg_flags = BQ27541_REG_FLAGS;
		chip->cmd_addr.reg_nac = BQ27541_REG_NAC;
		chip->cmd_addr.reg_fac = BQ27541_REG_FAC;
		chip->cmd_addr.reg_rm = BQ27541_REG_RM;
		chip->cmd_addr.reg_fcc = BQ27541_REG_FCC;
		chip->cmd_addr.reg_ai = BQ27541_REG_AI;
		chip->cmd_addr.reg_si = BQ27541_REG_SI;
		chip->cmd_addr.reg_mli = BQ27541_REG_MLI;
		chip->cmd_addr.reg_ap = BQ27541_REG_AP;
		chip->cmd_addr.reg_soc = BQ27541_REG_SOC;
		chip->cmd_addr.reg_inttemp = BQ27541_REG_INTTEMP;
		chip->cmd_addr.reg_soh = BQ27541_REG_SOH;
		chip->cmd_addr.flag_dsc = BQ27541_FLAG_DSC;
		chip->cmd_addr.flag_fc = BQ27541_FLAG_FC;
		chip->cmd_addr.cs_dlogen = BQ27541_CS_DLOGEN;
		chip->cmd_addr.cs_ss = BQ27541_CS_SS;
		chip->cmd_addr.reg_ar = BQ27541_REG_AR;
		chip->cmd_addr.reg_artte = BQ27541_REG_ARTTE;
		chip->cmd_addr.reg_tte = BQ27541_REG_TTE;
		chip->cmd_addr.reg_ttf = BQ27541_REG_TTF;
		chip->cmd_addr.reg_stte = BQ27541_REG_STTE;
		chip->cmd_addr.reg_mltte = BQ27541_REG_MLTTE;
		chip->cmd_addr.reg_ae = BQ27541_REG_AE;
		chip->cmd_addr.reg_ttecp = BQ27541_REG_TTECP;
		chip->cmd_addr.reg_cc = BQ27541_REG_CC;
		chip->cmd_addr.reg_nic = BQ27541_REG_NIC;
		chip->cmd_addr.reg_icr = BQ27541_REG_ICR;
		chip->cmd_addr.reg_logidx = BQ27541_REG_LOGIDX;
		chip->cmd_addr.reg_logbuf = BQ27541_REG_LOGBUF;
		chip->cmd_addr.reg_dod0 = BQ27541_REG_DOD0;
		chip->cmd_addr.subcmd_cntl_status = BQ27541_SUBCMD_CTNL_STATUS;
		chip->cmd_addr.subcmd_device_type = BQ27541_SUBCMD_DEVCIE_TYPE;
		chip->cmd_addr.subcmd_fw_ver = BQ27541_SUBCMD_FW_VER;
		chip->cmd_addr.subcmd_dm_code = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_prev_macw = BQ27541_SUBCMD_PREV_MACW;
		chip->cmd_addr.subcmd_chem_id = BQ27541_SUBCMD_CHEM_ID;
		chip->cmd_addr.subcmd_set_hib = BQ27541_SUBCMD_SET_HIB;
		chip->cmd_addr.subcmd_clr_hib = BQ27541_SUBCMD_CLR_HIB;
		chip->cmd_addr.subcmd_set_cfg = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_sealed = BQ27541_SUBCMD_SEALED;
		chip->cmd_addr.subcmd_reset = BQ27541_SUBCMD_RESET;
		chip->cmd_addr.subcmd_softreset = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_exit_cfg = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_enable_dlog = BQ27541_SUBCMD_ENABLE_DLOG;
		chip->cmd_addr.subcmd_disable_dlog = BQ27541_SUBCMD_DISABLE_DLOG;
		chip->cmd_addr.subcmd_enable_it = BQ27541_SUBCMD_ENABLE_IT;
		chip->cmd_addr.subcmd_disable_it = BQ27541_SUBCMD_DISABLE_IT;
		chip->cmd_addr.subcmd_hw_ver = BQ27541_SUBCMD_HW_VER;
		chip->cmd_addr.subcmd_df_csum = BQ27541_SUBCMD_DF_CSUM;
		chip->cmd_addr.subcmd_bd_offset = BQ27541_SUBCMD_BD_OFFSET;
		chip->cmd_addr.subcmd_int_offset = BQ27541_SUBCMD_INT_OFFSET;
		chip->cmd_addr.subcmd_cc_ver = BQ27541_SUBCMD_CC_VER;
		chip->cmd_addr.subcmd_ocv = BQ27541_SUBCMD_OCV;
		chip->cmd_addr.subcmd_bat_ins = BQ27541_SUBCMD_BAT_INS;
		chip->cmd_addr.subcmd_bat_rem = BQ27541_SUBCMD_BAT_REM;
		chip->cmd_addr.subcmd_set_slp = BQ27541_SUBCMD_SET_SLP;
		chip->cmd_addr.subcmd_clr_slp = BQ27541_SUBCMD_CLR_SLP;
		chip->cmd_addr.subcmd_fct_res = BQ27541_SUBCMD_FCT_RES;
		chip->cmd_addr.subcmd_cal_mode = BQ27541_SUBCMD_CAL_MODE;
	} else if (device_type == DEVICE_BQ27426) {
		chip->cmd_addr.reg_cntl = BQ27426_REG_CNTL;
		chip->cmd_addr.reg_temp = BQ27426_REG_TEMP;
		chip->cmd_addr.reg_volt = BQ27426_REG_VOLT;
		chip->cmd_addr.reg_flags = BQ27426_REG_FLAGS;
		chip->cmd_addr.reg_nac = BQ27426_REG_NAC;
		chip->cmd_addr.reg_fac = BQ27426_REG_FAC;
		chip->cmd_addr.reg_rm = BQ27426_REG_RM;
		chip->cmd_addr.reg_fcc = BQ27426_REG_FCC;
		chip->cmd_addr.reg_ai = BQ27426_REG_AI;
		chip->cmd_addr.reg_si = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_mli = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_ap = BQ27426_REG_AP;
		chip->cmd_addr.reg_soc = BQ27426_REG_SOC;
		chip->cmd_addr.reg_inttemp = BQ27426_REG_INTTEMP;
		chip->cmd_addr.reg_soh = BQ27426_REG_SOH;
		chip->cmd_addr.flag_dsc = BQ27426_FLAG_DSC;
		chip->cmd_addr.flag_fc = BQ27426_FLAG_FC;
		chip->cmd_addr.cs_dlogen = BQ27426_CMD_INVALID;
		chip->cmd_addr.cs_ss = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_ar = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_artte = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_tte = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_ttf = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_stte = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_mltte = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_ae = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_ttecp = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_cc = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_nic = BQ27426_REG_SOH;
		chip->cmd_addr.reg_icr = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_logidx = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_logbuf = BQ27426_CMD_INVALID;
		chip->cmd_addr.reg_dod0 = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_cntl_status = BQ27426_SUBCMD_CTNL_STATUS;
		chip->cmd_addr.subcmd_device_type = BQ27426_SUBCMD_DEVCIE_TYPE;
		chip->cmd_addr.subcmd_fw_ver = BQ27426_SUBCMD_FW_VER;
		chip->cmd_addr.subcmd_dm_code = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_prev_macw = BQ27426_SUBCMD_PREV_MACW;
		chip->cmd_addr.subcmd_chem_id = BQ27426_SUBCMD_CHEM_ID;
		chip->cmd_addr.subcmd_set_hib = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_clr_hib = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_set_cfg = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_sealed = BQ27426_SUBCMD_SEALED;
		chip->cmd_addr.subcmd_reset = BQ27426_SUBCMD_RESET;
		chip->cmd_addr.subcmd_softreset = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_exit_cfg = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_enable_dlog = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_disable_dlog = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_enable_it = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_disable_it = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_hw_ver = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_df_csum = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_bd_offset = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_int_offset = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_cc_ver = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_ocv = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_bat_ins = BQ27426_SUBCMD_BAT_INS;
		chip->cmd_addr.subcmd_bat_rem = BQ27426_SUBCMD_BAT_REM;
		chip->cmd_addr.subcmd_set_slp = BQ27426_SUBCMD_SET_SLP;
		chip->cmd_addr.subcmd_clr_slp = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_fct_res = BQ27426_CMD_INVALID;
		chip->cmd_addr.subcmd_cal_mode = BQ27426_CMD_INVALID;
	 } else { /*device_bq27411*/
		chip->cmd_addr.reg_cntl = BQ27411_REG_CNTL;
		chip->cmd_addr.reg_temp = BQ27411_REG_TEMP;
		chip->cmd_addr.reg_volt = BQ27411_REG_VOLT;
		chip->cmd_addr.reg_flags = BQ27411_REG_FLAGS;
		chip->cmd_addr.reg_nac = BQ27411_REG_NAC;
		chip->cmd_addr.reg_fac = BQ27411_REG_FAC;
		chip->cmd_addr.reg_rm = BQ27411_REG_RM;
		chip->cmd_addr.reg_fcc = BQ27411_REG_FCC;
		chip->cmd_addr.reg_ai = BQ27411_REG_AI;
		chip->cmd_addr.reg_si = BQ27411_REG_SI;
		chip->cmd_addr.reg_mli = BQ27411_REG_MLI;
		chip->cmd_addr.reg_ap = BQ27411_REG_AP;
		chip->cmd_addr.reg_soc = BQ27411_REG_SOC;
		chip->cmd_addr.reg_inttemp = BQ27411_REG_INTTEMP;
		chip->cmd_addr.reg_soh = BQ27411_REG_SOH;
		chip->cmd_addr.reg_fc = BQ27411_REG_FC;
		chip->cmd_addr.reg_qm = BQ27411_REG_QM;
		chip->cmd_addr.reg_pd = BQ27411_REG_PD;
		chip->cmd_addr.reg_rcu = BQ27411_REG_RCU;
		chip->cmd_addr.reg_rcf = BQ27411_REG_RCF;
		chip->cmd_addr.reg_fcu = BQ27411_REG_FCU;
		chip->cmd_addr.reg_fcf = BQ27411_REG_FCF;
		chip->cmd_addr.reg_sou = BQ27411_REG_SOU;
		chip->cmd_addr.reg_do0 = BQ27411_REG_DO0;
		chip->cmd_addr.reg_doe = BQ27411_REG_DOE;
		chip->cmd_addr.reg_trm = BQ27411_REG_TRM;
		chip->cmd_addr.reg_pc = BQ27411_REG_PC;
		chip->cmd_addr.reg_qs = BQ27411_REG_QS;
		chip->cmd_addr.flag_dsc = BQ27411_FLAG_DSC;
		chip->cmd_addr.flag_fc = BQ27411_FLAG_FC;
		chip->cmd_addr.cs_dlogen = BQ27411_CS_DLOGEN;
		chip->cmd_addr.cs_ss = BQ27411_CS_SS;
		/*bq27541 external standard cmds*/
		chip->cmd_addr.reg_ar = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_artte = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_tte = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_ttf = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_stte = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_mltte = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_ae = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_ttecp = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_cc = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_nic = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_icr = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_logidx = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_logbuf = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_dod0 = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_cntl_status = BQ27411_SUBCMD_CNTL_STATUS;
		chip->cmd_addr.subcmd_device_type = BQ27411_SUBCMD_DEVICE_TYPE;
		chip->cmd_addr.subcmd_fw_ver = BQ27411_SUBCMD_FW_VER;
		chip->cmd_addr.subcmd_dm_code = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_prev_macw = BQ27411_SUBCMD_PREV_MACW;
		chip->cmd_addr.subcmd_chem_id = BQ27411_SUBCMD_CHEM_ID;
		chip->cmd_addr.subcmd_set_hib = BQ27411_SUBCMD_SET_HIB;
		chip->cmd_addr.subcmd_clr_hib = BQ27411_SUBCMD_CLR_HIB;
		chip->cmd_addr.subcmd_set_cfg = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_sealed = BQ27411_SUBCMD_SEALED;
		chip->cmd_addr.subcmd_reset = BQ27411_SUBCMD_RESET;
		chip->cmd_addr.subcmd_softreset = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_exit_cfg = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_enable_dlog = BQ27411_SUBCMD_ENABLE_DLOG;
		chip->cmd_addr.subcmd_disable_dlog = BQ27411_SUBCMD_DISABLE_DLOG;
		chip->cmd_addr.subcmd_enable_it = BQ27411_SUBCMD_ENABLE_IT;
		chip->cmd_addr.subcmd_disable_it = BQ27411_SUBCMD_DISABLE_IT;
		/*bq27541 external sub cmds*/
		chip->cmd_addr.subcmd_hw_ver = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_df_csum = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_bd_offset = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_int_offset = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_cc_ver = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_ocv = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_bat_ins = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_bat_rem = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_set_slp = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_clr_slp = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_fct_res = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_cal_mode = BQ27541_BQ27411_CMD_INVALID;
	}
}

static void oplus_set_device_name(struct chip_bq27541 *chip, int device_type)
{
	if (device_type == DEVICE_TYPE_BQ27541)
		strncpy(chip->device_name, "bq27541", DEVICE_NAME_LEN);
	else if (device_type == DEVICE_TYPE_BQ27411)
		strncpy(chip->device_name, "bq27411", DEVICE_NAME_LEN);
	else if (device_type == DEVICE_TYPE_BQ28Z610)
		strncpy(chip->device_name, "bq28z610", DEVICE_NAME_LEN);
	else if (device_type == DEVICE_TYPE_ZY0602)
		strncpy(chip->device_name, "zy0602", DEVICE_NAME_LEN);
	else if (device_type == DEVICE_TYPE_ZY0603)
		strncpy(chip->device_name, "zy0603", DEVICE_NAME_LEN);
	else if (device_type == DEVICE_TYPE_BQ27426_0 ||
		device_type == DEVICE_TYPE_BQ27426_1 ||
		device_type == DEVICE_TYPE_BQ27426_2 ||
		device_type == DEVICE_TYPE_BQ27426_3 ||
		device_type == DEVICE_TYPE_BQ27426_4)
		strncpy(chip->device_name, "bq27426", DEVICE_NAME_LEN);
	else
		strncpy(chip->device_name, "other", DEVICE_NAME_LEN);

	if (chip->batt_bq27z561)
		strncpy(chip->device_name, "bq27z561", DEVICE_NAME_LEN);
	else if (chip->batt_nfg1000a)
		strncpy(chip->device_name, "nfg1000a", DEVICE_NAME_LEN);
}

static void oplus_set_device_type_by_extern_cmd(struct chip_bq27541 *chip)
{
	int ret;
	int device_type = 0;
	int data_check;
	int try_count = BQ27Z561_SUBCMD_TRY_COUNT;
	u8 extend_data[4] = {0};
	struct gauge_track_info_reg extend = { BQ27Z561_SUBCMD_DEVICE_TYPE, 2 };

	chip->device_type = DEVICE_BQ27541;
	chip->device_type_for_vooc = DEVICE_TYPE_FOR_VOOC_BQ27541;
try:
	ret = gauge_i2c_txsubcmd(chip, BQ27Z561_DATAFLASHBLOCK, extend.addr);
	if (ret < 0) {
		schedule_delayed_work(&chip->hw_config_retry_work, msecs_to_jiffies(3000));
		return;
	}

	msleep(5);
	ret = gauge_read_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, (extend.len + 2), extend_data);
	if (ret < 0) {
		schedule_delayed_work(&chip->hw_config_retry_work, msecs_to_jiffies(3000));
		return;
	}
	data_check = (extend_data[1] << 0x8) | extend_data[0];
	if (try_count-- > 0 && data_check != extend.addr) {
		pr_info("not match. extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
			extend_data[0], extend_data[1]);
		goto try;
	}
	if (try_count) {
		device_type = (extend_data[3] << 0x8) | extend_data[2];
		if (device_type == DEVICE_TYPE_NFG1000A)
			chip->batt_nfg1000a = true;
		else if (device_type == DEVICE_TYPE_BQ27Z561)
			chip->batt_bq27z561 = true;
	}
	pr_info("device_type : 0x%04x\n", device_type);
}

static void oplus_hw_config_retry_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_bq27541 *chip =
		container_of(dwork, struct chip_bq27541, hw_config_retry_work);

	oplus_set_device_type_by_extern_cmd(chip);
	chip->cmd_addr.reg_ai = BQ28Z610_REG_TI;
	oplus_set_device_name(chip, DEVICE_TYPE_BQ27541);
}

static int bq27541_hw_config(struct chip_bq27541 *chip)
{
	int ret = 0;
	int flags = 0;
	int device_type = 0;
	int fw_ver = 0;

#ifdef OPLUS_CUSTOM_OP_DEF
	exfg_information_register(&bq27541_gauge_ops);
#endif
	if (chip->gauge_fix_cadc) {
		ret = gauge_read_i2c(chip, ZY602_FW_CHECK_CMD, &flags);
		if (ret) {
			pr_err("error to read ipa mode\n");
		} else {
			if (flags == ZY602_FW_CHECK_ERROR) {
				device_type = DEVICE_TYPE_ZY0602;
				pr_err("i2c error,force the 0602 type\n");
				goto device_type_set;
			}
		}
		udelay(66);
	}
	if (chip->support_extern_cmd) {
		INIT_DELAYED_WORK(&chip->hw_config_retry_work, oplus_hw_config_retry_work);
		oplus_set_device_type_by_extern_cmd(chip);
		goto device_type_set;
	}
	bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	gauge_read_i2c(chip, BQ27541_BQ27411_REG_CNTL, &flags);
	udelay(66);
	ret = gauge_read_i2c(chip, BQ27541_BQ27411_REG_CNTL, &flags);
	if (ret > 0) {
		chip->device_type = DEVICE_BQ27541;
		pr_err(" error reading register %02x ret = %d\n", BQ27541_BQ27411_REG_CNTL, ret);
		return -ret;
	}
	udelay(66);
	bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_DEVICE_TYPE);
	udelay(66);
	ret = gauge_read_i2c(chip, BQ27541_BQ27411_REG_CNTL, &device_type);
	udelay(66);
	bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_FW_VER);
	udelay(66);
	ret |= gauge_read_i2c(chip, BQ27541_BQ27411_REG_CNTL, &fw_ver);
	if (ret > 0) {
		pr_err("error to read BQ27541_BQ27411_REG_CNTL\n");
		return -ret;
	}

device_type_set:
	if (device_type == DEVICE_TYPE_BQ27411) {
		chip->device_type = DEVICE_BQ27411;
		chip->device_type_for_vooc = DEVICE_TYPE_FOR_VOOC_BQ27411;
	} else if (device_type == DEVICE_TYPE_ZY0602) {
		chip->device_type = DEVICE_ZY0602;
		chip->device_type_for_vooc = DEVICE_TYPE_FOR_VOOC_BQ27541;
	} else if (device_type == DEVICE_TYPE_BQ27426_0 ||
		device_type == DEVICE_TYPE_BQ27426_1 ||
		device_type == DEVICE_TYPE_BQ27426_2 ||
		device_type == DEVICE_TYPE_BQ27426_3 ||
		device_type == DEVICE_TYPE_BQ27426_4) {
		chip->device_type = DEVICE_BQ27426;
		chip->device_type_for_vooc = DEVICE_TYPE_FOR_VOOC_BQ27541;
	} else {
		if (device_type == DEVICE_TYPE_ZY0603) {
			chip->batt_zy0603 = true;
		}
		chip->device_type = DEVICE_BQ27541;
		chip->device_type_for_vooc = DEVICE_TYPE_FOR_VOOC_BQ27541;
		if (!chip->batt_bq28z610) {
			bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
			udelay(66);
			bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_ENABLE_IT);
		}
	}
	gauge_set_cmd_addr(chip, chip->device_type);
	if (device_type == DEVICE_TYPE_BQ28Z610 ||
	    device_type == DEVICE_TYPE_ZY0603 || chip->batt_bq27z561 || chip->batt_nfg1000a) {
		chip->cmd_addr.reg_ai = BQ28Z610_REG_TI;
	}
	oplus_set_fg_device_type(chip->device_type);
	oplus_set_device_name(chip, device_type);
	dev_err(chip->dev, "DEVICE_TYPE is 0x%02X, FIRMWARE_VERSION is 0x%02X batt_zy0603 = %d\n", device_type, fw_ver,
		chip->batt_zy0603);
	return 0;
}

#define OPLUS_GAUGE_ABNORMAL_VBATT_MAX (4800)
#define OPLUS_GAUGE_ABNORMAL_VBATT_MIN (2000)
#define OPLUS_CP_ABNORMAL_FCC_MAX (5600)
#define OPLUS_CP_ABNORMAL_FCC_MIN (2500)
#define OPLUS_CP_ABNORMAL_SOH_MAX (100)
#define OPLUS_CP_ABNORMAL_SOH_MIN (70)
#define OPLUS_CP_ABNORMAL_QMAX_MAX (5600)
#define OPLUS_CP_ABNORMAL_QMAX_MIN (3500)
static void bq27541_parse_dt(struct chip_bq27541 *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;
	chip->support_extern_cmd = of_property_read_bool(node, "oplus,support_extern_cmd");
	if (chip->support_extern_cmd)
		chip->support_sha256_hmac = true;
	chip->modify_soc_smooth = of_property_read_bool(node, "qcom,modify-soc-smooth");
	chip->modify_soc_calibration = of_property_read_bool(node, "qcom,modify-soc-calibration");
	chip->batt_bq28z610 = of_property_read_bool(node, "qcom,batt_bq28z610");
	chip->bq28z610_need_balancing = of_property_read_bool(node, "qcom,bq28z610_need_balancing");
	chip->enable_sleep_mode = of_property_read_bool(node, "oplus,enable_sleep_mode");

	/* only for wite battery full param in guage dirver probe on 7250 platform */
	chip->battery_full_param = of_property_read_bool(node, "qcom,battery-full-param");
	chip->b_soft_reset_for_zy = of_property_read_bool(node, "zy,b_soft_reset_for_zy");

	/* for zy sh366002 current and model mix patch */
	chip->gauge_fix_cadc = of_property_read_bool(node, "zy,gauge-fix-cadc");
	chip->gauge_cal_board = of_property_read_bool(node, "zy,gauge-cal-board");
	chip->gauge_check_model = of_property_read_bool(node, "zy,gauge-check-model");
	chip->gauge_check_por = of_property_read_bool(node, "zy,gauge-check-por");

	rc = of_property_read_u32(node, "qcom,gauge_num", &chip->gauge_num);
	if (rc) {
		chip->gauge_num = 0;
	}
	rc = of_property_read_u32(node, "qcom,sha1_key_index", &chip->sha1_key_index);
	if (rc) {
		chip->sha1_key_index = 0;
	}

	rc = of_property_read_u32(node, "oplus,capacity-pct", &chip->capacity_pct);
	if (rc) {
		chip->capacity_pct = 0;
	}

	rc = of_property_read_u32(node, "oplus,gauge_abnormal_vbatt_max", &chip->gauge_abnormal_vbatt_max);
	if (rc < 0) {
		chip->gauge_abnormal_vbatt_max = OPLUS_GAUGE_ABNORMAL_VBATT_MAX;
	}
	rc = of_property_read_u32(node, "oplus,gauge_abnormal_vbatt_min", &chip->gauge_abnormal_vbatt_min);
	if (rc < 0) {
		chip->gauge_abnormal_vbatt_min = OPLUS_GAUGE_ABNORMAL_VBATT_MIN;
	}
	rc = of_property_read_u32(node, "oplus,cp_abnormal_fcc_max", &chip->cp_abnormal_fcc_max);
	if (rc < 0) {
		chip->cp_abnormal_fcc_max = OPLUS_CP_ABNORMAL_FCC_MAX;
	}
	rc = of_property_read_u32(node, "oplus,cp_abnormal_fcc_min", &chip->cp_abnormal_fcc_min);
	if (rc < 0) {
		chip->cp_abnormal_fcc_min = OPLUS_CP_ABNORMAL_FCC_MIN;
	}
	rc = of_property_read_u32(node, "oplus,cp_abnormal_soh_max", &chip->cp_abnormal_soh_max);
	if (rc < 0) {
		chip->cp_abnormal_soh_max = OPLUS_CP_ABNORMAL_SOH_MAX;
	}
	rc = of_property_read_u32(node, "oplus,cp_abnormal_soh_min", &chip->cp_abnormal_soh_min);
	if (rc < 0) {
		chip->cp_abnormal_soh_min = OPLUS_CP_ABNORMAL_SOH_MIN;
	}
	rc = of_property_read_u32(node, "oplus,cp_abnormal_qmax_max", &chip->cp_abnormal_qmax_max);
	if (rc < 0) {
		chip->cp_abnormal_qmax_max = OPLUS_CP_ABNORMAL_QMAX_MAX;
	}
	rc = of_property_read_u32(node, "oplus,cp_abnormal_qmax_min", &chip->cp_abnormal_qmax_min);
	if (rc < 0) {
		chip->cp_abnormal_qmax_min = OPLUS_CP_ABNORMAL_QMAX_MIN;
	}
	chg_info("bq27426_abnormal=[%d, %d, %d, %d, %d, %d, %d, %d]\n", chip->gauge_abnormal_vbatt_max, chip->gauge_abnormal_vbatt_min,
	chip->cp_abnormal_fcc_max, chip->cp_abnormal_fcc_min, chip->cp_abnormal_soh_max, chip->cp_abnormal_soh_min,
	chip->cp_abnormal_qmax_max, chip->cp_abnormal_qmax_min);
}

static int sealed(struct chip_bq27541 *chip)
{
	/*    return control_cmd_read(di, CONTROL_STATUS) & (1 << 13);*/
	int value = 0;
	int ret = 0;

	bq27541_cntl_cmd(chip, CONTROL_STATUS);
	/*    bq27541_cntl_cmd(di, CONTROL_STATUS);*/
	usleep_range(10000, 10000);
	ret = gauge_read_i2c(chip, CONTROL_STATUS, &value);
	if (ret)
		pr_err("error to read CONTROL_STATUS\n");

	/*    chg_debug(" REG_CNTL: 0x%x\n", value); */

	if (chip->device_type == DEVICE_BQ27541 || chip->device_type == DEVICE_ZY0602) {
		return value & BIT(14);
	} else if (chip->device_type == DEVICE_BQ27411) {
		return value & BIT(13);
	} else if (chip->device_type == DEVICE_BQ27426) {
		return value & BIT(13);
	} else {
		return 1;
	}
}

static int seal(struct chip_bq27541 *chip)
{
	int i = 0;

	if (sealed(chip)) {
		pr_err("bq27541/27411 sealed, return\n");
		return 1;
	}
	bq27541_cntl_cmd(chip, SEAL_SUBCMD);
	usleep_range(10000, 10000);
	for (i = 0; i < SEAL_POLLING_RETRY_LIMIT; i++) {
		if (sealed(chip)) {
			return 1;
		}
		usleep_range(10000, 10000);
	}
	oplus_chg_gauge_seal_unseal_fail(OPLUS_GAUGE_SEAL_FAIL);
	return 0;
}

static int unseal(struct chip_bq27541 *chip, u32 key)
{
	int i = 0;

	if (!sealed(chip)) {
		goto out;
	}
	if (chip->device_type == DEVICE_BQ27541 || chip->device_type == DEVICE_ZY0602) {
		/*bq27541_write(CONTROL_CMD, key & 0xFFFF, false, di);*/
		if (chip->batt_zy0603) {
			bq27541_cntl_cmd(chip, 0x5678);
			usleep_range(10000, 10000);
			/*bq27541_write(CONTROL_CMD, (key & 0xFFFF0000) >> 16, false, di);*/
			bq27541_cntl_cmd(chip, 0x1234);
			usleep_range(10000, 10000);
		} else {
			bq27541_cntl_cmd(chip, 0x1115);
			usleep_range(10000, 10000);
			/*bq27541_write(CONTROL_CMD, (key & 0xFFFF0000) >> 16, false, di);*/
			bq27541_cntl_cmd(chip, 0x1986);
			usleep_range(10000, 10000);
		}
	} else if (chip->device_type == DEVICE_BQ27411 || chip->device_type == DEVICE_BQ27426) {
		/*    bq27541_write(CONTROL_CMD, key & 0xFFFF, false, di);*/
		bq27541_cntl_cmd(chip, 0x8000);
		usleep_range(10000, 10000);
		/*    bq27541_write(CONTROL_CMD, (key & 0xFFFF0000) >> 16, false, di);*/
		bq27541_cntl_cmd(chip, 0x8000);
		usleep_range(10000, 10000);
	}
	bq27541_cntl_cmd(chip, 0xffff);
	usleep_range(10000, 10000);
	bq27541_cntl_cmd(chip, 0xffff);
	usleep_range(10000, 10000);

	while (i < SEAL_POLLING_RETRY_LIMIT) {
		i++;
		if (!sealed(chip)) {
			break;
		}
		usleep_range(10000, 10000);
	}

out:
	chg_debug("bq27541 : i=%d\n", i);

	if (i == SEAL_POLLING_RETRY_LIMIT) {
		pr_err("bq27541 failed\n");
		oplus_chg_gauge_seal_unseal_fail(OPLUS_GAUGE_UNSEAL_FAIL);
		return 0;
	} else {
		return 1;
	}
}

static int zy0602_unseal(struct chip_bq27541 *chip, u32 key)
{
	u8 CNTL1_VAL_2[2] = { 0x40, 0x00 };
	u8 read_buf[5] = { 0 };
	int retry = 2;
	int rc = 0;

	do {
		bq27541_cntl_cmd(chip, 0x1115);
		usleep_range(5000, 5000);
		/*    bq27541_write(CONTROL_CMD, (key & 0xFFFF0000) >> 16, false, di);*/
		bq27541_cntl_cmd(chip, 0x1986);
		usleep_range(5000, 5000);
		bq27541_cntl_cmd(chip, 0xffff);
		usleep_range(5000, 5000);
		bq27541_cntl_cmd(chip, 0xffff);
		usleep_range(10000, 10000);

		gauge_write_i2c_block(chip, 0x00, 2, CNTL1_VAL_2);
		pr_err("%s write {0x40,0x00} --> 0x00\n", __func__);
		usleep_range(2000, 2000);

		gauge_read_i2c_block(chip, 0x00, 2, read_buf);
		pr_err("%s 0x00 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);
		/*usleep_range(5000,5000);*/
		if (read_buf[0] == 0x62 && read_buf[1] == 0x01) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	pr_err("%s zy0602_unseal flag [%d]\n", __func__, rc);

	return rc;
}

static int zy0602_seal(struct chip_bq27541 *chip, u32 key)
{
	u8 CNTL1_VAL_2[2] = { 0x20, 0x00 };
	u8 CNTL1_VAL_3[2] = { 0x40, 0x00 };
	u8 read_buf[5] = { 0 };
	int retry = 2;
	int rc = 0;

	do {
		gauge_write_i2c_block(chip, 0x00, 2, CNTL1_VAL_2);
		pr_err("%s write {0x40,0x00} --> 0x00\n", __func__);
		usleep_range(200000, 200000);

		gauge_write_i2c_block(chip, 0x00, 2, CNTL1_VAL_3);
		pr_err("%s write {0x40,0x00} --> 0x00\n", __func__);
		usleep_range(2000, 2000);

		gauge_read_i2c_block(chip, 0x00, 2, read_buf);
		pr_err("%s 0x00 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);
		/*usleep_range(5000,5000);*/
		if (read_buf[0] == 0x00 && read_buf[1] == 0x00) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	pr_err("%s seal flag [%d]\n", __func__, rc);

	return rc;
}

static int zy0603_seal(struct chip_bq27541 *chip, u32 key)
{
	u8 seal_cmd[2] = { 0x30, 0x00 };
	u8 seal_op_st[2] = { 0x54, 0x00 };
	u8 seal_cmd2[2] = { 0x45, 0x00 };
	u8 read_buf[6] = { 0 };
	int retry = 2;
	int rc = 0;

	if (!chip)
		return -1;

	do {
		gauge_write_i2c_block(chip, 0x00, 2, seal_cmd2);
		pr_err("%s write {0x45,0x00} --> 0x00\n", __func__);
		usleep_range(100000, 100000);
		gauge_write_i2c_block(chip, 0x00, 2, seal_cmd);
		pr_err("%s write {0x30,0x00} --> 0x00\n", __func__);
		usleep_range(1000, 1000);

		gauge_write_i2c_block(chip, 0x3E, 2, seal_op_st);
		pr_err("%s write {0x54,0x00} --> 0x3E\n", __func__);
		usleep_range(1000, 1000);

		gauge_read_i2c_block(chip, 0x3E, 6, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0],
		       read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);
		/* usleep_range(5000,5000); */
		if ((read_buf[3] & 0x01) != 0) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	pr_err("%s zy0603_seal flag [%d]\n", __func__, rc);
	if (rc < 0) {
		oplus_chg_gauge_seal_unseal_fail(OPLUS_GAUGE_SEAL_FAIL);
	}

	return rc;
}

static int zy0603_unseal(struct chip_bq27541 *chip, u32 key)
{
	u8 seal_cmd_1[2] = { 0x78, 0x56 };
	u8 seal_cmd_2[2] = { 0x34, 0x12 };
	u8 seal_op_st[2] = { 0x54, 0x00 };
	u8 read_buf[6] = { 0 };
	int retry = 2;
	int rc = 0;

	if (!chip)
		return -1;

	do {
		gauge_write_i2c_block(chip, 0x00, 2, seal_cmd_1);
		pr_err("%s write {0x78,0x56} --> 0x00\n", __func__);
		usleep_range(1000, 1000);

		gauge_write_i2c_block(chip, 0x00, 2, seal_cmd_2);
		pr_err("%s write {0x34,0x12} --> 0x00\n", __func__);
		usleep_range(1000, 1000);

		gauge_write_i2c_block(chip, 0x3E, 2, seal_op_st);
		pr_err("%s write {0x54,0x00} --> 0x3E\n", __func__);
		usleep_range(1000, 1000);

		gauge_read_i2c_block(chip, 0x3E, 6, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0],
		       read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);
		/* usleep_range(5000,5000); */
		if ((read_buf[3] & 0x01) == 0) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	pr_err("%s zy0603_unseal flag [%d]\n", __func__, rc);
	if (rc < 0) {
		oplus_chg_gauge_seal_unseal_fail(OPLUS_GAUGE_UNSEAL_FAIL);
	}
	return rc;
}

static int bq27z561_seal(struct chip_bq27541 *chip, u32 key)
{
	u8 seal_cmd[2] = { 0x30, 0x00 };
	u8 seal_op_st[2] = { 0x54, 0x00 };
	u8 read_buf[4] = { 0 };
	int retry = 2;
	int rc = 0;

	if (!chip)
		return -EINVAL;

	do {
		gauge_write_i2c_block(chip, 0x3E, 2, seal_cmd);
		chg_err("%s write {0x30,0x00} --> 0x3E\n", __func__);
		usleep_range(1000, 1000);

		gauge_write_i2c_block(chip, 0x3E, 2, seal_op_st);
		chg_err("%s write {0x54,0x00} --> 0x3E\n", __func__);
		usleep_range(1000, 1000);

		gauge_read_i2c_block(chip, 0x3E, 4, read_buf);
		chg_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0],
		       read_buf[1], read_buf[2], read_buf[3]);
		if ((read_buf[3] & 0x01) != 0) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	chg_err("%s bq27z561_seal flag [%d]\n", __func__, rc);
	if (rc < 0) {
		oplus_chg_gauge_seal_unseal_fail(OPLUS_GAUGE_SEAL_FAIL);
	}

	return rc;
}

static int bq27z561_unseal(struct chip_bq27541 *chip, u32 key)
{
	u8 seal_cmd_1[2] = { 0x14, 0x04 };
	u8 seal_cmd_2[2] = { 0x72, 0x36 };
	u8 seal_op_st[2] = { 0x54, 0x00 };
	u8 read_buf[6] = { 0 };
	int retry = 5;
	int rc = 0;

	if (!chip)
		return -EINVAL;

	do {
		usleep_range(2000000, 2000000);
		gauge_write_i2c_block(chip, 0x00, 2, seal_cmd_1);
		chg_err("%s write {0x14,0x04} --> 0x00\n", __func__);
		usleep_range(1000000, 1000000);

		gauge_write_i2c_block(chip, 0x00, 2, seal_cmd_2);
		chg_err("%s write {0x72,0x36} --> 0x00\n", __func__);
		usleep_range(10000, 10000);

		gauge_write_i2c_block(chip, 0x3E, 2, seal_op_st);
		chg_err("%s write {0x54,0x00} --> 0x3E\n", __func__);
		usleep_range(50000, 50000);

		gauge_read_i2c_block(chip, 0x3E, 6, read_buf);
		chg_err("%s bq27z561_unseal 0x3E -->read [0x%02x][0x%02x]"
			" [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
			__func__, read_buf[0], read_buf[1],
			read_buf[2], read_buf[3], read_buf[4], read_buf[5]);
		if ((read_buf[3] & 0x01) == 0) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	chg_err("%s bq27z561_unseal flag [%d]\n", __func__, rc);
	if (rc < 0) {
		oplus_chg_gauge_seal_unseal_fail(OPLUS_GAUGE_UNSEAL_FAIL);
	}
	return rc;
}

static int bq27426_seal(struct chip_bq27541 *chip)
{
	u8 CNTL1_VAL_2[2] = { 0x20, 0x00 };
	int retry = 2;
	int rc = 0;
	int ret = 0;
	int value = 0;

	mutex_lock(&chip->gauge_alt_manufacturer_access);
	do {
		gauge_write_i2c_block(chip, 0x00, 2, CNTL1_VAL_2);
		pr_err("%s write {0x40,0x00} --> 0x00\n", __func__);
		usleep_range(200000, 200000);

		ret = gauge_read_i2c(chip, CONTROL_STATUS, &value);
		if (ret)
			pr_err("error to read CONTROL_STATUS\n");
		if (value & BIT(13)) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	pr_err("%s bq27426_seal flag [%d][0x%x]\n", __func__, rc, value);

	return rc;
}

static int zy0603_battery_current_check_cmd(struct chip_bq27541 *chip, bool enable)
{
	u8 current_check[2] = { 0x30, 0x05 };
	u8 op_st[3] = { 0x21, 0x47, 0x67 };
	u8 read_buf[6] = { 0 };
	int rc = 0;

	gauge_write_i2c_block(chip, 0x3E, 3, op_st);
	pr_err("%s write {0x21, 0x47, 0x67} --> 0x3E\n", __func__);
	usleep_range(5000, 5000);

	gauge_write_i2c_block(chip, 0x60, 2, current_check);
	pr_err("%s write {0x30, 0x05} --> 0x60\n", __func__);
	usleep_range(5000, 5000);

	gauge_read_i2c_block(chip, 0x3E, 3, read_buf);
	pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0], read_buf[1], read_buf[2]);

	return rc;
}

static int zy0603_battery_sleep_mode_cmd(struct chip_bq27541 *chip, bool enable)
{
	u8 sleep_cmd[2] = { 0x9A, 0x05 };
	u8 sleep_reg[2] = { 0x24, 0x41 };
	u8 op_st[3] = { 0x24, 0x41, 0x00 };
	u8 read_buf[6] = { 0 };
	int rc = 0;

	gauge_write_i2c_block(chip, 0x3E, 2, sleep_reg);
	pr_err("%s write {0x24, 0x41} --> 0x3E\n", __func__);
	usleep_range(5000, 5000);
	gauge_read_i2c_block(chip, 0x3E, 3, read_buf);
	pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0], read_buf[1], read_buf[2]);
	if (read_buf[2] == 0) {
		return -1;
	}

	gauge_write_i2c_block(chip, 0x3E, 3, op_st);
	pr_err("%s write {0x24, 0x41, 0x00} --> 0x3E\n", __func__);
	usleep_range(5000, 5000);

	gauge_write_i2c_block(chip, 0x60, 2, sleep_cmd);
	pr_err("%s write {0x9A, 0x05} --> 0x60\n", __func__);
	usleep_range(5000, 5000);

	gauge_read_i2c_block(chip, 0x3E, 3, read_buf);
	pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0], read_buf[1], read_buf[2]);

	return rc;
}

static int zy0603_battery_sleep_enable_cmd(struct chip_bq27541 *chip)
{
	u8 sleep_cmd[2] = { 0x97, 0x05 };
	u8 sleep_reg[2] = { 0x24, 0x41 };
	u8 op_st[3] = { 0x24, 0x41, 0x03 };
	u8 read_buf[6] = { 0 };
	int rc = 0;

	gauge_write_i2c_block(chip, 0x3E, 2, sleep_reg);
	pr_err("%s write {0x24, 0x41} --> 0x3E\n", __func__);
	usleep_range(5000, 5000);
	gauge_read_i2c_block(chip, 0x3E, 3, read_buf);
	pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0], read_buf[1], read_buf[2]);
	if (read_buf[2] == 0) {
		gauge_write_i2c_block(chip, 0x3E, 3, op_st);
		pr_err("%s write {0x24, 0x41, 0x03} --> 0x3E\n", __func__);
		usleep_range(5000, 5000);

		gauge_write_i2c_block(chip, 0x60, 2, sleep_cmd);
		pr_err("%s write {0x97, 0x05} --> 0x60\n", __func__);
		usleep_range(5000, 5000);

		gauge_read_i2c_block(chip, 0x3E, 3, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__, read_buf[0], read_buf[1], read_buf[2]);
	}

	return rc;
}
static int zy0603_write_data_cmd(struct chip_bq27541 *chip, bool enable)
{
	int rc = 0;
	int retry_cnt = 0;
	int error_flag = 0;

	rc = zy0603_unseal(chip, 0);
	if (rc == 0) {
		if (enable == false) {
			rc = zy0603_battery_sleep_mode_cmd(chip, enable);
			if (rc == 0) {
				zy0603_battery_current_check_cmd(chip, enable);
			}
		} else {
			zy0603_battery_sleep_enable_cmd(chip);
		}

		if ((chip->batt_zy0603) && (chip->afi_count > 0)) {
			do {
				error_flag = 0;
				if (GAUGE_OK != zy0603_disable_ssdf_and_pf_check(chip) ||
				    GAUGE_OK != zy0603_check_ssdf_pf_checksum(chip)) {
					retry_cnt++;
					error_flag = 1;
					zy0603_disable_ssdf_and_pf(chip);
				}
			} while (retry_cnt < 5 && error_flag == 1);
			if (gauge_ic && retry_cnt < 5) {
				gauge_ic->disabled = true;
				pr_err("%s v12 zy0603_disable_ssdf_and_pf_check ok\n", __func__);
			} else {
				pr_err("%s v12 zy0603_disable_ssdf_and_pf_check failed\n", __func__);
			}

			if (!zy0603_seal(chip, 0)) {
				zy0603_start_checksum_cal(chip);
			}
		} else {
			zy0603_seal(chip, 0);
		}
	}

	return rc;
}

static int bq27411_write_block_data_cmd(struct chip_bq27541 *chip, int block_id, u8 reg_addr, u8 new_value)
{
	int rc = 0;
	u8 old_value = 0, old_csum = 0, new_csum = 0;
	/*u8 new_csum_test = 0, csum_temp = 0;*/
	if(oplus_is_rf_ftm_mode()) {
		return 0;
	}

	usleep_range(1000, 1000);
	gauge_i2c_txsubcmd(chip, BQ27411_DATA_CLASS_ACCESS, block_id);
	usleep_range(10000, 10000);
	rc = bq27541_read_i2c_onebyte(chip, reg_addr, &old_value);
	if (rc) {
		pr_err("%s read reg_addr = 0x%x fail\n", __func__, reg_addr);
		return 1;
	}
	if (old_value == new_value) {
		return 0;
	}
	usleep_range(1000, 1000);
	rc = bq27541_read_i2c_onebyte(chip, BQ27411_CHECKSUM_ADDR, &old_csum);
	if (rc) {
		pr_err("%s read checksum fail\n", __func__);
		return 1;
	}
	usleep_range(1000, 1000);
	gauge_i2c_txsubcmd_onebyte(chip, reg_addr, new_value);
	usleep_range(1000, 1000);
	new_csum = (old_value + old_csum - new_value) & 0xff;
	/*
	csum_temp = (255 - old_csum - old_value) % 256;
	new_csum_test = 255 - ((csum_temp + new_value) % 256);
*/
	usleep_range(1000, 1000);
	gauge_i2c_txsubcmd_onebyte(chip, BQ27411_CHECKSUM_ADDR, new_csum);
	pr_err("bq27411 write blk_id = 0x%x, addr = 0x%x, old_val = 0x%x, new_val = 0x%x, old_csum = 0x%x, new_csum = 0x%x\n",
	       block_id, reg_addr, old_value, new_value, old_csum, new_csum);
	return 0;
}

static int bq27411_read_block_data_cmd(struct chip_bq27541 *chip, int block_id, u8 reg_addr)
{
	u8 value = 0;
	if(oplus_is_rf_ftm_mode()) {
		return 0;
	}

	usleep_range(1000, 1000);
	gauge_i2c_txsubcmd(chip, BQ27411_DATA_CLASS_ACCESS, block_id);
	usleep_range(10000, 10000);
	bq27541_read_i2c_onebyte(chip, reg_addr, &value);
	return value;
}

static int bq27411_enable_config_mode(struct chip_bq27541 *chip, bool enable)
{
	int config_mode = 0;
	int i = 0;
	int rc = 0;

	if (enable) { /*enter config mode*/
		usleep_range(1000, 1000);
		bq27541_cntl_cmd(chip, BQ27411_SUBCMD_SET_CFG);
		usleep_range(1000, 1000);
		for (i = 0; i < BQ27411_CONFIG_MODE_POLLING_LIMIT; i++) {
			rc = gauge_read_i2c(chip, BQ27411_SUBCMD_CONFIG_MODE, &config_mode);
			if (rc < 0) {
				pr_err("%s i2c read error\n", __func__);
				return 1;
			}
			if (config_mode & BIT(4)) {
				break;
			}
			msleep(50);
		}
	} else { /* exit config mode */
		usleep_range(1000, 1000);
		bq27541_cntl_cmd(chip, BQ27411_SUBCMD_EXIT_CFG);
		usleep_range(1000, 1000);
		for (i = 0; i < BQ27411_CONFIG_MODE_POLLING_LIMIT; i++) {
			rc = gauge_read_i2c(chip, BQ27411_SUBCMD_CONFIG_MODE, &config_mode);
			if (rc < 0) {
				pr_err("%s i2c read error\n", __func__);
				return 1;
			}
			if ((config_mode & BIT(4)) == 0) {
				break;
			}
			msleep(50);
		}
	}
	if (i == BQ27411_CONFIG_MODE_POLLING_LIMIT) {
		pr_err("%s fail config_mode = 0x%x, enable = %d\n", __func__, config_mode, enable);
		return 1;
	} else {
		pr_err("%s success i = %d, config_mode = 0x%x, enable = %d\n", __func__, i, config_mode, enable);
		return 0;
	}
}

static bool bq27411_check_soc_smooth_parameter(struct chip_bq27541 *chip, bool is_powerup)
{
	int value_read = 0;
	u8 dead_band_val = 0;
	u8 op_cfgb_val = 0;
	u8 dodat_val = 0;
	u8 rc = 0;

	return true; /*not check because it costs 5.5 seconds */

	msleep(4000);
	if (sealed(chip)) {
		if (!unseal(chip, BQ27411_UNSEAL_KEY)) {
			return false;
		} else {
			msleep(50);
		}
	}
	if (is_powerup) {
		dead_band_val = BQ27411_CC_DEAD_BAND_POWERUP_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_POWERUP_VALUE;
		dodat_val = BQ27411_DODATEOC_POWERUP_VALUE;
	} else { /*shutdown*/
		dead_band_val = BQ27411_CC_DEAD_BAND_SHUTDOWN_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_SHUTDOWN_VALUE;
		dodat_val = BQ27411_DODATEOC_SHUTDOWN_VALUE;
	}
	rc = bq27411_enable_config_mode(chip, true);
	if (rc) {
		pr_err("%s enable config mode fail\n", __func__);
		return false;
	}
	/*enable block data control */
	rc = gauge_i2c_txsubcmd_onebyte(chip, BQ27411_BLOCK_DATA_CONTROL, 0x00);
	if (rc) {
		pr_err("%s enable block data control fail\n", __func__);
		goto check_error;
	}
	usleep_range(5000, 5000);
	/*check cc-dead-band*/
	value_read = bq27411_read_block_data_cmd(chip, BQ27411_CC_DEAD_BAND_ID, BQ27411_CC_DEAD_BAND_ADDR);
	if (value_read != dead_band_val) {
		pr_err("%s cc_dead_band error, value_read = 0x%x\n", __func__, value_read);
		goto check_error;
	}
	/*check opconfigB*/
	value_read = bq27411_read_block_data_cmd(chip, BQ27411_OPCONFIGB_ID, BQ27411_OPCONFIGB_ADDR);
	if (value_read != op_cfgb_val) {
		pr_err("%s opconfigb error, value_read = 0x%x\n", __func__, value_read);
		goto check_error;
	}
	/*check dodateoc*/
	value_read = bq27411_read_block_data_cmd(chip, BQ27411_DODATEOC_ID, BQ27411_DODATEOC_ADDR);
	if (value_read != dodat_val) {
		pr_err("%s dodateoc error, value_read = 0x%x\n", __func__, value_read);
		goto check_error;
	}
	bq27411_enable_config_mode(chip, false);
	return true;

check_error:
	bq27411_enable_config_mode(chip, false);
	return false;
}

/* only for wite battery full param in guage dirver probe on 7250 platform */
static int bq27441_battery_full_param_write_cmd(struct chip_bq27541 *chip)
{
	u8 reg_data = 0, rc = 0;
	u8 CNTL1_VAL_1[2] = { 0x52, 0x00 };
	u8 CNTL1_VAL_2[2] = { 0x00, 0x00 };
	u8 CNTL1_VAL_3[2] = { 0x00, 0x00 };
	u8 CNTL1_VAL_4[2] = { 0x00, 0x00 };
	u8 CNTL1_VAL_5[2] = { 0x00, 0x00 };
	u8 CNTL1_VAL_6[2] = { 0x00, 0x00 };
	u8 CNTL1_VAL_7[2] = { 0x00, 0x00 };
	u8 CNTL1_VAL_8[2] = { 0x00, 0x00 };
	u8 CNTL1_VAL_9[2] = { 0x00, 0x00 };
	u8 read_buf[5] = { 0 };
	pr_err("%s begin\n", __func__);

	CNTL1_VAL_1[0] = 0x6C;
	CNTL1_VAL_1[1] = 0x00;
	gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
	usleep_range(15000, 15000);
	pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

	rc = gauge_read_i2c_block(chip, 0x40, 2, read_buf);
	pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);
	if (read_buf[0] == 0xFF && read_buf[1] == 0xBA) {
		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		usleep_range(15000, 15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0xFE;
		CNTL1_VAL_1[1] = 0xCF;
		gauge_write_i2c_block(chip, 0x40, 2, CNTL1_VAL_1);
		pr_err("%s 0x40 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		CNTL1_VAL_2[0] = 0x00;
		CNTL1_VAL_2[1] = 0x00;
		gauge_write_i2c_block(chip, 0x42, 2, CNTL1_VAL_2);
		pr_err("%s 0x42 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_2[0], CNTL1_VAL_2[1]);

		CNTL1_VAL_3[0] = 0xFE;
		CNTL1_VAL_3[1] = 0x03;
		gauge_write_i2c_block(chip, 0x44, 2, CNTL1_VAL_3);
		pr_err("%s 0x44 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_3[0], CNTL1_VAL_3[1]);

		CNTL1_VAL_4[0] = 0x2A;
		CNTL1_VAL_4[1] = 0xD9;
		gauge_write_i2c_block(chip, 0x46, 2, CNTL1_VAL_4);
		pr_err("%s 0x46 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_4[0], CNTL1_VAL_4[1]);

		CNTL1_VAL_5[0] = 0x08;
		CNTL1_VAL_5[1] = 0x90;
		gauge_write_i2c_block(chip, 0x48, 2, CNTL1_VAL_5);
		pr_err("%s 0x48 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_5[0], CNTL1_VAL_5[1]);

		CNTL1_VAL_6[0] = 0xDA;
		CNTL1_VAL_6[1] = 0xFA;
		gauge_write_i2c_block(chip, 0x4A, 2, CNTL1_VAL_6);
		pr_err("%s 0x4A -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_6[0], CNTL1_VAL_6[1]);

		CNTL1_VAL_7[0] = 0xD3;
		CNTL1_VAL_7[1] = 0xDE;
		gauge_write_i2c_block(chip, 0x4C, 2, CNTL1_VAL_7);
		pr_err("%s 0x4C -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_7[0], CNTL1_VAL_7[1]);

		CNTL1_VAL_8[0] = 0xC2;
		CNTL1_VAL_8[1] = 0x3D;
		gauge_write_i2c_block(chip, 0x4E, 2, CNTL1_VAL_8);
		pr_err("%s 0x4E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_8[0], CNTL1_VAL_8[1]);

		CNTL1_VAL_9[0] = 0x7F;
		CNTL1_VAL_9[1] = 0x00;
		gauge_write_i2c_block(chip, 0x50, 2, CNTL1_VAL_9);
		pr_err("%s 0x50 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_9[0], CNTL1_VAL_9[1]);

		gauge_i2c_txsubcmd_onebyte(
			chip, 0x60,
			(0xFFFF - (CNTL1_VAL_1[0] + CNTL1_VAL_1[1] + CNTL1_VAL_2[0] + CNTL1_VAL_2[1] + CNTL1_VAL_3[0] +
				   CNTL1_VAL_3[1] + CNTL1_VAL_4[0] + CNTL1_VAL_4[1] + CNTL1_VAL_5[0] + CNTL1_VAL_5[1] +
				   CNTL1_VAL_6[0] + CNTL1_VAL_6[1] + CNTL1_VAL_7[0] + CNTL1_VAL_7[1] + CNTL1_VAL_8[0] +
				   CNTL1_VAL_8[1] + CNTL1_VAL_9[0] + CNTL1_VAL_9[1])));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (0xFFFF - (CNTL1_VAL_1[0] + CNTL1_VAL_1[1] + CNTL1_VAL_2[0] + CNTL1_VAL_2[1] + CNTL1_VAL_3[0] +
				  CNTL1_VAL_3[1] + CNTL1_VAL_4[0] + CNTL1_VAL_4[1] + CNTL1_VAL_5[0] + CNTL1_VAL_5[1] +
				  CNTL1_VAL_6[0] + CNTL1_VAL_6[1] + CNTL1_VAL_7[0] + CNTL1_VAL_7[1] + CNTL1_VAL_8[0] +
				  CNTL1_VAL_8[1] + CNTL1_VAL_9[0] + CNTL1_VAL_9[1])));
		usleep_range(30000, 30000);

		CNTL1_VAL_1[0] = 0x52;
		CNTL1_VAL_1[1] = 0x00;
		gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000, 15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		rc = gauge_read_i2c_block(chip, 0x5B, 2, read_buf);
		pr_err("%s 0x5B -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);

		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		usleep_range(15000, 15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x00;
		CNTL1_VAL_1[1] = 0x8F;
		gauge_write_i2c_block(chip, 0x5B, 2, CNTL1_VAL_1);
		pr_err("%s 0x5B -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		gauge_i2c_txsubcmd_onebyte(chip, 0x60,
					     (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1]));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1]));
		usleep_range(15000, 15000);

		CNTL1_VAL_1[0] = 0x52;
		CNTL1_VAL_1[1] = 0x00;
		usleep_range(15000, 15000);
		gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000, 15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		rc = gauge_read_i2c_block(chip, 0x5D, 2, read_buf);
		pr_err("%s 0x5D -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);

		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		usleep_range(15000, 15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x10;
		CNTL1_VAL_1[1] = 0xEF;
		gauge_write_i2c_block(chip, 0x5D, 2, CNTL1_VAL_1);
		pr_err("%s 0x5D -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		gauge_i2c_txsubcmd_onebyte(chip, 0x60,
					     (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1]));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1]));
		usleep_range(15000, 15000);

		CNTL1_VAL_1[0] = 0x52;
		CNTL1_VAL_1[1] = 0x00;
		gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000, 15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		rc = gauge_read_i2c_block(chip, 0x56, 2, read_buf);
		pr_err("%s 0x56 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);
		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		usleep_range(15000, 15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x00;
		CNTL1_VAL_1[1] = 0x14;
		gauge_write_i2c_block(chip, 0x56, 2, CNTL1_VAL_1);
		pr_err("%s 0x56 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		gauge_i2c_txsubcmd_onebyte(chip, 0x60,
					     (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1]));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1]));
		usleep_range(15000, 15000);

		CNTL1_VAL_1[0] = 0x59;
		CNTL1_VAL_1[1] = 0x00;
		gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000, 15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		rc = gauge_read_i2c_block(chip, 0x40, 2, read_buf);
		pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);
		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		usleep_range(15000, 15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x00;
		CNTL1_VAL_1[1] = 0x4b;
		gauge_write_i2c_block(chip, 0x40, 2, CNTL1_VAL_1);
		pr_err("%s 0x40 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		gauge_i2c_txsubcmd_onebyte(chip, 0x60,
					     (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1]));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1]));
		usleep_range(15000, 15000);
	}

	CNTL1_VAL_1[0] = 0x59;
	CNTL1_VAL_1[1] = 0x00;
	gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
	usleep_range(15000, 15000);
	pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

	rc = gauge_read_i2c_block(chip, 0x40, 2, read_buf);
	pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);

	if (read_buf[0] != 0x00 || read_buf[1] != 0x4b) {
		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);
		usleep_range(15000, 15000);

		CNTL1_VAL_2[0] = 0x00;
		CNTL1_VAL_2[1] = 0x4B;
		gauge_write_i2c_block(chip, 0x40, 2, CNTL1_VAL_2);
		pr_err("%s 0x40 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_2[0], CNTL1_VAL_2[1]);

		gauge_i2c_txsubcmd_onebyte(chip, 0x60,
					     (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_2[0] - CNTL1_VAL_2[1]));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_2[0] - CNTL1_VAL_2[1]));
		usleep_range(30000, 30000);
		pr_err("%s end\n", __func__);
	}

	pr_err("%s end\n", __func__);
	return GAUGE_OK;
}

/*ZY gauge : disable  full sleep mode*/
static int bq27441_battery_full_sleep_mode_cmd(struct chip_bq27541 *chip, bool enable)
{
	u8 rc = 0;
	u8 CNTL1_VAL_1[10] = { 0xC1, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	u8 CNTL1_VAL_3[10] = { 0xC1, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	u8 CNTL1_VAL_2[2] = { 0xFF, 0x55 };
	u8 read_buf[5] = { 0 };

	pr_err("%s begin, enable[%d]\n", __func__, enable);
	rc = zy0602_unseal(chip, 0);
	if (rc) {
		pr_err("%s zy0602_unseal fail return\n", __func__);
		return -1;
	}
	gauge_i2c_txsubcmd_onebyte(chip, BQ27411_BLOCK_DATA_CONTROL, 0x00);
	usleep_range(2000, 2000);

	gauge_i2c_txsubcmd_onebyte(chip, 0x3E, 0x40);
	usleep_range(5000, 5000);
	pr_err("%s write 0x40 --> 0x3E\n", __func__);

	gauge_i2c_txsubcmd_onebyte(chip, 0x3F, 0x00);
	usleep_range(10000, 10000);
	pr_err("%s write 0x00 --> 0x3F\n", __func__);

	rc = gauge_read_i2c_block(chip, 0x40, 2, read_buf);
	pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);
	if (!enable) {
		if (read_buf[0] == 0xC1 && read_buf[1] == 0x11) {
			pr_err("%s do nothing\n", __func__);
		} else {
			gauge_write_i2c_block(chip, 0x40, 10, CNTL1_VAL_1);
			usleep_range(5000, 5000);
			pr_err("%s write {0xC1,0x11,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0} --> 0x40\n", __func__);

			gauge_i2c_txsubcmd_onebyte(chip, 0x60, 0x2D);
			usleep_range(5000, 5000);
			pr_err("%s write 0x2D --> 0x60\n", __func__);

			rc = gauge_read_i2c_block(chip, 0x40, 2, read_buf);
			pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);

			gauge_write_i2c_block(chip, 0x00, 2, CNTL1_VAL_2);
			pr_err("%s write {0xFF,0x55} --> 0x40\n", __func__);
			usleep_range(500000, 500000);
		}
	} else if (chip->enable_sleep_mode || opchg_get_shipmode_value() == 1) {
		if (read_buf[1] != 0x31) {
			gauge_write_i2c_block(chip, 0x40, 10, CNTL1_VAL_3);
			usleep_range(5000, 5000);
			pr_err("%s write {0xC1,0x31,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0} --> 0x40\n", __func__);

			gauge_i2c_txsubcmd_onebyte(chip, 0x60, 0x0D);
			usleep_range(5000, 5000);
			pr_err("%s write 0x0D --> 0x60\n", __func__);

			rc = gauge_read_i2c_block(chip, 0x40, 2, read_buf);
			pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);
			usleep_range(5000, 5000);

			gauge_write_i2c_block(chip, 0x00, 2, CNTL1_VAL_2);
			pr_err("%s write {0xFF,0x55} --> 0x40\n", __func__);
			usleep_range(500000, 500000);
		}
	}
	rc = zy0602_seal(chip, 0);
	if (rc) {
		pr_err("%s zy0602_seal fail return\n", __func__);
		return -1;
	}

	pr_err("%s end\n", __func__);
	return GAUGE_OK;
}

static int bq27411_write_soc_smooth_parameter_for_zy(struct chip_bq27541 *chip, bool is_powerup)
{
	bq27441_battery_full_sleep_mode_cmd(chip, !is_powerup);
	return 1;
}

static int bq27411_write_soc_smooth_parameter(struct chip_bq27541 *chip, bool is_powerup)
{
	int rc = 0;
	u8 dead_band_val = 0;
	u8 op_cfgb_val = 0;
	u8 dodat_val = 0;

	if (is_powerup) {
		dead_band_val = BQ27411_CC_DEAD_BAND_POWERUP_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_POWERUP_VALUE;
		dodat_val = BQ27411_DODATEOC_POWERUP_VALUE;
	} else { /*shutdown */
		dead_band_val = BQ27411_CC_DEAD_BAND_SHUTDOWN_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_SHUTDOWN_VALUE;
		dodat_val = BQ27411_DODATEOC_SHUTDOWN_VALUE;
	}

	/*enter config mode */
	rc = bq27411_enable_config_mode(chip, true);
	if (rc) {
		pr_err("%s enable config mode fail\n", __func__);
		return 1;
	}
	/*enable block data control */
	gauge_i2c_txsubcmd_onebyte(chip, BQ27411_BLOCK_DATA_CONTROL, 0x00);

	/* only for wite battery full param in guage dirver probe on 7250 platform */
	if (chip->battery_full_param) {
		usleep_range(5000, 5000);
		rc = bq27441_battery_full_param_write_cmd(chip);
		if (rc == BATT_FULL_ERROR)
			return BATT_FULL_ERROR;
	}

	usleep_range(5000, 5000);
	/* step1: update cc-dead-band */
	rc = bq27411_write_block_data_cmd(chip, BQ27411_CC_DEAD_BAND_ID, BQ27411_CC_DEAD_BAND_ADDR, dead_band_val);
	if (rc) {
		pr_err("%s cc_dead_band fail\n", __func__);
		goto exit_config_mode;
	}
	/* step2: update opconfigB */
	rc = bq27411_write_block_data_cmd(chip, BQ27411_OPCONFIGB_ID, BQ27411_OPCONFIGB_ADDR, op_cfgb_val);
	if (rc) {
		pr_err("%s opconfigB fail\n", __func__);
		goto exit_config_mode;
	}
	/* step3: update dodateoc */
	rc = bq27411_write_block_data_cmd(chip, BQ27411_DODATEOC_ID, BQ27411_DODATEOC_ADDR, dodat_val);
	if (rc) {
		pr_err("%s dodateoc fail\n", __func__);
		goto exit_config_mode;
	}
	bq27411_enable_config_mode(chip, false);
	return 0;

exit_config_mode:
	bq27411_enable_config_mode(chip, false);
	return 1;
}

static int zy0603_write_data_cmd(struct chip_bq27541 *chip, bool enable);
static int bq27411_modify_soc_smooth_parameter(struct chip_bq27541 *chip, bool is_powerup)
{
	int rc = 0;
	bool check_result = false;
	bool tried_again = false;
	int flags = 0;

	if (chip->batt_zy0603 && is_powerup == true) {
		pr_err("%s batt_zy0603 begin\n", __func__);
		zy0603_write_data_cmd(chip, false);
		zy0603_get_fw_version(chip, &gauge_ic->fg_soft_version);
		return GAUGE_OK;
	} else {
		if (chip->batt_bq28z610 && is_powerup == true
				&& gauge_i2c_err == 0 && oplus_is_rf_ftm_mode() == false) {
			pr_err("%s batt_bq28z610 powerup\n", __func__);
			/*power up disable sleep mode*/
			bq28z610_set_sleep_mode(chip, false);
			return GAUGE_OK;
		} else if (chip->device_type == DEVICE_BQ27426 && is_powerup == true
				&& gauge_i2c_err == 0 && oplus_is_rf_ftm_mode() == false && gauge_ic->enable_sleep_mode) {
			bq27426_modify_soc_smooth_parameter(gauge_ic, false);
			return GAUGE_OK;
		}
	}

	if (chip->device_type == DEVICE_ZY0602) {
		pr_err("%s  DEVICE_ZY0602 begin\n", __func__);
		pr_err("%s  device_type = %d enable_sleep_mode=%d\n", __func__,
			chip->device_type, chip->enable_sleep_mode);
		rc = gauge_read_i2c(chip, ZY602_FW_CHECK_CMD, &flags);
		if (rc) {
			pr_err("error to read ipa mode\n");
		} else {
			if (flags == ZY602_FW_CHECK_ERROR) {
				pr_err("on ipa mode,not need do sleep_mode\n");
				return GAUGE_OK;
			}
		}
		if (chip->enable_sleep_mode)
			bq27411_write_soc_smooth_parameter_for_zy(chip, false);
		else
			bq27411_write_soc_smooth_parameter_for_zy(chip, is_powerup);
		return GAUGE_OK;
	}

	if (chip->modify_soc_smooth == false || chip->device_type == DEVICE_BQ27541 || chip->device_type == DEVICE_BQ27426) {
		return GAUGE_ERROR;
	}
	pr_err("%s begin\n", __func__);
	if (sealed(chip)) {
		if (!unseal(chip, BQ27411_UNSEAL_KEY)) {
			return GAUGE_ERROR;
		} else {
			msleep(50);
		}
	}

write_parameter:
	rc = bq27411_write_soc_smooth_parameter(chip, is_powerup);
	if (rc == BATT_FULL_ERROR)
		return BATT_FULL_ERROR; /* only for wite battery full param in guage dirver probe on 7250 platform */
	if (rc && tried_again == false) {
		tried_again = true;
		goto write_parameter;
	} else {
		check_result = bq27411_check_soc_smooth_parameter(chip, is_powerup);
		if (check_result == false && tried_again == false) {
			tried_again = true;
			goto write_parameter;
		}
	}

	usleep_range(1000, 1000);
	if (sealed(chip) == 0) {
		usleep_range(1000, 1000);
		seal(chip);
	}
	pr_err("%s end\n", __func__);
	return GAUGE_OK;
}

static int bq8z610_sealed(struct chip_bq27541 *chip)
{
	int value = 0;
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = { 0, 0, 0, 0 };
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_SEAL_STATUS);
	usleep_range(10000, 10000);
	gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	pr_err("%s bq8z610_sealed CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x,\
		CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
	       __func__, CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2], CNTL1_VAL[3]);
	value = (CNTL1_VAL[3] & BQ28Z610_SEAL_BIT);
	if (value == BQ28Z610_SEAL_VALUE) {
		pr_err("bq8z610 sealed, value = %x return 1\n", value);
		return 1;
	} else {
		pr_err("bq8z610 sealed, value = %x return 0\n", value);
		return 0;
	}
}

static int bq8z610_seal(struct chip_bq27541 *chip)
{
	int i = 0;

	if (bq8z610_sealed(chip)) {
		pr_err("bq8z610 sealed, return\n");
		return 1;
	}
	gauge_i2c_txsubcmd(chip, 0, BQ28Z610_SEAL_SUBCMD);
	usleep_range(100000, 100000);
	for (i = 0; i < BQ28Z610_SEAL_POLLING_RETRY_LIMIT; i++) {
		if (bq8z610_sealed(chip)) {
			pr_err("bq8z610 sealed,used %d x100ms\n", i);
			return 1;
		}
		usleep_range(10000, 10000);
	}
	oplus_chg_gauge_seal_unseal_fail(OPLUS_GAUGE_SEAL_FAIL);
	return 0;
}

static int bq8z610_unseal(struct chip_bq27541 *chip)
{
	int i = 0;

	if (!bq8z610_sealed(chip)) {
		goto out;
	}
	gauge_i2c_txsubcmd(chip, 0, BQ28Z610_UNSEAL_SUBCMD1);
	usleep_range(10000, 10000);
	gauge_i2c_txsubcmd(chip, 0, BQ28Z610_UNSEAL_SUBCMD2);
	usleep_range(100000, 100000);
	while (i < BQ28Z610_SEAL_POLLING_RETRY_LIMIT) {
		i++;
		if (!bq8z610_sealed(chip)) {
			pr_err("bq8z610 unsealed,used %d x100ms\n", i);
			break;
		}
		usleep_range(10000, 10000);
	}

out:
	chg_debug("bq8z610 : i=%d\n", i);
	if (i == BQ28Z610_SEAL_POLLING_RETRY_LIMIT) {
		pr_err("bq8z610 unseal failed\n");
		oplus_chg_gauge_seal_unseal_fail(OPLUS_GAUGE_UNSEAL_FAIL);
		return 0;
	} else {
		return 1;
	}
}

static int bq28z610_write_flash_busy_wait_i2c_err(struct chip_bq27541 *chip)
{
	u8 I2C_VAL[BQ28Z610_REG_I2C_SIZE] = { 0, 0, 0 };
	u8 I2C_write1[BQ28Z610_REG_I2C_SIZE] = { 0x03, 0x46, 0xA0 };

	gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, &I2C_write1[0]);
	msleep(100);
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x0516);
	msleep(100);
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x4603); /* physical address is 0x4603 */
	msleep(100);
	gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, I2C_VAL);
	pr_err("%s I2C Configuration I2C_VAL[0] = %x,I2C_VAL[1] = %x,I2C_VAL[2] = %x\n", __func__, I2C_VAL[0],
	       I2C_VAL[1], I2C_VAL[2]);
	if (((I2C_VAL[2] << 16) | (I2C_VAL[1] << 8) | I2C_VAL[0]) != 0xA04603) {
		pr_err("%s To change I2C Configuration 0x20 -> 0xA0. ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s To change I2C Configuration 0x20 -> 0xA0. OK\n", __func__);
	}
	return 0;
}

int bq28z610_write_soc_smooth_parameter(struct chip_bq27541 *chip)
{
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = { 0, 0, 0, 0 };
	u8 CNTL1_write1[BQ28Z610_REG_CNTL1_SIZE] = { 0xF4, 0x46, 0xdC, 0x00 };
	u8 CNTL1_write2[BQ28Z610_REG_CNTL1_SIZE] = { 0x08, 0x47, 0x96, 0x00 }; /* 150ma */
	u8 CNTL1_write3[BQ28Z610_REG_CNTL1_SIZE] = { 0x0C, 0x47, 0x28, 0x00 };
	gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write1[0]);
	msleep(100);
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x06E9);
	msleep(100);
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x46F4);
	msleep(100);
	gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	pr_err("%s Charge Term Taper Current CNTL1_VAL[0] = %x,"
	       "CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
	       __func__, CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2], CNTL1_VAL[3]);
	if ((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x46F4) || (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x00DC)) {
		pr_err("%s Charge Term Taper Current 150mA (=0x0096) -> 220mA (=0x00DC). ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s Charge Term Taper Current  (=0x0096) -> 220mA (=0x00DC). OK\n", __func__);
	}
	gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write2[0]);
	msleep(100);
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x061A); /* 150ma */
	msleep(100);
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x4708);
	msleep(100);
	gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	pr_err("%s Dsg Current Threshold CNTL1_VAL[0] = %x,\
		CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
	       __func__, CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2], CNTL1_VAL[3]);
	if ((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x4708) || (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0096)) {
		pr_err("%s Dsg Current Threshold 40mA (0x0028) -> 150mA (0x0078) ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s Dsg Current Threshold 40mA (0x0028) -> 150mA (0x0078) OK\n", __func__);
	}
	gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write3[0]);
	msleep(100);
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x0684);
	msleep(100);
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x470C);
	msleep(100);
	gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	pr_err("%s Quit Current CNTL1_VAL[0] = %x,\
		CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
	       __func__, CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2], CNTL1_VAL[3]);
	if ((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x470C) || (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0028)) {
		pr_err("%s Quit Current 20mA (0x0014) -> 40mA (0x0028). ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s Quit Current 20mA (0x0014) -> 40mA (0x0028). OK\n", __func__);
	}
	return 0;
}

static int bq28z610_write_iterm_Taper_parameter(struct chip_bq27541 *chip)
{
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = { 0, 0, 0, 0 };
	u8 CNTL1_write1[BQ28Z610_REG_CNTL1_SIZE] = { 0xF4, 0x46, 0x96, 0x00 };
	gauge_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write1[0]);
	msleep(100);
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x062F);
	msleep(100);
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x46F4);
	msleep(100);
	gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	pr_err("%s Charge Term Taper Current CNTL1_VAL[0] = %x,\
		CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
	       __func__, CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2], CNTL1_VAL[3]);
	if ((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x46F4) || (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0096)) {
		pr_err("%s Charge Term Taper Current 220mA (=0x00DC) -> 150mA (=0x0096). ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s Charge Term Taper Current 220mA (=0x00DC) -> 150mA (=0x0096). OK\n", __func__);
	}
	return 0;
}

static void bq28z610_modify_soc_smooth_parameter(struct chip_bq27541 *chip)
{
	int rc = 0;
	bool tried_again = false;

	pr_err("%s begin\n", __func__);
	if (bq8z610_sealed(chip)) {
		if (!bq8z610_unseal(chip)) {
			return;
		} else {
			msleep(50);
		}
	}

write_parameter:
	rc = bq28z610_write_iterm_Taper_parameter(chip);
	rc = bq28z610_write_flash_busy_wait_i2c_err(chip);

	/* shipmode enable sleep mode */
	rc = bq28z610_set_sleep_mode_withoutunseal(chip, true);

	if (rc && tried_again == false) {
		tried_again = true;
		goto write_parameter;
	}
	usleep_range(1000, 1000);
	if (bq8z610_sealed(chip) == 0) {
		usleep_range(1000, 1000);
		bq8z610_seal(chip);
	}
	pr_err("%s end\n", __func__);
}

static int bq28z610_batt_full_zero_parameter_write_cmd(struct chip_bq27541 *chip)
{
	u8 rc = 0;
	u8 CNTL1_VAL_1[2] = { 0x00, 0x00 };
	u8 CNTL1_VAL_2[7] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	u8 read_buf[7] = { 0 };
	int retry_cnt = 0;

	pr_err("%s begin\n", __func__);
	/*############################################0x0C47#####################################################*/
	/*write 60ma*/
	pr_err("%s write 40ma --> 60ma 003C\n", __func__);

	CNTL1_VAL_1[0] = 0x0A;
	CNTL1_VAL_1[1] = 0x47;
	gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
	pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
	rc = gauge_read_i2c_block(chip, 0x3E, 6, read_buf); /*R*/
	pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0],
	       read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);
	if (!(read_buf[2] == 0x46 && read_buf[3] == 0x00 && read_buf[4] == 0x3C && read_buf[5] == 0x00)) {
		/*write 10s*/
		pr_err("%s write 0x0A45\n", __func__);
	recfg_pararm1:
		CNTL1_VAL_1[0] = 0x9A;
		CNTL1_VAL_1[1] = 0x45;
		gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = gauge_read_i2c_block(chip, 0x3E, 7, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x][0x%02x][0x%02x][0x%02x]\n", __func__,
		       read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5], read_buf[6]);

		CNTL1_VAL_2[0] = 0x9A;
		CNTL1_VAL_2[1] = 0x45;
		CNTL1_VAL_2[2] = 0x20;
		CNTL1_VAL_2[3] = 0x1C;
		CNTL1_VAL_2[4] = 0x20;
		CNTL1_VAL_2[5] = 0x1C;
		CNTL1_VAL_2[6] = 0x1E;
		gauge_write_i2c_block(chip, 0x3E, 7, CNTL1_VAL_2); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x] [0x%02x][0x%02x][0x%02x][0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_2[0], CNTL1_VAL_2[1], CNTL1_VAL_2[2], CNTL1_VAL_2[3], CNTL1_VAL_2[4], CNTL1_VAL_2[5],
		       CNTL1_VAL_2[6]);

		CNTL1_VAL_1[0] = 0xFF - CNTL1_VAL_2[0] - CNTL1_VAL_2[1] - CNTL1_VAL_2[2] - CNTL1_VAL_2[3] -
				 CNTL1_VAL_2[4] - CNTL1_VAL_2[5] - CNTL1_VAL_2[6];
		CNTL1_VAL_1[1] = 0x09;
		gauge_write_i2c_block(chip, 0x60, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x60 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		usleep_range(260000, 260000); /*15ms*/

		CNTL1_VAL_1[0] = 0x9A; /*READ 10S*/
		CNTL1_VAL_1[1] = 0x45;
		gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = gauge_read_i2c_block(chip, 0x3E, 7, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x][0x%02x][0x%02x][0x%02x]\n", __func__,
		       read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5], read_buf[6]);
		/*check param cfg*/
		if (!(read_buf[2] == CNTL1_VAL_2[2] && read_buf[3] == CNTL1_VAL_2[3] && read_buf[4] == CNTL1_VAL_2[4] &&
		      read_buf[5] == CNTL1_VAL_2[5] && read_buf[6] == CNTL1_VAL_2[6])) {
			retry_cnt++;
			if (retry_cnt >= 3) {
				goto param_cf_err;
			}
			pr_err("gauge err recfg_pararm1");
			goto recfg_pararm1;
		}
		retry_cnt = 0;
		usleep_range(5000, 5000);
		/*write 5846*/
		pr_err("%s write 0x5846\n", __func__);
	recfg_pararm2:
		CNTL1_VAL_1[0] = 0x58;
		CNTL1_VAL_1[1] = 0x46;
		gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = gauge_read_i2c_block(chip, 0x3E, 6, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0],
		       read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);

		CNTL1_VAL_2[0] = 0x58;
		CNTL1_VAL_2[1] = 0x46;
		CNTL1_VAL_2[2] = 0x33;
		CNTL1_VAL_2[3] = 0x00;
		CNTL1_VAL_2[4] = 0x27;
		CNTL1_VAL_2[5] = 0x00;
		gauge_write_i2c_block(chip, 0x3E, 6, CNTL1_VAL_2); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_2[0], CNTL1_VAL_2[1], CNTL1_VAL_2[2], CNTL1_VAL_2[3], CNTL1_VAL_2[4], CNTL1_VAL_2[5]);

		CNTL1_VAL_1[0] = 0xFF - CNTL1_VAL_2[0] - CNTL1_VAL_2[1] - CNTL1_VAL_2[2] - CNTL1_VAL_2[3] -
				 CNTL1_VAL_2[4] - CNTL1_VAL_2[5];
		/*CNTL1_VAL_1[0] = 0xE2;*/
		CNTL1_VAL_1[1] = 0x08;
		gauge_write_i2c_block(chip, 0x60, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x60 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		usleep_range(260000, 260000); /*15ms*/

		CNTL1_VAL_1[0] = 0x58; /*READ 7200S 1C20*/
		CNTL1_VAL_1[1] = 0x46;
		gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = gauge_read_i2c_block(chip, 0x3E, 6, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1],
		       read_buf[2], read_buf[3], read_buf[4], read_buf[5]);
		/*check param cfg*/
		if (!(read_buf[2] == CNTL1_VAL_2[2] && read_buf[3] == CNTL1_VAL_2[3] && read_buf[4] == CNTL1_VAL_2[4] &&
		      read_buf[5] == CNTL1_VAL_2[5])) {
			retry_cnt++;
			if (retry_cnt >= 3) {
				goto param_cf_err;
			}
			pr_err("gauge err recfg_pararm2");
			goto recfg_pararm2;
		}
		retry_cnt = 0;
		usleep_range(5000, 5000);
		/*############################################0x0A47###################################################*/
		/*write 0A47*/
		pr_err("%s write 0x0A47\n", __func__);
	recfg_pararm3:
		CNTL1_VAL_1[0] = 0x0A;
		CNTL1_VAL_1[1] = 0x47;
		gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = gauge_read_i2c_block(chip, 0x3E, 6, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0],
		       read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);

		CNTL1_VAL_2[0] = 0x0A;
		CNTL1_VAL_2[1] = 0x47;
		CNTL1_VAL_2[2] = 0x46;
		CNTL1_VAL_2[3] = 0x00;
		CNTL1_VAL_2[4] = 0x3C;
		CNTL1_VAL_2[5] = 0x00;
		gauge_write_i2c_block(chip, 0x3E, 6, CNTL1_VAL_2); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_2[0], CNTL1_VAL_2[1], CNTL1_VAL_2[2], CNTL1_VAL_2[3], CNTL1_VAL_2[4], CNTL1_VAL_2[5]);

		CNTL1_VAL_1[0] = 0xFF - CNTL1_VAL_2[0] - CNTL1_VAL_2[1] - CNTL1_VAL_2[2] - CNTL1_VAL_2[3] -
				 CNTL1_VAL_2[4] - CNTL1_VAL_2[5];
		/*CNTL1_VAL_1[0] = 0xE2;*/
		CNTL1_VAL_1[1] = 0x08;
		gauge_write_i2c_block(chip, 0x60, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x60 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		usleep_range(260000, 260000); /*15ms*/

		CNTL1_VAL_1[0] = 0x0A; /*READ 7200S 1C20*/
		CNTL1_VAL_1[1] = 0x47;
		gauge_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = gauge_read_i2c_block(chip, 0x3E, 6, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1],
		       read_buf[2], read_buf[3], read_buf[4], read_buf[5]);
		if (!(read_buf[2] == CNTL1_VAL_2[2] && read_buf[3] == CNTL1_VAL_2[3] && read_buf[4] == CNTL1_VAL_2[4] &&
		      read_buf[5] == CNTL1_VAL_2[5])) {
			retry_cnt++;
			if (retry_cnt >= 3) {
				goto param_cf_err;
			}
			pr_err("gauge err recfg_pararm3");
			goto recfg_pararm3;
		}
		usleep_range(5000, 5000);
		pr_err("gauge param cfg susccess\n");
	}
	pr_err("%s end\n", __func__);
	return GAUGE_OK;
param_cf_err:
	pr_err("%s param_cf_err err\n", __func__);
	return GAUGE_ERROR;
}

int bq28z610_batt_full_zero_parameter(struct chip_bq27541 *chip)
{
	int rc = 0;
	bool tried_again = false;

	if (chip->device_type != DEVICE_BQ27541) {
		pr_err("%s NOT DEVICE_BQ27541\n", __func__);
		return GAUGE_OK;
	}

	if (!chip->batt_bq28z610)
		return GAUGE_OK;

	pr_err("%s begin\n", __func__);
	if (bq8z610_sealed(chip)) {
		if (!bq8z610_unseal(chip)) {
			return GAUGE_OK;
		} else {
			msleep(50);
		}
	}

write_parameter:
	/*rc = bq28z610_write_soc_smooth_parameter(chip);
	rc = bq28z610_write_iterm_Taper_parameter(chip);
	rc = bq28z610_write_flash_busy_wait_i2c_err(chip);
	usleep_range(5000, 5000);
	*/
	rc = bq28z610_batt_full_zero_parameter_write_cmd(chip);
	if (rc && tried_again == false) {
		tried_again = true;
		goto write_parameter;
	}

	gauge_i2c_txsubcmd(chip, 0, BQ28Z610_SEAL_SUBCMD); /*seal*/
	msleep(1000);
	if (bq8z610_sealed(chip) == 0) {
		usleep_range(1000, 1000);
		bq8z610_seal(chip);
	}
	pr_err("%s end\n", __func__);
	return GAUGE_OK;
}

#define REG_DUMP_SIZE 1024
static int dump_reg[] = { 0x08, 0x12, 0x2c };
static int gauge_reg_dump(void)
{
	int val = 0;
	int i = 0;
	int l = 0;
	char *pos;
	int sum = 0, ret;
	u8 iv[32] = { 0 };
	char buf[REG_DUMP_SIZE] = { 0 };
	int len = REG_DUMP_SIZE;

	if (!gauge_ic) {
		return 0;
	}

	if (gauge_ic->batt_zy0603) {
		return 0;
	}

	if (atomic_read(&gauge_ic->suspended) == 1 || atomic_read(&gauge_ic->gauge_i2c_status)) {
		pr_err("%s: gauge suspend!\n", __func__);
		return -1;
	}

	pos = buf;
	if (oplus_vooc_get_allow_reading() == true) {
		l = sprintf(pos, "%d ", bq27541_get_battery_temperature());
		pos += l;
		sum += l;
		l = sprintf(pos, "/ %d ", bq27541_get_average_current());
		pos += l;
		sum += l;

		for (; (i < sizeof(dump_reg) / sizeof(int)) && (sum < len - 16); i++) {
			ret = gauge_read_i2c(gauge_ic, dump_reg[i], &val);
			if (ret) {
				dev_err(gauge_ic->dev, "[OPLUS_CHG]:error reading regdump, ret:%d\n", ret);
				return -1;
			}
			l = sprintf(pos, "/ %d ", val);
			pos += l;
			sum += l;
		}

		gauge_i2c_txsubcmd(gauge_ic, BQ27411_DATA_CLASS_ACCESS, 0x71);
		usleep_range(10000, 10000);
		gauge_read_i2c_block(gauge_ic, BQ28Z610_REG_CNTL1, 6, iv);
		for (i = 2; i < 6 && sum < len - 16; i++) {
			if ((i % 2) == 0) {
				l = sprintf(pos, "/ %d ", (iv[i + 1] << 8) + iv[i]);
				pos += l;
				sum += l;
			}
		}
		gauge_i2c_txsubcmd(gauge_ic, BQ27411_DATA_CLASS_ACCESS, 0x73);
		usleep_range(10000, 10000);
		gauge_read_i2c_block(gauge_ic, BQ28Z610_REG_CNTL1, 16, iv);
		for (i = 2; i < 16 && sum < len - 16; i++) {
			if (i != 3 && i != 4 && i != 7 && i != 8 && i != 11 && i != 12) {
				if ((i % 2) == 0) {
					l = sprintf(pos, "/ %d ", (iv[i + 1] << 8) + iv[i]);
					pos += l;
					sum += l;
				}
			}
		}
		gauge_i2c_txsubcmd(gauge_ic, BQ27411_DATA_CLASS_ACCESS, 0x74);
		usleep_range(10000, 10000);
		gauge_read_i2c_block(gauge_ic, BQ28Z610_REG_CNTL1, 26, iv);
		for (i = 12; i < 26 && sum < len - 16; i++) {
			if (i != 17 && i != 18) {
				if ((i % 2) == 0) {
					l = sprintf(pos, "/ %d ", (iv[i + 1] << 8) + iv[i]);
					pos += l;
					sum += l;
				}
			}
		}
		gauge_i2c_txsubcmd(gauge_ic, BQ27411_DATA_CLASS_ACCESS, 0x75);
		usleep_range(10000, 10000);
		gauge_read_i2c_block(gauge_ic, BQ28Z610_REG_CNTL1, 12, iv);
		for (i = 2; i < 12 && sum < len - 16; i++) {
			if (i != 3 && i != 5 && i != 6 && i != 8) {
				if ((i % 2) == 0) {
					l = sprintf(pos, "/ %d ", (iv[i + 1] << 8) + iv[i]);
					pos += l;
					sum += l;
				}
			}
		}
	}
	printk(KERN_ERR "[OPLUS_CHG] gauge regs: %s device_type:%d\n", buf, gauge_ic->device_type);
	return 0;
}

static int bq8z610_check_gauge_enable(struct chip_bq27541 *chip)
{
	int value = 0;
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = { 0, 0, 0, 0 };
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_GAUGE_EN);
	msleep(1000);
	gauge_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	pr_err("%s  CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x,\
		CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
	       __func__, CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2], CNTL1_VAL[3]);
	value = (CNTL1_VAL[2] & BQ28Z610_GAUGE_EN_BIT);
	if (value == BQ28Z610_GAUGE_EN_BIT) {
		pr_err("bq8z610 gauge_enable, value = %x return 1\n", value);
		return 1;
	} else {
		pr_err("bq8z610 gauge_enable, value = %x return 0\n", value);
		return 0;
	}
}

static int bq28z610_write_dod0_parameter(struct chip_bq27541 *chip)
{
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x0021);
	msleep(1000);
	gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x0021);
	msleep(2000);
	if (bq8z610_check_gauge_enable(chip) == false) {
		gauge_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x0021);
		msleep(300);
	}
	return 0;
}

static void bq28z610_modify_dod0_parameter(struct chip_bq27541 *chip)
{
	int rc = 0;

	pr_err("%s begin\n", __func__);
	if (bq8z610_sealed(chip)) {
		if (!bq8z610_unseal(chip)) {
			return;
		} else {
			msleep(50);
		}
	}
	rc = bq28z610_write_dod0_parameter(chip);
	usleep_range(1000, 1000);
	if (bq8z610_sealed(chip) == 0) {
		usleep_range(1000, 1000);
		bq8z610_seal(chip);
	}
	pr_err("%s end\n", __func__);
}

static int bq28z610_get_2cell_voltage(void)
{
	u8 cell_vol[BQ28Z610_MAC_CELL_VOLTAGE_SIZE] = { 0, 0, 0, 0 };
	u8 cell_vol_zy0603[BQ28Z610_MAC_CELL_VOLTAGE_SIZE + 2] = { 0, 0, 0, 0, 0, 0 };

	if (!gauge_ic) {
		return 0;
	}
	if (!gauge_ic->batt_zy0603) {
		mutex_lock(&gauge_ic->gauge_alt_manufacturer_access);
		gauge_i2c_txsubcmd(gauge_ic, BQ28Z610_MAC_CELL_VOLTAGE_EN_ADDR, BQ28Z610_MAC_CELL_VOLTAGE_CMD);
		usleep_range(1000, 1000);
		gauge_read_i2c_block(gauge_ic, BQ28Z610_MAC_CELL_VOLTAGE_ADDR, BQ28Z610_MAC_CELL_VOLTAGE_SIZE,
				       cell_vol);
		mutex_unlock(&gauge_ic->gauge_alt_manufacturer_access);
		gauge_ic->batt_cell_1_vol = (cell_vol[1] << 8) | cell_vol[0];
		gauge_ic->batt_cell_2_vol = (cell_vol[3] << 8) | cell_vol[2];
	} else {
		gauge_i2c_txsubcmd(gauge_ic, BQ28Z610_MAC_CELL_VOLTAGE_EN_ADDR, BQ28Z610_MAC_CELL_VOLTAGE_CMD);
		usleep_range(U_DELAY_1_MS, U_DELAY_1_MS);
		gauge_read_i2c_block(gauge_ic, BQ28Z610_MAC_CELL_VOLTAGE_EN_ADDR, BQ28Z610_MAC_CELL_VOLTAGE_SIZE + 2,
				       cell_vol_zy0603);
		if (cell_vol_zy0603[0] == 0x71 && cell_vol_zy0603[1] == 0x00) {
			gauge_ic->batt_cell_1_vol = (cell_vol_zy0603[3] << 8) | cell_vol_zy0603[2];
			gauge_ic->batt_cell_2_vol = (cell_vol_zy0603[5] << 8) | cell_vol_zy0603[4];
		}
	}

	if (gauge_ic->batt_cell_1_vol < gauge_ic->batt_cell_2_vol) {
		gauge_ic->batt_cell_max_vol = gauge_ic->batt_cell_2_vol;
		gauge_ic->batt_cell_min_vol = gauge_ic->batt_cell_1_vol;
	} else {
		gauge_ic->batt_cell_max_vol = gauge_ic->batt_cell_1_vol;
		gauge_ic->batt_cell_min_vol = gauge_ic->batt_cell_2_vol;
	}
	/*chg_err("batt_cell_1_vol = %dmV, batt_cell_2_vol = %dmV, batt_cell_max_vol = %dmV\n",
		gauge_ic->batt_cell_1_vol,
		gauge_ic->batt_cell_2_vol,
		gauge_ic->batt_cell_max_vol);*/

	return 0;
}

/*static int bq28z610_get_2cell_balance_time(void)
{
	u8 balance_time[BQ28Z610_MAC_CELL_BALANCE_TIME_SIZE] = {0,0,0,0};

	gauge_i2c_txsubcmd(BQ28Z610_MAC_CELL_BALANCE_TIME_EN_ADDR, BQ28Z610_MAC_CELL_BALANCE_TIME_CMD);
	usleep_range(1000, 1000);
	gauge_read_i2c_block(BQ28Z610_MAC_CELL_BALANCE_TIME_ADDR, BQ28Z610_MAC_CELL_BALANCE_TIME_SIZE, balance_time);
	pr_err("Cell_balance_time_remaining1 = %dS, Cell_balance_time_remaining2 = %dS\n", 
	(balance_time[1] << 8) | balance_time[0], (balance_time[3] << 8) | balance_time[2]);

	return 0;
}*/

#ifdef CONFIG_OPLUS_CHARGER_MTK
#define AUTH_MESSAGE_LEN 20
#define AUTH_TAG "ogauge_auth="
#define AUTH_SHA256_TAG "ogauge_sha256_auth="
#define AUTH_PROP "ogauge_auth"
#define BOOTARGS_PROP "bootargs"
#define AUTH_SHA256_PROP "ogauge_sha256_auth"
#ifdef MODULE
#include <asm/setup.h>
static char __oplus_chg_cmdline[COMMAND_LINE_SIZE];
static char *oplus_chg_cmdline = __oplus_chg_cmdline;

const char *oplus_chg_get_cmdline(const char *target_str)
{
	struct device_node * of_chosen = NULL;
	char *ogauge_auth = NULL;

	if (!target_str)
		return oplus_chg_cmdline;

	if (__oplus_chg_cmdline[0] != 0)
		return oplus_chg_cmdline;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		ogauge_auth = (char *)of_get_property(
					of_chosen, target_str, NULL);
		if (!ogauge_auth)
			pr_err("%s: failed to get %s\n", __func__, target_str);
		else {
			strcpy(__oplus_chg_cmdline, ogauge_auth);
			pr_err("%s: %s: %s\n", __func__, target_str, ogauge_auth);
		}
	} else {
		pr_err("%s: failed to get /chosen \n", __func__);
	}

	return oplus_chg_cmdline;
}
#else
const char *oplus_chg_get_cmdline(const char *target_str)
{
	if (!target_str)
		pr_err("%s: args set error\n", __func__);

	return saved_command_line;
}
#endif

static int get_auth_msg(u8 *source, u8 *rst)
{
	char *str = NULL;
	int i;

	str = strstr(oplus_chg_get_cmdline(AUTH_PROP), AUTH_TAG);
	if (str == NULL) {
		pr_err("Asynchronous auth not received from %s!!!\n", AUTH_PROP);
		str = strstr(oplus_chg_get_cmdline(BOOTARGS_PROP), AUTH_TAG);
		if (str == NULL) {
			pr_err("Asynchronous auth not received from %s!!!\n", BOOTARGS_PROP);
			return -1;
		}
	}
	pr_info("%s\n", str);
	str += strlen(AUTH_TAG);
	for (i = 0; i < AUTH_MESSAGE_LEN; i++) {
		source[i] = (str[2 * i] - 64) | ((str[2 * i + 1] - 64) << 4);
		pr_info("source index %d = %x\n", i, source[i]);
	}
	str += AUTH_MESSAGE_LEN * 2;
	for (i = 0; i < AUTH_MESSAGE_LEN; i++) {
		rst[i] = (str[2 * i] - 64) | ((str[2 * i + 1] - 64) << 4);
		pr_info("expected index %d = %x\n", i, rst[i]);
	}
	return 0;
}

static int get_auth_sha256_msg(u8 *source, u8 *rst)
{
	char *str = NULL;
	int i;

	str = strstr(oplus_chg_get_cmdline(AUTH_SHA256_PROP), AUTH_SHA256_TAG);
	if (str == NULL) {
		pr_err("Asynchronous authentication is not supported!!!\n");
		return -1;
	}
	chg_info("%s\n", str);
	str += strlen(AUTH_SHA256_TAG);
	for (i = 0; i < GAUGE_SHA256_AUTH_MSG_LEN; i++) {
		source[i] = (str[2 * i] - 64) | ((str[2 * i + 1] - 64) << 4);
		chg_info("source index %d = %x\n", i, source[i]);
	}
	str += GAUGE_SHA256_AUTH_MSG_LEN * 2;
	for (i = 0; i < GAUGE_SHA256_AUTH_MSG_LEN; i++) {
		rst[i] = (str[2 * i] - 64) | ((str[2 * i + 1] - 64) << 4);
		chg_info("expected index %d = %x\n", i, rst[i]);
	}

	return 0;
}
#endif

#define BLOCKDATACTRL 0X61
#define DATAFLASHBLOCK 0X3F
#define AUTHENDATA 0X40
#define AUTHENCHECKSUM 0X54
#define BQ27541_SHA_MAX_WAIT 10

static int bq27541_sha1_hmac_authenticate(struct bq27541_authenticate_data *authenticate_data)
{
	int i;
	int ret;
	unsigned char t;
	int len;
	u8 checksum_buf[1] = { 0x0 };
	u8 authen_cmd_buf[1] = { 0x00 };
	u8 recv_buf[AUTHEN_MESSAGE_MAX_COUNT] = { 0x0 };

	if (authenticate_data == NULL) {
		pr_err("%s authenticate_data NULL\n", __func__);
		return -1;
	}

	/* step 0: produce 20 bytes random data and checksum */
	for (i = 0; i < authenticate_data->message_len; i++) {
		checksum_buf[0] = checksum_buf[0] + authenticate_data->message[i];
	}
	checksum_buf[0] = 0xff - (checksum_buf[0] & 0xff);

#ifdef XBL_AUTH_DEBUG
	for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3; i = i + 4) {
		pr_info("%s: sending msg[%d]=%x, sending msg[%d]=%x,sending msg[%d]=%x, sending msg[%d]=%x\n", __func__,
			i, authenticate_data->message[i], i + 1, authenticate_data->message[i + 1], i + 2,
			authenticate_data->message[i + 2], i + 3, authenticate_data->message[i + 3]);
	}
#endif
	if (gauge_ic->sha1_key_index == ZY0602_KEY_INDEX) {
		len = authenticate_data->message_len;
		authenticate_data->message[len] = checksum_buf[0];

		/* step 1 */
		ret = gauge_i2c_txsubcmd_onebyte(gauge_ic, BLOCKDATACTRL, 01);
		if (ret < 0) {
			chg_err("%s i2c write error\n", __func__);
			return -1;
		}

		/* step 2: write 20 bytes to authendata_reg and 1 bytes checksum */
		msleep(1);
		gauge_write_i2c_block(gauge_ic, AUTHENDATA, authenticate_data->message_len + 1,
					authenticate_data->message);

		/* step 3: read authendata */
		/* Wait at most 100ms for the authendata is ready. */
		i = BQ27541_SHA_MAX_WAIT;
		while (i--) {
			msleep(10);
			gauge_read_i2c_block(gauge_ic, AUTHENDATA, authenticate_data->message_len, &recv_buf[0]);
			if (strncmp(authenticate_data->message, recv_buf, authenticate_data->message_len)) {
				pr_info("%s gauge authenticate read success, i = %d\n", __func__, i);
				break;
			}
		}
	} else {
		/* step 1: seal mode->write 0x00 to dataflashblock */
		ret = gauge_i2c_txsubcmd_onebyte(gauge_ic, DATAFLASHBLOCK, authen_cmd_buf[0]);
		if (ret < 0) {
			chg_err("%s i2c write error\n", __func__);
			return -1;
		}

		/* step 2: write 20 bytes to authendata_reg */
		gauge_write_i2c_block(gauge_ic, AUTHENDATA, authenticate_data->message_len,
					authenticate_data->message);
		msleep(5);

		/* step 3: write checksum to authenchecksum_reg for compute */
		gauge_i2c_txsubcmd_onebyte(gauge_ic, AUTHENCHECKSUM, checksum_buf[0]);
		msleep(10);

		/* step 4: read authendata */
		gauge_read_i2c_block(gauge_ic, AUTHENDATA, authenticate_data->message_len, &recv_buf[0]);
	}
	len = authenticate_data->message_len;
	for (i = 0; i < len / 2; i++) {
		t = recv_buf[i];
		recv_buf[i] = recv_buf[len - i - 1];
		recv_buf[len - i - 1] = t;
	}
#ifdef XBL_AUTH_DEBUG
	for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3; i = i + 4) {
		pr_info("%s: hw[%d]=%x,hw[%d]=%x,hw[%d]=%x,hw[%d]=%x\n", __func__, i, recv_buf[i], i + 1,
			recv_buf[i + 1], i + 2, recv_buf[i + 2], i + 3, recv_buf[i + 3]);
	}
#endif
	memcpy(authenticate_data->message, &recv_buf[0], authenticate_data->message_len);
#ifdef XBL_AUTH_DEBUG
	for (i = 0; i < authenticate_data->message_len; i++) {
		pr_err("HW_sha1_hmac_result[0x%x]", authenticate_data->message[i]);
	}
#endif
	return 0;
}

static bool get_smem_batt_info(oplus_gauge_auth_result *auth, int kk)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	int ret = 0;

	ret = get_auth_msg(auth->msg, auth->rcv_msg);
	if (ret == 0) {
		return true;
	} else {
		return false;
	}
#else
	size_t smem_size;
	void *smem_addr;
	oplus_gauge_auth_info_type *smem_data;
	int i;
	if (NULL == auth) {
		pr_err("%s: invalid parameters\n", __func__);
		return false;
	}

	memset(auth, 0, sizeof(oplus_gauge_auth_result));
	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_RESERVED_BOOT_INFO_FOR_APPS, &smem_size);
	if (IS_ERR(smem_addr)) {
		pr_err("unable to acquire smem SMEM_RESERVED_BOOT_INFO_FOR_APPS entry\n");
		return false;
	} else {
		smem_data = (oplus_gauge_auth_info_type *)smem_addr;
		if (smem_data == ERR_PTR(-EPROBE_DEFER)) {
			smem_data = NULL;
			pr_info("fail to get smem_data\n");
			return false;
		} else {
			if (0 == kk) {
				memcpy(auth, &smem_data->rst_k0, sizeof(oplus_gauge_auth_result));
			} else {
				if (ZY0602_KEY_INDEX == kk)
					memcpy(auth, &smem_data->rst_k2, sizeof(oplus_gauge_auth_result));
				else
					memcpy(auth, &smem_data->rst_k1, sizeof(oplus_gauge_auth_result));
				pr_info("%s: auth result=%d\n", __func__, auth->result);

#ifdef XBL_AUTH_DEBUG
				for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3; i = i + 4) {
					pr_info("%s: msg[%d]=%x,msg[%d]=%x,msg[%d]=%x,msg[%d]=%x\n", __func__, i,
						auth->msg[i], i + 1, auth->msg[i + 1], i + 2, auth->msg[i + 2], i + 3,
						auth->msg[i + 3]);
				}
				for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3; i = i + 4) {
					pr_info("%s: rcv_msg[%d]=%x,rcv_msg[%d]=%x,rcv_msg[%d]=%x,rcv_msg[%d]=%x\n",
						__func__, i, auth->rcv_msg[i], i + 1, auth->rcv_msg[i + 1], i + 2,
						auth->rcv_msg[i + 2], i + 3, auth->rcv_msg[i + 3]);
				}
#endif
			}
		}
	}
	return true;
#endif
}

static int zy0602_fg_read_ram_block(struct chip_bq27541 *chip, u32 reg, u8 length, u8 *val)
{
	int ret;
	int i;
	int sum;
	int ramkey;
	u8 checksum;

	mutex_lock(&chip->gauge_alt_manufacturer_access);
	ret = gauge_i2c_txsubcmd(chip, ZY602_MAC_CELL_DOD0_EN_ADDR, 0xA3);
	if (ret < 0)
		goto err;
	msleep(ZY0602_CMD_SBS_DELAY);
	ret = gauge_read_i2c(chip, ZY602_MAC_CELL_DOD0_EN_ADDR, &ramkey);
	if (ret < 0)
		goto err;

	msleep(ZY0602_CMD_SBS_DELAY);
	sum = ((u16)ramkey >> 8);
	checksum = (u8)((0xff & ramkey) ^ sum ^ 0xA3);
	ret = gauge_i2c_txsubcmd_onebyte(chip, ZY0602_CMD_DFSTART, 0x03);
	if (ret < 0)
		goto err;
	msleep(ZY0602_CMD_SBS_DELAY);
	ret = gauge_i2c_txsubcmd_onebyte(chip, ZY0602_CMD_DFCLASS, (u8)reg);
	if (ret < 0)
		goto err;

	msleep(ZY0602_CMD_SBS_DELAY);
	ret = gauge_i2c_txsubcmd_onebyte(chip, ZY0602_CMD_DFPAGE, checksum);
	if (ret < 0)
		goto err;
	msleep(ZY0602_CMD_SBS_DELAY);

	ret = gauge_read_i2c_block(chip, ZY0602_CMD_BLOCK, length, val);
	if (ret < 0)
		goto err;
	mutex_unlock(&chip->gauge_alt_manufacturer_access);

	for (i = 0; i < length; i++)
		val[i] = (u8)(val[i] ^ sum);

	return 0;
err:
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return ret;
}

static int zy0602_fg_read_firmware_version(struct chip_bq27541 *chip)
{
	int ret;
	int data = 0;

	mutex_lock(&chip->gauge_alt_manufacturer_access);
	ret = gauge_i2c_txsubcmd(chip, ZY602_MAC_CELL_DOD0_EN_ADDR, ZY0602_CMD_FIRMWARE_VERSION);
	if (ret < 0)
		goto err;
	msleep(ZY0602_CMD_SBS_DELAY);
	ret = gauge_read_i2c(chip, ZY602_MAC_CELL_DOD0_EN_ADDR, &data);
	if (ret < 0)
		goto err;
	mutex_unlock(&chip->gauge_alt_manufacturer_access);

	return data;
err:
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return 0;
}

static void zy0602_get_info(struct chip_bq27541 *chip, u8 *info, int len)
{
	int i;
	int j;
	int ret;
	int data = 0;
	int index = 0;
	int local_crc = 0;
	int try_count = ZY0602_SUBCMD_TRY_COUNT;
	u8 extend_data[32] = { 0 };
	u8 checksum;
	static int version = ZY0602_FIRMWARE_VERSION_DEFAULT;
	struct gauge_track_info_reg standard[] = {
		{ chip->cmd_addr.reg_cc, 2, 0, 1 },
		{ chip->cmd_addr.reg_volt, 2, 0, 1 },
		{ chip->cmd_addr.reg_soc, 2, 0, 1 },
		{ chip->cmd_addr.reg_soh, 2, 0, 1 },
	};
	struct gauge_track_info_reg extend[] = {
		{ ZY602_MAC_CELL_DOD0_CMD, 32, 0, 31 },
		{ ZY602_MAC_CELL_E4_CMD, 32, 12, 31 },
		{ ZY602_MAC_CELL_E5_CMD, 32, 0, 7 },
		{ ZY602_MAC_CELL_E5_CMD, 32, 20, 27 },
		{ ZY602_MAC_CELL_E6_CMD, 32, 0, 31 },
	};

	if (version == ZY0602_FIRMWARE_VERSION_DEFAULT)
		version = zy0602_fg_read_firmware_version(chip);
	if (version < ZY0602_FIRMWARE_VERSION_SUPPORT) {
		pr_info("gauge firmware version not support\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(standard); i++) {
		ret = gauge_read_i2c(chip, standard[i].addr, &data);
		if (ret < 0)
			continue;
		index += snprintf(info + index, len - index, "0x%02x=%02x,%02x|", standard[i].addr, (data & 0xff),
				  ((data >> 8) & 0xff));
	}

	ret = zy0602_fg_read_ram_block(chip, ZY0602_CMD_GAUGEBLOCK6, sizeof(extend_data), extend_data);
	if (ret >= 0)
		index += snprintf(info + index, len - index, "0x002b=%02x,%02x,%02x,%02x,%02x,%02x|", extend_data[26],
				  extend_data[27], extend_data[28], extend_data[29], extend_data[30], extend_data[31]);

	for (i = 0; i < ARRAY_SIZE(extend); i++) {
		try_count = ZY0602_SUBCMD_TRY_COUNT;
try:
		mutex_lock(&chip->gauge_alt_manufacturer_access);
		ret = gauge_i2c_txsubcmd(chip, ZY602_MAC_CELL_DOD0_EN_ADDR, extend[i].addr);
		if (ret < 0) {
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			continue;
		}

		if (sizeof(extend_data) >= extend[i].len) {
			msleep(ZY0602_CMD_SBS_DELAY);
			ret = bq27541_read_i2c_onebyte(chip, ZY602_MAC_CELL_DOD0_EN_ADDR + 1, &checksum);
			msleep(ZY0602_CMD_SBS_DELAY);
			ret = gauge_read_i2c_block(chip, ZY602_MAC_CELL_DOD0_ADDR, extend[i].len, extend_data);
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			if (ret < 0)
				continue;

			local_crc = 0;
			for (j = 0; j < extend[i].len; j++)
				local_crc += extend_data[j];
			local_crc = 0xff - (local_crc & 0xff);
			if (try_count > 0 && local_crc != checksum) {
				try_count--;
				chg_info("crc not match, addr:0x%x, ic_crc:0x%0x, local_crc:0x%0x, count:%d\n",
					 extend[i].addr, checksum, local_crc, try_count);
				msleep(ZY0602_CMD_SBS_DELAY);
				goto try;
			}

			if (!try_count)
				continue;
			index += snprintf(info + index, len - index, "0x%02x=", extend[i].addr);
			for (j = extend[i].start_index; j < extend[i].end_index; j++)
				index += snprintf(info + index, len - index, "%02x,", extend_data[j]);
			index += snprintf(info + index, len - index, "%02x", extend_data[j]);
			if (i < ARRAY_SIZE(extend) - 1)
				msleep(ZY0602_CMD_SBS_DELAY);
		} else {
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
		}
		if (i < ARRAY_SIZE(extend) - 1)
			index += snprintf(info + index, len - index, "|");
	}
}

static void bq28z610_get_info(struct chip_bq27541 *chip, u8 *info, int len)
{
	int i;
	int j;
	int ret;
	int data = 0;
	int index = 0;
	int data_check;
	int try_count = BQ28Z610_SUBCMD_TRY_COUNT;
	u8 extend_data[34] = { 0 };
	struct gauge_track_info_reg standard[] = {
		{ chip->cmd_addr.reg_temp, 2 },
		{ chip->cmd_addr.reg_volt, 2 },
		{ chip->cmd_addr.reg_flags, 2 },
		{ chip->cmd_addr.reg_nac, 2 },
		{ chip->cmd_addr.reg_rm, 2 },
		{ chip->cmd_addr.reg_fcc, 2 },
		{ BQ28Z610_REG_AI, 2 },
		{ chip->cmd_addr.reg_cc, 2 },
		{ chip->cmd_addr.reg_soc, 2 },
		{ chip->cmd_addr.reg_soh, 2 },
	};

	struct gauge_track_info_reg extend[] = {
		{ BQ28Z610_SUBCMD_CHEMID, 2 },
		{ BQ28Z610_SUBCMD_GAUGEING_STATUS, 3 },
		{ BQ28Z610_SUBCMD_DA_STATUS1, 12 },
		{ BQ28Z610_SUBCMD_IT_STATUS1, 16 },
		{ BQ28Z610_SUBCMD_IT_STATUS2, 24 },
		{ BQ28Z610_SUBCMD_IT_STATUS3, 16 },
		{ BQ28Z610_SUBCMD_CB_STATUS, 8 },
	};

	for (i = 0; i < ARRAY_SIZE(standard); i++) {
		ret = gauge_read_i2c(chip, standard[i].addr, &data);
		if (ret < 0)
			continue;
		index += snprintf(info + index, len - index, "0x%02x=%02x,%02x|", standard[i].addr, (data & 0xff),
				  ((data >> 8) & 0xff));
	}

	for (i = 0; i < ARRAY_SIZE(extend); i++) {
		try_count = BQ28Z610_SUBCMD_TRY_COUNT;
try:
		mutex_lock(&chip->gauge_alt_manufacturer_access);
		ret = gauge_i2c_txsubcmd(chip, BQ28Z610_DATAFLASHBLOCK, extend[i].addr);
		if (ret < 0) {
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			continue;
		}

		if (sizeof(extend_data) >= extend[i].len + 2) {
			usleep_range(1000, 1000);
			ret = gauge_read_i2c_block(chip, BQ28Z610_DATAFLASHBLOCK, (extend[i].len + 2), extend_data);
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			if (ret < 0)
				continue;
			data_check = (extend_data[1] << 0x8) | extend_data[0];
			if (try_count-- > 0 && data_check != extend[i].addr) {
				chg_err("not match. extend_data=0x%2x, expect=0x%2x\n", data_check, extend[i].addr);
				usleep_range(500, 500);
				goto try;
			}
			if (!try_count)
				continue;
			index += snprintf(info + index, len - index, "0x%04x=", extend[i].addr);
			for (j = 0; j < extend[i].len - 1; j++)
				index += snprintf(info + index, len - index, "%02x,", extend_data[j + 2]);
			index += snprintf(info + index, len - index, "%02x", extend_data[j + 2]);
			if (i < ARRAY_SIZE(extend) - 1)
				usleep_range(500, 500);
		} else {
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
		}
		if (i < ARRAY_SIZE(extend) - 1)
			index += snprintf(info + index, len - index, "|");
	}
}

static void bq27426_get_info(struct chip_bq27541 *chip, u8 *info, int len)
{
	int i;
	int ret;
	int data = 0;
	int index = 0;
	struct gauge_track_info_reg standard[] = {
		{ chip->cmd_addr.reg_temp, 2 },
		{ chip->cmd_addr.reg_inttemp, 2 },
		{ chip->cmd_addr.reg_volt , 2 },
		{ chip->cmd_addr.reg_flags  , 2 },
		{ chip->cmd_addr.reg_rm , 2 },
		{ chip->cmd_addr.reg_fcc , 2 },
		{ chip->cmd_addr.reg_ai , 2 },
		{ chip->cmd_addr.reg_ap , 2 },
		{ chip->cmd_addr.reg_soc , 2 },
		{ chip->cmd_addr.reg_soh , 2 },
		{ BQ27426_REM_CAP_UNFILT , 2 },
		{ BQ27426_REM_CAP_FILT , 2 },
		{ BQ27426_FCC_UNFILT , 2 },
		{ BQ27426_FCC_FLIT , 2 },
		{ BQ27426_REG_UNFILT , 2 },
		{ BQ27426_QMAX_16 , 2 },
		{ BQ27426_PRESENT_DOD_1A , 2 },
		{ BQ27426_OCV_CURRENT , 2 },
		{ BQ27426_OCV_VOLTAGE , 2 },
		{ BQ27426_DOD0_66 , 2 },
		{ BQ27426_DOD_AT_EOC , 2 },
		{ BQ27426_REM_CAP_6A , 2 },
		{ BQ27426_PASSED_CHG , 2 },
		{ BQ27426_QSTART , 2 },
		{ BQ27426_DOD_FINAL , 2 },
	};

	for (i = 0; i < ARRAY_SIZE(standard); i++) {
		ret = gauge_read_i2c(chip, standard[i].addr, &data);
		if (ret < 0)
			continue;
		index += snprintf(info + index, len - index,
			  "0x%02x=%02x,%02x|", standard[i].addr, (data & 0xff), ((data >> 8) & 0xff));
	}
}

static void bq27z561_get_info(struct chip_bq27541 *chip, u8 *info, int len)
{
	int i;
	int j;
	int ret;
	int data = 0;
	int index = 0;
	int data_check;
	int try_count = BQ27Z561_SUBCMD_TRY_COUNT;
	u8 extend_data[34] = {0};
	struct gauge_track_info_reg standard[] = {
		{ chip->cmd_addr.reg_temp, 2 },
		{ chip->cmd_addr.reg_volt , 2 },
		{ chip->cmd_addr.reg_flags  , 2 },
		{ chip->cmd_addr.reg_nac , 2 },
		{ chip->cmd_addr.reg_rm , 2 },
		{ chip->cmd_addr.reg_fcc , 2 },
		{ chip->cmd_addr.reg_cc , 2 },
		{ chip->cmd_addr.reg_soc , 2 },
		{ chip->cmd_addr.reg_soh , 2 },
	};

	struct gauge_track_info_reg extend[] = {
		{ BQ27Z561_SUBCMD_CHEMID , 2 },
		{ BQ27Z561_SUBCMD_GAUGE_STATUS  , 3 },
		{ BQ27Z561_SUBCMD_IT_STATUS1 , 16 },
		{ BQ27Z561_SUBCMD_IT_STATUS2 , 20 },
		{ BQ27Z561_SUBCMD_IT_STATUS3 , 12 },
		{ BQ27Z561_SUBCMD_FILTERED_CAP , 6 },
	};


	for (i = 0; i < ARRAY_SIZE(standard); i++) {
		ret = gauge_read_i2c(chip, standard[i].addr, &data);
		if (ret < 0)
			continue;
		index += snprintf(info + index, len - index,
			  "0x%02x=%02x,%02x|", standard[i].addr, (data & 0xff), ((data >> 8) & 0xff));
	}

	for (i = 0; i < ARRAY_SIZE(extend); i++) {
		try_count = BQ27Z561_SUBCMD_TRY_COUNT;
try:
		mutex_lock(&chip->gauge_alt_manufacturer_access);
		ret = gauge_i2c_txsubcmd(chip, BQ27Z561_DATAFLASHBLOCK, extend[i].addr);
		if (ret < 0) {
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			continue;
		}

		if (sizeof(extend_data) >= extend[i].len + 2) {
			usleep_range(1000, 1000);
			ret = gauge_read_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, (extend[i].len + 2), extend_data);
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			if (ret < 0)
				continue;
			data_check = (extend_data[1] << 0x8) | extend_data[0];
			if (try_count-- > 0 && data_check != extend[i].addr) {
				pr_info("0x%4x not match. try_count=%d, extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
					extend[i].addr, try_count, extend_data[0], extend_data[1]);
				usleep_range(2000, 2000);
				goto try;
			}
			if (!try_count)
				continue;
			index += snprintf(info + index, len - index, "0x%04x=", extend[i].addr);
			for (j = 0; j < extend[i].len - 1; j++)
				index += snprintf(info + index, len - index, "%02x,", extend_data[j + 2]);
			index += snprintf(info + index, len - index, "%02x", extend_data[j + 2]);
			if (i < ARRAY_SIZE(extend) - 1)
				usleep_range(2000, 2000);
		} else {
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
		}
		if (i  <  ARRAY_SIZE(extend) - 1)
			index += snprintf(info + index, len - index, "|");
	}
}

static int gauge_get_info(u8 *info, int len)
{
	if (!gauge_ic || !info || !len)
		return -1;

	if (!oplus_vooc_get_allow_reading() || atomic_read(&gauge_ic->gauge_i2c_status))
		return -1;

	if (DEVICE_ZY0602 == gauge_ic->device_type)
		zy0602_get_info(gauge_ic, info, len);
	else if (gauge_ic->batt_bq28z610 && DEVICE_BQ27541 == gauge_ic->device_type && !gauge_ic->batt_zy0603)
		bq28z610_get_info(gauge_ic, info, len);
	else if (gauge_ic->batt_bq27z561)
		bq27z561_get_info(gauge_ic, info, len);
	else if (DEVICE_BQ27426 == gauge_ic->device_type)
		bq27426_get_info(gauge_ic, info, len);
	else if (gauge_ic->batt_nfg1000a)
		nfg1000a_get_info(gauge_ic, info, len);

	return 0;
}

static int sub_gauge_get_info(u8 *info, int len)
{
	if (!sub_gauge_ic || !info || !len)
		return -1;

	if (!oplus_vooc_get_allow_reading() || atomic_read(&sub_gauge_ic->gauge_i2c_status))
		return -1;

	if (DEVICE_ZY0602 == sub_gauge_ic->device_type)
		zy0602_get_info(sub_gauge_ic, info, len);
	else if (sub_gauge_ic->batt_bq27z561)
		bq27z561_get_info(sub_gauge_ic, info, len);
	else if (sub_gauge_ic->batt_nfg1000a)
		nfg1000a_get_info(sub_gauge_ic, info, len);

	return 0;
}

static int bq28z610_get_calib_time(struct chip_bq27541 *chip, int *dod_calib_time, int *qmax_calib_time)
{
	int i;
	int ret = 0;
	int data_check;
	int try_count;
	u8 extend_data[16] = { 0 };
	int check_args[CALIB_TIME_CHECK_ARGS] = { 0 };
	static bool init_flag = false;
	struct gauge_track_info_reg extend[] = {
		{ BQ28Z610_MAC_CELL_DOD0_CMD, 14 },
		{ BQ28Z610_MAC_CELL_QMAX_CMD, 8 },
	};

	if (!chip || !dod_calib_time || !qmax_calib_time)
		return -1;

	for (i = 0; i < ARRAY_SIZE(extend); i++) {
		try_count = BQ28Z610_SUBCMD_TRY_COUNT;
try:
		mutex_lock(&chip->gauge_alt_manufacturer_access);
		ret = gauge_i2c_txsubcmd(chip, BQ28Z610_DATAFLASHBLOCK, extend[i].addr);
		if (ret < 0) {
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			return -1;
		}

		if (sizeof(extend_data) >= extend[i].len + 2) {
			usleep_range(1000, 1000);
			ret = gauge_read_i2c_block(chip, BQ28Z610_MAC_CELL_QMAX_EN_ADDR, (extend[i].len + 2),
						     extend_data);
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
			if (ret < 0)
				return -1;
			data_check = (extend_data[1] << 0x8) | extend_data[0];
			if (try_count-- > 0 && data_check != extend[i].addr) {
				chg_err("not match. extend_data=0x%2x, expect=0x%2x\n", data_check, extend[i].addr);
				usleep_range(500, 500);
				goto try;
			}
			if (!try_count)
				return -1;
			if (extend[i].addr == BQ28Z610_MAC_CELL_DOD0_CMD) {
				check_args[0] = (extend_data[13] << 0x08) | extend_data[12];
				check_args[3] = (extend_data[15] << 0x08) | extend_data[14];
			} else {
				check_args[1] = (extend_data[3] << 0x08) | extend_data[2];
				check_args[2] = (extend_data[7] << 0x08) | extend_data[6];
				check_args[4] = (extend_data[5] << 0x08) | extend_data[4];
				check_args[5] = (extend_data[9] << 0x08) | extend_data[8];
			}
			if (i < ARRAY_SIZE(extend) - 1)
				usleep_range(500, 500);
		} else {
			mutex_unlock(&chip->gauge_alt_manufacturer_access);
		}
	}

	if (!init_flag || check_args[0] != chip->calib_check_args_pre[0] || check_args[3] != chip->calib_check_args_pre[3])
		chip->dod_time = 0;
	else
		chip->dod_time++;

	if (!init_flag || check_args[1] != chip->calib_check_args_pre[1] || check_args[2] != chip->calib_check_args_pre[2] ||
	    check_args[4] != chip->calib_check_args_pre[4] || check_args[5] != chip->calib_check_args_pre[5])
		chip->qmax_time = 0;
	else
		chip->qmax_time++;

	init_flag = true;
	memcpy(chip->calib_check_args_pre, check_args, sizeof(check_args));
	*dod_calib_time = chip->dod_time;
	*qmax_calib_time = chip->qmax_time;

	return ret;
}

static int zy0602_get_calib_time(struct chip_bq27541 *chip, int *dod_calib_time, int *qmax_calib_time)
{
	int ret = 0;
	int check_args[CALIB_TIME_CHECK_ARGS] = { 0 };
	static bool init_flag = false;
	u8 extend_data[ZY602_MAC_CELL_DOD_QMAX_SIZE_A] = { 0 };

	if (!chip || !dod_calib_time || !qmax_calib_time)
		return -1;

	ret = gauge_read_i2c(chip, chip->cmd_addr.reg_dod0, &check_args[0]);
	if (ret < 0)
		return -1;

	mutex_lock(&chip->gauge_alt_manufacturer_access);
	ret = gauge_i2c_txsubcmd(chip, ZY602_MAC_CELL_QMAX_EN_ADDR, ZY602_MAC_CELL_QMAX_CMD);
	if (ret < 0) {
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		return -1;
	}
	usleep_range(1000, 1000);
	ret = gauge_read_i2c_block(chip, ZY602_MAC_CELL_QMAX_ADDR_A, ZY602_MAC_CELL_DOD_QMAX_SIZE_A, extend_data);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	if (ret < 0)
		return -1;

	check_args[1] = (extend_data[17] << 0x08) | extend_data[16];
	check_args[2] = (extend_data[19] << 0x08) | extend_data[18];
	if (!init_flag || check_args[0] != chip->calib_check_args_pre[0])
		chip->dod_time = 0;
	else
		chip->dod_time++;

	if (!init_flag || check_args[1] != chip->calib_check_args_pre[1] || check_args[2] != chip->calib_check_args_pre[2])
		chip->qmax_time = 0;
	else
		chip->qmax_time++;

	init_flag = true;
	memcpy(chip->calib_check_args_pre, check_args, sizeof(check_args));
	*dod_calib_time = chip->dod_time;
	*qmax_calib_time = chip->qmax_time;

	return ret;
}

static bool get_smem_sha256_batt_info(
	struct chip_bq27541 *chip, oplus_gauge_sha256_auth_result *sha256_auth)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	int ret = 0;

	if (!chip || !sha256_auth) {
		chg_err("invalid parameters\n");
		return false;
	}

	ret = get_auth_sha256_msg(sha256_auth->msg, sha256_auth->rcv_msg);
	if (!ret)
		return true;

	return false;
#else
	int i;
	size_t smem_size;
	void *smem_addr;
	oplus_gauge_auth_info_type *smem_data;

	if (!chip || !sha256_auth) {
		pr_err("%s: invalid parameters\n", __func__);
		return false;
	}

	memset(sha256_auth, 0, sizeof(oplus_gauge_sha256_auth_result));
	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_RESERVED_BOOT_INFO_FOR_APPS, &smem_size);
	if (IS_ERR(smem_addr)) {
		pr_err("unable to acquire smem SMEM_RESERVED_BOOT_INFO_FOR_APPS entry\n");
		return false;
	} else {
		smem_data = (oplus_gauge_auth_info_type *)smem_addr;
		if (smem_data == ERR_PTR(-EPROBE_DEFER)) {
			smem_data = NULL;
			pr_info("fail to get smem_data\n");
			return false;
		} else {
			if (chip->batt_bq27z561 || chip->batt_nfg1000a)
				memcpy(sha256_auth, &smem_data->sha256_rst_k0,
					sizeof(oplus_gauge_sha256_auth_result));

			for (i = 0; i < GAUGE_SHA256_AUTH_MSG_LEN - 3; i = i + 4)
				pr_info("%s: random[%d]=%x,random[%d]=%x,random[%d]=%x,random[%d]=%x\n",
					__func__, i, sha256_auth->msg[i], i + 1, sha256_auth->msg[i + 1], i + 2,
					sha256_auth->msg[i + 2], i + 3, sha256_auth->msg[i + 3]);
			for (i = 0; i < GAUGE_SHA256_AUTH_MSG_LEN - 3; i = i + 4)
				pr_info("%s: ap_encode[%d]=%x,ap_encode[%d]=%x,ap_encode[%d]=%x,ap_encode[%d]=%x\n",
					__func__, i, sha256_auth->rcv_msg[i], i + 1, sha256_auth->rcv_msg[i + 1], i + 2,
					sha256_auth->rcv_msg[i + 2], i + 3, sha256_auth->rcv_msg[i + 3]);
		}
	}
	return true;
#endif
}

static bool bq27z561_sha256_hmac_authenticate(struct chip_bq27541 *chip)
{
	int i;
	int ret;
	int count = 0;
	u8 checksum = 0;
	u8 data_buf[2] = {0};
	u8 *p_temp = chip->sha256_authenticate_data->random;
	int len = ARRAY_SIZE(chip->sha256_authenticate_data->random);
	int half_len = len / 2;

	mutex_lock(&chip->gauge_alt_manufacturer_access);
	for (i = 0; i < len; i++)
		checksum = checksum + chip->sha256_authenticate_data->random[i];
	checksum = 0xff - (checksum & 0xff);

	pr_info("%s start, len=%d, half_len:%d\n", __func__, len, half_len);
	ret = gauge_write_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, sizeof(data_buf), data_buf);
	if (ret < 0)
		goto error;

	ret = gauge_write_i2c_block(chip, BQ27Z561_AUTHENDATA_1ST, half_len, p_temp);
	if (ret < 0)
		goto error;

	ret = gauge_write_i2c_block(chip, BQ27Z561_AUTHENDATA_2ND, half_len, p_temp + half_len);
	if (ret < 0)
		goto error;

	ret = gauge_i2c_txsubcmd_onebyte(chip, BQ27Z561_AUTHENCHECKSUM, checksum);
	if (ret < 0)
		goto error;

	ret = gauge_i2c_txsubcmd_onebyte(chip, BQ27Z561_AUTHENLEN, 0x24);
	if (ret < 0)
		goto error;

	p_temp = chip->sha256_authenticate_data->gauge_encode;
try:
	msleep(60);
	ret = gauge_read_i2c_block(chip, BQ27Z561_AUTHENDATA_1ST, half_len, p_temp);
	if (ret < 0 && count < BQ27Z561_I2C_TRY_COUNT) {
		count++;
		pr_info("ret=%d, count=%d\n", ret, count);
		msleep(20);
		goto try;
	}
	ret = gauge_read_i2c_block(chip, BQ27Z561_AUTHENDATA_2ND, half_len, p_temp + half_len);
	if (ret < 0)
		goto error;
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return true;

error:
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return false;
}

static bool bq27541_sha256_hmac_result_check(struct chip_bq27541 *chip)
{
	int i;
	bool ret = false;
	int len = ARRAY_SIZE(chip->sha256_authenticate_data->random);
	u8 *ap_temp = chip->sha256_authenticate_data->ap_encode;
	u8 *gauge_temp = chip->sha256_authenticate_data->gauge_encode;

	for (i = 0; i < len - 3; i = i + 4)
		pr_info("%s: ap_encode[%d]=%x,ap_encode[%d]=%x,ap_encode[%d]=%x,ap_encode[%d]=%x\n",
			__func__, i, ap_temp[i], i + 1, ap_temp[i + 1], i + 2, ap_temp[i + 2], i + 3, ap_temp[i + 3]);
	for (i = 0; i < GAUGE_SHA256_AUTH_MSG_LEN - 3; i = i + 4)
		pr_info("%s: gauge_encode[%d]=%x,gauge_encode[%d]=%x,gauge_encode[%d]=%x,gauge_encode[%d]=%x\n",
			__func__, i, gauge_temp[i], i + 1, gauge_temp[i + 1], i + 2, gauge_temp[i + 2], i + 3, gauge_temp[i + 3]);
	if (strncmp(ap_temp, gauge_temp, len)) {
		pr_err("gauge authenticate compare failed\n");
		ret =  false;
	} else {
		pr_info("gauge authenticate succeed\n");
		ret = true;
	}

	return ret;
}

static int gauge_get_calib_time(int *dod_calib_time, int *qmax_calib_time, int gauge_index)
{
	int ret = 0;
	struct chip_bq27541 *chip;

	if (!gauge_index)
		chip = gauge_ic;
	else
		chip = sub_gauge_ic;

	if (!chip || !dod_calib_time || !qmax_calib_time)
		return -1;

	mutex_lock(&chip->calib_time_mutex);
	if (!oplus_vooc_get_allow_reading() || atomic_read(&chip->suspended) == 1 ||
	    atomic_read(&chip->gauge_i2c_status)) {
		*dod_calib_time = chip->dod_time_pre;
		*qmax_calib_time = chip->qmax_time_pre;
		goto error;
	}

	if (chip->batt_bq28z610 && DEVICE_BQ27541 == chip->device_type && !chip->batt_zy0603)
		ret = bq28z610_get_calib_time(chip, dod_calib_time, qmax_calib_time);
	else if (DEVICE_ZY0602 == chip->device_type)
		ret = zy0602_get_calib_time(chip, dod_calib_time, qmax_calib_time);
	else if (chip->batt_bq27z561)
		ret = bq27z561_get_calib_time(chip, dod_calib_time, qmax_calib_time);
	else if (chip->batt_nfg1000a)
		ret = nfg1000a_get_calib_time(chip, dod_calib_time, qmax_calib_time);

	if (ret < 0) {
		*dod_calib_time = chip->dod_time_pre;
		*qmax_calib_time = chip->qmax_time_pre;
		goto error;
	}

	chip->dod_time_pre = *dod_calib_time;
	chip->qmax_time_pre = *qmax_calib_time;

error:
	mutex_unlock(&chip->calib_time_mutex);

	return 0;
}

static bool init_sha256_gauge_auth(struct chip_bq27541 *chip)
{
	bool ret;
	oplus_gauge_sha256_auth_result sha256_auth;

	if (!chip || !chip->sha256_authenticate_data)
		return false;

	pr_info("%s start\n", __func__);
	ret = get_smem_sha256_batt_info(chip, &sha256_auth);
	if (!ret) {
		pr_err("get smem sha256 batt info failed\n");
		return ret;
	}

	memset(chip->sha256_authenticate_data, 0, sizeof(oplus_gauge_sha256_auth));
	memcpy(chip->sha256_authenticate_data->random,
		sha256_auth.msg, GAUGE_SHA256_AUTH_MSG_LEN);
	memcpy(chip->sha256_authenticate_data->ap_encode,
		sha256_auth.rcv_msg, GAUGE_SHA256_AUTH_MSG_LEN);
	if (chip->batt_bq27z561)
		ret = bq27z561_sha256_hmac_authenticate(chip);
	else if (chip->batt_nfg1000a)
		ret = nfg1000a_sha256_hmac_authenticate(chip);
	else
		ret = false;
	if (!ret)
		return ret;

	ret = bq27541_sha256_hmac_result_check(chip);

	return ret;
}

static bool init_gauge_auth(oplus_gauge_auth_result *rst, struct bq27541_authenticate_data *authenticate_data)
{
	int len = GAUGE_AUTH_MSG_LEN < AUTHEN_MESSAGE_MAX_COUNT ? GAUGE_AUTH_MSG_LEN : AUTHEN_MESSAGE_MAX_COUNT;
	if (NULL == rst || NULL == authenticate_data) {
		pr_err("Gauge authenticate failed\n");
		return false;
	}

	memset(authenticate_data, 0, sizeof(struct bq27541_authenticate_data));
	authenticate_data->message_len = len;
	memcpy(authenticate_data->message, rst->msg, len);
	bq27541_sha1_hmac_authenticate(authenticate_data);

	if (strncmp(authenticate_data->message, rst->rcv_msg, len)) {
		pr_err("Gauge authenticate compare failed\n");
		return false;
	} else {
		pr_err("Gauge authenticate succeed\n");
		authenticate_data->result = 1;
		rst->result = 1;
	}
	return true;
}

static void register_gauge_devinfo(struct chip_bq27541 *chip)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int ret = 0;
	char *version;
	char *manufacture;

	switch (chip->device_type) {
	case DEVICE_BQ27541:
		if (chip->batt_zy0603) {
			version = "zy0603";
			manufacture = "ZY";
		} else {
			version = "bq27541";
			manufacture = "TI";
		}
		break;
	case DEVICE_BQ27411:
		version = "bq27411";
		manufacture = "TI";
		break;
	case DEVICE_BQ27426:
		version = "bq27426";
		manufacture = "TI";
		break;
	case DEVICE_ZY0602:
		version = "zy0602";
		manufacture = "ZY";
		break;
	default:
		version = "unknown";
		manufacture = "UNKNOWN";
		break;
	}
	if (chip->gauge_num == 0) {
		ret = register_device_proc("gauge", version, manufacture);
	} else {
		ret = register_device_proc("sub_gauge", version, manufacture);
	}
	if (ret) {
		pr_err("register_gauge_devinfo fail\n");
	}
#endif
}

static void bq27541_reset(struct i2c_client *client_chip)
{
	int ui_soc = oplus_chg_get_ui_soc();
	struct chip_bq27541 *bq27541_chip = i2c_get_clientdata(client_chip);
	struct oplus_gauge_chip *gauge_chip;

	if (IS_ERR_OR_NULL(bq27541_chip))
		return;

	atomic_set(&bq27541_chip->shutdown, 1);
	gauge_chip = bq27541_chip->oplus_gauge;

	if (bq27541_chip->track_mode.mode_check_buf) {
		kfree(bq27541_chip->track_mode.mode_check_buf);
		bq27541_chip->track_mode.mode_check_buf = NULL;
	}

	if (bq27541_chip->batt_zy0603 || bq27541_chip->batt_bq27z561 || bq27541_chip->batt_nfg1000a)
		return;

	if (gauge_chip->gauge_ops->get_battery_mvolts() <= 3300 && gauge_chip->gauge_ops->get_battery_mvolts() > 2500 &&
	    ui_soc == 0 && gauge_chip->gauge_ops->get_battery_temperature() > 150) {
		if (!unseal(bq27541_chip, BQ27541_UNSEAL_KEY)) {
			pr_err("bq27541 unseal fail !\n");
			return;
		}
		chg_debug("bq27541 unseal OK vol = %d, ui_soc = %d, temp = %d!\n",
			  gauge_chip->gauge_ops->get_battery_mvolts(), ui_soc,
			  gauge_chip->gauge_ops->get_battery_temperature());
		if (bq27541_chip->device_type == DEVICE_BQ27541 || bq27541_chip->device_type == DEVICE_ZY0602) {
			bq27541_cntl_cmd(bq27541_chip, BQ27541_RESET_SUBCMD);
		} else if (bq27541_chip->device_type == DEVICE_BQ27411) {
			bq27541_cntl_cmd(bq27541_chip, BQ27411_RESET_SUBCMD); /*  27411  */
		}
		msleep(50);
		if (bq27541_chip->device_type == DEVICE_BQ27411) {
			if (!seal(bq27541_chip)) {
				pr_err("bq27411 seal fail\n");
			}
		} else if (bq27541_chip->device_type == DEVICE_BQ27426) {
			if (!bq27426_seal(bq27541_chip)) {
				pr_err("bq27426 seal fail\n");
			}
		}
		msleep(150);
		chg_debug("bq27541_reset, point = %d\r\n", gauge_chip->gauge_ops->get_battery_soc());
	} else if (bq27541_chip) {
		if ((bq27541_chip->device_type == DEVICE_ZY0602) && (bq27541_chip->gauge_fix_cadc))
			fg_gauge_enable_sleep_mode(bq27541_chip);
		else
			bq27411_modify_soc_smooth_parameter(bq27541_chip, false);
	}
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int bq27541_pm_resume(struct device *dev)
{
	struct chip_bq27541 *bq27541_chip = dev_get_drvdata(dev);
	if (!bq27541_chip) {
		return 0;
	}
	atomic_set(&bq27541_chip->suspended, 0);
	if (bq27541_chip->gauge_num == 0) {
		bq27541_get_battery_soc();
	} else {
		bq27541_get_sub_battery_soc();
	}
	return 0;
}

static int bq27541_pm_suspend(struct device *dev)
{
	struct chip_bq27541 *bq27541_chip = dev_get_drvdata(dev);
	if (!bq27541_chip) {
		return 0;
	}
	atomic_set(&bq27541_chip->suspended, 1);
	return 0;
}

static const struct dev_pm_ops bq27541_pm_ops = {
	.resume = bq27541_pm_resume,
	.suspend = bq27541_pm_suspend,
};

#else /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/
static int bq27541_resume(struct i2c_client *client)
{
	struct chip_bq27541 *bq27541_chip = i2c_get_clientdata(client);
	if (!bq27541_chip) {
		return 0;
	}
	atomic_set(&bq27541_chip->suspended, 0);
	if (bq27541_chip->gauge_num == 0) {
		bq27541_get_battery_soc();
	} else {
		bq27541_get_sub_battery_soc();
	}
	return 0;
}

static int bq27541_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct chip_bq27541 *bq27541_chip = i2c_get_clientdata(client);
	if (!bq27541_chip) {
		return 0;
	}
	atomic_set(&bq27541_chip->suspended, 1);
	return 0;
}
#endif /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/

bool oplus_gauge_ic_chip_is_null(void)
{
	if (!gauge_ic) {
		return true;
	} else {
		return false;
	}
}

static int oplus_start_extern_hmac_test(int count)
{
	struct oplus_external_auth_chip *chip = external_auth_chip;

	if (!chip) {
		return -1;
	}

	cancel_delayed_work_sync(&chip->test_work);
	chip->test_result.test_count_now = 0;
	chip->test_result.test_count_total = count;
	chip->test_result.test_fail_count = 0;
	chip->test_result.real_test_count_now = 0;
	chip->test_result.real_test_fail_count = 0;
	schedule_delayed_work_on(7, &chip->test_work, 0);

	return 0;
}

static int oplus_get_extern_hmac_test_result(int *count_total, int *count_now, int *fail_count)
{
	struct oplus_external_auth_chip *chip = external_auth_chip;

	if (!chip) {
		return -1;
	}

	*count_total = chip->test_result.test_count_total;
	*count_now = chip->test_result.test_count_now;
	*fail_count = chip->test_result.test_fail_count;
	chg_err("count_total:%d,count_now:%d,fail_count:%d,real_count:%d,real_fail:%d\n", *count_total, *count_now,
		*fail_count, chip->test_result.real_test_count_now, chip->test_result.real_test_fail_count);
	return 0;
}

static void oplus_extern_auth_test_func(struct work_struct *work)
{
	struct oplus_external_auth_chip *chip = external_auth_chip;
	int ret = 0;

	if (!chip) {
		return;
	}

	while (chip->test_result.test_count_now < chip->test_result.test_count_total) {
		chip->test_result.test_count_now++;
		ret = bq27541_get_battery_hmac();
		if (!ret) {
			chip->test_result.test_fail_count++;
		}
	}
}

static int bq27541_track_upload_mode_info(struct chip_bq27541 *chip)
{
	int index = 0;
	struct gauge_track_mode_info *p_track_mode;

	if (!chip)
		return -EINVAL;

	if (!chip->gauge_cal_board && !chip->gauge_check_model && !chip->gauge_check_por)
		return -EINVAL;

	p_track_mode = &chip->track_mode;
	if (!p_track_mode->track_init_done || !p_track_mode->mode_check_buf)
		return -EINVAL;

	if (!strlen(p_track_mode->mode_check_buf))
		return -EINVAL;

	mutex_lock(&p_track_mode->track_lock);
	if (p_track_mode->uploading) {
		pr_info("uploading, should return\n");
		mutex_unlock(&p_track_mode->track_lock);
		goto no_upload;
	}

	if (p_track_mode->load_trigger)
		kfree(p_track_mode->load_trigger);
	p_track_mode->load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!p_track_mode->load_trigger) {
		pr_err("gauge load_trigger memery alloc fail\n");
		mutex_unlock(&p_track_mode->track_lock);
		goto no_upload;
	}

	p_track_mode->load_trigger->type_reason = TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	p_track_mode->load_trigger->flag_reason = TRACK_NOTIFY_FLAG_GAUGE_MODE;
	p_track_mode->uploading = true;
	mutex_unlock(&p_track_mode->track_lock);

	if(chip->capacity_pct == 0) {
		index += snprintf(&(p_track_mode->load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$device_id@@%s", chip->device_name);
	} else {
		index += snprintf(&(p_track_mode->load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$device_id@@%s_%d", chip->device_name, chip->gauge_num);
	}
	if (p_track_mode->mode_check_buf) {
		index += snprintf(&(p_track_mode->load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"%s", p_track_mode->mode_check_buf);
		mutex_lock(&p_track_mode->buf_lock);
		memset(p_track_mode->mode_check_buf, 0, MODE_CHECK_MAX_LENGTH);
		mutex_unlock(&p_track_mode->buf_lock);
	}

	schedule_delayed_work(&p_track_mode->load_trigger_work, 0);
	pr_debug("success\n");
	return 0;

no_upload:
	if (p_track_mode->mode_check_buf) {
		mutex_lock(&p_track_mode->buf_lock);
		memset(p_track_mode->mode_check_buf, 0, MODE_CHECK_MAX_LENGTH);
		mutex_unlock(&p_track_mode->buf_lock);
	}
	return -EFAULT;
}

static void bq27541_track_mode_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_bq27541 *chip =
		container_of(dwork, struct chip_bq27541, track_mode.load_trigger_work);

	if (!chip->track_mode.load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->track_mode.load_trigger));
	kfree(chip->track_mode.load_trigger);
	chip->track_mode.load_trigger = NULL;
	chip->track_mode.uploading = false;
}

int bq27541_track_update_mode_buf(struct chip_bq27541 *chip, char *buf)
{
	int str_len;

	if (!chip || !chip->track_mode.track_init_done ||
			!buf || !chip->track_mode.mode_check_buf)
		return -EFAULT;

	mutex_lock(&chip->track_mode.buf_lock);
	str_len = strlen(chip->track_mode.mode_check_buf);
	if (str_len >= MODE_CHECK_MAX_LENGTH) {
		mutex_unlock(&chip->track_mode.buf_lock);
		pr_info("mode_check_buf arrive max\n");
		return -EFAULT;
	}
	pr_info("str_len=%d, len=%lu, %s\n", str_len, strlen(buf), buf);
	snprintf(&(chip->track_mode.mode_check_buf[str_len]), MODE_CHECK_MAX_LENGTH - str_len - 1, "%s", buf);
	mutex_unlock(&chip->track_mode.buf_lock);
	return 0;
}

static int bq27541_track_init(struct chip_bq27541 *chip)
{
	if (!chip)
		return -EFAULT;

	chip->track_mode.mode_check_buf = kzalloc(MODE_CHECK_MAX_LENGTH, GFP_KERNEL);
	if (!chip->track_mode.mode_check_buf)
		return -ENOMEM;
	mutex_init(&chip->track_mode.track_lock);
	mutex_init(&chip->track_mode.buf_lock);
	INIT_DELAYED_WORK(&chip->track_mode.load_trigger_work,
		bq27541_track_mode_load_trigger_work);
	chip->track_mode.track_init_done = true;

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))
static int bq27541_driver_probe(struct i2c_client *client)
#else
static int bq27541_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct chip_bq27541 *fg_ic;
	struct oplus_gauge_chip *chip;
	int rerun_num = 3;
	int rc = 0;

	if (!oplus_gauge_check_chip_is_null()) {
		chg_err("gauge chip_is not null, skip %s\n", __func__);
		return -ENOMEM;
	}

	fg_ic = kzalloc(sizeof(*fg_ic), GFP_KERNEL);
	if (!fg_ic) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, fg_ic);
	fg_ic->dev = &client->dev;
	fg_ic->client = client;
	atomic_set(&fg_ic->suspended, 0);
	atomic_set(&fg_ic->shutdown, 0);
	mutex_init(&fg_ic->chip_mutex);
	mutex_init(&fg_ic->calib_time_mutex);
	mutex_init(&fg_ic->gauge_alt_manufacturer_access);
	bq27541_parse_dt(fg_ic);

	if (fg_ic->gauge_num == 0) {
		gauge_ic = fg_ic;
	} else {
		sub_gauge_ic = fg_ic;
	}
rerun:
	rerun_num--;
	rc = bq27541_hw_config(fg_ic);
	if (rc < 0) {
		dev_err(&client->dev, "bq27541_hw_config fail! bq27541_driver_probe fail! \n");
		return -EFAULT;
	}

	if (fg_ic->batt_zy0603) {
		rc = zy0603_parse_afi_buf(fg_ic);
		dev_err(&client->dev, "zy0603 support and can't config gauge param\n");
	} else {
		dev_err(&client->dev, "2020 0924 param\n");
		if (!fg_ic->modify_soc_calibration) {
			bq28z610_batt_full_zero_parameter(fg_ic);
		}
	}

	if (fg_ic->device_type == DEVICE_BQ27426) {
		fg_ic->bqfs_info.bqfs_status = false;
		rc = bqfs_init(fg_ic);
		if (rc == -EPROBE_DEFER) {
			kfree(fg_ic);
			dev_err(&client->dev, "bqfs_init fail, will retry later!\n");
			return rc;
		}
	} else {
		fg_ic->bqfs_info.bqfs_status = true;
	}

	fg_ic->soc_pre = 50;
	if (fg_ic->batt_bq28z610) {
		fg_ic->batt_vol_pre = 3800;
		fg_ic->fc_pre = 0;
		fg_ic->qm_pre = 0;
		fg_ic->pd_pre = 0;
		fg_ic->rcu_pre = 0;
		fg_ic->rcf_pre = 0;
		fg_ic->fcu_pre = 0;
		fg_ic->fcf_pre = 0;
		fg_ic->sou_pre = 0;
		fg_ic->do0_pre = 0;
		fg_ic->doe_pre = 0;
		fg_ic->trm_pre = 0;
		fg_ic->pc_pre = 0;
		fg_ic->qs_pre = 0;
	} else {
		fg_ic->batt_vol_pre = 3800;
		fg_ic->fc_pre = 0;
		fg_ic->qm_pre = 0;
		fg_ic->pd_pre = 0;
		fg_ic->rcu_pre = 0;
		fg_ic->rcf_pre = 0;
		fg_ic->fcu_pre = 0;
		fg_ic->fcf_pre = 0;
		fg_ic->sou_pre = 0;
		fg_ic->do0_pre = 0;
		fg_ic->doe_pre = 0;
		fg_ic->trm_pre = 0;
		fg_ic->pc_pre = 0;
		fg_ic->qs_pre = 0;
	}
	fg_ic->max_vol_pre = 3800;
	fg_ic->min_vol_pre = 3800;
	fg_ic->current_pre = 999;
	fg_ic->bq27z561_seal_flag = 0;

	/*for zy0603 battery protect cuase turn off mos*/
	fg_ic->protect_check_done = false;
	fg_ic->afi_update_done = true;
	fg_ic->disabled = false;
	fg_ic->error_occured = false;
	fg_ic->need_check = true;
	INIT_DELAYED_WORK(&fg_ic->afi_update, zy0603_afi_update_work);
	rc = bq27411_modify_soc_smooth_parameter(fg_ic, true);
	fg_ic->protect_check_done = true;

	if (rc == BATT_FULL_ERROR && rerun_num > 0)
		goto rerun; /* only for wite battery full param in guage dirver probe on 7250 platform */
	chip = devm_kzalloc(&client->dev, sizeof(struct oplus_gauge_chip), GFP_KERNEL);
	if (!chip) {
		pr_err("kzalloc() failed.\n");
		if (fg_ic->gauge_num == 0) {
			gauge_ic = NULL;
		} else {
			sub_gauge_ic = NULL;
		}
		return -ENOMEM;
	}

	if (fg_ic->bq28z610_need_balancing)
		fg_ic->bq28z610_device_chem = bq28z610_get_device_chemistry(fg_ic);

	if (fg_ic->gauge_num == 0) {
		chg_err("gauge_init:\n");
		fg_ic->oplus_gauge = chip;
		oplus_gauge_init(chip);
		register_gauge_devinfo(fg_ic);
		chip->client = client;
		chip->dev = &client->dev;
		chip->gauge_ops = &bq27541_gauge_ops;
		chip->device_type = gauge_ic->device_type;
		chip->device_name = gauge_ic->device_name;
		chip->device_type_for_vooc = gauge_ic->device_type_for_vooc;
		chip->capacity_pct = gauge_ic->capacity_pct;
		atomic_set(&gauge_ic->gauge_i2c_status, 0);
		if (!gauge_ic->support_sha256_hmac)
			gauge_ic->authenticate_data =
				devm_kzalloc(&client->dev, sizeof(struct bq27541_authenticate_data), GFP_KERNEL);
		else
			gauge_ic->sha256_authenticate_data =
				devm_kzalloc(&client->dev, sizeof(oplus_gauge_sha256_auth), GFP_KERNEL);
		if (!gauge_ic->authenticate_data && !gauge_ic->sha256_authenticate_data) {
			pr_err("kzalloc() authenticate_data failed.\n");
			gauge_ic = NULL;
			return -ENOMEM;
		}
		sh366002_init(gauge_ic);
	} else {
		chg_err("sub_gauge_init:\n");
		fg_ic->oplus_gauge = chip;
		oplus_sub_gauge_init(chip);
		register_gauge_devinfo(fg_ic);
		chip->client = client;
		chip->dev = &client->dev;
		chip->gauge_ops = &bq27541_sub_gauge_ops;
		chip->device_type = sub_gauge_ic->device_type;
		chip->device_type_for_vooc = sub_gauge_ic->device_type_for_vooc;
		chip->capacity_pct = sub_gauge_ic->capacity_pct;
		atomic_set(&sub_gauge_ic->gauge_i2c_status, 0);
		sub_gauge_ic->authenticate_data =
			devm_kzalloc(&client->dev, sizeof(struct bq27541_authenticate_data), GFP_KERNEL);
		if (!sub_gauge_ic->authenticate_data) {
			pr_err("kzalloc() authenticate_data failed.\n");
			sub_gauge_ic = NULL;
			return -ENOMEM;
		}
		sh366002_init(sub_gauge_ic);
	}

	/*Add for support test the auth of hmac*/
	if (gauge_ic && (gauge_ic->sha1_key_index == ZY0602_KEY_INDEX) && NULL == external_auth_chip) {
		external_auth_chip = kzalloc(sizeof(struct oplus_external_auth_chip), GFP_KERNEL);
		if (external_auth_chip) {
			INIT_DELAYED_WORK(&external_auth_chip->test_work, oplus_extern_auth_test_func);
			external_auth_chip->test_result.test_count_total = 0;
			external_auth_chip->test_result.test_fail_count = 0;
			external_auth_chip->test_result.test_count_now = 0;
			external_auth_chip->test_result.real_test_count_now = 0;
			external_auth_chip->test_result.real_test_fail_count = 0;
			external_auth_chip->start_test_external_hmac = oplus_start_extern_hmac_test;
			external_auth_chip->get_hmac_test_result = oplus_get_extern_hmac_test_result;
			oplus_external_auth_init(external_auth_chip);
		}
	}

	if (fg_ic->support_extern_cmd && fg_ic->batt_bq27z561) {
		oplus_bq27541_get_batt_sn(fg_ic);

		INIT_DELAYED_WORK(&fg_ic->get_manu_battinfo_work, oplus_get_manu_battinfo_work);
		schedule_delayed_work(&fg_ic->get_manu_battinfo_work,
			round_jiffies_relative(msecs_to_jiffies(100)));
	}

	bq27541_track_init(fg_ic);

	chg_debug("bq27541_driver_probe success\n");
	return 0;
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/

static const struct of_device_id bq27541_match[] = {
	{ .compatible = "oplus,bq27541-battery" },
	{},
};

static const struct i2c_device_id bq27541_id[] = {
	{ "bq27541-battery", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq27541_id);

static struct i2c_driver bq27541_i2c_driver = {
	.driver = {
	.name = "bq27541-battery",
	.owner	= THIS_MODULE,
	.of_match_table = bq27541_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	.pm = &bq27541_pm_ops,
#endif
	},
	.probe = bq27541_driver_probe,
	.shutdown	= bq27541_reset,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume	 = bq27541_resume,
	.suspend	= bq27541_suspend,
#endif
	.id_table	= bq27541_id,
};
/*----------------------------------------------------------------------------*/
/*static void  bq27541_exit(void)
{
	i2c_del_driver(&bq27541_i2c_driver);
}*/
/*----------------------------------------------------------------------------*/

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(bq27541_i2c_driver);
#else
int bq27541_driver_init(void)
{
	int ret = 0;

	chg_debug(" start\n");

	if (i2c_add_driver(&bq27541_i2c_driver) != 0) {
		chg_err(" failed to register bq27541 i2c driver.\n");
	} else {
		chg_debug(" Success to register bq27541 i2c driver.\n");
	}

	return ret;
}

void bq27541_driver_exit(void)
{
	i2c_del_driver(&bq27541_i2c_driver);
}
oplus_chg_module_register(bq27541_driver);
#endif
MODULE_DESCRIPTION("Driver for bq27541 charger chip");
MODULE_LICENSE("GPL v2");
