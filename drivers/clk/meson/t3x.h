/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 */

#ifndef __T3X_H
#define __T3X_H

#define CLKCTRL_OSCIN_CTRL			(0x01 << 2)
#define CLKCTRL_RTC_BY_OSCIN_CTRL0		(0x02 << 2)
#define CLKCTRL_RTC_BY_OSCIN_CTRL1		(0x03 << 2)
#define CLKCTRL_RTC_CTRL			(0x04 << 2)
#define CLKCTRL_CHECK_CLK_RESULT		(0x05 << 2)
#define CLKCTRL_MBIST_ATSPEED_CTRL		(0x06 << 2)
#define CLKCTRL_LOCK_BIT_REG0			(0x08 << 2)
#define CLKCTRL_LOCK_BIT_REG1			(0x09 << 2)
#define CLKCTRL_LOCK_BIT_REG2			(0x0a << 2)
#define CLKCTRL_LOCK_BIT_REG3			(0x0b << 2)
#define CLKCTRL_PROT_BIT_REG0			(0x0c << 2)
#define CLKCTRL_PROT_BIT_REG1			(0x0d << 2)
#define CLKCTRL_PROT_BIT_REG2			(0x0e << 2)
#define CLKCTRL_PROT_BIT_REG3			(0x0f << 2)
#define CLKCTRL_SYS_CLK_CTRL0			(0x10 << 2)
#define CLKCTRL_SYS_CLK_EN0_REG0		(0x11 << 2)
#define CLKCTRL_SYS_CLK_EN0_REG1		(0x12 << 2)
#define CLKCTRL_SYS_CLK_EN0_REG2		(0x13 << 2)
#define CLKCTRL_SYS_CLK_EN0_REG3		(0x14 << 2)
#define CLKCTRL_SYS_CLK_EN1_REG0		(0x15 << 2)
#define CLKCTRL_SYS_CLK_EN1_REG1		(0x16 << 2)
#define CLKCTRL_SYS_CLK_EN1_REG2		(0x17 << 2)
#define CLKCTRL_SYS_CLK_EN1_REG3		(0x18 << 2)
#define CLKCTRL_SYS_CLK_VPU_EN0			(0x19 << 2)
#define CLKCTRL_SYS_CLK_VPU_EN1			(0x1a << 2)
#define CLKCTRL_AXI_CLK_CTRL0			(0x1b << 2)
#define CLKCTRL_SYS_WRAPPER_CLK_EN		(0x1c << 2)
#define CLKCTRL_TST_CTRL0			(0x20 << 2)
#define CLKCTRL_TST_CTRL1			(0x21 << 2)
#define CLKCTRL_CECA_CTRL0			(0x22 << 2)
#define CLKCTRL_CECA_CTRL1			(0x23 << 2)
#define CLKCTRL_CECB_CTRL0			(0x24 << 2)
#define CLKCTRL_CECB_CTRL1			(0x25 << 2)
#define CLKCTRL_SC_CLK_CTRL			(0x26 << 2)
#define CLKCTRL_DSPA_CLK_CTRL0			(0x27 << 2)
#define CLKCTRL_CLK12_24_CTRL			(0x2a << 2)
#define CLKCTRL_VID_CLK0_CTRL			(0x30 << 2)
#define CLKCTRL_VID_CLK0_CTRL2			(0x31 << 2)
#define CLKCTRL_VID_CLK0_DIV			(0x32 << 2)
#define CLKCTRL_VIID_CLK0_DIV			(0x33 << 2)
#define CLKCTRL_VIID_CLK0_CTRL			(0x34 << 2)
#define CLKCTRL_ENC0_HDMI_CLK_CTRL		(0x35 << 2)
#define CLKCTRL_ENC2_HDMI_CLK_CTRL		(0x36 << 2)
#define CLKCTRL_ENC_HDMI_CLK_CTRL		(0x37 << 2)
#define CLKCTRL_HDMI_CLK_CTRL			(0x38 << 2)
#define CLKCTRL_VID_PLL_CLK0_DIV		(0x39 << 2)
#define CLKCTRL_VPU_CLK_CTRL			(0x3a << 2)
#define CLKCTRL_VPU_CLKB_CTRL			(0x3b << 2)
#define CLKCTRL_VPU_CLKC_CTRL			(0x3c << 2)
#define CLKCTRL_VID_LOCK_CLK_CTRL		(0x3d << 2)
#define CLKCTRL_VDIN_MEAS_CLK_CTRL		(0x3e << 2)
#define CLKCTRL_VAPBCLK_CTRL			(0x3f << 2)
#define CLKCTRL_DSC_CLK_CTRL			(0x40 << 2)
#define CLKCTRL_CDAC_CLK_CTRL			(0x42 << 2)
#define CLKCTRL_GE2DCLK_CTRL			(0x43 << 2)
#define CLKCTRL_HTX_CLK_CTRL0			(0x47 << 2)
#define CLKCTRL_HTX_CLK_CTRL1			(0x48 << 2)
#define CLKCTRL_HRX_CLK_CTRL0			(0x4a << 2)
#define CLKCTRL_HRX_CLK_CTRL1			(0x4b << 2)
#define CLKCTRL_HRX_CLK_CTRL2			(0x4c << 2)
#define CLKCTRL_HRX_CLK_CTRL3			(0x4d << 2)
#define CLKCTRL_HRX_CLK_CTRL4			(0x4e << 2)
#define CLKCTRL_MALI_CLK_CTRL			(0x4f << 2)
#define CLKCTRL_VDEC_CLK_CTRL			(0x50 << 2)
#define CLKCTRL_VDEC2_CLK_CTRL			(0x51 << 2)
#define CLKCTRL_VDEC3_CLK_CTRL			(0x52 << 2)
#define CLKCTRL_VDEC4_CLK_CTRL			(0x53 << 2)
#define CLKCTRL_VC9000E_CLK_CTRL		(0x54 << 2)
#define CLKCTRL_CMPR_CLK_CTRL			(0x55 << 2)
#define CLKCTRL_TS_CLK_CTRL			(0x56 << 2)
#define CLKCTRL_USB_CLK_CTRL			(0x57 << 2)
#define CLKCTRL_USB_CLK_CTRL1			(0x58 << 2)
#define CLKCTRL_ETH_CLK_CTRL			(0x59 << 2)
#define CLKCTRL_NAND_CLK_CTRL			(0x5a << 2)
#define CLKCTRL_SD_EMMC_CLK_CTRL		(0x5b << 2)
#define CLKCTRL_SPICC_CLK_CTRL			(0x5d << 2)
#define CLKCTRL_GEN_CLK_CTRL			(0x5e << 2)
#define CLKCTRL_SAR_CLK_CTRL0			(0x5f << 2)
#define CLKCTRL_PWM_CLK_AB_CTRL			(0x60 << 2)
#define CLKCTRL_PWM_CLK_CD_CTRL			(0x61 << 2)
#define CLKCTRL_PWM_CLK_EF_CTRL			(0x62 << 2)
#define CLKCTRL_PWM_CLK_GH_CTRL			(0x63 << 2)
#define CLKCTRL_PWM_CLK_IJ_CTRL			(0x64 << 2)
#define CLKCTRL_SPICC_CLK_CTRL1			(0x70 << 2)
#define CLKCTRL_SPICC_CLK_CTRL2			(0x71 << 2)
#define CLKCTRL_VID_CLK1_CTRL			(0x73 << 2)
#define CLKCTRL_VID_CLK1_CTRL2			(0x74 << 2)
#define CLKCTRL_VID_CLK1_DIV			(0x75 << 2)
#define CLKCTRL_VIID_CLK1_DIV			(0x76 << 2)
#define CLKCTRL_VIID_CLK1_CTRL			(0x77 << 2)
#define CLKCTRL_VID_CLK2_CTRL			(0x78 << 2)
#define CLKCTRL_VID_CLK2_CTRL2			(0x79 << 2)
#define CLKCTRL_VID_CLK2_DIV			(0x7a << 2)
#define CLKCTRL_VIID_CLK2_DIV			(0x7b << 2)
#define CLKCTRL_VIID_CLK2_CTRL			(0x7c << 2)
#define CLKCTRL_VID_PLL_CLK1_DIV		(0x7d << 2)
#define CLKCTRL_VID_PLL_CLK2_DIV		(0x7e << 2)
#define CLKCTRL_HDMI_VID_PLL_CLK_DIV		(0x81 << 2)
#define CLKCTRL_NNA_CLK_CTRL0			(0x82 << 2)
#define CLKCTRL_NNA_CLK_CTRL1			(0x83 << 2)
#define CLKCTRL_HDMI_FPLL_CLK_DIV		(0x85 << 2)
#define CLKCTRL_HDMI_PLL_TMDS_CLK_DIV		(0x86 << 2)
#define CLKCTRL_TCON_CLK_CNTL			(0x87 << 2)
#define CLKCTRL_NNA_CLK_CNTL			(0x88 << 2)
#define CLKCTRL_ME_CLK_CNTL			(0x89 << 2)
#define CLKCTRL_FRC_CLK_CNTL			(0x8a << 2)
#define CLKCTRL_USB_CLK_CNTL			(0x8b << 2)
#define CLKCTRL_TSIN_CLK_CNTL			(0x8c << 2)
#define CLKCTRL_DEMOD_CLK_CNTL			(0x8d << 2)
#define CLKCTRL_DEMOD_CLK_CNTL1			(0x8e << 2)
#define CLKCTRL_DEMOD_32K_CNTL0			(0x8f << 2)
#define CLKCTRL_DEMOD_32K_CNTL1			(0x90 << 2)
#define CLKCTRL_ATV_DMD_SYS_CLK_CNTL		(0x91 << 2)

