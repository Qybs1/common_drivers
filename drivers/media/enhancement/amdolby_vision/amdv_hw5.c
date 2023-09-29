// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/amlogic/media/video_sink/video.h>
#include <linux/amlogic/media/vout/vout_notify.h>

#include "../amvecm/arch/vpp_regs.h"
#include "../amvecm/arch/vpp_hdr_regs.h"
#include "../amvecm/arch/vpp_dolbyvision_regs.h"
#include "../amvecm/amcsc.h"
#include "../amvecm/reg_helper.h"
#include <linux/amlogic/media/registers/regs/viu_regs.h>
#include <linux/amlogic/media/amdolbyvision/dolby_vision.h>
#include <linux/amlogic/media/vpu/vpu.h>
#include <linux/dma-map-ops.h>
#include <linux/amlogic/iomap.h>
#include "md_config.h"
#include <linux/of.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/arm-smccc.h>
#include <linux/amlogic/media/vout/lcd/lcd_notify.h>
#include <linux/amlogic/media/lut_dma/lut_dma.h>

#include "amdv.h"
#include "amdv_hw5.h"
#include "amdv_regs_hw5.h"
#include "mmu_config.h"

#include "dw_data.h"
#ifdef CONFIG_AMLOGIC_MEDIA_CODEC_MM
#include <linux/amlogic/media/codec_mm/codec_mm.h>
#endif

bool lut_trigger_by_reg = true;/*false: dma lut trigger by vsync*/
bool force_enable_top12_lut = true;

struct top1_pyramid_addr top1_py_addr;
struct dolby5_top1_type top1_type;
static u32 isr_cnt;
bool enable_top1;//todo
bool top1_done;
bool ignore_top1_result;
u32 l1l4_wr_index;
u32 l1l4_rd_index;
static bool new_top1_toggle;
static int last_int_top1 = 0x8;/*bit3 intensity_frm_wr_done*/
static int last_int_top1b = 0x8;/*bit3 pyramid_frm_wr_done*/
static int last_int_top2 = 0x10;/*bit4 out_frm_wr_done*/

static u32 last_py_level = NO_LEVEL;
u32 py_level = NO_LEVEL;/*todo*/
bool py_enabled = true;/*when top1 on,enable pyramid by default.some idk case disable pyramid*/
bool l1l4_enabled = true;/*when top1 on,enable l1l4 by default.some idk case disable l1l4*/
u32 l1l4_distance;
struct vd_proc_info_t *vd_proc_info;

u32 test_dv;
module_param(test_dv, uint, 0664);
MODULE_PARM_DESC(test_dv, "\n test_dv\n");

/*1:case0 480x270 420-8bit read from array*/
/*2:case0 480x270 420-8bit read from file*/
/*3:case5344 1080p 444-10  read from file*/
/*4:case5363 1080x1080 444-10  read from file*/
u32 fix_data;
module_param(fix_data, uint, 0664);
MODULE_PARM_DESC(fix_data, "\n fix_data\n");

/*bit0: little endian=0 bit1: little endian=1*/
/*bit2: cntl_64bit_rev=0 bit3:cntl_64bit_rev=1*/
/*bit4-7: cntl_color_map*/
/*bit8-11: rdmif_stride*/
uint debug_rdmif;
module_param(debug_rdmif, uint, 0664);
MODULE_PARM_DESC(debug_rdmif, "\n debug_rdmif\n");

u32 dump_pyramid;
u32 top1_crc_rd;

ulong fixed_y_buf_paddr;
ulong fixed_uv_buf_paddr;
u8 *y_vaddr;
u8 *uv_vaddr;

void fixed_buf_config(void)
{
	u32 y_buf_size;
	u32 uv_buf_size;

	y_buf_size = 512 * 270;
	uv_buf_size = 512 * 135;

	if (fix_data == CASE5344_TOP1_READFROM_FILE)
		y_buf_size = 1920 * 1080 * 4;
	else if (fix_data == CASE5363_TOP1_READFROM_FILE)
		y_buf_size = 540 * 540 * 4;

#ifdef CONFIG_AMLOGIC_MEDIA_CODEC_MM
	fixed_y_buf_paddr = codec_mm_alloc_for_dma("amdv",
		(y_buf_size + PAGE_SIZE - 1) / PAGE_SIZE, 0, CODEC_MM_FLAGS_DMA);
	pr_info("fixed_y_buf_paddr : %lx\n", fixed_y_buf_paddr);

	fixed_uv_buf_paddr = codec_mm_alloc_for_dma("amdv",
		(uv_buf_size + PAGE_SIZE - 1) / PAGE_SIZE, 0, CODEC_MM_FLAGS_DMA);
	pr_info("fixed_uv_buf_paddr : %lx\n", fixed_uv_buf_paddr);

	y_vaddr = codec_mm_vmap(fixed_y_buf_paddr, y_buf_size);
	uv_vaddr = codec_mm_vmap(fixed_uv_buf_paddr, uv_buf_size);


	if (fix_data == CASE0_TOP1_READFROM_ARRAY) {
		memcpy(y_vaddr, y_data, y_buf_size);
		memcpy(uv_vaddr, uv_data, uv_buf_size);

		codec_mm_dma_flush(y_vaddr, y_buf_size, DMA_TO_DEVICE);
		codec_mm_unmap_phyaddr(y_vaddr);
		codec_mm_dma_flush(uv_vaddr, uv_buf_size, DMA_TO_DEVICE);
		codec_mm_unmap_phyaddr(uv_vaddr);
	}

#endif
	pr_info("fixed buf config done\n");
}

void dump_pyramid_buf(unsigned int idx)
{
#ifdef CONFIG_AMLOGIC_ENABLE_VIDEO_PIPELINE_DUMP_DATA
	u8 *data_addr;
	unsigned int data_size;
	char name_buf[32];
	struct file *fp = NULL;
	loff_t pos;
	int i;

	if (idx < 1 || idx > 7) {
		pr_info("pyramid index error : %d\n", idx);
		return;
	}
	for (i = 0; i < 2; i++) {
		switch (idx) {
		case 1:
			data_addr = (u8 *)py_addr[i].py_vaddr[0];
			data_size = py_addr[i].top1_py_size[0];
			break;
		case 2:
			data_addr = (u8 *)py_addr[i].py_vaddr[1];
			data_size = py_addr[i].top1_py_size[1];
			break;
		case 3:
			data_addr = (u8 *)py_addr[i].py_vaddr[2];
			data_size = py_addr[i].top1_py_size[2];
			break;
		case 4:
			data_addr = (u8 *)py_addr[i].py_vaddr[3];
			data_size = py_addr[i].top1_py_size[3];
			break;
		case 5:
			data_addr = (u8 *)py_addr[i].py_vaddr[4];
			data_size = py_addr[i].top1_py_size[4];
			break;
		case 6:
			data_addr = (u8 *)py_addr[i].py_vaddr[5];
			data_size = py_addr[i].top1_py_size[5];
			break;
		case 7:
			data_addr = (u8 *)py_addr[i].py_vaddr[6];
			data_size = py_addr[i].top1_py_size[6];
			break;
		default:
			break;
		}

		snprintf(name_buf, sizeof(name_buf), "/mnt/pyramid_%d_%d.yuv", idx, i);
		fp = filp_open(name_buf, O_CREAT | O_RDWR, 0644);
		if (IS_ERR(fp)) {
			pr_info("%s: fp dump failed.\n", __func__);
			return;
		}

		pos = fp->f_pos;
		kernel_write(fp, data_addr, data_size, &pos);
		fp->f_pos = pos;
		pr_info("%s: write %d, %u size.\n", __func__, idx, data_size);
		filp_close(fp, NULL);
	}
#endif
}

void top1_crc_ro(void)
{
	u32 in_crc;
	u32 out_crc;

	if (top1_crc_rd) {
		VSYNC_WR_DV_REG_BITS(DOLBY5_CORE1_CRC_CNTRL, 1, 0, 1);
		in_crc = READ_VPP_DV_REG(DOLBY5_CORE1_CRC_IN_FRM);
		out_crc = READ_VPP_DV_REG(DOLBY5_CORE1_CRC_OUT_FRM);
		if (debug_dolby & 0x80000)
			pr_info("top1: in crc: 0x%x, o crc: 0x%x\n", in_crc, out_crc);
	}
}

