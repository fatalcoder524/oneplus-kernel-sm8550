/*copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef __SC6607_H__
#define __SC6607_H__

#define SC6607_VINDPM_VOL_MV(x)		(x)
#define SC6607_WD_TIMEOUT_S(x)		(x)
#define SC6607_BOOST_CURR_MA(x)		(x)
#define SC6607_VSYSMIN_VOL_MV(x)		(x)
#define SC6607_BIT(x)			(1 << x)

#define SC6607_I2C_RETRY_DELAY_US		5000
#define SC6607_I2C_RETRY_WRITE_MAX_COUNT	3
#define SC6607_I2C_RETRY_READ_MAX_COUNT	20

#define SC6607_ERR			(1 << 0)
#define SC6607_INFO			(1 << 1)
#define SC6607_DEBUG			(1 << 2)

#define SC6607_UA_PER_MA		1000
#define SC6607_UV_PER_MV		1000

#define SC6607_OPLUS_BC12_RETRY_TIME		round_jiffies_relative(msecs_to_jiffies(200))
#define SC6607_OPLUS_BC12_RETRY_TIME_CDP	round_jiffies_relative(msecs_to_jiffies(400))
#define SC6607_CHG_CURRENT_MAX_MA	(4000)

#define SC6607_AICL_POINT_VOL_9V 		7600
#define SC6607_DUAL_AICL_POINT_VOL_9V		8500
#define SC6607_AICL_POINT_VOL_5V_HIGH1          4350
#define SC6607_AICL_POINT_VOL_5V_HIGH		4250
#define SC6607_AICL_POINT_VOL_5V_MID		4150
#define SC6607_AICL_POINT_VOL_5V_LOW		4100
#define SC6607_HW_AICL_POINT_VOL_5V_PHASE1 	4400
#define SC6607_HW_AICL_POINT_VOL_5V_PHASE2 	4500
#define SC6607_HW_AICL_POINT_VOL_5V_PHASE3      4700
#define SC6607_SW_AICL_POINT_VOL_5V_PHASE1 	4500
#define SC6607_SW_AICL_POINT_VOL_5V_PHASE2 	4550


#define SC6607_DEFUALT_VBUS_LOW 100
#define SC6607_DEFUALT_VBUS_HIGH 200
#define SC6607_I2C_ERR_NUM 10
#define SC6607_MAIN_I2C_ERROR (1 << 0)


#define SC6607_VINDPM_VBAT_PHASE1	4300
#define SC6607_VINDPM_VBAT_PHASE2	4200
#define SC6607_VINDPM_THRES_PHASE1	400
#define SC6607_VINDPM_THRES_PHASE2	300
#define SC6607_VINDPM_THRES_PHASE3	200
#define SC6607_VINDPM_THRES_MIN	4400
#define INIT_STATUS_TIME_5S (5000)
#define INIT_STATUS_DELAY_CHECK (1000)
#define SC6607_CAMERA_ON_DELAY 300

#define SC6607_SUSPEND_AICR		100
#define SC6607_UNSUSPEND_JIFFIES	200

/* add hvdcp func*/
#define DM_0_0 1
#define DM_0_6 2
#define DP_0_6 2
#define DP_3_3 5

#define CONVERT_RETRY_COUNT		100
#define SC6607_5V_THRES_MV		5800
#define SC6607_9V_THRES_MV		7500
#define SC6607_9V_THRES1_MV		7600
#define CONVERY_DELAY_MS		50

#define HVDCP_EXIT_NORMAL	0
#define HVDCP_EXIT_ABNORMAL	1
#define CONV_DETACH_TIME 1000000
#define OPLUS_HVDCP_DETECT_TO_DETACH_TIME 90

#define PD_PHY_SLAVE_ADDRESS 0x62
#define LED_SLAVE_ADDRESS 0x64
#define SC6607_REG_ENTER_TEST_MODE  0x5D
#define SC6607_REG_CHECK_TEST_MODE  0XC1
#define SC6607_REG_CC_PULL_UP_IDRIVE 0x75
#define SC6607_REG_CC_PULL_DOWN_IDRIVE 0x8c
#define SC6607_REG_CONTINUOUS_TIME 0xE7
#define SC6607_REG_BMC_WIDTH_1 0xE8
#define SC6607_REG_BMC_WIDTH_2 0xE9
#define SC6607_REG_BMC_WIDTH_3 0xEA
#define SC6607_REG_BMC_WIDTH_4 0xEB

