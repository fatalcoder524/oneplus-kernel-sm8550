/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/of_fdt.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/rtc.h>
#include <linux/libfdt.h>
#include <linux/suspend.h>
#include "sensor_devinfo.h"
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include "hf_sensor_type.h"
#include <linux/gpio.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
#ifndef PDE_DATA
#define PDE_DATA pde_data
#endif
#endif

#define UINT2Ptr(n)	 (uint32_t *)(n)
#define Ptr2UINT32(p)   (*((uint32_t*)p))

int register_lcdinfo_notifier(struct notifier_block *nb);
int unregister_lcdinfo_notifier(struct notifier_block *nb);

struct proc_node {
	char *node_name;
	uint32_t id;
};

enum {
	RED_MAX_LUX = 0x01,
	GREEN_MAX_LUX,
	BLUE_MAX_LUX,
	WHITE_MAX_LUX,
	CALI_COE,
	ROW_COE,
};

struct oplus_als_cali_data {
	int red_max_lux;
	int green_max_lux;
	int blue_max_lux;
	int white_max_lux;
	int cali_coe;
	int row_coe;
	struct proc_dir_entry		 *proc_oplus_als;
};

static struct panel_node g_panel_node[PANEL_NUM] = {
	{
		.id = SAMSUNG,
		.lcm_name = "samsung",
	},
	{
		.id = BOE,
		.lcm_name = "boe",
	},
	{
		.id = TIANMA,
		.lcm_name = "tianma",
	},
	{
		.id = NT36672C,
		.lcm_name = "nt36672c",
	},
	{
		.id = HX83112F,
		.lcm_name = "hx83112f",
	},
	{
		.id = TM,
		.lcm_name = "tm",
	},
	{
		.id = P_3,
		.lcm_name = "P_3",
	},
	{
		.id = P_B,
		.lcm_name = "P_B",
	},
	{
		.id = P_7,
		.lcm_name = "P_7",
	},
	/* add from here */
	{
		.id = TD4377,
		.lcm_name = "td4377",
	},
	{
		.id = TXD_ILI9883C,
		.lcm_name = "txd_ili9883c",
	},
	{
		.id = DS_ILI9883C,
		.lcm_name = "ds_ili9883c",
	},
	{
		.id = ILI9883C,
		.lcm_name = "ili9883c",
	},
	{
		.id = FT8057P,
		.lcm_name = "ft8057p",
	},
	{
		.id = ILI7807S,
		.lcm_name = "ili7807s",
	},
	{
		.id = DJN,
		.lcm_name = "djn",
	},
	{
		.id = PANEL_NUM,
		.lcm_name = "panel_num",
	}
};
enum {
	IS_SUPPROT_HWCALI = 1,
	IS_IN_FACTORY_MODE,
	IS_SUPPORT_NEW_ARCH,
	GYRO_CALI_VERSION,
	ACC_CALI_RANGE,
	DO_MAG_SELFTEST,
	SAR_REG_ADDRESS,
	SAR_REG_VALUE,
	GOLD_REAR_CCT_3K,
	GOLD_REAR_CCT_6K,
	GOLD_ALS_FACTOR,
	GOLD_REAR_ALS_FACTOR,
	GOLD_REAR_CCT_FACTOR,
	GOLD_CCT_3K,
	GOLD_CCT_6K,
	GOLD_CCT_FACTOR,
	CCT_TYPE,
	GYRO_CALI_RANGE,
	GOLD_REAR_SPECTRUM_3K,
	GOLD_REAR_SPECTRUM_6K,
	REAR_SPECTRUM_GAIN,
	GOLD_CCT_CHANNELS,
	ACC_GYRO_NAME,
	REAR_CCT_NAME,
	GOLD_REAR_SPECTRUM_FACTOR_512GAIN,
	GOLD_REAR_SPECTRUM_FACTOR_2048GAIN,
	SUPPORT_PANEL,
	FOLD_HALL_LIMIT,
	CCT_GAIN_CALI,
	MULTI_OFFSET_LEVEL,
	MULTI_OFFSET_COEF,
};

#define ID_REAR_ALS     97
#define ID_REAR_CCT     98
#define SOURCE_NUM      6
#define MAX_SIZE        64

static struct delayed_work parameter_work;
static struct delayed_work utc_work;
static struct delayed_work lcdinfo_work;
static struct sensorhub_interface *si;
struct device_node* parent_node;

struct cali_data* g_cali_data = NULL;

static char para_buf[3][128] = {"", "", ""};
static bool is_support_new_arch = false;
static struct oplus_als_cali_data *gdata = NULL;
static struct proc_dir_entry *sensor_proc_dir = NULL;
static int gyro_cali_version = 1;
static int cct_type = CCT_NORMAL;
static int support_panel = 0;
static int g_reg_address = 0;
static int g_reg_value = 0;
static char acc_cali_range[64] = {0};
static int gyro_cali_range = 200;
static char gold_rear_cct_3k[64] = {0};
static char gold_rear_cct_6k[64] = {0};
static char gold_rear_cct_factor[64] = {0};
static char gold_cct_3k[64] = {0};
static char gold_cct_6k[64] = {0};
static char gold_cct_factor[64] = {0};
static uint32_t gold_als_factor = 1001;
static uint32_t gold_rear_als_factor = 1001;
static uint32_t lb_bri_max = 1500;
static char gold_rear_spectrum_3k[84] = {0};
static char gold_rear_spectrum_6k[84] = {0};
static int gold_cct_channels = 4;
static char *g_acc_gyro_name = NULL;
static char *g_rear_cct_name = NULL;
static char g_gold_rear_spectrum_factor_512gain[70] = {0};
static char g_gold_rear_spectrum_factor_2048gain[70] = {0};
static char g_fold_hall_limit[70] = {0};
static int g_cct_gain_cali = 0;
static uint32_t g_multi_offset_level = 0;
static char g_multi_offset_coef[16] = {0};
atomic_t utc_suspend;

struct als_info g_als_info;

bool is_support_new_arch_func(void)
{
	return is_support_new_arch;
}

__attribute__((weak)) unsigned int get_project(void) {
	return -1;
}

int oplus_get_dts_feature(struct device_node *p_node, char *node_name, char *feature_name, uint32_t *input)
{
	int index = 0;
	int len = 0;
	int ret = 0;
	char node[MAX_SIZE] = {0};
	char *device_name = NULL;
	struct device_node *ch_node = NULL;

	for (index = 1; index <= SOURCE_NUM; index++) {
		snprintf(node, MAX_SIZE, "%s_%d", node_name, index);
		ch_node = of_get_child_by_name(p_node, node);
		if (ch_node == NULL) {
			DEVINFO_LOG("get %s fail", node);
			return -1;
		}

		ret = of_property_read_string(ch_node, "device_name", (const char**)&device_name);
		if (device_name == NULL || ret < 0) {
			DEVINFO_LOG("get device_name prop fail \n");
			return -1;
		} else {
			DEVINFO_LOG("device_name = %s", device_name);
		}
		if (si && si->is_sensor_available && si->is_sensor_available(device_name)) {
			len = of_property_count_elems_of_size(ch_node, feature_name, sizeof(uint32_t));
			if (len < 0) {
				DEVINFO_LOG("get feature_name:%s fail\n", feature_name);
				return -1;
			} else {
				ret = of_property_read_variable_u32_array(ch_node, feature_name, input, len, 0);
				if (ret != len) {
					DEVINFO_LOG("error:read feature_name:%s %d element but length is %d\n", feature_name, ret, len);
					return -1;
				} else {
					DEVINFO_LOG("read %s:%s, len %d, successful\n", device_name, feature_name, len);
					return ret;
				}
			}
			break;
		}
	}
	DEVINFO_LOG("oplus_get_dts_feature %s:%s,error\n", node_name, feature_name);
	return -1;
}

