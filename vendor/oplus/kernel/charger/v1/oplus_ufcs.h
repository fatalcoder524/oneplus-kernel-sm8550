// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */

#ifndef _OPLUS_UFCS_H_
#define _OPLUS_UFCS_H_
#include <linux/time.h>
#include <linux/list.h>
#ifndef CONFIG_OPLUS_CHARGER_MTK
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
#include <linux/usb/typec.h>
#include <linux/usb/usbpd.h>
#endif
#endif
#include <linux/random.h>
#include <linux/device.h>
#include <linux/types.h>
#include "oplus_chg_track.h"

#define ufcs_info(fmt, ...) printk(KERN_INFO "[OPLUS_UFCS][%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define ufcs_debug(fmt, ...) printk(KERN_NOTICE "[OPLUS_UFCS][%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define ufcs_err(fmt, ...) printk(KERN_ERR "[OPLUS_UFCS][%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

#define UFCS_MANAGER_VERSION "1.1.0"
/*ufcs curve*/
#define UFCS_VOL_CURVE_HMIN 10000
#define UFCS_VOL_CURVE_LMAX 5500

#define UFCS_BCC_CURRENT_MIN (1000 / 100)
#define UFCS_BATT_CURR_TO_BCC_CURR 100
#define BATT_UFCS_SYS_MAX 40
#define FULL_UFCS_SYS_MAX 6
#define UFCS_R_AVG_NUM 10
#define UFCS_R_ROW_NUM 7

/*ufcs update time*/
#define UFCS_UPDATE_PDO_TIME 5
#define UFCS_UPDATE_FASTCHG_TIME 1
#define UFCS_UPDATE_TEMP_TIME 1
#define UFCS_UPDATE_IBAT_TIME 1
#define UFCS_UPDATE_BCC_TIME 2
#define UPDATE_FULL_TIME_S 1
#define UFCS_UPDATE_FULL_TIME_NS 400 * 1000 * 1000
#define UPDATE_CURVE_TIME_S 1
#define UFCS_UPDATE_CURVE_TIME_NS 400 * 1000 * 1000

#define UFCS_VBAT_DIFF_TIME round_jiffies_relative(msecs_to_jiffies(3 * 60 * 1000))
#define UFCS_UCT_TIME round_jiffies_relative(msecs_to_jiffies(500))

/*ufcs protection*/
#define UFCS_CP_IBUS_OVER_COUNTS 3
#define UFCS_CP_IBUS_MAX 15000

#define UFCS_TEMP_OVER_COUNTS 2
#define UFCS_TEMP_WARM_RANGE_THD 10
#define UFCS_TEMP_LOW_RANGE_THD 20

#define UFCS_CURVE_OV_CNT 4
#define UFCS_CURVE_VBAT_DELTA_MV 5
#define UFCS_CURVE_VBAT_DELTA_CNT 2
#define UFCS_CURVE_IBUS_DELTA_MA 50

#define UFCS_FG_TEMP_PROTECTION 800
#define UFCS_FG_TEMP_OV_CNT 5
#define UFCS_BTB_TEMP_OV_CNT 5
#define UFCS_MOS_TEMP_OV_CNT 5
#define UFCS_TBATT_OV_CNT 1
#define UFCS_DISCONNECT_IOUT_MIN 300
#define UFCS_DISCONNECT_IOUT_CNT 4
#define UFCS_ENALBE_CHECK_CNTS 3
#define UFCS_BTB_DIFF_OV_CNT 5

#define UFCS_FULL_COUNTS_HW 3
#define UFCS_FULL_COUNTS_COOL 6
#define UFCS_FULL_COUNTS_SW 6
#define UFCS_FULL_COUNTS_LOW_CURR 6

#define UFCS_IBAT_LOW_MIN 2000
#define UFCS_IBAT_LOW_CNT 4
#define UFCS_IBAT_HIGH_CNT 8

#define UFCS_VBAT_NUM 2
#define UFCS_R_IBUS_MIN 100
#define UFCS_R_VBUS_MIN 3000

/*ufcs aciton*/
#define UFCS_ACTION_START_DIFF_VOLT_V1 300
#define UFCS_ACTION_START_DIFF_VOLT_V2 600
#define UFCS_ACTION_CURR_MIN 800
#define UFCS_MASTER_ENALBE_CHECK_CNTS 10

#define UFCS_ACTION_START_DELAY 100
#define UFCS_ACTION_MOS_DELAY 50
#define UFCS_ACTION_VOLT_DELAY 500
#define UFCS_ACTION_CURR_DELAY 500
#define UFCS_ACTION_CHECK_DELAY 500
#define UFCS_ACTION_CHECK_ICURR_CNT 3

#define UFCS_RETRY_COUNT 1
#define UFCS_CHANGE_VBUS_ERR_TIMES 3