#ifndef CONFIG_OPLUS_CHARGER_MTK
enum charger_type {
	CHARGER_UNKNOWN,
	STANDARD_HOST,
	CHARGING_HOST,
	NONSTANDARD_CHARGER,
	STANDARD_CHARGER,
	APPLE_2_1A_CHARGER,
	APPLE_1_0A_CHARGER,
	APPLE_0_5A_CHARGER,
	WIRELESS_CHARGER,
};
#endif

enum sc6607_hvdcp_type {
	HVDCP_5V,
	HVDCP_9V,
	HVDCP_12V,
	HVDCP_20V,
	HVDCP_CONTINOUS,
	HVDCP_DPF_DMF,
};


enum SC6607_ADC_MODULE{
	SC6607_ADC_IBUS,
	SC6607_ADC_VBUS,
	SC6607_ADC_VAC,
	SC6607_ADC_VBATSNS,
	SC6607_ADC_VBAT,
	SC6607_ADC_IBAT,
	SC6607_ADC_VSYS,
	SC6607_ADC_TSBUS,
	SC6607_ADC_TSBAT,
	SC6607_ADC_TDIE,
	SC6607_ADC_BATID,
};

enum SC6607_VINDPM {
	SC6607_VINDPM_4000,
	SC6607_VINDPM_4100,
	SC6607_VINDPM_4200,
	SC6607_VINDPM_4300,
	SC6607_VINDPM_4400,
	SC6607_VINDPM_4500,
	SC6607_VINDPM_4600,
	SC6607_VINDPM_4700,
	SC6607_VINDPM_4800,
	SC6607_VINDPM_7600,
	SC6607_VINDPM_8200,
	SC6607_VINDPM_8400,
	SC6607_VINDPM_8600,
	SC6607_VINDPM_10000,
	SC6607_VINDPM_10500,
	SC6607_VINDPM_10700,
};

enum SC6607_WD_TIME {
	SC6607_WD_DISABLE,
	SC6607_WD_0_5_S,
	SC6607_WD_1_S,
	SC6607_WD_2_S,
	SC6607_WD_20_S,
	SC6607_WD_40_S,
	SC6607_WD_80_S,
	SC6607_WD_160_S,
};

enum SC6607_BOOST_CURR {
	SC6607_BOOST_CURR_500,
	SC6607_BOOST_CURR_900,
	SC6607_BOOST_CURR_1300,
	SC6607_BOOST_CURR_1500,
	SC6607_BOOST_CURR_2100,
	SC6607_BOOST_CURR_2500,
	SC6607_BOOST_CURR_2900,
	SC6607_BOOST_CURR_32500,
};

enum SC6607_VSYSMIN {
	SC6607_VSYSMIN_2600,
	SC6607_VSYSMIN_2800,
	SC6607_VSYSMIN_3000,
	SC6607_VSYSMIN_3200,
	SC6607_VSYSMIN_3400,
	SC6607_VSYSMIN_3500,
	SC6607_VSYSMIN_3600,
	SC6607_VSYSMIN_3700,
};

enum SC6607_CHG_STAT{
	SC6607_NOT_CHARGING,
	SC6607_TRICKLE_CHARGING,
	SC6607_PRE_CHARGING,
	SC6607_FAST_CC_CHARGING,
	SC6607_FAST_CV_CHARGING,
	SC6607_TERM_CHARGE,
};

enum SC6607_VBUS_TYPE{
	SC6607_VBUS_TYPE_NONE,
	SC6607_VBUS_TYPE_SDP,
	SC6607_VBUS_TYPE_CDP,
	SC6607_VBUS_TYPE_DCP,
	SC6607_VBUS_TYPE_HVDCP,
	SC6607_VBUS_TYPE_UNKNOWN,
	SC6607_VBUS_TYPE_NON_STD,
	SC6607_VBUS_TYPE_OTG,
};

enum SC6607_HK_GEN_FALG{
	SC6607_BUCK_FLG,
	SC6607_CP_FLG,
	SC6607_LED_FLG,
	SC6607_DPDM_FLG,
	SC6607_VOOC_FLG,
	SC6607_UFCS_FLG,
};

