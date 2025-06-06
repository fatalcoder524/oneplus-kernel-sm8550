// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[BQ27541]([%s][%d]): " fmt, __func__, __LINE__

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
#include <linux/power_supply.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

/* #include <mt-plat/battery_common.h> */
#include <linux/proc_fs.h>
#include <linux/init.h>
#else
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
#include <linux/proc_fs.h>
#include <linux/soc/qcom/smem.h>
#endif
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/device_info.h>
#endif
#include <linux/version.h>
#include<linux/gfp.h>
#include <linux/pinctrl/consumer.h>
#ifdef OPLUS_SHA1_HMAC
#include <linux/random.h>
#endif
#include <oplus_chg_module.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_vooc.h>
#include <oplus_mms.h>
#include <oplus_mms_gauge.h>
#include <oplus_mms_wired.h>
#include "test-kit.h"

#include "oplus_hal_bq27541.h"
#include "oplus_hal_nfg8011b.h"

#define GAUGE_ERROR		(-1)
#define GAUGE_OK		0
#define BATT_FULL_ERROR		2
#define XBL_AUTH_DEBUG
#define BCC_TYPE_IS_SVOOC	1
#define BCC_TYPE_IS_VOOC	0
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
#define SOC_MIN			0
#define SOC_MAX			100
#define RETRY_CNT		3

static const char *gpio_status_name[] = {
	[GPIO_STATUS_NC] = "not connect",
	[GPIO_STATUS_PD] = "pull down",
	[GPIO_STATUS_PU] = "pull up",
	[GPIO_STATUS_NOT_SUPPORT] = "not support",
};

static DEFINE_MUTEX(bq27541_i2c_access);
static bool get_smem_batt_info(oplus_gauge_auth_result *auth, int kk);
static bool
init_gauge_auth(struct chip_bq27541 *chip, oplus_gauge_auth_result *rst,
		struct bq27541_authenticate_data *authenticate_data);
static int zy0603_unseal(struct chip_bq27541 *chip);
static int zy0603_seal(struct chip_bq27541 *chip);
static bool zy0603_afi_update_done(struct chip_bq27541 *chip);
int oplus_vooc_get_fast_chg_type(void);
int oplus_vooc_get_fastchg_ing(void);
int oplus_vooc_get_vooc_by_normal_path(void);
static int zy0603_static_checksum_check(struct chip_bq27541 *chip);
static int zy0603_QmaxFgStatus_check(struct chip_bq27541 *chip);
static bool init_sha256_gauge_auth(struct chip_bq27541 *chip);

#ifndef I2C_ERR_MAX
#define I2C_ERR_MAX 2
#endif

static int oplus_bq27541_init(struct oplus_chg_ic_dev *ic_dev);
static int oplus_bq27541_exit(struct oplus_chg_ic_dev *ic_dev);

#define CHECK_IIC_RECOVER_TIME 5000
static __inline__ void bq27541_push_i2c_err(struct chip_bq27541 *chip, bool read)
{
	if (unlikely(!chip->ic_dev))
		return;
	if (unlikely(!chip->ic_dev->online))
		return;

	chip->i2c_err = true;
	if (atomic_read(&chip->i2c_err_count) > I2C_ERR_MAX)
		return;

	atomic_inc(&chip->i2c_err_count);
	if (atomic_read(&chip->i2c_err_count) > I2C_ERR_MAX) {
		oplus_bq27541_exit(chip->ic_dev);
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_OFFLINE);
		schedule_delayed_work(&chip->check_iic_recover, msecs_to_jiffies(CHECK_IIC_RECOVER_TIME));
	} else {
		oplus_chg_ic_creat_err_msg(chip->ic_dev, OPLUS_IC_ERR_I2C, 0,
					   read ? "read error" : "write error");
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
	}
}

static __inline__ void bq27541_i2c_err_clr(struct chip_bq27541 *chip)
{
	if (unlikely(chip->i2c_err)) {
		/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
		if (chip->i2c_rst_ext && chip->err_status)
			return;
		/* end workaround 230504153935012779 */
		chip->i2c_err = false;
		atomic_set(&chip->i2c_err_count, 0);
		oplus_bq27541_init(chip->ic_dev);
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ONLINE);
	}
}

#ifndef CONFIG_OPLUS_CHARGER_MTK
#if IS_ENABLED(CONFIG_OPLUS_ADSP_CHARGER)
void __attribute__((weak)) oplus_vooc_get_fastchg_started_pfunc(int (*pfunc)(void))
{
	return;
}
void __attribute__((weak)) oplus_vooc_get_fastchg_ing_pfunc(int (*pfunc)(void))
{
	return;
}
int __attribute__((weak)) oplus_get_fg_device_type(void)
{
	return 0;
}

void __attribute__((weak)) oplus_set_fg_device_type(int device_type)
{
	return;
}
#else /*IS_ENABLED(CONFIG_OPLUS_ADSP_CHARGER)*/
void __attribute__((weak)) oplus_vooc_get_fastchg_started_pfunc(int (*pfunc)(void));
void __attribute__((weak)) oplus_vooc_get_fastchg_ing_pfunc(int (*pfunc)(void));
int __attribute__((weak)) oplus_get_fg_device_type(void);
void __attribute__((weak)) oplus_set_fg_device_type(int device_type);
#endif /*IS_ENABLED(CONFIG_OPLUS_ADSP_CHARGER)*/
#else
int __attribute__((weak)) oplus_get_fg_device_type(void)
{
	return 0;
}

void __attribute__((weak)) oplus_set_fg_device_type(int device_type)
{
	return;
}
#endif

#ifdef CONFIG_OPLUS_CHARGER_MTK
/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
#ifdef CONFIG_OPLUS_FG_ERROR_RESET_I2C
/* this workaround only for flamingo, for scanning tool issue */
void __attribute__((weak)) oplus_set_fg_err_flag(struct i2c_adapter *adap, bool flag);
#endif
/* end workaround 230504153935012779 */
#endif
/**********************************************************
  *
  *   [I2C Function For Read/Write bq27541]
  *
  *********************************************************/

static bool normal_range_judge(int max, int min, int data)
{
	if (data <= max && data >= min)
		return true;
	else
		return false;
}

int oplus_vooc_get_fastchg_started(void)
{
	int fastchg_started_status = 0;
	struct oplus_mms *vooc_topic;
	union mms_msg_data data = { 0 };
	int rc;

	vooc_topic = oplus_mms_get_by_name("vooc");
	if (!vooc_topic)
		return 0;

	rc = oplus_mms_get_item_data(vooc_topic, VOOC_ITEM_VOOC_STARTED, &data, true);
	if (!rc)
		fastchg_started_status = data.intval;

	chg_err("get fastchg started status = %d\n", fastchg_started_status);
	return fastchg_started_status;
}

int bq27541_read_i2c(struct chip_bq27541 *chip, int cmd, int *returnData)
{
	int retry = 4;
	int retry_c = 3;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	int rc;
#endif

	if (!chip->client) {
		pr_err(" gauge_ic->client NULL, return\n");
		return 0;
	}
	if (oplus_is_rf_ftm_mode())
		return 0;
	if (cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;
	mutex_lock(&chip->chip_mutex);
	/* gauge_ic->client->timing = 300; */
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_read(chip->odb, cmd, returnData, 2);
	if (rc < 0)
		*returnData = rc;
#else
	*returnData = i2c_smbus_read_word_data(chip->client, cmd);
#endif
	if (chip->device_type == DEVICE_ZY0602 ||
	    cmd == BQ27541_BQ27411_REG_CNTL) {
		if (*returnData < 0) {
			while (retry > 0) {
				usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
				rc = oplus_dev_bus_bulk_read(chip->odb, cmd, returnData, 2);
				if (rc < 0)
					*returnData = rc;
#else
				*returnData = i2c_smbus_read_word_data(
					chip->client, cmd);
#endif
				if (*returnData < 0) {
					retry--;
				} else {
					break;
				}
			}
		}
	} else {
		if (*returnData < 0) {
			while(retry_c > 0) {
				usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
				rc = oplus_dev_bus_bulk_read(chip->odb, cmd, returnData, 2);
				if (rc < 0)
					*returnData = rc;
#else
				*returnData = i2c_smbus_read_word_data(
					chip->client, cmd);
#endif
				if (*returnData < 0) {
					retry_c--;
				} else {
					break;
				}
			}
		}
	}

	mutex_unlock(&chip->chip_mutex);
	/*pr_err(" cmd = 0x%x, returnData = 0x%x\r\n", cmd, *returnData) ;*/
	if (*returnData < 0) {
		chg_err("read err, rc = %d\n", *returnData);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		if (chip->i2c_rst_ext) {
			chip->err_status = false;
#ifdef CONFIG_OPLUS_FG_ERROR_RESET_I2C
/* this workaround only for flamingo, for scanning tool issue */
			oplus_set_fg_err_flag(chip->client->adapter, false);
#endif
		}
#endif
		bq27541_push_i2c_err(chip, true);
		return 1;
	} else {
		bq27541_i2c_err_clr(chip);
		return 0;
	}
}

static int bq28z610_i2c_deep_int(struct chip_bq27541 *chip)
{
	int rc = 0;

	if (!chip->client) {
		pr_err(" gauge_ic->client NULL, return\n");
		return 0;
	}
	if (oplus_is_rf_ftm_mode())
		return 0;
	mutex_lock(&chip->chip_mutex);
	rc = i2c_smbus_write_word_data(chip->client, BQ28Z610_REG_CNTL1, BQ28Z610_UNSEAL_SUBCMD1);
	usleep_range(10000, 10000);
	rc = i2c_smbus_write_word_data(chip->client, BQ28Z610_REG_CNTL1, BQ28Z610_UNSEAL_SUBCMD2);
	if (rc < 0)
		chg_err("write err, rc = %d\n", rc);

	mutex_unlock(&chip->chip_mutex);
	return 0;
}

int bq27541_i2c_txsubcmd(struct chip_bq27541 *chip, int cmd, int writeData)
{
	int rc = 0;
	int retry = 3;

	if (!chip->client) {
		pr_err(" gauge_ic->client NULL, return\n");
		return 0;
	}
	if (oplus_is_rf_ftm_mode())
		return 0;
	if (cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;
	mutex_lock(&chip->chip_mutex);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_write(chip->odb, cmd, &writeData, 2);
#else
	rc = i2c_smbus_write_word_data(chip->client, cmd, writeData);
#endif
	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
			rc = oplus_dev_bus_bulk_write(chip->odb, cmd, &writeData, 2);
#else
			rc = i2c_smbus_write_word_data(
				chip->client, cmd, writeData);
#endif
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (rc < 0) {
		chg_err("write err, rc = %d\n", rc);
		bq27541_push_i2c_err(chip, false);
		mutex_unlock(&chip->chip_mutex);
		return -1;
	} else {
		bq27541_i2c_err_clr(chip);
	}
	mutex_unlock(&chip->chip_mutex);
	return 0;
}

int bq27541_write_i2c_block(struct chip_bq27541 *chip, u8 cmd, u8 length, u8 *writeData)
{
	int rc = 0;
	int retry = 3;

	if (!chip->client) {
		pr_err(" gauge_ic->client NULL, return\n");
		return 0;
	}
	if (oplus_is_rf_ftm_mode())
		return 0;
	if (cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;
	mutex_lock(&chip->chip_mutex);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_write(chip->odb, cmd, writeData, length);
#else
	rc = i2c_smbus_write_i2c_block_data(chip->client, cmd, length,
					    writeData);
#endif
	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
			rc = oplus_dev_bus_bulk_write(chip->odb, cmd, writeData, length);
#else
			rc = i2c_smbus_write_i2c_block_data(chip->client,
				cmd, length, writeData);
#endif
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (rc < 0) {
		chg_err("write err, rc = %d\n", rc);
		bq27541_push_i2c_err(chip, false);
	} else {
		bq27541_i2c_err_clr(chip);
	}
	mutex_unlock(&chip->chip_mutex);
	return 0;
}

static int bq27541_write_i2c_block_unsafe(struct chip_bq27541 *chip, u8 cmd, u8 length, u8 *writeData)
{
	int rc = 0;
	int retry = 3;

	if (!chip->client) {
		pr_err(" gauge_ic->client NULL, return\n");
		return 0;
	}
	if (oplus_is_rf_ftm_mode())
		return 0;
	if (cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_write(chip->odb, cmd, writeData, length);
#else
	rc = i2c_smbus_write_i2c_block_data(chip->client, cmd, length, writeData);
#endif
	if (rc < 0) {
		while (retry > 0) {
			usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
			rc = oplus_dev_bus_bulk_write(chip->odb, cmd, writeData, length);
#else
			rc = i2c_smbus_write_i2c_block_data(chip->client, cmd, length, writeData);
#endif
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (rc < 0) {
		chg_err("write err, rc = %d\n", rc);
		bq27541_push_i2c_err(chip, false);
	} else {
		bq27541_i2c_err_clr(chip);
	}
	return 0;
}

#define I2C_SMBUS_BLOCK_MAX	32
int bq27541_read_i2c_block(struct chip_bq27541 *chip, u8 cmd, u8 length, u8 *returnData)
{
	int rc = 0;
	int retry = 3;

	if (!chip->client) {
		pr_err(" gauge_ic->client NULL,return\n");
		return 0;
	}
	if (oplus_is_rf_ftm_mode())
		return 0;
	if (cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;
	mutex_lock(&chip->chip_mutex);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_read(chip->odb, cmd, returnData, length);
#else
	rc = i2c_smbus_read_i2c_block_data(chip->client, cmd, length,
					   returnData);
#endif
	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
			rc = oplus_dev_bus_bulk_read(chip->odb, cmd, returnData, length);
#else
			rc = i2c_smbus_read_i2c_block_data(chip->client,
				cmd, length, returnData);
#endif
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (rc < 0) {
		chg_err("read err, rc = %d\n", rc);
		bq27541_push_i2c_err(chip, true);
		mutex_unlock(&chip->chip_mutex);
		return -1;
	} else {
		bq27541_i2c_err_clr(chip);
	}
	mutex_unlock(&chip->chip_mutex);
	/* pr_err(" cmd = 0x%x, returnData = 0x%x\r\n",cmd,*returnData)  ; */
	return 0;
}

static int bq27541_read_i2c_onebyte(struct chip_bq27541 *chip, u8 cmd, u8 *returnData)
{
	int rc;
	int retry = 3;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	unsigned int buf;
#endif

	if (!chip->client) {
		pr_err(" gauge_ic->client NULL, return\n");
		return 0;
	}
	if (oplus_is_rf_ftm_mode())
		return 0;
	if (cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;
	mutex_lock(&chip->chip_mutex);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_read(chip->odb, cmd, &buf);
	if (rc >= 0)
		rc = buf;
#else
	rc = i2c_smbus_read_byte_data(chip->client, cmd);
#endif
	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
			rc = oplus_dev_bus_read(chip->odb, cmd, &buf);
			if (rc >= 0)
				rc = buf;
#else
			rc = i2c_smbus_read_byte_data(chip->client, cmd);
#endif
			if (rc < 0) {
				retry--;
			} else {
				*returnData = (u8)rc;
				break;
			}
		}
	} else {
		*returnData = (u8)rc;
	}
	mutex_unlock(&chip->chip_mutex);
	/*pr_err(" cmd = 0x%x, returnData = 0x%x\r\n", cmd, *returnData) ; */
	if (rc < 0) {
		chg_err("read err, rc = %d\n", rc);
		bq27541_push_i2c_err(chip, true);
		return 1;
	} else {
		bq27541_i2c_err_clr(chip);
		return 0;
	}
}

int bq27541_i2c_txsubcmd_onebyte(struct chip_bq27541 *chip, u8 cmd, u8 writeData)
{
	int rc = 0;
	int retry = 3;

	if (!chip->client) {
		pr_err(" gauge_ic->client NULL, return\n");
		return 0;
	}
	if (oplus_is_rf_ftm_mode())
		return 0;
	if (cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;
	mutex_lock(&chip->chip_mutex);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_write(chip->odb, cmd, writeData);
#else
	rc = i2c_smbus_write_byte_data(chip->client, cmd, writeData);
#endif
	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
			rc = oplus_dev_bus_write(chip->odb, cmd, writeData);
#else
			rc = i2c_smbus_write_byte_data(chip->client,
				cmd, writeData);
#endif
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (rc < 0) {
		chg_err("write err, rc = %d\n", rc);
		bq27541_push_i2c_err(chip, true);
	} else {
		bq27541_i2c_err_clr(chip);
	}
	mutex_unlock(&chip->chip_mutex);
	return 0;
}

static u8 bq27541_calc_checksum(u8 *buf, int len)
{
	u8 checksum = 0;

	while (len--)
		checksum += buf[len];

	return 0xff - checksum;
}

static int bq27541_block_check_conditions(struct chip_bq27541 *chip, u8 *buf, int len, int offset, bool do_checksum)
{
	if (!chip || !buf || (offset < 0) || (offset >= BQ27541_BLOCK_SIZE) || (len <= 0) ||
	    (len + do_checksum > BQ27541_BLOCK_SIZE) || (offset + len + do_checksum > BQ27541_BLOCK_SIZE)) {
		chg_err("%soffset %d or len %d invalid\n", buf ? "buf is null or " : "", offset, len);
		return -EINVAL;
	}

	if (atomic_read(&chip->locked))
		return -EINVAL;

	return 0;
}

int bq27541_read_block(struct chip_bq27541 *chip, int addr, u8 *buf, int len, int offset, bool do_checksum)
{
	int ret;
	int data_check;
	int try_count = GAUGE_SUBCMD_TRY_COUNT;
	u8 extend_data[BQ28Z610_EXTEND_DATA_SIZE] = { 0 };
	u8 checksum = 0;

	ret = bq27541_block_check_conditions(chip, buf, len, offset, do_checksum);
	if (ret < 0)
		return ret;

try:
	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	ret = bq27541_i2c_txsubcmd(chip, GAUGE_EXTERN_DATAFLASHBLOCK, addr);
	if (ret < 0)
		goto error;
	usleep_range(1000, 1000);
	ret = bq27541_read_i2c_block(chip, GAUGE_EXTERN_DATAFLASHBLOCK, (offset + len + do_checksum + 2), extend_data);
	if (ret < 0)
		goto error;

	data_check = (extend_data[1] << 0x8) | extend_data[0];
	if (try_count-- > 0 && data_check != addr) {
		chg_err("0x%04x not match. try_count=%d extend_data[0]=0x%2x, extend_data[1]=0x%2x\n", addr, try_count,
			extend_data[0], extend_data[1]);
		mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
		usleep_range(2000, 2000);
		goto try;
	}
	if (data_check != addr)
		goto error;

	if (do_checksum) {
		checksum = bq27541_calc_checksum(&extend_data[offset + 2], len);
		if (checksum != extend_data[offset + len + 2]) {
			chg_err("[%*ph]checksum not match. expect=0x%02x actual=0x%02x\n",
				offset + len + do_checksum + 2, extend_data, checksum, extend_data[offset + len + 2]);
			goto error;
		}
	}

	memcpy(buf, &extend_data[offset + 2], len);
	chg_info("addr=0x%04x offset=%d buf=[%*ph] do_checksum=%d read success\n", addr, offset, len, buf, do_checksum);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	return 0;

error:
	chg_info("addr=0x%04x offset=%d buf=[%*ph] do_checksum=%d read fail\n", addr, offset, len, buf, do_checksum);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	return -EINVAL;
}

static bool zy0603_fg_condition_check(struct chip_bq27541 *chip)
{
	if (chip == NULL || !chip->batt_zy0603) {
		pr_err("%s gauge_ic is null or no batt_zy0603\n", __func__);
		return false;
	}

	if (atomic_read(&chip->suspended) == 1) {
		pr_info("%s gauge_ic->locked return\n", __func__);
		return false;
	}

	if (!zy0603_afi_update_done(chip)) {
		pr_info("%s afi_update return\n", __func__);
		return false;
	}

	if (oplus_vooc_get_fastchg_ing() &&
	    oplus_vooc_get_vooc_by_normal_path() &&
	    oplus_vooc_get_fast_chg_type() == BCC_TYPE_IS_VOOC)
		return true;

	if (atomic_read(&chip->locked) == 1) {
		pr_info("%s gauge_ic->locked or no mcu vooc return\n",
			__func__);
		return false;
	}

	return true;
}
static int zy0603_read_altmac_block(struct chip_bq27541 *chip, u16 reg, u8* buf,
						u8 offset, u8 length)
{
	if (!zy0603_fg_condition_check(chip)) {
		pr_info("%s zy0603_fg_condition_check fail\n", __func__);
		return -1;
	}

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, ZY0603_CMD_ALTMAC, reg);
	if (chip->i2c_err)
		goto fg_read_altmac_block_end;

	bq27541_read_i2c_block(chip, ZY0603_CMD_ALTBLOCK + offset, length, buf);
	if (chip->i2c_err)
		goto fg_read_altmac_block_end;

fg_read_altmac_block_end:
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	if (chip->i2c_err)
		return -1;

	return 0;
}

static int oplus_zy0603_check_sfr(struct chip_bq27541 *chip, bool *need_reset)
{
	s16 rc_sfr = 0;
	s32 qmax1 = 0, qmax0 = 0;
	u8 qmax_buf[ZY0603_QMAX_LEN] = {0};
	u8 sfr_buf[ZY0603_SFRCC_LEN] = {0};
	s32 ret;

	ret = zy0603_read_altmac_block(chip, ZY0603_READ_QMAX_CMD, qmax_buf,
				ZY0603_QMAX_DATA_OFFSET, ZY0603_QMAX_LEN);
	if (ret < 0) {
		pr_err("%s: could not read CMD 0x0075, ret = %d\n",
			__func__, ret);
		*need_reset = false;
		goto check_end;
	}
	qmax0 = (qmax_buf[1] << 8) | qmax_buf[0];
	qmax1 = (qmax_buf[3] << 8) | qmax_buf[2];
	qmax0 = max(qmax0, qmax1) * 2;
	if (qmax0 > ZY0603_MAX_QMAX_THRESHOLD)
		qmax0 = ZY0603_MAX_QMAX_THRESHOLD;

	ret = zy0603_read_altmac_block(chip, ZY0603_READ_SFRCC_CMD, sfr_buf,
				ZY0603_SFRCC_DATA_OFFSET, ZY0603_SFRCC_LEN);
	if (ret < 0) {
		pr_err("%s: could not read CMD 0x0073, ret = %d\n",
			__func__, ret);
		*need_reset = false;
		goto check_end;
	}

	rc_sfr = (sfr_buf[1] << 8) | sfr_buf[0];
	*need_reset = !!(abs(rc_sfr) > qmax0);

check_end:
	pr_info("%s: RC_SFR=%d, qmax=%d, ret=%d is_need_reset_sfr = %s",
		__func__, rc_sfr, qmax0, ret,
		*need_reset == true ?"true":"false");
	return 0;
}

static int oplus_detection_reset_condition(
			struct oplus_chg_ic_dev *ic_dev,
			bool *need_reset)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip && chip->batt_zy0603 &&
	    chip->zy0603_soc_opt &&
	    zy0603_fg_condition_check(chip)) {
		if (zy0603_static_checksum_check(chip) ||
		    zy0603_QmaxFgStatus_check(chip))
			*need_reset = false;
		else
			oplus_zy0603_check_sfr(chip, need_reset);
	}

	return 0;
}

static bool zy0603_check_modelratio(struct chip_bq27541 *chip)
{
	u8 r_buf[2] = {0};
	u8 modelratio[2] = {0xF7, 0x1D};
	zy0603_read_altmac_block(chip, ZY0603_MODELRATIO_REG, r_buf, 0, 2);
	if (r_buf[0]!= modelratio[0] ||
	    r_buf[1]!= modelratio[1])
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
		pr_err("%s cmd %d FAILED, retried counts: %d \n",
			__func__, cmd, retry_cnt);
		return GAUGE_ERROR;
	}
	return GAUGE_OK;
}

static bool zy0603_get_fw_version(struct chip_bq27541 *chip, int *fwversion)
{
	u8 version_data[4] = { 0 };
	u8 version_reg[2] = { 0x02, 0x00 };
	bool ret;

	if (!zy0603_fg_condition_check(chip)) {
		*fwversion = -1;
		chg_err("zy0603_fg_condition_check fail\n");
		return false;
	}

	bq27541_write_i2c_block(chip, ZY0603_CMD_ALTMAC, 2, version_reg);
	if (chip->i2c_err)
		goto fg_read_altmac_block_end;
	usleep_range(ZY0603_CMD_SBS_DELAY, ZY0603_CMD_SBS_DELAY);

	bq27541_read_i2c_block(chip, ZY0603_CMD_ALTMAC, 4, version_data);
fg_read_altmac_block_end:
	if (chip->i2c_err) {
		ret = false;
		*fwversion = -1;
	} else {
		*fwversion = (version_data[3] << 8) | version_data[2];
		ret = true;
	}
	chg_info("[0x%02x][0x%02x][0x%02x][0x%02x] fg_version %d %s\n",
		 version_data[0], version_data[1], version_data[2], version_data[3],
		 *fwversion, ret ? "i2c ok" : "i2c error");
	return ret;
}

static int zy0603_full_access_mode(struct chip_bq27541 *chip)
{
	u8 full_access_cmd_1[2] = {0xEF, 0xCD};
	u8 full_access_cmd_2[2] = {0xAB, 0x90};
	u8 full_access_op_st[2] = {0x54, 0x00};
	u8 read_buf[4] = {0};
	int retry = 2;
	int rc = 0;

	if (!zy0603_fg_condition_check(chip)) {
		pr_info("%s zy0603_fg_condition_check fail\n", __func__);
		return -1;
	}

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	do {
		bq27541_write_i2c_block(chip, ZY0603_CTRL_REG, 2,
			full_access_cmd_1);
		pr_info("%s write {0xEF, 0xCD} --> 0x00\n", __func__);
		usleep_range(U_DELAY_1_MS, U_DELAY_1_MS);

		bq27541_write_i2c_block(chip, ZY0603_CTRL_REG, 2,
			full_access_cmd_2);
		pr_info("%s write {0xAB, 0x90} --> 0x00\n", __func__);
		usleep_range(U_DELAY_1_MS, U_DELAY_1_MS);

		bq27541_write_i2c_block(chip, ZY0603_CMD_ALTMAC, 2,
			full_access_op_st);
		pr_info("%s write {0x54, 0x00} --> 0x3E\n", __func__);
		usleep_range(U_DELAY_1_MS, U_DELAY_1_MS);

		bq27541_read_i2c_block(chip, ZY0603_CMD_ALTMAC, 4, read_buf);
		pr_info("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
			__func__, read_buf[0], read_buf[1], read_buf[2],
			read_buf[3]);
		if ((read_buf[3] & 0x02) == 0) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	pr_info("%s full_access flag [%d]\n", __func__, rc);

	return rc;
}

static bool zy0603_fg_softreset(struct chip_bq27541 *chip)
{
	bool is_reset_done = false;
	int ret = -1;

	if (!zy0603_fg_condition_check(chip)) {
		pr_info("%s zy0603_fg_condition_check fail\n", __func__);
		return false;
	}

	pr_info("%s need soft  reset\n", __func__);
	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	ret = zy0603_unseal(chip);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);

	if (ret != 0) {
		pr_info("%s zy0603_unseal fail\n", __func__);
		return false;
	}

	ret = zy0603_full_access_mode(chip);
	if (ret == 0) {
		bq27541_i2c_txsubcmd(chip, ZY0603_CMD_ALTMAC, ZY0603_SOFT_RESET_CMD);
		pr_info("%s w 0x3E \n", __func__);
		if (chip->i2c_err)
			pr_info("%s soft reset fail", __func__);
		else
			is_reset_done = true;
	}

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	zy0603_seal(chip);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);

	return is_reset_done;
}

static int oplus_zy0603_reset_dynamic_para(struct chip_bq27541 *chip, bool *reset_done)
{
	if (!zy0603_fg_condition_check(chip)) {
		pr_info("%s zy0603_fg_condition_check fail\n", __func__);
		return -1;
	}

	if (chip->fg_soft_version == -1) {
		mutex_lock(&chip->bq28z610_alt_manufacturer_access);
		zy0603_get_fw_version(chip, &chip->fg_soft_version);
		mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	}

	if (chip->fg_soft_version  == -1) {
		pr_info("%s check_fw_version fail!", __func__);
		return -1;
	}

	pr_info("%s fwversion = 0x%04X \n",
		__func__, chip->fg_soft_version);
	if (chip->fg_soft_version > ZY0603_SOFT_RESET_VERSION_THRESHOLD) {
		if (zy0603_fg_softreset(chip))
			*reset_done = true;
	} else {
		/*fg version number lower than 0x16 must implement
		the hardware reset interface*/
		pr_info("%s fwversion = 0x%04X  need hard reset\n",
			__func__, chip->fg_soft_version);
		*reset_done = true;
	}
	return 0;
}

static int oplus_fg_reset(struct oplus_chg_ic_dev *ic_dev,
					bool *reset_done)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip && chip->batt_zy0603 &&
	    chip->zy0603_soc_opt) {
		oplus_zy0603_reset_dynamic_para(chip, reset_done);
		pr_info("%s: reset_done = %s", __func__,
			*reset_done == true ?"true":"false");
	}
	return 0;
}

/* OPLUS 2021-06-20 Add begin for zy0603 bad battery. */
static int zy0603_start_checksum_cal(struct chip_bq27541 *chip)
{
	u8 update_checksum[2] = {0x05, 0x00};
	if(NULL == chip)
		return -1;
	if(chip->afi_count == 0)
		return -1;
	return bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, update_checksum);
}