#define UFCS_RESISTENSE_CHEKC_CURR 4000
#define UFCS_CIRCUIT_PATH_CHEKC_CURR 4000
#define UFCS_CIRCUIT_PATH_CURR_MIN -1000
#define UFCS_RESISTENSE_WAIT_CNT 5
#define UFCS_RESISTENSE_CHEKC_CNT 10
#define UFCS_MOS0_SWITCH_IBUS_ENABLE_MIN 4000

#define UFCS_FASTCHG_TYPE_MASK 0xFF0
#define UFCS_FASTCHG_TYPE_SHIFT 4

#define UFCS_SMEM_RESERVED_BOOT_INFO_FOR_APPS 418
#define UFCS_GAUGE_AUTH_MSG_LEN 20
#define UFCS_WLS_AUTH_RANDOM_LEN 8
#define UFCS_WLS_AUTH_ENCODE_LEN 8
#define UFCS_AUTH_MSG_LEN 16
#define UFCS_ENCRYP_DATA_LEN 32
#define UFCS_WDT_TIMER 3000
#define UFCS_QOS_UP_VALUE 400
#define UFCS_STOP_DELAY_TIME 300

#define UFCS2SVOOC_VBUS_MIN 4000
#define UFCS2SVOOC_VBUS_MAX 6000
#define UFCS_DUMP_REG_CNT 0x10

typedef struct {
	int rst_k0_result;
	unsigned char rst_k0_msg[UFCS_GAUGE_AUTH_MSG_LEN];
	unsigned char rst_k0_rcv_msg[UFCS_GAUGE_AUTH_MSG_LEN];
	int rst_k1_result;
	unsigned char rst_k1_msg[UFCS_GAUGE_AUTH_MSG_LEN];
	unsigned char rst_k1_rcv_msg[UFCS_GAUGE_AUTH_MSG_LEN];
	unsigned char wls_random_num[UFCS_WLS_AUTH_RANDOM_LEN];
	unsigned char wls_encode_num[UFCS_WLS_AUTH_ENCODE_LEN];
	int rst_k2_result;
	unsigned char rst_k2_msg[UFCS_GAUGE_AUTH_MSG_LEN];
	unsigned char rst_k2_rcv_msg[UFCS_GAUGE_AUTH_MSG_LEN];
	unsigned char ufcs_k0_msg[UFCS_AUTH_MSG_LEN];
} oplus_ufcs_auth_info_type;

#define ROTLEFT(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x, 2) ^ ROTRIGHT(x, 13) ^ ROTRIGHT(x, 22))
#define EP1(x) (ROTRIGHT(x, 6) ^ ROTRIGHT(x, 11) ^ ROTRIGHT(x, 25))
#define SIG0(x) (ROTRIGHT(x, 7) ^ ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x, 17) ^ ROTRIGHT(x, 19) ^ ((x) >> 10))
#define UFCS_MIN(i, j) (((i) < (j)) ? (i) : (j))

enum { PROTOCOL_CHARGING_UNKNOWN = 0,
       PROTOCOL_CHARGING_PPS_OPLUS,
       PROTOCOL_CHARGING_PPS_THIRD,
       PROTOCOL_CHARGING_UFCS_THIRD,
       PROTOCOL_CHARGING_UFCS_OPLUS,
       PROTOCOL_CHARGING_SVOOC_OPLUS,
       PROTOCOL_CHARGING_SVOOC_THIRD,
       PROTOCOL_CHARGING_MAX = 100,
};

enum { UFCS_BYPASS_MODE = 1,
       UFCS_SC_MODE,
       UFCS_UNKNOWN_MODE = 100,
};

enum { UFCS_RESET_OUT,
       UFCS_RESET_FAIL,
       UFCS_RESET_ALLOW,
};

enum { UFCS_BAT_TEMP_NATURAL = 0,
       UFCS_BAT_TEMP_HIGH0,
       UFCS_BAT_TEMP_HIGH1,
       UFCS_BAT_TEMP_HIGH2,
       UFCS_BAT_TEMP_HIGH3,
       UFCS_BAT_TEMP_HIGH4,
       UFCS_BAT_TEMP_HIGH5,
       UFCS_BAT_TEMP_LOW0,
       UFCS_BAT_TEMP_LOW1,
       UFCS_BAT_TEMP_LOW2,
       UFCS_BAT_TEMP_LITTLE_COOL,
       UFCS_BAT_TEMP_LITTLE_COOL_LOW,
       UFCS_BAT_TEMP_COOL,
       UFCS_BAT_TEMP_NORMAL_LOW,
       UFCS_BAT_TEMP_NORMAL_HIGH,
       UFCS_BAT_TEMP_LITTLE_COLD,
       UFCS_BAT_TEMP_WARM,
       UFCS_BAT_TEMP_EXIT,
};