enum {
	SC6607_REG_DEVICE_ID = 0X00,
	SC6607_REG_HK_GEN_STATE,
	SC6607_REG_HK_GEN_FLG,
	SC6607_REG_HK_GEN_MASK,
	SC6607_REG_VAC_VBUS_OVP,
	SC6607_REG_TSBUS_FAULT,
	SC6607_REG_TSBAT_FAULT,
	SC6607_REG_HK_CTRL1 = 0x07,
	SC6607_REG_HK_CTRL2 = 0x08,
	SC6607_REG_HK_INT_STAT = 0X09,
	SC6607_REG_HK_INT_FLG,
	SC6607_REG_HK_INT_MASK,
	SC6607_REG_HK_FLT_STAT,
	SC6607_REG_HK_FLT_FLG = 0x0D,
	SC6607_REG_HK_FLT_MASK,
	SC6607_REG_HK_ADC_CTRL = 0xF,
	SC6607_REG_HK_IBUS_ADC = 0X11,
	SC6607_REG_HK_VBUS_ADC = 0X13,
	SC6607_REG_HK_VAC_ADC = 0X15,
	SC6607_REG_HK_VBATSNS_ADC = 0X17,
	SC6607_REG_HK_VBAT_ADC = 0X19,
	SC6607_REG_HK_IBAT_ADC = 0X1B,
	SC6607_REG_HK_VSYS_ADC = 0X1D,
	SC6607_REG_HK_TSBUS_ADC = 0X1F,
	SC6607_REG_HK_TSBAT_ADC = 0X21,
	SC6607_REG_HK_TDIE_ADC = 0X23,
	SC6607_REG_HK_BATID_ADC = 0X25,
	SC6607_REG_EDL_BUFFER = 0X27,
	SC6607_REG_VSYS_MIN = 0x30,
	SC6607_REG_VBAT,
	SC6607_REG_ICHG_CC,
	SC6607_REG_VINDPM,
	SC6607_REG_IINDPM,
	SC6607_REG_ICO_CTRL,
	SC6607_REG_PRECHARGE_CTRL,
	SC6607_REG_TERMINATION_CTRL,
	SC6607_REG_RECHARGE_CTRL = 0x38,
	SC6607_REG_VBOOST_CTRL,
	SC6607_REG_PROTECTION_DIS,
	SC6607_REG_RESET_CTRL,
	SC6607_REG_CHG_CTRL1 = 0x3C,
	SC6607_REG_CHG_CTRL2 = 0x3D,
	SC6607_REG_CHG_CTRL3 = 0x3E,
	SC6607_REG_CHG_CTRL4 = 0x3F,
	SC6607_REG_CHG_CTRL5 = 0x40,
	SC6607_REG_CHG_INT_STAT = 0x41,
	SC6607_REG_CHG_INT_STAT2,
	SC6607_REG_CHG_INT_FLG = 0x44,
	SC6607_REG_CHG_INT_FLG3 = 0x46,
	SC6607_REG_CHG_INT_MASK = 0x47,
	SC6607_REG_CHG_FLT_STAT = 0x50,
	SC6607_REG_CHG_FLT_FLG = 0x52,
	SC6607_REG_CHG_FLT_FLG2 = 0x53,
	SC6607_REG_CHG_FLT_MASK = 0x54,
	SC6607_REG_JEITA_TEMP = 0x56,
	SC6607_REG_BUCK_STAT,
	SC6607_REG_BUCK_FLG,
	SC6607_REG_BUCK_MASK,
	SC6607_REG_INTERNAL1 = 0x5D,
	SC6607_REG_INTERNAL2 = 0x5E,
	SC6607_REG_VBATSNS_OVP = 0x60,
	SC6607_REG_IBUS_OCP_UCP = 0x61,
	SC6607_REG_PMID2OUT_OVP = 0X62,
	SC6607_REG_PMID2OUT_UVP,
	SC6607_REG_CP_CTRL = 0x64,
	SC6607_REG_CP_CTRL_2 = 0x65,
	SC6607_REG_CP_INT_STAT = 0x66,
	SC6607_REG_CP_INT_FLG,
	SC6607_REG_CP_INT_MASK = 0X68,
	SC6607_REG_CP_FLT_STAT,
	SC6607_REG_CP_FLT_FLG = 0x6B,
	SC6607_REG_CP_PMID2OUT_FLG = 0x6C,
	SC6607_REG_CP_FLT_MASK = 0x6D,
	SC6607_REG_CP_FLT_DIS = 0x6F,
	SC6607_REG_DPDM_EN = 0x90,
	SC6607_REG_DPDM_CTRL = 0x91,
	SC6607_REG_DPDM_QC_CTRL,
	SC6607_REG_DPDM_TFCP_CTRL,
	SC6607_REG_DPDM_INT_FLAG,
	SC6607_REG_DPDM_INT_MASK,
	SC6607_REG_QC3_INT_FLAG,
	SC6607_REG_QC3_INT_MASK,
	SC6607_REG_DP_STAT,
	SC6607_REG_DM_STAT,
	SC6607_REG_DPDM_INTERNAL,
	SC6607_REG_DPDM_INTERNAL_3 = 0x9C,
	SC6607_REG_DPDM_CTRL_2 = 0x9D,
	SC6607_REG_DPDM_NONSTD_STAT,
	SC6607_REG_PHY_CTRL = 0XA0,
	SC6607_REG_TXBUF_DATA0 = 0XA1,
	SC6607_REG_TXBUF_DATA1 = 0XA2,
	SC6607_REG_ADAPTER_INFO = 0XA3,
	SC6607_REG_VOOCPHY_FLAG = 0XA4,
	SC6607_REG_VOOCPHY_IRQ = 0XA5,
	SC6607_REG_PREDATA_VALUE = 0XA6,
	SC6607_REG_T5_T7_SETTING = 0XA8,
	SC6607_REG_DP_HOLD_TIME = 0XA9,
	SC6607_REG_TX_DISCARD = 0XEC,
	SC6607_REG_MAX,
};

