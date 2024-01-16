/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ad82128.h  --  ad82128 ALSA SoC Audio driver
 *
 * Copyright 1998 Elite Semiconductor Memory Technology
 *
 * Author: ESMT Audio/Power Product BU Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __AD82128_H__
#define __AD82128_H__

#define AD82128_REGISTER_COUNT					 134
#define AD82128_RAM_TABLE_COUNT          120

// #define	PD_CRL_PIN_INDEX				122

/* Register Address Map */
#define AD82128_STATE_CTRL1_REG	0x00
#define AD82128_STATE_CTRL2_REG	0x01
#define AD82128_STATE_CTRL3_REG	0x02
#define AD82128_VOLUME_CTRL_REG	0x03
#define AD82128_VOLUME_CTRL_REG_CH1  0x04
#define AD82128_VOLUME_CTRL_REG_CH2  0x05
#define AD82128_STATE_CTRL5_REG	0x1A
#define AD82128_MONO_KEY_HIGH 0X5b
#define AD82128_MONO_KEY_LOW 0X5c


#define CFADDR    0x1d
#define A1CF1     0x1e
#define A1CF2     0x1f
#define A1CF3     0x20
#define A1CF4     0x21
#define CFUD      0x32

#define AD82128_ANALOG_CTRL_REG		0x5e

#define AD82128_FAULT_REG		0x7D
#define AD82128_MAX_REG			0x85

/* AD82128_STATE_CTRL2_REG */
#define AD82128_SSZ_DS			BIT(5)

/* AD82128_STATE_CTRL1_REG */
#define AD82128_SAIF_I2S		(0x0 << 5)
#define AD82128_SAIF_LEFTJ		(0x1 << 5)
#define AD82128_SAIF_FORMAT_MASK	GENMASK(7, 5)

/* AD82128_STATE_CTRL3_REG */
#define AD82128_MUTE			BIT(6)

/* AD82128_STATE_CTRL5_REG */
#define AD82128_SW_RESET			BIT(5)

/* AD82128 Subwoofer Config*/
#define AD82128_Subwoofer   BIT(6)

/* AD82128_ANALOG_CTRL_REG */
#define AD82128_ANALOG_GAIN_15_5DBV	(0x0)
#define AD82128_ANALOG_GAIN_14_0DBV	(0x1)
#define AD82128_ANALOG_GAIN_13_0DBV	(0x2)
#define AD82128_ANALOG_GAIN_11_5DBV	(0x3)
#define AD82128_ANALOG_GAIN_MASK	GENMASK(1, 0)
#define AD82128_ANALOG_GAIN_SHIFT	(0x0)

/* AD82128_FAULT_REG */
#define AD82128_CLKE		BIT(2)
#define AD82128_OCE			BIT(7)
#define AD82128_DCE			BIT(4)
#define AD82128_OTE			BIT(6)