enum { UFCS_TEMP_RANGE_INIT = 0,
       UFCS_TEMP_RANGE_LITTLE_COLD,
       UFCS_TEMP_RANGE_COOL,
       UFCS_TEMP_RANGE_LITTLE_COOL,
       UFCS_TEMP_RANGE_NORMAL_LOW,
       UFCS_TEMP_RANGE_NORMAL_HIGH,
       UFCS_TEMP_RANGE_WARM,
       UFCS_TEMP_RANGE_NORMAL,
};

enum { OPLUS_UFCS_STATUS_START = 0,
       OPLUS_UFCS_STATUS_OPEN_MOS,
       OPLUS_UFCS_STATUS_VOLT_CHANGE,
       OPLUS_UFCS_STATUS_CUR_CHANGE,
       OPLUS_UFCS_STATUS_STABLE_CHECK,
       OPLUS_UFCS_STATUS_FFC,
};

enum { OPLUS_UFCS_VOLT_UPDATE_V1 = 100,
       OPLUS_UFCS_VOLT_UPDATE_V2 = 200,
       OPLUS_UFCS_VOLT_UPDATE_V3 = 500,
       OPLUS_UFCS_VOLT_UPDATE_V4 = 1000,
       OPLUS_UFCS_VOLT_UPDATE_V5 = 2000,
       OPLUS_UFCS_VOLT_UPDATE_V6 = 5000,
};

enum { OPLUS_UFCS_CURR_UPDATE_V1 = 50,
       OPLUS_UFCS_CURR_UPDATE_V2 = 100,
       OPLUS_UFCS_CURR_UPDATE_V3 = 200,
       OPLUS_UFCS_CURR_UPDATE_V4 = 300,
       OPLUS_UFCS_CURR_UPDATE_V5 = 400,
       OPLUS_UFCS_CURR_UPDATE_V6 = 500,
       OPLUS_UFCS_CURR_UPDATE_V7 = 1000,
       OPLUS_UFCS_CURR_UPDATE_V8 = 1500,
       OPLUS_UFCS_CURR_UPDATE_V9 = 2000,
       OPLUS_UFCS_CURR_UPDATE_V10 = 2500,
       OPLUS_UFCS_CURR_UPDATE_V11 = 3000,
};

enum ufcs_fastchg_type {
	UFCS_FASTCHG_TYPE_UNKOWN,
	UFCS_FASTCHG_TYPE_THIRD = 0x8211,
	UFCS_FASTCHG_TYPE_V1 = 0x4211,
	UFCS_FASTCHG_TYPE_V2,
	UFCS_FASTCHG_TYPE_V3,
	UFCS_FASTCHG_TYPE_V4,
	UFCS_FASTCHG_TYPE_V5,
	UFCS_FASTCHG_TYPE_V6 = 0x4961,
	UFCS_FASTCHG_TYPE_MAX,
	UFCS_FASTCHG_TYPE_OTHER,
};

enum ufcs_power_type {
	UFCS_POWER_TYPE_UNKOWN = 18,
	UFCS_POWER_TYPE_V0 = 33,
	UFCS_POWER_TYPE_V1 = 44,
	UFCS_POWER_TYPE_V2 = 55,
	UFCS_POWER_TYPE_V3 = 67,
	UFCS_POWER_TYPE_V4 = 80,
	UFCS_POWER_TYPE_V5 = 88,
	UFCS_POWER_TYPE_V6 = 100,
	UFCS_POWER_TYPE_V7 = 125,
	UFCS_POWER_TYPE_V8 = 150,
	UFCS_POWER_TYPE_V9 = 240,
	UFCS_POWER_TYPE_MAX = 999,
};

enum ufcs_emark_id {
	UFCS_EMARK_ID_V0,
	UFCS_EMARK_ID_V1 = 6,
	UFCS_EMARK_ID_V2 = 7,
	UFCS_EMARK_ID_V3 = 8,
	UFCS_EMARK_ID_V4 = 9,
	UFCS_EMARK_ID_V5 = 10,
	UFCS_EMARK_ID_V6 = 11,
	UFCS_EMARK_ID_MAX = 255,
};

enum ufcs_emark_power {
	UFCS_EMARK_POWER_DEFAULT = 3000,
	UFCS_EMARK_POWER_THIRD = 5000,
	UFCS_EMARK_POWER_V0 = 6500,
	UFCS_EMARK_POWER_V1 = 10000,
	UFCS_EMARK_POWER_V2 = 12000,
	UFCS_EMARK_POWER_V3 = 13700,
	UFCS_EMARK_POWER_V4 = 15000,
	UFCS_EMARK_POWER_V5 = 20000,
	UFCS_EMARK_POWER_V6 = 24000,
	UFCS_EMARK_POWER_MAX = 30000,
};

enum ufcs_emark_status {
	UFCS_EMARK_FAIL,
	UFCS_EMARK_SUCC_NORMAL,
	UFCS_EMARK_SUCC_FAST,
	UFCS_EMARK_OTHER,
};