static void get_gold_cct_channels(struct device_node *ch_node)
{
	int ret = 0;
	ret = of_property_read_u32(ch_node, "gold_cct_channels", &gold_cct_channels);
	if (ret < 0) {
		DEVINFO_LOG("get gold_cct_channels fail");
		gold_cct_channels = 4;
	}
	DEVINFO_LOG("gold_cct_channels %d", gold_cct_channels);
}

static void is_support_lb_algo(struct device_node *ch_node)
{
	int ret = 0;
	int lb_algo = 0;
	ret = of_property_read_u32(ch_node, "use_lb_algo", &lb_algo);
	if (ret < 0) {
		DEVINFO_LOG("get use_lb_algo fail");
	}
	g_als_info.use_lb_algo = (lb_algo == 1) ? true : false;
	DEVINFO_LOG("support lb algo %d", g_als_info.use_lb_algo);
}

static void get_lb_max_brightness(struct device_node *ch_node)
{
	int ret = 0;
	ret = of_property_read_u32(ch_node, "lb_bri_max", &lb_bri_max);
	if (ret < 0) {
		lb_bri_max = 1500;
		DEVINFO_LOG("get lb_bri_max fail");
	}
	DEVINFO_LOG("lb_bri_max %d\n", lb_bri_max);
}

static void get_new_arch_info(struct device_node *ch_node)
{
	int is_support = 0;
	int ret = 0;

	ret = of_property_read_u32(ch_node, "new-arch-supported", &is_support);
	if (ret < 0) {
		 DEVINFO_LOG("get new-arch-supported fail");
		 return;
	}
	DEVINFO_LOG("get new-arch-supported = %d", is_support);

	is_support_new_arch = is_support;
}

static void transfer_lcdinfo_to_scp(struct work_struct *dwork)
{
	int ret = 0;
	if (si && si->send_lcdinfo) {
		ret = si->send_lcdinfo(&g_als_info);
	}
	if (ret < 0) {
		DEVINFO_LOG("send lcd info error\n");
	}
}

static void sync_utc2scp_work(struct work_struct *dwork)
{
	int ret = 0;
	if (atomic_read(&utc_suspend) == 1) {
		DEVINFO_LOG("Will suspend, stop sync utc \n");
		return;
	}
	if (!si || !si->send_utc_time) {
		DEVINFO_LOG("no utc interface\n");
		return;
	}
	ret = si->send_utc_time();

	if (ret < 0) {
		DEVINFO_LOG("sync utc error\n");
		return;
	}

	schedule_delayed_work(&utc_work, msecs_to_jiffies(2000));
}

static void get_accgyro_cali_version(void)
{
	uint32_t acc_thrd[3];
	int ret = 0;

	ret = oplus_get_dts_feature(parent_node, "gsensor", "gyro_cali_version", &gyro_cali_version);
	if (ret < 0) {
		gyro_cali_version = 1;
	}
	DEVINFO_LOG("gyro_cali_version = %d", gyro_cali_version);

	ret = oplus_get_dts_feature(parent_node, "gsensor", "gyro_cali_range", &gyro_cali_range);
	if (ret < 0) {
		gyro_cali_range = 200;
	}
	DEVINFO_LOG("gyro range  [%d]", gyro_cali_range);

	ret = oplus_get_dts_feature(parent_node, "gsensor", "acc_cali_range", acc_thrd);
	if (ret < 0) {
		return;
	} else {
		DEVINFO_LOG("acc range x y z [%u, %u, %u]", acc_thrd[0], acc_thrd[1], acc_thrd[2]);
		sprintf(acc_cali_range, "%u %u %u", acc_thrd[0], acc_thrd[1], acc_thrd[2]);
	}
}

static struct panel_node * find_panel(enum panel_id id)
{
	int index = 0;
	for (index = 0; index < PANEL_NUM; ++index) {
		if (g_panel_node[index].id == id) {
			return &g_panel_node[index];
		}
	}
	return NULL;
}
static void choose_special_gold_cct_value(int support_panel)
{
	int ret = 0;
	uint32_t gold_cct[6] = {0};/*r,g,b,c,w,f*/
	char als_panel[MAX_SIZE] = {0};
	struct panel_node *p_node = NULL;

	p_node = find_panel(support_panel);
	if (p_node == NULL) {
		DEVINFO_LOG("find panel fail\n");
		return;
	}

	snprintf(als_panel, MAX_SIZE, "als_panel_%s", p_node->lcm_name);
	/*get 3000k gold ch*/
	ret = oplus_get_dts_feature(parent_node, als_panel, "gold_cct_3k", gold_cct);
	if (ret < 0) {
		DEVINFO_LOG("gold_cct_3k fail\n");
		return;
	} else {
		DEVINFO_LOG("gold_cct_3k [%u, %u, %u, %u, %u, %u]",
			gold_cct[0], gold_cct[1], gold_cct[2],
			gold_cct[3], gold_cct[4], gold_cct[5]);

		sprintf(gold_cct_3k, "%u %u %u %u %u %u",
			 gold_cct[0], gold_cct[1], gold_cct[2],
			 gold_cct[3], gold_cct[4], gold_cct[5]);
	}

	/*get 6000k gold ch*/
	memset(gold_cct, 0, sizeof(gold_cct));
	ret = oplus_get_dts_feature(parent_node, als_panel, "gold_cct_6k", gold_cct);
	if (ret < 0) {
		DEVINFO_LOG("gold_cct_6k fail\n");
		return;
	} else {
		DEVINFO_LOG("gold_cct_6k [%u, %u, %u, %u, %u, %u]",
			gold_cct[0], gold_cct[1], gold_cct[2],
			gold_cct[3], gold_cct[4], gold_cct[5]);

		sprintf(gold_cct_6k, "%u %u %u %u %u %u",
			 gold_cct[0], gold_cct[1], gold_cct[2],
			 gold_cct[3], gold_cct[4], gold_cct[5]);
	}
}