static int zy0603_disable_ssdf_and_pf(struct chip_bq27541 *chip)
{
	u8 write_protectioncfgb[5] = {0x0F, 0x41, 0x00, 0xAF, 0x05}; /*zy 20211227*/
	u8 write_ssdftime[5] = {0x29, 0x41, 0x00, 0x95, 0x05};
	u8 write_static_df_cheksum[6] = {0x7A, 0x42};
	u8 w_modelratio[6] = {0x14, 0x47, 0xF7, 0x1D, 0x90, 0x06};
	u8 w_cfconfig_chgp[5] = {0x52, 0x47, 0xF7, 0x6F, 0x05};
	u8 w_cfconfig_r2d[5] = {0x5A, 0x47, 0xBB, 0xA3, 0x05};
	u8 w_fgmaxdelta[5] = {0x9B, 0x47, 0x9B, 0x82, 0x05};
	u8 pf_en_cmd[2] = {0x24, 0x00};
	u8 mfg_status_cm[2]={0x57, 0x00};
	u8 read_buf[4] = {0};
	int rc = 0;
	int retry_cnt = 0;

	if(NULL == chip || (chip && chip->afi_count == 0))
	return -1;

	write_static_df_cheksum[2] = chip->static_df_checksum_3e[0];
	write_static_df_cheksum[3] = chip->static_df_checksum_3e[1];
	write_static_df_cheksum[4] = chip->static_df_checksum_60[0];
	write_static_df_cheksum[5] = chip->static_df_checksum_60[1];
	if (!zy0603_unseal(chip)) {	/* ok */
		/* disable protectioncfgb[ssdf] = 0 */
		bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, write_protectioncfgb);
		pr_err("%s write {0x0F, 0x41, 0x00} --> 0x3e\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, write_protectioncfgb+3);
		pr_err("%s write {0xAF, 0x05} --> 0x60\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__,
			read_buf[0], read_buf[1], read_buf[2]);
		if(!((read_buf[0] == write_protectioncfgb[0]) && (read_buf[1] == write_protectioncfgb[1])
			&& (read_buf[2] == write_protectioncfgb[2]))) {
			rc = -1;
			return rc;
		}

		/* disable ssdf */
		bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, write_ssdftime);
		pr_err("%s write {0x29,0x41,0x00} --> 0x3e\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, write_ssdftime+3);
		pr_err("%s write {0x95,0x05} --> 0x60\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__,
			read_buf[0], read_buf[1], read_buf[2]);
		if (!((read_buf[0] == write_ssdftime[0]) && (read_buf[1] == write_ssdftime[1]) && (read_buf[2] == write_ssdftime[2]))) {
			rc = -1;
			return rc;
		}

		if (chip->zy0603_soc_opt) {
			/* modelratio */
			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, w_modelratio);
			pr_info("%s modelratio write {0x14, 0x47, 0xF7, 0x1D} --> 0x3e\n",
				__func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, w_modelratio+4);
			pr_info("%s modelratio write {0x96, 0x06} --> 0x60\n",
				__func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);
			pr_info("%s modelratio 0x3E -->read"
				"[0x%02x][0x%02x] [0x%02x] [0x%02x]\n", __func__,
				read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
			if(!((read_buf[0] == w_modelratio[0]) &&
			    (read_buf[1] == w_modelratio[1]) &&
			    (read_buf[2] == w_modelratio[2]) &&
			    (read_buf[3] == w_modelratio[3]))) {
				rc = -1;
				return rc;
			}

			/* w_cfconfig_chgp */
			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, w_cfconfig_chgp);
			pr_info("%s w_cfconfig_chgp write {0x52, 0x47, 0xF7} --> 0x3e\n",
				__func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, w_cfconfig_chgp+3);
			pr_info("%s  w_cfconfig_chgp write {0xAF, 0x05} --> 0x60\n",
				__func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
			pr_info("%s w_cfconfig_chgp 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n",
				__func__, read_buf[0], read_buf[1], read_buf[2]);
			if(!((read_buf[0] == w_cfconfig_chgp[0]) &&
			    (read_buf[1] == w_cfconfig_chgp[1]) &&
			    (read_buf[2] == w_cfconfig_chgp[2]))) {
				rc = -1;
				return rc;
			}

			/* w_cfconfig_r2d */
			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, w_cfconfig_r2d);
			pr_info("%s w_cfconfig_r2d write {0x5A, 0x47, 0xBB} --> 0x3e\n",
				__func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, w_cfconfig_r2d+3);
			pr_err("%s w_cfconfig_r2d write {0xA3, 0x05} --> 0x60\n",
				__func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
			pr_info("%s w_cfconfig_r2d 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n",
				__func__, read_buf[0], read_buf[1], read_buf[2]);
			if(!((read_buf[0] == w_cfconfig_r2d[0]) &&
			    (read_buf[1] == w_cfconfig_r2d[1]) &&
			    (read_buf[2] == w_cfconfig_r2d[2]))) {
				rc = -1;
				return rc;
			}

			/* w_fgmaxdelta */
			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, w_fgmaxdelta);
			pr_info("%s w_fgmaxdelta write {0x9B, 0x47, 0x9B} --> 0x3e\n",
				__func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, w_fgmaxdelta+3);
			pr_info("%s w_fgmaxdelta write {0x82, 0x05} --> 0x60\n",
				__func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
			pr_info("%s w_fgmaxdelta 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n",
				__func__, read_buf[0], read_buf[1], read_buf[2]);
			if(!((read_buf[0] == w_fgmaxdelta[0]) &&
			    (read_buf[1] == w_fgmaxdelta[1]) &&
			    (read_buf[2] == w_fgmaxdelta[2]))) {
				rc = -1;
				return rc;
			}
		}

		/* update StaticDFChecksum */
		bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, write_static_df_cheksum);
		pr_err("%s write static_df_checksum_3e --> 0x3e\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, write_static_df_cheksum+4);
		pr_err("%s write {0x4B,0x06} --> 0x60\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__,
			read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
		if(!((read_buf[0] == write_static_df_cheksum[0]) && (read_buf[1] == write_static_df_cheksum[1])
				&& (read_buf[2] == write_static_df_cheksum[2]) && (read_buf[3] == write_static_df_cheksum[3]))) {
			rc = -1;
			return rc;
		}
		/* disable PF_EN */
		do {
			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, pf_en_cmd);
			pr_err("%s write {0x24,0x00} --> 0x3e\n", __func__);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, mfg_status_cm);
			pr_err("%s write {0x57,0x00} --> 0x3e\n", __func__);

			bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);
			pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__,
				read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
			if (read_buf[2] & (0x1 << 6)) {
				retry_cnt++;
				pr_err("retry_dis_pfen retry_cnt[%d]\n", retry_cnt);
			}
		} while ((read_buf[2] & (0x1 << 6)) && retry_cnt <= 3);
		if (retry_cnt > 3) {
			pr_err("retry_dis_pfen failed[%d]\n", retry_cnt);
		}

		if(!((read_buf[0] == mfg_status_cm[0]) && (read_buf[1] == mfg_status_cm[1]) && ((read_buf[3]&0x40) == 0))) {
			rc = -1;
			pr_err("read_buf check retry_dis_pfen failed[%d]\n", retry_cnt);
			return rc;
		}
	}

	return rc;
}

static int zy0603_disable_ssdf_and_pf_check(struct chip_bq27541 *chip)
{
	u8 write_ssdftime[2] = {0x29, 0x41};
	u8 ProtectionCfgB[3] = {0x0f, 0x41, 0x00};
	u8 mfg_status_cm[2]={0x57, 0x00};
	u8 read_buf[4] = {0};
	int comm_cfconfig_cmd[3] = {ZY0603_GFCONFIG_CHGP_REG,
				    ZY0603_GFCONFIG_R2D_REG,
				    ZY0603_GFMAXDELTA_REG};
	u8 comm_target_data[3] = {0xF7, 0XBB, 0x9B};
	int rc = 0;
	int error_flag = 0;
	int retry_cnt = 0;
	int i_index;

	/* ssdf_en check in ProtectionCfgB */
	do {
		bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, ProtectionCfgB);
		pr_err("%s write {0x29,0x41} --> 0x3e\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__,
			read_buf[0], read_buf[1], read_buf[2]);
		error_flag = 0;
		if (((read_buf[0] == ProtectionCfgB[0]) && (read_buf[1] == ProtectionCfgB[1]) && (read_buf[2] == ProtectionCfgB[2]))) {
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

	if (chip->zy0603_soc_opt) {
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
			pr_err("%s modelratio FAILED, retried counts: %d \n", __func__,
				retry_cnt);
			return GAUGE_ERROR;
		}

		for (i_index = 0; i_index < 3; i_index++) {
			rc = zy0603_check_comm_gfconfig(chip, comm_cfconfig_cmd[i_index],
							comm_target_data[i_index]);
			if (rc == GAUGE_ERROR)
				return GAUGE_ERROR;
		}
	}

	/* ssdf check */
	error_flag = 0;
	retry_cnt = 0;
	do {
		bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, write_ssdftime);
		pr_err("%s write {0x29,0x41} --> 0x3e\n", __func__);
		usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);

		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 3, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__,
			read_buf[0], read_buf[1], read_buf[2]);
		error_flag = 0;
		if (((read_buf[0] == write_ssdftime[0]) && (read_buf[1] == write_ssdftime[1]) && (read_buf[2] == 0x00))) {
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
		bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, mfg_status_cm);
		pr_err("%s write {0x57,0x00} --> 0x3e\n", __func__);

		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__,
			read_buf[0], read_buf[1], read_buf[2], read_buf[3]);

		error_flag = 0;
		if ((read_buf[0] == mfg_status_cm[0] && read_buf[1] == mfg_status_cm[1] && !(read_buf[2] & (0x1 << 6)))) {
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
	u8 checksum_reg[2] = {0x7A, 0x42};
	u8 read_buf[4] = {0};
	int rc = 0;
	int retry = 2;

	if (!chip) {
		return GAUGE_ERROR;
	}
	do {
		bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, checksum_reg);
		pr_err("%s write {0x7A, 0x42} --> 0x3E\n", __func__);
		usleep_range(5000, 5000);

		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x][0x%02x][0x%02x]\n", __func__,
			read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
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
	u8 update_checksum[2] = {0x05, 0x00};
	u8 read_buf[4] = {0};

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	/* w */
	bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, update_checksum);

	/* r */
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 4, read_buf);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	/* check */
	if (read_buf[3] & (1 << 7)) {
		rc = 1;	/* exception */
		pr_err("%s checksum exception [0x%x] [0x%x] [0x%x] [0x%x]\n",
				__func__, read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
	} else {
		rc = 0;	/* normal */
	}

	return rc;
}

static int zy0603_QmaxFgStatus_check(struct chip_bq27541 *chip)
{
	int rc = 0;
	u8 QmaxFGstatus[2] = {0x75, 0x00};
	u8 read_buf[7] = {0};
	int qmax_cell1, qmax_cell2, fg_status;

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	/* w */
	bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, QmaxFGstatus);

	/* r */
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 7, read_buf);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);

	/* check */
	fg_status = read_buf[6];
	qmax_cell1 = (read_buf[3] << 8) | read_buf[2];
	qmax_cell2 = (read_buf[5] << 8) | read_buf[4];
	if (!(qmax_cell1 >= chip->zy_dts_qmax_min && qmax_cell1 <= chip->zy_dts_qmax_max) ||
		!(qmax_cell2 >= chip->zy_dts_qmax_min && qmax_cell2 <= chip->zy_dts_qmax_max)	||
		!(fg_status == 0x33)) {
		rc = 1;	/* exception */
		pr_err("Qmax or fg_status exception\n");
		pr_err("%s [0x%0x] [0x%0x] [0x%0x] [0x%0x] [0x%0x] [0x%0x] [0x%0x]\n",
				__func__, read_buf[0], read_buf[1], read_buf[2],
				read_buf[3], read_buf[4], read_buf[5], read_buf[6]);
	} else {
		rc = 0;	/* normal */
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

	u8 write_buf[16 + 4] = {0};
	u8 read_buf[16 + 4] = {0};
	u16 addr;

	printk("update %s ...\n", msg);
	for (i=0; i < len1; i++) {
		checksum = 0xFF;
		addr = base_addr + i*16;
		write_buf[0] = (u8)addr;
		checksum -= write_buf[0];
		write_buf[1] = (u8)(addr>>8);
		checksum -= write_buf[1];
		for (j=0; j < 16; j++) {
			write_buf[2+j] = afi_buf1[j+i*16];	/* 0x4000 + 30 = 0x401E */
			checksum -= write_buf[2+j];
		}
		write_buf[18] = checksum;
		write_buf[19] = 16+4;								/* total len */

		retry = 0;
		do {
			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 18, write_buf);	/* addr + valid data */
			usleep_range(U_DELAY_1_MS, U_DELAY_1_MS);
			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, write_buf+18); /* checksum + self (18 + 19) */
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);
			bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 18, read_buf); 	/* addr + valid data */
			for (k=0; k < 18; k++) {
				error_flag = 0;
				if(write_buf[k] != read_buf[k]) {
					pr_err("%s 1 w r[%d][%d] = 0x%0x 0x%0x", msg, i, k, write_buf[k], read_buf[k]);	/* expect 0x78 */
					error_flag = 1;
					retry++;
					pr_err("%s 1 error retry[%d]\n", msg, retry);
					break;
				}
			}
		} while (error_flag == 1 && retry < 5);
		pr_err("\n");
	}

	if (len2 > 0) {											/* tail: len2 < 16 */
		checksum = 0xFF;
		addr = base_addr + len1*16;
		write_buf[0] = (u8)addr;
		checksum -= write_buf[0];
		write_buf[1] = (u8)(addr>>8);
		checksum -= write_buf[1];
		for (j=0; j < len2; j++) {
			write_buf[2+j] = afi_buf1[j+len1*16];
			checksum -= write_buf[2+j];
		}
		write_buf[len2 + 2] = checksum;
		write_buf[len2 + 3] = len2 + 4;

		retry = 0;
		do {
			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, len2 + 2, write_buf);
			usleep_range(U_DELAY_1_MS, U_DELAY_1_MS);
			bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL2, 2, write_buf+len2+2);
			usleep_range(U_DELAY_5_MS, U_DELAY_5_MS);
			bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, len2 + 2, read_buf);
			for (k=0; k < len2 + 2; k++) {
				error_flag = 0;
				if(write_buf[k] != read_buf[k]) {
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

#define OPLUS_AFI_UPDATE_INTERVAL_SEC 		5
#define OPLUS_AFI_UPDATE_INTERVAL	round_jiffies_relative(msecs_to_jiffies(OPLUS_AFI_UPDATE_INTERVAL_SEC*1000))
int oplus_vooc_get_vooc_by_normal_path(void)
{
	int vooc_by_normal = 0;
	struct oplus_mms *vooc_topic;
	union mms_msg_data data = { 0 };
	int rc;

	vooc_topic = oplus_mms_get_by_name("vooc");
	if (!vooc_topic)
		return 0;

	rc = oplus_mms_get_item_data(vooc_topic, VOOC_ITEM_VOOC_BY_NORMAL_PATH, &data, true);
	if (!rc)
		vooc_by_normal = data.intval;

	pr_err("%s, get vooc by normal = %d\n", __func__, vooc_by_normal);
	return vooc_by_normal;
}

static int oplus_gauge_get_afi_condition(void)
{
	int afi_condition = 0;
	struct oplus_mms *vooc_topic;
	union mms_msg_data data = { 0 };
	int rc;

	vooc_topic = oplus_mms_get_by_name("vooc");
	if (!vooc_topic)
		return 0;

	rc = oplus_mms_get_item_data(vooc_topic, VOOC_ITEM_GET_AFI_CONDITION, &data, true);
	if (!rc)
		afi_condition = data.intval;

	pr_err("%s, get afi condition = %d\n", __func__, afi_condition);
	return afi_condition;
}

static void zy0603_afi_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_bq27541 *chip = container_of(dwork, struct chip_bq27541, afi_update);
	int ret = 0;
	int error_flag = 0;
	bool gauge_locked = false;

	chip->need_check = false;
	if (chip->error_occured) {
		goto error_occured;
	}
error_occured:
	msleep(M_DELAY_10_S);
	gauge_locked = !!atomic_read(&chip->locked);
	if (oplus_gauge_get_afi_condition()) {
		if (!gauge_locked || (oplus_vooc_get_vooc_by_normal_path() &&
				oplus_vooc_get_fast_chg_type() == BCC_TYPE_IS_VOOC)) {
			if (!zy0603_unseal(chip)) {
				chip->afi_update_done = false;
				ret = zy0603_afi_param_update(chip);
				if (ret < 0) {
					printk(KERN_ERR "[%s] zy0603_afi_param_update fail\n", __func__);
				}

				do {
					error_flag = 0;
					if (!zy0603_seal(chip)) {
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

static bool zy0603_afi_update_done(struct chip_bq27541 *chip)
{
	if (!chip) {
		return false;
	}

	if (chip->batt_zy0603 && chip->afi_count > 0 && !chip->afi_update_done) {
		chg_info("return for afi_update_done not finished\n");
		return false;
	} else {
		return true;
	}
}

#define CHECKSUM_ERROR_CNT	3
static int zy0603_protect_check(struct oplus_chg_ic_dev *ic_dev)
{
	static int checksum_error = 0;
	static int qmax_fgstatus_error = 0;
	bool gauge_locked = false;
	/* static bool error_occured = false; */
	int checksum_error_flag = 0, qmaxfg_error_flag = 0;
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return false;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	/*printk(KERN_ERR "[%s] v12 unseal start\n", __func__);*/
	if (!chip || (chip && !chip->batt_zy0603) || (chip && chip->afi_count == 0)) {
		printk(KERN_ERR "[%s] return for %s\n", __func__, !chip ? "guage is null" : "is not zy0603 gauge");
		return -1;
	}

	/*printk(KERN_ERR "[%s] ssdf and pf disable = %d\n", __func__, gauge_ic->disabled);*/
	/* if (oplus_vooc_get_allow_reading() && gauge_ic->disabled !error_occured) { */
	gauge_locked = !!atomic_read(&chip->locked);
	if ((!gauge_locked ||
			(oplus_vooc_get_vooc_by_normal_path() &&
			oplus_vooc_get_fast_chg_type() == BCC_TYPE_IS_VOOC)) &&
			chip->disabled && chip->need_check) {
		if (zy0603_static_checksum_check(chip)) {
			checksum_error_flag = 1;
			checksum_error++;
			printk(KERN_ERR "[%s] staticchecksum_error = %d\n", __func__, checksum_error);
			if (CHECKSUM_ERROR_CNT <= checksum_error) {
				checksum_error = 0;
				if (chip && chip->afi_update_done) {
					schedule_delayed_work(&chip->afi_update, 0);
				}
			}
		} else {
			checksum_error_flag = 0;
			checksum_error = 0;
		}
		if (zy0603_QmaxFgStatus_check(chip)) {
			qmaxfg_error_flag = 1;
			qmax_fgstatus_error++;

			printk(KERN_ERR "[%s] qmax_fgstatus_error = %d\n", __func__, qmax_fgstatus_error);
			if (CHECKSUM_ERROR_CNT <= qmax_fgstatus_error) {
				qmax_fgstatus_error = 0;
				if (chip && chip->afi_update_done) {
					schedule_delayed_work(&chip->afi_update, 0);
				}
			}
		} else {
			qmaxfg_error_flag = 0;
			qmax_fgstatus_error = 0;
		}
		/* error_occured = true; */
	}
	printk(KERN_ERR "[%s] checksum_error[%d %d] qmaxfg_error[%d, %d]\n",
		__func__, checksum_error_flag, checksum_error, qmaxfg_error_flag, qmax_fgstatus_error);

	return 0;
}

static int zy0603_parse_afi_buf(struct chip_bq27541 *chip) {
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
	dev_err(chip->dev, "%s, static_df_checksum_3e = [%x,%x]\n", __func__, chip->static_df_checksum_3e[0], chip->static_df_checksum_3e[1]);
	data = of_get_property(afi_data_node, "static_df_checksum_60", &len);
	if (!data || len != 2) {
		dev_err(chip->dev, "%s, parse static_df_checksum_60  failed\n", __func__);
		return -ENOMEM;
	}
	chip->static_df_checksum_60 = data;
	dev_err(chip->dev, "%s, static_df_checksum_60 = [%x,%x]\n", __func__, chip->static_df_checksum_60[0], chip->static_df_checksum_60[1]);
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
		dev_err(chip->dev, "zy0603_parse_afi_buf chwit i = %d, len= %d  end [%x]\n", i, len, chip->afi_buf[i][chip->afi_buf_len[i] - 1]);
	}
	return rc;
}

/* OPLUS 2013-08-24 wangjc Add begin for add adc interface. */
static int bq27541_get_battery_cc(struct chip_bq27541 *chip) /*  sjc20150105  */
{
	int ret = 0;
	int cc = 0;
	int retry = RETRY_CNT;

	if (!chip) {
		return 0;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->cc_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_cc, &cc);
		while (!normal_range_judge(CC_MAX, CC_MIN, cc) && retry > 0 && !ret) {
			chg_err("bq27541_get_battery_cc %d out of range, retry %d\n", cc, retry);
			usleep_range(10000, 10000);
			ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_cc, &cc);
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading cc.\n");
			return 0;
		}
	} else {
		if (chip->cc_pre) {
			return chip->cc_pre;
		} else {
			return 0;
		}
	}
	chip->cc_pre = cc;
	return cc;
}

static int bq28z610_get_true_fcc(struct chip_bq27541 *chip, int *true_fcc)
{
	int ret;
	u8 buf[BQ28Z610_TRUE_FCC_NUM_SIZE] = { 0 };

	if (!chip || !true_fcc)
		return -EINVAL;

	ret = bq27541_read_block(
		chip, BQ28Z610_REG_TRUE_FCC, buf, BQ28Z610_TRUE_FCC_NUM_SIZE, BQ28Z610_TRUE_FCC_OFFSET, false);
	if (ret < 0)
		return ret;

	*true_fcc = (buf[1] << 8) | buf[0];
	chg_info("true_fcc=%d\n", *true_fcc);

	return ret;
}

static void bq28z610_set_fcc_sync(struct chip_bq27541 *chip)
{
	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_FCC_SYNC_CMD);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	chg_info("set fcc sync\n");
}

static void bq27541_fcc_too_small_check_work(struct work_struct *work)
{
	int ret;
	int true_fcc = 0;
	struct chip_bq27541 *chip = container_of(
		work, struct chip_bq27541, fcc_too_small_check_work);


	ret = bq28z610_get_true_fcc(chip, &true_fcc);
	if (!ret && (true_fcc > 200)) /* TODO: true_fcc value is more than 200 */
		bq28z610_set_fcc_sync(chip);

	chip->fcc_too_small_checking = false;
}

static void bq27541_fcc_too_small_check(struct chip_bq27541 *chip, int fcc)
{
	if (!chip)
		return;

	if (chip->batt_bq28z610 && !chip->batt_zy0603) {
		if (chip->fcc_too_small_checking) {
			chg_info("fcc too small checking, ignore this time");
			return;
		}

		/* TODO: fcc value is less than 200 */
		if (fcc < 200) {
			chip->fcc_too_small_checking = true;
			schedule_work(&chip->fcc_too_small_check_work);
		}
	}
}

static int bq27541_get_battery_fcc(struct chip_bq27541 *chip) /*  sjc20150105  */
{
	int ret = 0;
	int fcc = 0;
	int retry = RETRY_CNT;

	if (!chip) {
		return 0;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->fcc_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_fcc, &fcc);
		while (!normal_range_judge(FCC_MAX, FCC_MIN, fcc) && retry > 0 && !ret) {
			chg_err("bq27541_get_battery_fcc %d out of range, retry %d\n", fcc, retry);
			usleep_range(10000, 10000);
			ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_fcc, &fcc);
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading fcc.\n");
			return ret;
		}
	} else {
		if (chip->fcc_pre) {
			return chip->fcc_pre;
		} else {
			return 0;
		}
	}
	chip->fcc_pre = fcc;
	bq27541_fcc_too_small_check(chip, fcc);

	return fcc;
}

__maybe_unused static int bq27541_get_prev_batt_fcc(struct chip_bq27541 *chip) /*  sjc20150105  */
{
	if (!chip) {
		return 0;
	}
	return chip->fcc_pre;
}

static int bq27541_get_battery_soh(struct chip_bq27541 *chip) /*  sjc20150105  */
{
	int ret = 0;
	int soh = 0;
	int retry = RETRY_CNT;

	if (!chip) {
		return 0;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->soh_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_soh, &soh);
		while (!normal_range_judge(SOH_MAX, SOH_MIN, soh) && retry > 0 && !ret) {
			chg_err("bq27541_get_battery_soh %d out of range, retry %d\n", soh, retry);
			usleep_range(10000, 10000);
			ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_soh, &soh);
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading fcc.\n");
			return 0;
		}
	} else {
		if (chip->soh_pre) {
			return chip->soh_pre;
		} else {
			return 0;
		}
	}
	chip->soh_pre = soh;
	return soh;
}

static int bq27541_soc_calibrate(struct chip_bq27541 *chip, int soc)
{
	unsigned int soc_calib;
	/*int counter_temp = 0; */
	/*
	if (!gauge_ic->batt_psy) {
		gauge_ic->batt_psy = power_supply_get_by_name("battery");
		gauge_ic->soc_pre = soc;
	}
*/
	if (!chip) {
		return 0;
	}
	soc_calib = soc;
	if (soc >= 100) {
		soc_calib = 100;
	} else if (soc < 0) {
		soc_calib = 0;
	}
	chip->soc_pre = soc_calib;
	/*pr_info("soc:%d, soc_calib:%d\n", soc, soc_calib); */
	return soc_calib;
}

/*
 * Return the battery Voltage in milivolts
 * Or < 0 if something fails.
 */

static void bq27541_cntl_cmd(struct chip_bq27541 *chip, int subcmd)
{
	bq27541_i2c_txsubcmd(chip, BQ27541_BQ27411_REG_CNTL, subcmd);
}

static int bq28z610_get_2cell_voltage(struct chip_bq27541 *chip);
/* static int bq28z610_get_2cell_balance_time(void); */

static int bq27541_get_battery_mvolts(struct chip_bq27541 *chip)
{
	int ret = 0;
	int volt = 0;
	int retry = RETRY_CNT;

	if (!chip) {
		return 0;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->batt_vol_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_volt, &volt);
		if (ret) {
			dev_err(chip->dev,
				"error reading voltage, ret:%d\n", ret);
			chip->batt_cell_max_vol = chip->max_vol_pre;
			chip->batt_cell_min_vol = chip->min_vol_pre;
			return chip->batt_vol_pre;
		}
		if (chip->batt_bq28z610) {
			bq28z610_get_2cell_voltage(chip);
			chip->max_vol_pre = chip->batt_cell_max_vol;
			chip->min_vol_pre = chip->batt_cell_min_vol;
			chip->batt_vol_pre = chip->batt_cell_max_vol;
			/* bq28z610_get_2cell_balance_time(); */
		} else {
			chip->batt_cell_max_vol = volt;
			chip->batt_cell_min_vol = volt;
			chip->batt_vol_pre = volt;
			chip->max_vol_pre = chip->batt_cell_max_vol;
			chip->min_vol_pre = chip->batt_cell_min_vol;
		}
		while (!normal_range_judge(VOLT_MAX, VOLT_MIN, chip->batt_cell_max_vol) && retry > 0 && !ret) {
			chg_err("bq27541_get_battery_mvolts %d out of range, retry %d\n", chip->batt_cell_max_vol, retry);
			usleep_range(10000, 10000);
			ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_volt, &volt);
			if (ret) {
				dev_err(chip->dev,
					"error reading voltage, ret:%d\n", ret);
				chip->batt_cell_max_vol = chip->max_vol_pre;
				chip->batt_cell_min_vol = chip->min_vol_pre;
				return chip->batt_vol_pre;
			}
			if (chip->batt_bq28z610) {
				bq28z610_get_2cell_voltage(chip);
				chip->max_vol_pre = chip->batt_cell_max_vol;
				chip->min_vol_pre = chip->batt_cell_min_vol;
				chip->batt_vol_pre = chip->batt_cell_max_vol;
				/* bq28z610_get_2cell_balance_time(); */
			} else {
				chip->batt_cell_max_vol = volt;
				chip->batt_cell_min_vol = volt;
				chip->batt_vol_pre = volt;
				chip->max_vol_pre = chip->batt_cell_max_vol;
				chip->min_vol_pre = chip->batt_cell_min_vol;
			}
			retry--;
		}
		return chip->batt_cell_max_vol;
	} else {
		return chip->batt_vol_pre;
	}
}

static int bq27541_get_battery_fc(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->fc_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_fc, &val);
		if (ret) {
			dev_err(chip->dev, "error reading fc, ret:%d\n",
				ret);
			return chip->fc_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->fc_pre = val;
			return val;
		} else {
			return chip->fc_pre;
		}
	} else {
		return chip->fc_pre;
	}
}

static int bq27541_get_battery_qm(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->qm_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_qm, &val);
		if (ret) {
			dev_err(chip->dev, "error reading qm, ret:%d\n",
				ret);
			return chip->qm_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->qm_pre = val;
			return val;
		} else {
			return chip->qm_pre;
		}
	} else {
		return chip->qm_pre;
	}
}

static int bq27541_get_battery_pd(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->pd_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_pd, &val);
		if (ret) {
			dev_err(chip->dev, "error reading pd, ret:%d\n",
				ret);
			return chip->pd_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->pd_pre = val;
			return val;
		} else {
			return chip->pd_pre;
		}
	} else {
		return chip->pd_pre;
	}
}

static int bq27541_get_battery_rcu(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->rcu_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_rcu, &val);
		if (ret) {
			dev_err(chip->dev, "error reading rcu, ret:%d\n",
				ret);
			return chip->rcu_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->rcu_pre = val;
			return val;
		} else {
			return chip->rcu_pre;
		}
	} else {
		return chip->rcu_pre;
	}
}

static int bq27541_get_battery_rcf(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->rcf_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_rcf, &val);
		if (ret) {
			dev_err(chip->dev, "error reading rcf, ret:%d\n",
				ret);
			return chip->rcf_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->rcf_pre = val;
			return val;
		} else {
			return chip->rcf_pre;
		}
	} else {
		return chip->rcf_pre;
	}
}

static int bq27541_get_battery_fcu(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->fcu_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_fcu, &val);
		if (ret) {
			dev_err(chip->dev, "error reading fcu, ret:%d\n",
				ret);
			return chip->fcu_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->fcu_pre = val;
			return val;
		} else {
			return chip->fcu_pre;
		}
	} else {
		return chip->fcu_pre;
	}
}

static int bq27541_get_battery_fcf(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->fcf_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_fcf, &val);
		if (ret) {
			dev_err(chip->dev, "error reading fcf, ret:%d\n",
				ret);
			return chip->fcf_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->fcf_pre = val;
			return val;
		} else {
			return chip->fcf_pre;
		}
	} else {
		return chip->fcf_pre;
	}
}

static int bq27541_get_battery_sou(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->sou_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_sou, &val);
		if (ret) {
			dev_err(chip->dev, "error reading sou, ret:%d\n",
				ret);
			return chip->sou_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->sou_pre = val;
			return val;
		} else {
			return chip->sou_pre;
		}
	} else {
		return chip->sou_pre;
	}
}

static int bq27541_get_battery_do0(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->do0_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_do0, &val);
		if (ret) {
			dev_err(chip->dev, "error reading do0, ret:%d\n",
				ret);
			return chip->do0_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->do0_pre = val;
			return val;
		} else {
			return chip->do0_pre;
		}
	} else {
		return chip->do0_pre;
	}
}

static int bq27541_get_battery_doe(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->doe_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_doe, &val);
		if (ret) {
			dev_err(chip->dev, "error reading doe, ret:%d\n",
				ret);
			return chip->doe_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->doe_pre = val;
			return val;
		} else {
			return chip->doe_pre;
		}
	} else {
		return chip->doe_pre;
	}
}

static int bq27541_get_battery_trm(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->trm_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_trm, &val);
		if (ret) {
			dev_err(chip->dev, "error reading trm, ret:%d\n",
				ret);
			return chip->trm_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->trm_pre = val;
			return val;
		} else {
			return chip->trm_pre;
		}
	} else {
		return chip->trm_pre;
	}
}

static int bq27541_get_battery_pc(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->pc_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_pc, &val);
		if (ret) {
			dev_err(chip->dev, "error reading pc, ret:%d\n",
				ret);
			return chip->pc_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->pc_pre = val;
			return val;
		} else {
			return chip->pc_pre;
		}
	} else {
		return chip->pc_pre;
	}
}

static int bq27541_get_battery_qs(struct chip_bq27541 *chip)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return -1;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->qs_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_qs, &val);
		if (ret) {
			dev_err(chip->dev, "error reading qs, ret:%d\n",
				ret);
			return chip->qs_pre;
		}
		if (!chip->batt_bq28z610) {
			chip->qs_pre = val;
			return val;
		} else {
			return chip->qs_pre;
		}
	} else {
		return chip->qs_pre;
	}
}

static int bq27541_get_battery_mvolts_2cell_max(struct chip_bq27541 *chip)
{
	if (!chip) {
		return 0;
	}
	return chip->batt_cell_max_vol;
}