enum ufcs_power_imax {
	UFCS_CURR_MAX_V0 = 3000,
	UFCS_CURR_MAX_V1 = 4000,
	UFCS_CURR_MAX_V2 = 5000,
	UFCS_CURR_MAX_V3 = 6100,
	UFCS_CURR_MAX_V4 = 7300,
	UFCS_CURR_MAX_V5 = 8300,
	UFCS_CURR_MAX_V6 = 10000,
	UFCS_CURR_MAX_V7 = 12000,
	UFCS_CURR_MAX_V8 = 15000,
	UFCS_CURR_MAX_V9 = 20000,
	UFCS_CURR_MAX_OTHER = 24000,
};

enum ufcs_power_vmax {
	UFCS_VOL_MAX_V0 = 5000,
	UFCS_VOL_MAX_V1 = 11000,
	UFCS_VOL_MAX_V2 = 20000,
	UFCS_VOL_MAX_V3 = 30000,
	UFCS_VOL_MAX_OTHER = 40000,
};

struct ufcs_power_param {
	enum ufcs_power_vmax vmax;
	enum ufcs_power_imax imax;
	enum ufcs_power_type power;
};

static struct ufcs_power_param ufcs_power_table[] = {
	{ UFCS_VOL_MAX_V1, UFCS_CURR_MAX_V0, UFCS_POWER_TYPE_V0 },
	{ UFCS_VOL_MAX_V1, UFCS_CURR_MAX_V1, UFCS_POWER_TYPE_V1 },
	{ UFCS_VOL_MAX_V1, UFCS_CURR_MAX_V2, UFCS_POWER_TYPE_V2 },
	{ UFCS_VOL_MAX_V1, UFCS_CURR_MAX_V3, UFCS_POWER_TYPE_V3 },
	{ UFCS_VOL_MAX_V1, UFCS_CURR_MAX_V4, UFCS_POWER_TYPE_V4 },
	{ UFCS_VOL_MAX_V1, UFCS_CURR_MAX_V5, UFCS_POWER_TYPE_V5 },
	{ UFCS_VOL_MAX_V1, UFCS_CURR_MAX_V6, UFCS_POWER_TYPE_V6 },
	{ UFCS_VOL_MAX_V1, UFCS_CURR_MAX_V7, UFCS_POWER_TYPE_V7 },
	{ UFCS_VOL_MAX_V1, UFCS_CURR_MAX_V8, UFCS_POWER_TYPE_V8 },
	{ UFCS_VOL_MAX_V1, UFCS_CURR_MAX_V9, UFCS_POWER_TYPE_V9 },
};

enum ufcs_powere_type {
	POWER_TYPE_UFCS,
	POWER_TYPE_PPS,
	POWER_TYPE_SVOOC,
	POWER_TYPE_OTHER,
};

enum { OPLUS_UFCS_CURRENT_LIMIT_0A = 0,
       OPLUS_UFCS_CURRENT_LIMIT_1A = 1000,
       OPLUS_UFCS_CURRENT_LIMIT_2A = 2000,
       OPLUS_UFCS_CURRENT_LIMIT_3A = 3000,
       OPLUS_UFCS_CURRENT_LIMIT_4A = 4000,
       OPLUS_UFCS_CURRENT_LIMIT_5A = 5000,
       OPLUS_UFCS_CURRENT_LIMIT_6A = 6000,
       OPLUS_UFCS_CURRENT_LIMIT_7A = 7000,
       OPLUS_UFCS_CURRENT_LIMIT_8A = 8000,
       OPLUS_UFCS_CURRENT_LIMIT_10A = 10000,
       OPLUS_UFCS_CURRENT_LIMIT_12A = 12000,
       OPLUS_UFCS_CURRENT_LIMIT_14A = 14000,
       OPLUS_UFCS_CURRENT_LIMIT_MAX = 15000,
};

enum { UFCS_NOT_SUPPORT = 0,
       UFCS_CHECKING,
       UFCS_CHARGERING,
       UFCS_CHARGE_END,
};

enum { UFCS_SUPPORT_NOT = 0,
       UFCS_SUPPORT_MCU,
       UFCS_SUPPORT_2CP,
       UFCS_SUPPORT_3CP,
       UFCS_SUPPORT_AP_VOOCPHY,
       UFCS_SUPPORT_ADSP_VOOCPHY,
       UFCS_SUPPORT_BIDIRECT_VOOCPHY,
};

enum { UFCS_BATT_CURVE_TEMP_RANGE_LITTLE_COLD,
       UFCS_BATT_CURVE_TEMP_RANGE_COOL,
       UFCS_BATT_CURVE_TEMP_RANGE_LITTLE_COOL,
       UFCS_BATT_CURVE_TEMP_RANGE_NORMAL_LOW,
       UFCS_BATT_CURVE_TEMP_RANGE_NORMAL_HIGH,
       UFCS_BATT_CURVE_TEMP_RANGE_WARM,
       UFCS_BATT_CURVE_TEMP_RANGE_MAX,
};