static void get_front_cct_feature(void)
{
	int ret = 0;
	uint32_t gold_cct[6] = {0};/*r,g,b,c,w,f*/

	ret = oplus_get_dts_feature(parent_node, "light", "cct_type", &cct_type);
	if (ret < 0) {
		cct_type = CCT_NORMAL;
	}
	DEVINFO_LOG("cct_type = %d", cct_type);

	ret = oplus_get_dts_feature(parent_node, "light", "support_panel", &support_panel);
	if (ret < 0) {
		support_panel = 0; /* not support 2 panel */
	}
	DEVINFO_LOG("support_panel = %d", support_panel);

	if (support_panel > 0) {
		choose_special_gold_cct_value(support_panel);
	} else {
		/*get 3000k gold ch*/
		ret = oplus_get_dts_feature(parent_node, "light", "gold_cct_3k", gold_cct);
		if (ret < 0) {
			DEVINFO_LOG("gold_cct_3k fail\n");
			return;
		} else {
			DEVINFO_LOG("gold_cct_3k [%u, %u, %u, %u, %u, %u]",
				gold_cct[0], gold_cct[1], gold_cct[2],
				gold_cct[3], gold_cct[4], gold_cct[5]);

			sprintf(gold_cct_3k, "%u %u %u %u %u %u",
				 gold_cct[0], gold_cct[1], gold_cct[2],
				 gold_cct[3], gold_cct[4], gold_cct[5]);
		}

		/*get 6000k gold ch*/
		memset(gold_cct, 0, sizeof(gold_cct));
		ret = oplus_get_dts_feature(parent_node, "light", "gold_cct_6k", gold_cct);
		if (ret < 0) {
			DEVINFO_LOG("gold_cct_6k fail\n");
			return;
		} else {
			DEVINFO_LOG("gold_cct_6k [%u, %u, %u, %u, %u, %u]",
				gold_cct[0], gold_cct[1], gold_cct[2],
				gold_cct[3], gold_cct[4], gold_cct[5]);

			sprintf(gold_cct_6k, "%u %u %u %u %u %u",
				 gold_cct[0], gold_cct[1], gold_cct[2],
				 gold_cct[3], gold_cct[4], gold_cct[5]);
		}
	}

	/* get gold_cct_factor */
	memset(gold_cct, 0, sizeof(gold_cct));
	ret = oplus_get_dts_feature(parent_node, "light", "gold_cct_factor", gold_cct);
	if (ret < 0) {
		DEVINFO_LOG("gold_cct_factor fail, use default\n");
		sprintf(gold_cct_factor, "%d %d %d %d %d %d", 1001, 1001, 1001, 1001, 1001, 1001);
		return;
	} else {
		DEVINFO_LOG("gold_cct_factor [%u, %u, %u, %u, %u, %u]",
			gold_cct[0], gold_cct[1], gold_cct[2],
			gold_cct[3], gold_cct[4], gold_cct[5]);

		sprintf(gold_cct_factor, "%u %u %u %u %u %u",
			 gold_cct[0], gold_cct[1], gold_cct[2],
			 gold_cct[3], gold_cct[4], gold_cct[5]);
	}

	ret = oplus_get_dts_feature(parent_node, "light", "need_gain_cali", &g_cct_gain_cali);
	if (ret < 0) {
		DEVINFO_LOG("g_cct_gain_cali fail, use default\n");
	}
}

struct proc_node als_cali_file[] = {
	{"red_max_lux", RED_MAX_LUX},
	{"green_max_lux", GREEN_MAX_LUX},
	{"blue_max_lux", BLUE_MAX_LUX},
	{"white_max_lux", WHITE_MAX_LUX},
	{"cali_coe", CALI_COE},
	{"row_coe", ROW_COE},
};

static int als_cali_read_func(struct seq_file *s, void *v)
{
	void *p = s->private;

	DEVINFO_LOG("Ptr2UINT32(p) = %d \n", Ptr2UINT32(p));
	switch (Ptr2UINT32(p)) {
	case RED_MAX_LUX:
		seq_printf(s, "%d", gdata->red_max_lux);
		break;
	case GREEN_MAX_LUX:
		seq_printf(s, "%d", gdata->green_max_lux);
		break;
	case BLUE_MAX_LUX:
		seq_printf(s, "%d", gdata->blue_max_lux);
		break;
	case WHITE_MAX_LUX:
		seq_printf(s, "%d", gdata->white_max_lux);
		break;
	case CALI_COE:
		seq_printf(s, "%d", gdata->cali_coe);
		break;
	case ROW_COE:
		seq_printf(s, "%d", gdata->row_coe);
		break;
	default:
		seq_printf(s, "not support\n");
	}
	return 0;
}

static ssize_t als_cali_write(struct file *filp, const char *ubuf, size_t cnt, loff_t *data)
{
	char buf[64] = {0};
	void *buf_str = buf;
	int val = 0;
	int ret = 0;
	struct seq_file *s = filp->private_data;
	void *p = s->private;
	int node = Ptr2UINT32(p);

	if (cnt >= sizeof(buf)) {
		return -EINVAL;
	} else {
		if (copy_from_user(buf_str, ubuf, cnt)) {
			return -EFAULT;
		}
	}

	ret = kstrtoint(buf, 0, &val);
	DEVINFO_LOG("node1 = %d,val = %d\n", node, val);

	switch (node) {
	case RED_MAX_LUX:
		gdata->red_max_lux = val;
		break;
	case GREEN_MAX_LUX:
		gdata->green_max_lux = val;
		break;
	case BLUE_MAX_LUX:
		gdata->blue_max_lux = val;
		break;
	case WHITE_MAX_LUX:
		gdata->white_max_lux = val;
		break;
	case CALI_COE:
		gdata->cali_coe = val;
		break;
	case ROW_COE:
		gdata->row_coe = val;
		break;
	default:
		DEVINFO_LOG("ivalid node type\n");
	}
	return cnt;
}


static int als_cali_open(struct inode *inode, struct file *file)
{
	return single_open(file, als_cali_read_func, PDE_DATA(inode));
}

static const struct proc_ops als_cali_para_fops = {
	.proc_open  = als_cali_open,
	.proc_write = als_cali_write,
	.proc_read  = seq_read,
	.proc_release = single_release,
	.proc_lseek = default_llseek,
};