static void dolby5_top1_rdmif
	(int hsize,
	int vsize,
	int bit_mode,
	int fmt_mode,
	int block_mode,
	ulong baddr[3],
	int stride[3])
{
	unsigned int separate_en = 0; //420
	unsigned int yuv444 = 0; //444
	unsigned int yuv420 = 0;

	unsigned int luma_xbgn = 0;
	unsigned int luma_xend = hsize - 1;
	unsigned int luma_ybgn = 0;
	unsigned int luma_yend = vsize - 1;

	unsigned int chrm_xbgn = 0;
	unsigned int chrm_xend = 0;
	unsigned int chrm_ybgn = 0;
	unsigned int chrm_yend = 0;

	unsigned int luma_hsize = 0;
	unsigned int chrm_hsize = 0;

	unsigned int cntl_bits_mode;
	unsigned int cntl_pixel_bytes;
	unsigned int cntl_color_map = 0; //1: 420 nv12, 2 420 nv21, 0: 444/422
	unsigned int cntl_bit16_mode = 0;
	unsigned int cvfmt_en = 0; //420
	unsigned int chfmt_en = 0; //420/422
	unsigned int yc_ratio = 0;
	unsigned int little_endian = 1;/*1:hevc 420-10 little endian 128bit read*/
	unsigned int cntl_64bit_rev = 0;/*0 for h265 420-10*/
	u32 pic_32byte_aligned = 0;

	if (fix_data)
		block_mode = 0;
	if (bit_mode == 1)/*8bit, h264, no dw*/
		little_endian = 0;/*match with VD1_IF0_GEN_REG bit4*/

	if (fmt_mode == 2)
		separate_en = 1;

	if (fmt_mode == 0) {
		yuv444 = 1;
	} else if (fmt_mode == 2) {/*420*/
		yuv420 = 1;
		cntl_color_map = 2;//2 for h265 dw 420-10
		if (bit_mode == 1) {/*8bit, h264*/
			cntl_color_map = 1;/*match with VD1_IF0_GEN_REG2 bit0-1*/
			cntl_64bit_rev = 1;/*match with VD1_IF0_GEN_REG3 bit0*/
		}
		cvfmt_en = 1;
		chfmt_en = 1;
	} else {
		chfmt_en = 1;
	}

	if (debug_rdmif & 0xf0)
		cntl_color_map = (debug_rdmif >> 4) & 0xf;
	if (yuv444 == 1)
		chrm_xend = hsize - 1;
	else
		chrm_xend = (hsize + 1) / 2 - 1;

	if (yuv420 == 1)
		chrm_yend = (vsize + 1) / 2 - 1;
	else
		chrm_yend = vsize - 1;

	luma_hsize = hsize;
	chrm_hsize = chrm_xend - chrm_xbgn + 1;

	if (chfmt_en == 1)
		yc_ratio = 1;
	else
		yc_ratio = 0;

	VSYNC_WR_DV_REG(DOLBY_TOP1_LUMA_X, luma_xend << 16 | luma_xbgn);
	VSYNC_WR_DV_REG(DOLBY_TOP1_LUMA_Y, luma_yend << 16 | luma_ybgn);
	VSYNC_WR_DV_REG(DOLBY_TOP1_CHROMA_X, chrm_xend << 16 | chrm_xbgn);
	VSYNC_WR_DV_REG(DOLBY_TOP1_CHROMA_Y, chrm_yend << 16 | chrm_ybgn);

	switch ((bit_mode << 4) | fmt_mode) {
	case 0x0:
		cntl_bits_mode = 2;
		cntl_pixel_bytes = 2;
		break; //10bit 444
	case 0x01:
		cntl_bits_mode = 1;
		cntl_pixel_bytes = 1;
		break; //10bit 422
	case 0x02:
		cntl_bits_mode = 0;//p010 mode(10bit valid data + 6bit padding0)
		cntl_pixel_bytes = 1;
		cntl_bit16_mode = 1;//p010,16bit
		if (fix_data == CASE5344_TOP1_READFROM_FILE ||
			fix_data == CASE5363_TOP1_READFROM_FILE) {
			cntl_bits_mode = 3;
			cntl_pixel_bytes = 1;
			cntl_bit16_mode = 0;
		}
		break; //10bit 420
	case 0x10:
		cntl_bits_mode = 0;
		cntl_pixel_bytes = 2;
		break; //8bit 444
	case 0x11:
		cntl_bits_mode = 0;
		cntl_pixel_bytes = 1;
		break; //8bit 422
	case 0x12:
		cntl_bits_mode = 0;
		cntl_pixel_bytes = 1;
		break; //8bit 420
	default:
		cntl_bits_mode = 2;
		cntl_pixel_bytes = 2;
		break; //10bit 444
	}

	if ((debug_rdmif & 1))
		VSYNC_WR_DV_REG(DOLBY_TOP1_GEN_REG,
			(1 << 25) |                //luma last line end mode
			(0 << 19) |                //cntl hold lines
			(yuv444 << 16) |           //demux_mode
			(cntl_pixel_bytes << 14) | //cntl_pixel_bytes
			(0 << 4) |                 //little endian 128bit read
			(separate_en << 1) |       //separate_en
			1);
	else if ((debug_rdmif & 2))
		VSYNC_WR_DV_REG(DOLBY_TOP1_GEN_REG,
			(1 << 25) |                //luma last line end mode
			(0 << 19) |                //cntl hold lines
			(yuv444 << 16) |           //demux_mode
			(cntl_pixel_bytes << 14) | //cntl_pixel_bytes
			(1 << 4) |                 //little endian 128bit read
			(separate_en << 1) |       //separate_en
			1);
	else
		VSYNC_WR_DV_REG(DOLBY_TOP1_GEN_REG,
			(1 << 25) |                //luma last line end mode
			(0 << 19) |                //cntl hold lines
			(yuv444 << 16) |           //demux_mode
			(cntl_pixel_bytes << 14) | //cntl_pixel_bytes
			(little_endian << 4) |     //little endian 128bit read
			(separate_en << 1) |       //separate_en
			1);                        //cntl_enable

	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_GEN_REG2, cntl_color_map, 0, 2);
	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_GEN_REG3, cntl_bits_mode, 8, 2);
	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_GEN_REG3, cntl_bit16_mode, 7, 1);

	if ((debug_rdmif & 4))
		VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_GEN_REG3, 0, 0, 1);//cntl_64bit_rev 2x64 bit revert
	else if ((debug_rdmif & 8))
		VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_GEN_REG3, 1, 0, 1);//cntl_64bit_rev 2x64 bit revert
	else
		VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_GEN_REG3, cntl_64bit_rev, 0, 1);//cntl_64bit_rev

	if (block_mode)
		pic_32byte_aligned = 7;
	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_GEN_REG3, (block_mode & 0x3), 1, 2);
	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_GEN_REG3, block_mode, 12, 2);
	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_GEN_REG3, block_mode, 14, 2);
	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_GEN_REG3, (pic_32byte_aligned << 6) |
		(block_mode << 4) |
		(block_mode << 2) |
		(block_mode << 0),
		18,
		9);

	VSYNC_WR_DV_REG(DOLBY_TOP1_FMT_CTRL,
		(1 << 28) | //hfmt repeat
		(yc_ratio << 21) |
		(chfmt_en << 20) |
		(1 << 19) | //vfmt repeat
		(1 << 17) |
		(0 << 8) |
		(16 >> yc_ratio << 1) |
		cvfmt_en);

	VSYNC_WR_DV_REG(DOLBY_TOP1_FMT_W, (luma_hsize << 16) | chrm_hsize);

	if (fix_data) {
		VSYNC_WR_DV_REG(DOLBY_TOP1_BADDR_Y, fixed_y_buf_paddr >> 4);
		if (fix_data != CASE5344_TOP1_READFROM_FILE &&
			fix_data != CASE5363_TOP1_READFROM_FILE)/*p444,one plane*/
			VSYNC_WR_DV_REG(DOLBY_TOP1_BADDR_CB, fixed_uv_buf_paddr >> 4);
	} else {
		VSYNC_WR_DV_REG(DOLBY_TOP1_BADDR_Y, baddr[0] >> 4);
		VSYNC_WR_DV_REG(DOLBY_TOP1_BADDR_CB, baddr[1] >> 4);
	}
	VSYNC_WR_DV_REG(DOLBY_TOP1_BADDR_CR, baddr[2] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP1_STRIDE_0, (stride[1] << 16) | stride[0]); //stride_u | stride_y
	VSYNC_WR_DV_REG(DOLBY_TOP1_STRIDE_1,
		VSYNC_RD_DV_REG(DOLBY_TOP1_STRIDE_1) |
		(1 << 16) |
		stride[2]); //acc_mode | stride_v
	//if (fix_data && y_vaddr) {
	//	u32 *p_buf = (u32 *)y_vaddr;
	//	u64 *p_buf_64 = (u64 *)y_vaddr;
	//	pr_dv_dbg("read p_buf_8bit %x %x %x %x %x %x %x %x\n",
	//		y_vaddr[0],y_vaddr[1],y_vaddr[2],y_vaddr[3],
	//		y_vaddr[4],y_vaddr[5],y_vaddr[6],y_vaddr[7],
	//		y_vaddr[8],y_vaddr[9],y_vaddr[10],y_vaddr[11]);
	//	pr_dv_dbg("p_buf_32bit %x %x %x %x, end: %x %x\n", p_buf[0], p_buf[1],p_buf[2],
	//				p_buf[3],p_buf_64[34552-2],p_buf_64[34552-1]);
	//	pr_dv_dbg("p_buf_64bit %llx %llx %llx\n", p_buf_64[0], p_buf_64[1], p_buf_64[2]);
	//}
};

/*check pyramid is enable in cfg case5350 5353 5354*/
/*if disabled, we force enable pyramid for top1+top1b due to hw5 not support bypass top1b*/
static void check_pr_enabled_in_cfg(void)
{
	bool pr_enabled = true;
	bool l1l4 = true;

	if (tv_hw5_setting && tv_hw5_setting->pq_config) {
		l1l4 = tv_hw5_setting->pq_config->tdc.ana_config.enalbe_l1l4_gen;
		pr_enabled = tv_hw5_setting->pq_config->tdc.pr_config.supports_precision_rendering;

		if (pr_enabled && tv_hw5_setting->dynamic_cfg)
			pr_enabled = pr_enabled &&
				!((tv_hw5_setting->dynamic_cfg->update_flag & ((u32)(1 << 5))) &&
				!tv_hw5_setting->dynamic_cfg->precision_rendering_mode);
	}
	if (!pr_enabled && (debug_dolby & 0x80000))
		pr_dv_dbg("top1 enabled but pyramid disabled!\n");

	py_enabled = pr_enabled;
	l1l4_enabled = l1l4;
	if (!l1l4_enabled)
		l1l4_distance = 0;
	else if (py_enabled)
		l1l4_distance = 1;
	else
		l1l4_distance = 2;
}

/*Check pyramid is enable in reg, case5343*/
/*For low res(w<480), even if cfg enable pyramid, controlpath will disable*/
/*intensity image and pyramid*/
static void check_pr_enabled_in_reg(u32 *p_reg_top1, u32 *p_reg_top1b)
{
	int reg_val, reg_addr;
	int reg_val_b, reg_addr_b;

	reg_val = p_reg_top1[2 * 2];
	reg_val = reg_val & 0xffffffff;
	reg_addr = p_reg_top1[2 * 2 + 1];
	reg_addr = reg_addr & 0xffff;
	reg_addr = reg_addr >> 2;

	reg_val_b = p_reg_top1b[2 * 2];
	reg_val_b = reg_val_b & 0xffffffff;
	reg_addr_b = p_reg_top1b[2 * 2 + 1];
	reg_addr_b = reg_addr_b & 0xffff;
	reg_addr_b = reg_addr_b >> 2;

	if (reg_addr == 1 && (reg_val & 0x14) == 0x14 && reg_addr_b == 1 && (reg_val_b & 0x1)) {
		if ((debug_dolby & 0x80000))
			pr_dv_dbg("top1 intensity/l1l4/pyramid bypassed!\n");
		ignore_top1_result = true;
	} else {
		ignore_top1_result = false;
	}
}

/*if pyramid is enable in cfg, we force enable pyramid for top1+top1b due to*/
/*hw not support bypass top1b*/
/*top1b vdr_res is 0, so need to calculate top1b size according top1 size*/
static u32 calc_top1b_size(u32 top1_size)
{
	u32 width;
	u32 height;
	u32 new_size;

	width = top1_size & 0xFFFF;
	height = (top1_size >> 16) & 0xFFFF;

	if (width * height > 1024 * 576) {
		width = width >> 1;
		height = height >> 1;
	}
	new_size =  (width & 0xFFFF) | ((height & 0xFFFF) << 16);
	return new_size;
}

static void dolby5_ahb_reg_config(u32 *reg_baddr,
	u32 core_sel, u32 reg_num)
{
	int i;
	int reg_val, reg_addr;
	static u32 ves_top1 = 0x21c03c0;
	u32 tmp;

	for (i = 0; i < reg_num; i = i + 1) {
		reg_val = reg_baddr[i * 2];
		reg_addr = reg_baddr[i * 2 + 1];

		reg_addr = reg_addr & 0xffff;
		reg_val = reg_val & 0xffffffff;
		if (i < 6)
			if (debug_dolby & 0x10000000)
				pr_dv_dbg("=== addr: 0x%x val:0x%x ===%x %x %x\n",
				reg_addr, reg_val, last_int_top1, last_int_top1b, last_int_top2);

		reg_addr = reg_addr >> 2;
		if (core_sel == 0) {//core1
			if (reg_addr == 1 && !py_enabled) {
				if ((debug_dolby & 0x80000))
					pr_dv_dbg("update top1 reg_addr 0x%x value from 0x%x to 0x%x\n",
						reg_addr << 2, reg_val, reg_val & ~(0x4));

				reg_val = reg_val & ~(0x4); /*CNTRL_REGADDR:force enable intensity*/
			}
			/*0x10 clear interrupt*/
			if (reg_addr == 4)/*clear interrupt*/
				VSYNC_WR_DV_REG(DOLBY5_CORE1_REG_BASE + reg_addr, last_int_top1);
			else
				VSYNC_WR_DV_REG(DOLBY5_CORE1_REG_BASE + reg_addr, reg_val);

			if (reg_addr == 5)
				last_int_top1 = reg_val;
			if (reg_addr == 0x25)/*VDR_RES_REGADDR*/
				ves_top1 = reg_val;
		} else if (core_sel == 1) {//core1b

			if (reg_addr == 1 && !py_enabled) {
				if ((debug_dolby & 0x80000))
					pr_dv_dbg("update top1b reg_addr 0x%x from %x to 0xe6a\n",
						reg_addr << 2, reg_val);
				reg_val = 0xe6a; /*CNTRL_REGADDR:force enable pyramid/intensity*/
			}

			if (reg_addr == 6 && reg_val == 0 && !py_enabled) {
				tmp = calc_top1b_size(ves_top1);/*VDR_RES_REGADDR*/
				if ((debug_dolby & 0x80000))
					pr_dv_dbg("update top1b reg_addr 0x%x from %x to %x\n",
						reg_addr << 2, reg_val, tmp);
				reg_val = tmp;
			}

			/*0x10 clear interrupt*/
			if (reg_addr == 4)/*clear interrupt*/
				VSYNC_WR_DV_REG(DOLBY5_CORE1B_REG_BASE + reg_addr, last_int_top1b);
			else
				VSYNC_WR_DV_REG(DOLBY5_CORE1B_REG_BASE + reg_addr, reg_val);
			if (reg_addr == 5)
				last_int_top1b = reg_val;
		} else if (core_sel == 2) { //core2
			/*0x10 clear interrupt*/
			if (reg_addr == 4)/*clear interrupt*/
				VSYNC_WR_DV_REG(DOLBY5_CORE2_REG_BASE0 + reg_addr, last_int_top2);
			else
				VSYNC_WR_DV_REG(DOLBY5_CORE2_REG_BASE0 + reg_addr, reg_val);
			if (reg_addr == 5)
				last_int_top2 = reg_val;

			/*enable crc cntrl (0xf3d-0xd00)*/
			if ((dolby_vision_flags & FLAG_CERTIFICATION) && reg_addr == 573)
				VSYNC_WR_DV_REG(DOLBY5_CORE2_REG_BASE0 + reg_addr, 1);
		}
	}
}