static int bq27541_get_battery_mvolts_2cell_min(struct chip_bq27541 *chip)
{
	if (!chip) {
		return 0;
	}
	return chip->batt_cell_min_vol;
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

	if (atomic_read(&chip->suspended) == 1) {
		return 0;
	}

	if (chip->batt_bq28z610) {
		if (atomic_read(&chip->locked) == 0) {
			mutex_lock(&chip->bq28z610_alt_manufacturer_access);
			bq27541_i2c_txsubcmd(chip, BQ28Z610_DEVICE_CHEMISTRY_EN_ADDR,
					     BQ28Z610_DEVICE_CHEMISTRY_CMD);
			usleep_range(1000, 1000);
			ret = bq27541_read_i2c_block(chip,
				BQ28Z610_DEVICE_CHEMISTRY_ADDR,
				BQ28Z610_DEVICE_CHEMISTRY_SIZE, data);
			mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
			if (ret) {
				dev_err(chip->dev,
					"error reading operation status.\n");
				return 0;
			}
			dev_info(chip->dev,
				 "device chemistry: [%c%c%c%c]\n", data[0],
				 data[1], data[2], data[3]);
			if (data[0] == 0x4C && data[1] == 0x49 &&
			    data[2] == 0x4F && data[3] == 0x4E) {
				return DEVICE_CHEMISTRY_LION;
			} else if (data[0] == 0x43 && data[1] == 0x32 &&
				   data[2] == 0x41 && data[3] == 0x31) {
				return DEVICE_CHEMISTRY_C2A1;
			} else if (data[0] == 0x43 && data[1] == 0x32 &&
				   data[2] == 0x41 && data[3] == 0x32) {
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
	static int pre_balancing_config = 0;
	static int count = 0;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->suspended) == 1) {
		return 0;
	}

	if (chip->batt_bq28z610) {
		if (atomic_read(&chip->locked) == 0) {
			mutex_lock(&chip->bq28z610_alt_manufacturer_access);
			bq27541_i2c_txsubcmd(chip, BQ28Z610_OPERATION_STATUS_EN_ADDR,
					     BQ28Z610_OPERATION_STATUS_CMD);
			usleep_range(1000, 1000);
			ret = bq27541_read_i2c_block(chip,
				BQ28Z610_OPERATION_STATUS_ADDR,
				BQ28Z610_OPERATION_STATUS_SIZE, data);
			mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
			if (ret) {
				dev_err(chip->dev,
					"error reading operation status.\n");
				return pre_balancing_config;
			}
			balancing_config =
				(u32)(((u32)data[3] << 24 | (u32)data[2] << 16 | (u32)data[1] << 8 | data[0]) &
				      BQ28Z610_BALANCING_CONFIG_BIT) >>
				28;
			count++;
			if (balancing_config ^ pre_balancing_config ||
			    count >= 10) {
				count = 0;
				dev_info(chip->dev,
					 "operation status[0x%x], cb28[%d]\n",
					 data[3] << 24 | data[2] << 16 |
						 data[1] << 8 | data[0],
					 balancing_config);
			}
			pre_balancing_config = balancing_config;
			return balancing_config;
		} else {
			return pre_balancing_config;
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

static int bq27541_get_battery_temperature(struct chip_bq27541 *chip)
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

	if (!chip) {
		return 0;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_temp, &temp);
		while (!normal_range_judge(TEMP_MAX, TEMP_MIN, temp) && retry > 0 && !ret) {
			chg_err("bq27541_get_battery_temperature %d out of range, retry %d\n", temp, retry);
			usleep_range(10000, 10000);
			ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_temp, &temp);
			retry--;
		}
		if (ret) {
			count++;
			dev_err(chip->dev, "error reading temperature\n");
			if (count > 1) {
				count = 0;
				chip->temp_pre =
					-400 -
					ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
				return -400;
			} else {
				return chip->temp_pre +
				       ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
			}
		}
		count = 0;
	} else {
		return chip->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
	}

	if (chip->bq28z610_need_balancing) {
		t = get_current();
		if (t != NULL && !strncmp(t->comm, "tbatt_pwroff", 12))
			goto cb_exit;

		batt_balancing_config = bq28z610_get_balancing_config(chip);
		if (pre_batt_balancing_config == 0 &&
		    batt_balancing_config == 1 && temp_status == 0) {
			if (chip->temp_pre +
				    ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN <=
			    BATT_TEMP_16C) {
				temp_status = TEMP_LT_16C;
				delta_temp = 10;
			} else if (chip->temp_pre +
					   ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN <=
				   BATT_TEMP_39C) {
				temp_status = TEMP_LT_39C;
				delta_temp = 0;
			} else {
				temp_status = TEMP_HT_39C;
				delta_temp = 0;
			}
			printk(KERN_ERR
			       "SJC-TEST: temp_status[%d], delta_temp[%d]\n",
			       temp_status, delta_temp);
		}
		pre_batt_balancing_config = batt_balancing_config;

		if (batt_balancing_config == 1) {
			cb_flag = 1;
			cb_count++;

			if (chip->bq28z610_device_chem ==
				    DEVICE_CHEMISTRY_C2A1 ||
			    chip->bq28z610_device_chem ==
				    DEVICE_CHEMISTRY_C2A2) {
				if (cb_count >= 4 &&
				    temp_status == TEMP_LT_16C) {
					temp = temp - 20;
					printk(KERN_ERR
					       "SJC-TEST C2A1: - 20\n");
				} else if (cb_count >= 3 &&
					   temp_status != TEMP_HT_39C) {
					temp = temp - 15;
					printk(KERN_ERR
					       "SJC-TEST C2A1: - 15\n");
				} else if (cb_count >= 2) {
					temp = temp - 10;
					printk(KERN_ERR
					       "SJC-TEST C2A1: - 10\n");
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
				if (cb_count >= 6 &&
				    temp_status == TEMP_LT_16C) {
					temp = temp - (60 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n",
					       60 + delta_temp);
				} else if (cb_count >= 5 &&
					   temp_status != TEMP_HT_39C) {
					temp = temp - (50 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n",
					       50 + delta_temp);
				} else if (cb_count >= 4) {
					temp = temp - (40 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n",
					       40 + delta_temp);
				} else if (cb_count >= 3) {
					temp = temp - (30 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n",
					       30 + delta_temp);
				} else if (cb_count >= 2) {
					temp = temp - (20 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n",
					       20 + delta_temp);
				} else if (cb_count >= 1) {
					temp = temp - (10 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n",
					       10 + delta_temp);
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
			if (chip->bq28z610_device_chem ==
				    DEVICE_CHEMISTRY_C2A1 ||
			    chip->bq28z610_device_chem ==
				    DEVICE_CHEMISTRY_C2A2) {
				if (cb_count >= 4 &&
				    temp_status == TEMP_LT_16C) {
					temp = temp - 20;
					printk(KERN_ERR
					       "SJC-TEST 4 C2A1: - 20\n");
				} else if (cb_count >= 3 &&
					   temp_status != TEMP_HT_39C) {
					temp = temp - 15;
					printk(KERN_ERR
					       "SJC-TEST 3 C2A1: - 15\n");
				} else if (cb_count >= 2) {
					temp = temp - 10;
					printk(KERN_ERR
					       "SJC-TEST 2 C2A1: - 10\n");
				} else if (cb_count >= 1) {
					temp = temp - 5;
					printk(KERN_ERR
					       "SJC-TEST 1 C2A1: - 5\n");
				}
			} else {
				if (cb_count >= 6 &&
				    temp_status == TEMP_LT_16C) {
					temp = temp - (60 + delta_temp);
					printk(KERN_ERR "SJC-TEST 6: - %d\n",
					       60 + delta_temp);
				} else if (cb_count >= 5 &&
					   temp_status != TEMP_HT_39C) {
					temp = temp - (50 + delta_temp);
					printk(KERN_ERR "SJC-TEST 5: - %d\n",
					       50 + delta_temp);
				} else if (cb_count >= 4) {
					temp = temp - (40 + delta_temp);
					printk(KERN_ERR "SJC-TEST 4: - %d\n",
					       40 + delta_temp);
				} else if (cb_count >= 3) {
					temp = temp - (30 + delta_temp);
					printk(KERN_ERR "SJC-TEST 3: - %d\n",
					       30 + delta_temp);
				} else if (cb_count >= 2) {
					temp = temp - (20 + delta_temp);
					printk(KERN_ERR "SJC-TEST 2: - %d\n",
					       20 + delta_temp);
				} else if (cb_count >= 1) {
					temp = temp - (10 + delta_temp);
					printk(KERN_ERR "SJC-TEST 1: - %d\n",
					       10 + delta_temp);
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
#ifdef CONFIG_OPLUS_CHARGER_MTK
	/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
	if (chip->i2c_rst_ext) {
		if (temp == 0) {
			chg_err("fg read temperature I2C error, set err flag\n");
			chip->err_status = true;
#ifdef CONFIG_OPLUS_FG_ERROR_RESET_I2C
/* this workaround only for flamingo, for scanning tool issue */
			oplus_set_fg_err_flag(chip->client->adapter, true);
#endif
			bq27541_push_i2c_err(chip, true);
			temp = -400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
		} else {
			chip->err_status = false;
#ifdef CONFIG_OPLUS_FG_ERROR_RESET_I2C
/* this workaround only for flamingo, for scanning tool issue */
			oplus_set_fg_err_flag(chip->client->adapter, false);
#endif
			bq27541_i2c_err_clr(chip);
		}
	}
	/* end workaround 230504153935012779 */
#endif
	if ((temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN) > 1000) {
		temp = chip->temp_pre;
	}
	chip->temp_pre = temp;

	return temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

static int bq27541_get_batt_remaining_capacity(struct chip_bq27541 *chip)
{
	int ret;
	int cap = 0;
	int retry = RETRY_CNT;

	if (!chip) {
		return 0;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->rm_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_rm, &cap);
		while (!normal_range_judge(FCC_MAX, 0, cap) && retry > 0 && !ret) {
			chg_err("bq27541_get_batt_remaining_capacity %d out of range, retry %d\n", cap, retry);
			usleep_range(10000, 10000);
			ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_rm, &cap);
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading capacity.\n");
			return ret;
		}
		chip->rm_pre = cap;
		return chip->rm_pre;
	} else {
		return chip->rm_pre;
	}
}

static int zy602_get_dod0_parameters(struct chip_bq27541 *chip, int *batt_dod0_1)
{
	unsigned char dod0_buffer[ZY602_MAC_CELL_DOD0_SIZE] = { 0 };

	if (!chip || (DEVICE_ZY0602 != chip->device_type) || !batt_dod0_1) {
		chg_err("gauge ic is NULL or is not zy602 or not allow_reading!\n");
		return -ENODEV;
	}

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, ZY602_MAC_CELL_DOD0_EN_ADDR, ZY602_MAC_CELL_DOD0_CMD);

	usleep_range(1000, 1000);
	bq27541_read_i2c_block(chip, ZY602_MAC_CELL_DOD0_ADDR, ZY602_MAC_CELL_DOD0_SIZE, dod0_buffer);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	*batt_dod0_1 = (dod0_buffer[11] << 8) | dod0_buffer[10];

	return 0;
}

static int bq28z610_get_dod0_parameters(
	struct chip_bq27541 *chip, int *batt_dod0_1, int *batt_dod0_2, int *batt_dod0_passed_q)
{
	unsigned char dod0_buffer[BQ28Z610_MAC_CELL_DOD0_SIZE] = {0, 0, 0, 0, 0, 0};
	short dod0_passed_q_temp;

	if (!chip || !chip->batt_bq28z610 || !batt_dod0_1 || !batt_dod0_2 || !batt_dod0_passed_q) {
		chg_err("gauge ic is NULL or is not 28z610!\n");
		return -ENODEV;
	}

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_MAC_CELL_DOD0_EN_ADDR,
			BQ28Z610_MAC_CELL_DOD0_CMD);
	usleep_range(1000, 1000);
	if (chip->batt_zy0603) {
		bq27541_read_i2c_block(chip, ZY0603_MAC_CELL_SOCCAL0,
			BQ28Z610_MAC_CELL_DOD0_SIZE, dod0_buffer);
	} else {
		bq27541_read_i2c_block(chip, BQ28Z610_MAC_CELL_DOD0_ADDR,
			BQ28Z610_MAC_CELL_DOD0_SIZE, dod0_buffer);
	}
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	dod0_passed_q_temp = (dod0_buffer[5] << 8) | dod0_buffer[4];
	*batt_dod0_1 = (dod0_buffer[1] << 8) | dod0_buffer[0];
	*batt_dod0_2 = (dod0_buffer[3] << 8) | dod0_buffer[2];
	if (dod0_passed_q_temp & 0x8000) {
		dod0_passed_q_temp = -((~(dod0_passed_q_temp-1)) & 0xFFFF);
	}
	*batt_dod0_passed_q = dod0_passed_q_temp;

	return 0;
}

static int zy602_get_qmax_parameters(struct chip_bq27541 *chip, int *batt_qmax_1)
{
	unsigned char qmax_buffer_1[ZY602_MAC_CELL_QMAX_SIZE_A] = { 0 };

	if (!chip || (DEVICE_ZY0602 != chip->device_type) || !batt_qmax_1) {
		chg_err("chip ic is NULL or is not zy602 or not allow_reading!\n");
		return -ENODEV;
	}

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, ZY602_MAC_CELL_QMAX_EN_ADDR, ZY602_MAC_CELL_QMAX_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(chip, ZY602_MAC_CELL_QMAX_ADDR_A, ZY602_MAC_CELL_QMAX_SIZE_A, qmax_buffer_1);

	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	*batt_qmax_1 = (qmax_buffer_1[17] << 8) | qmax_buffer_1[16];

	return 0;
}

static int bq28z610_get_qmax_parameters(
	struct chip_bq27541 *chip, int *batt_qmax_1, int *batt_qmax_2, int *batt_qmax_passed_q)
{
	unsigned char qmax_buffer_1[BQ28Z610_MAC_CELL_QMAX_SIZE_A] = {0, 0, 0, 0};
	unsigned char qmax_buffer_2[BQ28Z610_MAC_CELL_QMAX_SIZE_B] = {0, 0};
	short qmax_passed_q_temp;

	if (!chip || !chip->batt_bq28z610 || !batt_qmax_1 || !batt_qmax_2 || !batt_qmax_passed_q) {
		chg_err("gauge ic is NULL or is not 28z610!\n");
		return -ENODEV;
	}

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_MAC_CELL_QMAX_EN_ADDR,
		BQ28Z610_MAC_CELL_QMAX_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(chip, BQ28Z610_MAC_CELL_QMAX_ADDR_A,
		BQ28Z610_MAC_CELL_QMAX_SIZE_A, qmax_buffer_1);

	bq27541_i2c_txsubcmd(chip, BQ28Z610_MAC_CELL_QMAX_EN_ADDR,
		BQ28Z610_MAC_CELL_QMAX_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(chip, BQ28Z610_MAC_CELL_QMAX_ADDR_B,
		BQ28Z610_MAC_CELL_QMAX_SIZE_B, qmax_buffer_2);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	*batt_qmax_1 = (qmax_buffer_1[1] << 8) | qmax_buffer_1[0];
	*batt_qmax_2 = (qmax_buffer_1[3] << 8) | qmax_buffer_1[2];
	qmax_passed_q_temp = (qmax_buffer_2[1] << 8) | qmax_buffer_2[0];
	if (qmax_passed_q_temp & 0x8000) {
		qmax_passed_q_temp = -((~(qmax_passed_q_temp-1)) & 0xFFFF);
	}
	*batt_qmax_passed_q = qmax_passed_q_temp;

	return 0;
}

static int bq28z610_get_firstcell_voltage(struct chip_bq27541 *chip)
{
	if (!chip || !chip->batt_bq28z610) {
		chg_err("gauge ic is NULL or is not 28z610!\n");
		return 0;
	}

	bq28z610_get_2cell_voltage(chip);
	return chip->batt_cell_1_vol;
}

static int bq28z610_get_secondcell_voltage(struct chip_bq27541 *chip)
{
	if (!chip || !chip->batt_bq28z610) {
		chg_err("gauge ic is NULL or is not 28z610!\n");
		return 0;
	}

	bq28z610_get_2cell_voltage(chip);
	return chip->batt_cell_2_vol;
}

__maybe_unused static int bq27541_get_prev_batt_remaining_capacity(
	struct chip_bq27541 *chip)
{
	if (!chip) {
		return 0;
	}
	return chip->rm_pre;
}

static int zy602_get_battery_passed_q(struct chip_bq27541 *chip)
{
	int ret = 0;
	int passed_q = 0;

	if ((!chip) || (DEVICE_ZY0602 != chip->device_type))
		return 0;

	if (atomic_read(&chip->suspended) == 1)
		return chip->passed_q_pre;

	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_logbuf, &passed_q);
		if (ret) {
			dev_err(chip->dev, "error reading passed_q.\n");
			return ret;
		}
	} else {
		if (chip->passed_q_pre)
			return chip->passed_q_pre;
		else
			return 0;
	}
	chip->passed_q_pre = passed_q;
	return passed_q;
}

static int bq27541_get_battery_soc(struct chip_bq27541 *chip)
{
	int ret;
	int soc = 0;
	int retry = RETRY_CNT;

	if (!chip) {
		return 50;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return chip->soc_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_soc, &soc);
		while (!normal_range_judge(SOC_MAX, SOC_MIN, soc) && retry > 0 && !ret) {
			chg_err("bq27541_get_battery_soc %d out of range, retry %d\n", soc, retry);
			usleep_range(10000, 10000);
			ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_soc, &soc);
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading soc.ret:%d\n",
				ret);
			goto read_soc_err;
		}
	} else {
		if (chip->soc_pre) {
			return chip->soc_pre;
		} else {
			return 0;
		}
	}
	soc = bq27541_soc_calibrate(chip, soc);
	return soc;

read_soc_err:
	if (chip->soc_pre) {
		return chip->soc_pre;
	} else {
		return 0;
	}
}

static int bq27541_get_average_current(struct chip_bq27541 *chip)
{
	int ret;
	int curr = 0;
	int retry = RETRY_CNT;
	int temp_curr = 0;

	if (!chip) {
		return 0;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return -chip->current_pre;
	}
	if (atomic_read(&chip->locked) == 0) {
		ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_ai, &curr);
		if (!ret && (curr & 0x8000))
			temp_curr = ((~(curr - 1)) & 0xFFFF);
		else if (!ret)
			temp_curr = -curr;
		while (!normal_range_judge(CURR_MAX, CURR_MIN, temp_curr) && retry > 0 && !ret) {
			chg_err("bq27541_get_average_current %d out of range, retry %d\n", temp_curr, retry);
			usleep_range(10000, 10000);
			ret = bq27541_read_i2c(chip, chip->cmd_addr.reg_ai, &curr);
			if (!ret && (curr & 0x8000))
				temp_curr = ((~(curr - 1)) & 0xFFFF);
			else if (!ret)
				temp_curr = -curr;
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading current.\n");
			return chip->current_pre;
		}
	} else {
		return -chip->current_pre;
	}
	/* negative current */
	if (curr & 0x8000) {
		curr = -((~(curr - 1)) & 0xFFFF);
	}
	chip->current_pre = curr;
	return -curr;
}

static int bq27541_sha1_hmac_authenticate(
	struct chip_bq27541 *chip, struct bq27541_authenticate_data *authenticate_data);

static bool bq27541_get_battery_hmac(struct chip_bq27541 *chip)
{
	if (!chip) {
		return true;
	}

	if (chip->batt_bq28z610  && !oplus_is_rf_ftm_mode()) {
		/* 		return bq27541_is_authenticate_OK(gauge_ic); */
		get_smem_batt_info(&chip->auth_data, 1);
		if (init_gauge_auth(chip, &chip->auth_data, chip->authenticate_data))
			return true;
		chg_info("gauge authenticate failed, try again\n");
		get_smem_batt_info(&chip->auth_data, 0);
		return init_gauge_auth(chip, &chip->auth_data, chip->authenticate_data);
	} else if (chip->support_sha256_hmac && !oplus_is_rf_ftm_mode()) {
		return init_sha256_gauge_auth(chip);
	} else {
		return true;
	}
}

static bool bq27541_get_battery_authenticate(struct chip_bq27541 *chip)
{
	static bool get_temp = false;

	if (!chip) {
		return true;
	}

	if (chip->temp_pre == 0 && get_temp == false) {
		bq27541_get_battery_temperature(chip);
		msleep(10);
		bq27541_get_battery_temperature(chip);
	}
	get_temp = true;
	if (chip->temp_pre ==
	    (-400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN)) {
		return false;
	} else {
		return true;
	}
}

__maybe_unused static int bq27541_get_prev_battery_mvolts(struct chip_bq27541 *chip)
{
	if (!chip) {
		return 0;
	}
	return chip->batt_vol_pre;
}

__maybe_unused static int bq27541_get_prev_battery_mvolts_2cell_max(struct chip_bq27541 *chip)
{
	if (!chip) {
		return 0;
	}
	return chip->max_vol_pre;
}

__maybe_unused static int bq27541_get_prev_battery_mvolts_2cell_min(struct chip_bq27541 *chip)
{
	if (!chip) {
		return 0;
	}
	return chip->min_vol_pre;
}