static void oplus_als_cali_data_init(void)
{
	struct proc_dir_entry *pentry;
	struct oplus_als_cali_data *data = NULL;
	int ret;
	int i;

	DEVINFO_LOG("call \n");
	if (gdata) {
		DEVINFO_LOG("has been inited\n");
		return;
	}

	data = kzalloc(sizeof(struct oplus_als_cali_data), GFP_KERNEL);
	if (data == NULL) {
		DEVINFO_LOG("kzalloc fail\n");
		return;
	}
	gdata = data;

	ret = oplus_get_dts_feature(parent_node, "light", "als_ratio" , &gdata->row_coe);
	if (ret < 0) {
		gdata->row_coe = 1000;
	}
	DEVINFO_LOG("row_coe = %d\n", gdata->row_coe);

	ret = oplus_get_dts_feature(parent_node, "light", "gold_als_factor", &gold_als_factor);
	if (ret < 0) {
		gold_als_factor = 1001;
	}
	DEVINFO_LOG("gold_als_factor = %d\n", gold_als_factor);

	ret = oplus_get_dts_feature(parent_node, "rear_cct", "gold_rear_als_factor", &gold_rear_als_factor);
	if (ret < 0) {
		gold_rear_als_factor = 1001;
	}
	DEVINFO_LOG("gold_rear_als_factor = %d\n", gold_rear_als_factor);

	sensor_proc_dir = proc_mkdir("sensor", NULL);
	if (!sensor_proc_dir) {
		DEVINFO_LOG("can't create proc_sensor proc\n");
		return;
	}

	gdata->proc_oplus_als = proc_mkdir("als_cali", sensor_proc_dir);
	if (!gdata->proc_oplus_als) {
		DEVINFO_LOG("can't create als cali proc\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(als_cali_file); i++) {
		if (als_cali_file[i].node_name) {
			pentry = proc_create_data(als_cali_file[i].node_name, S_IRUGO | S_IWUGO,
						 gdata->proc_oplus_als, &als_cali_para_fops,
						 UINT2Ptr(&als_cali_file[i].id));
			if (!pentry) {
				DEVINFO_LOG("can't create %s\n", als_cali_file[i].node_name);
				ret = -ENOMEM;
			}
		}
	}
}

static void get_gold_rear_spectrum(void)
{
	int ret = 0;
	uint32_t gold_spectrum[14] = {0};

	/*get 3000k gold ch*/
	ret = oplus_get_dts_feature(parent_node, "rear_cct", "gold_rear_spectrum_3k", gold_spectrum);
	if (ret < 0) {
		DEVINFO_LOG("gold_rear_spectrum_3k fail\n");
		return;
	} else {
		DEVINFO_LOG("gold_rear_spectrum_3k [%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u]",
			gold_spectrum[0], gold_spectrum[1], gold_spectrum[2], gold_spectrum[3],
			gold_spectrum[4], gold_spectrum[5], gold_spectrum[6], gold_spectrum[7],
			gold_spectrum[8], gold_spectrum[9], gold_spectrum[10], gold_spectrum[11],
			gold_spectrum[12], gold_spectrum[13]);

		sprintf(gold_rear_spectrum_3k, "%u %u %u %u %u %u %u %u %u %u %u %u %u %u",
			gold_spectrum[0], gold_spectrum[1], gold_spectrum[2], gold_spectrum[3],
			gold_spectrum[4], gold_spectrum[5], gold_spectrum[6], gold_spectrum[7],
			gold_spectrum[8], gold_spectrum[9], gold_spectrum[10], gold_spectrum[11],
			gold_spectrum[12], gold_spectrum[13]);
	}

	/*get 6000k gold ch*/
	memset(gold_spectrum, 0, sizeof(gold_spectrum));
	ret = oplus_get_dts_feature(parent_node, "rear_cct", "gold_rear_spectrum_6k", gold_spectrum);
	if (ret < 0) {
		DEVINFO_LOG("gold_rear_spectrum_6k fail\n");
		return;
	} else {
		DEVINFO_LOG("gold_rear_spectrum_6k [%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u]",
			gold_spectrum[0], gold_spectrum[1], gold_spectrum[2], gold_spectrum[3],
			gold_spectrum[4], gold_spectrum[5], gold_spectrum[6], gold_spectrum[7],
			gold_spectrum[8], gold_spectrum[9], gold_spectrum[10], gold_spectrum[11],
			gold_spectrum[12], gold_spectrum[13]);

		sprintf(gold_rear_spectrum_6k, "%u %u %u %u %u %u %u %u %u %u %u %u %u %u",
			gold_spectrum[0], gold_spectrum[1], gold_spectrum[2], gold_spectrum[3],
			gold_spectrum[4], gold_spectrum[5], gold_spectrum[6], gold_spectrum[7],
			gold_spectrum[8], gold_spectrum[9], gold_spectrum[10], gold_spectrum[11],
			gold_spectrum[12], gold_spectrum[13]);
	}

	/* get gold_rear_spectrum_factor_512gain */
	memset(gold_spectrum, 0, sizeof(gold_spectrum));
	ret = oplus_get_dts_feature(parent_node, "rear_cct", "gold_rear_spectrum_factor_512gain", gold_spectrum);
	if (ret < 0) {
		DEVINFO_LOG("gold_rear_spectrum_factor_512gain fail\n");
		return;
	} else {
		DEVINFO_LOG("gold_rear_spectrum_factor_512gain [%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u]",
			gold_spectrum[0], gold_spectrum[1], gold_spectrum[2], gold_spectrum[3],
			gold_spectrum[4], gold_spectrum[5], gold_spectrum[6], gold_spectrum[7],
			gold_spectrum[8], gold_spectrum[9], gold_spectrum[10], gold_spectrum[11],
			gold_spectrum[12], gold_spectrum[13]);

		sprintf(g_gold_rear_spectrum_factor_512gain, "%u %u %u %u %u %u %u %u %u %u %u %u %u %u",
			gold_spectrum[0], gold_spectrum[1], gold_spectrum[2], gold_spectrum[3],
			gold_spectrum[4], gold_spectrum[5], gold_spectrum[6], gold_spectrum[7],
			gold_spectrum[8], gold_spectrum[9], gold_spectrum[10], gold_spectrum[11],
			gold_spectrum[12], gold_spectrum[13]);
	}

	/* get gold_rear_spectrum_factor_2048gain */
	memset(gold_spectrum, 0, sizeof(gold_spectrum));
	ret = oplus_get_dts_feature(parent_node, "rear_cct", "gold_rear_spectrum_factor_2048gain", gold_spectrum);
	if (ret < 0) {
		DEVINFO_LOG("gold_rear_spectrum_6k fail\n");
		return;
	} else {
		DEVINFO_LOG("gold_rear_spectrum_factor_2048gain [%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u]",
			gold_spectrum[0], gold_spectrum[1], gold_spectrum[2], gold_spectrum[3],
			gold_spectrum[4], gold_spectrum[5], gold_spectrum[6], gold_spectrum[7],
			gold_spectrum[8], gold_spectrum[9], gold_spectrum[10], gold_spectrum[11],
			gold_spectrum[12], gold_spectrum[13]);

		sprintf(g_gold_rear_spectrum_factor_2048gain, "%u %u %u %u %u %u %u %u %u %u %u %u %u %u",
			gold_spectrum[0], gold_spectrum[1], gold_spectrum[2], gold_spectrum[3],
			gold_spectrum[4], gold_spectrum[5], gold_spectrum[6], gold_spectrum[7],
			gold_spectrum[8], gold_spectrum[9], gold_spectrum[10], gold_spectrum[11],
			gold_spectrum[12], gold_spectrum[13]);
	}
}

static void get_fold_hall_limit(void)
{
	int ret = 0;
	uint32_t fold_hall_limit[10] = {0};

	ret = oplus_get_dts_feature(parent_node, "hall", "fold_hall_limit", fold_hall_limit);
	if (ret < 0) {
		DEVINFO_LOG("fold_hall_limit fail\n");
		return;
	} else {
		DEVINFO_LOG("fold_hall_limit [%u, %u], [%u, %u], [%u, %u], [%u, %u], [%u, %u]\n",
			fold_hall_limit[0], fold_hall_limit[1],
			fold_hall_limit[2], fold_hall_limit[3],
			fold_hall_limit[4], fold_hall_limit[5],
			fold_hall_limit[6], fold_hall_limit[7],
			fold_hall_limit[8], fold_hall_limit[9]);

		sprintf(g_fold_hall_limit, "%u %u %u %u %u %u %u %u %u %u",
			fold_hall_limit[0], fold_hall_limit[1],
			fold_hall_limit[2], fold_hall_limit[3],
			fold_hall_limit[4], fold_hall_limit[5],
			fold_hall_limit[6], fold_hall_limit[7],
			fold_hall_limit[8], fold_hall_limit[9]);
	}
}
static void get_gold_rear_cct(void)
{
	int ret = 0;
	uint32_t gold_rear_cct[6] = {0};/*r,g,b,c,w,f*/

	/*get 3000k gold ch*/
	ret = oplus_get_dts_feature(parent_node, "rear_cct", "gold_rear_cct_3k", gold_rear_cct);
	if (ret < 0) {
		DEVINFO_LOG("gold_rear_cct_3k fail\n");
		return;
	} else {
		DEVINFO_LOG("gold_rear_cct_3k [%u, %u, %u, %u, %u, %u]",
			gold_rear_cct[0], gold_rear_cct[1], gold_rear_cct[2],
			gold_rear_cct[3], gold_rear_cct[4], gold_rear_cct[5]);

		sprintf(gold_rear_cct_3k, "%u %u %u %u %u %u",
			 gold_rear_cct[0], gold_rear_cct[1], gold_rear_cct[2],
			 gold_rear_cct[3], gold_rear_cct[4], gold_rear_cct[5]);
	}

	/*get 6000k gold ch*/
	memset(gold_rear_cct, 0, sizeof(gold_rear_cct));
	ret = oplus_get_dts_feature(parent_node, "rear_cct", "gold_rear_cct_6k", gold_rear_cct);
	if (ret < 0) {
		DEVINFO_LOG("gold_rear_cct_6k fail\n");
		return;
	} else {
		DEVINFO_LOG("gold_rear_cct_6k [%u, %u, %u, %u, %u, %u]",
			gold_rear_cct[0], gold_rear_cct[1], gold_rear_cct[2],
			gold_rear_cct[3], gold_rear_cct[4], gold_rear_cct[5]);

		sprintf(gold_rear_cct_6k, "%u %u %u %u %u %u",
			 gold_rear_cct[0], gold_rear_cct[1], gold_rear_cct[2],
			 gold_rear_cct[3], gold_rear_cct[4], gold_rear_cct[5]);
	}

	/* get gold_rear_cct_factor */
	memset(gold_rear_cct, 0, sizeof(gold_rear_cct));
	ret = oplus_get_dts_feature(parent_node, "rear_cct", "gold_rear_cct_factor", gold_rear_cct);
	if (ret < 0) {
		DEVINFO_LOG("gold_rear_cct_factor fail, use default\n");
		sprintf(gold_rear_cct_factor, "%d %d %d %d %d %d", 976, 994, 1038, 981, 920, 1001);
		return;
	} else {
		DEVINFO_LOG("gold_rear_cct_factor [%u, %u, %u, %u, %u, %u]",
			gold_rear_cct[0], gold_rear_cct[1], gold_rear_cct[2],
			gold_rear_cct[3], gold_rear_cct[4], gold_rear_cct[5]);

		sprintf(gold_rear_cct_factor, "%u %u %u %u %u %u",
			 gold_rear_cct[0], gold_rear_cct[1], gold_rear_cct[2],
			 gold_rear_cct[3], gold_rear_cct[4], gold_rear_cct[5]);
	}
}

static void get_proximity_paras(void)
{
	int ret = 0;
	uint32_t multi_offset_coef[3] = {0};
	uint32_t multi_offset_level[1] = {0};
	ret = oplus_get_dts_feature(parent_node, "proximity", "multi_oft_level", multi_offset_level);
	if (ret < 0) {
		DEVINFO_LOG("multi_oft_level fail\n");
		return;
	} else {
		g_multi_offset_level = multi_offset_level[0];
		DEVINFO_LOG("g_multi_offset_level %d\n", g_multi_offset_level);
	}

	ret = oplus_get_dts_feature(parent_node, "proximity", "multi_oft_coef", multi_offset_coef);
	if (ret < 0) {
		DEVINFO_LOG("multi_oft_coef fail\n");
		return;
	} else if (ret == g_multi_offset_level) {
		DEVINFO_LOG("g_multi_offset_coef [%d %d %d]\n", multi_offset_coef[0],
				multi_offset_coef[1], multi_offset_coef[2]);
		sprintf(g_multi_offset_coef, "%d %d %d", multi_offset_coef[0],
				multi_offset_coef[1], multi_offset_coef[2]);
	} else {
		DEVINFO_LOG("multi_oft_coef ret %d\n", ret);
	}
}

static void sensor_devinfo_work(struct work_struct *dwork)
{
	/*to make sure scp is up*/
	if (si && si->init_sensorlist && si->get_lcdinfo_brocast_type) {
		si->init_sensorlist();
		g_als_info.senstype = si->get_lcdinfo_brocast_type();
		si->get_sensor_name(SENSOR_TYPE_GYROSCOPE, &g_acc_gyro_name);
		si->get_sensor_name(SENSOR_TYPE_REAR_ALS, &g_rear_cct_name);
	}

	oplus_als_cali_data_init();
	get_accgyro_cali_version();
	get_front_cct_feature();
	get_gold_rear_cct();
	get_gold_rear_spectrum();
	get_fold_hall_limit();
	get_proximity_paras();

	DEVINFO_LOG("success \n");
}

static ssize_t parameter_proc_read(struct file *file, char __user *buf,
	size_t count, loff_t *off)
{
	char page[512];
	int len = 0;

	len = sprintf(page, "{%s,\n %s,\n %s}", para_buf[0], para_buf[1], para_buf[2]);

	if ((para_buf[2][0] == '\0') && (para_buf[1][0] == '\0')) {
		page[len - 4] = ' ';
		page[len - 6] = ' ';
		page[len - 7] = ' ';
	} else if (para_buf[2][0] == '\0') {
		page[len - 4] = ' ';
	}

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buf, page, (len < count ?  len : count))) {
		return -EFAULT;
	}
	*off +=  len < count ?  len : count;
	return (len < count ?  len : count);
}