/* ANA_CTRL - Registers
 * REG_BASE:  REGISTER_BASE_ADDR = 0xfe008000
 */
#define CLKCTRL_SYS0PLL_CTRL0			(0x200 << 2)
#define CLKCTRL_SYS0PLL_CTRL1			(0x201 << 2)
#define CLKCTRL_SYS0PLL_CTRL2			(0x202 << 2)
#define CLKCTRL_SYS0PLL_CTRL3			(0x203 << 2)
#define CLKCTRL_SYS0PLL_STS			(0x204 << 2)
#define CLKCTRL_SYS1PLL_CTRL0			(0x210 << 2)
#define CLKCTRL_SYS1PLL_CTRL1			(0x211 << 2)
#define CLKCTRL_SYS1PLL_CTRL2			(0x212 << 2)
#define CLKCTRL_SYS1PLL_CTRL3			(0x213 << 2)
#define CLKCTRL_SYS1PLL_STS			(0x214 << 2)
#define CLKCTRL_FIXPLL_CTRL0			(0x220 << 2)
#define CLKCTRL_FIXPLL_CTRL1			(0x221 << 2)
#define CLKCTRL_FIXPLL_CTRL2			(0x222 << 2)
#define CLKCTRL_FIXPLL_CTRL3			(0x223 << 2)
#define CLKCTRL_FIXPLL_STS			(0x227 << 2)
#define CLKCTRL_GP0PLL_CTRL0			(0x240 << 2)
#define CLKCTRL_GP0PLL_CTRL1			(0x241 << 2)
#define CLKCTRL_GP0PLL_CTRL2			(0x242 << 2)
#define CLKCTRL_GP0PLL_CTRL3			(0x243 << 2)
#define CLKCTRL_GP0PLL_STS			(0x247 << 2)
#define CLKCTRL_GP1PLL_CTRL0			(0x280 << 2)
#define CLKCTRL_GP1PLL_CTRL1			(0x281 << 2)
#define CLKCTRL_GP1PLL_CTRL2			(0x282 << 2)
#define CLKCTRL_GP1PLL_CTRL3			(0x283 << 2)
#define CLKCTRL_GP1PLL_STS			(0x284 << 2)
#define CLKCTRL_GP2PLL_CTRL0			(0x285 << 2)
#define CLKCTRL_GP2PLL_CTRL1			(0x286 << 2)
#define CLKCTRL_GP2PLL_CTRL2i			(0x287 << 2)
#define CLKCTRL_GP2PLL_CTRL3			(0x288 << 2)
#define CLKCTRL_GP2PLL_STS			(0x289 << 2)
#define CLKCTRL_SYS2PLL_CTRL0			(0x290 << 2)
#define CLKCTRL_SYS2PLL_CTRL1			(0x291 << 2)
#define CLKCTRL_SYS2PLL_CTRL2			(0x292 << 2)
#define CLKCTRL_SYS2PLL_CTRL3			(0x293 << 2)
#define CLKCTRL_SYS2PLL_STS			(0x294 << 2)
#define CLKCTRL_SYS3PLL_CTRL0			(0x295 << 2)
#define CLKCTRL_SYS3PLL_CTRL1			(0x296 << 2)
#define CLKCTRL_SYS3PLL_CTRL2			(0x297 << 2)
#define CLKCTRL_SYS3PLL_CTRL3			(0x298 << 2)
#define CLKCTRL_SYS3PLL_STS			(0x299 << 2)
#define CLKCTRL_HIFIPLL_CTRL0			(0x2a0 << 2)
#define CLKCTRL_HIFIPLL_CTRL1			(0x2a1 << 2)
#define CLKCTRL_HIFIPLL_CTRL2			(0x2a2 << 2)
#define CLKCTRL_HIFIPLL_CTRL3			(0x2a3 << 2)
#define CLKCTRL_HIFIPLL_STS			(0x2a7 << 2)
#define CLKCTRL_HIFI1PLL_CTRL0			(0x2b0 << 2)
#define CLKCTRL_HIFI1PLL_CTRL1			(0x2b1 << 2)
#define CLKCTRL_HIFI1PLL_CTRL2			(0x2b2 << 2)
#define CLKCTRL_HIFI1PLL_CTRL3			(0x2b3 << 2)
#define CLKCTRL_HIFI1PLL_STS			(0x2b4 << 2)
#define CLKCTRL_FPLL_CTRL0			(0x2c0 << 2)
#define CLKCTRL_FPLL_CTRL1			(0x2c1 << 2)
#define CLKCTRL_FPLL_CTRL2			(0x2c2 << 2)
#define CLKCTRL_FPLL_CTRL3			(0x2c3 << 2)
#define CLKCTRL_FPLL_STS			(0x2c4 << 2)
#define CLKCTRL_NNAPLL_CTRL0			(0x2d0 << 2)
#define CLKCTRL_NNAPLL_CTRL1			(0x2d1 << 2)
#define CLKCTRL_NNAPLL_CTRL2			(0x2d2 << 2)
#define CLKCTRL_NNAPLL_CTRL3			(0x2d3 << 2)
#define CLKCTRL_NNAPLL_STS			(0x2d4 << 2)
#define CLKCTRL_HDMI_PLL0_CTRL0			(0x2e0 << 2)
#define CLKCTRL_HDMI_PLL0_CTRL1			(0x2e1 << 2)
#define CLKCTRL_HDMI_PLL0_CTRL2			(0x2e2 << 2)
#define CLKCTRL_HDMI_PLL0_CTRL3			(0x2e3 << 2)
#define CLKCTRL_HDMI_PLL0_STS			(0x2e4 << 2)
#define CLKCTRL_HDMI_PLL1_CTRL0			(0x2e5 << 2)
#define CLKCTRL_HDMI_PLL1_CTRL1			(0x2e6 << 2)
#define CLKCTRL_HDMI_PLL1_CTRL2			(0x2e7 << 2)
#define CLKCTRL_HDMI_PLL1_CTRL3			(0x2e8 << 2)
#define CLKCTRL_HDMI_PLL1_STS			(0x2e9 << 2)
#define CLKCTRL_AUD21_PLL_CTRL0			(0x2ea << 2)
#define CLKCTRL_AUD21_PLL_CTRL1			(0x2eb << 2)
#define CLKCTRL_AUD21_PLL_CTRL2			(0x2ec << 2)
#define CLKCTRL_AUD21_PLL_CTRL3			(0x2ed << 2)
#define CLKCTRL_AUD21_PLL_STS			(0x2ee << 2)
#define CLKCTRL_PIX_PLL_CTRL0			(0x2f0 << 2)
#define CLKCTRL_PIX_PLL_CTRL1			(0x2f1 << 2)
#define CLKCTRL_PIX_PLL_CTRL2			(0x2f2 << 2)
#define CLKCTRL_PIX_PLL_CTRL3			(0x2f3 << 2)
#define CLKCTRL_PIX_PLL_STS			(0x2f4 << 2)