__maybe_unused static int bq27541_get_prev_battery_temperature(struct chip_bq27541 *chip)
{
	if (!chip) {
		return 0;
	}
	return chip->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

__maybe_unused static int bq27541_get_prev_battery_soc(struct chip_bq27541 *chip)
{
	if (!chip) {
		return 50;
	}
	return chip->soc_pre;
}

__maybe_unused static int bq27541_get_prev_average_current(struct chip_bq27541 *chip)
{
	if (!chip) {
		return 0;
	}
	return -chip->current_pre;
}
static void bq27541_set_battery_full(bool full)
{
	/* Do nothing */
}

static void bq28z610_modify_dod0_parameter(struct chip_bq27541 *chip);

static int bq28z610_modify_dod0(struct chip_bq27541 *chip)
{
	if (!chip || chip->batt_zy0603) {
		return 0;
	}
	if (chip->batt_bq28z610) {
		bq28z610_modify_dod0_parameter(chip);
	}
	return 0;
}

static int zy0602_unseal(struct chip_bq27541 *chip, u32 key)
{
	u8 CNTL1_VAL_2[2] = {0x40, 0x00};
	u8 read_buf[5] = {0};
	int retry = 2;
	int rc = 0;

	do {
		bq27541_cntl_cmd(chip, 0x1115);
		usleep_range(5000, 5000);
		bq27541_cntl_cmd(chip, 0x1986);
		usleep_range(5000, 5000);
		bq27541_cntl_cmd(chip, 0xffff);
		usleep_range(5000, 5000);
		bq27541_cntl_cmd(chip, 0xffff);
		usleep_range(10000, 10000);

		bq27541_write_i2c_block(chip, 0x00, 2, CNTL1_VAL_2);
		chg_info("write {0x40,0x00} --> 0x00\n");
		usleep_range(2000, 2000);

		bq27541_read_i2c_block(chip, 0x00, 2, read_buf);
		chg_info("0x00 -->read [0x%02x][0x%02x]\n", read_buf[0], read_buf[1]);
		if (read_buf[0] == 0x62 && read_buf[1] == 0x01) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	chg_info("zy0602_unseal flag [%d]\n", rc);

	return rc;
}

static int zy0602_seal(struct chip_bq27541 *chip, u32 key)
{
	u8 CNTL1_VAL_2[2] = {0x20, 0x00};
	u8 CNTL1_VAL_3[2] = {0x40, 0x00};
	u8 read_buf[5] = {0};
	int retry = 2;
	int rc = 0;

	do {
		bq27541_write_i2c_block(chip, 0x00, 2, CNTL1_VAL_2);
		chg_info("write {0x40,0x00} --> 0x00\n");
		usleep_range(200000, 200000);

		bq27541_write_i2c_block(chip, 0x00, 2, CNTL1_VAL_3);
		chg_info("write {0x40,0x00} --> 0x00\n");
		usleep_range(2000, 2000);

		bq27541_read_i2c_block(chip, 0x00, 2, read_buf);
		chg_info("0x00 -->read [0x%02x][0x%02x]\n", read_buf[0], read_buf[1]);
		if (read_buf[0] == 0x00 && read_buf[1] == 0x00) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	chg_info("seal flag [%d]\n", rc);

	return rc;
}

/*ZY gauge : disable  full sleep mode*/
static int bq27441_battery_full_sleep_mode_cmd(struct chip_bq27541 *chip, bool enable)
{
	u8 rc = 0;
	u8 CNTL1_VAL_1[10] = { 0xC1, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	u8 CNTL1_VAL_3[10] = { 0xC1, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	u8 CNTL1_VAL_2[2] = { 0xFF, 0x55 };
	u8 read_buf[5] = { 0 };

	chg_info("begin, enable[%d]\n", enable);
	rc = zy0602_unseal(chip, 0);
	if (rc) {
		chg_err("zy0602_unseal fail return\n");
		return -1;
	}
	bq27541_i2c_txsubcmd_onebyte(chip, BQ27411_BLOCK_DATA_CONTROL, 0x00);
	usleep_range(2000, 2000);

	bq27541_i2c_txsubcmd_onebyte(chip, 0x3E, 0x40);
	usleep_range(5000, 5000);
	chg_info("write 0x40 --> 0x3E\n");

	bq27541_i2c_txsubcmd_onebyte(chip, 0x3F, 0x00);
	usleep_range(10000, 10000);
	chg_info("write 0x00 --> 0x3F\n");

	rc = bq27541_read_i2c_block(chip, 0x40, 2, read_buf);
	chg_info("0x40 -->read [0x%02x][0x%02x]\n", read_buf[0], read_buf[1]);
	if (!enable) {
		if (read_buf[0] == 0xC1 && read_buf[1] == 0x11) {
			chg_info("do nothing\n");
		} else {
			bq27541_write_i2c_block(chip, 0x40, 10, CNTL1_VAL_1);
			usleep_range(5000, 5000);
			chg_info("write {0xC1,0x11,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0} --> 0x40\n");

			bq27541_i2c_txsubcmd_onebyte(chip, 0x60, 0x2D);
			usleep_range(5000, 5000);
			chg_info("write 0x2D --> 0x60\n");

			rc = bq27541_read_i2c_block(chip, 0x40, 2, read_buf);
			chg_info("0x40 -->read [0x%02x][0x%02x]\n", read_buf[0], read_buf[1]);

			bq27541_write_i2c_block(chip, 0x00, 2, CNTL1_VAL_2);
			chg_info("write {0xFF,0x55} --> 0x40\n");
			usleep_range(500000, 500000);
		}
	} else if (chip->enable_sleep_mode || oplus_wired_shipmode_is_enabled()) {
		if (read_buf[1] != 0x31) {
			bq27541_write_i2c_block(chip, 0x40, 10, CNTL1_VAL_3);
			usleep_range(5000, 5000);
			chg_info("write {0xC1,0x31,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0} --> 0x40\n");

			bq27541_i2c_txsubcmd_onebyte(chip, 0x60, 0x0D);
			usleep_range(5000, 5000);
			chg_info("write 0x0D --> 0x60\n");

			rc = bq27541_read_i2c_block(chip, 0x40, 2, read_buf);
			chg_info("0x40 -->read [0x%02x][0x%02x]\n", read_buf[0], read_buf[1]);
			usleep_range(5000, 5000);

			bq27541_write_i2c_block(chip, 0x00, 2, CNTL1_VAL_2);
			chg_info("write {0xFF,0x55} --> 0x40\n");
			usleep_range(500000, 500000);
		}
	}
	rc = zy0602_seal(chip, 0);
	if (rc) {
		chg_err("zy0602_seal fail return\n");
		return -1;
	}

	chg_info("end\n");
	return GAUGE_OK;
}

static int bq27411_write_soc_smooth_parameter_for_zy(struct chip_bq27541 *chip, bool is_powerup)
{
	bq27441_battery_full_sleep_mode_cmd(chip, !is_powerup);
	return 1;
}

static void bq28z610_modify_soc_smooth_parameter(struct chip_bq27541 *chip);
static int zy0603_write_data_cmd(struct chip_bq27541 *chip, bool enable);
static int bq28z610_update_soc_smooth_parameter(struct chip_bq27541 *chip)
{
	if (!chip)
		return -1;
	if (chip->batt_zy0603) {
		pr_err("%s batt_zy0603 begin\n", __func__);
		zy0603_write_data_cmd(chip, true);
		return GAUGE_OK;
	}

	if (chip->device_type == DEVICE_ZY0602)
		bq27411_write_soc_smooth_parameter_for_zy(chip, false);

	if (chip->batt_bq28z610)
		bq28z610_modify_soc_smooth_parameter(chip);

	return 0;
}

int bq27541_get_passedchg(struct chip_bq27541 *chip, int *val)
{
	int rc = -1;
	u8 value;
	s16 v = 0;

	if (chip == NULL)
		return rc;

	if (atomic_read(&chip->suspended) == 1)
		return rc;

	/*enable block data control */
	if (chip->batt_bq28z610) {
		rc = bq27541_i2c_txsubcmd_onebyte(chip, BQ27411_BLOCK_DATA_CONTROL,
						  0x00);
		if (rc) {
			pr_err("%s enable block data control fail\n", __func__);
			goto out;
		}
		usleep_range(5000, 5000);

		bq27541_i2c_txsubcmd(chip, BQ27411_DATA_CLASS_ACCESS, 0x0074);
		usleep_range(10000, 10000);
		rc = bq27541_read_i2c_onebyte(chip, 0x4E, &value);
		if (rc) {
			pr_err("%s read 0x4E fail\n", __func__);
			goto out;
		}
		v |= value;
		rc = bq27541_read_i2c_onebyte(chip, 0x4F, &value);
		if (rc) {
			pr_err("%s read 0x4F fail\n", __func__);
			goto out;
		}
		v |= (value << 8);
	} else {
		rc = bq27541_read_i2c_onebyte(chip, 0x6C, &value);
		if (rc) {
			pr_err("%s read 0x6c fail\n", __func__);
			goto out;
		}
		v |= value;
		rc = bq27541_read_i2c_onebyte(chip, 0x6D, &value);
		if (rc) {
			pr_err("%s read 0x6d fail\n", __func__);
			goto out;
		}
		v |= (value << 8);
	}

	*val = v;

	if (atomic_read(&chip->suspended) == 1)
		return -1;
out:
	return rc;
}

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
		chip->cmd_addr.subcmd_disable_dlog =
			BQ27541_SUBCMD_DISABLE_DLOG;
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
		chip->cmd_addr.subcmd_disable_dlog =
			BQ27411_SUBCMD_DISABLE_DLOG;
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

static void oplus_set_device_type_by_extern_cmd(struct chip_bq27541 *chip)
{
	int ret;
	int device_type = 0;
	int data_check;
	int try_count = GAUGE_SUBCMD_TRY_COUNT;
	u8 extend_data[4] = {0};

	chip->device_type = DEVICE_BQ27541;
	chip->device_type_for_vooc = DEVICE_TYPE_FOR_VOOC_BQ27541;
try:
	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	ret = bq27541_i2c_txsubcmd(chip, GAUGE_EXTERN_DATAFLASHBLOCK, GAUGE_SUBCMD_DEVICE_TYPE);
	if (ret < 0) {
		mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
		return;
	}

	msleep(5);
	ret = bq27541_read_i2c_block(chip, GAUGE_EXTERN_DATAFLASHBLOCK, ARRAY_SIZE(extend_data), extend_data);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	if (ret < 0)
		return;
	data_check = (extend_data[1] << 0x8) | extend_data[0];
	if (try_count-- > 0 && data_check != GAUGE_SUBCMD_DEVICE_TYPE) {
		pr_info("not match. extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
			extend_data[0], extend_data[1]);
		goto try;
	}
	if (try_count) {
		device_type = (extend_data[3] << 0x8) | extend_data[2];
		if (device_type == DEVICE_TYPE_NFG8011B)
			chip->batt_nfg8011b = true;
	}
	chg_info("device_type : 0x%04x\n", device_type);
}

static void bq27541_hw_config(struct chip_bq27541 *chip)
{
	int ret = 0;
	int flags = 0;
	int device_type = 0;
	int fw_ver = 0;

	if (chip->support_extern_cmd) {
		oplus_set_device_type_by_extern_cmd(chip);
		goto device_type_set;
	}

	bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_read_i2c(chip, BQ27541_BQ27411_REG_CNTL, &flags);
	udelay(66);
	ret = bq27541_read_i2c(chip, BQ27541_BQ27411_REG_CNTL, &flags);
	if (ret < 0) {
		chip->device_type = DEVICE_BQ27541;
		pr_err(" error reading register %02x ret = %d\n",
		       BQ27541_BQ27411_REG_CNTL, ret);
		return;
	}
	udelay(66);
	bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_DEVICE_TYPE);
	udelay(66);
	bq27541_read_i2c(chip, BQ27541_BQ27411_REG_CNTL, &device_type);
	udelay(66);
	bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_cntl_cmd(chip, BQ27541_BQ27411_SUBCMD_FW_VER);
	udelay(66);
	bq27541_read_i2c(chip, BQ27541_BQ27411_REG_CNTL, &fw_ver);

device_type_set:
	if (device_type == DEVICE_TYPE_BQ27411) {
		chip->device_type = DEVICE_BQ27411;
		chip->device_type_for_vooc = DEVICE_TYPE_FOR_VOOC_BQ27411;
	} else if (device_type == DEVICE_TYPE_ZY0602) {
		chip->device_type = DEVICE_ZY0602;
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
	    device_type == DEVICE_TYPE_ZY0603 || chip->batt_nfg8011b) {
		chip->cmd_addr.reg_ai = BQ28Z610_REG_TI;
	}
	oplus_set_fg_device_type(chip->device_type);
	dev_err(chip->dev,
		"DEVICE_TYPE is 0x%02X, FIRMWARE_VERSION is 0x%02X  batt_zy0603 = %d\n",
		device_type, fw_ver, chip->batt_zy0603);
}

static int bq27541_get_id_status(struct chip_bq27541 *chip)
{
	int value_up = 0, value_down = 0;
	int gpio_value = GPIO_STATUS_NOT_SUPPORT;

	if (!chip || !gpio_is_valid(chip->id_gpio) ||
	    IS_ERR_OR_NULL(chip->pinctrl) || IS_ERR_OR_NULL(chip->id_not_pull) ||
	    IS_ERR_OR_NULL(chip->id_pull_down) || IS_ERR_OR_NULL(chip->id_pull_up)) {
		return GPIO_STATUS_NOT_SUPPORT;
	}

	mutex_lock(&chip->pinctrl_lock);
	gpio_direction_input(chip->id_gpio);
	pinctrl_select_state(chip->pinctrl, chip->id_pull_up);
	usleep_range(10000, 10000);
	value_up = gpio_get_value(chip->id_gpio);

	pinctrl_select_state(chip->pinctrl, chip->id_pull_down);
	usleep_range(10000, 10000);
	value_down = gpio_get_value(chip->id_gpio);

	pinctrl_select_state(chip->pinctrl, chip->id_not_pull);

	if (value_up == 1 && value_down == 0)
		gpio_value = GPIO_STATUS_NC;
	else if (value_up == 0 && value_down == 0)
		gpio_value = GPIO_STATUS_PD;
	else if (value_up == 1 && value_down == 1)
		gpio_value = GPIO_STATUS_PU;
	chg_info("value_up=%d value_down=%d\n", value_up, value_down);
	mutex_unlock(&chip->pinctrl_lock);

	return gpio_value;
}

static int bq27541_gpio_init(struct chip_bq27541 *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;

	mutex_init(&chip->pinctrl_lock);
	chip->id_gpio = of_get_named_gpio(node, "oplus,id-gpio", 0);
	if (!gpio_is_valid(chip->id_gpio)) {
		chg_err("id gpio not specified\n");
		return -EINVAL;
	}
	rc = gpio_request(chip->id_gpio, "bq27541-id-gpio");
	if (rc < 0) {
		chg_err("bq27541-id gpio request error, rc=%d\n", rc);
		return rc;
	}
	rc = of_property_read_u32(node, "oplus,id-match-status", &chip->id_match_status);
	if (rc < 0) {
		chg_err("oplus,id-match-status read failed, rc=%d\n", rc);
		chip->id_match_status = GPIO_STATUS_NOT_SUPPORT;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	chip->id_not_pull = pinctrl_lookup_state(chip->pinctrl, "id_not_pull");
	if (IS_ERR_OR_NULL(chip->id_not_pull)) {
		chg_err("get id_not_pull fail\n");
		goto free_id_gpio;
	}

	chip->id_pull_up = pinctrl_lookup_state(chip->pinctrl, "id_pull_up");
	if (IS_ERR_OR_NULL(chip->id_pull_up)) {
		chg_err("get id_pull_up fail\n");
		goto free_id_gpio;
	}
	chip->id_pull_down = pinctrl_lookup_state(chip->pinctrl, "id_pull_down");
	if (IS_ERR_OR_NULL(chip->id_pull_down)) {
		chg_err("get id_pull_down fail\n");
		goto free_id_gpio;
	}

	chip->id_value = bq27541_get_id_status(chip);

	chg_info("id gpio value %d %s\n", chip->id_value, gpio_status_name[chip->id_value]);
	return 0;

free_id_gpio:
	if (gpio_is_valid(chip->id_gpio))
		gpio_free(chip->id_gpio);
	return rc;
}

#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
static bool test_kit_battery_id_gpio_test(struct test_feature *feature, char *buf, size_t len)
{
	struct chip_bq27541 *chip;
	int index = 0;
	int gpio_status = GPIO_STATUS_NOT_SUPPORT;

	if (buf == NULL) {
		pr_err("buf is NULL\n");
		return false;
	}
	if (feature == NULL) {
		pr_err("feature is NULL\n");
		index += snprintf(buf + index, len - index, "feature is NULL");
		return false;
	}

	chip = feature->private_data;
	gpio_status = bq27541_get_id_status(chip);
	index += snprintf(buf + index, len - index, "id-%d: %s\n",
			  chip->gauge_num, gpio_status_name[gpio_status]);
	return true;
}

static bool test_kit_fpga_fg_test(struct test_feature *feature, char *buf, size_t len)
{
	struct chip_bq27541 *chip;
	int index = 0;
	int temp = 0;
	bool test_reset = false;
	int i = 0;

	if (buf == NULL) {
		pr_err("buf is NULL\n");
		return false;
	}
	if (feature == NULL) {
		pr_err("feature is NULL\n");
		index += snprintf(buf + index, len - index, "feature is NULL");
		return false;
	}

	chip = feature->private_data;

	for(i = 0; i < 3; i++) {
		temp = bq27541_get_battery_temperature(chip);
		chg_info("temp = %d\n", temp);
		if (temp > -400)
			test_reset = true;
		else
			return false;
	}

	return test_reset;
}

static const struct test_feature_cfg battery_id_gpio_test_cfg = {
	.name = "battery_id_gpio_test",
	.test_func = test_kit_battery_id_gpio_test,
};

static const struct test_feature_cfg fpga_fg_test_cfg = {
	.name = "fpga_fg_test",
	.test_func = test_kit_fpga_fg_test,
};
#endif

static void bq27541_parse_dt(struct chip_bq27541 *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;

	chip->calib_info_save_support =
		of_property_read_bool(node, "oplus,calib_info_save_support");
	chip->modify_soc_smooth =
		of_property_read_bool(node, "qcom,modify-soc-smooth");
	chip->modify_soc_calibration =
		of_property_read_bool(node, "qcom,modify-soc-calibration");
	chip->remove_iterm_taper = of_property_read_bool(node, "oplus,remove_iterm_taper");
	chip->batt_bq28z610 = of_property_read_bool(node, "qcom,batt_bq28z610");
	chip->batt_bq27z561 = false;
	chip->bq28z610_need_balancing =
		of_property_read_bool(node, "qcom,bq28z610_need_balancing");
	chip->enable_sleep_mode = of_property_read_bool(node, "oplus,enable_sleep_mode");
	chip->battery_full_param = of_property_read_bool(
		node,
		"qcom,battery-full-param"); /* only for wite battery full param in guage dirver probe on 7250 platform */
	rc = of_property_read_u32(node, "qcom,sha1_key_index",
				  &chip->sha1_key_index);
	if (rc) {
		chip->sha1_key_index = 0;
	}
	rc = of_property_read_u32(node, "oplus,batt_num", &chip->batt_num);
	if (rc < 0) {
		chg_err("can't get oplus,batt_num, rc=%d\n", rc);
		chip->batt_num = 1;
	}
	rc = of_property_read_u32(node, "qcom,gauge_num", &chip->gauge_num);
	if (rc) {
		chip->gauge_num = 0;
	}
	chip->zy0603_soc_opt =
		of_property_read_bool(node, "oplus,zy0603_soc_opt");
	/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
	chip->i2c_rst_ext = of_property_read_bool(node, "oplus,i2c_rst_ext");
	/* end workaround 230504153935012779 */

	chip->support_extern_cmd = of_property_read_bool(node, "oplus,support_extern_cmd");
	if (chip->support_extern_cmd)
		chip->support_sha256_hmac = true;

	chip->batt_nfg8011b = of_property_read_bool(node, "oplus,batt_nfg8011b");
	if (chip->batt_nfg8011b)
		chg_info("batt_nfg8011b gauge %d\n", chip->batt_nfg8011b);

	chip->fpga_test_support = of_property_read_bool(node, "oplus,fpga_test_support");
	if (chip->fpga_test_support) {
		chg_info("fpga_test_support gauge %d\n", chip->gauge_num);
	}

	rc = bq27541_gpio_init(chip);
	if (rc < 0) {
		chg_err("bq27541 gpio init failed, rc = %d!\n", rc);
	}

	chip->support_eco_design = !!(oplus_chg_get_nvid_support_flags() & BIT(ECO_DESIGN_SUPPORT_REGION));
}

static int sealed(struct chip_bq27541 *chip)
{
	/*    return control_cmd_read(di, CONTROL_STATUS) & (1 << 13);*/
	int value = 0;

	bq27541_cntl_cmd(chip, CONTROL_STATUS);
	/*    bq27541_cntl_cmd(di, CONTROL_STATUS);*/
	usleep_range(10000, 10000);
	bq27541_read_i2c(chip, CONTROL_STATUS, &value);
	/*    chg_info(" REG_CNTL: 0x%x\n", value); */

	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		return value & BIT(14);
	} else if (chip->device_type == DEVICE_BQ27411) {
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
	return 0;
}

static int unseal(struct chip_bq27541 *chip, u32 key)
{
	int i = 0;

	if (!sealed(chip)) {
		goto out;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
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
	} else if (chip->device_type == DEVICE_BQ27411) {
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
	chg_info("bq27541 : i=%d\n", i);

	if (i == SEAL_POLLING_RETRY_LIMIT) {
		pr_err("bq27541 failed\n");
		return 0;
	} else {
		return 1;
	}
}

static int zy0603_seal(struct chip_bq27541 *chip)
{
	u8 seal_cmd[2] = {0x30, 0x00};
	u8 seal_op_st[2] = {0x54, 0x00};
	u8 seal_cmd2[2] = {0x45, 0x00};
	u8 read_buf[6] = {0};
	int retry = 2;
	int rc = 0;

	if (!chip)
		return -1;

	do {
		bq27541_write_i2c_block(chip, 0x00, 2, seal_cmd2);
		pr_err("%s write {0x45,0x00} --> 0x00\n", __func__);
		usleep_range(100000, 100000);

		bq27541_write_i2c_block(chip, 0x00, 2, seal_cmd);
		pr_err("%s write {0x30,0x00} --> 0x00\n", __func__);
		usleep_range(1000, 1000);

		bq27541_write_i2c_block(chip, 0x3E, 2, seal_op_st);
		pr_err("%s write {0x54,0x00} --> 0x3E\n", __func__);
		usleep_range(1000, 1000);

		bq27541_read_i2c_block(chip, 0x3E, 6, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__,
			read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);
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

	return rc;
}

static int zy0603_unseal(struct chip_bq27541 *chip)
{
	u8 seal_cmd_1[2] = {0x78, 0x56};
	u8 seal_cmd_2[2] = {0x34, 0x12};
	u8 seal_op_st[2] = {0x54, 0x00};
	u8 read_buf[6] = {0};
	int retry = 2;
	int rc = 0;

	if (!chip)
		return -1;

	do {
		bq27541_write_i2c_block(chip, 0x00, 2, seal_cmd_1);
		pr_err("%s write {0x78,0x56} --> 0x00\n", __func__);
		usleep_range(1000, 1000);

		bq27541_write_i2c_block(chip, 0x00, 2, seal_cmd_2);
		pr_err("%s write {0x34,0x12} --> 0x00\n", __func__);
		usleep_range(1000, 1000);

		bq27541_write_i2c_block(chip, 0x3E, 2, seal_op_st);
		pr_err("%s write {0x54,0x00} --> 0x3E\n", __func__);
		usleep_range(1000, 1000);

		bq27541_read_i2c_block(chip, 0x3E, 6, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n", __func__,
			read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);
		if ((read_buf[3] & 0x01) == 0) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	pr_err("%s zy0603_unseal flag [%d]\n", __func__, rc);

	return rc;
}

static int zy0603_battery_current_check_cmd(struct chip_bq27541 *chip, bool enable)
{
	u8 current_check[2] = {0x30, 0x05};
	u8 op_st[3] = {0x21, 0x47, 0x67};
	u8 read_buf[6] = {0};
	int rc = 0;

	bq27541_write_i2c_block(chip, 0x3E, 3, op_st);
	pr_err("%s write {0x21, 0x47, 0x67} --> 0x3E\n", __func__);
	usleep_range(5000, 5000);

	bq27541_write_i2c_block(chip, 0x60, 2, current_check);
	pr_err("%s write {0x30, 0x05} --> 0x60\n", __func__);
	usleep_range(5000, 5000);

	bq27541_read_i2c_block(chip, 0x3E, 3, read_buf);
	pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__,
		read_buf[0], read_buf[1], read_buf[2]);

	return rc;
}

static int zy0603_battery_sleep_mode_cmd(struct chip_bq27541 *chip, bool enable)
{
	u8 sleep_cmd[2] = {0x9A, 0x05};
	u8 sleep_reg[2] = {0x24, 0x41};
	u8 op_st[3] = {0x24, 0x41, 0x00};
	u8 read_buf[6] = {0};
	int rc = 0;

	bq27541_write_i2c_block(chip, 0x3E, 2, sleep_reg);
	pr_err("%s write {0x24, 0x41} --> 0x3E\n", __func__);
	usleep_range(5000, 5000);
	bq27541_read_i2c_block(chip, 0x3E, 3, read_buf);
	pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__,
		read_buf[0], read_buf[1], read_buf[2]);
	if(read_buf[2] == 0) {
		return -1;
	}

	bq27541_write_i2c_block(chip, 0x3E, 3, op_st);
	pr_err("%s write {0x24, 0x41, 0x00} --> 0x3E\n", __func__);
	usleep_range(5000, 5000);

	bq27541_write_i2c_block(chip, 0x60, 2, sleep_cmd);
	pr_err("%s write {0x9A, 0x05} --> 0x60\n", __func__);
	usleep_range(5000, 5000);

	bq27541_read_i2c_block(chip, 0x3E, 3, read_buf);
	pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__,
		read_buf[0], read_buf[1], read_buf[2]);

	return rc;
}

static int zy0603_battery_sleep_enable_cmd(struct chip_bq27541 *chip)
{
	u8 sleep_cmd[2] = {0x97, 0x05};
	u8 sleep_reg[2] = {0x24, 0x41};
	u8 op_st[3] = {0x24, 0x41, 0x03};
	u8 read_buf[6] = {0};
	int rc = 0;

	bq27541_write_i2c_block(chip, 0x3E, 2, sleep_reg);
	pr_err("%s write {0x24, 0x41} --> 0x3E\n", __func__);
	usleep_range(5000, 5000);
	bq27541_read_i2c_block(chip, 0x3E, 3, read_buf);
	pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__,
		read_buf[0], read_buf[1], read_buf[2]);
	if(read_buf[2] == 0) {
		bq27541_write_i2c_block(chip, 0x3E, 3, op_st);
		pr_err("%s write {0x24, 0x41, 0x03} --> 0x3E\n", __func__);
		usleep_range(5000, 5000);

		bq27541_write_i2c_block(chip, 0x60, 2, sleep_cmd);
		pr_err("%s write {0x97, 0x05} --> 0x60\n", __func__);
		usleep_range(5000, 5000);

		bq27541_read_i2c_block(chip, 0x3E, 3, read_buf);
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x]\n", __func__,
			read_buf[0], read_buf[1], read_buf[2]);
	}

	return rc;
}
static int zy0603_write_data_cmd(struct chip_bq27541 *chip, bool enable)
{
	int rc = 0;
	int retry_cnt = 0;
	int error_flag = 0;

	if (!chip)
		return -ENODEV;

	rc = zy0603_unseal(chip);
	if (rc == 0) {
		if (enable == false) {
			rc = zy0603_battery_sleep_mode_cmd(chip, enable);
			if (rc == 0) {
				zy0603_battery_current_check_cmd(chip, enable);
			}
		} else {
			zy0603_battery_sleep_enable_cmd(chip);
		}

		if (chip->batt_zy0603 && chip->afi_count > 0) {
			do {
				error_flag = 0;
				if (GAUGE_OK != zy0603_disable_ssdf_and_pf_check(chip) ||
					GAUGE_OK != zy0603_check_ssdf_pf_checksum(chip)) {
						retry_cnt++;
						error_flag = 1;
						zy0603_disable_ssdf_and_pf(chip);
				}
			} while (retry_cnt < 5 && error_flag == 1);
			if (retry_cnt < 5) {
				chip->disabled = true;
				pr_err("%s v12 zy0603_disable_ssdf_and_pf_check ok\n", __func__);
			} else {
				pr_err("%s v12 zy0603_disable_ssdf_and_pf_check failed\n", __func__);
			}

			if (!zy0603_seal(chip)) {
				zy0603_start_checksum_cal(chip);
			}
		} else {
			zy0603_seal(chip);
		}
	}

	return rc;
}

static int bq27411_write_block_data_cmd(struct chip_bq27541 *chip, int block_id,
					u8 reg_addr, u8 new_value)
{
	int rc = 0;
	u8 old_value = 0, old_csum = 0, new_csum = 0;
	/*u8 new_csum_test = 0, csum_temp = 0;*/

	if (oplus_is_rf_ftm_mode())
		return 0;
	usleep_range(1000, 1000);
	bq27541_i2c_txsubcmd(chip, BQ27411_DATA_CLASS_ACCESS, block_id);
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
	bq27541_i2c_txsubcmd_onebyte(chip, reg_addr, new_value);
	usleep_range(1000, 1000);
	new_csum = (old_value + old_csum - new_value) & 0xff;
	/*
	csum_temp = (255 - old_csum - old_value) % 256;
	new_csum_test = 255 - ((csum_temp + new_value) % 256);
*/
	usleep_range(1000, 1000);
	bq27541_i2c_txsubcmd_onebyte(chip, BQ27411_CHECKSUM_ADDR, new_csum);
	pr_err("bq27411 write blk_id = 0x%x, addr = 0x%x, old_val = 0x%x, new_val = 0x%x, old_csum = 0x%x, new_csum = 0x%x\n",
	       block_id, reg_addr, old_value, new_value, old_csum, new_csum);
	return 0;
}

static int bq27411_read_block_data_cmd(struct chip_bq27541 *chip, int block_id,
				       u8 reg_addr)
{
	u8 value = 0;

	if (oplus_is_rf_ftm_mode())
		return 0;
	usleep_range(1000, 1000);
	bq27541_i2c_txsubcmd(chip, BQ27411_DATA_CLASS_ACCESS, block_id);
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
			rc = bq27541_read_i2c(chip, BQ27411_SUBCMD_CONFIG_MODE,
					      &config_mode);
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
			rc = bq27541_read_i2c(chip, BQ27411_SUBCMD_CONFIG_MODE,
					      &config_mode);
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
		pr_err("%s fail config_mode = 0x%x, enable = %d\n", __func__,
		       config_mode, enable);
		return 1;
	} else {
		pr_err("%s success i = %d, config_mode = 0x%x, enable = %d\n",
		       __func__, i, config_mode, enable);
		return 0;
	}
}

static bool bq27411_check_soc_smooth_parameter(struct chip_bq27541 *chip,
					       bool is_powerup)
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
	rc = bq27541_i2c_txsubcmd_onebyte(chip, BQ27411_BLOCK_DATA_CONTROL, 0x00);
	if (rc) {
		pr_err("%s enable block data control fail\n", __func__);
		goto check_error;
	}
	usleep_range(5000, 5000);
	/*check cc-dead-band*/
	value_read = bq27411_read_block_data_cmd(chip, BQ27411_CC_DEAD_BAND_ID,
						 BQ27411_CC_DEAD_BAND_ADDR);
	if (value_read != dead_band_val) {
		pr_err("%s cc_dead_band error, value_read = 0x%x\n", __func__,
		       value_read);
		goto check_error;
	}
	/*check opconfigB*/
	value_read = bq27411_read_block_data_cmd(chip, BQ27411_OPCONFIGB_ID,
						 BQ27411_OPCONFIGB_ADDR);
	if (value_read != op_cfgb_val) {
		pr_err("%s opconfigb error, value_read = 0x%x\n", __func__,
		       value_read);
		goto check_error;
	}
	/*check dodateoc*/
	value_read = bq27411_read_block_data_cmd(chip, BQ27411_DODATEOC_ID,
						 BQ27411_DODATEOC_ADDR);
	if (value_read != dodat_val) {
		pr_err("%s dodateoc error, value_read = 0x%x\n", __func__,
		       value_read);
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
	int rc = 0;
	u8 reg_data = 0;
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
	bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
	usleep_range(15000, 15000);
	pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0],
	       CNTL1_VAL_1[1]);

	rc = bq27541_read_i2c_block(chip, 0x40, 2, read_buf);
	pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0],
	       read_buf[1]);
	if (read_buf[0] == 0xFF && read_buf[1] == 0xBA) {
		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		usleep_range(15000, 15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0xFE;
		CNTL1_VAL_1[1] = 0xCF;
		bq27541_write_i2c_block(chip, 0x40, 2, CNTL1_VAL_1);
		pr_err("%s 0x40 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		CNTL1_VAL_2[0] = 0x00;
		CNTL1_VAL_2[1] = 0x00;
		bq27541_write_i2c_block(chip, 0x42, 2, CNTL1_VAL_2);
		pr_err("%s 0x42 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_2[0], CNTL1_VAL_2[1]);

		CNTL1_VAL_3[0] = 0xFE;
		CNTL1_VAL_3[1] = 0x03;
		bq27541_write_i2c_block(chip, 0x44, 2, CNTL1_VAL_3);
		pr_err("%s 0x44 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_3[0], CNTL1_VAL_3[1]);

		CNTL1_VAL_4[0] = 0x2A;
		CNTL1_VAL_4[1] = 0xD9;
		bq27541_write_i2c_block(chip, 0x46, 2, CNTL1_VAL_4);
		pr_err("%s 0x46 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_4[0], CNTL1_VAL_4[1]);

		CNTL1_VAL_5[0] = 0x08;
		CNTL1_VAL_5[1] = 0x90;
		bq27541_write_i2c_block(chip, 0x48, 2, CNTL1_VAL_5);
		pr_err("%s 0x48 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_5[0], CNTL1_VAL_5[1]);

		CNTL1_VAL_6[0] = 0xDA;
		CNTL1_VAL_6[1] = 0xFA;
		bq27541_write_i2c_block(chip, 0x4A, 2, CNTL1_VAL_6);
		pr_err("%s 0x4A -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_6[0], CNTL1_VAL_6[1]);

		CNTL1_VAL_7[0] = 0xD3;
		CNTL1_VAL_7[1] = 0xDE;
		bq27541_write_i2c_block(chip, 0x4C, 2, CNTL1_VAL_7);
		pr_err("%s 0x4C -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_7[0], CNTL1_VAL_7[1]);

		CNTL1_VAL_8[0] = 0xC2;
		CNTL1_VAL_8[1] = 0x3D;
		bq27541_write_i2c_block(chip, 0x4E, 2, CNTL1_VAL_8);
		pr_err("%s 0x4E -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_8[0], CNTL1_VAL_8[1]);

		CNTL1_VAL_9[0] = 0x7F;
		CNTL1_VAL_9[1] = 0x00;
		bq27541_write_i2c_block(chip, 0x50, 2, CNTL1_VAL_9);
		pr_err("%s 0x50 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_9[0], CNTL1_VAL_9[1]);

		bq27541_i2c_txsubcmd_onebyte(chip,
			0x60,
			(0xFFFF -
			 (CNTL1_VAL_1[0] + CNTL1_VAL_1[1] + CNTL1_VAL_2[0] +
			  CNTL1_VAL_2[1] + CNTL1_VAL_3[0] + CNTL1_VAL_3[1] +
			  CNTL1_VAL_4[0] + CNTL1_VAL_4[1] + CNTL1_VAL_5[0] +
			  CNTL1_VAL_5[1] + CNTL1_VAL_6[0] + CNTL1_VAL_6[1] +
			  CNTL1_VAL_7[0] + CNTL1_VAL_7[1] + CNTL1_VAL_8[0] +
			  CNTL1_VAL_8[1] + CNTL1_VAL_9[0] + CNTL1_VAL_9[1])));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (0xFFFF -
			(CNTL1_VAL_1[0] + CNTL1_VAL_1[1] + CNTL1_VAL_2[0] +
			 CNTL1_VAL_2[1] + CNTL1_VAL_3[0] + CNTL1_VAL_3[1] +
			 CNTL1_VAL_4[0] + CNTL1_VAL_4[1] + CNTL1_VAL_5[0] +
			 CNTL1_VAL_5[1] + CNTL1_VAL_6[0] + CNTL1_VAL_6[1] +
			 CNTL1_VAL_7[0] + CNTL1_VAL_7[1] + CNTL1_VAL_8[0] +
			 CNTL1_VAL_8[1] + CNTL1_VAL_9[0] + CNTL1_VAL_9[1])));
		usleep_range(30000, 30000);

		CNTL1_VAL_1[0] = 0x52;
		CNTL1_VAL_1[1] = 0x00;
		bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000, 15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		rc = bq27541_read_i2c_block(chip, 0x5B, 2, read_buf);
		pr_err("%s 0x5B -->read [0x%02x][0x%02x]\n", __func__,
		       read_buf[0], read_buf[1]);

		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		usleep_range(15000, 15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x00;
		CNTL1_VAL_1[1] = 0x8F;
		bq27541_write_i2c_block(chip, 0x5B, 2, CNTL1_VAL_1);
		pr_err("%s 0x5B -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		bq27541_i2c_txsubcmd_onebyte(chip, 0x60, (read_buf[0] + read_buf[1] +
						    reg_data - CNTL1_VAL_1[0] -
						    CNTL1_VAL_1[1]));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] -
			CNTL1_VAL_1[1]));
		usleep_range(15000, 15000);

		CNTL1_VAL_1[0] = 0x52;
		CNTL1_VAL_1[1] = 0x00;
		usleep_range(15000, 15000);
		bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000, 15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		rc = bq27541_read_i2c_block(chip, 0x5D, 2, read_buf);
		pr_err("%s 0x5D -->read [0x%02x][0x%02x]\n", __func__,
		       read_buf[0], read_buf[1]);

		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		usleep_range(15000, 15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x10;
		CNTL1_VAL_1[1] = 0xEF;
		bq27541_write_i2c_block(chip, 0x5D, 2, CNTL1_VAL_1);
		pr_err("%s 0x5D -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		bq27541_i2c_txsubcmd_onebyte(chip, 0x60, (read_buf[0] + read_buf[1] +
						    reg_data - CNTL1_VAL_1[0] -
						    CNTL1_VAL_1[1]));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] -
			CNTL1_VAL_1[1]));
		usleep_range(15000, 15000);

		CNTL1_VAL_1[0] = 0x52;
		CNTL1_VAL_1[1] = 0x00;
		bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000, 15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		rc = bq27541_read_i2c_block(chip, 0x56, 2, read_buf);
		pr_err("%s 0x56 -->read [0x%02x][0x%02x]\n", __func__,
		       read_buf[0], read_buf[1]);
		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		usleep_range(15000, 15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x00;
		CNTL1_VAL_1[1] = 0x14;
		bq27541_write_i2c_block(chip, 0x56, 2, CNTL1_VAL_1);
		pr_err("%s 0x56 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		bq27541_i2c_txsubcmd_onebyte(chip, 0x60, (read_buf[0] + read_buf[1] +
						    reg_data - CNTL1_VAL_1[0] -
						    CNTL1_VAL_1[1]));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] -
			CNTL1_VAL_1[1]));
		usleep_range(15000, 15000);

		CNTL1_VAL_1[0] = 0x59;
		CNTL1_VAL_1[1] = 0x00;
		bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000, 15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		rc = bq27541_read_i2c_block(chip, 0x40, 2, read_buf);
		pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__,
		       read_buf[0], read_buf[1]);
		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		usleep_range(15000, 15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x00;
		CNTL1_VAL_1[1] = 0x4b;
		bq27541_write_i2c_block(chip, 0x40, 2, CNTL1_VAL_1);
		pr_err("%s 0x40 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		bq27541_i2c_txsubcmd_onebyte(chip, 0x60, (read_buf[0] + read_buf[1] +
						    reg_data - CNTL1_VAL_1[0] -
						    CNTL1_VAL_1[1]));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] -
			CNTL1_VAL_1[1]));
		usleep_range(15000, 15000);
	}

	CNTL1_VAL_1[0] = 0x59;
	CNTL1_VAL_1[1] = 0x00;
	bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1);
	usleep_range(15000, 15000);
	pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0],
	       CNTL1_VAL_1[1]);

	rc = bq27541_read_i2c_block(chip, 0x40, 2, read_buf);
	pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0],
	       read_buf[1]);

	if (read_buf[0] != 0x00 || read_buf[1] != 0x4b) {
		rc = bq27541_read_i2c_onebyte(chip, 0x60, &reg_data);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);
		usleep_range(15000, 15000);

		CNTL1_VAL_2[0] = 0x00;
		CNTL1_VAL_2[1] = 0x4B;
		bq27541_write_i2c_block(chip, 0x40, 2, CNTL1_VAL_2);
		pr_err("%s 0x40 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_2[0], CNTL1_VAL_2[1]);

		bq27541_i2c_txsubcmd_onebyte(chip, 0x60, (read_buf[0] + read_buf[1] +
						    reg_data - CNTL1_VAL_2[0] -
						    CNTL1_VAL_2[1]));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,
		       (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_2[0] -
			CNTL1_VAL_2[1]));
		usleep_range(30000, 30000);
		pr_err("%s end\n", __func__);
	}

	if (rc < 0)
		chg_err("read i2c err\n");

	pr_err("%s end\n", __func__);
	return GAUGE_OK;
}

static int bq27411_write_soc_smooth_parameter(struct chip_bq27541 *chip,
					      bool is_powerup)
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
	bq27541_i2c_txsubcmd_onebyte(chip, BQ27411_BLOCK_DATA_CONTROL, 0x00);

	/* only for wite battery full param in guage dirver probe on 7250 platform */
	if (chip->battery_full_param) {
		usleep_range(5000, 5000);
		rc = bq27441_battery_full_param_write_cmd(chip);
		if (rc == BATT_FULL_ERROR)
			return BATT_FULL_ERROR;
	}

	usleep_range(5000, 5000);
	/* step1: update cc-dead-band */
	rc = bq27411_write_block_data_cmd(chip, BQ27411_CC_DEAD_BAND_ID,
					  BQ27411_CC_DEAD_BAND_ADDR,
					  dead_band_val);
	if (rc) {
		pr_err("%s cc_dead_band fail\n", __func__);
		goto exit_config_mode;
	}
	/* step2: update opconfigB */
	rc = bq27411_write_block_data_cmd(chip, BQ27411_OPCONFIGB_ID,
					  BQ27411_OPCONFIGB_ADDR, op_cfgb_val);
	if (rc) {
		pr_err("%s opconfigB fail\n", __func__);
		goto exit_config_mode;
	}
	/* step3: update dodateoc */
	rc = bq27411_write_block_data_cmd(chip, BQ27411_DODATEOC_ID,
					  BQ27411_DODATEOC_ADDR, dodat_val);
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

