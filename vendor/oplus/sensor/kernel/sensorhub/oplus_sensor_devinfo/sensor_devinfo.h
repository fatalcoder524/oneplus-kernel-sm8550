/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef SENSOR_DEVINFO_H
#define SENSOR_DEVINFO_H
#include "sensor_interface.h"
typedef struct {/*old struct*/
	int prox_ne;
	int prox_sw;
	int ges_offset_n;
	int ges_offset_e;
	int ges_offset_s;
	int ges_offset_w;
	int gsensor_data[3];
	int prox_offset_l;
	int prox_offset_h;
	int gain_als;
	int gyro_data[3];
	union {
		struct {
			int ps0_offset;
			int ps0_value;
			int ps0_distance_delta;
			int ps1_offset;
			int ps1_value;
			int ps1_distance_delta;
		};
		struct {
			int ps_low_offset;
			int ps_normal_offset;
			int ps_low;
			int ps_normal;
			int dump1;
			int dump2;
		};
		int ps_data[6];
	};
	int nReserve[43];
} sensor_cali_file_v1_t;

typedef struct {/*new struct*/
	union {
		struct {
			int ps0_offset;
			int ps0_mean;
			int ps0_distance_delta;
			int ps1_offset;
			int ps1_mean;
			int ps1_distance_delta;
			int double_ps_reserve[4];
		};
		struct {
			int ps_low_offset;
			int ps_normal_offset;
			int ps_low;
			int ps_normal;
			int underlcd_ps_reserve[6];
		};
		int ps_data[10];
	};
	int als_gain;
	int als_reserve[9];
	int gsensor_data[3];
	int gsensor_reserve[7];
	int gyro_data[3];
	int gyro_reserve[7];
	int cct_cali_data[6];
	int cct_reserve[4];
	int rear_als_gain;
	int rear_als_reserve[9];
	int nReserve[14];
} sensor_cali_file_v2_t;

enum panel_id{
	SAMSUNG = 1,
	BOE,
	TIANMA,
	NT36672C,
	HX83112F,
	TM,
	P_3,
	P_B,
	TD4377,
	TXD_ILI9883C,
	DS_ILI9883C,
	ILI9883C,
	FT8057P,
	P_7,
	ILI7807S,
	DJN,
	PANEL_NUM
};

struct panel_node {
	enum panel_id id;
	char *lcm_name;
};

enum {
	OPLUS_ACTION_RW_REGISTER = 110,
	OPLUS_ACTION_SCP_SYNC_UTC,
	OPLUS_ACTION_SCP_DEVICE_INFO,
	OPLUS_ACTION_SCP_SYNC_CALI_DATA,
	OPLUS_ACTION_CONFIG_REG,
	OPLUS_ACTION_SELF_TEST,
	OPLUS_ACTION_SET_FACTORY_MODE,
	OPLUS_ACTION_SET_LCD_INFO,
};

enum sensor_mode {
	PS_FACTORY_MODE = 1,
	PS_NORMAL_MODE,
	GSENSOR_FACTORY_MODE,
	GSENSOR_NORMAL_MODE,
	REAR_CCT_FACTORY_MODE,
	REAR_CCT_NORMAL_MODE,
	CCT_FACTORY_MODE,
	CCT_NORMAL_MODE,
	CCT_CLOCK_MODE,
	PS_LP_MODE,
	PS_HP_MODE,
	HINGE_DETECT_FACTORY_MODE,
	HINGE_DETECT_NORMAL_MODE,
	REAR_SPECTRUM_NORMAL_MODE,
	REAR_SPECTRUM_FACTORY_512_GAIN,
	REAR_SPECTRUM_FACTORY_2048_GAIN,
	REAR_PS_FACTORY_MODE,
	REAR_PS_NORMAL_MODE,
	LEAR_CALI_MODE,
	LEAK_CALI_NORMAL_MODE,
	CCT_FACTORY_512_GAIN,
	CCT_FACTORY_1024_GAIN,
	CCT_FACTORY_2048_GAIN,
	CCT_FACTORY_GAIN_NORMAL,
};

enum light_sensor_type {
	NORMAL_LIGHT_TYPE = 1,
	UNDER_SCREEN_LIGHT_TYPE,
	NORMAL_NEED_COMPENSATION,
};

enum {
	CCT_NORMAL = 0x01,
	CCT_WISE = 0x02,
};

enum sensor_id {
	OPLUS_ACCEL = 1,
	OPLUS_GYRO,
	OPLUS_MAG,
	OPLUS_LIGHT,
	OPLUS_PROXIMITY,
	OPLUS_SAR,
	OPLUS_CCT,
	OPLUS_BAROMETER,
	LAST_SENOSR,
	OPLUS_PICK_UP = LAST_SENOSR + 1,
	OPLUS_LUX_LOD,
	OPLUS_ALSPS_ARCH,
};

extern int get_sensor_parameter(struct cali_data *data);
extern void update_sensor_parameter(void);
extern bool is_sensor_available(char *name);
extern int oplus_send_selftest_cmd_to_hub(int sensorType, void *testresult);
extern int oplus_send_factory_mode_cmd_to_hub(int sensorType, int mode, void *result);
extern int get_light_sensor_type(void);
extern bool is_support_new_arch_func(void);

#endif /*SENSOR_DEVINFO_H*/