static void dolby5_top1_ini(struct dolby5_top1_type *dolby5_top1)
{
	int core1_hsize = dolby5_top1->core1_hsize;
	int core1_vsize = dolby5_top1->core1_vsize;
	//int rdma_num = dolby5_top1->rdma_num;
	//int rdma_size = dolby5_top1->rdma_size;
	int bit_mode = dolby5_top1->bit_mode;
	int fmt_mode = dolby5_top1->fmt_mode;
	int block_mode = dolby5_top1->block_mode;
	ulong rdmif_baddr[3];
	int rdmif_stride[3];
	int vsync_sel = dolby5_top1->vsync_sel;
	int i;
	u32 *p_reg_top1;
	u32 *p_reg_top1b;
	int top1_ahb_num;
	int top1b_ahb_num;

	if (hw5_reg_from_file || (test_dv & DEBUG_FIXED_REG) /*fixed reg*/) {
		p_reg_top1 = (u32 *)top1_reg_buf;
		top1_ahb_num = top1_reg_num;
		p_reg_top1b = (u32 *)top1b_reg_buf;
		top1b_ahb_num = top1b_reg_num;
	} else {
		p_reg_top1 = dolby5_top1->core1_ahb_baddr;
		top1_ahb_num = dolby5_top1->core1_ahb_num;
		p_reg_top1b = dolby5_top1->core1b_ahb_baddr;
		top1b_ahb_num = dolby5_top1->core1b_ahb_num;
	}

	check_pr_enabled_in_cfg();
	if (py_enabled && (dolby_vision_flags & FLAG_CERTIFICATION))
		check_pr_enabled_in_reg(p_reg_top1, p_reg_top1b);

	if (p_reg_top1)
		dolby5_ahb_reg_config(p_reg_top1, 0, top1_ahb_num);
	if (p_reg_top1b)
		dolby5_ahb_reg_config(p_reg_top1b, 1, top1b_ahb_num);

	if (debug_dolby & 0x10000000)
		pr_dv_dbg("==== config core1/1b_ahb_reg first before top reg! ====\n");

	for (i = 0; i < 3; i++) {
		rdmif_baddr[i]  = dolby5_top1->rdmif_baddr[i];
		rdmif_stride[i] = dolby5_top1->rdmif_stride[i];
	}

	VSYNC_WR_DV_REG(DOLBY_TOP1_PIC_SIZE, (core1_vsize << 16) | core1_hsize);

	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_RDMA_CTRL, 0x1, 16, 3);
	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_RDMA_CTRL, 0x95, 0, 16);

	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_PYWR_CTRL, dolby5_top1->py_level, 0, 1);
	VSYNC_WR_DV_REG(DOLBY_TOP1_PYWR_BADDR1, dolby5_top1->py_baddr[0] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP1_PYWR_BADDR2, dolby5_top1->py_baddr[1] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP1_PYWR_BADDR3, dolby5_top1->py_baddr[2] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP1_PYWR_BADDR4, dolby5_top1->py_baddr[3] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP1_PYWR_BADDR5, dolby5_top1->py_baddr[4] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP1_PYWR_BADDR6, dolby5_top1->py_baddr[5] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP1_PYWR_BADDR7, dolby5_top1->py_baddr[6] >> 4);

	VSYNC_WR_DV_REG(DOLBY_TOP1_PYWR_STRIDE12,
		(dolby5_top1->py_stride[1] << 16) | dolby5_top1->py_stride[0]);
	VSYNC_WR_DV_REG(DOLBY_TOP1_PYWR_STRIDE34,
		(dolby5_top1->py_stride[3] << 16) | dolby5_top1->py_stride[2]);
	VSYNC_WR_DV_REG(DOLBY_TOP1_PYWR_STRIDE56,
		(dolby5_top1->py_stride[5] << 16) | dolby5_top1->py_stride[4]);
	VSYNC_WR_DV_REG(DOLBY_TOP1_PYWR_STRIDE7, dolby5_top1->py_stride[6]);

	VSYNC_WR_DV_REG(DOLBY_TOP1_WDMA_BADDR0, dolby5_top1->wdma_baddr >> 4);

	dolby5_top1_rdmif(core1_hsize,
		core1_vsize,
		bit_mode,
		fmt_mode,
		block_mode,
		rdmif_baddr,
		rdmif_stride);

	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_CTRL0, vsync_sel, 24, 2);

	if (vsync_sel == 1)
		VSYNC_WR_DV_REG(DOLBY_TOP1_CTRL0,
			VSYNC_RD_DV_REG(DOLBY_TOP1_CTRL0) |
			(dolby5_top1->reg_frm_rst << 30) |
			(vsync_sel << 24));

	VSYNC_WR_DV_REG(DOLBY_TOP1_CTRL0,
		(VSYNC_RD_DV_REG(DOLBY_TOP1_CTRL0) & ~(1 << 30)) |
		(vsync_sel << 24));
}

static void top2_ahb_ini(u64 *core2_ahb_baddr)
{
	u32 *p_reg;
	int top2_ahb_num = TOP2_REG_NUM;

	if (hw5_reg_from_file || (test_dv & DEBUG_FIXED_REG) /*fixed reg*/) {
		p_reg = (u32 *)top2_reg_buf;
		top2_ahb_num = top2_reg_num;
	} else {
		p_reg = (u32 *)core2_ahb_baddr;
	}
	if (p_reg)
		dolby5_ahb_reg_config(p_reg, 2, top2_ahb_num);
}

static void dolby5_top2_ini(u32 core2_hsize,
	u32 core2_vsize,
	dma_addr_t py_baddr[7],
	u32 py_stride[7])
{
	int rdma_num = 2;/*fix*/
	int rdma1_num = 1;/*fix*/
	int rdma_size[12] = {166, 258};/*fix*/

	VSYNC_WR_DV_REG(DOLBY_TOP2_PIC_SIZE, (core2_vsize << 16) | core2_hsize);
	VSYNC_WR_DV_REG_BITS(DOLBY_TOP2_RDMA_CTRL, rdma_num, 0, 5);
	VSYNC_WR_DV_REG_BITS(DOLBY_TOP2_RDMA_CTRL, rdma1_num, 8, 3);

	VSYNC_WR_DV_REG(DOLBY_TOP2_RDMA_SIZE0, (rdma_size[0] << 16) | rdma_size[1]);
	VSYNC_WR_DV_REG(DOLBY_TOP2_RDMA_SIZE1, (rdma_size[2] << 16) | rdma_size[3]);
	VSYNC_WR_DV_REG(DOLBY_TOP2_RDMA_SIZE2, (rdma_size[4] << 16) | rdma_size[5]);
	VSYNC_WR_DV_REG(DOLBY_TOP2_RDMA_SIZE3, (rdma_size[6] << 16) | rdma_size[7]);
	VSYNC_WR_DV_REG(DOLBY_TOP2_RDMA_SIZE4, (rdma_size[8] << 16) | rdma_size[9]);
	VSYNC_WR_DV_REG(DOLBY_TOP2_RDMA_SIZE5, (rdma_size[10] << 16) | rdma_size[11]);

	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_BADDR1, py_baddr[0] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_BADDR2, py_baddr[1] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_BADDR3, py_baddr[2] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_BADDR4, py_baddr[3] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_BADDR5, py_baddr[4] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_BADDR6, py_baddr[5] >> 4);
	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_BADDR7, py_baddr[6] >> 4);

	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_STRIDE12, (py_stride[1] << 16) | py_stride[0]);
	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_STRIDE34, (py_stride[3] << 16) | py_stride[2]);
	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_STRIDE56, (py_stride[5] << 16) | py_stride[4]);
	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_STRIDE7, py_stride[6]);
}

void dolby5_mmu_config(u8 *baddr, int num)
{
	u32 mmu_lut;
	u32 lut0;
	u32 lut1;
	u32 lut2;
	u32 lut3;
	u32 lut4;
	u32 lut5;
	u32 lut6;
	u32 lut7;
	int i;

#if PYRAMID_SW_RST
	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRMIF_MMU_ADDR, 0);
#else
	WRITE_VPP_DV_REG(DOLBY_TOP2_PYRMIF_MMU_ADDR, 0);
#endif

	for (i = 0; i < (num + 7) >> 3; i = i + 1) {
		mmu_lut = 0;
		lut0 = (baddr[i * 8]) & 0x7;
		lut1 = (baddr[i * 8 + 1]) & 0x7;
		lut2 = (baddr[i * 8 + 2]) & 0x7;
		lut3 = (baddr[i * 8 + 3]) & 0x7;
		mmu_lut = (lut3 << 9) | (lut2 << 6) | (lut1 << 3) | lut0;
		lut4 = (baddr[i * 8 + 4]) & 0x7;
		lut5 = (baddr[i * 8 + 5]) & 0x7;
		lut6 = (baddr[i * 8 + 6]) & 0x7;
		lut7 = (baddr[i * 8 + 7]) & 0x7;
		mmu_lut = (((lut7 << 9) | (lut6 << 6) | (lut5 << 3) | lut4) << 12) | mmu_lut;

		if (i < 1)
			if (debug_dolby & 1)
				pr_dv_dbg("mmu %d: %x %x %x %x %x %x %x %x, mmu_lut %x\n",
					i, lut0, lut1, lut2, lut3, lut4, lut5, lut6, lut7, mmu_lut);
#if PYRAMID_SW_RST
		VSYNC_WR_DV_REG(DOLBY_TOP2_PYRMIF_MMU_DATA, mmu_lut);
#else
		WRITE_VPP_DV_REG(DOLBY_TOP2_PYRMIF_MMU_DATA, mmu_lut);
#endif
	}
}