static int bq8z610_sealed(struct chip_bq27541 *chip)
{
	int value = 0;
	int ret = 0;
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = { 0 };

	ret = bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_SEAL_STATUS);
	if (ret == -1) {
		chg_err("bq8z610 sealed, bq27541_i2c_txsubcmd ret error");
		return -1;
	}
	usleep_range(10000, 10000);
	ret = bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	if (ret == -1) {
		chg_err("bq8z610 sealed, read ret error");
		return -1;
	}
	chg_info("bq8z610_sealed CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x, CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x\n",
	CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2], CNTL1_VAL[3]);
	value = (CNTL1_VAL[3] & BQ28Z610_SEAL_BIT);
	if (value == BQ28Z610_SEAL_VALUE) {
		chg_info("bq8z610 sealed, value = %x return 1\n", value);
		return 1;
	} else {
		chg_info("bq8z610 sealed, value = %x return 0\n", value);
		return 0;
	}
}

static int bq8z610_seal(struct chip_bq27541 *chip)
{
	int i = 0;
	int ret = 0;

	if (bq8z610_sealed(chip)) {
		chg_info("bq8z610 sealed, return\n");
		return 1;
	}
	bq27541_i2c_txsubcmd(chip, 0, BQ28Z610_SEAL_SUBCMD);
	usleep_range(100000, 100000);
	for (i = 0; i < BQ28Z610_SEAL_POLLING_RETRY_LIMIT; i++) {
		ret = bq8z610_sealed(chip);
		if (1 == ret) {
			chg_info("bq8z610 sealed,used %d x100ms\n", i);
			return 1;
		} else if (-1 == ret) {
			chg_err("bq8z610 seal failed by ret error\n");
			return 0;
		}
		usleep_range(10000, 10000);
	}
	return 0;
}

static int bq8z610_unseal(struct chip_bq27541 *chip)
{
	int i = 0;
	int ret = 0;

	if (!bq8z610_sealed(chip))
		goto out;

	bq27541_i2c_txsubcmd(chip, 0, BQ28Z610_UNSEAL_SUBCMD1);
	usleep_range(10000, 10000);
	bq27541_i2c_txsubcmd(chip, 0, BQ28Z610_UNSEAL_SUBCMD2);
	usleep_range(100000, 100000);
	while (i < BQ28Z610_SEAL_POLLING_RETRY_LIMIT) {
		i++;
		ret = bq8z610_sealed(chip);
		if (0 == ret) {
			chg_info("bq8z610 unsealed,used %d x100ms\n", i);
			break;
		} else if (-1 == ret) {
			chg_err("bq8z610 unseal failed by ret error\n");
			return 0;
		}
		usleep_range(10000, 10000);
	}

out:
	chg_debug("bq8z610 : i=%d\n", i);
	if (i == BQ28Z610_SEAL_POLLING_RETRY_LIMIT) {
		chg_err("bq8z610 unseal failed\n");
		return 0;
	} else {
		return 1;
	}
}

static int bq28z610_set_sleep_mode_withoutunseal(struct chip_bq27541 *chip, bool val)
{
	int rc = -1;
	u8 disable_data[BQ28Z610_REG_I2C_SIZE] = { 0xfd, 0x46, 0x01 };
	u8 enable_data[BQ28Z610_REG_I2C_SIZE] = { 0xfd, 0x46, 0x11 };
	u8 read_data[BQ28Z610_REG_I2C_SIZE] = { 0, 0, 0 };
	bool read_sleep_mode = 0;

	chg_info("enter, val=%d\n", val);
	if (chip == NULL)
		return rc;
	if (chip->batt_zy0603)
		return rc;
	if (!chip->batt_bq28z610)
		return rc;

	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_CNTL1_DA_CONFIG);
	usleep_range(10000, 10000);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, read_data);
	read_sleep_mode = (read_data[2] & BIT(4)) >> 4 ? true : false;
	chg_info("read_data=[%x],[%x],[%x] read_sleep_mode = %d\n",
		 read_data[0], read_data[1], read_data[2], read_sleep_mode);
	if (read_sleep_mode == val)
		return 0;

	if (val) {
		chg_info("enable sleep_mode, val=%d\n", val);
		rc = bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, &enable_data[0]);
		msleep(10);
		bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x05ab);
		msleep(10);
	} else {
		chg_info("disable sleep_mode, val=%d\n", val);
		rc = bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, &disable_data[0]);
		msleep(10);
		bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x05bb);
		msleep(10);
	}
	return rc;
}

static int bq28z610_set_sleep_mode(struct chip_bq27541 *chip, bool val)
{
	int rc = -1;
	int ret = 0;

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
		ret = bq8z610_sealed(chip);
		if ((ret == 0) || (ret == -1)) {
			usleep_range(1000, 1000);
			bq8z610_seal(chip);
		}
	}

	return rc;
}

static int zy0603_write_data_cmd(struct chip_bq27541 *chip, bool enable);
static int bq27411_modify_soc_smooth_parameter(struct chip_bq27541 *chip, bool is_powerup)
{
	int rc = 0;
	bool check_result = false;
	bool tried_again = false;

	if (chip->batt_zy0603 && is_powerup) {
		chg_info("batt_zy0603 begin\n");
		zy0603_write_data_cmd(chip, false);
		zy0603_get_fw_version(chip, &chip->fg_soft_version);
		return GAUGE_OK;
	} else {
		if (chip->batt_bq28z610 && is_powerup &&
			!chip->i2c_err && oplus_is_rf_ftm_mode() == false) {
			chg_info("batt_bq28z610 powerup\n");
			/*power up disable sleep mode*/
			bq28z610_set_sleep_mode(chip, false);
			return GAUGE_OK;
		}
	}

	if (chip->device_type == DEVICE_ZY0602) {
		chg_info("DEVICE_ZY0602 begin\n");
		chg_info("device_type = %d  enable_sleep_mode=%d\n",
			 chip->device_type, chip->enable_sleep_mode);
		if (chip->enable_sleep_mode)
			bq27411_write_soc_smooth_parameter_for_zy(chip, false);
		else
			bq27411_write_soc_smooth_parameter_for_zy(chip, is_powerup);
		return GAUGE_OK;
	}

	if (!chip->modify_soc_smooth || chip->device_type == DEVICE_BQ27541)
		return GAUGE_ERROR;
	chg_info("begin\n");
	if (sealed(chip)) {
		if (!unseal(chip, BQ27411_UNSEAL_KEY))
			return GAUGE_ERROR;
		else
			msleep(50);
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
	chg_info("end\n");
	return GAUGE_OK;
}

static int bq28z610_write_flash_busy_wait_i2c_err(struct chip_bq27541 *chip)
{
	/* int rc = 0; */
	u8 I2C_VAL[BQ28Z610_REG_I2C_SIZE] = { 0, 0, 0 };
	u8 I2C_write1[BQ28Z610_REG_I2C_SIZE] = { 0x03, 0x46, 0xA0 };

	bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE,
				&I2C_write1[0]);
	msleep(100);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x0516);
	msleep(100);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1,
			     0x4603); /* physical address is 0x4603 */
	msleep(100);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE,
			       I2C_VAL);
	pr_err("%s I2C Configuration I2C_VAL[0] = %x,I2C_VAL[1] = %x,I2C_VAL[2] = %x\n",
	       __func__, I2C_VAL[0], I2C_VAL[1], I2C_VAL[2]);
	if (((I2C_VAL[2] << 16) | (I2C_VAL[1] << 8) | I2C_VAL[0]) != 0xA04603) {
		pr_err("%s To change I2C Configuration 0x20 -> 0xA0. ERR.\n",
		       __func__);
		return -1;
	} else {
		pr_err("%s To change I2C Configuration 0x20 -> 0xA0. OK\n",
		       __func__);
	}
	return 0;
}

int bq28z610_write_soc_smooth_parameter(struct chip_bq27541 *chip)
{
	/* int rc = 0; */
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = { 0, 0, 0, 0 };
	u8 CNTL1_write1[BQ28Z610_REG_CNTL1_SIZE] = { 0xF4, 0x46, 0xdC, 0x00 };
	/* u8 CNTL1_write2[BQ28Z610_REG_CNTL1_SIZE] = {0x08,0x47,0x78,0x00}; 120ma */
	u8 CNTL1_write2[BQ28Z610_REG_CNTL1_SIZE] = { 0x08, 0x47, 0x96,
						     0x00 }; /* 150ma */
	u8 CNTL1_write3[BQ28Z610_REG_CNTL1_SIZE] = { 0x0C, 0x47, 0x28, 0x00 };
	bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE,
				&CNTL1_write1[0]);
	msleep(100);
	/* bq8z610_cntl2_cmd(0x06E9); */
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x06E9);
	/* usleep_range(10000, 5000); */
	msleep(100);
	/* bq8z610_cntl1_cmd(0x46F4); */
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x46F4);
	/* usleep_range(5000, 5000); */
	msleep(100);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE,
			       CNTL1_VAL);
	pr_err("%s Charge Term Taper Current CNTL1_VAL[0] = %x, "
	       "CNTL1_VAL[1] = %x, CNTL1_VAL[2] = %x, CNTL1_VAL[3] = %x\n",
	       __func__, CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2],
	       CNTL1_VAL[3]);
	if ((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x46F4) ||
	    (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x00DC)) {
		pr_err("%s Charge Term Taper Current 150mA (=0x0096) -> 220mA (=0x00DC). ERR.\n",
		       __func__);
		return -1;
	} else {
		pr_err("%s Charge Term Taper Current  (=0x0096) -> 220mA (=0x00DC). OK\n",
		       __func__);
	}
	bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE,
				&CNTL1_write2[0]);
	msleep(100);
	/* bq8z610_cntl2_cmd(0x06E9); */
	/* bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x0638); 120ma */
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x061A); /* 150ma */
	/* usleep_range(5000, 5000); */
	msleep(100);
	/* bq8z610_cntl1_cmd(0x46F4); */
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x4708);
	/* usleep_range(5000, 5000); */
	msleep(100);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE,
			       CNTL1_VAL);
	pr_err("%s Dsg Current Threshold CNTL1_VAL[0] = %x, "
	       "CNTL1_VAL[1] = %x, CNTL1_VAL[2] = %x, CNTL1_VAL[3] = %x\n",
	       __func__, CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2],
	       CNTL1_VAL[3]);
	/* if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x4708) || (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0078)) 120ma */
	if ((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x4708) ||
	    (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0096)) {
		pr_err("%s Dsg Current Threshold 40mA (0x0028) -> 150mA (0x0078) ERR.\n",
		       __func__);
		return -1;
	} else {
		pr_err("%s Dsg Current Threshold 40mA (0x0028) -> 150mA (0x0078) OK\n",
		       __func__);
	}
	bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE,
				&CNTL1_write3[0]);
	msleep(100);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x0684);
	msleep(100);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x470C);
	msleep(100);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE,
			       CNTL1_VAL);
	pr_err("%s Quit Current CNTL1_VAL[0] = %x, "
	       "CNTL1_VAL[1] = %x, CNTL1_VAL[2] = %x, CNTL1_VAL[3] = %x\n",
	       __func__, CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2],
	       CNTL1_VAL[3]);
	if ((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x470C) ||
	    (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0028)) {
		pr_err("%s Quit Current 20mA (0x0014) -> 40mA (0x0028). ERR.\n",
		       __func__);
		return -1;
	} else {
		pr_err("%s Quit Current 20mA (0x0014) -> 40mA (0x0028). OK\n",
		       __func__);
	}
	return 0;
}

static int bq28z610_write_iterm_Taper_parameter(struct chip_bq27541 *chip)
{
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = { 0, 0, 0, 0 };
	u8 CNTL1_write1[BQ28Z610_REG_CNTL1_SIZE] = { 0xF4, 0x46, 0x96, 0x00 };
	bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE,
				&CNTL1_write1[0]);
	msleep(100);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL2, 0x062F);
	msleep(100);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x46F4);
	msleep(100);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE,
			       CNTL1_VAL);
	pr_err("%s Charge Term Taper Current CNTL1_VAL[0] = %x, "
	       "CNTL1_VAL[1] = %x, CNTL1_VAL[2] = %x, CNTL1_VAL[3] = %x\n",
	       __func__, CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2],
	       CNTL1_VAL[3]);
	if ((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x46F4) ||
	    (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0096)) {
		pr_err("%s Charge Term Taper Current 220mA (=0x00DC) -> 150mA (=0x0096). ERR.\n",
		       __func__);
		return -1;
	} else {
		pr_err("%s Charge Term Taper Current 220mA (=0x00DC) -> 150mA (=0x0096). OK\n",
		       __func__);
	}
	return 0;
}

static void bq28z610_modify_soc_smooth_parameter(struct chip_bq27541 *chip)
{
	int rc = 0;
	bool tried_again = false;
	int ret = 0;

	chg_info("begin\n");
	if (bq8z610_sealed(chip)) {
		if (!bq8z610_unseal(chip))
			return;
		else
			msleep(50);
	}

write_parameter:
	if (!chip->remove_iterm_taper)
		rc = bq28z610_write_iterm_Taper_parameter(chip);
	rc = bq28z610_write_flash_busy_wait_i2c_err(chip);

	/* shipmode enable sleep mode */
	rc = bq28z610_set_sleep_mode_withoutunseal(chip, true);

	if (rc && !tried_again) {
		tried_again = true;
		goto write_parameter;
	}
	usleep_range(1000, 1000);
	ret = bq8z610_sealed(chip);
	if ((ret == 0) || (ret == -1)) {
		usleep_range(1000, 1000);
		bq8z610_seal(chip);
	}
	chg_info("end\n");
}

static int
bq28z610_batt_full_zero_parameter_write_cmd(struct chip_bq27541 *chip)
{
	int rc = 0;
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
	bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
	pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0],
	       CNTL1_VAL_1[1]);
	rc = bq27541_read_i2c_block(chip, 0x3E, 6, read_buf); /*R*/
	pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
	       __func__, read_buf[0], read_buf[1], read_buf[2], read_buf[3],
	       read_buf[4], read_buf[5]);
	if (!(read_buf[2] == 0x46 && read_buf[3] == 0x00 &&
	      read_buf[4] == 0x3C && read_buf[5] == 0x00)) {
		/*################################################0x9A45################################################*/
		/*write 10s*/
		pr_err("%s write 0x0A45\n", __func__);
	recfg_pararm1:
		CNTL1_VAL_1[0] = 0x9A;
		CNTL1_VAL_1[1] = 0x45;
		bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(chip, 0x3E, 7, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x][0x%02x][0x%02x][0x%02x]\n",
		       __func__, read_buf[0], read_buf[1], read_buf[2],
		       read_buf[3], read_buf[4], read_buf[5], read_buf[6]);

		CNTL1_VAL_2[0] = 0x9A;
		CNTL1_VAL_2[1] = 0x45;
		CNTL1_VAL_2[2] = 0x20;
		CNTL1_VAL_2[3] = 0x1C;
		CNTL1_VAL_2[4] = 0x20;
		CNTL1_VAL_2[5] = 0x1C;
		CNTL1_VAL_2[6] = 0x1E;
		bq27541_write_i2c_block(chip, 0x3E, 7, CNTL1_VAL_2); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x] [0x%02x][0x%02x][0x%02x][0x%02x][0x%02x]\n",
		       __func__, CNTL1_VAL_2[0], CNTL1_VAL_2[1], CNTL1_VAL_2[2],
		       CNTL1_VAL_2[3], CNTL1_VAL_2[4], CNTL1_VAL_2[5],
		       CNTL1_VAL_2[6]);

		CNTL1_VAL_1[0] = 0xFF - CNTL1_VAL_2[0] - CNTL1_VAL_2[1] -
				 CNTL1_VAL_2[2] - CNTL1_VAL_2[3] -
				 CNTL1_VAL_2[4] - CNTL1_VAL_2[5] -
				 CNTL1_VAL_2[6];
		CNTL1_VAL_1[1] = 0x09;
		bq27541_write_i2c_block(chip, 0x60, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x60 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		usleep_range(260000, 260000); /*15ms*/

		CNTL1_VAL_1[0] = 0x9A; /*READ 10S*/
		CNTL1_VAL_1[1] = 0x45;
		bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(chip, 0x3E, 7, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x][0x%02x][0x%02x][0x%02x]\n",
		       __func__, read_buf[0], read_buf[1], read_buf[2],
		       read_buf[3], read_buf[4], read_buf[5], read_buf[6]);
		/*check param cfg*/
		if (!(read_buf[2] == CNTL1_VAL_2[2] &&
		      read_buf[3] == CNTL1_VAL_2[3] &&
		      read_buf[4] == CNTL1_VAL_2[4] &&
		      read_buf[5] == CNTL1_VAL_2[5] &&
		      read_buf[6] == CNTL1_VAL_2[6])) {
			retry_cnt++;
			if (retry_cnt >= 3) {
				goto param_cf_err;
			}
			pr_err("gauge err recfg_pararm1");
			goto recfg_pararm1;
		}
		retry_cnt = 0;
		usleep_range(5000, 5000);
		/*##############################################0x5846#################################################*/
		/*write 5846*/
		pr_err("%s write 0x5846\n", __func__);
	recfg_pararm2:
		CNTL1_VAL_1[0] = 0x58;
		CNTL1_VAL_1[1] = 0x46;
		bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(chip, 0x3E, 6, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
		       __func__, read_buf[0], read_buf[1], read_buf[2],
		       read_buf[3], read_buf[4], read_buf[5]);

		CNTL1_VAL_2[0] = 0x58;
		CNTL1_VAL_2[1] = 0x46;
		CNTL1_VAL_2[2] = 0x33;
		CNTL1_VAL_2[3] = 0x00;
		CNTL1_VAL_2[4] = 0x27;
		CNTL1_VAL_2[5] = 0x00;
		bq27541_write_i2c_block(chip, 0x3E, 6, CNTL1_VAL_2); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
		       __func__, CNTL1_VAL_2[0], CNTL1_VAL_2[1], CNTL1_VAL_2[2],
		       CNTL1_VAL_2[3], CNTL1_VAL_2[4], CNTL1_VAL_2[5]);

		CNTL1_VAL_1[0] = 0xFF - CNTL1_VAL_2[0] - CNTL1_VAL_2[1] -
				 CNTL1_VAL_2[2] - CNTL1_VAL_2[3] -
				 CNTL1_VAL_2[4] - CNTL1_VAL_2[5];
		/*CNTL1_VAL_1[0] = 0xE2;*/
		CNTL1_VAL_1[1] = 0x08;
		bq27541_write_i2c_block(chip, 0x60, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x60 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		usleep_range(260000, 260000); /*15ms*/

		CNTL1_VAL_1[0] = 0x58; /*READ 7200S 1C20*/
		CNTL1_VAL_1[1] = 0x46;
		bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(chip, 0x3E, 6, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x][0x%02x][0x%02x][0x%02x][0x%02x]\n",
		       __func__, read_buf[0], read_buf[1], read_buf[2],
		       read_buf[3], read_buf[4], read_buf[5]);
		/*check param cfg*/
		if (!(read_buf[2] == CNTL1_VAL_2[2] &&
		      read_buf[3] == CNTL1_VAL_2[3] &&
		      read_buf[4] == CNTL1_VAL_2[4] &&
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
		bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(chip, 0x3E, 6, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
		       __func__, read_buf[0], read_buf[1], read_buf[2],
		       read_buf[3], read_buf[4], read_buf[5]);

		CNTL1_VAL_2[0] = 0x0A;
		CNTL1_VAL_2[1] = 0x47;
		CNTL1_VAL_2[2] = 0x46;
		CNTL1_VAL_2[3] = 0x00;
		CNTL1_VAL_2[4] = 0x3C;
		CNTL1_VAL_2[5] = 0x00;
		bq27541_write_i2c_block(chip, 0x3E, 6, CNTL1_VAL_2); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
		       __func__, CNTL1_VAL_2[0], CNTL1_VAL_2[1], CNTL1_VAL_2[2],
		       CNTL1_VAL_2[3], CNTL1_VAL_2[4], CNTL1_VAL_2[5]);

		CNTL1_VAL_1[0] = 0xFF - CNTL1_VAL_2[0] - CNTL1_VAL_2[1] -
				 CNTL1_VAL_2[2] - CNTL1_VAL_2[3] -
				 CNTL1_VAL_2[4] - CNTL1_VAL_2[5];
		/*CNTL1_VAL_1[0] = 0xE2;*/
		CNTL1_VAL_1[1] = 0x08;
		bq27541_write_i2c_block(chip, 0x60, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x60 -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		usleep_range(260000, 260000); /*15ms*/

		CNTL1_VAL_1[0] = 0x0A; /*READ 7200S 1C20*/
		CNTL1_VAL_1[1] = 0x47;
		bq27541_write_i2c_block(chip, 0x3E, 2, CNTL1_VAL_1); /*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__,
		       CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(chip, 0x3E, 6, read_buf); /*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x][0x%02x][0x%02x][0x%02x][0x%02x]\n",
		       __func__, read_buf[0], read_buf[1], read_buf[2],
		       read_buf[3], read_buf[4], read_buf[5]);
		if (!(read_buf[2] == CNTL1_VAL_2[2] &&
		      read_buf[3] == CNTL1_VAL_2[3] &&
		      read_buf[4] == CNTL1_VAL_2[4] &&
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

	if (rc < 0)
		chg_err("read i2c err\n");
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
	int ret = 0;

	if (chip->device_type != DEVICE_BQ27541) {
		pr_err("%s NOT DEVICE_BQ27541\n", __func__);
		return GAUGE_OK;
	}

	if (!chip->batt_bq28z610)
		return GAUGE_OK;

	pr_err("%s begin\n", __func__);
	if (bq8z610_sealed(chip)) {
		if (!bq8z610_unseal(chip))
			return GAUGE_OK;
		else
			msleep(50);
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

	bq27541_i2c_txsubcmd(chip, 0, BQ28Z610_SEAL_SUBCMD); /*seal*/
	msleep(1000);
	ret = bq8z610_sealed(chip);
	if ((0 == ret) || (-1 == ret)) {
		usleep_range(1000, 1000);
		bq8z610_seal(chip);
	}
	pr_err("%s end\n", __func__);
	return GAUGE_OK;
}

int oplus_vooc_get_fastchg_ing(void)
{
	int fastchg_status = 0;
	struct oplus_mms *vooc_topic;
	union mms_msg_data data = { 0 };
	int rc;

	vooc_topic = oplus_mms_get_by_name("vooc");
	if (!vooc_topic)
		return 0;

	rc = oplus_mms_get_item_data(vooc_topic, VOOC_ITEM_VOOC_CHARGING, &data, true);
	if (!rc)
		fastchg_status = data.intval;

	pr_err("%s, get fastchg status = %d\n", __func__, fastchg_status);
	return fastchg_status;
}

int oplus_vooc_get_fast_chg_type(void)
{
	int svooc_type = 0;
	struct oplus_mms *vooc_topic;
	union mms_msg_data data = { 0 };
	int rc;

	vooc_topic = oplus_mms_get_by_name("vooc");
	if (!vooc_topic)
		return 0;

	rc = oplus_mms_get_item_data(vooc_topic, VOOC_ITEM_GET_BCC_SVOOC_TYPE, &data, true);
	if (!rc)
		svooc_type = data.intval;

	pr_err("%s, get svooc type = %d\n", __func__, svooc_type);
	return svooc_type;
}

static int bq28z610_get_battery_bcc_parameters(struct oplus_chg_ic_dev *ic_dev, char *buf)
{
	return 0;
}

static int bq28z610_get_fastchg_battery_bcc_parameters(struct oplus_chg_ic_dev *ic_dev, char *buf)
{
	return 0;
}

static int bq28z610_get_prev_battery_bcc_parameters(struct oplus_chg_ic_dev *ic_dev, char *buf)
{
	return 0;
}

static int bq28z610_set_bcc_debug_parameters(struct oplus_chg_ic_dev *ic_dev, const char *buf)
{
	return 0;
}

#define REG_DUMP_SIZE 1024
static int dump_reg[] = { 0x08, 0x12, 0x2c };
static int gauge_reg_dump(struct chip_bq27541 *chip)
{
	int val = 0;
	int i;
	int l = 0;
	char *pos;
	int sum = 0, ret;
	u8 iv[32] = { 0 };
	char buf[REG_DUMP_SIZE] = { 0 };
	int len = REG_DUMP_SIZE;
	bool read_done = false;

	if (!chip) {
		return 0;
	}
	if (chip->batt_zy0603) {
			return 0;
	}
	if (chip->batt_zy0603) {
			return 0;
	}
	if (chip->device_type == DEVICE_BQ27541 ||
	    chip->device_type == DEVICE_ZY0602) {
		pr_err("%s: gauge ic error\n", __func__);
		/*return -1;*/
	}
	if (atomic_read(&chip->suspended) == 1) {
		pr_err("%s: gauge suspend!\n", __func__);
		return -1;
	}

	pos = buf;
	if (atomic_read(&chip->locked) == 0) {
		l = sprintf(pos, "%d ", bq27541_get_battery_temperature(chip));
		pos += l;
		sum += l;
		l = sprintf(pos, "/ %d ", bq27541_get_average_current(chip));
		pos += l;
		sum += l;

		for (i = 0; !read_done; i++) {
			ret = bq27541_read_i2c(chip, dump_reg[i], &val);
			if (ret) {
				chg_err("error reading regdump, ret:%d\n", ret);
				return -1;
			}
			l = sprintf(pos, "/ %d ", val);
			pos += l;
			sum += l;

			read_done = !(i < sizeof(dump_reg) / sizeof(int));
			read_done &= !(sum < len - 16);
		}

		bq27541_i2c_txsubcmd(chip, BQ27411_DATA_CLASS_ACCESS, 0x71);
		usleep_range(10000, 10000);
		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 6, iv);
		for (i = 2; (i < 6) && (sum < len - 16); i++) {
			if ((i % 2) == 0) {
				l = sprintf(pos, "/ %d ",
					    (iv[i + 1] << 8) + iv[i]);
				pos += l;
				sum += l;
			}
		}
		bq27541_i2c_txsubcmd(chip, BQ27411_DATA_CLASS_ACCESS, 0x73);
		usleep_range(10000, 10000);
		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 16, iv);
		for (i = 2; (i < 16) && (sum < len - 16); i++) {
			if (i != 3 && i != 4 && i != 7 && i != 8 && i != 11 &&
			    i != 12) {
				if ((i % 2) == 0) {
					l = sprintf(pos, "/ %d ",
						    (iv[i + 1] << 8) + iv[i]);
					pos += l;
					sum += l;
				}
			}
		}
		bq27541_i2c_txsubcmd(chip, BQ27411_DATA_CLASS_ACCESS, 0x74);
		usleep_range(10000, 10000);
		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 26, iv);
		for (i = 12; (i < 26) && (sum < len - 16); i++) {
			if (i != 17 && i != 18) {
				if ((i % 2) == 0) {
					l = sprintf(pos, "/ %d ",
						    (iv[i + 1] << 8) + iv[i]);
					pos += l;
					sum += l;
				}
			}
		}
		bq27541_i2c_txsubcmd(chip, BQ27411_DATA_CLASS_ACCESS, 0x75);
		usleep_range(10000, 10000);
		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 12, iv);
		for (i = 2; (i < 12) && (sum < len - 16); i++) {
			if (i != 3 && i != 5 && i != 6 && i != 8) {
				if ((i % 2) == 0) {
					l = sprintf(pos, "/ %d ",
						    (iv[i + 1] << 8) + iv[i]);
					pos += l;
					sum += l;
				}
			}
		}
	}
	chg_err("gauge regs: %s\n", buf);
	return 0;
}

static int bq8z610_check_gauge_enable(struct chip_bq27541 *chip)
{
	/*    return control_cmd_read(di, CONTROL_STATUS) & (1 << 13);*/
	int value = 0;
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = { 0, 0, 0, 0 };
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_GAUGE_EN);
	/* usleep_range(10000, 10000); */
	msleep(1000);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE,
			       CNTL1_VAL);
	pr_err("%s  CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x, "
	       "CNTL1_VAL[2] = %x, CNTL1_VAL[3] = %x\n",
	       __func__, CNTL1_VAL[0], CNTL1_VAL[1], CNTL1_VAL[2],
	       CNTL1_VAL[3]);
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
	/* bq8z610_cntl1_cmd(0x46F4); */
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x0021);
	/* usleep_range(5000, 5000); */
	msleep(1000);
	/* bq8z610_cntl1_cmd(0x00DC); */
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x0021);
	/* usleep_range(5000, 5000); */
	msleep(2000);
	if (bq8z610_check_gauge_enable(chip) == false) {
		/* bq8z610_cntl1_cmd(0x00DC); */
		bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, 0x0021);
		/* usleep_range(5000, 5000); */
		msleep(300);
	}
	return 0;
}

static void bq28z610_modify_dod0_parameter(struct chip_bq27541 *chip)
{
	int rc = 0;
	int ret = 0;

	chg_info("begin\n");
	if (bq8z610_sealed(chip)) {
		if (!bq8z610_unseal(chip))
			return;
		else
			msleep(50);
	}
	rc = bq28z610_write_dod0_parameter(chip);
	usleep_range(1000, 1000);
	ret = bq8z610_sealed(chip);
	if ((0 == ret) || (-1 == ret)) {
		usleep_range(1000, 1000);
		bq8z610_seal(chip);
	}
	
	if (rc < 0)
		chg_err("read i2c err\n");

	chg_info("end\n");
}