static const struct proc_ops parameter_proc_fops = {
	.proc_read = parameter_proc_read,
	.proc_write = NULL,
	.proc_lseek = default_llseek,
};

static int get_msensor_parameter(struct device_node *ch_node, int num)
{
	int ret = 0;
	int elements = 0;
	int index = 0;
	int para_num = 0;
	uint32_t temp_data;
	uint32_t mag_data[30]={0};
	const char *libname = NULL;
	const char *match_project = NULL;
	char temp_buf[128] = {0}, float_buf[10];
	char project[10] = "00000";
	struct device_node *para_ch_node = NULL;

	ret = of_property_read_string(ch_node, "libname", &libname);
	if (libname == NULL || ret < 0) {
		DEVINFO_LOG("get libname prop fail");
		return -1;
	}
	DEVINFO_LOG(" %s libname is %s\n", ch_node->name, libname);

	ret = of_property_read_u32(ch_node, "para_num", &para_num);
	if (ret < 0) {
		DEVINFO_LOG("para num is null, no need to match project");
		ret = of_property_read_variable_u32_array(ch_node, "soft-mag-parameter", mag_data, 1, 30);
		if (ret < 0) {
			DEVINFO_LOG("get soft-mag-parameter prop fail");
			return -1;
		}
		elements = ret;
	} else {
		DEVINFO_LOG(" %s match project start, para_num = %d\n", ch_node->name, para_num);

		/*sprintf(project, "%d", get_project());
		DEVINFO_LOG("project %s\n", project);
		*/
		for_each_child_of_node(ch_node, para_ch_node) {
			DEVINFO_LOG("parse %s", para_ch_node->name);
			ret = of_property_read_string(ch_node, "match_projects", &match_project);
			if (ret < 0 || match_project == NULL) {
				DEVINFO_LOG("get match_project prop fail");
				return -1;
			}
			DEVINFO_LOG(" match project %s\n", match_project);

			if (strstr(match_project, project) != NULL) {
				ret = of_property_read_variable_u32_array(ch_node, "soft-mag-parameter", mag_data, 1, 30);
				if (ret < 0) {
					DEVINFO_LOG("get soft-mag-parameter prop fail");
					return -1;
				}
				elements = ret;

				DEVINFO_LOG("match project success");
				break;
			}
		}
	}

	if (!strcmp(libname, "mmc") || !strcmp(libname, "mxg")) { /*Memsic parameter need analyze*/
		for (index = 0; index < 9; index++) {
			temp_data = mag_data[2 * index];
			sprintf(float_buf, "%c%d.%d%d%d%d", mag_data[2 * index + 1] ? '-' : ' ',
				temp_data / 10000, temp_data % 10000 / 1000, temp_data % 1000 / 100, temp_data % 100 / 10,
				temp_data % 10);
			sprintf(para_buf[num], "%s,%s", temp_buf, float_buf);
			strcpy(temp_buf, para_buf[num]);
		}
		temp_buf[0] = ' ';
		sprintf(para_buf[num], "\"%s\":[%s]", libname, temp_buf);
	} else if (!strcmp(libname, "akm")) {
		for (index = 1; index < elements; index++) {
			sprintf(para_buf[num], "%s,%d", temp_buf, mag_data[index]);
			strcpy(temp_buf, para_buf[num]);
		}
		sprintf(para_buf[num], "\"%s\":[%u%s]", libname, mag_data[0], temp_buf);
	}
	return 0;
}