/* CPU_CTRL
 * REG_BASE:  REGISTER_BASE_ADDR = 0xfe00e000
 */
#define CPUCTRL_SYS_CPU_CLK_CTRL0		(0x51 << 2)
#define CPUCTRL_SYS_CPU_CLK_CTRL1		(0x52 << 2)
#define CPUCTRL_SYS_CPU_CLK_CTRL2		(0x53 << 2)
#define CPUCTRL_SYS_CPU_CLK_CTRL3		(0x54 << 2)
#define CPUCTRL_SYS_CPU_CLK_CTRL4		(0x55 << 2)
#define CPUCTRL_SYS_CPU_CLK_CTRL5		(0x56 << 2)
#define CPUCTRL_SYS_CPU_CLK_CTRL6		(0x57 << 2)
#define CPUCTRL_SYS_CPU_CLK_CTRL7		(0x58 << 2)
#define CPUCTRL_SYS_CPU_CLK_CTRL8		(0x59 << 2)
#define CPUCTRL_SYS_CPU_CLK_CTRL9		(0x5a << 2)
#define CPUCTRL_SYS_CPU_CLK_CTRL10		(0x5b << 2)
#define CPUCTRL_SYS_CPU_CLK_RESULT		(0x5c << 2)

#define CLKCTRL_PCIEPLL_CTRL0			(0x50 << 2)
#define CLKCTRL_PCIEPLL_CTRL1			(0x51 << 2)
#define CLKCTRL_PCIEPLL_CTRL2			(0x52 << 2)
#define CLKCTRL_PCIEPLL_CTRL3			(0x53 << 2)
/* TODO */
#define SECURE_PLL_CLK	0x82000098
#define SECURE_CPU_CLK	0x82000099