static int bq28z610_get_2cell_voltage(struct chip_bq27541 *chip)
{
	u8 cell_vol[BQ28Z610_MAC_CELL_VOLTAGE_SIZE] = {0, 0, 0, 0};
	u8 cell_vol_zy0603[BQ28Z610_MAC_CELL_VOLTAGE_SIZE + 2] = {0, 0, 0, 0, 0, 0};

	if (!chip) {
		return 0;
	}
	if (!chip->batt_zy0603) {
	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_MAC_CELL_VOLTAGE_EN_ADDR,
		BQ28Z610_MAC_CELL_VOLTAGE_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(chip, BQ28Z610_MAC_CELL_VOLTAGE_ADDR,
		BQ28Z610_MAC_CELL_VOLTAGE_SIZE, cell_vol);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	chip->batt_cell_1_vol = (cell_vol[1] << 8) | cell_vol[0];
	chip->batt_cell_2_vol = (cell_vol[3] << 8) | cell_vol[2];
	} else {
		mutex_lock(&chip->bq28z610_alt_manufacturer_access);
		bq27541_i2c_txsubcmd(chip, BQ28Z610_MAC_CELL_VOLTAGE_EN_ADDR,
			BQ28Z610_MAC_CELL_VOLTAGE_CMD);
		usleep_range(U_DELAY_1_MS, U_DELAY_1_MS);
		bq27541_read_i2c_block(chip, BQ28Z610_MAC_CELL_VOLTAGE_EN_ADDR,
			BQ28Z610_MAC_CELL_VOLTAGE_SIZE + 2, cell_vol_zy0603);
		mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
		/*pr_err("%s cell_vol_zy0603[0x%0x][0x%0x][0x%0x][0x%0x][0x%0x][0x%0x]\n", __func__, cell_vol_zy0603[0],
			cell_vol_zy0603[1], cell_vol_zy0603[2], cell_vol_zy0603[3], cell_vol_zy0603[4], cell_vol_zy0603[5]);*/
		if (cell_vol_zy0603[0] == 0x71 && cell_vol_zy0603[1] == 0x00) {
			chip->batt_cell_1_vol = (cell_vol_zy0603[3] << 8) | cell_vol_zy0603[2];
			chip->batt_cell_2_vol = (cell_vol_zy0603[5] << 8) | cell_vol_zy0603[4];
		}
	}

	if (chip->batt_cell_1_vol < chip->batt_cell_2_vol) {
		chip->batt_cell_max_vol = chip->batt_cell_2_vol;
		chip->batt_cell_min_vol = chip->batt_cell_1_vol;
	} else {
		chip->batt_cell_max_vol = chip->batt_cell_1_vol;
		chip->batt_cell_min_vol = chip->batt_cell_2_vol;
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

	bq27541_i2c_txsubcmd(BQ28Z610_MAC_CELL_BALANCE_TIME_EN_ADDR, BQ28Z610_MAC_CELL_BALANCE_TIME_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(BQ28Z610_MAC_CELL_BALANCE_TIME_ADDR, BQ28Z610_MAC_CELL_BALANCE_TIME_SIZE, balance_time);
	pr_err("Cell_balance_time_remaining1 = %dS, Cell_balance_time_remaining2 = %dS\n",
	(balance_time[1] << 8) | balance_time[0], (balance_time[3] << 8) | balance_time[2]);

	return 0;
}*/

static bool bq8z610_deep_init(struct chip_bq27541 *chip)
{
	if (!bq8z610_sealed(chip)) {
		chg_err("bq8z610 already unsealed\n");
		return true;
	}
	bq28z610_i2c_deep_int(chip);

	usleep_range(100000, 100000);
	if (!bq8z610_sealed(chip)) {
		return true;
	} else {
		chg_err("bq8z610 unseal failed\n");
		return false;
	}
}

static void bq8z610_deep_deinit(struct chip_bq27541 *chip)
{
	int i = 0;

	if (bq8z610_sealed(chip) == 0) {
		usleep_range(1000, 1000);
		bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_SEAL_SUBCMD);
		usleep_range(100000, 100000);
		for (i = 0; i < BQ28Z610_SEAL_POLLING_RETRY_LIMIT; i++) {
			if (bq8z610_sealed(chip)) {
				chg_info("bq8z610 sealed,used %d x100ms\n", i);
				return;
			}
			usleep_range(10000, 10000);
		}
	}
}

static int bq28z610_set_term_volt(struct chip_bq27541 *chip, int volt_mv)
{
	int rc = -1, value = 0, main_value = 0, sub_value = 0;
	u8 write_main[BQ28Z610_TERM_VOLT_SIZE] = { 0xBE, 0x45, (volt_mv * 2) & BQ28Z610_DEEP_DISCHG_CHECK,
		(volt_mv * 2) >> BQ28Z610_DEEP_DISCHG_SHIFT_MASK };
	u8 write_sub[BQ28Z610_TERM_VOLT_SIZE] = { 0xC3, 0x45, volt_mv, volt_mv >> BQ28Z610_DEEP_DISCHG_SHIFT_MASK };
	u8 check_data[2] = { 0, BQ28Z610_TERM_VOLT_CHECK_SIZE };
	u8 deep_read[BQ28Z610_DEEP_DISCHG_SIZE] = { 0, 0, 0, 0, 0 };
	u8 deep_write[BQ28Z610_DEEP_DISCHG_SIZE] = { 0x82, 0x40, 0x0b, 0, 0, 0,
		(volt_mv * 2) & BQ28Z610_DEEP_DISCHG_CHECK, (volt_mv * 2) >> BQ28Z610_DEEP_DISCHG_SHIFT_MASK };
	u8 deep_check[2] = { 0, BQ28Z610_DEEP_DISCHG_CEHECK_SIZE };

	bool value_check = false;
	u8 read_main[BQ28Z610_TERM_VOLT_SIZE] = { 0xBE, 0x45, (volt_mv * 2) & BQ28Z610_DEEP_DISCHG_CHECK,
		(volt_mv * 2) >> BQ28Z610_DEEP_DISCHG_SHIFT_MASK };
	u8 read_sub[BQ28Z610_TERM_VOLT_SIZE] = { 0xC3, 0x45, volt_mv, volt_mv >> BQ28Z610_DEEP_DISCHG_SHIFT_MASK };


	if (!chip || atomic_read(&chip->suspended) == 1 || (!chip->batt_bq28z610 && !chip->batt_bq27z561))
		return rc;

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_NAME_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, deep_read);

	value = (deep_read[6] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + deep_read[5];

	chg_info("[%d, %d][0x%x, 0x%x], [0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x]\n", value, volt_mv,
			deep_read[0], deep_read[1], deep_read[2], deep_read[3], deep_read[4], deep_read[5], deep_read[6], deep_read[7],
			deep_read[8], deep_read[9], deep_read[10]);

	if ((value == volt_mv * 2) && (deep_read[7] == (BQ28Z610_DEEP_DISCHG_CHECK - (deep_read[5] + deep_read[6]) & BQ28Z610_DEEP_DISCHG_CHECK))) {
		mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
		return 0;
	}

	deep_write[3] = deep_read[2];
	deep_write[4] = deep_read[3];
	deep_write[5] = deep_read[4];
	deep_write[8] = BQ28Z610_DEEP_DISCHG_CHECK - (deep_write[6] + deep_write[7]) & BQ28Z610_DEEP_DISCHG_CHECK;
	deep_write[9] = deep_read[8];
	deep_write[10] = deep_read[9];
	deep_write[11] = deep_read[10];

	if (!bq8z610_deep_init(chip)) {
		mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
		return rc;
	}

write_parameter:
	rc = bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, deep_write);
	usleep_range(1000, 1000);

	deep_check[0] = BQ28Z610_DEEP_DISCHG_CHECK - (deep_write[0] + deep_write[1] + deep_write[2] + deep_write[3] + deep_write[4]
	+ deep_write[5] + deep_write[6] + deep_write[7] + deep_write[8] + deep_write[9] + deep_write[10] + deep_write[11]) & BQ28Z610_DEEP_DISCHG_CHECK;
	rc = bq27541_write_i2c_block(chip, BQ28Z610_TERM_VOLT_CHECK_ADDR, 2, deep_check);
	usleep_range(1000, 1000);

	rc = bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_TERM_VOLT_SIZE, write_main);
	check_data[0] = BQ28Z610_DEEP_DISCHG_CHECK - (write_main[0] + write_main[1] + write_main[2] + write_main[3]) & BQ28Z610_DEEP_DISCHG_CHECK;
	rc = bq27541_write_i2c_block(chip, BQ28Z610_TERM_VOLT_CHECK_ADDR, 2, check_data);

	rc = bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_TERM_VOLT_SIZE, write_sub);
	check_data[0] = BQ28Z610_DEEP_DISCHG_CHECK - (write_sub[0] + write_sub[1] + write_sub[2] + write_sub[3]) & BQ28Z610_DEEP_DISCHG_CHECK;
	rc = bq27541_write_i2c_block(chip, BQ28Z610_TERM_VOLT_CHECK_ADDR, 2, check_data);

	if (value_check == false) {
		bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_NAME_CMD);
		usleep_range(1000, 1000);
		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, deep_read);
		value = (deep_read[6] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + deep_read[5];

		bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_TERM_VOLT_CMD);
		usleep_range(1000, 1000);
		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_TERM_VOLT_SIZE, read_main);
		main_value = (read_main[3] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_main[2];

		bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_TERM_VOLT_S_CMD);
		usleep_range(1000, 1000);
		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_TERM_VOLT_SIZE, read_sub);
		sub_value = (read_sub[3] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_sub[2];

		if (rc < 0 || value != volt_mv * 2 ||
			((deep_read[7] != (BQ28Z610_DEEP_DISCHG_CHECK - (deep_read[5] + deep_read[6]) & BQ28Z610_DEEP_DISCHG_CHECK))) ||
			(((read_main[3] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_main[2]) != volt_mv * 2 ||
			(((read_main[1] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_main[0]) != BQ28Z610_TERM_VOLT_CMD))||
			(((read_sub[3] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_sub[2]) != volt_mv ||
			(((read_sub[1] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_sub[0]) != BQ28Z610_TERM_VOLT_S_CMD))) {
			value_check = true;
			chg_info("deep_read[%d, %d][0x%x, 0x%x, 0x%x]main:0x%x[0x%x, 0x%x, 0x%x, 0x%x]sub:0x%x[0x%x, 0x%x, 0x%x, 0x%x]\n",
				value, volt_mv, deep_read[5], deep_read[6], deep_read[7], main_value, read_main[0], read_main[1], read_main[2], read_main[3],
				sub_value, read_sub[0], read_sub[1], read_sub[2], read_sub[3]);
			goto write_parameter;
		}
	}

	bq8z610_deep_deinit(chip);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);

	return rc;
}

static int bq28z610_get_term_volt(struct chip_bq27541 *chip, int *volt)
{
	int rc = -EINVAL;
	int value = 0;
	u8 read_data[BQ28Z610_DEEP_DISCHG_SIZE] = { 0 };

	if (!chip || atomic_read(&chip->suspended) == 1 || (!chip->batt_bq28z610 && !chip->batt_bq27z561)) {
		*volt = chip->deep_term_volt_pre;
		return rc;
	}

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_NAME_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, read_data);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);

	value = (read_data[6] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_data[5];

	chg_info("[0x%x, 0x%x] [0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x] volt=%d\n",
		read_data[0], read_data[1], read_data[2], read_data[3], read_data[4], read_data[5],
		read_data[6], read_data[7], read_data[8], read_data[9], read_data[10], value / 2);
	if (read_data[7] == (BQ28Z610_DEEP_DISCHG_CHECK - (read_data[5] + read_data[6]) & BQ28Z610_DEEP_DISCHG_CHECK)) {
		*volt = value / 2;
		chip->deep_term_volt_pre = *volt;
		rc = 0;
	}

	return rc;
}

static int bq28z610_get_deep_dischg_num(struct chip_bq27541 *chip)
{
	int dischg_num = 0;
	u8 read_data[BQ28Z610_DEEP_DISCHG_SIZE] = { 0, 0, 0, 0, 0 };

	if (!chip || atomic_read(&chip->suspended) == 1 || (!chip->batt_bq28z610 && !chip->batt_bq27z561)) {
		return chip->deep_dischg_count_pre;
	}

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_NAME_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, read_data);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);

	dischg_num = (read_data[3] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_data[2];
	chg_info("[0x%x, 0x%x] [0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x]\n",
			read_data[0], read_data[1], read_data[2], read_data[3], read_data[4],
			read_data[5], read_data[6], read_data[7], read_data[8], read_data[9], read_data[10]);

	if (read_data[4] == (BQ28Z610_DEEP_DISCHG_CHECK - (read_data[2] + read_data[3]) & BQ28Z610_DEEP_DISCHG_CHECK)) {
		chip->deep_dischg_count_pre = dischg_num;
		return dischg_num;
	} else {
		return chip->deep_dischg_count_pre;
	}
}

static int bq28z610_set_deep_dischg_num(struct chip_bq27541 *chip, int dischg_num)
{
	int rc = -1;
	int value = 0;
	u8 write_data[BQ28Z610_DEEP_DISCHG_SIZE] = { 0x82, 0x40, 0x0b, dischg_num & BQ28Z610_DEEP_DISCHG_CHECK, dischg_num >> BQ28Z610_DEEP_DISCHG_SHIFT_MASK };
	u8 read_data[BQ28Z610_DEEP_DISCHG_SIZE] = { 0, 0, 0, 0, 0 };
	u8 check_data[2] = { 0, BQ28Z610_DEEP_DISCHG_CEHECK_SIZE };
	bool value_check = false;

	if (!chip || atomic_read(&chip->suspended) == 1 || (!chip->batt_bq28z610 && !chip->batt_bq27z561))
		return rc;

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_NAME_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, read_data);

	chg_info("[0x%x, 0x%x]  [0x%x, 0x%x, 0x%x] [0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x]\n",
		read_data[0], read_data[1], read_data[2], read_data[3], read_data[4],
		read_data[5], read_data[6], read_data[7], read_data[8], read_data[9], read_data[10]);
	value = (read_data[3] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_data[2];
	if ((value == dischg_num) && (read_data[4] == (BQ28Z610_DEEP_DISCHG_CHECK - (read_data[2] + read_data[3]) & BQ28Z610_DEEP_DISCHG_CHECK))) {
		mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
		return rc;
	}

	if (!bq8z610_deep_init(chip)) {
		mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
		return rc;
	}

	write_data[5] = BQ28Z610_DEEP_DISCHG_CHECK - (write_data[3] + write_data[4]) & BQ28Z610_DEEP_DISCHG_CHECK;
	write_data[6] = read_data[5];
	write_data[7] = read_data[6];
	write_data[8] = read_data[7];
	write_data[9] = read_data[8];
	write_data[10] = read_data[9];
	write_data[11] = read_data[10];

	chg_info("[0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x]\n", write_data[3], write_data[4], write_data[5],
		write_data[6], write_data[7], write_data[8], write_data[9], write_data[10], write_data[11]);

write_parameter:
	rc = bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, write_data);
	usleep_range(1000, 1000);
	check_data[0] = BQ28Z610_DEEP_DISCHG_CHECK - (write_data[0] + write_data[1] + write_data[2] + write_data[3]
		+ write_data[4] + write_data[5] + write_data[6] + write_data[7] + write_data[8] + write_data[9]
		+ write_data[10] + write_data[11]) & BQ28Z610_DEEP_DISCHG_CHECK;
	rc |= bq27541_write_i2c_block(chip, BQ28Z610_TERM_VOLT_CHECK_ADDR, 2, check_data);
	usleep_range(1000, 1000);


	if (value_check == false) {
		bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_NAME_CMD);
		usleep_range(1000, 1000);
		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, read_data);
		value = (read_data[3] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_data[2];
		if (rc < 0 || value != dischg_num || ((read_data[4] != (BQ28Z610_DEEP_DISCHG_CHECK
			- (read_data[2] + read_data[3]) & BQ28Z610_DEEP_DISCHG_CHECK)))) {
			value_check = true;
			goto write_parameter;
		}
	}

	bq8z610_deep_deinit(chip);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);

	return rc;
}

static int bq28z610_set_last_cc(struct chip_bq27541 *chip, int cc)
{
	int rc = -1;
	int value = 0;
	u8 write_data[BQ28Z610_DEEP_DISCHG_SIZE] = { 0x82, 0x40, 0x0b, 0, 0, 0, 0, 0, 0,
		cc & BQ28Z610_DEEP_DISCHG_CHECK, cc >> BQ28Z610_DEEP_DISCHG_SHIFT_MASK };
	u8 read_data[BQ28Z610_DEEP_DISCHG_SIZE] = { 0, 0, 0, 0, 0 };
	u8 check_data[2] = { 0, BQ28Z610_DEEP_DISCHG_CEHECK_SIZE };
	bool cc_check = false;

	if (!chip || atomic_read(&chip->suspended) == 1 || (!chip->batt_bq28z610 && !chip->batt_bq27z561))
		return rc;

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_NAME_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, read_data);

	chg_info("[0x%x, 0x%x]  [0x%x, 0x%x, 0x%x] [0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x]\n",
		read_data[0], read_data[1], read_data[2], read_data[3], read_data[4], read_data[5],
		read_data[6], read_data[7], read_data[8], read_data[9], read_data[10]);
	value = (read_data[9] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_data[8];
	if ((value == cc) && (read_data[10] == (BQ28Z610_DEEP_DISCHG_CHECK
		- (read_data[8] + read_data[9]) & BQ28Z610_DEEP_DISCHG_CHECK))) {
		mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
		return rc;
	}

	if (!bq8z610_deep_init(chip)) {
		mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
		return rc;
	}

	write_data[3] = read_data[2];
	write_data[4] = read_data[3];
	write_data[5] = read_data[4];
	write_data[6] = read_data[5];
	write_data[7] = read_data[6];
	write_data[8] = read_data[7];
	write_data[11] = BQ28Z610_DEEP_DISCHG_CHECK - (write_data[9] + write_data[10]) & BQ28Z610_DEEP_DISCHG_CHECK;

	chg_info("[0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x]\n", write_data[3], write_data[4], write_data[5],
		write_data[6], write_data[7], write_data[8], write_data[9], write_data[10], write_data[11]);

write_parameter:
	rc = bq27541_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, write_data);
	usleep_range(1000, 1000);
	check_data[0] = BQ28Z610_DEEP_DISCHG_CHECK - (write_data[0] + write_data[1] + write_data[2] + write_data[3]
		+ write_data[4] + write_data[5] + write_data[6] + write_data[7] + write_data[8] + write_data[9]
		+ write_data[10] + write_data[11]) & BQ28Z610_DEEP_DISCHG_CHECK;
	rc |= bq27541_write_i2c_block(chip, BQ28Z610_TERM_VOLT_CHECK_ADDR, 2, check_data);
	usleep_range(1000, 1000);

	if (cc_check == false) {
		bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_NAME_CMD);
		usleep_range(1000, 1000);
		bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, read_data);
		value = (read_data[9] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_data[8];
		if (rc < 0 || value != cc || (read_data[10] != (BQ28Z610_DEEP_DISCHG_CHECK
			- (read_data[8] + read_data[9]) & BQ28Z610_DEEP_DISCHG_CHECK))) {
			cc_check = true;
			goto write_parameter;
		}
	}

	bq8z610_deep_deinit(chip);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);

	return rc;
}

static int bq28z610_get_last_cc(struct chip_bq27541 *chip)
{
	int cc = 0;
	u8 read_data[BQ28Z610_DEEP_DISCHG_SIZE] = { 0, 0, 0, 0, 0 };

	if (!chip || atomic_read(&chip->suspended) == 1 || (!chip->batt_bq28z610 && !chip->batt_bq27z561))
		return chip->last_cc_pre;

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_NAME_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_DEEP_DISCHG_SIZE, read_data);
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);

	cc = (read_data[9] << BQ28Z610_DEEP_DISCHG_SHIFT_MASK) + read_data[8];
	chg_info("[0x%x, 0x%x] [0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x][0x%x, 0x%x, 0x%x]\n",
			read_data[0], read_data[1], read_data[2], read_data[3], read_data[4], read_data[5],
			read_data[6], read_data[7], read_data[8], read_data[9], read_data[10]);

	if (read_data[10] == (BQ28Z610_DEEP_DISCHG_CHECK - (read_data[8] + read_data[9]) & BQ28Z610_DEEP_DISCHG_CHECK)) {
		chip->last_cc_pre = cc;
		return cc;
	} else {
		return chip->last_cc_pre;
	}
}

static int oplus_get_batt_deep_dischg_count(struct oplus_chg_ic_dev *ic_dev, int *count)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		nfg8011b_get_deep_dischg_counts(chip, count);
	else
		*count = bq28z610_get_deep_dischg_num(chip);

	return 0;
}

static int oplus_get_batt_chem_id(struct oplus_chg_ic_dev *ic_dev, u8 *buf, int len)
{
	struct chip_bq27541 *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	if (len < CHEM_ID_LENGTH)
		return -EINVAL;
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	memcpy(buf, chip->chem_id, CHEM_ID_LENGTH);

	return rc;
}

static int oplus_set_deep_dischg_count(struct oplus_chg_ic_dev *ic_dev, int *count)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		nfg8011b_set_deep_dischg_counts(chip, *count);
	else
		bq28z610_set_deep_dischg_num(chip, *count);
	return 0;
}

static int oplus_set_deep_term_volt(struct oplus_chg_ic_dev *ic_dev, int *volt_mv)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		nfg8011b_set_term_volt(chip, *volt_mv);
	else
		bq28z610_set_term_volt(chip, *volt_mv);
	return 0;
}

static int oplus_get_deep_term_volt(struct oplus_chg_ic_dev *ic_dev, int *volt)
{
	struct chip_bq27541 *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		rc = nfg8011b_get_define_term_volt(chip, volt);
	else
		rc = bq28z610_get_term_volt(chip, volt);

	return rc;
}

static int oplus_set_last_cc(struct oplus_chg_ic_dev *ic_dev, int *cc)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (chip->batt_nfg8011b)
		nfg8011b_set_last_cc(chip, *cc);
	else
		bq28z610_set_last_cc(chip, *cc);

	return 0;
}

static int oplus_get_last_cc(struct oplus_chg_ic_dev *ic_dev, int *cc)
{
	struct chip_bq27541 *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (chip->batt_nfg8011b)
		rc = nfg8011b_get_last_cc(chip, cc);
	else
		*cc = bq28z610_get_last_cc(chip);

	return rc;
}

static int oplus_get_batt_id_info(struct oplus_chg_ic_dev *ic_dev, int *count)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*count = chip->id_value;

	return 0;
}

static int oplus_get_batt_id_match_info(struct oplus_chg_ic_dev *ic_dev, int *count)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->id_match_status == GPIO_STATUS_NOT_SUPPORT)
		return -ENOTSUPP;

	if (chip->id_value == chip->id_match_status)
		*count = ID_MATCH_SILI;
	else
		*count = ID_NOT_MATCH;

	return 0;
}


#ifdef CONFIG_OPLUS_CHARGER_MTK
#define AUTH_MESSAGE_LEN 20
#define AUTH_TAG "ogauge_auth="
#define AUTH_SHA256_TAG "ogauge_sha256_auth="
#define AUTH_PROP "ogauge_auth"
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

static int get_auth_msg(u8 *source, u8 *rst) {
	char *str = NULL;
	int i;

	str = strstr(oplus_chg_get_cmdline(AUTH_PROP), AUTH_TAG);
	if (str == NULL) {
		pr_err("Asynchronous authentication is not supported!!!\n");
		return -1;
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

static int bq27541_sha1_hmac_authenticate(
	struct chip_bq27541 *chip, struct bq27541_authenticate_data *authenticate_data)
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
		checksum_buf[0] =
			checksum_buf[0] + authenticate_data->message[i];
	}
	checksum_buf[0] = 0xff - (checksum_buf[0] & 0xff);

#ifdef XBL_AUTH_DEBUG
	for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3; i = i + 4) {
		pr_info("%s: sending msg[%d]=%x, sending msg[%d]=%x,sending msg[%d]=%x, sending msg[%d]=%x\n",
			__func__, i, authenticate_data->message[i], i + 1,
			authenticate_data->message[i + 1], i + 2,
			authenticate_data->message[i + 2], i + 3,
			authenticate_data->message[i + 3]);
	}
#endif
	/* step 1: seal mode->write 0x00 to dataflashblock */
	ret = bq27541_i2c_txsubcmd_onebyte(chip, DATAFLASHBLOCK, authen_cmd_buf[0]);

	if (ret < 0) {
		chg_err("%s i2c write error\n", __func__);
		return -1;
	}
	/* step 2: write 20 bytes to authendata_reg */

	bq27541_write_i2c_block(chip, AUTHENDATA, authenticate_data->message_len,
				authenticate_data->message);
	msleep(5);
	/* step 3: write checksum to authenchecksum_reg for compute */
	bq27541_i2c_txsubcmd_onebyte(chip, AUTHENCHECKSUM, checksum_buf[0]);
	msleep(10);
	/* step 4: read authendata */
	bq27541_read_i2c_block(chip, AUTHENDATA, authenticate_data->message_len,
			       &recv_buf[0]);

	len = authenticate_data->message_len;
	for (i = 0; i < len / 2; i++) {
		t = recv_buf[i];
		recv_buf[i] = recv_buf[len - i - 1];
		recv_buf[len - i - 1] = t;
	}
#ifdef XBL_AUTH_DEBUG
	for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3; i = i + 4) {
		pr_info("%s: hw[%d]=%x,hw[%d]=%x,hw[%d]=%x,hw[%d]=%x\n",
			__func__, i, recv_buf[i], i + 1, recv_buf[i + 1], i + 2,
			recv_buf[i + 2], i + 3, recv_buf[i + 3]);
	}
#endif
	memcpy(authenticate_data->message, &recv_buf[0],
	       authenticate_data->message_len);
#ifdef XBL_AUTH_DEBUG
	for (i = 0; i < authenticate_data->message_len; i++) {
		pr_err("HW_sha1_hmac_result[0x%x]",
		       authenticate_data->message[i]);
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
	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY,
				  SMEM_RESERVED_BOOT_INFO_FOR_APPS, &smem_size);
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
				memcpy(auth, &smem_data->rst_k0,
				       sizeof(oplus_gauge_auth_result));
			} else {
				memcpy(auth, &smem_data->rst_k1,
				       sizeof(oplus_gauge_auth_result));
				pr_info("%s: auth result=%d\n", __func__,
					auth->result);

#ifdef XBL_AUTH_DEBUG
				for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3;
				     i = i + 4) {
					pr_info("%s: msg[%d]=%x,msg[%d]=%x,msg[%d]=%x,msg[%d]=%x\n",
						__func__, i, auth->msg[i],
						i + 1, auth->msg[i + 1], i + 2,
						auth->msg[i + 2], i + 3,
						auth->msg[i + 3]);
				}
				for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3;
				     i = i + 4) {
					pr_info("%s: rcv_msg[%d]=%x,rcv_msg[%d]=%x,rcv_msg[%d]=%x,rcv_msg[%d]=%x\n",
						__func__, i, auth->rcv_msg[i],
						i + 1, auth->rcv_msg[i + 1],
						i + 2, auth->rcv_msg[i + 2],
						i + 3, auth->rcv_msg[i + 3]);
				}
#endif
			}
		}
	}
	return true;
#endif
}

#define EXTEND_DATA_SIZE 34
static int bq28z610_get_info(struct chip_bq27541 *chip, u8 *info, int len)
{
	int i;
	int j;
	int ret;
	int data = 0;
	int index = 0;
	int data_check;
	int try_count = BQ28Z610_SUBCMD_TRY_COUNT;
	u8 extend_data[EXTEND_DATA_SIZE] = {0};
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
		ret = bq27541_read_i2c(chip, standard[i].addr, &data);
		if (ret < 0)
			continue;
		index += snprintf(info + index, len - index,
			  "0x%02x=%02x,%02x|", standard[i].addr, (data & 0xff), ((data >> 8) & 0xff));
	}

	for (i = 0; i < ARRAY_SIZE(extend); i++) {
		try_count = BQ28Z610_SUBCMD_TRY_COUNT;
try:
		mutex_lock(&chip->bq28z610_alt_manufacturer_access);
		ret = bq27541_i2c_txsubcmd(chip, BQ28Z610_DATAFLASHBLOCK, extend[i].addr);
		if (ret < 0) {
			mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
			continue;
		}

		if (sizeof(extend_data) >= extend[i].len + 2) {
			usleep_range(1000, 1000);
			ret = bq27541_read_i2c_block(chip, BQ28Z610_DATAFLASHBLOCK, (extend[i].len + 2), extend_data);
			mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
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
			mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
		}
		if (i  <  ARRAY_SIZE(extend) - 1)
			index += snprintf(info + index, len - index, "|");
	}

	return index;
}

static int oplus_bq27541_get_reg_info(struct oplus_chg_ic_dev *ic_dev, u8 *info, int len)
{
	struct chip_bq27541 *chip;
	int index = -1;

	if (ic_dev == NULL || !info) {
		chg_err("oplus_chg_ic_dev or info is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip || atomic_read(&chip->suspended) || atomic_read(&chip->locked))
		return -1;

	if (chip->batt_bq28z610 && DEVICE_BQ27541 == chip->device_type && !chip->batt_zy0603)
		index = bq28z610_get_info(chip, info, len);
	else if (chip->batt_nfg8011b)
		index = nfg8011b_get_info(chip, info, len);

	return index;
}

static int bq28z610_get_calib_time(struct chip_bq27541 *chip, int *dod_calib_time, int *qmax_calib_time)
{
	int i;
	int ret;
	int data_check;
	int try_count;
	u8 extend_data[16] = {0};
	int check_args[CALIB_TIME_CHECK_ARGS] = {0};
	struct gauge_track_info_reg extend[] = {
		{ BQ28Z610_MAC_CELL_DOD0_CMD, 14 },
		{ BQ28Z610_MAC_CELL_QMAX_CMD, 8 },
	};

	if (!chip || !dod_calib_time || !qmax_calib_time)
		return -1;

	for (i = 0; i < ARRAY_SIZE(extend); i++) {
		try_count = BQ28Z610_SUBCMD_TRY_COUNT;
try:
		mutex_lock(&chip->bq28z610_alt_manufacturer_access);
		ret = bq27541_i2c_txsubcmd(chip, BQ28Z610_DATAFLASHBLOCK, extend[i].addr);
		if (ret < 0) {
			mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
			return -1;
		}

		if (sizeof(extend_data) >= extend[i].len + 2) {
			usleep_range(1000, 1000);
			ret = bq27541_read_i2c_block(chip, BQ28Z610_DATAFLASHBLOCK, (extend[i].len + 2), extend_data);
			mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
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
			mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
		}
	}

	if (check_args[0] != chip->calib_check_args_pre[0] || check_args[3] != chip->calib_check_args_pre[3])
		chip->dod_time = 1;
	else
		chip->dod_time++;

	if (check_args[1] != chip->calib_check_args_pre[1] || check_args[2] != chip->calib_check_args_pre[2] ||
	    check_args[4] != chip->calib_check_args_pre[4] || check_args[5] != chip->calib_check_args_pre[5])
		chip->qmax_time = 1;
	else
		chip->qmax_time++;

	memcpy(chip->calib_check_args_pre, check_args, sizeof(check_args));
	*dod_calib_time = chip->dod_time;
	*qmax_calib_time = chip->qmax_time;

	return ret;
}

static void oplus_bq27541_calib_args_to_check_args(
			struct chip_bq27541 *chip, char *calib_args, int len)
{
	int i;
	int j;

	if (len != (CALIB_TIME_CHECK_ARGS * 2)) {
		chg_err("len not match\n");
		return;
	}

	for (i = 0, j = 0; i < CALIB_TIME_CHECK_ARGS; i++, j += 2) {
		chip->calib_check_args_pre[i] = (calib_args[j + 1] << 0x8) + calib_args[j];
		chg_debug("calib_check_args_pre[%d]=0x%04x\n", i, chip->calib_check_args_pre[i]);
	}
}

static void oplus_bq27541_check_args_to_calib_args(
			struct chip_bq27541 *chip, char *calib_args, int len)
{
	int i;
	int j;

	if (len != (CALIB_TIME_CHECK_ARGS * 2)) {
		chg_err("len not match\n");
		return;
	}

	for (i = 0, j = 0; i < CALIB_TIME_CHECK_ARGS; i++, j += 2) {
		calib_args[j] = chip->calib_check_args_pre[i] & 0xff;
		calib_args[j + 1] = (chip->calib_check_args_pre[i] >> 0x8) & 0xff;
		chg_debug("calib_args[%d]=0x%02x, 0x%02x\n", i, calib_args[j], calib_args[j + 1]);
	}
}

static int oplus_bq27541_get_calib_time(struct oplus_chg_ic_dev *ic_dev,
	int *dod_calib_time, int *qmax_calib_time, char *calib_args, int len)
{
	struct chip_bq27541 *chip;
	int rc = -1;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	if (calib_args == NULL || !dod_calib_time || !qmax_calib_time || !len)
		return -EINVAL;

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	if (chip->calib_info_save_support && !chip->calib_info_init) {
		*dod_calib_time = -1;
		*qmax_calib_time = -1;
		return 0;
	}

	if (atomic_read(&chip->suspended) || atomic_read(&chip->locked)) {
		*dod_calib_time = chip->dod_time_pre;
		*qmax_calib_time = chip->qmax_time_pre;
		return 0;
	}
	mutex_lock(&chip->calib_time_mutex);
	if (chip->batt_bq28z610 && DEVICE_BQ27541 == chip->device_type && !chip->batt_zy0603)
		rc = bq28z610_get_calib_time(chip, dod_calib_time, qmax_calib_time);
	else if (chip->batt_nfg8011b)
		rc = nfg8011b_get_calib_time(chip, dod_calib_time, qmax_calib_time);

	oplus_bq27541_check_args_to_calib_args(chip, calib_args, len);
	chip->dod_time_pre = *dod_calib_time;
	chip->qmax_time_pre = *qmax_calib_time;
	mutex_unlock(&chip->calib_time_mutex);

	return rc;
}

static int oplus_bq27541_set_calib_time(struct oplus_chg_ic_dev *ic_dev,
	int dod_calib_time, int qmax_calib_time, char *calib_args, int len)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL)
		return -ENODEV;

	if (calib_args == NULL || !len)
		return -EINVAL;

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (len != (CALIB_TIME_CHECK_ARGS * 2)) {
		chg_err("len not match\n");
		return -EINVAL;
	}

	if (dod_calib_time) {
		chip->dod_time_pre = dod_calib_time;
		chip->qmax_time_pre = qmax_calib_time;
	} else {
		chip->dod_time_pre = 1;
		chip->qmax_time_pre = 1;
	}
	chip->dod_time = dod_calib_time;
	chip->qmax_time = qmax_calib_time;
	oplus_bq27541_calib_args_to_check_args(chip, calib_args, len);
	chip->calib_info_init = true;

	return 0;
}