#define OVLAP_HSIZE 96
static void dolby5_dpth_ctrl(int hsize, int vsize, struct vd_proc_info_t *vd_proc_info)
{
	enum top2_source core2_src = FROM_VD1;/*0:from vdin  1:from vd1 2:from di*/
	u32 ovlp_en = 0;/*1: ovlp enable 0:bypass*/
	u32 ovlp_ihsize = 1920;/*ovlp input hsize*/
	u32 ovlp_ivsize = 2160;/*ovlp input vsize*/
	u32 ovlp_ahsize = 32;/*ovlp added(ovlp) hsize, need update according to vpp*/

	struct ovlp_win_para win0 = {0, 1920 + 96, 2160, 0, 1920 - 1, 0, 2160 - 1};
	struct ovlp_win_para win1 = {0, 1920 + 96, 2160, 96, 1920 - 1 + 96, 0, 2160 - 1};
	struct ovlp_win_para win2 = {0, 1920 + 32, 2160, 0, 1920 - 1 + 7, 0, 2160 - 1};
	struct ovlp_win_para win3 = {0, 1920 + 32, 2160, 32 - 9, 1920 + 32 - 1, 0, 2160 - 1};

	u32 vdin_p2s_hsize = 0;
	u32 vdin_p2s_vsize = 0;
	u32 vdin_s2p_hsize = 0;
	u32 vdin_s2p_vsize = 0;

	/*update from vpp*/
	u32 vd1_hsize = hsize;/*3840*/
	u32 vd1_vsize = vsize;/*2160*/
	u32 vd1_slice0_hsize = 1920 + 96;/*1920 + 96*/
	u32 vd1_slice0_vsize = 2160;/*2160*/
	u32 vd1_slice1_hsize = 1920 + 96;/*1920 + 96*/
	u32 vd1_slice1_vsize = 2160;/*2160*/
	u32 slice0_extra_size = 7;
	u32 slice1_extra_size = 9;
	u32 max_output_extra;
	u32 input_extra = OVLAP_HSIZE;
	u32 slice_en;
	u32 slice_num = 1;

	if (vd_proc_info) {
		slice_num = vd_proc_info->slice_num;
		if (slice_num == 2) {
			vd1_slice0_hsize = vd_proc_info->slice[0].hsize;
			vd1_slice0_vsize = vd_proc_info->slice[0].vsize;
			vd1_slice1_hsize = vd_proc_info->slice[1].hsize;
			vd1_slice1_vsize = vd_proc_info->slice[1].vsize;
			if (vd_proc_info->slice[0].scaler_in_hsize >= vd1_hsize / 2)
				slice0_extra_size =
				vd_proc_info->slice[0].scaler_in_hsize - vd1_hsize / 2;
			if (vd_proc_info->slice[1].scaler_in_hsize >= vd1_hsize / 2)
				slice1_extra_size =
				vd_proc_info->slice[1].scaler_in_hsize - vd1_hsize / 2;
		}
	}

	slice_en = slice_num == 2 ? 0x3 : 0x1;
	ovlp_en = slice_num == 2 ? 0x1 : 0x0;

	if ((debug_dolby & 0x80000))
		pr_info("slice %d,size %d %d %d %d %d %d %d %d\n",
				slice_num, vd1_hsize, vd1_vsize,
				vd1_slice0_hsize, vd1_slice0_vsize,
				vd1_slice1_hsize, vd1_slice1_vsize,
				slice0_extra_size, slice1_extra_size);

	VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_CTRL, core2_src & 0x3, 0, 2);//reg_dv_out_sel
	VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_CTRL, core2_src & 0x3, 2, 2);//reg_dv_in_sel
	VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_CTRL, slice_en & 0x3, 12, 2);//reg_dv_in_hs_en
	VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_CTRL, slice_en & 0x3, 14, 2);//reg_dv_out_hs_en

	if (core2_src == FROM_VDIN0) {//vdin0
		//reg_path_sel, 0:vdin0 2ppc 1:vdin1 1ppc
		VSYNC_WR_DV_REG_BITS(VDIN_TOP_CTRL, 0, 20, 1);
		VSYNC_WR_DV_REG_BITS(VDIN0_CORE_CTRL, 1, 1, 1);//vdin0 reg_dolby_en
		VSYNC_WR_DV_REG_BITS(VDIN1_CORE_CTRL, 0, 1, 1);//vdin1 reg_dolby_en
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_CTRL, 96, 4,  8);//reg_vdin_p2s_ovlp_size
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_P2S, 1, 29, 2);//reg_vdin_p2s_mode  ppc->2slce
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_VDIN_CTRL, slice_en ^ 0x3, 8, 2);//dv_ppc_out
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_VDIN_CTRL, slice_en ^ 0x3, 0, 2);//dv_ppc_in
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_VDIN_CTRL, slice_en ^ 0x3, 4, 2);//dv_slc_out
	} else if (core2_src == FROM_VDIN1) {//vdin1
		//reg_path_sel,0:vind0 2ppc 1:vdin1 1ppc
		VSYNC_WR_DV_REG_BITS(VDIN_TOP_CTRL, 1, 20, 1);
		VSYNC_WR_DV_REG_BITS(VDIN0_CORE_CTRL, 0, 1, 1);//vdin0 reg_dolby_en
		VSYNC_WR_DV_REG_BITS(VDIN1_CORE_CTRL, 1, 1, 1);//vdin1 reg_dolby_en
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_VDIN_CTRL, 1, 8, 2);//vdin1 reg_vdin_hs_ppc_out
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_VDIN_CTRL, 1, 6, 2);//reg_vdin_hs_slc_byp
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_VDIN_CTRL, 1, 0, 2);//reg_dv_ppc_in
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_VDIN_CTRL, 1, 2, 2);//reg_vdin_hs_byp_in
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_VDIN_CTRL, 1, 4, 2);//reg_dv_slc_out
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_CTRL, 0, 0, 2);//reg_dv_out_sel
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_CTRL, 3, 2, 2);//reg_dv_in_sel
	} else if (core2_src == FROM_VD1) { //vd1
		//VSYNC_WR_DV_REG_BITS(T3X_VD1_S0_DV_BYPASS_CTRL, 1, 0, 1);//vd1_slice0 dv_en
		//VSYNC_WR_DV_REG_BITS(T3X_VD1_S1_DV_BYPASS_CTRL, slice_num == 2 ? 1 : 0, 0, 1);
	} else if (core2_src == FROM_DI) {  //di
		//[1:0]inp_out_sel={[16]dvtv_di_sel,inp_2vdin_sel};
		//inp mif:0:to di_pre 1:to vdin 2:to dolby tv
		//[1:0]inp_in_sel={[16]dvtv_di_sel,vdin_2inp_sel};
		//pre current data : 0: mif 1:vdin 2:dolby tv
		VSYNC_WR_DV_REG_BITS(VIUB_MISC_CTRL0, 1, 16, 1);   //inp_mif(m0) to dv_slc0

		if (slice_num == 2) {
			//[4]: //0: wrmif0 input from nrwr   1:from dvs0_out
			VSYNC_WR_DV_REG_BITS(DI_TOP_CTRL1, 1, 4, 1);
			//[5]: //0: wrmif1 input from di_out 1:from dvs1_out
			VSYNC_WR_DV_REG_BITS(DI_TOP_CTRL1, 1, 5, 1);
			//[6]: //mem_mif(m2) to  0:nr 1:dv
			VSYNC_WR_DV_REG_BITS(DI_TOP_CTRL1, 1, 6, 1);
			//[7]: //dv_out_slc0 to  0:di 1:wrmif0
			VSYNC_WR_DV_REG_BITS(DI_TOP_CTRL1, 1, 7, 1);
			//post_frm_rst sel: 0:viu  1:internal 2:pre-post link, use pre timming
			VSYNC_WR_DV_REG_BITS(DI_TOP_POST_CTRL, 2, 30, 2);
		}
	} else {
		pr_dv_dbg("dolby5 path ctrl: err config");
	}

	if (slice_num == 2) {
		if (vd1_slice0_hsize > vd1_hsize / 2) {
			input_extra = vd1_slice0_hsize - vd1_hsize / 2;
			if (input_extra != OVLAP_HSIZE ||
				vd_proc_info->overlap_size != OVLAP_HSIZE)
				pr_info("amdv input extra overlap %d != %d\n",
						input_extra, OVLAP_HSIZE);

		}
		max_output_extra = slice1_extra_size > slice0_extra_size ?
							slice1_extra_size : slice0_extra_size;
		ovlp_ahsize = (max_output_extra + 15) / 16 * 16;/*align to 16*/

		ovlp_ihsize = vd1_hsize / 2;
		ovlp_ivsize = vd1_vsize;

		win0.ovlp_win_en = 1;
		win0.ovlp_win_hsize = vd1_slice0_hsize;
		win0.ovlp_win_vsize = vd1_slice0_vsize;
		win0.ovlp_win_hbgn = 0;
		win0.ovlp_win_hend = ovlp_ihsize - 1;
		win0.ovlp_win_vbgn = 0;
		win0.ovlp_win_vend = ovlp_ivsize - 1;

		win1.ovlp_win_en = 1;
		win1.ovlp_win_hsize = vd1_slice1_hsize;
		win1.ovlp_win_vsize = vd1_slice1_vsize;
		win1.ovlp_win_hbgn = input_extra;
		win1.ovlp_win_hend = ovlp_ihsize - 1 + win1.ovlp_win_hbgn;
		win1.ovlp_win_vbgn = 0;
		win1.ovlp_win_vend = ovlp_ivsize - 1;

		win2.ovlp_win_en = 1;
		win2.ovlp_win_hsize = ovlp_ihsize + ovlp_ahsize;
		win2.ovlp_win_vsize = vd1_slice0_vsize;
		win2.ovlp_win_hbgn = 0;
		win2.ovlp_win_hend = ovlp_ihsize - 1 + slice0_extra_size;
		win2.ovlp_win_vbgn = 0;
		win2.ovlp_win_vend = ovlp_ivsize - 1;

		win3.ovlp_win_en = 1;
		win3.ovlp_win_hsize = ovlp_ihsize + ovlp_ahsize;
		win3.ovlp_win_vsize = vd1_slice1_vsize;
		win3.ovlp_win_hbgn = ovlp_ahsize - slice1_extra_size;
		win3.ovlp_win_hend = ovlp_ihsize + ovlp_ahsize - 1;
		win3.ovlp_win_vbgn = 0;
		win3.ovlp_win_vend = ovlp_ivsize - 1;

		if (debug_dolby & 0x80000) {
			pr_info("scaler_in_hsize %d %d, output_extra %d, ovlp_ahsize %d\n",
					vd_proc_info->slice[0].scaler_in_hsize,
					vd_proc_info->slice[1].scaler_in_hsize,
					max_output_extra, ovlp_ahsize);
			pr_info("win0: %d %d %2d %d %2d %d\n",
					win0.ovlp_win_hsize, win0.ovlp_win_vsize,
					win0.ovlp_win_hbgn, win0.ovlp_win_hend,
					win0.ovlp_win_vbgn, win0.ovlp_win_vend);
			pr_info("win1: %d %d %2d %d %2d %d\n",
					win1.ovlp_win_hsize, win1.ovlp_win_vsize,
					win1.ovlp_win_hbgn, win1.ovlp_win_hend,
					win1.ovlp_win_vbgn, win1.ovlp_win_vend);
			pr_info("win2: %d %d %2d %d %2d %d\n",
					win2.ovlp_win_hsize, win2.ovlp_win_vsize,
					win2.ovlp_win_hbgn, win2.ovlp_win_hend,
					win2.ovlp_win_vbgn, win2.ovlp_win_vend);
			pr_info("win3: %d %d %2d %d %2d %d\n",
					win3.ovlp_win_hsize, win3.ovlp_win_vsize,
					win3.ovlp_win_hbgn, win3.ovlp_win_hend,
					win3.ovlp_win_vbgn, win3.ovlp_win_vend);
		}

		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_SIZE,
			ovlp_ihsize & 0x1fff, 16, 13); //ovlp_ihsize
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_SIZE,
			ovlp_ivsize & 0x1fff, 0, 13); //ovlp_ivsize

		//ovlp setting
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP,
		ovlp_en & 0x1, 31, 1);//reg_ovlp_en
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP,
			win0.ovlp_win_en & 0x1, 7, 1);//reg_ovlp_win0_en
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP,
			win1.ovlp_win_en & 0x1, 8, 1);//reg_ovlp_win1_en
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP,
			win2.ovlp_win_en & 0x1, 9, 1);//reg_ovlp_win2_en
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP,
			win3.ovlp_win_en & 0x1, 10, 1);//reg_ovlp_win3_en
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP,
			ovlp_ahsize & 0x1fff, 16, 13);//reg_ovlp_size}

		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN0_SIZE,
			win0.ovlp_win_hsize & 0x1fff, 16, 13);//ovlp_ivsize
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN0_SIZE,
			win0.ovlp_win_vsize & 0x1fff, 0, 13);//ovlp_ihsize
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN0_H,
			win0.ovlp_win_hbgn & 0x1fff, 16, 13);//ovlp_ivsize
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN0_H,
			win0.ovlp_win_hend & 0x1fff, 0, 13);//ovlp_ihsize
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN0_V,
			win0.ovlp_win_vbgn & 0x1fff, 16, 13);//ovlp_ivsize
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN0_V,
			win0.ovlp_win_vend & 0x1fff, 0, 13);//ovlp_ihsize

		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN1_SIZE,
			win1.ovlp_win_hsize & 0x1fff, 16, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN1_SIZE,
			win1.ovlp_win_vsize & 0x1fff, 0, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN1_H,
			win1.ovlp_win_hbgn & 0x1fff, 16, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN1_H,
			win1.ovlp_win_hend & 0x1fff, 0, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN1_V,
			win1.ovlp_win_vbgn & 0x1fff, 16, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN1_V,
			win1.ovlp_win_vend & 0x1fff, 0, 13);

		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN2_SIZE,
			win2.ovlp_win_hsize & 0x1fff, 16, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN2_SIZE,
			win2.ovlp_win_vsize & 0x1fff, 0, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN2_H,
			win2.ovlp_win_hbgn & 0x1fff, 16, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN2_H,
			win2.ovlp_win_hend & 0x1fff, 0, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN2_V,
			win2.ovlp_win_vbgn & 0x1fff, 16, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN2_V,
			win2.ovlp_win_vend & 0x1fff, 0, 13);

		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN3_SIZE,
			win3.ovlp_win_hsize & 0x1fff, 16, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN3_SIZE,
			win3.ovlp_win_vsize & 0x1fff, 0, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN3_H,
			win3.ovlp_win_hbgn & 0x1fff, 16, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN3_H,
			win3.ovlp_win_hend & 0x1fff, 0, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN3_V,
			win3.ovlp_win_vbgn & 0x1fff, 16, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP_WIN3_V,
			win3.ovlp_win_vend & 0x1fff, 0, 13);
	} else {
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP, 0, 31, 1);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP, 0, 7, 1);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP, 0, 8, 1);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP, 0, 9, 1);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_OVLP, 0, 10, 1);
	}

	if (core2_src == FROM_VDIN0 || core2_src == FROM_VDIN1) {//vdin
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_P2S, vdin_p2s_hsize & 0x1fff, 0, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_P2S, vdin_p2s_vsize & 0x1fff, 16, 13);

		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_S2P, vdin_s2p_hsize & 0x1fff, 0, 13);
		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_S2P, vdin_s2p_vsize & 0x1fff, 16, 13);
	}
}