enum { UFCS_BATT_CURVE_SOC_RANGE_MIN,
       UFCS_BATT_CURVE_SOC_RANGE_LOW,
       UFCS_BATT_CURVE_SOC_RANGE_MID_LOW,
       UFCS_BATT_CURVE_SOC_RANGE_MID,
       UFCS_BATT_CURVE_SOC_RANGE_MID_HIGH,
       UFCS_BATT_CURVE_SOC_RANGE_HIGH,
       UFCS_BATT_CURVE_SOC_RANGE_MAX,
};

enum { UFCS_LOW_CURR_FULL_CURVE_TEMP_LITTLE_COOL,
       UFCS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW,
       UFCS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_HIGH,
       UFCS_LOW_CURR_FULL_CURVE_TEMP_MAX,
};

typedef enum {
	UFCS_STOP_VOTER_NONE = 0,
	UFCS_STOP_VOTER_FULL = (1 << 0),
	UFCS_STOP_VOTER_BTB_OVER = (1 << 1),
	UFCS_STOP_VOTER_MOS_OVER = (1 << 2),
	UFCS_STOP_VOTER_TBATT_OVER = (1 << 3),
	UFCS_STOP_VOTER_RESISTENSE_OVER = (1 << 4),
	UFCS_STOP_VOTER_IBAT_OVER = (1 << 5),
	UFCS_STOP_VOTER_DISCONNECT_OVER = (1 << 6),
	UFCS_STOP_VOTER_CHOOSE_CURVE = (1 << 7),
	UFCS_STOP_VOTER_TIME_OVER = (1 << 8),
	UFCS_STOP_VOTER_PDO_ERROR = (1 << 9),
	UFCS_STOP_VOTER_TYPE_ERROR = (1 << 10),
	UFCS_STOP_VOTER_MMI_TEST = (1 << 11),
	UFCS_STOP_VOTER_USB_TEMP = (1 << 12),
	UFCS_STOP_VOTER_TFG_OVER = (1 << 13),
	UFCS_STOP_VOTER_VBATT_DIFF = (1 << 14),
	UFCS_STOP_VOTER_SOURCE_EXIT = (1 << 15),
	UFCS_STOP_VOTER_STARTUP_FAIL = (1 << 16),
	UFCS_STOP_VOTER_CIRCUIT_SWITCH = (1 << 17),
	UFCS_STOP_VOTER_HARDRESET = (1 << 18),
	UFCS_STOP_VOTER_FLASH_LED = (1 << 19),
	UFCS_STOP_VOTER_OTHER_ABORMAL = (1 << 31),
} UFCS_STOP_VOTER;

struct ufcs_batt_curve {
	unsigned int target_vbus;
	unsigned int target_vbat;
	unsigned int target_ibus;
	bool exit;
	unsigned int target_time;
};

struct ufcs_batt_curves {
	struct ufcs_batt_curve batt_curves[BATT_UFCS_SYS_MAX];
	int batt_curve_num;
};

struct ufcs_batt_curves_soc {
	struct ufcs_batt_curves batt_curves_temp[UFCS_BATT_CURVE_TEMP_RANGE_MAX];
};

struct ufcs_full_curve {
	unsigned int iterm;
	unsigned int vterm;
	bool exit;
};

struct ufcs_full_curves_temp {
	struct ufcs_full_curve full_curves[FULL_UFCS_SYS_MAX];
	int full_curve_num;
};

struct oplus_ufcs_r_info {
	int r0;
	int r1;
	int r2;
	int r3;
	int r4;
	int r5;
	int r6;
};

struct oplus_ufcs_r_limit {
	int limit_exit_mohm;
	int limit_v1_mohm;
	int limit_v2_mohm;
	int limit_v3_mohm;
	int limit_v4_mohm;
};

struct oplus_ufcs_btb_limit {
	int limit_v0_diff;
	int limit_v1_diff;
	int limit_v2_diff;
	int limit_v3_diff;
	int limit_v4_diff;
};

struct oplus_ufcs_timer {
	struct timespec pdo_timer;
	struct timespec fastchg_timer;
	struct timespec temp_timer;
	struct timespec ibat_timer;
	struct timespec full_check_timer;
	struct timespec curve_check_timer;
	int batt_curve_time;
	int ufcs_timeout_time;
	bool set_pdo_flag;
	bool set_temp_flag;
	bool check_ibat_flag;
	bool full_check_flag;
	bool curve_check_flag;
	int work_delay;
};

struct ufcs_protection_counts {
	int cool_fw;
	int warm_fw;
	int sw_full;
	int hw_full;
	int low_curr_full;
	int ibat_low;
	int ibat_high;
	int btb_high;
	int mos_high;
	int btb_diff;
	int tbatt_over;
	int tfg_over;
	int output_low;
	int ibus_over;
};

struct ufcs_current_limits {
	/*ufcs current*/
	int current_batt_curve;
	int current_batt_temp;
	int current_bcc;
	int current_cool_down;
	int current_normal_cool_down;
	int cp_ibus_down;
	int cp_r_down;
	int btb_diff_down;
	int current_imax;
	int current_slow_chg;
	int full_1time_limit;
};