static bool get_smem_sha256_batt_info(
	struct chip_bq27541 *chip, struct oplus_gauge_sha256_auth_result *sha256_auth)
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
	size_t smem_size;
	void *smem_addr;
	oplus_gauge_auth_info_type *smem_data;

	if (!chip || !sha256_auth) {
		chg_err("invalid parameters\n");
		return false;
	}

	memset(sha256_auth, 0, sizeof(struct oplus_gauge_sha256_auth_result));
	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_RESERVED_BOOT_INFO_FOR_APPS, &smem_size);
	if (IS_ERR(smem_addr)) {
		pr_err("unable to acquire smem SMEM_RESERVED_BOOT_INFO_FOR_APPS entry\n");
		return false;
	} else {
		smem_data = (oplus_gauge_auth_info_type *)smem_addr;
		if (smem_data == ERR_PTR(-EPROBE_DEFER)) {
			chg_err("fail to get smem_data\n");
			return false;
		} else {
			memcpy(sha256_auth, &smem_data->sha256_rst_k0,
					sizeof(struct oplus_gauge_sha256_auth_result));
			chg_info("ap_random=[%*ph]\n", GAUGE_SHA256_AUTH_MSG_LEN, sha256_auth->msg);
			chg_info("ap_encode=[%*ph]\n", GAUGE_SHA256_AUTH_MSG_LEN, sha256_auth->rcv_msg);
		}
	}

	return true;
#endif
}

static bool bq27541_sha256_hmac_result_check(struct chip_bq27541 *chip)
{
	bool ret = false;
	int len = ARRAY_SIZE(chip->sha256_authenticate_data->random);
	u8 *ap_temp = chip->sha256_authenticate_data->ap_encode;
	u8 *gauge_temp = chip->sha256_authenticate_data->gauge_encode;

	chg_info("ap_encode   =[%*ph]\n", len, ap_temp);
	chg_info("gauge_encode=[%*ph]\n", GAUGE_SHA256_AUTH_MSG_LEN, gauge_temp);

	if (memcmp(ap_temp, gauge_temp, len)) {
		chg_err("gauge authenticate compare failed\n");
		ret =  false;
	} else {
		chg_info("gauge authenticate succeed\n");
		ret = true;
	}

	return ret;
}

static bool init_sha256_gauge_auth(struct chip_bq27541 *chip)
{
	bool ret;
	struct oplus_gauge_sha256_auth_result sha256_auth;

	if (!chip || !chip->sha256_authenticate_data)
		return false;

	ret = get_smem_sha256_batt_info(chip, &sha256_auth);
	if (!ret) {
		pr_err("get smem sha256 batt info failed\n");
		return ret;
	}

	memset(chip->sha256_authenticate_data, 0, sizeof(struct oplus_gauge_sha256_auth));
	memcpy(chip->sha256_authenticate_data->random,
		sha256_auth.msg, GAUGE_SHA256_AUTH_MSG_LEN);
	memcpy(chip->sha256_authenticate_data->ap_encode,
		sha256_auth.rcv_msg, GAUGE_SHA256_AUTH_MSG_LEN);

	if (chip->batt_nfg8011b)
		ret = nfg8011b_sha256_hmac_authenticate(chip);
	else
		ret = false;
	if (!ret)
		return ret;

	ret = bq27541_sha256_hmac_result_check(chip);

	return ret;
}

static bool init_gauge_auth(struct chip_bq27541 *chip,
			    oplus_gauge_auth_result *rst,
			    struct bq27541_authenticate_data *authenticate_data)
{
	int len = GAUGE_AUTH_MSG_LEN < AUTHEN_MESSAGE_MAX_COUNT ?
			  GAUGE_AUTH_MSG_LEN :
				AUTHEN_MESSAGE_MAX_COUNT;
	if (NULL == rst || NULL == authenticate_data) {
		pr_err("Gauge authenticate failed\n");
		return false;
	}
	/* comment for keeping authenticattion in kernel
	if (rst->result) {
		pr_err("Gauge authenticate succeed from xbl\n");
		return true;
	}
	*/
	memset(authenticate_data, 0, sizeof(struct bq27541_authenticate_data));
	authenticate_data->message_len = len;
	memcpy(authenticate_data->message, rst->msg, len);
	bq27541_sha1_hmac_authenticate(chip, authenticate_data);

	if (memcmp(authenticate_data->message, rst->rcv_msg, len)) {
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
	int ret = 0;
	char *version;
	char *manufacture;

	switch (chip->device_type) {
	case DEVICE_BQ27541:
		if(chip->batt_zy0603) {
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
	case DEVICE_ZY0602:
		version = "zy0602";
		manufacture = "ZY";
		break;
	default:
		version = "unknown";
		manufacture = "UNKNOWN";
		break;
	}
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (chip->gauge_num == 0)
		ret = register_device_proc("gauge", version, manufacture);
	else
		ret = register_device_proc("sub_gauge", version, manufacture);
#endif
	if (ret) {
		pr_err("register_gauge_devinfo fail\n");
	}
}

static void bq27541_reset(struct i2c_client *client)
{
	struct chip_bq27541 *chip;
	int ui_soc;

	chip = dev_get_drvdata(&client->dev);
	/* TODO int ui_soc = oplus_chg_get_ui_soc(); */
	ui_soc = bq27541_get_battery_soc(chip);

	if (!chip || chip->batt_zy0603 || chip->batt_nfg8011b)
		return;

	if (bq27541_get_battery_mvolts(chip) <= 3300 &&
	    bq27541_get_battery_mvolts(chip) > 2500 && ui_soc == 0 &&
	    bq27541_get_battery_temperature(chip) > 150) {
		if (!unseal(chip, BQ27541_UNSEAL_KEY)) {
			pr_err("bq27541 unseal fail !\n");
			return;
		}
		chg_info(
			"bq27541 unseal OK vol = %d, ui_soc = %d, temp = %d!\n",
			bq27541_get_battery_mvolts(chip), ui_soc,
			bq27541_get_battery_temperature(chip));
		if (chip->device_type == DEVICE_BQ27541 ||
		    chip->device_type == DEVICE_ZY0602) {
			bq27541_cntl_cmd(chip, BQ27541_RESET_SUBCMD);
		} else if (chip->device_type == DEVICE_BQ27411) {
			bq27541_cntl_cmd(chip, BQ27411_RESET_SUBCMD); /*  27411  */
		}
		msleep(50);
		if (chip->device_type == DEVICE_BQ27411) {
			if (!seal(chip)) {
				pr_err("bq27411 seal fail\n");
			}
		}
		msleep(150);
		chg_info("bq27541_reset, point = %d\r\n",
			 bq27541_get_battery_soc(chip));
	} else if (chip) {
		bq27411_modify_soc_smooth_parameter(chip, false);
	}
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int bq27541_pm_resume(struct device *dev)
{
	struct chip_bq27541 *chip;

	chip = dev_get_drvdata(dev);
	if (!chip) {
		return 0;
	}
	atomic_set(&chip->suspended, 0);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_RESUME);
	return 0;
}

static int bq27541_pm_suspend(struct device *dev)
{
	struct chip_bq27541 *chip;

	chip = dev_get_drvdata(dev);
	if (!chip) {
		return 0;
	}
	atomic_set(&chip->suspended, 1);
	return 0;
}

static const struct dev_pm_ops bq27541_pm_ops = {
	.resume = bq27541_pm_resume,
	.suspend = bq27541_pm_suspend,
};

#else /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/
static int bq27541_resume(struct i2c_client *client)
{
	struct chip_bq27541 *chip;

	chip = dev_get_drvdata(&client->dev);
	if (!chip) {
		return 0;
	}
	atomic_set(&chip->suspended, 0);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_RESUME);
	return 0;
}

static int bq27541_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct chip_bq27541 *chip;

	chip = dev_get_drvdata(&client->dev);
	if (!chip) {
		return 0;
	}
	atomic_set(&chip->suspended, 1);
	return 0;
}
#endif /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/

bool oplus_gauge_ic_chip_is_null(struct oplus_chg_ic_dev *ic_dev)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return false;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip) {
		return true;
	} else {
		return false;
	}
}

static int oplus_bq27541_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;

	return 0;
}

static int oplus_bq27541_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;

	return 0;
}

static int oplus_bq27541_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	return gauge_reg_dump(chip);
}

static int oplus_bq27541_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[],
				  int len)
{
	return 0;
}

static int oplus_bq27541_get_batt_vol(struct oplus_chg_ic_dev *ic_dev, int index, int *volt)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL || volt == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	switch (index) {
	case 0:
		if ((atomic_read(&chip->suspended) != 0) ||
			(atomic_read(&chip->locked) != 0)) {
			*volt = chip->volt_1_pre;
		} else {
			if (chip->batt_nfg8011b || DEVICE_ZY0602 == chip->device_type)
				*volt = bq27541_get_battery_mvolts(chip);
			else if (DEVICE_BQ27541 == chip->device_type)
				*volt = bq28z610_get_firstcell_voltage(chip);
			else if (DEVICE_BQ27411 == chip->device_type)
				*volt = bq27541_get_battery_mvolts(chip);
			else
				*volt = bq27541_get_battery_mvolts(chip);

			chip->volt_1_pre = *volt;
		}
		break;
	case 1:
		if ((atomic_read(&chip->suspended) != 0) ||
			(atomic_read(&chip->locked) != 0)) {
			*volt = chip->volt_2_pre;
		} else {
			if (chip->batt_nfg8011b || DEVICE_ZY0602 == chip->device_type)
				*volt = bq27541_get_battery_mvolts(chip);
			else if (DEVICE_BQ27541 == chip->device_type)
				*volt = bq28z610_get_secondcell_voltage(chip);
			else if (DEVICE_BQ27411 == chip->device_type)
				*volt = bq27541_get_battery_mvolts(chip);
			else
				*volt = bq27541_get_battery_mvolts(chip);

			chip->volt_2_pre = *volt;
		}
		break;
	default:
		chg_err("Unknown index(=%d), max is 2\n", index);
		return -EINVAL;
	}

	return 0;
}

static int oplus_bq27541_get_batt_max(struct oplus_chg_ic_dev *ic_dev,
				      int *vol_mv)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	bq27541_get_battery_mvolts(chip);
	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*vol_mv = bq27541_get_prev_battery_mvolts_2cell_max(chip);
	else
		*vol_mv = bq27541_get_battery_mvolts_2cell_max(chip);

	return 0;
}

static int oplus_bq27541_get_batt_min(struct oplus_chg_ic_dev *ic_dev,
				      int *vol_mv)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	bq27541_get_battery_mvolts(chip);
	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*vol_mv = bq27541_get_prev_battery_mvolts_2cell_min(chip);
	else
		*vol_mv = bq27541_get_battery_mvolts_2cell_min(chip);

	return 0;
}

static int oplus_bq27541_get_batt_curr(struct oplus_chg_ic_dev *ic_dev,
				       int *curr_ma)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*curr_ma = bq27541_get_prev_average_current(chip);
	else
		*curr_ma = bq27541_get_average_current(chip);

	return 0;
}

static int oplus_bq27541_get_batt_temp(struct oplus_chg_ic_dev *ic_dev,
				       int *temp)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*temp = bq27541_get_prev_battery_temperature(chip);
	else
		*temp = bq27541_get_battery_temperature(chip);

	return 0;
}

static int oplus_bq27541_get_batt_soc(struct oplus_chg_ic_dev *ic_dev, int *soc)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*soc = bq27541_get_prev_battery_soc(chip);
	else
		*soc = bq27541_get_battery_soc(chip);

	return 0;
}

static int oplus_bq27541_get_batt_fcc(struct oplus_chg_ic_dev *ic_dev, int *fcc)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*fcc = bq27541_get_prev_batt_fcc(chip);
	else
		*fcc = bq27541_get_battery_fcc(chip);

	return 0;
}

static int oplus_bq27541_get_batt_cc(struct oplus_chg_ic_dev *ic_dev, int *cc)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*cc = chip->cc_pre;
	else
		*cc = bq27541_get_battery_cc(chip);

	return 0;
}

static int oplus_bq27541_get_batt_rm(struct oplus_chg_ic_dev *ic_dev, int *rm)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*rm = bq27541_get_prev_batt_remaining_capacity(chip);
	else
		*rm = bq27541_get_batt_remaining_capacity(chip);

	return 0;
}

static int oplus_bq27541_get_batt_soh(struct oplus_chg_ic_dev *ic_dev, int *soh)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*soh = chip->soh_pre;
	else
		*soh = bq27541_get_battery_soh(chip);

	return 0;
}

static int oplus_bq27541_get_batt_auth(struct oplus_chg_ic_dev *ic_dev,
				       bool *pass)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	*pass = bq27541_get_battery_authenticate(chip);

	return 0;
}

static int oplus_bq27541_get_afi_update_done(struct oplus_chg_ic_dev *ic_dev,
						bool *status)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
	       chg_err("oplus_chg_ic_dev is NULL");
	       return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	*status = zy0603_afi_update_done(chip);

	return 0;
}

static int oplus_bq27541_get_batt_hmac(struct oplus_chg_ic_dev *ic_dev,
				       bool *pass)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	*pass = bq27541_get_battery_hmac(chip);

	return 0;
}

static int oplus_bq27541_set_batt_full(struct oplus_chg_ic_dev *ic_dev,
				       bool full)
{
	bq27541_set_battery_full(full);

	return 0;
}

static int oplus_bq27541_get_batt_fc(struct oplus_chg_ic_dev *ic_dev, int *fc)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*fc = chip->fc_pre;
	else
		*fc = bq27541_get_battery_fc(chip);

	return 0;
}

static int oplus_bq27541_get_batt_qm(struct oplus_chg_ic_dev *ic_dev, int *qm)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*qm = chip->qm_pre;
	else
		*qm = bq27541_get_battery_qm(chip);

	return 0;
}

static int oplus_bq27541_get_batt_pd(struct oplus_chg_ic_dev *ic_dev, int *pd)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*pd = chip->pd_pre;
	else
		*pd = bq27541_get_battery_pd(chip);

	return 0;
}

static int oplus_bq27541_get_batt_rcu(struct oplus_chg_ic_dev *ic_dev, int *rcu)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*rcu = chip->rcu_pre;
	else
		*rcu = bq27541_get_battery_rcu(chip);

	return 0;
}

static int oplus_bq27541_get_batt_rcf(struct oplus_chg_ic_dev *ic_dev, int *rcf)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*rcf = chip->rcf_pre;
	else
		*rcf = bq27541_get_battery_rcf(chip);

	return 0;
}

static int oplus_bq27541_get_batt_fcu(struct oplus_chg_ic_dev *ic_dev, int *fcu)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*fcu = chip->fcu_pre;
	else
		*fcu = bq27541_get_battery_fcu(chip);

	return 0;
}

static int oplus_bq27541_get_batt_fcf(struct oplus_chg_ic_dev *ic_dev, int *fcf)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*fcf = chip->fcf_pre;
	else
		*fcf = bq27541_get_battery_fcf(chip);

	return 0;
}

static int oplus_bq27541_get_batt_sou(struct oplus_chg_ic_dev *ic_dev, int *sou)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*sou = chip->sou_pre;
	else
		*sou = bq27541_get_battery_sou(chip);

	return 0;
}

static int oplus_bq27541_get_batt_do0(struct oplus_chg_ic_dev *ic_dev, int *do0)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*do0 = chip->do0_pre;
	else
		*do0 = bq27541_get_battery_do0(chip);

	return 0;
}

static int oplus_bq27541_get_batt_doe(struct oplus_chg_ic_dev *ic_dev, int *doe)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*doe = chip->doe_pre;
	else
		*doe = bq27541_get_battery_doe(chip);

	return 0;
}

static int oplus_bq27541_get_batt_trm(struct oplus_chg_ic_dev *ic_dev, int *trm)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*trm = chip->trm_pre;
	else
		*trm = bq27541_get_battery_trm(chip);

	return 0;
}

static int oplus_bq27541_get_batt_pc(struct oplus_chg_ic_dev *ic_dev, int *pc)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*pc = chip->pc_pre;
	else
		*pc = bq27541_get_battery_pc(chip);

	return 0;
}

static int oplus_bq27541_get_batt_qs(struct oplus_chg_ic_dev *ic_dev, int *qs)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*qs = chip->qs_pre;
	else
		*qs = bq27541_get_battery_qs(chip);

	return 0;
}

static int oplus_bq27541_update_dod0(struct oplus_chg_ic_dev *ic_dev)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (atomic_read(&chip->locked) != 0)
		return -EINVAL;

	return bq28z610_modify_dod0(chip);
}

static int
oplus_bq27541_update_soc_smooth_parameter(struct oplus_chg_ic_dev *ic_dev)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (atomic_read(&chip->locked) != 0)
		return -EINVAL;
	return bq28z610_update_soc_smooth_parameter(chip);
}

static int oplus_bq27541_get_cb_status(struct oplus_chg_ic_dev *ic_dev,
				       int *status)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (atomic_read(&chip->locked) != 0)
		return -EINVAL;
	*status = bq28z610_get_battery_balancing_status();

	return 0;
}

static int oplus_bq27541_get_passedchg(struct oplus_chg_ic_dev *ic_dev,
				       int *val)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
	       chg_err("oplus_chg_ic_dev is NULL");
	       return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (atomic_read(&chip->locked) != 0)
		return -EINVAL;
	return bq27541_get_passedchg(chip, val);
}

static int oplus_bq27541_set_lock(struct oplus_chg_ic_dev *ic_dev, bool lock)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	atomic_set(&chip->locked, lock ? 1 : 0);

	return 0;
}

static int oplus_bq27541_is_locked(struct oplus_chg_ic_dev *ic_dev,
				   bool *locked)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*locked = !!atomic_read(&chip->locked);

	return 0;
}

static int oplus_bq27541_get_batt_num(struct oplus_chg_ic_dev *ic_dev, int *num)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*num = chip->batt_num;

	return 0;
}

static int oplus_bq27541_get_gauge_type(struct oplus_chg_ic_dev *ic_dev, int *gauge_type)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL || gauge_type == NULL) {
		chg_err("oplus_chg_ic_dev or gauge_type is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*gauge_type = GAUGE_TYPE_PACK;

	return 0;
}

static int oplus_bq27541_get_device_type(struct oplus_chg_ic_dev *ic_dev,
					 int *type)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*type = chip->device_type;

	return 0;
}

static int
oplus_bq27541_get_device_type_for_vooc(struct oplus_chg_ic_dev *ic_dev,
				       int *type)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*type = chip->device_type_for_vooc;

	return 0;
}

static int bq28z610_get_battery_gauge_type_for_bcc(struct oplus_chg_ic_dev *ic_dev,
					 int *type)
{
	struct chip_bq27541 *chip;
	int gauge_type;

	if (ic_dev == NULL || type == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	switch (chip->device_type) {
	case DEVICE_BQ27541:
		if(chip->batt_zy0603)
			gauge_type = DEVICE_ZY0603;
		else
			gauge_type = DEVICE_BQ27541;
		break;
	case DEVICE_BQ27411:
		gauge_type = DEVICE_BQ27411;
		break;
	case DEVICE_ZY0602:
		gauge_type = DEVICE_ZY0602;
		break;
	default:
		gauge_type = DEVICE_BQ28Z610;
		break;
	}

	if (chip->batt_nfg8011b)
		gauge_type = DEVICE_NFG8011B;

	*type = gauge_type;
	return 0;
}

static int bq28z610_get_battery_dod0(struct oplus_chg_ic_dev *ic_dev, int index, int *dod0)
{
	struct chip_bq27541 *chip;
	int dod0_1;
	int dod0_2;
	int dod_passed_q;

	if (ic_dev == NULL || dod0 == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0)) {
		dod0_1 = chip->dod0_1_pre;
		dod0_2 = chip->dod0_2_pre;
	} else {
		if (chip->batt_nfg8011b) {
			nfg8011b_get_dod0_parameters(chip, &dod0_1);
			dod0_2 = dod0_1;
		} else if (DEVICE_ZY0602 == chip->device_type) {
			zy602_get_dod0_parameters(chip, &dod0_1);
			dod0_2 = dod0_1;
		} else if (DEVICE_BQ27541 == chip->device_type) {
			bq28z610_get_dod0_parameters(chip, &dod0_1, &dod0_2, &dod_passed_q);
		} else if (DEVICE_BQ27411 == chip->device_type) {
			dod0_1 = bq27541_get_battery_do0(chip);
			dod0_2 = dod0_1;
		} else {
			dod0_1 = chip->dod0_1_pre;
			dod0_2 = chip->dod0_2_pre;
		}
	}

	switch (index) {
	case 0:
		*dod0 = dod0_1;
		break;
	case 1:
		*dod0 = dod0_2;
		break;
	default:
		chg_err("Unknown index(=%d), max is 2\n", index);
		return -EINVAL;
	}

	return 0;
}

static int bq28z610_get_battery_dod0_passed_q(struct oplus_chg_ic_dev *ic_dev, int *dod_passed_q)
{
	struct chip_bq27541 *chip;
	int dod0_1;
	int dod0_2;

	if (ic_dev == NULL || dod_passed_q == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0)) {
		*dod_passed_q = chip->dod_passed_q_pre;
	} else {
		if (chip->batt_nfg8011b)
			*dod_passed_q = 0;
		else if (DEVICE_ZY0602 == chip->device_type)
			*dod_passed_q = zy602_get_battery_passed_q(chip);
		else if (DEVICE_BQ27541 == chip->device_type)
			bq28z610_get_dod0_parameters(chip, &dod0_1, &dod0_2, dod_passed_q);
		else if (DEVICE_BQ27411 == chip->device_type)
			*dod_passed_q = 0;
		else
			*dod_passed_q = chip->dod_passed_q_pre;
	}

	return 0;
}

static int bq28z610_get_battery_qmax(struct oplus_chg_ic_dev *ic_dev, int index, int *qmax)
{
	struct chip_bq27541 *chip;
	int qmax_1;
	int qmax_2;
	int qmax_passed_q;
	int retry = RETRY_CNT;

	if (ic_dev == NULL || qmax == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0)) {
		qmax_1 = chip->qmax_1_pre;
		qmax_2 = chip->qmax_2_pre;
	} else {
		if (chip->batt_nfg8011b) {
			nfg8011b_get_qmax_parameters(chip, &qmax_1);
			qmax_2 = qmax_1;
		} else if (DEVICE_ZY0602 == chip->device_type) {
			zy602_get_qmax_parameters(chip, &qmax_1);
			qmax_2 = qmax_1;
		} else if (DEVICE_BQ27541 == chip->device_type) {
			bq28z610_get_qmax_parameters(chip, &qmax_1, &qmax_2, &qmax_passed_q);
		} else if (DEVICE_BQ27411 == chip->device_type) {
			qmax_1 = bq27541_get_battery_qm(chip);
			qmax_2 = qmax_1;
		} else {
			qmax_1 = chip->qmax_1_pre;
			qmax_2 = qmax_1;
		}
		while ((!normal_range_judge(QMAX_MAX, QMAX_MIN, qmax_1) || !normal_range_judge(QMAX_MAX, QMAX_MIN, qmax_2)) && retry > 0) {
			chg_err("bq28z610_get_battery_qmax %d %d out of range, retry %d\n", qmax_1, qmax_2, retry);
			usleep_range(10000, 10000);
			if (chip->batt_nfg8011b) {
				nfg8011b_get_qmax_parameters(chip, &qmax_1);
				qmax_2 = qmax_1;
			} else if (DEVICE_ZY0602 == chip->device_type) {
				zy602_get_qmax_parameters(chip, &qmax_1);
				qmax_2 = qmax_1;
			} else if (DEVICE_BQ27541 == chip->device_type) {
				bq28z610_get_qmax_parameters(chip, &qmax_1, &qmax_2, &qmax_passed_q);
			} else if (DEVICE_BQ27411 == chip->device_type) {
				qmax_1 = bq27541_get_battery_qm(chip);
				qmax_2 = qmax_1;
			} else {
				qmax_1 = chip->qmax_1_pre;
				qmax_2 = qmax_1;
			}
			retry--;
		}
	}

	switch (index) {
	case 0:
		*qmax = qmax_1;
		break;
	case 1:
		*qmax = qmax_2;
		break;
	default:
		chg_err("Unknown index(=%d), max is 2\n", index);
		return -EINVAL;
	}

	return 0;
}

static int bq28z610_get_battery_qmax_passed_q(struct oplus_chg_ic_dev *ic_dev, int *qmax_passed_q)
{
	struct chip_bq27541 *chip;
	int qmax_1;
	int qmax_2;

	if (ic_dev == NULL || qmax_passed_q == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0)) {
		*qmax_passed_q = chip->qmax_passed_q_pre;
	} else {
		if (chip->batt_nfg8011b)
			*qmax_passed_q = 0;
		else if (DEVICE_ZY0602 == chip->device_type)
			*qmax_passed_q = 0;
		else if (DEVICE_BQ27541 == chip->device_type)
			bq28z610_get_qmax_parameters(chip, &qmax_1, &qmax_2, qmax_passed_q);
		else if (DEVICE_BQ27411 == chip->device_type)
			*qmax_passed_q = 0;
		else
			*qmax_passed_q = chip->qmax_passed_q_pre;
	}

	return 0;
}


static int oplus_bq27541_is_suspend(struct oplus_chg_ic_dev *ic_dev,
				    bool *suspend)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*suspend = atomic_read(&chip->suspended);

	return 0;
}

static int oplus_bq27541_get_batt_exist(struct oplus_chg_ic_dev *ic_dev,
					bool *exist)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (atomic_read(&chip->i2c_err_count) > I2C_ERR_MAX)
		*exist = false;
	else
		*exist = true;

	return 0;
}

static void bq27541_check_iic_recover(struct work_struct *work)
{
	struct chip_bq27541 *chip = container_of(
		work, struct chip_bq27541, check_iic_recover.work);

	/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
	if (chip->i2c_rst_ext)
		bq27541_get_battery_temperature(chip);
	else
	/* end workaround 230504153935012779 */
		bq27541_get_battery_soc(chip);
	chg_err("gauge online state:%d\n", chip->ic_dev->online);
	if (!chip->ic_dev->online) {
		schedule_delayed_work(&chip->check_iic_recover, msecs_to_jiffies(CHECK_IIC_RECOVER_TIME));
	} else {
	 	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ONLINE);
	}
}

static int oplus_bq27541_get_batt_sn(struct chip_bq27541 *chip)
{
	int i;
	int ret;
	int retry = 0;
	u8 check_sum = 0xFF;
	u8 buf[BQ28Z610_BATT_SN_READ_BUF_LEN] = {0x00};
	u8 buf_zy0602[ZY0602_BATT_SN_READ_BUF_LEN] = {0x00};
	u8 batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE] = {0x00};

	if (!chip) {
		chg_err("chip fg_ic is not ready.");
		return -ENODEV;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return 0;
	}

RETRY:
	while (retry < BQ28Z610_BATT_SN_RETRY_MAX) {
		retry++;
		if (chip->batt_zy0603 || chip->batt_bq28z610) {
			mutex_lock(&chip->bq28z610_alt_manufacturer_access);
			ret = bq27541_i2c_txsubcmd(chip, BQ28Z610_BATT_SN_EN_ADDR, BQ28Z610_BATT_SN_CMD);
			if (ret < 0) {
				mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
				continue;
			}
			usleep_range(1000, 1000);
			ret = bq27541_read_i2c_block(chip, BQ28Z610_BATT_SN_EN_ADDR,
						    BQ28Z610_BATT_SN_READ_BUF_LEN, buf);
			mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
			if (ret < 0)
				continue;

			if ((buf[0] == (BQ28Z610_BATT_SN_CMD & 0xFF))
			    && (buf[1] == (BQ28Z610_BATT_SN_CMD >> 8))) {
				memcpy(batt_sn, &buf[2], OPLUS_BATT_SERIAL_NUM_SIZE);
				break;
			}
		} else if (chip->device_type == DEVICE_ZY0602) {
			mutex_lock(&chip->bq28z610_alt_manufacturer_access);
			ret = bq27541_i2c_txsubcmd_onebyte(chip, BQ27411_BLOCK_DATA_CONTROL, 0x01);
			if (ret < 0) {
				mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
				continue;
			}
			usleep_range(5000, 5000);
			ret = bq27541_i2c_txsubcmd_onebyte(chip, ZY0602_BATT_SN_EN_ADDR, 0x02);
			if (ret < 0) {
				mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
				continue;
			}
			usleep_range(10000, 10000);

			ret = bq27541_read_i2c_block(chip, ZY0602_BATT_SN_READ_ADDR,
							ZY0602_BATT_SN_READ_BUF_LEN, buf_zy0602);
			mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
			if (ret < 0)
				continue;
			memcpy(batt_sn, &buf_zy0602[8], OPLUS_BATT_SERIAL_NUM_SIZE);
			break;
		} else if (chip->batt_nfg8011b) {
			ret = nfg8011b_read_block(chip, NFG8011B_SUBCMD_MANU_INFO_ADDR, batt_sn,
					OPLUS_BATT_SERIAL_NUM_SIZE, 0, false);
			if (!ret)
				break;
		} else {
			/* TODO other gauge.*/
			retry = BQ28Z610_BATT_SN_RETRY_MAX;
		}
		chg_err("read sn fail, retry(%d)", retry);
	}

	if (retry < BQ28Z610_BATT_SN_RETRY_MAX) {
		if (batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1] != BQ28Z610_BATTINFO_NO_CHECKSUM) {
			check_sum = 0xFF;
			for (i = 0; i < OPLUS_BATT_SERIAL_NUM_SIZE - 1; i++) {
				check_sum -= batt_sn[i];
			}
			if (check_sum != batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1]) {
				chg_err("check_sum fail calc[%d] read[%d], retry.",
					check_sum, batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1]);
				goto RETRY;
			}
		}
		/* replace checksum byte by end of string '\0' */
		batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1] = '\0';
		strlcpy(chip->battinfo.batt_serial_num, (char*)batt_sn, OPLUS_BATT_SERIAL_NUM_SIZE);
	} else {
		chg_err("get sn failed");
	}

	return 0;
}

struct bq28z610_afi_decoder {
	u8 checksum;
	u8 data_len;
	u8 data[];
};