void enable_amdv_hw5(int enable)
{
	int gd_en = 0;

	if (debug_dolby & 8)
		pr_dv_dbg("enable %d,dv on %d %d %d,mode %d %d,video %d %d\n",
			  enable, dolby_vision_on,
			  top1_info.core_on,
			  top2_info.core_on,
			  dolby_vision_mode,
			  get_amdv_target_mode(),
			  top1_info.amdv_setting_video_flag,
			  top2_info.amdv_setting_video_flag);
	if (enable) {
		if (is_aml_t3x())
			vd_proc_info = get_vd_proc_amdv_info();
		if (!dolby_vision_on) {
			set_amdv_wait_on();
			if (is_aml_t3x()) {
				update_dma_buf();
				/* common flow should */
				/* stop hdr core before */
				/* start dv core */
				if (dolby_vision_flags & FLAG_CERTIFICATION)
					hdr_vd1_off(VPP_TOP0);
				if (!top2_info.core_on)
					set_frame_count(0);
				if (enable_top1 && (amdv_mask & 1) &&
					top1_info.amdv_setting_video_flag) {
					VSYNC_WR_DV_REG_BITS
						(T3X_VD1_S0_DV_BYPASS_CTRL,
						 1,
						 0, 1); /* dv path enable */
					top1_info.core_on = true;
				}
				if ((amdv_mask & 1) &&
					top2_info.amdv_setting_video_flag) {
					VSYNC_WR_DV_REG_BITS
						(T3X_VD1_S0_DV_BYPASS_CTRL,
						 1,
						 0, 1); /* dv path enable */
					if (vd_proc_info && vd_proc_info->slice_num == 2)
						VSYNC_WR_DV_REG_BITS
							(T3X_VD1_S1_DV_BYPASS_CTRL,
							 1,
							 0, 1); /* dv path enable */
					top2_info.core_on = true;
				}
				if (!(amdv_mask & 1) ||
					(!top2_info.amdv_setting_video_flag &&
					!top1_info.amdv_setting_video_flag)) {
					VSYNC_WR_DV_REG_BITS
						(T3X_VD1_S0_DV_BYPASS_CTRL,
						 0,
						 0, 1); /* dv path disable */
					VSYNC_WR_DV_REG_BITS
						(T3X_VD1_S1_DV_BYPASS_CTRL,
						 0,
						 0, 1); /* dv path disable */
					top1_info.core_on = false;
					top2_info.core_on = false;
					//VSYNC_WR_DV_REG(VPU_DOLBY_WRAP_GCLK, 0x55);
					//dv_mem_power_off(VPU_DOLBY0);
				}
			}
			if (dolby_vision_flags & FLAG_CERTIFICATION) {
				/* bypass dither/PPS/SR/CM, EO/OE */
				if ((!vd_proc_info || vd_proc_info->slice_num == 1) && !need_pps) {
					bypass_pps_sr_gamma_gainoff(5);
				} else {
					/*black screen when bypass from preblend*/
					/*to VADJ1 at 2slice*/
					bypass_pps_sr_gamma_gainoff(4);
					VSYNC_WR_DV_REG_BITS(T3X_VD_PROC_BYPASS_CTRL, 0, 1, 1);
				}
				/* bypass all video effect */
				video_effect_bypass(1);
			} else {
				/* bypass all video effect */
				if (dolby_vision_flags & FLAG_BYPASS_VPP)
					video_effect_bypass(1);
			}
			isr_cnt = 0;
			top1_done = false;
			pr_info("TV core turn on\n");
		} else {
			if (!top1_info.core_on && enable_top1 &&
				(amdv_mask & 1) &&
				top1_info.amdv_setting_video_flag) {
				if (is_aml_t3x()) {
					/*enable dv path*/
					VSYNC_WR_DV_REG_BITS
						(T3X_VD1_S0_DV_BYPASS_CTRL,
						 1, 0, 1);
					hdr_vd1_off(VPP_TOP0);
				}
				top1_info.core_on = true;
				pr_dv_dbg("TV top1 turn on\n");
			}
			if (!top2_info.core_on &&
				(amdv_mask & 1) &&
				top2_info.amdv_setting_video_flag) {
				if (is_aml_t3x()) {
					/*enable dv path*/
					VSYNC_WR_DV_REG_BITS
						(T3X_VD1_S0_DV_BYPASS_CTRL,
						 1, 0, 1);
					hdr_vd1_off(VPP_TOP0);
					if (vd_proc_info && vd_proc_info->slice_num == 2)
						VSYNC_WR_DV_REG_BITS
							(T3X_VD1_S1_DV_BYPASS_CTRL,
							 1,
							 0, 1); /* dv path enable */
				}
				top2_info.core_on = true;
				pr_dv_dbg("TV top2 turn on\n");
				if (dolby_vision_flags & FLAG_CERTIFICATION) {
					/* bypass dither/PPS/SR/CM, EO/OE */
					if ((!vd_proc_info || vd_proc_info->slice_num == 1) &&
						!need_pps) {
						bypass_pps_sr_gamma_gainoff(5);
					} else {
						/*black screen when bypass from preblend*/
						/*to VADJ1 at 2slice*/
						bypass_pps_sr_gamma_gainoff(4);
						VSYNC_WR_DV_REG_BITS(T3X_VD_PROC_BYPASS_CTRL,
							0, 1, 1);
					}
					/* bypass all video effect */
					video_effect_bypass(1);
				}
			}
			if (!(amdv_mask & 1) ||
				(!top1_info.amdv_setting_video_flag &&
				!top2_info.amdv_setting_video_flag)) {
				if (is_aml_t3x()) {
					/*disable dv path*/
					VSYNC_WR_DV_REG_BITS
					(T3X_VD1_S0_DV_BYPASS_CTRL,
					 0, 0, 1);
					VSYNC_WR_DV_REG_BITS
					(T3X_VD1_S1_DV_BYPASS_CTRL,
					 0, 0, 1);
					//VSYNC_WR_DV_REG(VPU_DOLBY_WRAP_GCLK, 0x55);//todo
					//dv_mem_power_off(VPU_DOLBY0);//todo
				}
				top1_info.core_on = false;
				top1_info.core_on_cnt = 0;
				top2_info.core_on = false;
				top2_info.core_on_cnt = 0;
				set_frame_count(0);
				set_vf_crc_valid(0);
				crc_count = 0;
				pr_dv_dbg("TV CORE turn off\n");
			}
		}
		dolby_vision_on = true;
		clear_dolby_vision_wait();
		set_vsync_count(0);
	} else {
		if (dolby_vision_on) {
			if (is_aml_tvmode()) {
				if (is_aml_t3x()) {
					VSYNC_WR_DV_REG_BITS
					(T3X_VD1_S0_DV_BYPASS_CTRL,
					 0, 0, 1);
					VSYNC_WR_DV_REG_BITS
					(T3X_VD1_S1_DV_BYPASS_CTRL,
					 0, 0, 1);
					//VSYNC_WR_DV_REG(VPU_DOLBY_WRAP_GCLK, 0x55);
					//dv_mem_power_off(VPU_DOLBY0); //todo
					//vpu_module_clk_disable(0, DV_TVCORE, 0);
				}
				if (p_funcs_tv) { /* destroy ctx */
					p_funcs_tv->tv_hw5_control_path
						(invalid_hw5_setting);
					p_funcs_tv->tv_hw5_control_path_analyzer
						(invalid_hw5_setting);
				}
				pr_dv_dbg("DV TV core turn off\n");
				if (tv_hw5_setting) {
					tv_hw5_setting->top2.src_format =
					FORMAT_SDR;
					tv_hw5_setting->top1.src_format =
					FORMAT_SDR;
				}
			}
			video_effect_bypass(0);
		}
		set_vf_crc_valid(0);
		reset_dv_param();
		clear_dolby_vision_wait();
		if (!is_aml_gxm() && !is_aml_txlx()) {
			hdr_osd_off(VPP_TOP0);
			hdr_vd1_off(VPP_TOP0);
		}
		/*dv release control of pwm*/
		if (is_aml_tvmode())
			gd_en = 0;
	}
}

void dolby5_bypass_ctrl(unsigned int en)
{
	if (en) {
		/*enable vpu dolby clock gate*/
		vpu_module_clk_enable(0, DV_TVCORE, 1);
		vpu_module_clk_enable(0, DV_TVCORE, 0);

		/*dv core top control bit0*/
		WRITE_VPP_DV_REG_BITS(T3X_VD1_S0_DV_BYPASS_CTRL, 1, 0, 1);
		//VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_CTRL, (en & 0x1), 31, 1);
		/*top2 bypass control bit31*/
		WRITE_VPP_DV_REG(VPU_DOLBY_WRAP_CTRL, ((en & 0x1) << 31) | (0x55005));
		/*overlap force reset, or picture will shift*/
		WRITE_VPP_DV_REG(VPU_DOLBY_WRAP_GCLK, (1 << 19) | (1 << 17));
	} else {
		vpu_module_clk_disable(0, DV_TVCORE, 0);
	}
}