struct oplus_ufcs_limits {
	int default_ufcs_normal_high_temp;
	int default_ufcs_little_cool_temp;
	int default_ufcs_cool_temp;
	int default_ufcs_little_cold_temp;
	int default_ufcs_normal_low_temp;
	int ufcs_warm_allow_vol;
	int ufcs_warm_allow_soc;

	int ufcs_batt_over_low_temp;
	int ufcs_little_cold_temp;
	int ufcs_cool_temp;
	int ufcs_little_cool_temp;
	int ufcs_normal_low_temp;
	int ufcs_normal_high_temp;
	int ufcs_batt_over_high_temp;
	int ufcs_strategy_temp_num;

	int ufcs_strategy_soc_over_low;
	int ufcs_strategy_soc_min;
	int ufcs_strategy_soc_low;
	int ufcs_strategy_soc_mid_low;
	int ufcs_strategy_soc_mid;
	int ufcs_strategy_soc_mid_high;
	int ufcs_strategy_soc_high;
	int ufcs_strategy_soc_num;

	int ufcs_strategy_normal_current;
	int ufcs_strategy_batt_high_temp0;
	int ufcs_strategy_batt_high_temp1;
	int ufcs_strategy_batt_high_temp2;
	int ufcs_strategy_batt_low_temp2;
	int ufcs_strategy_batt_low_temp1;
	int ufcs_strategy_batt_low_temp0;
	int ufcs_strategy_high_current0;
	int ufcs_strategy_high_current1;
	int ufcs_strategy_high_current2;
	int ufcs_strategy_low_current2;
	int ufcs_strategy_low_current1;
	int ufcs_strategy_low_current0;

	int ufcs_low_curr_full_cool_temp;
	int ufcs_low_curr_full_little_cool_temp;
	int ufcs_low_curr_full_normal_low_temp;
	int ufcs_low_curr_full_normal_high_temp;

	int ufcs_over_high_or_low_current;
	int ufcs_strategy_change_count;
	int ufcs_full_cool_sw_vbat;
	int ufcs_full_normal_sw_vbat;
	int ufcs_full_normal_hw_vbat;
	int ufcs_full_warm_vbat;
	int ufcs_full_ffc_vbat;
	int ufcs_full_cool_sw_vbat_third;
	int ufcs_full_normal_sw_vbat_third;
	int ufcs_full_normal_hw_vbat_third;
	int ufcs_timeout_third;
	int ufcs_timeout_oplus;
	int ufcs_ibat_over_third;
	int ufcs_ibat_over_oplus;
};

/****************status flags**************************/
struct oplus_ufcs_error {
	int i2c_error;
	int rcv_hardreset;
	int hardware_error;
	int power_rdy_timeout;
};

struct oplus_ufcs_sink_info {
	int batt_temp;
	int usb_temp;
	int batt_volt;
	int batt_curr;
};

struct oplus_ufcs_src_cap {
	int cap_num;
	int max_volt[7];
	int min_volt[7];
	int max_curr[7];
	int min_curr[7];
};

struct oplus_ufcs_encry_array {
	u8 phone_random[16];
	u8 adapter_random[16];
	u8 adapter_encry_result[32];
};

struct oplus_emark_info {
	int imax;
	int vmax;
	int status;
	int hw_id;
};

#define CHARGER_POWER_NUM 7
struct oplus_charger_power {
	int imax[CHARGER_POWER_NUM];
	int vmax[CHARGER_POWER_NUM];
	int type[CHARGER_POWER_NUM];
	int num;
};

struct oplus_cable_info {
	int imax;
	int vmax;
	int resistence;
};

struct oplus_charger_info {
	struct oplus_emark_info emark;
	struct oplus_charger_power adapter;
	struct oplus_cable_info cable;
};

struct ufcs_charging_data {
	int batt_input_current;
	int ap_batt_volt;
	int ap_batt_current;
	int ap_batt_soc;
	int ap_batt_temperature;
	int ap_fg_temperature;
	int charger_output_volt;
	int charger_output_current;
	int current_adapter_max;
	int ap_input_current;

	int ap_input_volt;
	int cp_master_ibus;
	int cp_master_vac;
	int cp_master_vout;

	int slave_input_volt;
	int cp_slave_ibus;
	int cp_slave_vac;
	int cp_slave_vout;
};

struct oplus_ufcs_chip {
	struct device *dev;
	struct i2c_client *client;
	struct oplus_ufcs_operations *ops;

	struct power_supply *ufcs_batt_psy;
	struct delayed_work ufcs_stop_work;
	struct delayed_work update_ufcs_work;
	struct delayed_work ufcs_restart_work;
	struct delayed_work ufcs_uct_work;
	struct delayed_work modify_cpufeq_work;
	struct delayed_work ready_force2svooc_work;