#define BQ28Z610_WRITE_PARAM_MAX_LEN 32
#define BQ28Z610_CHECKSUM_SIZE 2
#define BQ28Z610_AFI_BUF_MAX_LEN 512
static int bq28z610_write_param(struct chip_bq27541 *chip, struct bq28z610_afi_decoder *decoder)
{
	int retry = 1;
	int rc = 0;
	u8 read_data[BQ28Z610_WRITE_PARAM_MAX_LEN] = { 0 };
	u8 write_checksum_data[BQ28Z610_CHECKSUM_SIZE] = { 0 };
	int head_check = 0;
	int cmd = (decoder->data[1] << 8) | decoder->data[0];
	int len = decoder->data_len;

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	do {
		chg_info("write cmd 0x%04x cnt=%d\n", cmd, retry);
		rc = bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, cmd);
		if (rc)
			continue;
		usleep_range(10000, 10000);
		rc = bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, len, read_data);
		if (rc)
			continue;
		head_check = (read_data[1] << 8) | read_data[0];
		if (head_check != cmd) {
			rc = -1;
			chg_err("head check fail 0x%04x expect 0x%04x\n", head_check, cmd);
			continue;
		}
		if (!memcmp(read_data, decoder->data, len)) {
			chg_info("param already modify raw[%*ph]\n", len, decoder->data);
			rc = 0;
			goto done;
		}
		chg_info("original raw[%*ph], change to raw[%*ph]\n", len, read_data, len, decoder->data);
		write_checksum_data[0] = decoder->checksum;
		write_checksum_data[1] = len + BQ28Z610_CHECKSUM_SIZE;

		mutex_lock(&chip->chip_mutex);
		rc = bq27541_write_i2c_block_unsafe(chip, BQ28Z610_REG_CNTL1, len, decoder->data);
		if (rc != 0) {
			chg_err("write %x block fail rc=%d\n", BQ28Z610_REG_CNTL1, rc);
			mutex_unlock(&chip->chip_mutex);
			continue;
		}
		rc = bq27541_write_i2c_block_unsafe(chip, BQ28Z610_REG_CNTL2, BQ28Z610_CHECKSUM_SIZE,
						    write_checksum_data);
		if (rc != 0) {
			chg_err("write %x block fail rc=%d\n", BQ28Z610_REG_CNTL2, rc);
			mutex_unlock(&chip->chip_mutex);
			continue;
		}
		mutex_unlock(&chip->chip_mutex);
		rc = bq27541_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, cmd);
		if (rc != 0)
			continue;
		usleep_range(10000, 10000);
		rc = bq27541_read_i2c_block(chip, BQ28Z610_REG_CNTL1, len, read_data);
		if (rc != 0)
			continue;

		head_check = (read_data[1] << 8) | read_data[0];
		if (head_check != cmd) {
			rc = -2;
			chg_err("head check fail 0x%04x expect 0x%04x\n", head_check, cmd);
			continue;
		}

		if (!memcmp(read_data, decoder->data, len)) {
			chg_info("param modify success\n");
			rc = 0;
			goto done;
		}
		rc = -3;
	} while (retry--);

done:
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	return rc;
}

static u8 bq28z610_calc_checksum(u8 *buf, int len)
{
	u8 checksum = 0;

	while (len--)
		checksum += buf[len];

	checksum = 0xff - checksum;
	return checksum;
}

#define BQ28Z610_AFI_PARAM_DTS "oplus,bq28z610_afi_buf"
static int bq28z610_afi_param_update(struct chip_bq27541 *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct bq28z610_afi_decoder *decoder = NULL;
	int rc = 0;
	int item_len = 0;
	u8 checksum = 0xff;
	int len = 0;

	if (!chip->batt_bq28z610 || chip->batt_zy0603 || oplus_is_rf_ftm_mode())
		return -ENODEV;

	if (!of_property_read_bool(node, BQ28Z610_AFI_PARAM_DTS)) {
		chg_info("not support bq28z610 afi update\n");
		return -ENOTSUPP;
	}

	chip->bq28z610_afi_cnt = of_property_count_u8_elems(node, BQ28Z610_AFI_PARAM_DTS);
	if (chip->bq28z610_afi_cnt <= sizeof(struct bq28z610_afi_decoder) ||
	    chip->bq28z610_afi_cnt > BQ28Z610_AFI_BUF_MAX_LEN) {
		chg_err("bq28z610 afi cnt invalid %d\n", chip->bq28z610_afi_cnt);
		return -EINVAL;
	}

	chip->bq28z610_afi_buf = (u8 *)devm_kzalloc(chip->dev, chip->bq28z610_afi_cnt, GFP_KERNEL);
	if (!chip->bq28z610_afi_buf) {
		chg_err("failed to allocate bq28z610 afi buf\n");
		return -ENOMEM;
	}

	rc = of_property_read_u8_array(node, BQ28Z610_AFI_PARAM_DTS, chip->bq28z610_afi_buf, chip->bq28z610_afi_cnt);
	if (rc) {
		chg_err("read %s array fail rc=%d\n", BQ28Z610_AFI_PARAM_DTS, rc);
		goto read_dts_fail;
	}

	do {
		decoder = (struct bq28z610_afi_decoder *)(chip->bq28z610_afi_buf + len);
		if (!decoder) {
			chg_err("decoder is null\n");
			rc = -EINVAL;
			goto check_afi_fail;
		}
		if (decoder->data_len > BQ28Z610_WRITE_PARAM_MAX_LEN || decoder->data_len <= 2) {
			chg_err("data_len invalid %d (> %d || <= 2) raw[%*ph]\n", decoder->data_len,
				BQ28Z610_WRITE_PARAM_MAX_LEN, item_len, decoder);
			rc = -EINVAL;
			goto check_afi_fail;
		}
		item_len = decoder->data_len + sizeof(struct bq28z610_afi_decoder);
		len += item_len;
		if (len > chip->bq28z610_afi_cnt ||
		    ((chip->bq28z610_afi_cnt - len > 0) &&
		     chip->bq28z610_afi_cnt - len < sizeof(struct bq28z610_afi_decoder))) {
			chg_err("check afi buf fail %d %d\n", len, chip->bq28z610_afi_cnt);
			rc = -EINVAL;
			goto check_afi_fail;
		}

		checksum = bq28z610_calc_checksum(decoder->data, decoder->data_len);
		if (checksum != decoder->checksum) {
			chg_err("checksum fail checksum=%x current_checksum=%x raw[%*ph]\n", decoder->checksum,
				checksum, item_len, decoder);
			rc = -EINVAL;
			goto check_afi_fail;
		}
	} while (len < chip->bq28z610_afi_cnt);

	if (bq8z610_sealed(chip)) {
		if (!bq8z610_unseal(chip))
			goto read_dts_fail;
		else
			msleep(50);
	}

	len = 0;
	do {
		decoder = (struct bq28z610_afi_decoder *)(chip->bq28z610_afi_buf + len);
		item_len = decoder->data_len + sizeof(struct bq28z610_afi_decoder);
		rc = bq28z610_write_param(chip, decoder);
		if (rc)
			chg_err("write param fail rc=%d raw[%*ph]\n", rc, item_len, decoder);
		else
			chg_info("write param success raw[%*ph]\n", item_len, decoder);
		usleep_range(1000, 1000);
		len += item_len;
	} while (len < chip->bq28z610_afi_cnt);

	rc = bq8z610_sealed(chip);
	if ((rc == 0) || (rc == -1)) {
		usleep_range(1000, 1000);
		bq8z610_seal(chip);
	}
check_afi_fail:
read_dts_fail:
	if (chip->bq28z610_afi_buf)
		devm_kfree(chip->dev, chip->bq28z610_afi_buf);
	return rc;
}

static int oplus_bq27541_get_battinfo_sn(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	struct chip_bq27541 *chip;
	int bsnlen = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip || !buf || len < OPLUS_BATT_SERIAL_NUM_SIZE)
		return -EINVAL;

	chg_info("BattSN(%s):%s", ic_dev->name, chip->battinfo.batt_serial_num);
	bsnlen = strlcpy(buf, chip->battinfo.batt_serial_num, OPLUS_BATT_SERIAL_NUM_SIZE);

	return bsnlen;
}

static int oplus_bq27541_get_manu_date(struct oplus_chg_ic_dev *ic_dev, char *buf, int len)
{
	struct chip_bq27541 *chip;
	int date_len = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip || !buf || len < OPLUS_BATTINFO_DATE_SIZE)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	date_len = snprintf(buf, len, "%d-%02d-%02d", (((chip->battinfo.manu_date >> 9) & 0x7F) + 1980),
			    (chip->battinfo.manu_date >> 5) & 0xF, chip->battinfo.manu_date & 0x1F);
	return date_len;
}

static int oplus_bq27541_get_first_usage_date(struct oplus_chg_ic_dev *ic_dev, char *buf, int len)
{
	struct chip_bq27541 *chip;
	int date_len = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip || !buf || len < OPLUS_BATTINFO_DATE_SIZE)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	date_len = snprintf(buf, len, "%d-%02d-%02d", (((chip->battinfo.first_usage_date >> 9) & 0x7F) + 1980),
			    (chip->battinfo.first_usage_date >> 5) & 0xF, chip->battinfo.first_usage_date & 0x1F);

	return date_len;
}

static int oplus_bq27541_set_first_usage_date(struct oplus_chg_ic_dev *ic_dev, const char *buf)
{
	struct chip_bq27541 *chip;
	int year = 0;
	int month = 0;
	int day = 0;
	int rc = 0;
	u16 date = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip || !buf)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	sscanf(buf, "%d-%d-%d", &year, &month, &day);
	date = (((year - 1980) & 0x7F) << 9) | ((month & 0xF) << 5) | (day & 0x1F);

	if (chip->batt_nfg8011b) {
		rc = nfg8011b_write_block(chip, NFG8011B_SUBCMD_DEEP_INFO_ADDR, (u8 *)&date, sizeof(date),
					  NFG8011B_ECO_FIRST_USAGE_DATE_OFFSET, true);
		if (rc) {
			chg_err("set fail rc = %d\n", rc);
			return rc;
		}
		chip->battinfo.first_usage_date = date;
	} else {
		chg_err("not support\n");
		return -ENOTSUPP;
	}

	return 0;
}

static int oplus_bq27541_get_ui_cycle_count(struct oplus_chg_ic_dev *ic_dev, u16 *ui_cycle_count)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip || !ui_cycle_count)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	*ui_cycle_count = chip->battinfo.ui_cycle_count;
	return 0;
}

static int oplus_bq27541_set_ui_cycle_count(struct oplus_chg_ic_dev *ic_dev, u16 ui_cycle_count)
{
	struct chip_bq27541 *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	if (chip->batt_nfg8011b) {
		rc = nfg8011b_write_block(chip, NFG8011B_SUBCMD_DEEP_INFO_ADDR, (u8 *)&ui_cycle_count,
					  sizeof(ui_cycle_count), NFG8011B_ECO_UI_CC_OFFSET, true);
		if (rc) {
			chg_err("set fail rc = %d\n", rc);
			return rc;
		}
		chip->battinfo.ui_cycle_count = ui_cycle_count;
	} else {
		chg_err("not support\n");
		return -ENOTSUPP;
	}

	return 0;
}

static int oplus_bq27541_get_ui_soh(struct oplus_chg_ic_dev *ic_dev, u8 *ui_soh)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	*ui_soh = chip->battinfo.ui_soh;

	return 0;
}

static int oplus_bq27541_set_ui_soh(struct oplus_chg_ic_dev *ic_dev, u8 ui_soh)
{
	struct chip_bq27541 *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	if (chip->batt_nfg8011b) {
		rc = nfg8011b_write_block(chip, NFG8011B_SUBCMD_DEEP_INFO_ADDR, (u8 *)&ui_soh, sizeof(ui_soh),
					  NFG8011B_ECO_UI_SOH_OFFSET, true);
		if (rc) {
			chg_err("set fail rc = %d\n", rc);
			return rc;
		}
		chip->battinfo.ui_soh = ui_soh;
	} else {
		chg_err("not support\n");
		return -ENOTSUPP;
	}

	return 0;
}

static int oplus_bq27541_get_used_flag(struct oplus_chg_ic_dev *ic_dev, u8 *used_flag)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip || !used_flag)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	*used_flag = chip->battinfo.used_flag;

	return 0;
}

static int oplus_bq27541_set_used_flag(struct oplus_chg_ic_dev *ic_dev, u8 used_flag)
{
	struct chip_bq27541 *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	if (chip->batt_nfg8011b) {
		rc = nfg8011b_write_block(chip, NFG8011B_SUBCMD_DEEP_INFO_ADDR, (u8 *)&used_flag, sizeof(used_flag),
					  NFG8011B_ECO_USED_FLAG_OFFSET, true);
		if (rc) {
			chg_err("set fail rc = %d\n", rc);
			return rc;
		}
		chip->battinfo.used_flag = used_flag;
	} else {
		chg_err("not support\n");
		return -ENOTSUPP;
	}

	return 0;
}

static void oplus_bq27541_get_manu_battinfo_work(struct work_struct *work)
{
	int rc = 0;
	struct chip_bq27541 *chip = container_of(work, struct chip_bq27541, get_manu_battinfo_work.work);

	if (!chip->support_eco_design) {
		chg_info("not support eco design\n");
		return;
	}

	if (chip->batt_nfg8011b) {
		rc = nfg8011b_read_block(chip, NFG8011B_SUBCMD_MANU_DATE_ADDR, (u8 *)&chip->battinfo.manu_date,
					 sizeof(chip->battinfo.manu_date), 0, false);
		if (rc)
			chg_err("get manu_date fail rc = %d\n", rc);
		rc = nfg8011b_read_block(chip, NFG8011B_SUBCMD_DEEP_INFO_ADDR, (u8 *)&chip->battinfo.first_usage_date,
					 sizeof(chip->battinfo.first_usage_date), NFG8011B_ECO_FIRST_USAGE_DATE_OFFSET,
					 true);
		if (rc)
			chg_err("get first_usage_date fail rc = %d\n", rc);
		rc = nfg8011b_read_block(chip, NFG8011B_SUBCMD_DEEP_INFO_ADDR, (u8 *)&chip->battinfo.ui_cycle_count,
					 sizeof(chip->battinfo.ui_cycle_count), NFG8011B_ECO_UI_CC_OFFSET, true);
		if (rc)
			chg_err("get ui_cycle_count fail rc = %d\n", rc);
		rc = nfg8011b_read_block(chip, NFG8011B_SUBCMD_DEEP_INFO_ADDR, (u8 *)&chip->battinfo.ui_soh,
					 sizeof(chip->battinfo.ui_soh), NFG8011B_ECO_UI_SOH_OFFSET, true);
		if (rc)
			chg_err("get ui_soh fail rc = %d\n", rc);
		rc = nfg8011b_read_block(chip, NFG8011B_SUBCMD_DEEP_INFO_ADDR, (u8 *)&chip->battinfo.used_flag,
					 sizeof(chip->battinfo.used_flag), NFG8011B_ECO_USED_FLAG_OFFSET, true);
		if (rc)
			chg_err("get used_flag fail rc = %d\n", rc);
	}
}

static int oplus_gauge_set_spare_power_enable(struct oplus_chg_ic_dev *ic_dev)
{
	int ret = -EINVAL;
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		ret = nfg8011b_set_spare_power_enable(chip);
	return ret;
}

static int oplus_gauge_get_sili_simulate_term_volt(struct oplus_chg_ic_dev *ic_dev, int *volt)
{
	struct chip_bq27541 *chip;
	int ret = -EINVAL;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		ret = nfg8011b_get_sili_simulate_term_volt(chip, volt);

	return ret;
}

static int oplus_gauge_get_sili_ic_alg_dsg_enable(struct oplus_chg_ic_dev *ic_dev, bool *dsg_enable)
{
	struct chip_bq27541 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	*dsg_enable = chip->dsg_enable;

	return 0;
}

static int oplus_gauge_get_sili_ic_alg_term_volt(struct oplus_chg_ic_dev *ic_dev, int *volt)
{
	struct chip_bq27541 *chip;
	int ret = -EINVAL;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		ret = nfg8011b_get_sili_ic_alg_term_volt(chip, volt);

	return ret;
}

static int oplus_gauge_set_sili_ic_alg_cfg(struct oplus_chg_ic_dev *ic_dev, int cfg)
{
	struct chip_bq27541 *chip;
	int ret = -EINVAL;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		ret = nfg8011b_set_sili_ic_alg_cfg(chip, cfg);

	return ret;
}

static int oplus_gauge_set_sili_ic_alg_term_volt(struct oplus_chg_ic_dev *ic_dev, int volt)
{
	struct chip_bq27541 *chip;
	int ret = -EINVAL;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		ret = nfg8011b_set_sili_ic_alg_term_volt(chip, volt);

	return ret;
}

static int oplus_gauge_get_sili_lifetime_status(
	struct oplus_chg_ic_dev *ic_dev, struct oplus_gauge_lifetime *lifetime_status)
{
	struct chip_bq27541 *chip;
	int ret = -EINVAL;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		ret = nfg8011b_get_sili_lifetime_status(chip, lifetime_status);

	return ret;
}

static int oplus_gauge_get_sili_lifetime_info(
	struct oplus_chg_ic_dev *ic_dev, u8 *buf, int len)
{
	struct chip_bq27541 *chip;
	int ret = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		ret = nfg8011b_get_sili_lifetime_info(chip, buf, len);

	return ret;
}

static int gauge_get_sili_alg_application_info(
	struct oplus_chg_ic_dev *ic_dev, u8 *buf, int len)
{
	struct chip_bq27541 *chip;
	int ret = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->batt_nfg8011b)
		ret = nfg8011b_get_sili_alg_application_info(chip, buf, len);

	return ret;
}

static void *oplus_chg_get_func(struct oplus_chg_ic_dev *ic_dev,
				enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
					       oplus_bq27541_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_bq27541_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       oplus_bq27541_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST,
					       oplus_bq27541_smt_test);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL,
					       oplus_bq27541_get_batt_vol);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX,
					       oplus_bq27541_get_batt_max);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN,
					       oplus_bq27541_get_batt_min);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR,
			oplus_bq27541_get_batt_curr);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP,
			oplus_bq27541_get_batt_temp);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC,
					       oplus_bq27541_get_batt_soc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC,
					       oplus_bq27541_get_batt_fcc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_CC,
					       oplus_bq27541_get_batt_cc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_RM,
					       oplus_bq27541_get_batt_rm);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH,
					       oplus_bq27541_get_batt_soh);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH,
			oplus_bq27541_get_batt_auth);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC,
			oplus_bq27541_get_batt_hmac);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL,
			oplus_bq27541_set_batt_full);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FC,
					       oplus_bq27541_get_batt_fc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_QM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_QM,
					       oplus_bq27541_get_batt_qm);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_PD:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_PD,
					       oplus_bq27541_get_batt_pd);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU,
					       oplus_bq27541_get_batt_rcu);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF,
					       oplus_bq27541_get_batt_rcf);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU,
					       oplus_bq27541_get_batt_fcu);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF,
					       oplus_bq27541_get_batt_fcf);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU,
					       oplus_bq27541_get_batt_sou);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0,
					       oplus_bq27541_get_batt_do0);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE,
					       oplus_bq27541_get_batt_doe);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM,
					       oplus_bq27541_get_batt_trm);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_PC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_PC,
					       oplus_bq27541_get_batt_pc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_QS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_QS,
					       oplus_bq27541_get_batt_qs);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0,
					       oplus_bq27541_update_dod0);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH,
			oplus_bq27541_update_soc_smooth_parameter);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS,
			oplus_bq27541_get_cb_status);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG,
			oplus_bq27541_get_passedchg);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_LOCK:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_LOCK,
					       oplus_bq27541_set_lock);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_LOCKED:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_IS_LOCKED,
					       oplus_bq27541_is_locked);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM,
					       oplus_bq27541_get_batt_num);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_GAUGE_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_GAUGE_TYPE,
					       oplus_bq27541_get_gauge_type);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE,
			oplus_bq27541_get_device_type);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC,
			oplus_bq27541_get_device_type_for_vooc);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_IS_SUSPEND,
					       oplus_bq27541_is_suspend);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BCC_PARMS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BCC_PARMS,
					       bq28z610_get_battery_bcc_parameters);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DOD0:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DOD0,
					       bq28z610_get_battery_dod0);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DOD0_PASSED_Q:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DOD0_PASSED_Q,
					       bq28z610_get_battery_dod0_passed_q);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_QMAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_QMAX,
					       bq28z610_get_battery_qmax);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_QMAX_PASSED_Q:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_QMAX_PASSED_Q,
					       bq28z610_get_battery_qmax_passed_q);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_BCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_BCC,
					       bq28z610_get_battery_gauge_type_for_bcc);
		break;
	case OPLUS_IC_FUNC_GAUGE_FASTCHG_UPDATE_BCC_PARMS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_FASTCHG_UPDATE_BCC_PARMS,
					       bq28z610_get_fastchg_battery_bcc_parameters);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_PREV_BCC_PARMS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_PREV_BCC_PARMS,
					       bq28z610_get_prev_battery_bcc_parameters);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_BCC_PARMS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_BCC_PARMS,
					       bq28z610_set_bcc_debug_parameters);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_PROTECT_CHECK:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_PROTECT_CHECK,
					       zy0603_protect_check);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_SILI_SPARE_POWER:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_SILI_SPARE_POWER,
					       oplus_gauge_set_spare_power_enable);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_SILI_SIMULATE_TERM_VOLT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_SILI_SIMULATE_TERM_VOLT,
					       oplus_gauge_get_sili_simulate_term_volt);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_SILI_IC_ALG_TERM_VOLT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_SILI_IC_ALG_TERM_VOLT,
					       oplus_gauge_get_sili_ic_alg_term_volt);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_SILI_IC_ALG_CFG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_SILI_IC_ALG_CFG,
					       oplus_gauge_set_sili_ic_alg_cfg);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_SILI_IC_ALG_DSG_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_SILI_IC_ALG_DSG_ENABLE,
						oplus_gauge_get_sili_ic_alg_dsg_enable);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_SILI_IC_ALG_TERM_VOLT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_SILI_IC_ALG_TERM_VOLT,
						oplus_gauge_set_sili_ic_alg_term_volt);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_SILI_LIFETIME_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_SILI_LIFETIME_STATUS,
			oplus_gauge_get_sili_lifetime_status);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_SILI_LIFETIME_INFO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_SILI_LIFETIME_INFO,
			oplus_gauge_get_sili_lifetime_info);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_SILI_ALG_APPLICATION_INFO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_SILI_ALG_APPLICATION_INFO,
			gauge_get_sili_alg_application_info);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_AFI_UPDATE_DONE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_AFI_UPDATE_DONE,
					       oplus_bq27541_get_afi_update_done);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST,
			oplus_bq27541_get_batt_exist);
		break;
	case OPLUS_IC_FUNC_GAUGE_CHECK_RESET:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_CHECK_RESET,
					       oplus_detection_reset_condition);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_RESET:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_RESET,
					      oplus_fg_reset);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEEP_DISCHG_COUNT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DEEP_DISCHG_COUNT,
						  oplus_get_batt_deep_dischg_count);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_DEEP_DISCHG_COUNT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_DEEP_DISCHG_COUNT,
						  oplus_set_deep_dischg_count);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_DEEP_TERM_VOLT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_DEEP_TERM_VOLT,
						  oplus_set_deep_term_volt);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_LAST_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_LAST_CC,
						  oplus_set_last_cc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATTID_INFO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATTID_INFO,
						  oplus_get_batt_id_info);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_REG_INFO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_REG_INFO,
					      oplus_bq27541_get_reg_info);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_CALIB_TIME:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_CALIB_TIME,
					      oplus_bq27541_get_calib_time);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_CALIB_TIME:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_CALIB_TIME,
					      oplus_bq27541_set_calib_time);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SN,
					      oplus_bq27541_get_battinfo_sn);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_MANU_DATE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_MANU_DATE,
					       oplus_bq27541_get_manu_date);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_FIRST_USAGE_DATE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_FIRST_USAGE_DATE,
					       oplus_bq27541_get_first_usage_date);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_FIRST_USAGE_DATE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_FIRST_USAGE_DATE,
					       oplus_bq27541_set_first_usage_date);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_UI_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_UI_CC,
					       oplus_bq27541_get_ui_cycle_count);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_UI_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_UI_CC,
					       oplus_bq27541_set_ui_cycle_count);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_UI_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_UI_SOH,
					       oplus_bq27541_get_ui_soh);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_UI_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_UI_SOH,
					       oplus_bq27541_set_ui_soh);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_USED_FLAG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_USED_FLAG,
					       oplus_bq27541_get_used_flag);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_USED_FLAG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_USED_FLAG,
					       oplus_bq27541_set_used_flag);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATTID_MATCH_INFO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATTID_MATCH_INFO,
					      oplus_get_batt_id_match_info);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEEP_TERM_VOLT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DEEP_TERM_VOLT,
					      oplus_get_deep_term_volt);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_CHEM_ID:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_CHEM_ID,
					      oplus_get_batt_chem_id);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_LAST_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_LAST_CC,
					      oplus_get_last_cc);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq bq27541_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_ONLINE },
	{ .virq_id = OPLUS_IC_VIRQ_OFFLINE },
	{ .virq_id = OPLUS_IC_VIRQ_RESUME },
};

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
static struct regmap_config bq27541_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xffff,
};
#endif

#define LEVEL_SHIFT_READY_TRY_COUNT_MAX 3
static bool bq27541_need_level_shift(struct device_node *node)
{
	struct device_node *ic_node;

	ic_node = of_parse_phandle(node, "oplus,level_shift_ic", 0);
	if (!ic_node)
		return false;

	return true;
}

static bool is_level_shift_available(struct device_node *node)
{
	static int try_count = 0;

	if (try_count++ > LEVEL_SHIFT_READY_TRY_COUNT_MAX)
		return true;

	if (of_get_oplus_chg_ic(node, "oplus,level_shift_ic", 0))
		return true;

	return false;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))
static int bq27541_driver_probe(struct i2c_client *client)
#else
static int bq27541_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
#endif
{
	struct chip_bq27541 *fg_ic;
	int rerun_num = 3;
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	int rc = 0;

	if (bq27541_need_level_shift(client->dev.of_node) &&
	    !is_level_shift_available(client->dev.of_node)) {
		dev_err(&client->dev, "level shift driver not ready, try after\n");
		return -EPROBE_DEFER;
	}

	fg_ic = devm_kzalloc(&client->dev, sizeof(*fg_ic), GFP_KERNEL);
	if (!fg_ic) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		return -ENOMEM;
	}
	i2c_set_clientdata(client, fg_ic);
	fg_ic->dev = &client->dev;
	fg_ic->client = client;

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	fg_ic->regmap = devm_regmap_init_i2c(client, &bq27541_regmap_config);
	if (!fg_ic->regmap) {
		rc = -ENODEV;
		goto regmap_init_err;
	}
#endif /* CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT */

	atomic_set(&fg_ic->suspended, 0);
	mutex_init(&fg_ic->chip_mutex);
	mutex_init(&fg_ic->calib_time_mutex);
	mutex_init(&fg_ic->bq28z610_alt_manufacturer_access);
	bq27541_parse_dt(fg_ic);
	/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
	fg_ic->err_status = false;
	/* end workaround 230504153935012779 */
rerun:
	rerun_num--;
	bq27541_hw_config(fg_ic);

	if (fg_ic->batt_zy0603) {
		rc = zy0603_parse_afi_buf(fg_ic);
		dev_err(&client->dev, "zy0603 support and can't config gauge param\n");
	} else {
		if (!fg_ic->modify_soc_calibration) {
			bq28z610_batt_full_zero_parameter(fg_ic);
		}
	}
	/*
	INIT_DELAYED_WORK(&fg_ic->hw_config, bq27541_hw_config);
	schedule_delayed_work(&fg_ic->hw_config, 0);
*/
	INIT_WORK(&fg_ic->fcc_too_small_check_work, bq27541_fcc_too_small_check_work);
	INIT_DELAYED_WORK(&fg_ic->check_iic_recover, bq27541_check_iic_recover);
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

	register_gauge_devinfo(fg_ic);
	if (fg_ic->bq28z610_need_balancing)
		fg_ic->bq28z610_device_chem = bq28z610_get_device_chemistry(fg_ic);

	if (!fg_ic->support_sha256_hmac)
		fg_ic->authenticate_data =
			devm_kzalloc(&client->dev, sizeof(struct bq27541_authenticate_data), GFP_KERNEL);
	else
		fg_ic->sha256_authenticate_data =
			devm_kzalloc(&client->dev, sizeof(struct oplus_gauge_sha256_auth), GFP_KERNEL);

	if (!fg_ic->authenticate_data && !fg_ic->sha256_authenticate_data) {
		pr_err("kzalloc() authenticate_data failed.\n");
		return -ENOMEM;
	}

	oplus_bq27541_get_batt_sn(fg_ic);
	atomic_set(&fg_ic->locked, 0);
	bq28z610_afi_param_update(fg_ic);
	rc = of_property_read_u32(fg_ic->dev->of_node, "oplus,ic_type",
				  &ic_type);
	if (rc < 0) {
		chg_err("can't get ic type, rc=%d\n", rc);
		goto error;
	}
	rc = of_property_read_u32(fg_ic->dev->of_node, "oplus,ic_index",
				  &ic_index);
	if (rc < 0) {
		chg_err("can't get ic index, rc=%d\n", rc);
		goto error;
	}
	ic_cfg.name = fg_ic->dev->of_node->name;
	ic_cfg.index = ic_index;
	switch (fg_ic->device_type) {
	case DEVICE_BQ27541:
		if (fg_ic->batt_zy0603)
			snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "gauge-zy0603:%d", ic_index);
		else if (fg_ic->batt_bq28z610)
			snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "gauge-bq28z610:%d", ic_index);
		else if (fg_ic->batt_nfg8011b)
			snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "gauge-nfg8011b:%d", ic_index);
		else
			snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "gauge-bq27541:%d", ic_index);
		snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
		break;
	case DEVICE_BQ27411:
		snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "gauge-bq27411:%d", ic_index);
		snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
		break;
	case DEVICE_ZY0602:
		snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "gauge-zy0602:%d", ic_index);
		snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
		break;
	default:
		snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "gauge-unknown:%d", ic_index);
		snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
		break;
	}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	fg_ic->odb = devm_oplus_device_bus_register(fg_ic->dev, &bq27541_regmap_config, ic_cfg.manu_name);
	if (IS_ERR_OR_NULL(fg_ic->odb)) {
		chg_err("register odb error\n");
		rc = -EFAULT;
		goto error;
	}
#endif /* CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT */

	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_get_func;
	ic_cfg.virq_data = bq27541_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(bq27541_virq_table);
	ic_cfg.of_node = fg_ic->dev->of_node;
	fg_ic->ic_dev = devm_oplus_chg_ic_register(fg_ic->dev, &ic_cfg);
	if (!fg_ic->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", fg_ic->dev->of_node->name);
		goto ic_reg_error;
	}
	chg_info("register %s\n", fg_ic->dev->of_node->name);

#ifndef CONFIG_OPLUS_CHARGER_MTK
	oplus_vooc_get_fastchg_started_pfunc(&oplus_vooc_get_fastchg_started);
	oplus_vooc_get_fastchg_ing_pfunc(&oplus_vooc_get_fastchg_ing);
#endif

	oplus_bq27541_init(fg_ic->ic_dev);

#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
	if (gpio_is_valid(fg_ic->id_gpio)) {
		fg_ic->battery_id_gpio_test = test_feature_register(&battery_id_gpio_test_cfg, fg_ic);
		if (IS_ERR_OR_NULL(fg_ic->battery_id_gpio_test))
			chg_err("battery_id_gpio_test register error");
	}
	if (fg_ic->fpga_test_support) {
		fg_ic->fpga_fg_test = test_feature_register(&fpga_fg_test_cfg, fg_ic);
		if (IS_ERR_OR_NULL(fg_ic->fpga_fg_test))
			chg_err("fpga_fg_test register error");
	}
#endif
	if (fg_ic->batt_nfg8011b) {
		memset(fg_ic->chem_id, 0, sizeof(fg_ic->chem_id));
		nfg8011b_get_sili_chemistry_info(fg_ic, fg_ic->chem_id, sizeof(fg_ic->chem_id));
		nfg8011b_effect_term_volt_init(fg_ic);
	}

	INIT_DELAYED_WORK(&fg_ic->get_manu_battinfo_work, oplus_bq27541_get_manu_battinfo_work);
	schedule_delayed_work(&fg_ic->get_manu_battinfo_work, 0);

	chg_info("bq27541_driver_probe success\n");
	return 0;

ic_reg_error:
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	devm_oplus_device_bus_unregister(fg_ic->odb);
#endif
error:
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
regmap_init_err:
	devm_kfree(&client->dev, fg_ic);
#endif
	return rc;
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

static __init int bq27541_driver_init(void)
{
	return i2c_add_driver(&bq27541_i2c_driver);
}

static __exit void bq27541_driver_exit(void)
{
	i2c_del_driver(&bq27541_i2c_driver);
}

oplus_chg_module_register(bq27541_driver);

MODULE_DESCRIPTION("Driver for bq27541 charger chip");
MODULE_LICENSE("GPL v2");