void dump_lut(void)
{
	int i;

	if ((debug_dolby & 0x40000) && enable_top1) {
		WRITE_VPP_DV_REG(DOLBY5_CORE1_ICSCLUT_DBG_RD, 0x1);
		for (i = 0; i < 595; i++) {
			WRITE_VPP_DV_REG(DOLBY5_CORE1_ICSCLUT_RDADDR, i);
			if (i == 0)
				READ_VPP_DV_REG(DOLBY5_CORE1_ICSCLUT_RDDATA);/*discard last data*/
			if (i >= 1)
				pr_dv_dbg("top1: ICSC[%d]: %x",
					i - 1, READ_VPP_DV_REG(DOLBY5_CORE1_ICSCLUT_RDDATA));
			if (i == 594) {
				WRITE_VPP_DV_REG(DOLBY5_CORE1_ICSCLUT_RDADDR, i);
				pr_dv_dbg("top1: ICSC[%d]: %x",
					i, READ_VPP_DV_REG(DOLBY5_CORE1_ICSCLUT_RDDATA));
			}
		}
	}

	if ((debug_dolby & 0x40000) && top2_info.core_on) {
		/*bit 3-0: ocsc-icsc-cvmsmi-cvmtai*/
		WRITE_VPP_DV_REG(DOLBY5_CORE2_DEBUG_LUT_CNTRL, 0x4);
		for (i = 0; i < 595; i++) {
			WRITE_VPP_DV_REG(DOLBY5_CORE2_ICSCLUT_RDADDR, i);
			if (i == 0)
				READ_VPP_DV_REG(DOLBY5_CORE2_ICSCLUT_RDDATA);/*discard last data*/
			if (i >= 1)
				pr_dv_dbg("ICSC[%d]: %x",
					i - 1, READ_VPP_DV_REG(DOLBY5_CORE2_ICSCLUT_RDDATA));
			if (i == 594) {
				WRITE_VPP_DV_REG(DOLBY5_CORE2_ICSCLUT_RDADDR, i);
				pr_dv_dbg("ICSC[%d]: %x",
					i, READ_VPP_DV_REG(DOLBY5_CORE2_ICSCLUT_RDDATA));
			}
		}
		WRITE_VPP_DV_REG(DOLBY5_CORE2_DEBUG_LUT_CNTRL, 0x8);
		for (i = 0; i < 129; i++) {
			WRITE_VPP_DV_REG(DOLBY5_CORE2_OCSCLUT_RDADDR, i);
			if (i == 0)
				READ_VPP_DV_REG(DOLBY5_CORE2_OCSCLUT_RDDATA);/*discard last*/
			if (i >= 1)
				pr_dv_dbg("OCSC[%d]: %x",
					i - 1, READ_VPP_DV_REG(DOLBY5_CORE2_OCSCLUT_RDDATA));
			if (i == 128) {
				WRITE_VPP_DV_REG(DOLBY5_CORE2_OCSCLUT_RDADDR, i);
				pr_dv_dbg("OCSC[%d]: %x",
					i, READ_VPP_DV_REG(DOLBY5_CORE2_OCSCLUT_RDDATA));
			}
		}
		WRITE_VPP_DV_REG(DOLBY5_CORE2_DEBUG_LUT_CNTRL, 0x1);
		for (i = 0; i < 513; i++) {
			WRITE_VPP_DV_REG(DOLBY5_CORE2_CVM_TAILUT_RDADDR, i);
			if (i == 0)
				READ_VPP_DV_REG(DOLBY5_CORE2_CVM_TAILUT_RDDATA);/*discard last*/
			if (i >= 1)
				pr_dv_dbg("CVM TAI[%d]: %x",
					i - 1, READ_VPP_DV_REG(DOLBY5_CORE2_CVM_TAILUT_RDDATA));
			if (i == 512) {
				WRITE_VPP_DV_REG(DOLBY5_CORE2_CVM_TAILUT_RDDATA, i);
				pr_dv_dbg("CVM TAI[%d]: %x",
					i, READ_VPP_DV_REG(DOLBY5_CORE2_CVM_TAILUT_RDDATA));
			}
		}
		WRITE_VPP_DV_REG(DOLBY5_CORE2_DEBUG_LUT_CNTRL, 0x2);
		for (i = 0; i < 513; i++) {
			if (i == 0)
				READ_VPP_DV_REG(DOLBY5_CORE2_CVM_SMILUT_RDDATA);
			if (i >= 1)
				pr_dv_dbg("CVM SMI[%d]: %x",
				i - 1, READ_VPP_DV_REG(DOLBY5_CORE2_CVM_SMILUT_RDDATA));
			if (i == 512) {
				WRITE_VPP_DV_REG(DOLBY5_CORE2_CVM_SMILUT_RDADDR, i);
				pr_dv_dbg("CVM SMI[%d]: %x",
				i, READ_VPP_DV_REG(DOLBY5_CORE2_CVM_SMILUT_RDDATA));
			}
			pr_dv_dbg("CVM SMI[%d]: %x",
				i, READ_VPP_DV_REG(DOLBY5_CORE2_CVM_SMILUT_RDDATA));
		}
	}
}

static u32 top1_stride_rdmif(u32 buffr_width, u8 plane_bits)
{
	u32 line_stride;

	/* input: buffer width not hsize */
	/* 1 stride = 16 byte, DDR 64bytes alignment */
	line_stride = (((buffr_width * plane_bits + 127) / 128 + 3) >> 2) << 2;
	return line_stride;
}

int tv_top1_set(u64 *top1_reg,
				u64 *top1b_reg,
				bool reset,
				int video_enable,
				bool toggle)
{
	int i;
	int plane_bits = 8;

	if (debug_dolby & 1)
		pr_dv_dbg("top1_set:on %d %d,reset %d,toggle %d,video %d,wr %d\n",
				   top1_info.core_on, top1_info.core_on_cnt,
				   reset, toggle, video_enable, py_wr_id);

	if (reset) /*hw reset*/
		amdv_core_reset(AMDV_HW5);

	//enable tvcore clk
	if (!top2_info.core_on && !top1_info.core_on) {/*enable once*/
		vpu_module_clk_enable(0, DV_TVCORE, 1);
		vpu_module_clk_enable(0, DV_TVCORE, 0);
	}

	//first frame only top1, need disable top2 rdma
	if (!top2_info.core_on && !force_enable_top12_lut)
		VSYNC_WR_DV_REG_BITS(DOLBY_TOP2_RDMA_CTRL, 0, 30, 1);
	top1_type.core1_ahb_baddr = (u32 *)top1_reg;
	top1_type.core1b_ahb_baddr = (u32 *)top1b_reg;
	top1_type.core1_ahb_num = TOP1_REG_NUM;
	top1_type.core1b_ahb_num = TOP1B_REG_NUM;

	top1_type.core1_hsize = top1_vd_info.width;
	top1_type.core1_vsize = top1_vd_info.height;
	//top1_type.bit_mode = top1_vd_info.bitdepth;
	//top1_vd_info.bitdepth = 8;
	top1_type.rdmif_baddr[0] = top1_vd_info.canvasaddr[0];
	top1_type.rdmif_baddr[1] = top1_vd_info.canvasaddr[1];
	top1_type.rdmif_baddr[2] = top1_vd_info.canvasaddr[2];

	for (i = 0; i < 2; i++) {
		if (top1_vd_info.bitdepth == 8)
			plane_bits = 8;
		else if (top1_vd_info.bitdepth == 10)
			plane_bits = 16;
		/*vf->canvas0_config[0].width instead of valid data width vf->width*/
		top1_type.rdmif_stride[i] = top1_stride_rdmif(top1_vd_info.buf_width, plane_bits);
		if (fix_data == CASE5344_TOP1_READFROM_FILE)
			top1_type.rdmif_stride[i] = 480;
		else if (fix_data == CASE5363_TOP1_READFROM_FILE)
			top1_type.rdmif_stride[i] = 135;/*(540x32+127)/128*/

		if (debug_rdmif & 0xff00)
			top1_type.rdmif_stride[i] = (debug_rdmif >> 8) & 0xff;
	}
	top1_type.rdmif_stride[2] = 0;

	for (i = 0; i < 7; i++)
		top1_type.py_baddr[i] = py_addr[py_wr_id].top1_py_paddr[i];

	if (top1_type.core1_hsize > 512 && top1_type.core1_vsize > 288)
		top1_type.py_level = SEVEN_LEVEL;
	else
		top1_type.py_level = SIX_LEVEL;

	/*??? to be confirm how to config if py disable*/
	top1_type.py_stride[0] = top1_stride_rdmif(1024, 10);
	top1_type.py_stride[1] = top1_stride_rdmif(512, 10);
	top1_type.py_stride[2] = top1_stride_rdmif(256, 10);
	top1_type.py_stride[3] = top1_stride_rdmif(128, 10);
	top1_type.py_stride[4] = top1_stride_rdmif(64, 10);
	top1_type.py_stride[5] = top1_stride_rdmif(32, 10);
	top1_type.py_stride[6] = top1_stride_rdmif(16, 10);

	if (top1_vd_info.type & VIDTYPE_VIU_NV21)
		top1_type.fmt_mode = 2;
	else if (top1_vd_info.type & VIDTYPE_VIU_422)
		top1_type.fmt_mode = 1;
	else if (top1_vd_info.type & VIDTYPE_VIU_444)
		top1_type.fmt_mode = 0;

	if (top1_vd_info.bitdepth == 10)
		top1_type.bit_mode = 0;
	else if (top1_vd_info.bitdepth == 8)
		top1_type.bit_mode = 1;

	top1_type.block_mode = top1_vd_info.blk_mode;

	if (fix_data == CASE5344_TOP1_READFROM_FILE || fix_data == CASE5363_TOP1_READFROM_FILE) {
		top1_type.fmt_mode = 0;//test, force 444
		top1_vd_info.bitdepth = 10;
		top1_type.bit_mode = 0;
		top1_type.block_mode = 0;
	}

	if (debug_dolby & 0x80000)
		pr_dv_dbg("top1 fmt %d,bit %d,stride %d,level %d,py_id %d,hist_id %d\n",
					top1_type.fmt_mode,
					top1_type.bit_mode,
					top1_type.rdmif_stride[0],
					top1_type.py_level,
					py_wr_id,
					l1l4_wr_index);

	top1_type.vsync_sel = 1;
	top1_type.reg_frm_rst = 1;
	top1_type.wdma_baddr = dv5_md_hist.hist_paddr[0];

	dolby5_top1_ini(&top1_type);//todo, locate here??

	VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_IRQ, 1, 0, 1); //top1 dolby int, pulse
	VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_RDMA_CTRL, 1, 30, 1);//open top1 rdma //todo

	if (!top1_info.core_on) {/*update lut data when top2 not on*/
		set_dovi_setting_update_flag(true);
		amdv_update_setting(NULL);
		dma_lut_write();
	}

	return 0;
}

/*hsize: real data width, maybe smaller than ori data*/
void update_top2_reg(int hsize, int vsize)
{
	u64 ori_size = tv_hw5_setting->top2_reg[281];
	u32 new_size = vsize << 16 | hsize;
	int reg_addr;

	if (hw5_reg_from_file)
		return;

	reg_addr = ori_size >> 32;
	reg_addr = reg_addr & 0xffffffff;
	if ((ori_size & 0xffffffff) != new_size && reg_addr == 0x460) {
		//tv_hw5_setting->top2_reg[89] = 0x00000160040c0000;/*AOI todo*/
		//tv_hw5_setting->top2_reg[94] = 0x00000174072b0000;/*AOI todo*/
		tv_hw5_setting->top2_reg[281] =
		0x0000046000000000 | new_size;//0x460 VDR_RES_REGADDR
		if (debug_dolby & 2)
			pr_info("RES from 0x%llx=>0x%llx\n",
			ori_size, tv_hw5_setting->top2_reg[281]);
	}
}