static void mag_soft_parameter_init(struct device_node *ch_node, int index)
{
	int ret = -1;
	static bool create = false;

	ret = get_msensor_parameter(ch_node, index);
	if (ret == -1) {
		 para_buf[index][0] = '\0';
	} else if (create == false) {
		 proc_create("mag_soft_parameter.json", 0666, NULL, &parameter_proc_fops);
		 create = true;
	}
}

static int sensor_feature_read_func(struct seq_file *s, void *v)
{
	void *p = s->private;
	int selftest_result = 0;

	DEVINFO_LOG("Ptr2UINT32(p) = %d \n", Ptr2UINT32(p));
	switch (Ptr2UINT32(p)) {
	case IS_SUPPROT_HWCALI:
		if (si && si->is_sensor_available && (si->is_sensor_available("tcs3701")
			|| si->is_sensor_available("sip3625"))) {
			seq_printf(s, "%d", 1);
		} else {
			seq_printf(s, "%d", 0);
		}
		break;
	case IS_IN_FACTORY_MODE:
		seq_printf(s, "%d", 1);
		break;
	case IS_SUPPORT_NEW_ARCH:
		seq_printf(s, "%d", is_support_new_arch);
		break;
	case GYRO_CALI_VERSION:
		seq_printf(s, "%d", gyro_cali_version);
		break;
	case ACC_CALI_RANGE:
		seq_printf(s, "%s", acc_cali_range);
		break;
	case SAR_REG_ADDRESS:
		seq_printf(s, "0x%x", g_reg_address);
		break;
	case SAR_REG_VALUE:
		seq_printf(s, "0x%x", g_reg_value);
		break;
	case DO_MAG_SELFTEST:
		if (si && si->send_selft_test) {
			si->send_selft_test(SENSOR_TYPE_MAGNETIC_FIELD, &selftest_result);
		}
		break;
	case GOLD_REAR_CCT_3K:
		seq_printf(s, "%s", gold_rear_cct_3k);
		DEVINFO_LOG("gold_rear_cct_3k = %s \n", gold_rear_cct_3k);
		break;
	case GOLD_REAR_CCT_6K:
		DEVINFO_LOG("gold_rear_cct_6k = %s \n", gold_rear_cct_6k);
		seq_printf(s, "%s", gold_rear_cct_6k);
		break;
	case GOLD_REAR_SPECTRUM_3K:
		seq_printf(s, "%s", gold_rear_spectrum_3k);
		DEVINFO_LOG("gold_rear_spectrum_3k = %s \n", gold_rear_spectrum_3k);
		break;
	case GOLD_REAR_SPECTRUM_6K:
		seq_printf(s, "%s", gold_rear_spectrum_6k);
		DEVINFO_LOG("gold_rear_spectrum_6k = %s \n", gold_rear_spectrum_6k);
		break;
	case GOLD_ALS_FACTOR:
		DEVINFO_LOG("gold_als_factor = %d\n", gold_als_factor);
		seq_printf(s, "%d", gold_als_factor);
		break;
	case GOLD_REAR_ALS_FACTOR:
		DEVINFO_LOG("gold_rear_als_factor = %d\n", gold_rear_als_factor);
		seq_printf(s, "%d", gold_rear_als_factor);
		break;
	case GOLD_REAR_CCT_FACTOR:
		DEVINFO_LOG("gold_rear_cct_factor = %s \n", gold_rear_cct_factor);
		seq_printf(s, "%s", gold_rear_cct_factor);
		break;
	case GOLD_CCT_3K:
		seq_printf(s, "%s", gold_cct_3k);
		DEVINFO_LOG("gold_cct_3k = %s \n", gold_cct_3k);
		break;
	case GOLD_CCT_6K:
		DEVINFO_LOG("gold_cct_6k = %s \n", gold_cct_6k);
		seq_printf(s, "%s", gold_cct_6k);
		break;
	case GOLD_CCT_FACTOR:
		DEVINFO_LOG("gold_cct_factor = %s \n", gold_cct_factor);
		seq_printf(s, "%s", gold_cct_factor);
		break;
	case CCT_TYPE:
		seq_printf(s, "%d", cct_type);
		break;
	case GYRO_CALI_RANGE:
		seq_printf(s, "%d", gyro_cali_range);
		break;
	case GOLD_CCT_CHANNELS:
		seq_printf(s, "%d", gold_cct_channels);
		break;
	case ACC_GYRO_NAME:
		DEVINFO_LOG("acc_gyro_name = %s \n", g_acc_gyro_name);
		seq_printf(s, "%s", g_acc_gyro_name);
		break;
	case REAR_CCT_NAME:
		DEVINFO_LOG("rear_cct_name = %s \n", g_rear_cct_name);
		seq_printf(s, "%s", g_rear_cct_name);
		break;
	case GOLD_REAR_SPECTRUM_FACTOR_512GAIN:
		DEVINFO_LOG("gold_rear_spectrum_factor_512gain = %s \n", g_gold_rear_spectrum_factor_512gain);
		seq_printf(s, "%s", g_gold_rear_spectrum_factor_512gain);
		break;
	case GOLD_REAR_SPECTRUM_FACTOR_2048GAIN:
		DEVINFO_LOG("gold_rear_spectrum_factor_2048gain = %s \n", g_gold_rear_spectrum_factor_2048gain);
		seq_printf(s, "%s", g_gold_rear_spectrum_factor_2048gain);
		break;
	case SUPPORT_PANEL:
		seq_printf(s, "%d", support_panel);
		break;
	case FOLD_HALL_LIMIT:
		DEVINFO_LOG("fold_hall_limit = %s \n", g_fold_hall_limit);
		seq_printf(s, "%s", g_fold_hall_limit);
		break;
	case CCT_GAIN_CALI:
		DEVINFO_LOG("g_cct_gain_cali = %d \n", g_cct_gain_cali);
		seq_printf(s, "%d", g_cct_gain_cali);
		break;
	case MULTI_OFFSET_LEVEL:
		DEVINFO_LOG("g_multi_offset_level = %d \n", g_multi_offset_level);
		seq_printf(s, "%d", g_multi_offset_level);
		break;
	case MULTI_OFFSET_COEF:
		DEVINFO_LOG("g_multi_offset_coef = %s \n", g_multi_offset_coef);
		seq_printf(s, "%s", g_multi_offset_coef);
		break;
	default:
		seq_printf(s, "not support chendai\n");
	}
	return 0;
}