	atomic_t ufcs_freq_state;

	/*curve data*/
	struct ufcs_batt_curves_soc batt_curves_third_soc[UFCS_BATT_CURVE_SOC_RANGE_MAX];
	struct ufcs_batt_curves_soc batt_curves_oplus_soc[UFCS_BATT_CURVE_SOC_RANGE_MAX];
	struct ufcs_batt_curves batt_curves;
	struct ufcs_full_curves_temp low_curr_full_curves_temp[UFCS_LOW_CURR_FULL_CURVE_TEMP_MAX];
	struct oplus_ufcs_limits limits;
	struct oplus_ufcs_timer timer;
	struct oplus_ufcs_r_info r_column[UFCS_R_AVG_NUM];
	struct oplus_ufcs_r_info r_avg;
	struct oplus_ufcs_r_info r_default;
	struct oplus_ufcs_r_limit r_limit;
	struct oplus_ufcs_btb_limit btb_limit;
	int rmos_mohm;
	/*ufcs charging data*/
	struct ufcs_protection_counts count;
	struct oplus_charger_info charger_info;
	int cp_mode;
	struct ufcs_charging_data data;
	struct ufcs_current_limits ilimit;

	/*action data*/
	int target_charger_volt;
	int target_charger_current;
	int ask_charger_volt;
	int ask_charger_current;
	int ask_charger_volt_last;
	int ask_charger_current_last;
	int target_charger_current_pre;
	bool pre_res_check;
	bool circuit_path_check;

	/*curve data*/
	int batt_curve_index;

	/*ufcs status*/
	int ufcs_fastchg_batt_temp_status;
	int ufcs_temp_cur_range;
	int ufcs_low_curr_full_temp_status;
	int ufcs_chging;
	int ufcs_status;
	int ufcs_stop_status;
	int ufcs_support_type;
	int ufcs_boot_delay_ms;
	int ufcs_fastchg_type;
	int ufcs_exit_pth;
	bool ufcs_bcc_support;
	bool ufcs_authentication;
	bool ufcs_ping;
	bool force_third_id;
	struct oplus_ufcs_error *ufcs_error;

	int ufcs_dummy_started;
	int ufcs_fastchg_started;
	int ufcs_force_check;
	bool ufcs_mos1_switch;
	bool mos_switch_enable;
	bool ufcs_flash_unsupport;
	int ufcs_cpu_up_freq;

	/*other*/
	int ufcs_iic_err;
	int ufcs_iic_err_num;
	int bcc_max_curr;
	int bcc_min_curr;
	int bcc_exit_curr;
	int ufcs_freq_mincore;
	int ufcs_freq_midcore;
	int ufcs_freq_maxcore;
	int ufcs_current_change_timeout;
	int chg_power;
	int ufcs_power;
	int ufcs_imax;
	int ufcs_vmax;
	int ufcs_exit_ms;

	int last_ufcs_power;
	int ufcs_keep_last_status;
	int ufcs_startup_retry_times;
	int ufcs_recover_cnt;

	u8 int_column[UFCS_DUMP_REG_CNT];
	u8 reg_dump[UFCS_DUMP_REG_CNT];

	struct mutex track_ufcs_err_lock;
	u32 debug_force_ufcs_err;
	bool ufcs_err_uploading;
	oplus_chg_track_trigger *ufcs_err_load_trigger;
	struct delayed_work ufcs_err_load_trigger_work;
};

struct oplus_ufcs_operations {
	bool (*ufcs_get_btb_temp_status)(void);
	bool (*ufcs_get_mos_temp_status)(void);
	int (*ufcs_get_batt_btb_tdiff)(void);
	int (*ufcs_mos_set)(int on);
	int (*ufcs_mos_get)(void);
	int (*ufcs_get_scap_imax)(int vbus_mv);
	void (*ufcs_cp_hardware_init)(void);
	int (*ufcs_cp_mode_init)(int mode);
	int (*ufcs_chip_enable)(void);
	int (*ufcs_chip_disable)(void);
	int (*ufcs_get_error)(struct oplus_ufcs_error *error);
	int (*ufcs_transfer_sink_info)(struct oplus_ufcs_sink_info *info);
	int (*ufcs_handshake)(void);
	int (*ufcs_ping)(void);
	int (*ufcs_get_src_cap)(struct oplus_ufcs_src_cap *buffer);
	int (*ufcs_request_pdo)(int volt, int curr);
	int (*ufcs_adapter_encryption)(struct oplus_ufcs_encry_array *array);
	int (*ufcs_get_max_curr)(int volt);
	int (*ufcs_get_src_info)(int *volt, int *curr);
	int (*ufcs_get_cable_info)(struct oplus_cable_info *buffer);
	int (*ufcs_get_emark_info)(struct oplus_emark_info *buffer);
	int (*ufcs_get_power_ability)(struct oplus_charger_power *buffer);
	u16 (*ufcs_get_adapter_id)(void);
	int (*ufcs_config_watchdog)(int time_ms);
	int (*ufcs_exit)(void);
	int (*ufcs_hard_reset)(void);
	bool (*ufcs_check_uct_mode)(void);
	bool (*ufcs_check_oplus_id)(void);
	bool (*ufcs_check_abnormal_id)(void);
	bool (*ufcs_quit_uct_mode)(void);
	bool (*ufcs_get_mos0_switch)(void);
	bool (*ufcs_set_mos0_switch)(bool enable);
	bool (*ufcs_get_mos1_switch)(void);
	bool (*ufcs_set_mos1_switch)(bool enable);

