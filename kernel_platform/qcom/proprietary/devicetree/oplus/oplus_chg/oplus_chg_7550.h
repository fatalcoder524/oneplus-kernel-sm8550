//ifdef OPLUS_FEATURE_CHG_BASIC

#define OPLUS_CHG_IC_BUCK		0x00
#define OPLUS_CHG_IC_BOOST		0x01
#define OPLUS_CHG_IC_BUCK_BOOST		0x02
#define OPLUS_CHG_IC_CP_DIV2		0x03
#define OPLUS_CHG_IC_CP_MUL2		0x04
#define OPLUS_CHG_IC_CP_TW2		0x05
#define OPLUS_CHG_IC_RX			0x06
#define OPLUS_CHG_IC_VIRTUAL_RX		0x07
#define OPLUS_CHG_IC_VIRTUAL_BUCK	0x08
#define OPLUS_CHG_IC_VIRTUAL_CP		0x09
#define OPLUS_CHG_IC_VIRTUAL_USB	0x0a
#define OPLUS_CHG_IC_TYPEC		0x0b

#define CURR_LIMIT_VOOC_3_6A_SVOOC_2_5A	0x01
#define CURR_LIMIT_VOOC_2_5A_SVOOC_2_0A	0x02
#define CURR_LIMIT_VOOC_3_0A_SVOOC_3_0A	0x03
#define CURR_LIMIT_VOOC_4_0A_SVOOC_4_0A	0x04
#define CURR_LIMIT_VOOC_5_0A_SVOOC_5_0A	0x05
#define CURR_LIMIT_VOOC_6_0A_SVOOC_6_5A	0x06

#define CURR_LIMIT_7BIT_1_0A		0x01
#define CURR_LIMIT_7BIT_1_5A		0x02
#define CURR_LIMIT_7BIT_2_0A		0x03
#define CURR_LIMIT_7BIT_2_5A		0x04
#define CURR_LIMIT_7BIT_3_0A		0x05
#define CURR_LIMIT_7BIT_3_5A		0x06
#define CURR_LIMIT_7BIT_4_0A		0x07
#define CURR_LIMIT_7BIT_4_5A		0x08
#define CURR_LIMIT_7BIT_5_0A		0x09
#define CURR_LIMIT_7BIT_5_5A		0x0a
#define CURR_LIMIT_7BIT_6_0A		0x0b
#define CURR_LIMIT_7BIT_6_3A		0x0c
#define CURR_LIMIT_7BIT_6_5A		0x0d
#define CURR_LIMIT_7BIT_7_0A		0x0e
#define CURR_LIMIT_7BIT_7_5A		0x0f
#define CURR_LIMIT_7BIT_8_0A		0x10
#define CURR_LIMIT_7BIT_8_5A		0x11
#define CURR_LIMIT_7BIT_9_0A		0x12
#define CURR_LIMIT_7BIT_9_5A		0x13
#define CURR_LIMIT_7BIT_10_0A		0x14
#define CURR_LIMIT_7BIT_10_5A		0x15
#define CURR_LIMIT_7BIT_11_0A		0x16
#define CURR_LIMIT_7BIT_11_5A		0x17
#define CURR_LIMIT_7BIT_12_0A		0x18
#define CURR_LIMIT_7BIT_12_5A		0x19

/* VADC scale function index */
#define OPLUS_ADC_SCALE_DEFAULT			0x0
#define OPLUS_ADC_SCALE_THERM_100K_PULLUP		0x1
#define OPLUS_ADC_SCALE_PMIC_THERM			0x2
#define OPLUS_ADC_SCALE_XOTHERM			0x3
#define OPLUS_ADC_SCALE_PMI_CHG_TEMP			0x4
#define OPLUS_ADC_SCALE_HW_CALIB_DEFAULT		0x5
#define OPLUS_ADC_SCALE_HW_CALIB_THERM_100K_PULLUP	0x6
#define OPLUS_ADC_SCALE_HW_CALIB_XOTHERM		0x7
#define OPLUS_ADC_SCALE_HW_CALIB_PMIC_THERM		0x8
#define OPLUS_ADC_SCALE_HW_CALIB_CUR			0x9
#define OPLUS_ADC_SCALE_HW_CALIB_PM5_CHG_TEMP		0xA
#define OPLUS_ADC_SCALE_HW_CALIB_PM5_SMB_TEMP		0xB
#define OPLUS_ADC_SCALE_HW_CALIB_BATT_THERM_100K	0xC
#define OPLUS_ADC_SCALE_HW_CALIB_BATT_THERM_30K		0xD
#define OPLUS_ADC_SCALE_HW_CALIB_BATT_THERM_400K	0xE
#define OPLUS_ADC_SCALE_HW_CALIB_PM5_SMB1398_TEMP	0xF

/* charge protocol arbitration */
#define CHG_PROTOCOL_BC12		0
#define CHG_PROTOCOL_PD			1
#define CHG_PROTOCOL_PPS		2
#define CHG_PROTOCOL_VOOC		3
#define CHG_PROTOCOL_UFCS		4
#define CHG_PROTOCOL_QC			5
//endif OPLUS_FEATURE_CHG_BASIC