enum sc6607_fields {
	F_VAC_OVP,
	F_VBUS_OVP,
	F_TSBUS_FLT,
	F_TSBAT_FLT,
	F_ACDRV_MANUAL_PRE,
	F_ACDRV_EN,
	F_ACDRV_MANUAL_EN,
	F_WD_TIME_RST,
	F_WD_TIMER,
	F_REG_RST,
	F_VBUS_PD,
	F_VAC_PD,
	F_TSBUS_TSBAT_FLT_DIS,
	F_EDL_TSBUS_SEL,
	F_ADC_EN,
	F_ADC_FREEZE,
	F_BATSNS_EN,
	F_VBAT,
	F_ICHG_CC,
	F_VINDPM_VBAT,
	F_VINDPM_DIS,
	F_VINDPM,
	F_IINDPM_DIS,
	F_IINDPM,
	F_FORCE_ICO,
	F_ICO_EN,
	F_IINDPM_ICO,
	F_VPRECHG,
	F_IPRECHG,
	F_TERM_EN,
	F_ITERM,
	F_RECHG_DIS,
	F_RECHG_DG,
	F_VRECHG,
	F_VBOOST,
	F_IBOOST,
	F_CONV_OCP_DIS,
	F_TSBAT_JEITA_DIS,
	F_IBAT_OCP_DIS,
	F_VPMID_OVP_OTG_DIS,
	F_VBAT_OVP_BUCK_DIS,
	F_T_BATFET_RST,
	F_T_PD_nRST,
	F_BATFET_RST_EN,
	F_BATFET_DLY,
	F_BATFET_DIS,
	F_nRST_SHIPMODE_DIS,
	F_HIZ_EN,
	F_PERFORMANCE_EN,
	F_DIS_BUCKCHG_PATH,
	F_DIS_SLEEP_FOR_OTG,
	F_QB_EN,
	F_BOOST_EN,
	F_CHG_EN,
	F_VBAT_TRACK,
	F_IBATOCP,
	F_VSYSOVP_DIS,
	F_VSYSOVP_TH,
	F_JEITA_ISET_COOL,
	F_JEITA_VSET_WARM,
	F_TMR2X_EN,
	F_CHG_TIMER_EN,
	F_CHG_TIMER,
	F_TDIE_REG_DIS,
	F_TDIE_REG,
	F_PFM_DIS,
	F_BOOST_FREQ,
	F_BUCK_FREQ,
	F_BAT_LOAD_EN,
	F_CHG_STAT,
	F_BOOST_GOOD,
	F_JEITA_COOL_TEMP,
	F_JEITA_WARM_TEMP,
	F_BOOST_NTC_HOT_TEMP,
	F_BOOST_NTC_COLD_TEMP,
	F_TESTM_EN,
	F_KEY_EN_OWN,
	F_VBATSNS_OVP_DIS,
	F_VBATSNS_DIS,
	F_VBATSNS_OVP,
	F_IBUS_UCP_FALL_DG_SET,
	F_IBUS_OCP,
	F_PMID2OUT_OVP_DIS,
	F_PMID2OUT_OVP_BLK,
	F_PMID2OUT_OVP,
	F_PMID2OUT_UVP_DIS,
	F_PMID2OUT_UVP_BLK,
	F_PMID2OUT_UVP,
	F_FSW_SET,
	F_FREQ_SHIFT,
	F_FSW_SET_Ratio,
	F_MODE,
	F_CP_EN,
	F_SS_TIMEOUT,
	F_IBUS_UCP_DIS,
	F_IBUS_OCP_DIS,
	F_VBUS_PD_100MA_1S_DIS,
	F_VBUS_IN_RANGE_DIS,
	F_FORCE_INDET,
	F_AUTO_INDET_EN,
	F_HVDCP_EN,
	F_QC_EN,
	F_DP_DRIV,
	F_DM_DRIV,
	F_BC1_2_VDAT_REF_SET,
	F_BC1_2_DP_DM_SINK_CAP,
	F_I2C_DPDM_BYPASS_EN,
	F_DPDM_PULL_UP_EN,
	F_WDT_TFCP_MASK,
	F_WDT_TFCP_FLAG,
	F_WDT_TFCP_RST,
	F_WDT_TFCP_CFG,
	F_WDT_TFCP_DIS,
	F_VBUS_STAT,
	F_BC1_2_DONE,
	F_DP_OVP,
	F_DM_OVP,
	F_DM_500K_PD_EN,
	F_DP_500K_PD_EN,
	F_DM_SINK_EN,
	F_DP_SINK_EN,
	F_DP_SRC_10UA,
	F_DPDM_3P3_EN,
	F_SS_TIMEOUT_MASK,
	F_PHY_EN,
	F_PHY_RST,
	F_MAX_FIELDS,
};