	int (*ufcs_get_cp_master_vbus)(void);
	int (*ufcs_get_cp_master_ibus)(void);
	int (*ufcs_get_cp_master_vac)(void);
	int (*ufcs_get_cp_master_vout)(void);
	int (*ufcs_get_cp_master_vbat)(void);

	int (*ufcs_event_handle)(void);
};

#ifndef OPLUS_UFCS_ENCRYPTION_H
#define OPLUS_UFCS_ENCRYPTION_H
#define UFCS_ENCRYPTION_BLOCK_SIZE 32
#define UFCS_ENCRYPTION_HASH_A 0x6a09e667
#define UFCS_ENCRYPTION_HASH_B 0xbb67ae85
#define UFCS_ENCRYPTION_HASH_C 0x3c6ef372
#define UFCS_ENCRYPTION_HASH_D 0xa54ff53a
#define UFCS_ENCRYPTION_HASH_E 0x510e527f
#define UFCS_ENCRYPTION_HASH_F 0x9b05688c
#define UFCS_ENCRYPTION_HASH_G 0x1f83d9ab
#define UFCS_ENCRYPTION_HASH_H 0x5be0cd19

static unsigned char ufcs_source_data[64] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x80,
};

static unsigned int ufcs_verify[64] = { 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
					0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
					0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
					0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
					0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
					0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
					0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
					0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
					0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
					0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
					0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
#endif /*OPLUS_UFCS_ENCRYPTION_H*/

int oplus_ufcs_init(struct oplus_ufcs_chip *chip);
void oplus_ufcs_variables_reset(int status);
int oplus_ufcs_get_chg_status(void);
int oplus_ufcs_set_chg_status(int status);
bool oplus_ufcs_check_oplus_id(void);
void oplus_ufcs_set_third_id(bool status);
bool oplus_ufcs_switch_ufcs_check(void);
void oplus_ufcs_source_exit(void);
void oplus_ufcs_stop_usb_temp(void);
void oplus_ufcs_set_vbatt_diff(bool diff);
bool oplus_ufcs_get_btb_temp_over(void);
int oplus_ufcs_get_protocol_status(void);
bool oplus_is_ufcs_charging(void);
bool oplus_ufcs_show_vooc(void);
int oplus_ufcs_get_power(void);
bool oplus_ufcs_is_allow_real(void);
int oplus_ufcs_pd_exit(void);
int oplus_ufcs_get_ffc_vth(void);
int oplus_ufcs_get_ffc_started(void);
int oplus_ufcs_set_ffc_started(bool status);
int oplus_ufcs_get_ufcs_mos_started(void);
int oplus_ufcs_get_stop_status(void);
void oplus_ufcs_stop_flash_led(bool on);
bool oplus_ufcs_get_ufcs_dummy_started(void);
void oplus_ufcs_set_ufcs_dummy_started(bool enable);
bool oplus_ufcs_get_ufcs_fastchg_started(void);
bool oplus_ufcs_get_force_check_status(void);
void oplus_ufcs_print_log(void);
int oplus_ufcs_get_support_type(void);
void oplus_ufcs_clear_dbg_info(void);
void oplus_ufcs_restart_ufcs_check(void);
int oplus_ufcs_get_fastchg_type(void);
void oplus_ufcs_set_bcc_current(int val);
int oplus_ufcs_get_bcc_max_curr(void);
int oplus_ufcs_get_bcc_min_curr(void);
int oplus_ufcs_get_bcc_exit_curr(void);
bool oplus_ufcs_bcc_get_temp_range(void);
void oplus_ufcs_init_key(void);
int oplus_ufcs_get_batt_curve_current(void);
int oplus_ufcs_get_current_cool_down(void);
int oplus_ufcs_get_current_normal_cool_down(void);
int oplus_chg_track_pack_ufcs_emark_info(u8 *ufcs_info, int lenth);
void oplus_ufcs_clear_startup_retry(void);
bool oplus_ufcs_get_last_charging_status(void);
int oplus_ufcs_get_last_protocol_status(void);
int oplus_ufcs_get_last_power(void);
void oplus_ufcs_clear_last_charging_status(void);
int oplus_ufcs_event_handle(void);
int oplus_ufcs_adapter_id_to_power(void);

#endif /*_OPLUS_UFCS_H_*/