static ssize_t sensor_feature_write(struct file *filp, const char *ubuf, size_t cnt, loff_t *data)
{
	char buf[64] = {0};
	void *buf_str = buf;
	int val = 0;
	int ret = 0;
	int result = 0;
	struct seq_file *s = filp->private_data;
	void *p = s->private;
	int node = Ptr2UINT32(p);

	if (cnt >= sizeof(buf)) {
		return -EINVAL;
	} else {
		if (copy_from_user(buf_str, ubuf, cnt)) {
			return -EFAULT;
		}
	}

	ret = kstrtoint(buf, 0, &val);
	DEVINFO_LOG("node1 = %d,val = %d buf = %s\n", node, val, buf);

	switch (node) {
	case IS_SUPPROT_HWCALI:
		break;
	case IS_IN_FACTORY_MODE:
		if (!si || !si->send_factory_mode) {
			DEVINFO_LOG("no sensorhub interface\n");
			return -EFAULT;
		}
		switch (val) {
		case PS_NORMAL_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_PROXIMITY, 0, &result);
			break;
		case PS_FACTORY_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_PROXIMITY, 1, &result);
			break;
		case PS_LP_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_PROXIMITY, 2, &result);
			break;
		case PS_HP_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_PROXIMITY, 3, &result);
			break;
		case GSENSOR_NORMAL_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_ACCELEROMETER, 0, &result);
			break;
		case GSENSOR_FACTORY_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_ACCELEROMETER, 1, &result);
			break;
		case REAR_CCT_NORMAL_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_CCT, 0, &result);
			break;
		case REAR_CCT_FACTORY_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_CCT, 1, &result);
			break;
		case CCT_NORMAL_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_FRONT_CCT, 0, &result);
			break;
		case CCT_FACTORY_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_FRONT_CCT, 1, &result);
			break;
		case CCT_CLOCK_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_FRONT_CCT, 2, &result);
			break;
		case HINGE_DETECT_NORMAL_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_HINGE_DETECT, 0, &result);
			break;
		case HINGE_DETECT_FACTORY_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_HINGE_DETECT, 1, &result);
			break;
		case REAR_SPECTRUM_NORMAL_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_REAR_SPECTRAL, 0, &result);
			break;
		case REAR_SPECTRUM_FACTORY_512_GAIN:
			ret = si->send_factory_mode(SENSOR_TYPE_REAR_SPECTRAL, 1, &result);
			break;
		case REAR_SPECTRUM_FACTORY_2048_GAIN:
			ret = si->send_factory_mode(SENSOR_TYPE_REAR_SPECTRAL, 2, &result);
			break;
		case REAR_PS_FACTORY_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_REAR_PROXIMITY, 1, &result);
			break;
		case REAR_PS_NORMAL_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_REAR_PROXIMITY, 0, &result);
			break;
		case LEAR_CALI_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_PWM_RGB, 1, &result);
			break;
		case LEAK_CALI_NORMAL_MODE:
			ret = si->send_factory_mode(SENSOR_TYPE_PWM_RGB, 0, &result);
			break;
		case CCT_FACTORY_512_GAIN:
			ret = si->send_factory_mode(SENSOR_TYPE_PWM_RGB, 2, &result);
			break;
		case CCT_FACTORY_1024_GAIN:
			ret = si->send_factory_mode(SENSOR_TYPE_PWM_RGB, 3, &result);
			break;
		case CCT_FACTORY_2048_GAIN:
			ret = si->send_factory_mode(SENSOR_TYPE_PWM_RGB, 4, &result);
			break;
		case CCT_FACTORY_GAIN_NORMAL:
			ret = si->send_factory_mode(SENSOR_TYPE_PWM_RGB, 5, &result);
			break;
		default:
			DEVINFO_LOG("ivalid sensor mode\n");
		}
		if (ret < 0) {
			DEVINFO_LOG("set_factory_mode fail\n");
		}
		break;
	case SAR_REG_ADDRESS:
		sscanf(buf, "%x", &val);
		DEVINFO_LOG("sar_reg_add = 0x%x\n", val);
		g_reg_address = val;
		break;
	case SAR_REG_VALUE:
		sscanf(buf, "%x", &val);
		g_reg_value = val;

		DEVINFO_LOG("g_reg_add = 0x%x g_reg_val = 0x%x\n", g_reg_address, g_reg_value);
		if (si && si->send_reg_config) {
			ret = si->send_reg_config(SENSOR_TYPE_SAR);
		}
		if (ret < 0) {
			DEVINFO_LOG("send sar config fail\n");
		}
		break;
	default:
		DEVINFO_LOG("ivalid node type\n");
	}
	return cnt;
}

static int sensor_feature_open(struct inode *inode, struct file *file)
{
	return single_open(file, sensor_feature_read_func, (void*)PDE_DATA(inode));
}

static const struct proc_ops sensor_info_fops = {
	.proc_open  = sensor_feature_open,
	.proc_write = sensor_feature_write,
	.proc_read  = seq_read,
	.proc_release = single_release,
	.proc_lseek = default_llseek,
};