/* PLL secure clock index */
enum sec_pll {
	SECID_SYS0_DCO_PLL = 0,
	SECID_SYS0_DCO_PLL_DIS,
	SECID_SYS0_PLL_OD,
	SECID_SYS1_DCO_PLL,
	SECID_SYS1_DCO_PLL_DIS,
	SECID_SYS1_PLL_OD,
	SECID_CPU_CLK_SEL,
	SECID_CPU_CLK_RD,
	SECID_CPU_CLK_DYN,
	SECID_A76_CLK_SEL,
	SECID_A76_CLK_RD,
	SECID_A76_CLK_DYN,
	SECID_DSU_CLK_SEL,
	SECID_DSU_CLK_RD,
	SECID_DSU_CLK_DYN,
	SECID_DSU_FINAL_CLK_SEL,
	SECID_DSU_FINAL_CLK_RD,
	SECID_SYS_CLK_RD,
	SECID_SYS_CLK_DYN,
	SECID_AXI_CLK_RD,
	SECID_AXI_CLK_DYN,
	SECID_SYS2_DCO_PLL,
	SECID_SYS2_DCO_PLL_DIS,
	SECID_SYS2_PLL_OD,
	SECID_SYS3_DCO_PLL,
	SECID_SYS3_DCO_PLL_DIS,
	SECID_SYS3_PLL_OD,
};

#endif /* __T3X_H */