static int m_reg_tab[AD82128_REGISTER_COUNT][2] = {
	{ 0x00, 0x00 }, //##State_Control_1
	{ 0x01, 0x81 }, //##State_Control_2
	{ 0x02, 0x7f }, //##State_Control_3
	{ 0x03, 0x00 }, //##Master_volume_control
	{ 0x04, 0x18 }, //##Channel_1_volume_control
	{ 0x05, 0x18 }, //##Channel_2_volume_control
	{ 0x06, 0x18 }, //##Channel_3_volume_control
	{ 0x07, 0x18 }, //##Channel_4_volume_control
	{ 0x08, 0x18 }, //##Channel_5_volume_control
	{ 0x09, 0x18 }, //##Channel_6_volume_control
	{ 0x0a, 0x00 }, //##Reserve
	{ 0x0b, 0x00 }, //##Reserve
	{ 0x0c, 0x90 }, //##State_Control_4
	{ 0x0d, 0x80 }, //##Channel_1_configuration_registers
	{ 0x0e, 0x80 }, //##Channel_2_configuration_registers
	{ 0x0f, 0x80 }, //##Channel_3_configuration_registers
	{ 0x10, 0x80 }, //##Channel_4_configuration_registers
	{ 0x11, 0x80 }, //##Channel_5_configuration_registers
	{ 0x12, 0x80 }, //##Channel_6_configuration_registers
	{ 0x13, 0x80 }, //##Channel_7_configuration_registers
	{ 0x14, 0x80 }, //##Channel_8_configuration_registers
	{ 0x15, 0x6a }, //##Reserve
	{ 0x16, 0x6a }, //##Reserve
	{ 0x17, 0x6a }, //##Reserve
	{ 0x18, 0x6a }, //##Reserve
	{ 0x19, 0x00 }, //##Reserve
	{ 0x1a, 0x28 }, //##State_Control_5
	{ 0x1b, 0x80 }, //##State_Control_6
	{ 0x1c, 0x20 }, //##State_Control_7
	{ 0x1d, 0x7f }, //##Coefficient_RAM_Base_Address
	{ 0x1e, 0x00 }, //##First_4bits_of_coefficients_A1
	{ 0x1f, 0x00 }, //##Second_8bits_of_coefficients_A1
	{ 0x20, 0x00 }, //##Third_8bits_of_coefficients_A1
	{ 0x21, 0x00 }, //##Fourth_bits_of_coefficients_A1
	{ 0x22, 0x00 }, //##First_4bits_of_coefficients_A2
	{ 0x23, 0x00 }, //##Second_8bits_of_coefficients_A2
	{ 0x24, 0x00 }, //##Third_8bits_of_coefficients_A2
	{ 0x25, 0x00 }, //##Fourth_8bits_of_coefficients_A2
	{ 0x26, 0x00 }, //##First_4bits_of_coefficients_B1
	{ 0x27, 0x00 }, //##Second_8bits_of_coefficients_B1
	{ 0x28, 0x00 }, //##Third_8bits_of_coefficients_B1
	{ 0x29, 0x00 }, //##Fourth_8bits_of_coefficients_B1
	{ 0x2a, 0x00 }, //##First_4bits_of_coefficients_B2
	{ 0x2b, 0x00 }, //##Second_8bits_of_coefficients_B2
	{ 0x2c, 0x00 }, //##Third_8bits_of_coefficients_B2
	{ 0x2d, 0x00 }, //##Fourth_bits_of_coefficients_B2
	{ 0x2e, 0x00 }, //##First_4bits_of_coefficients_A0
	{ 0x2f, 0x80 }, //##Second_8bits_of_coefficients_A0
	{ 0x30, 0x00 }, //##Third_8bits_of_coefficients_A0
	{ 0x31, 0x00 }, //##Fourth_8bits_of_coefficients_A0
	{ 0x32, 0x00 }, //##Coefficient_RAM_RW_control
	{ 0x33, 0x06 }, //##State_Control_8
	{ 0x34, 0xf0 }, //##State_Control_9
	{ 0x35, 0x00 }, //##Volume_Fine_tune
	{ 0x36, 0x00 }, //##Volume_Fine_tune
	{ 0x37, 0x04 }, //##Device_ID_register
	{ 0x38, 0x00 }, //##Level_Meter_Clear
	{ 0x39, 0x00 }, //##Power_Meter_Clear
	{ 0x3a, 0x00 }, //##First_8bits_of_C1_Level_Meter
	{ 0x3b, 0x00 }, //##Second_8bits_of_C1_Level_Meter
	{ 0x3c, 0xc0 }, //##Third_8bits_of_C1_Level_Meter
	{ 0x3d, 0x83 }, //##Fourth_8bits_of_C1_Level_Meter
	{ 0x3e, 0x00 }, //##First_8bits_of_C2_Level_Meter
	{ 0x3f, 0x00 }, //##Second_8bits_of_C2_Level_Meter
	{ 0x40, 0xc0 }, //##Third_8bits_of_C2_Level_Meter
	{ 0x41, 0x94 }, //##Fourth_8bits_of_C2_Level_Meter
	{ 0x42, 0x00 }, //##First_8bits_of_C3_Level_Meter
	{ 0x43, 0x00 }, //##Second_8bits_of_C3_Level_Meter
	{ 0x44, 0x00 }, //##Third_8bits_of_C3_Level_Meter
	{ 0x45, 0x00 }, //##Fourth_8bits_of_C3_Level_Meter
	{ 0x46, 0x00 }, //##First_8bits_of_C4_Level_Meter
	{ 0x47, 0x00 }, //##Second_8bits_of_C4_Level_Meter
	{ 0x48, 0x00 }, //##Third_8bits_of_C4_Level_Meter
	{ 0x49, 0x00 }, //##Fourth_8bits_of_C4_Level_Meter
	{ 0x4a, 0x00 }, //##First_8bits_of_C5_Level_Meter
	{ 0x4b, 0x00 }, //##Second_8bits_of_C5_Level_Meter
	{ 0x4c, 0x00 }, //##Third_8bits_of_C5_Level_Meter
	{ 0x4d, 0x00 }, //##Fourth_8bits_of_C5_Level_Meter
	{ 0x4e, 0x00 }, //##First_8bits_of_C6_Level_Meter
	{ 0x4f, 0x00 }, //##Second_8bits_of_C6_Level_Meter
	{ 0x50, 0x00 }, //##Third_8bits_of_C6_Level_Meter
	{ 0x51, 0x00 }, //##Fourth_8bits_of_C6_Level_Meter
	{ 0x52, 0x00 }, //##First_8bits_of_C7_Level_Meter
	{ 0x53, 0x00 }, //##Second_8bits_of_C7_Level_Meter
	{ 0x54, 0x00 }, //##Third_8bits_of_C7_Level_Meter
	{ 0x55, 0x00 }, //##Fourth_8bits_of_C7_Level_Meter
	{ 0x56, 0x00 }, //##First_8bits_of_C8_Level_Meter
	{ 0x57, 0x00 }, //##Second_8bits_of_C8_Level_Meter
	{ 0x58, 0x00 }, //##Third_8bits_of_C8_Level_Meter
	{ 0x59, 0x00 }, //##Fourth_8bits_of_C8_Level_Meter
	{ 0x5a, 0x05 }, //##I2S_data_output_selection_register
	{ 0x5b, 0x00 }, //##Mono_Key_High_Byte
	{ 0x5c, 0x00 }, //##Mono_Key_Low_Byte
	{ 0x5d, 0x07 }, //##Hires_Item
	{ 0x5e, 0x00 }, //##Analog_gain
	{ 0x5f, 0x00 }, //##Reserve
	{ 0x60, 0x00 }, //##Reserve
	{ 0x61, 0x00 }, //##Reserve
	{ 0x62, 0x00 }, //##Reserve
	{ 0x63, 0x00 }, //##Reserve
	{ 0x64, 0x00 }, //##Reserve
	{ 0x65, 0x00 }, //##Reserve
	{ 0x66, 0x00 }, //##Reserve
	{ 0x67, 0x00 }, //##Reserve
	{ 0x68, 0x00 }, //##Reserve
	{ 0x69, 0x00 }, //##Reserve
	{ 0x6a, 0x00 }, //##Reserve
	{ 0x6b, 0x00 }, //##Reserve
	{ 0x6c, 0x11 }, //##FS_and_PMF_read_out
	{ 0x6d, 0x00 }, //##OC_level_setting
	{ 0x6e, 0x40 }, //##DTC_setting
	{ 0x6f, 0x74 }, //##Testmode_register0
	{ 0x70, 0x07 }, //##Reserve
	{ 0x71, 0x40 }, //##Testmode_register1
	{ 0x72, 0x38 }, //##Testmode_register2
	{ 0x73, 0x18 }, //##Dither_signal_setting
	{ 0x74, 0x06 }, //##Error_delay
	{ 0x75, 0x55 }, //##First_8bits_of_MBIST_User_Program_Even
	{ 0x76, 0x55 }, //##Second_8bits_of_MBIST_User_Program_Even
	{ 0x77, 0x55 }, //##Third_8bits_of_MBIST_User_Program_Even
	{ 0x78, 0x55 }, //##Fourth_8bits_of_MBIST_User_Program_Even
	{ 0x79, 0x55 }, //##First_8bits_of_MBIST_User_Program_Odd
	{ 0x7a, 0x55 }, //##Second_8bits_of_MBIST_User_Program_Odd
	{ 0x7b, 0x55 }, //##Third_8bits_of_MBIST_User_Program_Odd
	{ 0x7c, 0x55 }, //##Fourth_8bits_of_MBIST_User_Program_Odd
	{ 0x7d, 0xfe }, //##Error_register
	{ 0x7e, 0xfe }, //##Error_latch_register
	{ 0x7f, 0x00 }, //##Error_clear_register
	{ 0x80, 0x00 }, //##Protection_register_set
	{ 0x81, 0x00 }, //##Memory_MBIST_status
	{ 0x82, 0x00 }, //##PWM_output_control
	{ 0x83, 0x00 }, //##Testmode_control_register
	{ 0x84, 0x00 }, //##RAM1_test_register_address
	{ 0x85, 0x00 }, //##First_8bits_of_RAM1_data
};