static struct proc_node sensor_feature_file[] = {
	{"is_support_hwcali", IS_SUPPROT_HWCALI},
	{"is_support_new_arch", IS_SUPPORT_NEW_ARCH},
	{"is_in_factory_mode", IS_IN_FACTORY_MODE},
	{"gyro_cali_version", GYRO_CALI_VERSION},
	{"acc_cali_range", ACC_CALI_RANGE},
	{"do_mag_selftest", DO_MAG_SELFTEST},
	{"sar_reg_address", SAR_REG_ADDRESS},
	{"sar_reg_value", SAR_REG_VALUE},
	{"gold_rear_cct_3k", GOLD_REAR_CCT_3K},
	{"gold_rear_cct_6k", GOLD_REAR_CCT_6K},
	{"gold_cct_3k", GOLD_CCT_3K},
	{"gold_cct_6k", GOLD_CCT_6K},
	{"cct_type", CCT_TYPE},
	{"gold_als_factor", GOLD_ALS_FACTOR},
	{"gold_rear_als_factor", GOLD_REAR_ALS_FACTOR},
	{"gold_rear_cct_factor", GOLD_REAR_CCT_FACTOR},
	{"gyro_cali_range", GYRO_CALI_RANGE},
	{"gold_rear_spectrum_3k", GOLD_REAR_SPECTRUM_3K},
	{"gold_rear_spectrum_6k", GOLD_REAR_SPECTRUM_6K},
	{"gold_cct_channels", GOLD_CCT_CHANNELS},
	{"acc_gyro_name", ACC_GYRO_NAME},
	{"rear_cct_name", REAR_CCT_NAME},
	{"gold_rear_spectrum_factor_512gain", GOLD_REAR_SPECTRUM_FACTOR_512GAIN},
	{"gold_rear_spectrum_factor_2048gain", GOLD_REAR_SPECTRUM_FACTOR_2048GAIN},
	{"gold_cct_factor", GOLD_CCT_FACTOR},
	{"support_panel", SUPPORT_PANEL},
	{"fold_hall_limit", FOLD_HALL_LIMIT},
	{"cct_gain_cali", CCT_GAIN_CALI},
	{"multi_offset_level", MULTI_OFFSET_LEVEL},
	{"multi_offset_coef", MULTI_OFFSET_COEF},
};

static int oplus_sensor_feature_init(void)
{
	struct proc_dir_entry *p_entry;
	static struct proc_dir_entry *oplus_sensor = NULL;
	int i;

	oplus_sensor = proc_mkdir("oplusSensorFeature", NULL);
	if (!oplus_sensor) {
		DEVINFO_LOG("proc_mkdir err\n ");
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(sensor_feature_file); i++) {
		if (sensor_feature_file[i].node_name) {
		p_entry = proc_create_data(sensor_feature_file[i].node_name, S_IRUGO | S_IWUGO,
						  oplus_sensor, &sensor_info_fops, UINT2Ptr(&sensor_feature_file[i].id));
			if (!p_entry) {
				DEVINFO_LOG("create %s err\n", sensor_feature_file[i].node_name);
				proc_remove(oplus_sensor);
				oplus_sensor = NULL;
				return -ENOMEM;
			}
		}
	}
	return 0;
}


static int scp_ready_event(struct notifier_block *this,
	unsigned long event, void *ptr)
{
	if (event == SCP_EVENT_READY) {
		DEVINFO_LOG(" receiver SCP_EVENT_READY event send cfg data\n ");
		schedule_delayed_work(&parameter_work, msecs_to_jiffies(5000));
	}

	return NOTIFY_DONE;
}

static struct notifier_block scp_ready_notifier = {
	.notifier_call = scp_ready_event,
};

static int scp_utc_sync_pm_event(struct notifier_block *notifier,
	unsigned long pm_event, void *unused)
{
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
	case PM_HIBERNATION_PREPARE:
		atomic_set(&utc_suspend, 1);
		break;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
		atomic_set(&utc_suspend, 0);
		schedule_delayed_work(&utc_work, msecs_to_jiffies(100));
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block scp_utc_sync_notifier = {
	.notifier_call = scp_utc_sync_pm_event,
	.priority = 0,
};

static int lcdinfo_callback(struct notifier_block *nb,
	unsigned long event, void *data)
{
	int val = 0;
	if (!data) {
		return 0;
	}
	switch (event) {
	case LCM_DC_MODE_TYPE:
		val = *(bool*)data;
		if (val != g_als_info.dc_mode) {
			g_als_info.dc_mode = val;
			if (g_als_info.use_lb_algo) {
				schedule_delayed_work(&lcdinfo_work, 0);
			}
		}
		break;
	case LCM_BRIGHTNESS_TYPE:
		val = *(int*)data;
		if (20 <= val && val <= lb_bri_max) {
			val = val / 20 * 20;
		} else if (val > lb_bri_max) {
			val = lb_bri_max + 1;
		} else if (1 <= val && val < 20) {
			val = 1;
		}
		if (val != g_als_info.brightness) {
			g_als_info.brightness = val;
			if (g_als_info.use_lb_algo) {
				schedule_delayed_work(&lcdinfo_work, 0);
			}
		}
		break;
	default:
		break;
	}
	return 0;
}

static struct notifier_block lcdinfo_notifier = {
	.notifier_call = lcdinfo_callback,
};

static int oplus_devinfo_probe(struct platform_device *pdev)
{
	int ret = 0;
	int index = 0;
	struct device_node *ch_node = NULL;
	init_sensorhub_interface(&si);
	parent_node = pdev->dev.of_node;
	g_als_info.use_lb_algo = false;
	for_each_child_of_node(parent_node, ch_node) {
		if (strstr(ch_node->name, "msensor") != NULL) {
			mag_soft_parameter_init(ch_node, index);
			index++;
		} else if (0 == strncmp(ch_node->name, "cali_arch", 10)) {
			get_new_arch_info(ch_node);
		} else if (strstr(ch_node->name, "light") != NULL) {
			is_support_lb_algo(ch_node);
			get_lb_max_brightness(ch_node);
			get_gold_cct_channels(ch_node);
		}
	}

	ret = oplus_sensor_feature_init();
	if (ret != 0) {
		DEVINFO_LOG("oplus_sensor_feature_init err\n ");
		return -ENOMEM;
	}

	g_cali_data = kzalloc(sizeof(struct cali_data), GFP_KERNEL);
	if (!g_cali_data) {
		DEVINFO_LOG("kzalloc err\n ");
		return -ENOMEM;
	}
	g_als_info.brightness = 10000;
	g_als_info.dc_mode = 0;

	atomic_set(&utc_suspend, 0);
	INIT_DELAYED_WORK(&utc_work, sync_utc2scp_work);
	schedule_delayed_work(&utc_work, msecs_to_jiffies(2000));
	register_pm_notifier(&scp_utc_sync_notifier);

	INIT_DELAYED_WORK(&lcdinfo_work, transfer_lcdinfo_to_scp);
	register_lcdinfo_notifier(&lcdinfo_notifier);

	/*init parameter*/
	INIT_DELAYED_WORK(&parameter_work, sensor_devinfo_work);
	scp_A_register_notify(&scp_ready_notifier);
	return 0;
}
static int oplus_devinfo_remove(struct platform_device *pdev)
{
	if (g_cali_data) {
		kfree(g_cali_data);
		g_cali_data = NULL;
	}

	return 0;
}
static const struct of_device_id of_drv_match[] = {
	{ .compatible = "oplus,sensor-devinfo"},
	{},
};
MODULE_DEVICE_TABLE(of, of_drv_match);

static struct platform_driver _driver = {
	.probe	  = oplus_devinfo_probe,
	.remove	 = oplus_devinfo_remove,
	.driver	 = {
	.name	   = "sensor-devinfo",
	.of_match_table = of_drv_match,
	},
};

static int __init oplus_devinfo_init(void)
{
	DEVINFO_LOG("oplus_devinfo_init call\n");

	platform_driver_register(&_driver);
	ssc_interactive_init();
	return 0;
}

static void __exit oplus_devinfo_exit(void)
{
	ssc_interactive_exit();
}

late_initcall(oplus_devinfo_init);
module_exit(oplus_devinfo_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("sensor devinfo");
MODULE_AUTHOR("Murphy");