enum DPDM_SET_STATE{
	DPDM_HIZ,
	DPDM_DOWN_20K,
	DPDM_V0_6,
	DPDM_V1_8,
	DPDM_V2_7,
	DPDM_V3_3,
	DPDM_DOWN_500K,
};

enum DPDM_STATE {
	DPDM_V0_TO_V0_325 = 0,
	DPDM_V0_325_TO_V1,
	DPDM_V1_TO_V1_35,
	DPDM_V1_35_TO_V22,
	DPDM_V2_2_TO_V3,
	DPDM_V3,
	DPDM_MAX,
};

enum DPDM_CAP {
	DPDM_CAP_SNK_50UA = 0,
	DPDM_CAP_SNK_100UA,
	DPDM_CAP_SRC_10UA,
	DPDM_CAP_SRC_250UA,
};

enum BC12_RESULT {
	UNKNOWN_DETECED,
	SDP_DETECED,
	CDP_DETECED,
	DCP_DETECED,
	HVDCP_DETECED,
	NON_STANDARD_DETECTED,
	APPLE_3A_DETECTED = (1 << 3) | NON_STANDARD_DETECTED,
	APPLE_2_1A_DETECTED = (2 << 3) | NON_STANDARD_DETECTED,
	SS_2A_DETECTED = (3 << 3) | NON_STANDARD_DETECTED,
	APPLE_1A_DETECTED = (4 << 3) | NON_STANDARD_DETECTED,
	APPLE_2_4A_DETECTED = (5 << 3) | NON_STANDARD_DETECTED,
	OCP_DETECED,
};

enum bc12_state {
	BC12_DETECT_INIT,
	NON_STANDARD_ADAPTER_DETECTION,
	FLOAT_DETECTION,
	BC12_PRIMARY_DETECTION,
	HIZ_SET,
	BC12_SECONDARY_DETECTION,
	HVDCP_HANKE,
};

enum BOOST_ON_MASK {
	BOOST_ON_OTG = 0,
	BOOST_ON_CAMERA,
	BOOST_ON_MAX,
};

enum {
	BUCK_CHARGER_FLAG = 0,
	CHARGER_PUMP_FLAG,
	LED_FLAG,
	DPDM_FLAG,
	VOOC_FLAG,
	UFCS_FLAG,
	HK_FLAG,
	MAX_FLAG,
};

#define SC6607_VOOCPHY_DEVICE_ID        			0x66

/* Register 0Bh */
#define SC6607_HK_V2X_UVLO_MASK		BIT(7)
#define SC6607_HK_POR_MASK			BIT(6)
#define SC6607_HK_RESET_MASK			BIT(5)
#define SC6607_HK_ADC_DONE_MASK		BIT(4)
#define SC6607_HK_REGN_OK_MASK		BIT(3)
#define SC6607_HK_VBAT_UVLO_MASK		BIT(2)
#define SC6607_HK_VBUS_PRESENT_MASK		BIT(1)
#define SC6607_HK_VAC_PRESENT_MASK		BIT(0)

/* Register 0Dh */
#define SC6607_HK_WD_TIMEOUT_MASK		BIT(6)
#define SC6607_HK_TSHUT_MASK			BIT(5)
#define SC6607_HK_TSBAT_HOT_MASK		BIT(4)
#define SC6607_HK_TSBUS_FLT_MASK		BIT(2)
#define SC6607_HK_VBUS_OVP_MASK		BIT(1)
#define SC6607_HK_VAC_OVP_MASK		BIT(0)

