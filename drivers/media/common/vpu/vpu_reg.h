/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 */

#ifndef __VPU_REG_H__
#define __VPU_REG_H__
#include <linux/platform_device.h>

#define VPU_REG_END                  0xffff

/* ********************************
 * register define
 * *********************************
 */

#define AO_RTI_GEN_PWR_SLEEP0        0x03a
#define AO_RTI_GEN_PWR_ISO0          0x03b

/* HIU bus */

#define HHI_MEM_PD_REG0              0x40
#define HHI_VPU_MEM_PD_REG0          0x41
#define HHI_VPU_MEM_PD_REG1          0x42
#define HHI_VPU_MEM_PD_REG2          0x4d
#define HHI_VPU_MEM_PD_REG3          0x4e
#define HHI_VPU_MEM_PD_REG4          0x4c
#define HHI_VPU_MEM_PD_REG3_SM1      0x43
#define HHI_VPU_MEM_PD_REG4_SM1      0x44

/* pwrctrl reg */
#define PWRCTRL_PWR_ACK0             0x0000
#define PWRCTRL_PWR_ACK1             0x0001
#define PWRCTRL_PWR_OFF0             0x0004
#define PWRCTRL_PWR_OFF1             0x0005
#define PWRCTRL_ISO_EN0              0x0008
#define PWRCTRL_ISO_EN1              0x0009
#define PWRCTRL_FOCRST0              0x000c
#define PWRCTRL_FOCRST1              0x000d
#define PWRCTRL_MEM_PD5_SC2          0x0015
#define PWRCTRL_MEM_PD6_SC2          0x0016
#define PWRCTRL_MEM_PD7_SC2          0x0017
#define PWRCTRL_MEM_PD8_SC2          0x0018
#define PWRCTRL_MEM_PD9_SC2          0x0019

#define PWRCTRL_MEM_PD3_T5           0x00a
#define PWRCTRL_MEM_PD4_T5           0x00b
#define PWRCTRL_MEM_PD5_T5           0x00c
#define PWRCTRL_MEM_PD6_T5           0x00d
#define PWRCTRL_MEM_PD7_T5           0x00e

#define HHI_VPU_CLK_CNTL             0x6f
#define HHI_VAPBCLK_CNTL             0x7d
#define HHI_VPU_CLK_CTRL             0x6f

#define CLKCTRL_VPU_CLK_CTRL         0x003a
#define CLKCTRL_VAPBCLK_CTRL         0x003f

#define CLKCTRL_VOUTENC_CLK_CTRL     0x0046

/* cbus */
#define RESET0_LEVEL                 0x0420
#define RESET1_LEVEL                 0x0421
#define RESET2_LEVEL                 0x0422
#define RESET3_LEVEL                 0x0423
#define RESET4_LEVEL                 0x0424
#define RESET5_LEVEL                 0x0425
#define RESET6_LEVEL                 0x0426
#define RESET7_LEVEL                 0x0427

/* vpu clk gate */
/* vcbus */
#define VPU_CLK_GATE                 0x2723

#define VPP_RDARB_MODE               0x3978
#define VPP_RDARB_REQEN_SLV          0x3979
#define VPU_RDARB_MODE_L1C1          0x2790
#define DI_RDARB_MODE_L1C1           0x2050
#define DI_RDARB_REQEN_SLV_L1C1      0x2051
#define DI_WRARB_MODE_L1C1           0x2054
#define DI_WRARB_REQEN_SLV_L1C1      0x2055
#define DI_RDARB_UGT_L1C1            0x205b
#define DI_WRARB_UGT_L1C1            0x205d
#define VPU_RDARB_MODE_L1C2          0x2799
#define VPU_RDARB_MODE_L2C1          0x279d
#define VPP_RDARB_REQEN_SLV_L2C1     0x279e
#define VPU_WRARB_MODE_L2C1          0x27a2
#define VPU_WRARB_REQEN_SLV_L2C1     0x27a3
#define VPU_RDARB_UGT_L2C1           0x27c2
#define VPU_WRARB_UGT_L2C1           0x27c3

#define VENC_VDAC_TST_VAL            0x1b7f
#define VPP_DUMMY_DATA               0x1d00
#define VPU_VPU_PWM_V0               0x1ce0

#define VPU_VOUT_BLEND_DUMDATA       0x0011
#define VPP_VD1_MATRIX_OFFSET0_1     0x0289
#define VPU_VOUT_DTH_DATA            0x0103

#define ENCP_DVI_HSO_BEGIN           0x1c30
#define S5_VPU_VPU_PWM_V0            0x2730

int vpu_ioremap(struct platform_device *pdev, int *reg_map_table);

unsigned int vpu_clk_read(unsigned int _reg);
void vpu_clk_write(unsigned int _reg, unsigned int _value);
void vpu_clk_setb(unsigned int _reg, unsigned int _value,
		  unsigned int _start, unsigned int _len);
unsigned int vpu_clk_getb(unsigned int _reg,
			  unsigned int _start, unsigned int _len);
void vpu_clk_set_mask(unsigned int _reg, unsigned int _mask);
void vpu_clk_clr_mask(unsigned int _reg, unsigned int _mask);

unsigned int vpu_pwrctrl_read(unsigned int _reg);
unsigned int vpu_pwrctrl_getb(unsigned int _reg,
			      unsigned int _start, unsigned int _len);

unsigned int vpu_ao_read(unsigned int _reg);
void vpu_ao_write(unsigned int _reg, unsigned int _value);
void vpu_ao_setb(unsigned int _reg, unsigned int _value,
		 unsigned int _start, unsigned int _len);

#endif