/*toggle:true——update all reg. false——only update dolby inside reg*/
int tv_top2_set(u64 *reg_data,
			     int hsize,
			     int vsize,
			     int video_enable,
			     int src_chroma_format,
			     bool hdmi,
			     bool hdr10,
			     bool reset,
			     bool toggle,
			     bool pr_done)
{
	int i;
	bool bypass_tvcore = (!hsize || !vsize || !(amdv_mask & 1));
	dma_addr_t py_baddr[7] = {py_addr[py_rd_id].top1_py_paddr[0],
					py_addr[py_rd_id].top1_py_paddr[1],
					py_addr[py_rd_id].top1_py_paddr[2],
					py_addr[py_rd_id].top1_py_paddr[3],
					py_addr[py_rd_id].top1_py_paddr[4],
					py_addr[py_rd_id].top1_py_paddr[5],
					py_addr[py_rd_id].top1_py_paddr[6]};

	u32 py_stride[7] = {128, 128, 128, 128, 128, 128, 128};

	u32 vd1_slice0_hsize = hsize;/*1920 + 96, todo*/
	u32 vd1_slice0_vsize = vsize;/*2160*/
	u32 slice_num = 1;/*1 or 2, todo, need update from vpp*/
	bool tmp_reset = false;
	static int tmp_cnt;
	u32 pyrd_crtl;
	u32 top_misc;

	if (dolby_vision_on &&
		(dolby_vision_flags & FLAG_DISABE_CORE_SETTING)) {
		if (top2_info.core_on) {/*set every vsync*/
			if (lut_trigger_by_reg) {
				/*use reg to trigger lut, should after DOLBY_TOP2_RDMA set */
				top_misc = VSYNC_RD_DV_REG(VPU_TOP_MISC);
				top_misc |= 1 << 10;
				VSYNC_WR_DV_REG(VPU_TOP_MISC, top_misc);
				top_misc &= ~(1 << 10);
				VSYNC_WR_DV_REG(VPU_TOP_MISC, top_misc);
			}
			VSYNC_WR_DV_REG(DOLBY5_CORE2_REG_BASE0 + 2, 1);/*Metadata Program start*/
			VSYNC_WR_DV_REG(DOLBY5_CORE2_REG_BASE0 + 3, 1);/*Metadata Program end */
			VSYNC_WR_DV_REG(DOLBY5_CORE2_REG_BASE0 + 4, last_int_top2);/*clear int*/
		}
		return 0;
	}

	if (top1_info.core_on && top1_done)
		top1_done = false; //todo

	//if (force_update_reg & 1)
		//reset = true;

	if (!video_enable) {
		if (!top1_info.core_on && is_aml_t3x())
			VSYNC_WR_DV_REG_BITS
				(T3X_VD1_S0_DV_BYPASS_CTRL,
				 0, 0, 1);
		return 0;
	}
	//enable tvcore clk
	if (!top2_info.core_on && !top1_info.core_on) {/*enable once*/
		vpu_module_clk_enable(0, DV_TVCORE, 1);
		vpu_module_clk_enable(0, DV_TVCORE, 0);
	}

	dump_lut();
	if (is_aml_t3x() && video_enable) {
		/* mempd for ipcore */
		/*if (get_dv_vpu_mem_power_status(VPU_DOLBY0) ==*/
		/*	VPU_MEM_POWER_DOWN ||*/
		/*	get_dv_mem_power_flag(VPU_DOLBY0) ==*/
		/*	VPU_MEM_POWER_DOWN)*/
		/*	dv_mem_power_on(VPU_DOLBY0);*/

		/*bit0-15 gclk_ctrl,bit17 detunnel sw_rst,bit19 sw_rst_overlap*/
		VSYNC_WR_DV_REG(VPU_DOLBY_WRAP_GCLK, 1 << 17 | 1 << 19);
	}
	if (video_enable)
		tmp_cnt++;
	if (test_dv & DEBUG_HW5_RESET_EACH_VSYNC)
		tmp_reset = true;
	else if (tmp_cnt % 600 == 0)
		tmp_reset = true;

	if (test_dv & DEBUG_HW5_TOGGLE_EACH_VSYNC)
		toggle = true;

	if ((force_update_reg & 1) && tmp_reset)
		reset = true;

	//if (top2_info.run_mode_count < amdv_run_mode_delay)
	//	reset = true;

	if (debug_dolby & 1)
		pr_dv_dbg("top2_set:on %d %d,reset %d,toggle %d,video %d,rd %d\n",
				  top2_info.core_on, top2_info.core_on_cnt,
				  reset, toggle, video_enable, py_rd_id);

	if (reset) {/*hw reset*/
		amdv_core_reset(AMDV_HW5);
	}

	if (!enable_top1) {
		if (!force_enable_top12_lut)
			VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_RDMA_CTRL, 0, 30, 1);
	}

	vd_proc_info = get_vd_proc_amdv_info();
	if (vd_proc_info) {
		slice_num = vd_proc_info->slice_num;
		if (slice_num == 2) {
			vd1_slice0_hsize = vd_proc_info->slice[0].hsize;
			vd1_slice0_vsize = vd_proc_info->slice[0].vsize;
		}
	}
	if (test_dv & DEBUG_5065_RGB_BUG) /*rgb*/
		tv_hw5_setting->top2_reg[23] = 0x00000058000002c1;

	//update_top2_reg(vd1_slice0_hsize, vsize);
	py_level = NO_LEVEL;//todo
	if (enable_top1)
		py_level = top1_type.py_level;
	if (hw5_reg_from_file)
		py_level = SIX_LEVEL;//temp debug, case0 frame1, 6 level
	if (test_dv & DEBUG_HW5_NO_LEVEL)
		py_level = NO_LEVEL; //temp debug, case0 frame0, 0 level
	if (test_dv & DEBUG_HW5_SEVEN_LEVEL)
		py_level = SEVEN_LEVEL; //temp debug,7 level

#if PYRAMID_SW_RST /*software start pyramid*/
	/*VPU_DOLBY_WRAP_GCLK bit16 reset total dolby core, need reload pyramid mmu*/
	pyrd_crtl = VSYNC_RD_DV_REG(DOLBY_TOP2_PYRD_CTRL);
	pyrd_crtl &= 0xFFFFFFFC;
	pyrd_crtl |= py_level & 3;
	VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_CTRL, pyrd_crtl);
	if (/*enable_top1 && */(last_py_level != py_level || reset)) {
		pyrd_crtl |= 1 << 30;
		VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_CTRL, pyrd_crtl);/*pyramid mmu rst*/
		pyrd_crtl &= ~(1 << 30);
		VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_CTRL, pyrd_crtl);
		if (py_level == SIX_LEVEL)
			dolby5_mmu_config(pyrd_seq_lvl6, L6_MMU_NUM);
		else if (py_level == SEVEN_LEVEL)
			dolby5_mmu_config(pyrd_seq_lvl7, L7_MMU_NUM);
	}
	if (py_level != NO_LEVEL) {
		VSYNC_WR_DV_REG_BITS(DOLBY_TOP2_CTRL0, 1, 0, 2);
		pyrd_crtl |= 1 << 31;
		VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_CTRL, pyrd_crtl);/*pyramid sw rst*/
		pyrd_crtl &= ~(1 << 31);
		VSYNC_WR_DV_REG(DOLBY_TOP2_PYRD_CTRL, pyrd_crtl);
	}
#else /*must set py_level before next vsync*/
	/*VPU_DOLBY_WRAP_GCLK bit16 reset total dolby core, need reload pyramid mmu*/
	WRITE_VPP_DV_REG_BITS(DOLBY_TOP2_PYRD_CTRL, py_level, 0, 2); /*must set before next vsync*/
	if (/*enable_top1 && */(last_py_level != py_level || reset)) {
		WRITE_VPP_DV_REG_BITS(DOLBY_TOP2_PYRD_CTRL, 1, 30, 1);/*pyramid mmu rst*/
		WRITE_VPP_DV_REG_BITS(DOLBY_TOP2_PYRD_CTRL, 0, 30, 1);
		if (py_level == SIX_LEVEL)
			dolby5_mmu_config(pyrd_seq_lvl6, L6_MMU_NUM);
		else if (py_level == SEVEN_LEVEL)
			dolby5_mmu_config(pyrd_seq_lvl7, L7_MMU_NUM);
	}
#endif
	last_py_level = py_level;

	if (reset || toggle) {/*no need set these regs when no toggle*/
		/*t3x from ucode 0x152b,it is error*/
		if (hdmi) {
			VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_DTNL, 0x2c2d0, 0, 18);
			VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_DTNL, hsize, 18, 13);
		}

		if (hdmi && !hdr10 && !dv_unique_drm) {
			/*hdmi DV STD and DV LL:  need detunnel*/
			if (slice_num == 2)
				VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_IRQ, 3, 19, 2);
			else
				VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_IRQ, 1, 19, 1);
		} else {
			if (slice_num == 2)
				VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_IRQ, 0, 19, 2);
			else
				VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_IRQ, 0, 19, 1);
		}

		if (top2_info.core_on && !bypass_tvcore) {
			if (is_aml_t3x()) {
				VSYNC_WR_DV_REG_BITS
					(T3X_VD1_S0_DV_BYPASS_CTRL,
					 1, 0, 1);
				VSYNC_WR_DV_REG_BITS
					(T3X_VD1_S1_DV_BYPASS_CTRL,
					 slice_num == 2 ? 1 : 0, 0, 1);
			}
		} else if (top2_info.core_on && bypass_tvcore) {
			if (is_aml_t3x()) {
				VSYNC_WR_DV_REG_BITS
					(T3X_VD1_S0_DV_BYPASS_CTRL,
					 0, 0, 1);
				VSYNC_WR_DV_REG_BITS
					(T3X_VD1_S1_DV_BYPASS_CTRL,
					 0, 0, 1);
			}
		}
		dolby5_dpth_ctrl(hsize, vsize, vd_proc_info);

		VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_CTRL, 0, 31, 1);//top2 enable

		if (!enable_top1 || (test_dv & DEBUG_ENABLE_TOP2_INT))
			VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_IRQ, 1, 1, 1); //top2 dolby int, pulse
		else
			VSYNC_WR_DV_REG_BITS(VPU_DOLBY_WRAP_IRQ, 1, 0, 1); //top2 dolby int, disable

		py_stride[0] = top1_stride_rdmif(1024, 10);
		py_stride[1] = top1_stride_rdmif(512, 10);
		py_stride[2] = top1_stride_rdmif(256, 10);
		py_stride[3] = top1_stride_rdmif(128, 10);
		py_stride[4] = top1_stride_rdmif(64, 10);
		py_stride[5] = top1_stride_rdmif(32, 10);
		py_stride[6] = top1_stride_rdmif(16, 10);

		/*if not get a frame in advance,there will no pyramid.use zero pyramid*/
		if (enable_top1 && !pr_done) {
			for (i = 0; i < 7; i++)
				py_baddr[i] = py_addr[2].top1_py_paddr[i];
			if (debug_dolby & 1)
				pr_dv_dbg("missed top1\n");
		}

		dolby5_top2_ini(vd1_slice0_hsize, vd1_slice0_vsize,
				py_baddr, py_stride);

		//VSYNC_WR_DV_REG_BITS(DOLBY_TOP2_RDMA_CTRL, 1, 28, 1);//shadow_en
	    //VSYNC_WR_DV_REG_BITS(DOLBY_TOP2_RDMA_CTRL, 1, 29, 1);//shadow_rst
		if (force_enable_top12_lut) {
			VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_RDMA_CTRL, 0x1, 16, 3);
			VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_RDMA_CTRL, 0x95, 0, 16);
			VSYNC_WR_DV_REG_BITS(DOLBY_TOP1_RDMA_CTRL, 1, 30, 1);//open top1 rdma
		}
		VSYNC_WR_DV_REG_BITS(DOLBY_TOP2_RDMA_CTRL, 1, 30, 1);//open top2 rdma
	}

	if (reset || toggle) {
		top2_ahb_ini(reg_data);
	} else { /*set every vsync*/
		VSYNC_WR_DV_REG(DOLBY5_CORE2_REG_BASE0 + 2, 1);/*Metadata Program start*/
		VSYNC_WR_DV_REG(DOLBY5_CORE2_REG_BASE0 + 4, last_int_top2);/*clear interrupt*/
		VSYNC_WR_DV_REG(DOLBY5_CORE2_REG_BASE0 + 3, 1);/*Metadata Program end */
	}
	if ((dolby_vision_flags & FLAG_CERTIFICATION) && is_amdv_on())/*set every vsync*/
		VSYNC_WR_DV_REG(DOLBY5_CORE2_CRC_CNTRL, 1);

	if (reset || toggle) {/*no need update lut data when no toggle*/
		set_dovi_setting_update_flag(true);
		amdv_update_setting(NULL);
		dma_lut_write();
	}

	if (lut_trigger_by_reg) {/*set every vsync*/
		/*use reg to trigger lut, should after DOLBY_TOP2_RDMA set */
		top_misc = VSYNC_RD_DV_REG(VPU_TOP_MISC);
		top_misc |= 1 << 10;
		VSYNC_WR_DV_REG(VPU_TOP_MISC, top_misc);
		top_misc &= ~(1 << 10);
		VSYNC_WR_DV_REG(VPU_TOP_MISC, top_misc);
	}

	return 0;
}