/* Register 0Fh */
#define SC6607_HK_CTRL3		0x07

/* Register 10h */
#define SC6607_ADC_FUNC_DIS		0x79

/* Register 11h */
#define SC6607_VOOCPHY_IBUS_POL_H_SHIFT          	8
#define SC6607_VOOCPHY_IBUS_POL_H_MASK              0x0F
#define SC6607_VOOCPHY_IBUS_ADC_LSB                 2500/1000


/* Register 13h */
#define SC6607_VOOCPHY_VBUS_POL_H_SHIFT          	8
#define SC6607_VOOCPHY_VBUS_POL_H_MASK              0x0F
#define SC6607_VOOCPHY_VBUS_ADC_LSB                 375/100


/* Register 15h */
#define SC6607_VOOCPHY_VAC_POL_H_SHIFT          	8
#define SC6607_VOOCPHY_VAC_POL_H_MASK               0x0F
#define SC6607_VOOCPHY_VAC_ADC_LSB                  5

/* Register 17h */
#define SC6607_VOOCPHY_VBAT_POL_H_SHIFT          	8
#define SC6607_VOOCPHY_VBAT_POL_H_MASK              0x0F
#define SC6607_VOOCPHY_VBAT_ADC_LSB                 125/100

/* Register 19h */
#define SC6607_VOOCPHY_VBATSNS_POL_H_SHIFT          8
#define SC6607_VOOCPHY_VBATSNS_POL_H_MASK           0x0F
#define SC6607_VOOCPHY_VBATSNS_ADC_LSB              125/100


/* Register 1Dh */
#define SC6607_VOOCPHY_VSYS_POL_H_SHIFT          	8
#define SC6607_VOOCPHY_VSYS_POL_H_MASK           	0x0F
#define SC6607_VOOCPHY_VSYS_ADC_LSB              	125/100

/* Register 1Fh */
#define SC6607_VOOCPHY_TSBUS_POL_H_SHIFT          	8
#define SC6607_VOOCPHY_TSBUS_POL_H_MASK             0x03
#define SC6607_VOOCPHY_TSBUS_ADC_LSB                9766/1000

/* Register 21h */
#define SC6607_VOOCPHY_TSBAT_POL_H_SHIFT          	8
#define SC6607_VOOCPHY_TSBAT_POL_H_MASK             0x03
#define SC6607_VOOCPHY_TSBAT_ADC_LSB                9766/1000

/* Register 31h */
#define SC6607_BUCK_VBAT_OFFSET		3840
#define SC6607_BUCK_VBAT_STEP			8

/* Register 32h */
#define SC6607_BUCK_ICHG_OFFSET		0
#define SC6607_BUCK_ICHG_STEP			50

/* Register 34h */
#define SC6607_BUCK_IINDPM_OFFSET		100
#define SC6607_BUCK_IINDPM_STEP		50

/* Register 35h */
#define SC6607_BUCK_IINDPM_ICO_OFFSET	100
#define SC6607_BUCK_IINDPM_ICO_STEP		50

/* Register 36h */
#define SC6607_BUCK_IPRECHG_OFFSET		50
#define SC6607_BUCK_IPRECHG_STEP		50

/* Register 37h */
#define SC6607_BUCK_ITERM_OFFSET		100
#define SC6607_BUCK_ITERM_STEP		50

/* Register 39h */
#define SC6607_BUCK_VBOOST_OFFSET		3900
#define SC6607_BUCK_VBOOST_STEP		100

/* Register 44h */
#define SC6607_BUCK_VSLEEP_MASK		BIT(3)

/* Register 46h */
#define SC6607_BUCK_TDIE_FLAG_MASK		BIT(6)

/* Register 47~49h */
#define SC6607_BUCK_TDIE_REG_MASK		BIT(22)
#define SC6607_BUCK_TSBAT_COOL_MASK		BIT(21)
#define SC6607_BUCK_TSBAT_WARM_MASK		BIT(20)
#define SC6607_BUCK_ICO_MASK			BIT(18)
#define SC6607_BUCK_IINDPM_MASK		BIT(17)
#define SC6607_BUCK_VINDPM_MASK		BIT(16)
#define SC6607_BUCK_CHG_MASK			BIT(13)
#define SC6607_BUCK_BOOST_OK_MASK		BIT(12)
#define SC6607_BUCK_VSYSMIN_MASK		BIT(11)
#define SC6607_BUCK_QB_ON_MASK		BIT(10)
#define SC6607_BUCK_BATFET_MASK		BIT(8)
#define SC6607_BUCK_VSYS_SHORT_MASK		BIT(4)
#define SC6607_BUCK_VSLEEP_BUCK_MASK		BIT(3)
#define SC6607_BUCK_VBAT_DPL_MASK		BIT(2)
#define SC6607_BUCK_VBAT_LOW_BOOST_MASK	BIT(1)
#define SC6607_BUCK_VBUS_GOOD_MASK		BIT(0)