static int m_ram1_tab[][5] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ1_A1
	{ 0x01, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ1_A2
	{ 0x02, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ1_B1
	{ 0x03, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ1_B2
	{ 0x04, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ1_A0
	{ 0x05, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ2_A1
	{ 0x06, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ2_A2
	{ 0x07, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ2_B1
	{ 0x08, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ2_B2
	{ 0x09, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ2_A0
	{ 0x0a, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ3_A1
	{ 0x0b, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ3_A2
	{ 0x0c, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ3_B1
	{ 0x0d, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ3_B2
	{ 0x0e, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ3_A0
	{ 0x0f, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ4_A1
	{ 0x10, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ4_A2
	{ 0x11, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ4_B1
	{ 0x12, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ4_B2
	{ 0x13, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ4_A0
	{ 0x14, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ5_A1
	{ 0x15, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ5_A2
	{ 0x16, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ5_B1
	{ 0x17, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ5_B2
	{ 0x18, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ5_A0
	{ 0x19, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ6_A1
	{ 0x1a, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ6_A2
	{ 0x1b, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ6_B1
	{ 0x1c, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ6_B2
	{ 0x1d, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ6_A0
	{ 0x1e, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ7_A1
	{ 0x1f, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ7_A2
	{ 0x20, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ7_B1
	{ 0x21, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ7_B2
	{ 0x22, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ7_A0
	{ 0x23, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ8_A1
	{ 0x24, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ8_A2
	{ 0x25, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ8_B1
	{ 0x26, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ8_B2
	{ 0x27, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ8_A0
	{ 0x28, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ9_A1
	{ 0x29, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ9_A2
	{ 0x2a, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ9_B1
	{ 0x2b, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ9_B2
	{ 0x2c, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ9_A0
	{ 0x2d, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ10_A1
	{ 0x2e, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ10_A2
	{ 0x2f, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ10_B1
	{ 0x30, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ10_B2
	{ 0x31, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ10_A0
	{ 0x32, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ11_A1
	{ 0x33, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ11_A2
	{ 0x34, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ11_B1
	{ 0x35, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ11_B2
	{ 0x36, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ11_A0
	{ 0x37, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ12_A1
	{ 0x38, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ12_A2
	{ 0x39, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ12_B1
	{ 0x3a, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ12_B2
	{ 0x3b, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ12_A0
	{ 0x3c, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ13_A1
	{ 0x3d, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ13_A2
	{ 0x3e, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ13_B1
	{ 0x3f, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ13_B2
	{ 0x40, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ13_A0
	{ 0x41, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ14_A1
	{ 0x42, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ14_A2
	{ 0x43, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ14_B1
	{ 0x44, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ14_B2
	{ 0x45, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ14_A0
	{ 0x46, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ15_A1
	{ 0x47, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ15_A2
	{ 0x48, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ15_B1
	{ 0x49, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ15_B2
	{ 0x4a, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ15_A0
	{ 0x4b, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ16_A1
	{ 0x4c, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ16_A2
	{ 0x4d, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ16_B1
	{ 0x4e, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_EQ16_B2
	{ 0x4f, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_EQ16_A0
	{ 0x50, 0x07, 0xff, 0xff, 0xf0 }, //##Channel_1_Mixer1
	{ 0x51, 0x00, 0x00, 0x00, 0x00 }, //##Channel_1_Mixer2
	{ 0x52, 0x00, 0x7e, 0x88, 0xe0 }, //##Channel_1_Prescale
	{ 0x53, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_Postscale
	{ 0x54, 0x02, 0x00, 0x00, 0x00 }, //##CH1.2_Power_Clipping
	{ 0x55, 0x00, 0x00, 0x01, 0xa0 }, //##Noise_Gate_Attack_Level
	{ 0x56, 0x00, 0x00, 0x05, 0x30 }, //##Noise_Gate_Release_Level
	{ 0x57, 0x00, 0x01, 0x00, 0x00 }, //##DRC1_Energy_Coefficient
	{ 0x58, 0x00, 0x01, 0x00, 0x00 }, //##DRC2_Energy_Coefficient
	{ 0x59, 0x00, 0x01, 0x00, 0x00 }, //##DRC3_Energy_Coefficient
	{ 0x5a, 0x00, 0x01, 0x00, 0x00 }, //##DRC4_Energy_Coefficient
	{ 0x5b, 0x00, 0x00, 0x19, 0x04 }, //##DRC1_Power_Meter
	{ 0x5c, 0x00, 0x00, 0x00, 0x00 }, //##DRC3_Power_Meter
	{ 0x5d, 0x00, 0x00, 0x00, 0x00 }, //##DRC5_Power_Meter
	{ 0x5e, 0x00, 0x00, 0x00, 0x00 }, //##DRC7_Power_Meter
	{ 0x5f, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_DRC_GAIN1
	{ 0x60, 0x02, 0x00, 0x00, 0x00 }, //##Channel_1_DRC_GAIN2
	{ 0x61, 0x0e, 0x00, 0x00, 0x00 }, //##Channel_1_DRC_GAIN3
	{ 0x62, 0x0e, 0x01, 0xc0, 0x70 }, //##DRC1_FF_threshold
	{ 0x63, 0x02, 0x00, 0x00, 0x00 }, //##DRC1_FF_slope
	{ 0x64, 0x00, 0x00, 0x40, 0x00 }, //##DRC1_FF_aa
	{ 0x65, 0x00, 0x00, 0x40, 0x00 }, //##DRC1_FF_da
	{ 0x66, 0x0e, 0x01, 0xc0, 0x70 }, //##DRC2_FF_threshold
	{ 0x67, 0x02, 0x00, 0x00, 0x00 }, //##DRC2_FF_slope
	{ 0x68, 0x00, 0x00, 0x40, 0x00 }, //##DRC2_FF_aa
	{ 0x69, 0x00, 0x00, 0x40, 0x00 }, //##DRC2_FF_da
	{ 0x6a, 0x0e, 0x01, 0xc0, 0x70 }, //##DRC3_FF_threshold
	{ 0x6b, 0x02, 0x00, 0x00, 0x00 }, //##DRC3_FF_slope
	{ 0x6c, 0x00, 0x00, 0x40, 0x00 }, //##DRC3_FF_aa
	{ 0x6d, 0x00, 0x00, 0x40, 0x00 }, //##DRC3_FF_da
	{ 0x6e, 0x0e, 0x01, 0xc0, 0x70 }, //##DRC4_FF_threshold
	{ 0x6f, 0x02, 0x00, 0x00, 0x00 }, //##DRC4_FF_slope
	{ 0x70, 0x00, 0x00, 0x40, 0x00 }, //##DRC4_FF_aa
	{ 0x71, 0x00, 0x00, 0x40, 0x00 }, //##DRC4_FF_da
	{ 0x72, 0x00, 0x7f, 0xf8, 0x01 }, //##DRC1_gain
	{ 0x73, 0x00, 0x00, 0x00, 0x00 }, //##DRC3_gain
	{ 0x74, 0x00, 0x00, 0x00, 0x00 }, //##DRC5_gain
	{ 0x75, 0x00, 0x00, 0x00, 0x00 }, //##DRC7_gain
	{ 0x76, 0x00, 0x80, 0x00, 0x00 }, //##I2SO_LCH_gain
	{ 0x77, 0x02, 0x00, 0x00, 0x00 }, //##SRS_gain
};

static int m_ram2_tab[][5] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ1_A1
	{ 0x01, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ1_A2
	{ 0x02, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ1_B1
	{ 0x03, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ1_B2
	{ 0x04, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ1_A0
	{ 0x05, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ2_A1
	{ 0x06, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ2_A2
	{ 0x07, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ2_B1
	{ 0x08, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ2_B2
	{ 0x09, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ2_A0
	{ 0x0a, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ3_A1
	{ 0x0b, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ3_A2
	{ 0x0c, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ3_B1
	{ 0x0d, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ3_B2
	{ 0x0e, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ3_A0
	{ 0x0f, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ4_A1
	{ 0x10, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ4_A2
	{ 0x11, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ4_B1
	{ 0x12, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ4_B2
	{ 0x13, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ4_A0
	{ 0x14, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ5_A1
	{ 0x15, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ5_A2
	{ 0x16, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ5_B1
	{ 0x17, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ5_B2
	{ 0x18, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ5_A0
	{ 0x19, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ6_A1
	{ 0x1a, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ6_A2
	{ 0x1b, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ6_B1
	{ 0x1c, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ6_B2
	{ 0x1d, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ6_A0
	{ 0x1e, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ7_A1
	{ 0x1f, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ7_A2
	{ 0x20, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ7_B1
	{ 0x21, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ7_B2
	{ 0x22, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ7_A0
	{ 0x23, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ8_A1
	{ 0x24, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ8_A2
	{ 0x25, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ8_B1
	{ 0x26, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ8_B2
	{ 0x27, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ8_A0
	{ 0x28, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ9_A1
	{ 0x29, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ9_A2
	{ 0x2a, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ9_B1
	{ 0x2b, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ9_B2
	{ 0x2c, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ9_A0
	{ 0x2d, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ10_A1
	{ 0x2e, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ10_A2
	{ 0x2f, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ10_B1
	{ 0x30, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ10_B2
	{ 0x31, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ10_A0
	{ 0x32, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ11_A1
	{ 0x33, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ11_A2
	{ 0x34, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ11_B1
	{ 0x35, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ11_B2
	{ 0x36, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ11_A0
	{ 0x37, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ12_A1
	{ 0x38, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ12_A2
	{ 0x39, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ12_B1
	{ 0x3a, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ12_B2
	{ 0x3b, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ12_A0
	{ 0x3c, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ13_A1
	{ 0x3d, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ13_A2
	{ 0x3e, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ13_B1
	{ 0x3f, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ13_B2
	{ 0x40, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ13_A0
	{ 0x41, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ14_A1
	{ 0x42, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ14_A2
	{ 0x43, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ14_B1
	{ 0x44, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ14_B2
	{ 0x45, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ14_A0
	{ 0x46, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ15_A1
	{ 0x47, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ15_A2
	{ 0x48, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ15_B1
	{ 0x49, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ15_B2
	{ 0x4a, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ15_A0
	{ 0x4b, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ16_A1
	{ 0x4c, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ16_A2
	{ 0x4d, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ16_B1
	{ 0x4e, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_EQ16_B2
	{ 0x4f, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_EQ16_A0
	{ 0x50, 0x00, 0x00, 0x00, 0x00 }, //##Channel_2_Mixer1
	{ 0x51, 0x07, 0xff, 0xff, 0xf0 }, //##Channel_2_Mixer2
	{ 0x52, 0x00, 0x7e, 0x88, 0xe0 }, //##Channel_2_Prescale
	{ 0x53, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_Postscale
	{ 0x54, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x55, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x56, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x57, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x58, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x59, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x5a, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x5b, 0x00, 0x00, 0x18, 0x88 }, //##DRC2_Power_Meter
	{ 0x5c, 0x00, 0x00, 0x00, 0x00 }, //##DRC4_Power_Mete
	{ 0x5d, 0x00, 0x00, 0x00, 0x00 }, //##DRC6_Power_Meter
	{ 0x5e, 0x00, 0x00, 0x00, 0x00 }, //##DRC8_Power_Meter
	{ 0x5f, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_DRC_GAIN1
	{ 0x60, 0x02, 0x00, 0x00, 0x00 }, //##Channel_2_DRC_GAIN2
	{ 0x61, 0x0e, 0x00, 0x00, 0x00 }, //##Channel_2_DRC_GAIN3
	{ 0x62, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x63, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x64, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x65, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x66, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x67, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x68, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x69, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x6a, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x6b, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x6c, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x6d, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x6e, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x6f, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x70, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x71, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
	{ 0x72, 0x00, 0x00, 0x00, 0x00 }, //##DRC2_gain
	{ 0x73, 0x00, 0x00, 0x00, 0x00 }, //##DRC4_gain
	{ 0x74, 0x00, 0x00, 0x00, 0x00 }, //##DRC6_gain
	{ 0x75, 0x00, 0x00, 0x00, 0x00 }, //##DRC8_gain
	{ 0x76, 0x00, 0x80, 0x00, 0x00 }, //##I2SO_RCH_gain
	{ 0x77, 0x00, 0x00, 0x00, 0x00 }, //##Reserve
};

const u32 ad82128_volume[] = {
	0xE6,		//0, -103dB
	0xE5,		//1, -102.5dB
	0xE4,		//2, -102dB
	0xE3,		//3, -101.5dB
	0xE2,		//4, -101dB
	0xE1,		//5, -100.5dB
	0xE0,		//6, -100dB
	0xDF,		//7, -99.5dB
	0xDE,		//8, -99dB
	0xDD,		//9, -98.5dB
	0xDC,		//10, -98dB
	0xDB,		//11, -97.5dB
	0xDA,		//12, -97dB
	0xD9,		//13, -96.5dB
	0xD8,		//14, -96dB
	0xD7,		//15, -95.5dB
	0xD6,		//16, -95dB
	0xD5,		//17, -94.5dB
	0xD4,		//18, -94dB
	0xD3,		//19, -93.5dB
	0xD2,		//20, -93dB
	0xD1,		//21, -92.5dB
	0xD0,		//22, -92dB
	0xCF,		//23, -91.5dB
	0xCE,		//24, -91dB
	0xCD,		//25, -90.5dB
	0xCC,		//26, -90dB
	0xCB,		//27, -89.5dB
	0xCA,		//28, -89dB
	0xC9,		//29, -88.5dB
	0xC8,		//30, -88dB
	0xC7,		//31, -87.5dB
	0xC6,		//32, -87dB
	0xC5,		//33, -86.5dB
	0xC4,		//34, -86dB
	0xC3,		//35, -85.5dB
	0xC2,		//36, -85dB
	0xC1,		//37, -84.5dB
	0xC0,		//38, -84dB
	0xBF,		//39, -83.5dB
	0xBE,		//40, -83dB
	0xBD,		//41, -82.5dB
	0xBC,		//42, -82dB
	0xBB,		//43, -81.5dB
	0xBA,		//44, -81dB
	0xB9,		//45, -80.5dB
	0xB8,		//46, -80dB
	0xB7,		//47, -79.5dB
	0xB6,		//48, -79dB
	0xB5,		//49, -78.5dB
	0xB4,		//50, -78dB
	0xB3,		//51, -77.5dB
	0xB2,		//52, -77dB
	0xB1,		//53, -76.5dB
	0xB0,		//54, -76dB
	0xAF,		//55, -75.5dB
	0xAE,		//56, -75dB
	0xAD,		//57, -74.5dB
	0xAC,		//58, -74dB
	0xAB,		//59, -73.5dB
	0xAA,		//60, -73dB
	0xA9,		//61, -72.5dB
	0xA8,		//62, -72dB
	0xA7,		//63, -71.5dB
	0xA6,		//64, -71dB
	0xA5,		//65, -70.5dB
	0xA4,		//66, -70dB
	0xA3,		//67, -69.5dB
	0xA2,		//68, -69dB
	0xA1,		//69, -68.5dB
	0xA0,		//70, -68dB
	0x9F,		//71, -67.5dB
	0x9E,		//72, -67dB
	0x9D,		//73, -66.5dB
	0x9C,		//74, -66dB
	0x9B,		//75, -65.5dB
	0x9A,		//76, -65dB
	0x99,		//77, -64.5dB
	0x98,		//78, -64dB
	0x97,		//79, -63.5dB
	0x96,		//80, -63dB
	0x95,		//81, -62.5dB
	0x94,		//82, -62dB
	0x93,		//83, -61.5dB
	0x92,		//84, -61dB
	0x91,		//85, -60.5dB
	0x90,		//86, -60dB
	0x8F,		//87, -59.5dB
	0x8E,		//88, -59dB
	0x8D,		//89, -58.5dB
	0x8C,		//90, -58dB
	0x8B,		//91, -57dB
	0x8A,		//92, -57dB
	0x89,		//93, -56.5dB
	0x88,		//94, -56dB
	0x87,		//95, -55.5dB
	0x86,		//96, -55dB
	0x85,		//97, -54.5dB
	0x84,		//98, -54dB
	0x83,		//99, -53.5dB
	0x82,		//100, -53dB
	0x81,		//101, -52.5dB
	0x80,		//102, -52dB
	0x7F,		//103, -51.5dB
	0x7E,		//104, -51dB
	0x7D,		//105, -50.5dB
	0x7C,		//106, -50dB
	0x7B,		//107, -49.5dB
	0x7A,		//108, -49dB
	0x79,		//109, -48.5dB
	0x78,		//110, -48dB
	0x77,		//111, -47.5dB
	0x76,		//112, -47dB
	0x75,		//113, -46.5dB
	0x74,		//114, -46dB
	0x73,		//115, -45.5dB
	0x72,		//116, -45dB
	0x71,		//117, -44.5dB
	0x70,		//118, -44dB
	0x6F,		//119, -43.5dB
	0x6E,		//120, -43dB
	0x6D,		//121, -42.5dB
	0x6C,		//122, -42dB
	0x6B,		//123, -41.5dB
	0x6A,		//124, -41dB
	0x69,		//125, -40.5dB
	0x68,		//126, -40dB
	0x67,		//127, -39.5dB
	0x66,		//128, -39dB
	0x65,		//129, -38.5dB
	0x64,		//130, -38dB
	0x63,		//131, -37.5dB
	0x62,		//132, -37dB
	0x61,		//133, -36.5dB
	0x60,		//134, -36dB
	0x5F,		//135, -35.5dB
	0x5E,		//136, -35dB
	0x5D,		//137, -34.5dB
	0x5C,		//138, -34dB
	0x5B,		//139, -33.5dB
	0x5A,		//140, -33dB
	0x59,		//141, -32.5dB
	0x58,		//142, -32dB
	0x57,		//143, -31.5dB
	0x56,		//144, -31dB
	0x55,		//145, -30.5dB
	0x54,		//146, -30dB
	0x53,		//147, -29.5dB
	0x52,		//148, -29dB
	0x51,		//149, -28.5dB
	0x50,		//150, -28dB
	0x4F,		//151, -27.5dB
	0x4E,		//152, -27dB
	0x4D,		//153, -26.5dB
	0x4C,		//154, -26dB
	0x4B,		//155, -25.5dB
	0x4A,		//156, -25dB
	0x49,		//157, -24.5dB
	0x48,		//158, -24dB
	0x47,		//159, -23.5dB
	0x46,		//160, -23dB
	0x45,		//161, -22.5dB
	0x44,		//162, -22dB
	0x43,		//163, -21.5dB
	0x42,		//164, -21dB
	0x41,		//165, -20.5dB
	0x40,		//166, -20dB
	0x3F,		//167, -19.5dB
	0x3E,		//168, -19dB
	0x3D,		//169, -18.5dB
	0x3C,		//170, -18dB
	0x3B,		//171, -17.5dB
	0x3A,		//172, -17dB
	0x39,		//173, -16.5dB
	0x38,		//174, -16dB
	0x37,		//175, -15.5dB
	0x36,		//176, -15dB
	0x35,		//177, -14.5dB
	0x34,		//178, -14dB
	0x33,		//179, -13.5dB
	0x32,		//180, -13dB
	0x31,		//181, -12.5dB
	0x30,		//182, -12dB
	0x2F,		//183, -11.5dB
	0x2E,		//184, -11dB
	0x2D,		//185, -10.5dB
	0x2C,		//186, -10dB
	0x2B,		//187, -9.5dB
	0x2A,		//188, -9dB
	0x29,		//189, -8.5dB
	0x28,		//190, -8dB
	0x27,		//191, -7.5dB
	0x26,		//192, -7dB
	0x25,		//193, -6.5dB
	0x24,		//194, -6dB
	0x23,		//195, -5.5dB
	0x22,		//196, -5dB
	0x21,		//197, -4.5dB
	0x20,		//198, -4dB
	0x1F,		//199, -3.5dB
	0x1E,		//200, -3dB
	0x1D,		//201, -2.5dB
	0x1C,		//202, -2dB
	0x1B,		//203, -1.5dB
	0x1A,		//204, -1dB
	0x19,		//205, -0.5dB
	0x18,		//206, 0dB
	0x17,		//207, 0.5dB
	0x16,		//208, 1dB
	0x15,		//209, 1.5dB
	0x14,		//210, 2dB
	0x13,		//211, 2.5dB
	0x12,		//212, 3dB
	0x11,		//213, 3.5dB
	0x10,		//214, 4dB
	0x0F,		//215, 4.5dB
	0x0E,		//216, 5dB
	0x0D,		//217, 5.5dB
	0x0C,		//218, 6dB
	0x0B,		//219, 6.5dB
	0x0A,		//220, 7dB
	0x09,		//221, 7.5dB
	0x08,		//222, 8dB
	0x07,		//223, 8.5dB
	0x06,		//224, 9dB
	0x05,		//225, 9.5dB
	0x04,		//226, 10dB
	0x03,		//227, 10.5dB
	0x02,		//228, 11dB
	0x01,		//229, 11.5dB
	0x00,		//230, 12dB
};

#endif /* __AD82128_H__ */