/*toggle:true——update all reg. false——only update dolby inside reg*/
int tv_top_set(u64 *top1_reg,
				 u64 *top1b_reg,
				 u64 *top2_reg,
			     int hsize,
			     int vsize,
			     int video_enable,
			     int src_chroma_format,
			     bool hdmi,
			     bool hdr10,
			     bool reset,
			     bool toggle,
			     bool pr_done)
{
	u32 top_misc;

	if (enable_top1) {
		if (!top1_info.core_on) {
			reset = true;/*first frame with top1, reset*/
			/*reset pyramid index*/
			py_wr_id = 0;
			py_rd_id = 0;
			l1l4_rd_index = 0;
			l1l4_wr_index = 0;
			isr_cnt = 0;
			top1_done = false;
		}

		/*update pyramid write index when toggle new frame, except first frame*/
		if (top1_info.core_on_cnt != 0) {
			if (reset || toggle)
				py_wr_id = py_wr_id ^ 1;
		}

		tv_top1_set(top1_reg, top1b_reg, reset,
			video_enable, toggle);
		if (!top1_info.core_on)
			top1_info.core_on_cnt = 0;
		if (video_enable && toggle) {
			++top1_info.core_on_cnt;
			new_top1_toggle = true;
		}
	} else {
		top1_info.core_on = false;
		top1_info.core_on_cnt = 0;
		if (!top2_info.core_on)
			isr_cnt = 0;
	}

	/*first frame with top1, not enable top2*/
	if (!enable_top1 || top1_info.core_on) {
		if (/*top1_info.core_on_cnt == 0 &&*/
			top2_info.core_on_cnt == 0)
			reset = true;/*first frame with top2, reset*/

		/*update pyramid read index when toggle new frame, except first frame*/
		if (top1_info.core_on && top2_info.core_on_cnt != 0) {
			if (reset || toggle)
				py_rd_id = py_rd_id ^ 1;
		}

		tv_top2_set(top2_reg, hsize, vsize, video_enable,
				src_chroma_format, hdmi, hdr10, reset, toggle, pr_done);
		if (video_enable && toggle)
			++top2_info.core_on_cnt;
	}

	/*first frame with only top1, trigger lut dma*/
	if (enable_top1 && top2_info.core_on_cnt == 0) {
		if (lut_trigger_by_reg) {/*set every vsync*/
			/*use reg to trigger lut, should after DOLBY_TOP_RDMA set */
			top_misc = VSYNC_RD_DV_REG(VPU_TOP_MISC);
			top_misc |= 1 << 10;
			VSYNC_WR_DV_REG(VPU_TOP_MISC, top_misc);
			top_misc &= ~(1 << 10);
			VSYNC_WR_DV_REG(VPU_TOP_MISC, top_misc);
		}
	}

	return 0;
}

int dma_lut_init(void)
{
	int ret;
	u32 channel = DV_CHAN;
	struct lut_dma_set_t lut_dma_set;

	lut_dma_set.channel = channel;
	lut_dma_set.dma_dir = LUT_DMA_WR;
	if (lut_trigger_by_reg)
		lut_dma_set.irq_source = REG_TRIGGER;/*1<<5*/
	else
		lut_dma_set.irq_source = VIU1_VSYNC;/*1<<0*/

	pr_dv_dbg("lut_dma_set.irq_source %d\n", lut_dma_set.irq_source);
	lut_dma_set.mode = LUT_DMA_MANUAL;
	lut_dma_set.table_size = lut_dma_info[0].dma_total_size;
	ret = lut_dma_register(&lut_dma_set);
	if (ret >= 0) {
		lut_dma_support = true;
	} else {
		pr_info("%s failed, not support\n", __func__);
		lut_dma_support = false;
	}
	return ret;
}

void dma_lut_uninit(void)
{
	u32 channel = DV_CHAN;

	lut_dma_unregister(LUT_DMA_WR, channel);
}

int dma_lut_write(void)
{
	int dma_size;
	ulong phy_addr;
	u32 channel = DV_CHAN;

	if (!lut_dma_support)
		return 0;

	if (force_enable_top12_lut) {
		dma_size = lut_dma_info[0].dma_total_size;
		phy_addr = (ulong)(lut_dma_info[cur_dmabuf_id].dma_paddr);
	} else {
		if (enable_top1) {
			if (!top1_info.core_on) {
				dma_size = lut_dma_info[0].dma_total_size -
					lut_dma_info[0].dma_top2_size;
				phy_addr = (ulong)(lut_dma_info[cur_dmabuf_id].dma_paddr);
			} else {
				dma_size = lut_dma_info[0].dma_total_size;
				phy_addr = (ulong)(lut_dma_info[cur_dmabuf_id].dma_paddr);
			}
		} else {
			dma_size = lut_dma_info[0].dma_top2_size;
			phy_addr = (ulong)(lut_dma_info[cur_dmabuf_id].dma_paddr);
		}
	}

	cur_dmabuf_id = cur_dmabuf_id ^ 1;
	lut_dma_write_phy_addr(channel,
			       phy_addr,
			       dma_size);
	return 1;
}

void get_l1l4_hist(void)
{
	u32 index;

	if (!tv_hw5_setting)
		return;

	if (!enable_top1) {
		tv_hw5_setting->top1_stats.enable = false;
		return;
	}

	tv_hw5_setting->top1_stats.enable = true;

	index = l1l4_rd_index;
	memcpy(tv_hw5_setting->top1_stats.hist,
		dv5_md_hist.histogram[index], sizeof(dv5_md_hist.histogram[index]));
	tv_hw5_setting->top1_stats.top1_l1l4.l1_min = dv5_md_hist.l1l4_md[index][0];
	tv_hw5_setting->top1_stats.top1_l1l4.l1_max = dv5_md_hist.l1l4_md[index][1];
	tv_hw5_setting->top1_stats.top1_l1l4.l1_mid = dv5_md_hist.l1l4_md[index][2];
	tv_hw5_setting->top1_stats.top1_l1l4.l4_std = dv5_md_hist.l1l4_md[index][3];

	if (debug_dolby & 0x100000)
		pr_info("get hist[%d], index %d/%d, l1l4_distance %d\n",
			index, l1l4_rd_index, l1l4_wr_index, l1l4_distance);

	if (dolby_vision_flags & FLAG_CERTIFICATION) {
		if (top2_info.core_on_cnt >= l1l4_distance)/*cmodel hist delay one or two frame*/
			l1l4_rd_index = (l1l4_rd_index + 1) % HIST_BUF_COUNT;
	} else {
		l1l4_rd_index = (l1l4_rd_index + 1) % HIST_BUF_COUNT;
	}
}

#define FOR_DEBUG 0

#if FOR_DEBUG
u16 histogram[128];
u8 hist[256];
#endif
void set_l1l4_hist(void)
{
	u32 index;
	u32 i;
	u32 metadata0;
	u32 metadata1;

	if (!tv_hw5_setting || !enable_top1)
		return;

	metadata0 = READ_VPP_DV_REG(DOLBY5_CORE1_L1_MINMAX);
	metadata1 = READ_VPP_DV_REG(DOLBY5_CORE1_L1_MID_L4);

	if (new_top1_toggle) {
		new_top1_toggle = false;
		if (top1_info.core_on_cnt > 1)
			l1l4_wr_index = (l1l4_wr_index + 1) % HIST_BUF_COUNT;
	}

	index = l1l4_wr_index;

	dv5_md_hist.l1l4_md[index][0] = metadata0 & 0xffff;
	dv5_md_hist.l1l4_md[index][1] = metadata0 >> 16;
	dv5_md_hist.l1l4_md[index][2] = metadata1 & 0xffff;
	dv5_md_hist.l1l4_md[index][3] = metadata1 >> 16;

	memcpy(&dv5_md_hist.hist[0], dv5_md_hist.hist_vaddr[0], 256);

	for (i = 0; i < 256 / 2; i++)
		dv5_md_hist.histogram[index][i] = dv5_md_hist.hist[i * 2 + 0] |
			(dv5_md_hist.hist[i * 2 + 1] << 8);

	if (debug_dolby & 0x100000) {
		pr_info("set top1 hist[%d]:\n", index);
		for (i = 0; i < 128 / 8; i++)
			pr_info("%d, %d, %d, %d, %d, %d, %d, %d\n",
				dv5_md_hist.histogram[index][i * 8],
				dv5_md_hist.histogram[index][i * 8 + 1],
				dv5_md_hist.histogram[index][i * 8 + 2],
				dv5_md_hist.histogram[index][i * 8 + 3],
				dv5_md_hist.histogram[index][i * 8 + 4],
				dv5_md_hist.histogram[index][i * 8 + 5],
				dv5_md_hist.histogram[index][i * 8 + 6],
				dv5_md_hist.histogram[index][i * 8 + 7]);
		pr_info("meta0: 0x%x, meta1: 0x%x, l1l4_index %d/%d\n",
			metadata0, metadata1, l1l4_rd_index, l1l4_wr_index);

		#if FOR_DEBUG
			index = index ^ 1;
			memcpy(&hist[0], dv5_md_hist.hist_vaddr[index], 256);
			for (i = 0; i < 256 / 2; i++)
				histogram[i] = hist[i * 2 + 0] |
					(hist[i * 2 + 1] << 8);
			pr_info("top1 hist[%d]:\n", index);
			for (i = 0; i < 3; i++)
				pr_info("%d, %d, %d, %d, %d, %d, %d, %d\n",
					histogram[i * 8],
					histogram[i * 8 + 1],
					histogram[i * 8 + 2],
					histogram[i * 8 + 3],
					histogram[i * 8 + 4],
					histogram[i * 8 + 5],
					histogram[i * 8 + 6],
					histogram[i * 8 + 7]);
		#endif
	}
}

irqreturn_t amdv_isr(int irq, void *dev_id)
{
	/*1:top1 done;2+3:top1+top2 done;4+5:top1+top2 done*/

	//bool reset = false;

	isr_cnt++;
	if (debug_dolby & 0x1)
		pr_info("amdv_isr_cnt %d\n", isr_cnt);

	if (enable_top1) {
		top1_done = true;

		set_l1l4_hist();

		WRITE_VPP_DV_REG_BITS(VPU_DOLBY_WRAP_IRQ, 1, 8, 1); //top1 int, clear
		WRITE_VPP_DV_REG_BITS(VPU_DOLBY_WRAP_IRQ, 0, 8, 1);

	} else {
		WRITE_VPP_DV_REG_BITS(VPU_DOLBY_WRAP_IRQ, 1, 9, 1); //top2 int, clear
		WRITE_VPP_DV_REG_BITS(VPU_DOLBY_WRAP_IRQ, 0, 9, 1);
	}

	return IRQ_HANDLED;
}

void print_dv_ro(void)
{
	if (is_aml_t3x() && (enable_top1 || force_enable_top12_lut)) {
		if (debug_dolby & 0x4000)
			pr_info("vysnc-top1: RO %x %x %x %x %x %x, %x\n",
					READ_VPP_DV_REG(DOLBY_TOP1_RO_6),
					READ_VPP_DV_REG(DOLBY_TOP1_RO_5),
					READ_VPP_DV_REG(DOLBY_TOP1_RO_4),
					READ_VPP_DV_REG(DOLBY_TOP1_RO_3),
					READ_VPP_DV_REG(DOLBY_TOP1_RO_2),
					READ_VPP_DV_REG(DOLBY_TOP1_RO_1),
					READ_VPP_DV_REG(DOLBY_TOP1_RO_0));
	}

	if (is_aml_t3x() && top2_info.core_on) {
		if (debug_dolby & 0x4000)
			pr_info("vysnc-top2: RO %x %x %x %x %x %x, frame st %x %x\n",
					READ_VPP_DV_REG(DOLBY_TOP2_RO_5),
					READ_VPP_DV_REG(DOLBY_TOP2_RO_4),
					READ_VPP_DV_REG(DOLBY_TOP2_RO_3),
					READ_VPP_DV_REG(DOLBY_TOP2_RO_2),
					READ_VPP_DV_REG(DOLBY_TOP2_RO_1),
					READ_VPP_DV_REG(DOLBY_TOP2_RO_0),
					READ_VPP_DV_REG(DOLBY5_CORE2_INP_FRM_ST),
					READ_VPP_DV_REG(DOLBY5_CORE2_OP_FRM_ST));
	}

	top1_crc_ro();
}