/* Register 52h */
#define SC6607_BUCK_CONV_OCP_FLG_MASK	BIT(4)
#define SC6607_BUCK_VSYS_OVP_FLG_MASK	BIT(3)
#define SC6607_BUCK_IBUS_RCP_FLG_MASK	BIT(2)
#define SC6607_BUCK_IBAT_OCP_FLG_MASK	BIT(1)
#define SC6607_BUCK_VBAT_OVP_BUCK_FLG_MASK	BIT(0)

/* Register 53h */
#define SC6607_BUCK_VPMID_OVP_OTG_FLG_MASK	BIT(0)
#define SC6607_BUCK_CHG_TIMEOUT_FLG_MASK	BIT(2)

/* Register 64h */
#define SC6607_VOOCPHY_CP_EN_MASK               	0x01
#define SC6607_VOOCPHY_CP_EN_SHIFT              	1
#define SC6607_VOOCPHY_CP_ENABLE                 	1
#define SC6607_VOOCPHY_CP_DISABLE                    0

/* Register 65h */
#define	SC6607_VOOCPHY_IBUS_UCP_DIS_MASK            0x08
#define	SC6607_VOOCPHY_IBUS_UCP_DIS_SHIFT           3
#define	SC6607_VOOCPHY_IBUS_UCP_ENABLE              0
#define	SC6607_VOOCPHY_IBUS_UCP_DISABLE             1

/* Register 6Bh */
#define SC6607_VOOCPHY_SS_TIMEOUT_FLAG_MASK         0x10
#define SC6607_VOOCPHY_SS_TIMEOUT_FLAG_SHIFT        4
#define SC6607_VOOCPHY_IBUS_UCP_FALL_FLAG_MASK      0x08
#define SC6607_VOOCPHY_IBUS_UCP_FALL_FLAG_SHIFT     3
#define SC6607_VOOCPHY_IBUS_OCP_FLAG_MASK           0x04
#define SC6607_VOOCPHY_IBUS_OCP_FLAG_SHIFT          2
#define SC6607_VOOCPHY_VBAT_OVP_FLAG_MASK     		0x02
#define SC6607_VOOCPHY_VBAT_OVP_FLAG_SHIFT    		1
#define SC6607_VOOCPHY_VBATSNS_OVP_FLAG_MASK        0x01
#define SC6607_VOOCPHY_VBATSNS_OVP_FLAG_SHIFT       0

/* Register 6Ch */
#define SC6607_VOOCPHY_PMID2OUT_OVP_FLAG_MASK       0x04
#define SC6607_VOOCPHY_PMID2OUT_OVP_FLAG_SHIFT      2
#define SC6607_VOOCPHY_PMID2OUT_UCP_FLAG_MASK        0x02
#define SC6607_VOOCPHY_PMID2OUT_UCP_FLAG_SHIFT       1
#define SC6607_VOOCPHY_PIN_DIAG_FALL_FLAG_MASK      0x01
#define SC6607_VOOCPHY_PIN_DIAG_FALL_FLAG_SHIFT     0

/* Register 94h */
#define SC6607_BUCK_DP_OVP_FLG_MASK	BIT(1)
#define SC6607_BUCK_DM_OVP_FLG_MASK	BIT(0)

struct soft_bc12 {
	u8 bc12_state;
	enum DPDM_STATE dp_state;
	enum DPDM_STATE dm_state;
	enum BC12_RESULT result;

	u8 flag;
	bool detect_done;
	bool first_noti_sdp;
	bool detect_ing;

	struct mutex running_lock;
	struct delayed_work detect_work;
	int next_run_time;
};

struct sc6607 {
	struct device *dev;
	struct i2c_client *client;

	struct regmap *regmap;
	struct regmap_field *regmap_fields[F_MAX_FIELDS];

	const char *chg_dev_name;
	const char *eint_name;

	struct wakeup_source *suspend_ws;
	/*fix chgtype identify error*/
	struct wakeup_source *keep_resume_ws;
	wait_queue_head_t wait;

	atomic_t driver_suspended;
	atomic_t charger_suspended;
	atomic_t otg_enable_cnt;
	unsigned long request_otg;

	enum charger_type chg_type;
	enum power_supply_type oplus_chg_type;

	int irq;
	int irq_gpio;
	struct pinctrl *pinctrl;
	struct pinctrl_state *splitchg_inter_active;
	struct pinctrl_state *splitchg_inter_sleep;

	bool power_good;
	bool wd_rerun_detect;
	struct sc6607_platform_data *platform_data;
	struct charger_device *chg_dev;

	struct power_supply *psy;
	struct power_supply *chg_psy;
	struct power_supply_desc psy_desc;
	int vbus_type;
	int hw_aicl_point;
	bool camera_on;
	bool calling_on;

	bool is_force_dpdm;
	bool pd_sdp_port;
	bool usb_connect_start;

	struct mutex	dpdm_lock;
	struct mutex adc_read_lock;
	struct regulator	*dpdm_reg;
	bool	dpdm_enabled;
	struct soft_bc12 bc12;
	int soft_bc12_type;
	bool soft_bc12;
	bool bc12_done;
	bool open_adc_by_vbus;

	char chg_power_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];
	char err_reason[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN];
	char dump_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];
	struct mutex track_upload_lock;
	struct mutex track_hk_err_lock;
	u32 debug_force_hk_err;
	bool hk_err_uploading;
	oplus_chg_track_trigger *hk_err_load_trigger;
	struct delayed_work hk_err_load_trigger_work;
	struct delayed_work hw_bc12_detect_work;
	struct delayed_work init_status_work;
	struct delayed_work sc6607_aicr_setting_work;
	struct delayed_work init_status_check_work;
	struct delayed_work sc6607_camera_off_work;
	bool track_init_done;

	u8 chip_id;
#ifdef CONFIG_OPLUS_CHARGER_MTK
	int pd_type;
	struct adapter_device *pd_adapter;
	struct mutex charger_pd_lock;
	struct tcpc_device *tcpc;
	struct notifier_block pd_nb;
#endif
	int aicr;
	int pd_curr_max;
	int main_curr_max;
	int charger_current_pre;
	bool hvdcp_can_enabled;
	bool disable_qc;
	bool support_tsbat;
	bool pdqc_setup_5v;
	int  qc_to_9v_count;
	bool hvdcp_cfg_9v_done;
	int hvdcp_exit_stat;
	unsigned long long hvdcp_detect_time;
	unsigned long long hvdcp_detach_time;
	struct delayed_work sc6607_vol_convert_work;
	struct delayed_work check_charger_out_work;

	int  bc12_timeouts;
	struct timer_list bc12_timeout;
};

struct sc6607_platform_data {
	u32 vsyslim;
	u32 batsns_en;
	u32 vbat;
	u32 ichg;
	u32 vindpm;
	u32 iindpm_dis;
	u32 iindpm;
	u32 ico_enable;
	u32 iindpm_ico;
	u32 vprechg;
	u32 iprechg;
	u32 iterm_en;
	u32 iterm;
	u32 rechg_dis;
	u32 rechg_dg;
	u32 rechg_volt;
	u32 vboost;
	u32 iboost;
	u32 conv_ocp_dis;
	u32 tsbat_jeita_dis;
	u32 ibat_ocp_dis;
	u32 vpmid_ovp_otg_dis;
	u32 vbat_ovp_buck_dis;
	u32 ibat_ocp;
	u32 ntc_suport_1000k;
#ifdef OPLUS_FEATURE_CHG_BASIC
/********* workaround: Octavian needs to enable adc start *********/
	bool enable_adc;
/********* workaround: Octavian needs to enable adc end *********/
#endif
	u32 cc_pull_up_idrive;
	u32 cc_pull_down_idrive;
	u32 continuous_time;
	u32 bmc_width[4];
};

struct sc6607_alert_handler {
	u32 bit_mask;
	int (*handler)(struct sc6607 *);
};

struct sc6607_temp_param {
	__s32 bts_temp;
	__s32 temperature_r;
};

struct sc6607_ntc_temp{
	struct sc6607_temp_param *pst_temp_table;
	int table_size;
};

struct sc6607_track_check_reg {
	u8 addr;
	u8 data;
};

extern void oplus_set_usb_props_type(enum power_supply_type type);
extern bool oplus_pd_without_usb(void);
int oplus_sy6970_charger_unsuspend(void);
int oplus_sc6607_read_vbus(void);
int oplus_sc6607_read_ibus(void);
int oplus_sc6607_read_vac(void);
int oplus_sc6607_read_vsys(void);
int oplus_sc6607_read_vbat(void);
bool check_ntc_suport_1000k(void);
#endif /*__SC6607_H__*/

