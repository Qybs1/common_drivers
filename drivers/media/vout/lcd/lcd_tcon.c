// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 *
 * Copyright (C) 2019 Amlogic, Inc. All rights reserved.
 *
 */

#include <linux/init.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/major.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/reset.h>
#include <linux/of_reserved_mem.h>
#include <linux/cma.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-mapping.h>
#include <linux/sched/clock.h>
#include <linux/amlogic/aml_free_reserved.h>
#include <linux/amlogic/media/vout/lcd/lcd_vout.h>
#include <linux/amlogic/media/vout/lcd/lcd_unifykey.h>
#include <linux/amlogic/media/vout/lcd/lcd_notify.h>
#include <linux/amlogic/media/vout/lcd/lcd_tcon_fw.h>
#include <linux/amlogic/media/vout/vout_notify.h>
#ifdef CONFIG_AMLOGIC_BACKLIGHT
#include <linux/amlogic/media/vout/lcd/aml_bl.h>
#endif
#include <linux/fs.h>
#include <linux/uaccess.h>
#if IS_ENABLED(CONFIG_AMLOGIC_TEE)
#include <linux/amlogic/tee.h>
#endif
#include "lcd_common.h"
#include "lcd_reg.h"
#include "lcd_tcon.h"
#include "lcd_tcon_pdf.h"
#include "lcd_tcon_swpdf.h"

enum {
	TCON_AXI_MEM_TYPE_OD = 0,
	TCON_AXI_MEM_TYPE_DEMURA,
};

static struct lcd_tcon_config_s *lcd_tcon_conf;
static struct tcon_rmem_s tcon_rmem = {
	.flag = 0,
	.rsv_mem_size = 0,
	.rsv_mem_paddr = 0,
};

static struct tcon_mem_map_table_s tcon_mm_table = {
	.version = 0xff,
	.block_cnt = 0,
	.data_complete = 0,
	.bin_path_valid = 0,

	.lut_valid_flag = 0,
};
static struct lcd_tcon_local_cfg_s tcon_local_cfg;

static int lcd_tcon_data_multi_remvoe(struct tcon_mem_map_table_s *mm_table);
static int lcd_tcon_data_multi_reset(struct tcon_mem_map_table_s *mm_table);

#ifdef TCON_DBG_TIME
static unsigned long long *dbg_vsync_time, *dbg_list_trave_time;
static unsigned int dbg_vsync_cnt, dbg_list_trave_cnt;
static unsigned int dbg_cnt_0, dbg_cnt_1;

static inline void lcd_tcon_dbg_vsync_time_save(unsigned long long n, int line_cnt, int flag)
{
	if (!dbg_vsync_time)
		return;

	if (dbg_vsync_cnt >= dbg_cnt_0)
		dbg_vsync_cnt = 0;
	if ((flag >> 12) & 0xf) {
		dbg_vsync_time[dbg_vsync_cnt++] = n;
		dbg_vsync_time[dbg_vsync_cnt++] = 0xffff000000000000 | (line_cnt << 16) | flag;
	} else {
		dbg_vsync_time[dbg_vsync_cnt++] = n;
		dbg_vsync_time[dbg_vsync_cnt++] = (line_cnt << 16) | flag;
	}
}

static inline void lcd_tcon_dbg_list_trave_time_save(unsigned long long n, int flag)
{
	if (!dbg_list_trave_time)
		return;

	if (dbg_list_trave_cnt >= dbg_cnt_1)
		dbg_list_trave_cnt = 0;
	if ((flag >> 12) & 0xf) {
		dbg_list_trave_time[dbg_list_trave_cnt++] = n;
		dbg_list_trave_time[dbg_list_trave_cnt++] = 0xffff111100000000 | flag;
	} else {
		dbg_list_trave_time[dbg_list_trave_cnt++] = n;
		dbg_list_trave_time[dbg_list_trave_cnt++] = flag;
	}
}

static void lcd_tcon_time_print(unsigned long long *table)
{
	unsigned int len, i;
	char *buf;

	buf = kcalloc(1024, sizeof(char), GFP_KERNEL);
	if (!buf)
		return;

	len = 0;
	for (i = 0; i < 9; i++)
		len += sprintf(buf + len, " %llu,", table[i]);
	len += sprintf(buf + len, " %llu", table[9]);
	pr_err("%s\n", buf);

	kfree(buf);
}

void lcd_tcon_dbg_trace_clear(void)
{
	memset(tcon_mm_table.vsync_time, 0, sizeof(unsigned long long) * 10);
	memset(tcon_mm_table.list_trave_time, 0, sizeof(unsigned long long) * 10);

	if (dbg_vsync_time)
		memset(dbg_vsync_time, 0, sizeof(unsigned long long) * dbg_cnt_0);
	if (dbg_list_trave_time)
		memset(dbg_list_trave_time, 0, sizeof(unsigned long long) * dbg_cnt_1);
	dbg_vsync_cnt = 0;
	dbg_list_trave_cnt = 0;
}

void lcd_tcon_dbg_trace_print(unsigned int flag)
{
	char *buf;
	unsigned long long data;
	unsigned int len, n, m;
	int i, j;

	len = 24 * 20;
	buf = kcalloc(len, sizeof(char), GFP_KERNEL);
	if (!buf)
		return;

	pr_err("vsync_time:\n");
	lcd_tcon_time_print(tcon_mm_table.vsync_time);
	pr_err("list_trave_time:\n");
	lcd_tcon_time_print(tcon_mm_table.list_trave_time);

	if (flag & 0x1) {
		pr_info("\ndbg_vsync_time:\n");
		if (dbg_vsync_time) {
			for (i = 0; i < dbg_cnt_0; i += 8) {
				n = 0;
				m = 0;
				for (j = 0; j < 8; j++) {
					data = dbg_vsync_time[i + j];
					if (j % 2)
						n += sprintf(buf + n, " 0x%llx", data);
					else
						n += sprintf(buf + n, " %llu", data);
					if (data)
						m = 1;
				}
				pr_err("%s\n", buf);
				if (m == 0)
					break;
			}
		}
	}

	if (flag & 0x2) {
		pr_info("\ndbg_list_trave_time:\n");
		if (dbg_list_trave_time) {
			for (i = 0; i < dbg_cnt_1; i += 8) {
				n = 0;
				m = 0;
				for (j = 0; j < 8; j++) {
					data = dbg_list_trave_time[i + j];
					if (j % 2)
						n += sprintf(buf + n, " 0x%llx", data);
					else
						n += sprintf(buf + n, " %llu", data);
					if (data)
						m = 1;
				}
				pr_err("%s\n", buf);
				if (m == 0)
					break;
			}
		}
	}
	kfree(buf);
}
#endif

/* **********************************
 * tcon common function
 * **********************************
 */
int lcd_tcon_valid_check(void)
{
	if (!lcd_tcon_conf) {
		LCDERR("invalid tcon data\n");
		return -1;
	}
	if (lcd_tcon_conf->tcon_valid == 0) {
		LCDERR("invalid tcon\n");
		return -1;
	}

	return 0;
}

struct lcd_tcon_config_s *get_lcd_tcon_config(void)
{
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return NULL;

	return lcd_tcon_conf;
}

struct tcon_rmem_s *get_lcd_tcon_rmem(void)
{
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return NULL;

	return &tcon_rmem;
}

struct tcon_mem_map_table_s *get_lcd_tcon_mm_table(void)
{
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return NULL;

	return &tcon_mm_table;
}

struct lcd_tcon_local_cfg_s *get_lcd_tcon_local_cfg(void)
{
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return NULL;

	return &tcon_local_cfg;
}

unsigned int lcd_tcon_data_size_align(unsigned int size)
{
	unsigned int new_size;

	/* ready for burst 128bit */
	new_size = ((size + 15) / 16) * 16;

	return new_size;
}

unsigned char lcd_tcon_checksum(unsigned char *buf, unsigned int len)
{
	unsigned int temp = 0;
	unsigned int i;

	if (!buf)
		return 0;
	if (len == 0)
		return 0;
	for (i = 0; i < len; i++)
		temp += buf[i];

	return (unsigned char)(temp & 0xff);
}

unsigned char lcd_tcon_lrc(unsigned char *buf, unsigned int len)
{
	unsigned char temp = 0;
	unsigned int i;

	if (!buf)
		return 0xff;
	if (len == 0)
		return 0xff;
	temp = buf[0];
	for (i = 1; i < len; i++)
		temp = temp ^ buf[i];

	return temp;
}

void lcd_tcon_mem_sync(struct aml_lcd_drv_s *pdrv,
		unsigned long paddr, unsigned int mem_size)
{
	if (!pdrv || !paddr || !mem_size)
		return;

	if (lcd_debug_print_flag & LCD_DBG_PR_ISR)
		LCDPR("%s: paddr=0x%lx, mem_size=%#x\n", __func__, paddr, mem_size);

	dma_sync_single_for_device(pdrv->dev,
		paddr,
		PAGE_ALIGN(mem_size),
		DMA_TO_DEVICE);
}

unsigned char *lcd_tcon_paddrtovaddr(unsigned long paddr, unsigned int mem_size)
{
	unsigned int highmem_flag = 0;
	int max_mem_size = 0;
	void *vaddr = NULL;

	if (tcon_rmem.flag == 0) {
		LCDPR("%s: invalid paddr\n", __func__);
		return NULL;
	}

	if (tcon_rmem.flag == 1) {
		highmem_flag = PageHighMem(phys_to_page(paddr));
		if (lcd_debug_print_flag & LCD_DBG_PR_ISR) {
			LCDPR("%s: paddr 0x%lx highmem_flag:%d\n",
				__func__, paddr, highmem_flag);
		}
		if (highmem_flag) {
			max_mem_size = PAGE_ALIGN(mem_size);
			max_mem_size = roundup(max_mem_size, PAGE_SIZE);
			vaddr = lcd_vmap(paddr, max_mem_size);
			if (!vaddr) {
				LCDPR("tcon paddr mapping error: addr: 0x%lx\n",
					(unsigned long)paddr);
				return NULL;
			}
			/*LCDPR("tcon vaddr: 0x%p\n", vaddr);*/
		} else {
			vaddr = phys_to_virt(paddr);
			if (!vaddr) {
				LCDERR("tcon vaddr mapping failed: 0x%lx\n",
					(unsigned long)paddr);
				return NULL;
			}
			/*LCDPR("tcon vaddr: 0x%p\n", vaddr);*/
		}
	} else if (tcon_rmem.flag == 2) {
		vaddr = ioremap_cache(paddr, mem_size);
		if (!vaddr) {
			LCDERR("tcon vaddr mapping failed: 0x%lx, size: 0x%x\n",
				(unsigned long)paddr, mem_size);
			return NULL;
		}

		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
			LCDPR("tcon paddr:0x%lx, vaddr:0x%px, size:0x%x\n",
				(unsigned long)paddr, vaddr, mem_size);
		}
	}

	return (unsigned char *)vaddr;
}

/* **********************************
 * tcon function api
 * **********************************
 */
//ret:  bit[0]:tcon: fatal error, block driver
//      bit[1]:tcon: warning, only print warning message
int lcd_tcon_init_setting_check(struct aml_lcd_drv_s *pdrv, struct lcd_detail_timing_s *ptiming,
		unsigned char *core_reg_table)
{
	char *ferr_str, *warn_str;
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return -1;

	ferr_str = kzalloc(PR_BUF_MAX, GFP_KERNEL);
	if (!ferr_str) {
		LCDERR("tcon: setting_check fail for NOMEM\n");
		return 0;
	}
	warn_str = kzalloc(PR_BUF_MAX, GFP_KERNEL);
	if (!warn_str) {
		LCDERR("tcon: setting_check fail for NOMEM\n");
		kfree(ferr_str);
		return 0;
	}

	if (lcd_tcon_conf->tcon_check)
		ret = lcd_tcon_conf->tcon_check(pdrv, ptiming, core_reg_table, ferr_str, warn_str);
	else
		ret = 0;

	if (ret) {
		pr_err("**************** lcd tcon setting check ****************\n");
		if (ret & 0x1) {
			pr_err("lcd: tcon: FATAL ERROR:\n"
				"%s\n", ferr_str);
		}
		if (ret & 0x2) {
			pr_err("lcd: tcon: WARNING:\n"
				"%s\n", warn_str);
		}
		pr_err("************** lcd tcon setting check end ****************\n");
	}
	kfree(ferr_str);
	kfree(warn_str);

	return ret;
}

unsigned int lcd_tcon_reg_read(struct aml_lcd_drv_s *pdrv, unsigned int addr)
{
	unsigned int val;
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return 0;

	if (addr < lcd_tcon_conf->top_reg_base) {
		if (lcd_tcon_conf->core_reg_width == 8)
			val = lcd_tcon_read_byte(pdrv, addr);
		else
			val = lcd_tcon_read(pdrv, addr);
	} else {
		val = lcd_tcon_read(pdrv, addr);
	}

	return val;
}

void lcd_tcon_reg_write(struct aml_lcd_drv_s *pdrv,
			unsigned int addr, unsigned int val)
{
	unsigned char temp;
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return;

	if (addr < lcd_tcon_conf->top_reg_base) {
		if (lcd_tcon_conf->core_reg_width == 8) {
			temp = (unsigned char)val;
			lcd_tcon_write_byte(pdrv, addr, temp);
		} else {
			lcd_tcon_write(pdrv, addr, val);
		}
	} else {
		lcd_tcon_write(pdrv, addr, val);
	}
}

#define PR_LINE_BUF_MAX    200
void lcd_tcon_reg_table_print(void)
{
	int i, j, n, cnt, left;
	char *buf;
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return;

	if (!tcon_mm_table.core_reg_table) {
		LCDERR("%s: reg_table is null\n", __func__);
		return;
	}

	buf = kcalloc(PR_LINE_BUF_MAX, sizeof(char), GFP_KERNEL);
	if (!buf)
		return;

	LCDPR("%s:\n", __func__);
	cnt = tcon_mm_table.core_reg_table_size;
	for (i = 0; i < cnt; i += 16) {
		n = snprintf(buf, PR_LINE_BUF_MAX, "%04x: ", i);
		for (j = 0; j < 16; j++) {
			if ((i + j) >= cnt)
				break;
			left = PR_LINE_BUF_MAX - n - 1;
			n += snprintf(buf + n, left, " %02x",
				      tcon_mm_table.core_reg_table[i + j]);
		}
		buf[n] = '\0';
		pr_info("%s\n", buf);
	}
	kfree(buf);
}

void lcd_tcon_reg_readback_print(struct aml_lcd_drv_s *pdrv)
{
	int i, j, n, cnt, offset, left;
	char *buf;
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return;

	buf = kcalloc(PR_LINE_BUF_MAX, sizeof(char), GFP_KERNEL);
	if (!buf)
		return;

	LCDPR("%s:\n", __func__);
	cnt = tcon_mm_table.core_reg_table_size;
	offset = lcd_tcon_conf->core_reg_start;
	if (lcd_tcon_conf->core_reg_width == 8) {
		for (i = offset; i < cnt; i += 16) {
			n = snprintf(buf, PR_LINE_BUF_MAX, "%04x: ", i);
			for (j = 0; j < 16; j++) {
				if ((i + j) >= cnt)
					break;
				left = PR_LINE_BUF_MAX - n - 1;
				n += snprintf(buf + n, left, " %02x",
					lcd_tcon_read_byte(pdrv, i + j));
			}
			buf[n] = '\0';
			pr_info("%s\n", buf);
		}
	} else {
		if (lcd_tcon_conf->reg_table_width == 32) {
			cnt /= 4;
			for (i = offset; i < cnt; i += 4) {
				n = snprintf(buf, PR_LINE_BUF_MAX, "%04x: ", i);
				for (j = 0; j < 4; j++) {
					if ((i + j) >= cnt)
						break;
					left = PR_LINE_BUF_MAX - n - 1;
					n += snprintf(buf + n, left, " %08x",
						lcd_tcon_read(pdrv, i + j));
				}
				buf[n] = '\0';
				pr_info("%s\n", buf);
			}
		} else {
			for (i = offset; i < cnt; i += 16) {
				n = snprintf(buf, PR_LINE_BUF_MAX, "%04x: ", i);
				for (j = 0; j < 16; j++) {
					if ((i + j) >= cnt)
						break;
					left = PR_LINE_BUF_MAX - n - 1;
					n += snprintf(buf + n, left, " %02x",
						lcd_tcon_read(pdrv, i + j));
				}
				buf[n] = '\0';
				pr_info("%s\n", buf);
			}
		}
	}

	kfree(buf);
}

unsigned int lcd_tcon_table_read(unsigned int addr)
{
	unsigned char *table8;
	unsigned int *table32, size = 0, val = 0;
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return 0;

	if (!tcon_mm_table.core_reg_table) {
		LCDERR("tcon reg_table is null\n");
		return 0;
	}

	if (lcd_tcon_conf->core_reg_width == 8)
		size = tcon_mm_table.core_reg_table_size;
	else
		size = tcon_mm_table.core_reg_table_size / 4;
	if (addr >= size) {
		LCDERR("invalid tcon reg_table addr: 0x%04x\n", addr);
		return 0;
	}

	if (lcd_tcon_conf->core_reg_width == 8) {
		table8 = tcon_mm_table.core_reg_table;
		val = table8[addr];
	} else {
		table32 = (unsigned int *)tcon_mm_table.core_reg_table;
		val = table32[addr];
	}

	return val;
}

unsigned int lcd_tcon_table_write(unsigned int addr, unsigned int val)
{
	unsigned char *table8;
	unsigned int *table32, size = 0, read_val = 0;
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return 0;

	if (!tcon_mm_table.core_reg_table) {
		LCDERR("tcon reg_table is null\n");
		return 0;
	}

	if (lcd_tcon_conf->core_reg_width == 8)
		size = tcon_mm_table.core_reg_table_size;
	else
		size = tcon_mm_table.core_reg_table_size / 4;
	if (addr >= size) {
		LCDERR("invalid tcon reg_table addr: 0x%04x\n", addr);
		return 0;
	}

	if (lcd_tcon_conf->core_reg_width == 8) {
		table8 = tcon_mm_table.core_reg_table;
		table8[addr] = (unsigned char)(val & 0xff);
		read_val = table8[addr];
	} else {
		table32 = (unsigned int *)tcon_mm_table.core_reg_table;
		table32[addr] = val;
		read_val = table32[addr];
	}

	return read_val;
}

int lcd_tcon_core_reg_get(struct aml_lcd_drv_s *pdrv,
			  unsigned char *buf, unsigned int size)
{
	unsigned int reg_max = 0, offset = 0, val;
	int i, ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return -1;

	if (size > lcd_tcon_conf->reg_table_len) {
		LCDERR("%s: size 0x%x is not enough(0x%x)\n",
		       __func__, size, lcd_tcon_conf->reg_table_len);
		return -1;
	}

	offset = lcd_tcon_conf->core_reg_start;
	if (lcd_tcon_conf->core_reg_width == 8) {
		for (i = offset; i < size; i++)
			buf[i] = lcd_tcon_read_byte(pdrv, i);
	} else {
		reg_max = size / 4;
		for (i = offset; i < reg_max; i++) {
			val = lcd_tcon_read(pdrv, i);
			buf[i * 4] = val & 0xff;
			buf[i * 4 + 1] = (val >> 8) & 0xff;
			buf[i * 4 + 2] = (val >> 16) & 0xff;
			buf[i * 4 + 3] = (val >> 24) & 0xff;
		}
	}

	return 0;
}

int lcd_tcon_od_set(struct aml_lcd_drv_s *pdrv, int flag)
{
	unsigned int reg, bit, temp;
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return -1;

	if (lcd_tcon_conf->reg_core_od == REG_LCD_TCON_MAX) {
		LCDERR("%s: invalid od reg\n", __func__);
		return -1;
	}

	if (flag) {
		if (tcon_rmem.flag == 0) {
			LCDERR("%s: invalid memory, disable od function\n",
				__func__);
			return -1;
		}
	}

	if (!(pdrv->status & LCD_STATUS_IF_ON))
		return -1;

	reg = lcd_tcon_conf->reg_core_od;
	bit = lcd_tcon_conf->bit_od_en;
	temp = flag ? 1 : 0;
	if (lcd_tcon_conf->core_reg_width == 8)
		lcd_tcon_setb_byte(pdrv, reg, temp, bit, 1);
	else
		lcd_tcon_setb(pdrv, reg, temp, bit, 1);

	lcd_delay_ms(100);
	LCDPR("%s: %d\n", __func__, flag);

	return 0;
}

int lcd_tcon_od_get(struct aml_lcd_drv_s *pdrv)
{
	unsigned int reg, bit, temp;
	int ret = 0;

	ret = lcd_tcon_valid_check();
	if (ret)
		return 0;

	if (lcd_tcon_conf->reg_core_od == REG_LCD_TCON_MAX) {
		LCDERR("%s: invalid od reg\n", __func__);
		return 0;
	}

	reg = lcd_tcon_conf->reg_core_od;
	bit = lcd_tcon_conf->bit_od_en;
	if (lcd_tcon_conf->core_reg_width == 8)
		temp = lcd_tcon_read_byte(pdrv, reg);
	else
		temp = lcd_tcon_read(pdrv, reg);
	ret = ((temp >> bit) & 1);

	return ret;
}

void lcd_tcon_global_reset(struct aml_lcd_drv_s *pdrv)
{
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return;

	if (lcd_tcon_conf->tcon_global_reset) {
		lcd_tcon_conf->tcon_global_reset(pdrv);
		LCDPR("reset tcon\n");
	}
}

int lcd_tcon_core_update(struct aml_lcd_drv_s *pdrv)
{
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return -1;

	lcd_tcon_core_reg_set(pdrv, lcd_tcon_conf,
		&tcon_mm_table, tcon_local_cfg.cur_core_reg_table);

	return 0;
}

int lcd_tcon_reload_pre(struct aml_lcd_drv_s *pdrv)
{
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return -1;

	lcd_tcon_conf->tcon_is_busy = 1;
	if (lcd_tcon_conf->tcon_reload_pre)
		lcd_tcon_conf->tcon_reload_pre(pdrv);

	return 0;
}

int lcd_tcon_reload(struct aml_lcd_drv_s *pdrv)
{
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return -1;

	if (lcd_tcon_conf->tcon_reload)
		lcd_tcon_conf->tcon_reload(pdrv);

	lcd_tcon_conf->tcon_is_busy = 0;

	return 0;
}

int lcd_tcon_enable(struct aml_lcd_drv_s *pdrv)
{
	struct lcd_tcon_fw_s *tcon_fw = aml_lcd_tcon_get_fw();
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return -1;

	if (lcd_tcon_conf->tcon_enable)
		lcd_tcon_conf->tcon_enable(pdrv);

	tcon_fw->tcon_state |= TCON_FW_STATE_TCON_EN;

	return 0;
}

void lcd_tcon_disable(struct aml_lcd_drv_s *pdrv)
{
	struct lcd_tcon_fw_s *tcon_fw = aml_lcd_tcon_get_fw();
	int ret;

	ret = lcd_tcon_valid_check();
	if (ret)
		return;

	LCDPR("%s\n", __func__);

	tcon_fw->tcon_state &= ~TCON_FW_STATE_TCON_EN;

	/* release data*/
	if (tcon_mm_table.version > 0 && tcon_mm_table.version < 0xff)
		lcd_tcon_data_multi_reset(&tcon_mm_table);

	if (lcd_tcon_conf->tcon_disable)
		lcd_tcon_conf->tcon_disable(pdrv);
	if (lcd_tcon_conf->tcon_global_reset) {
		lcd_tcon_conf->tcon_global_reset(pdrv);
		LCDPR("reset tcon\n");
	}
}

int lcd_tcon_top_init(struct aml_lcd_drv_s *pdrv)
{
	int ret = lcd_tcon_valid_check();

	if (ret)
		return ret;

	ret = -1;
	if (lcd_tcon_conf->tcon_top_init)
		ret = lcd_tcon_conf->tcon_top_init(pdrv);

	return ret;
}

void lcd_tcon_dbg_check(struct aml_lcd_drv_s *pdrv, struct lcd_detail_timing_s *ptiming)
{
	int ret;

	ret = lcd_tcon_init_setting_check(pdrv, ptiming, tcon_local_cfg.cur_core_reg_table);
	if (ret == 0)
		pr_info("lcd tcon setting check: PASS\n");
}

#ifdef TCON_DBG_TIME
static void lcd_tcon_time_sort_save(unsigned long long *table, unsigned long long data)
{
	int i, j;

	for (i = 9; i >= 0; i--) {
		if (data > table[i]) {
			for (j = 0; j < i; j++)
				table[j] = table[j + 1];
			table[i] = data;
			break;
		}
	}
}
#endif

static int lcd_tcon_data_multi_match_policy(struct aml_lcd_drv_s *pdrv, unsigned int frame_rate,
		struct tcon_data_multi_s *data_multi, struct tcon_data_list_s *data_list)
{
#ifdef CONFIG_AMLOGIC_BACKLIGHT
	struct aml_bl_drv_s *bldrv;
	struct bl_pwm_config_s *bl_pwm = NULL;
#endif
	unsigned int temp;

	switch (data_list->ctrl_method) {
	case LCD_TCON_DATA_CTRL_MULTI_VFREQ_DIRECT:
	case LCD_TCON_DATA_CTRL_MULTI_VFREQ_NOTIFY:

		if (frame_rate < data_list->min || frame_rate > data_list->max)
			goto lcd_tcon_data_multi_match_policy_exit;
		if (!data_multi)
			break;
		if (lcd_debug_print_flag & LCD_DBG_PR_ISR) {
			snprintf(data_multi->dbg_str, 64, "vfreq %d-%d hit, %d",
				data_list->min, data_list->max, frame_rate);
		}
		break;
	case LCD_TCON_DATA_CTRL_MULTI_BL_LEVEL:
#ifdef CONFIG_AMLOGIC_BACKLIGHT
		bldrv = aml_bl_get_driver(pdrv->index);
		if (!bldrv)
			goto lcd_tcon_data_multi_match_policy_err_type;
		temp = bldrv->level;

		if (temp < data_list->min || temp > data_list->max)
			goto lcd_tcon_data_multi_match_policy_exit;
		if (!data_multi)
			break;
		if (lcd_debug_print_flag & LCD_DBG_PR_ISR) {
			snprintf(data_multi->dbg_str, 64, "bl_level %d-%d hit, %d",
				data_list->min, data_list->max, temp);
		}
#endif
		break;
	case LCD_TCON_DATA_CTRL_MULTI_BL_PWM_DUTY:
#ifdef CONFIG_AMLOGIC_BACKLIGHT
		bldrv = aml_bl_get_driver(pdrv->index);
		if (!bldrv)
			goto lcd_tcon_data_multi_match_policy_err_type;

		switch (bldrv->bconf.method) {
		case BL_CTRL_PWM:
			bl_pwm = bldrv->bconf.bl_pwm;
			break;
		case BL_CTRL_PWM_COMBO:
			if (data_list->ctrl_data) {
				if (data_list->ctrl_data[0])
					bl_pwm = bldrv->bconf.bl_pwm_combo0;
				else
					bl_pwm = bldrv->bconf.bl_pwm_combo1;
			}
			break;
		default:
			break;
		}
		if (!bl_pwm)
			goto lcd_tcon_data_multi_match_policy_err_type;

		temp = bl_pwm->pwm_duty;
		if (temp < data_list->min || temp > data_list->max)
			goto lcd_tcon_data_multi_match_policy_exit;
		if (!data_multi)
			break;
		if (lcd_debug_print_flag & LCD_DBG_PR_ISR) {
			snprintf(data_multi->dbg_str, 64, "bl_pwm[%d] duty %d-%d hit, %d",
				bl_pwm->index, data_list->min, data_list->max, temp);
		}
#endif
		break;
	case LCD_TCON_DATA_CTRL_DEFAULT:
		return 1;
	default:
		return -1;
	}

	return 0;

lcd_tcon_data_multi_match_policy_exit:
	return -1;

#ifdef CONFIG_AMLOGIC_BACKLIGHT
lcd_tcon_data_multi_match_policy_err_type:
	LCDERR("%s: %s type invalid\n", __func__, data_list->block_name);
	return -1;
#endif
}

static int lcd_tcon_data_multi_match_init(struct aml_lcd_drv_s *pdrv,
		struct tcon_data_list_s *data_list,
		struct lcd_tcon_data_part_ctrl_s *ctrl_part, unsigned char *p)
{
	unsigned int data_byte, data_cnt;
	unsigned int i, j, k;

	if (!ctrl_part)
		return -1;
	if (!(ctrl_part->ctrl_data_flag & LCD_TCON_DATA_CTRL_FLAG_MULTI))
		return -1;

	data_byte = ctrl_part->data_byte_width;
	data_cnt = ctrl_part->data_cnt;
	data_list->ctrl_method = LCD_TCON_DATA_CTRL_MULTI_MAX;

	k = 0;
	data_list->min = 0;
	data_list->max = 0;
	data_list->ctrl_data_cnt = 0;
	kfree(data_list->ctrl_data);
	data_list->ctrl_data = NULL;

	switch (ctrl_part->ctrl_method) {
	case LCD_TCON_DATA_CTRL_MULTI_VFREQ_DIRECT:
		if (data_cnt != 2)
			goto lcd_tcon_data_multi_match_init_err_data_cnt;

		data_list->ctrl_method = ctrl_part->ctrl_method;
		for (j = 0; j < data_byte; j++)
			data_list->min |= (p[k + j] << (j * 8));
		k += data_byte;
		for (j = 0; j < data_byte; j++)
			data_list->max |= (p[k + j] << (j * 8));
		break;
	case LCD_TCON_DATA_CTRL_MULTI_VFREQ_NOTIFY:
		if (data_cnt <= 2)
			goto lcd_tcon_data_multi_match_init_err_data_cnt;

		data_list->ctrl_method = ctrl_part->ctrl_method;
		for (j = 0; j < data_byte; j++)
			data_list->min |= (p[k + j] << (j * 8));
		k += data_byte;
		for (j = 0; j < data_byte; j++)
			data_list->max |= (p[k + j] << (j * 8));

		data_list->ctrl_data_cnt = data_cnt - 2;
		if (data_list->ctrl_data_cnt == 0)
			break;
		data_list->ctrl_data =
			kcalloc(data_list->ctrl_data_cnt, sizeof(unsigned int), GFP_KERNEL);
		if (!data_list->ctrl_data)
			goto lcd_tcon_data_multi_match_init_err_malloc;
		for (i = 0; i < data_list->ctrl_data_cnt; i++) {
			k += data_byte;
			for (j = 0; j < data_byte; j++)
				data_list->ctrl_data[i] |= (p[k + j] << (j * 8));
		}
		break;
	case LCD_TCON_DATA_CTRL_MULTI_BL_LEVEL:
		if (data_cnt != 2)
			goto lcd_tcon_data_multi_match_init_err_data_cnt;

		data_list->ctrl_method = ctrl_part->ctrl_method;
		for (j = 0; j < data_byte; j++)
			data_list->min |= (p[k + j] << (j * 8));
		k += data_byte;
		for (j = 0; j < data_byte; j++)
			data_list->max |= (p[k + j] << (j * 8));
		break;
	case LCD_TCON_DATA_CTRL_MULTI_BL_PWM_DUTY:
		if (data_cnt != 3)
			goto lcd_tcon_data_multi_match_init_err_data_cnt;

		data_list->ctrl_method = ctrl_part->ctrl_method;
		//pwm combo index
		data_list->ctrl_data_cnt = 1;
		data_list->ctrl_data = kzalloc(sizeof(unsigned int), GFP_KERNEL);
		if (!data_list->ctrl_data)
			goto lcd_tcon_data_multi_match_init_err_malloc;
		for (j = 0; j < data_byte; j++)
			data_list->ctrl_data[0] |= (p[k + j] << (j * 8));
		k += data_byte;
		for (j = 0; j < data_byte; j++)
			data_list->min |= (p[k + j] << (j * 8));
		k += data_byte;
		for (j = 0; j < data_byte; j++)
			data_list->max |= (p[k + j] << (j * 8));
		break;
	case LCD_TCON_DATA_CTRL_DEFAULT:
		return 1;
	default:
		return -1;
	}

	return 0;

lcd_tcon_data_multi_match_init_err_data_cnt:
	LCDERR("%s: ctrl_part %s data_cnt %d error\n",
		__func__, ctrl_part->name, data_cnt);
	return -1;

lcd_tcon_data_multi_match_init_err_malloc:
	LCDERR("%s: ctrl_part %s malloc error\n", __func__, ctrl_part->name);
	return -1;
}

static int lcd_tcon_data_multi_match_check(struct aml_lcd_drv_s *pdrv,
		unsigned int frame_rate,
		struct lcd_tcon_data_part_ctrl_s *ctrl_part, unsigned char *p)
{
#ifdef CONFIG_AMLOGIC_BACKLIGHT
	struct aml_bl_drv_s *bldrv;
	struct bl_pwm_config_s *bl_pwm = NULL;
	unsigned int temp;
#endif
	unsigned int data_byte, data_cnt, data, min, max;
	unsigned int j, k;

	if (!ctrl_part)
		return -1;
	if (!(ctrl_part->ctrl_data_flag & LCD_TCON_DATA_CTRL_FLAG_MULTI))
		return -1;

	data_byte = ctrl_part->data_byte_width;
	data_cnt = ctrl_part->data_cnt;

	k = 0;
	data = 0;
	min = 0;
	max = 0;

	switch (ctrl_part->ctrl_method) {
	case LCD_TCON_DATA_CTRL_MULTI_VFREQ_DIRECT:
		if (data_cnt != 2)
			goto lcd_tcon_data_multi_match_check_err_data_cnt;

		for (j = 0; j < data_byte; j++)
			min |= (p[k + j] << (j * 8));
		k += data_byte;
		for (j = 0; j < data_byte; j++)
			max |= (p[k + j] << (j * 8));
		if (frame_rate < min || frame_rate > max)
			goto lcd_tcon_data_multi_match_check_exit;
		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
			LCDPR("%s: vfreq %d-%d hit, %d", __func__, min, max, frame_rate);
		break;
	case LCD_TCON_DATA_CTRL_MULTI_BL_LEVEL:
#ifdef CONFIG_AMLOGIC_BACKLIGHT
		bldrv = aml_bl_get_driver(pdrv->index);
		if (!bldrv)
			goto lcd_tcon_data_multi_match_check_err_type;
		temp = bldrv->level;

		if (data_cnt != 2)
			goto lcd_tcon_data_multi_match_check_err_data_cnt;
		for (j = 0; j < data_byte; j++)
			min |= (p[k + j] << (j * 8));
		k += data_byte;
		for (j = 0; j < data_byte; j++)
			max |= (p[k + j] << (j * 8));
		if (temp < min || temp > max)
			goto lcd_tcon_data_multi_match_check_exit;
		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
			LCDPR("%s: bl_level %d-%d hit, %d", __func__, min, max, temp);
#endif
		break;
	case LCD_TCON_DATA_CTRL_MULTI_BL_PWM_DUTY:
#ifdef CONFIG_AMLOGIC_BACKLIGHT
		bldrv = aml_bl_get_driver(pdrv->index);
		if (!bldrv)
			goto lcd_tcon_data_multi_match_check_err_type;

		if (data_cnt != 3)
			goto lcd_tcon_data_multi_match_check_err_data_cnt;
		for (j = 0; j < data_byte; j++)
			data |= (p[k + j] << (j * 8)); //pwm_index
		k += data_byte;
		for (j = 0; j < data_byte; j++)
			min |= (p[k + j] << (j * 8));
		k += data_byte;
		for (j = 0; j < data_byte; j++)
			max |= (p[k + j] << (j * 8));

		switch (bldrv->bconf.method) {
		case BL_CTRL_PWM:
			bl_pwm = bldrv->bconf.bl_pwm;
			break;
		case BL_CTRL_PWM_COMBO:
			if (data == 0)
				bl_pwm = bldrv->bconf.bl_pwm_combo0;
			else
				bl_pwm = bldrv->bconf.bl_pwm_combo1;
			break;
		default:
			break;
		}
		if (!bl_pwm)
			goto lcd_tcon_data_multi_match_check_err_type;

		temp = bl_pwm->pwm_duty;
		if (temp < min || temp > max)
			goto lcd_tcon_data_multi_match_check_exit;

		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
			LCDPR("%s: bl_pwm[%d] duty %d-%d hit, %d",
				__func__, bl_pwm->index, min, max, temp);
		}
#endif
		break;
	case LCD_TCON_DATA_CTRL_DEFAULT:
		return 1;
	default:
		return -1;
	}

	return 0;

lcd_tcon_data_multi_match_check_exit:
	return -1;

lcd_tcon_data_multi_match_check_err_data_cnt:
	LCDERR("%s: ctrl_part %s data_cnt error\n", __func__, ctrl_part->name);
	return -1;

#ifdef CONFIG_AMLOGIC_BACKLIGHT
lcd_tcon_data_multi_match_check_err_type:
	LCDERR("%s: ctrl_part %s type invalid\n", __func__, ctrl_part->name);
	return -1;
#endif
}

static int lcd_tcon_data_multi_list_init(struct aml_lcd_drv_s *pdrv,
		struct tcon_data_list_s *data_list)
{
	struct lcd_tcon_data_block_header_s *block_header;
	struct lcd_tcon_data_block_ext_header_s *ext_header;
	struct lcd_tcon_data_part_ctrl_s *ctrl_part;
	unsigned char *data_buf, *p, *part_start, part_type;
	unsigned int size, data_offset, offset, i;
	unsigned int *part_pos, ext_header_size;
	unsigned short part_cnt;
	int ret;

	data_buf = data_list->block_vaddr;
	block_header = (struct lcd_tcon_data_block_header_s *)data_buf;
	p = data_buf + LCD_TCON_DATA_BLOCK_HEADER_SIZE;
	ext_header = (struct lcd_tcon_data_block_ext_header_s *)p;
	part_cnt = ext_header->part_cnt;
	ext_header_size = block_header->ext_header_size;

	part_pos = (unsigned int *)(p + LCD_TCON_DATA_BLOCK_EXT_HEADER_SIZE_PRE);
	part_start = data_buf + LCD_TCON_DATA_BLOCK_HEADER_SIZE + ext_header_size;
	size = 0;
	for (i = 0; i < part_cnt; i++) {
		p = part_start + part_pos[i];
		data_offset = part_pos[i];
		part_type = p[LCD_TCON_DATA_PART_NAME_SIZE + 3];

		switch (part_type) {
		case LCD_TCON_DATA_PART_TYPE_CONTROL:
			offset = LCD_TCON_DATA_PART_CTRL_SIZE_PRE;
			ctrl_part = (struct lcd_tcon_data_part_ctrl_s *)p;
			size = offset + (ctrl_part->data_cnt * ctrl_part->data_byte_width);
			if (!(ctrl_part->ctrl_data_flag & LCD_TCON_DATA_CTRL_FLAG_MULTI))
				break;
			if ((size + data_offset) > block_header->block_size)
				return -1;
			ret = lcd_tcon_data_multi_match_init(pdrv, data_list,
				ctrl_part, (p + offset));
			if (ret == 0)
				return 0;
			if (ret == 1)
				return 1;
			break;
		default:
			return -1;
		}
	}

	return -1;
}

/* return:
 *    0: matched
 *    1: dft list
 *   -1: not match
 */
int lcd_tcon_data_multi_match_find(struct aml_lcd_drv_s *pdrv, unsigned char *data_buf)
{
	struct lcd_tcon_data_block_header_s *block_header;
	struct lcd_tcon_data_block_ext_header_s *ext_header;
	struct lcd_tcon_data_part_ctrl_s *ctrl_part;
	unsigned char *p, part_type;
	unsigned int frame_rate, size, data_offset, offset, i;
	unsigned short part_cnt;
	int ret;

	block_header = (struct lcd_tcon_data_block_header_s *)data_buf;
	p = data_buf + LCD_TCON_DATA_BLOCK_HEADER_SIZE;
	ext_header = (struct lcd_tcon_data_block_ext_header_s *)p;
	part_cnt = ext_header->part_cnt;
	frame_rate = pdrv->config.timing.act_timing.frame_rate;

	data_offset = LCD_TCON_DATA_BLOCK_HEADER_SIZE + block_header->ext_header_size;
	size = 0;
	for (i = 0; i < part_cnt; i++) {
		p = data_buf + data_offset;
		part_type = p[LCD_TCON_DATA_PART_NAME_SIZE + 3];

		switch (part_type) {
		case LCD_TCON_DATA_PART_TYPE_CONTROL:
			offset = LCD_TCON_DATA_PART_CTRL_SIZE_PRE;
			ctrl_part = (struct lcd_tcon_data_part_ctrl_s *)p;
			size = offset + (ctrl_part->data_cnt *
					 ctrl_part->data_byte_width);
			if (!(ctrl_part->ctrl_data_flag & LCD_TCON_DATA_CTRL_FLAG_MULTI))
				break;
			ret = lcd_tcon_data_multi_match_check(pdrv, frame_rate,
				ctrl_part, (p + offset));
			if (ret == 0)
				return 0;
			if (ret == 1)
				return 1;
			break;
		default:
			return -1;
		}
		data_offset += size;
	}

	return -1;
}

/* for tcon power on init,
 * will apply block_type: LCD_TCON_DATA_BLOCK_TYPE_BASIC_INIT
 */
void lcd_tcon_data_multi_current_update(struct tcon_mem_map_table_s *mm_table,
		struct lcd_tcon_data_block_header_s *block_header, unsigned int index)
{
	struct tcon_data_multi_s *data_multi = NULL;
	struct tcon_data_list_s *temp_list;
	int i;

	if (!mm_table || !mm_table->data_multi)
		return;
	if (mm_table->data_multi_cnt == 0)
		return;

	for (i = 0; i < mm_table->data_multi_cnt; i++) {
		data_multi = &mm_table->data_multi[i];
		if (data_multi->block_type != block_header->block_type)
			continue;
		temp_list = data_multi->list_header;
		while (temp_list) {
			if (temp_list->id == index) {
				data_multi->list_cur = temp_list;
				if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
					LCDPR("%s: multi[%d]: type=0x%x, id=%d, %s\n",
						__func__, i,
						data_multi->block_type,
						data_multi->list_cur->id,
						data_multi->list_cur->block_name);
				}
				return;
			}
			temp_list = temp_list->next;
		}
	}
}

/* for tcon multi lut update bypass debug */
void lcd_tcon_data_multi_bypass_set(struct tcon_mem_map_table_s *mm_table,
				    unsigned int block_type, int flag)
{
	struct tcon_data_multi_s *data_multi = NULL;
	int i;

	if (!mm_table || !mm_table->data_multi)
		return;
	if (mm_table->data_multi_cnt == 0)
		return;

	for (i = 0; i < mm_table->data_multi_cnt; i++) {
		data_multi = &mm_table->data_multi[i];
		if (data_multi->block_type == block_type) {
			data_multi->bypass_flag = flag;
			LCDPR("tcon multi[%d]: block_type=0x%x, bypass: %d\n",
			      i, block_type, flag);
			return;
		}
	}

	LCDERR("tcon multi[%d]: block_type=0x%x invalid\n", i, block_type);
}

/* for tcon vsync switch multi lut dynamically,
 * will bypass block_type: LCD_TCON_DATA_BLOCK_TYPE_BASIC_INIT
 */
static int lcd_tcon_data_multi_update(struct aml_lcd_drv_s *pdrv,
				      struct tcon_mem_map_table_s *mm_table)
{
	struct tcon_data_multi_s *data_multi = NULL;
	struct tcon_data_list_s *temp_list, *match_list;
	unsigned int acc_data[2], n = 0, frame_rate;
#ifdef TCON_DBG_TIME
	unsigned long long local_time[4];
	unsigned int temp;
#endif
	int i, ret = 0;

	if (!mm_table || !mm_table->data_multi)
		return 0;
	if (mm_table->data_multi_cnt == 0)
		return 0;

#ifdef TCON_DBG_TIME
	if (lcd_debug_print_flag & LCD_DBG_PR_TEST) {
		local_time[0] = sched_clock();
		lcd_tcon_dbg_list_trave_time_save(local_time[0], 0);
	}
#endif

	frame_rate = mm_table->frame_rate;
	for (i = 0; i < mm_table->data_multi_cnt; i++) {
		data_multi = &mm_table->data_multi[i];
		/* bypass LCD_TCON_DATA_BLOCK_TYPE_BASIC_INIT for multi lut switch */
		if (is_block_type_basic_init(data_multi->block_type))
			continue;
		/* bypass_flag for debug */
		if (data_multi->bypass_flag)
			continue;

		/* step1: check current list first, for threshold overlap*/
		temp_list = data_multi->list_cur;
		if (temp_list) {
			n++;
			ret = lcd_tcon_data_multi_match_policy(pdrv, frame_rate,
					data_multi, temp_list);
			if (ret == 0) //current range, no need update
				continue;
		}
#ifdef TCON_DBG_TIME
		if (lcd_debug_print_flag & LCD_DBG_PR_TEST) {
			local_time[1] = sched_clock();
			lcd_tcon_dbg_list_trave_time_save((local_time[1] - local_time[0]), 1);
		}
#endif

		/* step2: traversing list*/
		temp_list = data_multi->list_header;
		match_list = NULL;
		while (temp_list) {
			n++;
			ret = lcd_tcon_data_multi_match_policy(pdrv, frame_rate,
					data_multi, temp_list);
			if (ret == 0) {
				match_list = temp_list;
				break;
			}
			temp_list = temp_list->next;
		}
#ifdef TCON_DBG_TIME
		if (lcd_debug_print_flag & LCD_DBG_PR_TEST) {
			local_time[2] = sched_clock();
			lcd_tcon_dbg_list_trave_time_save((local_time[2] - local_time[0]), 2);
		}
#endif

		if (!match_list) //no active list, no need update setting
			continue;

		if (data_multi->list_cur &&
		    data_multi->list_cur->id == match_list->id) {
			//same list, no need update setting
			continue;
		}

		if (data_multi->block_type == LCD_TCON_DATA_BLOCK_TYPE_ACC_LUT &&
		    match_list->ctrl_method == LCD_TCON_DATA_CTRL_MULTI_VFREQ_NOTIFY) {
			acc_data[0] = pdrv->index;
			if (match_list->ctrl_data)
				acc_data[1] = match_list->ctrl_data[0];
			else
				acc_data[1] = 0xff; //default gamma lut
			aml_lcd_atomic_notifier_call_chain(LCD_EVENT_GAMMA_UPDATE,
					(void *)acc_data);
			data_multi->list_cur = match_list;
			data_multi->list_update = NULL;
		} else {
			data_multi->list_update = match_list;
		}
		if (lcd_debug_print_flag & LCD_DBG_PR_ISR) {
			LCDPR("%s: multi[%d]: type=0x%x, %s: id=%d, %s\n",
			      __func__, i,
			      data_multi->block_type,
			      data_multi->dbg_str,
			      match_list->id,
			      match_list->block_name);
		}
	}

#ifdef TCON_DBG_TIME
	if (lcd_debug_print_flag & LCD_DBG_PR_TEST) {
		local_time[3] = sched_clock();
		temp = (0xf << 12) | (n << 4) | 3;
		lcd_tcon_dbg_list_trave_time_save((local_time[3] - local_time[0]), temp);
		lcd_tcon_time_sort_save(mm_table->list_trave_time,
			(local_time[3] - local_time[0]));
	}
#endif

	return ret;
}

static int lcd_tcon_data_multi_set(struct aml_lcd_drv_s *pdrv,
				   struct tcon_mem_map_table_s *mm_table)
{
	struct tcon_data_multi_s *data_multi = NULL;
#ifdef TCON_DBG_TIME
	unsigned long long local_time[4];
	int line_cnt;
#endif
	int i, lut_hit = 0, temp, ret = 0;

	if (!mm_table || !mm_table->data_multi)
		return 0;
	if (mm_table->data_multi_cnt == 0)
		return 0;

#ifdef TCON_DBG_TIME
	if (lcd_debug_print_flag & LCD_DBG_PR_TEST) {
		local_time[0] = sched_clock();
		line_cnt = lcd_get_encl_line_cnt(pdrv);
		lcd_tcon_dbg_vsync_time_save(local_time[0], line_cnt, 0);
	}
#endif

	temp = vout_frame_rate_measure(1); //1000 multi
	if (temp == 0)
		temp = pdrv->config.timing.act_timing.frame_rate;
	else
		temp /= 1000;
	mm_table->frame_rate = temp;

#ifdef TCON_DBG_TIME
	if (lcd_debug_print_flag & LCD_DBG_PR_TEST) {
		local_time[1] = sched_clock();
		line_cnt = lcd_get_encl_line_cnt(pdrv);
		lcd_tcon_dbg_vsync_time_save(local_time[1] - local_time[0], line_cnt, 1);
	}
#endif

	lcd_tcon_data_multi_update(pdrv, &tcon_mm_table);
#ifdef TCON_DBG_TIME
	if (lcd_debug_print_flag & LCD_DBG_PR_TEST) {
		local_time[2] = sched_clock();
		line_cnt = lcd_get_encl_line_cnt(pdrv);
		lcd_tcon_dbg_vsync_time_save(local_time[2] - local_time[0], line_cnt, 2);
	}
#endif

	for (i = 0; i < mm_table->data_multi_cnt; i++) {
		data_multi = &mm_table->data_multi[i];
		/* bypass LCD_TCON_DATA_BLOCK_TYPE_BASIC_INIT for multi lut switch */
		/* bypass LCD_TCON_DATA_BLOCK_TYPE_ACC_LUT for soc gamma update */
		if (is_block_type_basic_init(data_multi->block_type))
			continue;
		/* bypass_flag for debug */
		if (data_multi->bypass_flag)
			continue;
		if (!data_multi->list_update)
			continue;

		lut_hit++;
		ret = lcd_tcon_data_common_parse_set(pdrv,
				data_multi->list_update->block_vaddr,
				data_multi->list_update->block_paddr, 0);

		if (ret) //list update failed
			continue;
		data_multi->list_cur = data_multi->list_update;
		data_multi->list_update = NULL;
	}
#ifdef TCON_DBG_TIME
	if (lcd_debug_print_flag & LCD_DBG_PR_TEST) {
		local_time[3] = sched_clock();
		line_cnt = lcd_get_encl_line_cnt(pdrv);
		temp = (0xf << 12) | (lut_hit << 4) | 3;
		lcd_tcon_dbg_vsync_time_save(local_time[3] - local_time[0], line_cnt, temp);
		lcd_tcon_time_sort_save(mm_table->vsync_time, (local_time[3] - local_time[0]));
	}
#endif

	return ret;
}

void lcd_tcon_vsync_isr(struct aml_lcd_drv_s *pdrv)
{
	struct lcd_tcon_fw_s *tcon_fw = aml_lcd_tcon_get_fw();
	struct tcon_pdf_s *tcon_pdf = lcd_tcon_get_pdf();
	unsigned long flags = 0;

	if ((pdrv->status & LCD_STATUS_IF_ON) == 0)
		return;
	if (pdrv->tcon_isr_bypass)
		return;

	if (lcd_tcon_conf->tcon_is_busy)
		return;

	if (lcd_tcon_conf->lut_dma_update)
		lcd_tcon_conf->lut_dma_update(pdrv);

	if (tcon_mm_table.version > 0 && tcon_mm_table.version < 0xff) {
		if (tcon_mm_table.multi_lut_update) {
			spin_lock_irqsave(&tcon_local_cfg.multi_list_lock, flags);
			lcd_tcon_data_multi_set(pdrv, &tcon_mm_table);
			spin_unlock_irqrestore(&tcon_local_cfg.multi_list_lock, flags);
		}
	}

	if (tcon_pdf->vs_handler)
		tcon_pdf->vs_handler(tcon_pdf);
	else if (pdrv->config.customer_sw_pdf)
		lcd_swpdf_vs_handle();

	if (tcon_fw->vsync_isr)
		tcon_fw->vsync_isr(tcon_fw);
}

/* **********************************
 * tcon config
 * **********************************
 */
static int lcd_tcon_vac_load(void)
{
	unsigned int n;
	unsigned char *buff = tcon_rmem.vac_rmem.mem_vaddr;
	unsigned int data_cnt;
	unsigned char data_checksum, data_lrc, temp_checksum, temp_lrc;

	if (tcon_rmem.vac_rmem.mem_size == 0 || !buff)
		return -1;

	data_cnt = (buff[0] |
		    (buff[1] << 8) |
		    (buff[2] << 16) |
		    (buff[3] << 24));
	if (data_cnt == 0) {
		LCDPR("%s: vac data_cnt is zero\n", __func__);
		return -1;
	}
	data_checksum = buff[4];
	data_lrc = buff[5];
	temp_checksum = lcd_tcon_checksum(&buff[8], data_cnt);
	temp_lrc = lcd_tcon_lrc(&buff[8], data_cnt);
	if (data_checksum != temp_checksum) {
		LCDERR("%s: vac_data checksum error\n", __func__);
		return -1;
	}
	if (data_lrc != temp_lrc) {
		LCDERR("%s: vac_data lrc error\n", __func__);
		return -1;
	}
	if (buff[6] != 0x55 || buff[7] != 0xaa) {
		LCDERR("%s: vac_data pattern error\n", __func__);
		return -1;
	}

	if (lcd_debug_print_flag & LCD_DBG_PR_ADV) {
		for (n = 0; n < 30; n++)
			LCDPR("%s: vac_data[%d]: 0x%x", __func__,
			      n, buff[n * 1]);
	}

	return 0;
}

static int lcd_tcon_demura_set_load(void)
{
	unsigned int n;
	char *buff = tcon_rmem.demura_set_rmem.mem_vaddr;
	unsigned int data_cnt;
	unsigned char data_checksum, data_lrc, temp_checksum, temp_lrc;

	if (tcon_rmem.demura_set_rmem.mem_size == 0 || !buff)
		return -1;

	data_cnt = (buff[0] |
		    (buff[1] << 8) |
		    (buff[2] << 16) |
		    (buff[3] << 24));
	if (data_cnt == 0) {
		LCDPR("%s: demura_set data_cnt is zero\n", __func__);
		return -1;
	}
	data_checksum = buff[4];
	data_lrc = buff[5];
	temp_checksum = lcd_tcon_checksum(&buff[8], data_cnt);
	temp_lrc = lcd_tcon_lrc(&buff[8], data_cnt);
	if (data_checksum != temp_checksum) {
		LCDERR("%s: demura_set checksum error\n", __func__);
		return -1;
	}
	if (data_lrc != temp_lrc) {
		LCDERR("%s: demura_set lrc error\n", __func__);
		return -1;
	}
	if (buff[6] != 0x55 || buff[7] != 0xaa) {
		LCDERR("%s: demura_set pattern error\n", __func__);
		return -1;
	}

	if (lcd_debug_print_flag & LCD_DBG_PR_ADV) {
		for (n = 0; n < 30; n++)
			LCDPR("%s: demura_set[%d]: 0x%x",
			      __func__, n, buff[n]);
	}

	return 0;
}

static int lcd_tcon_demura_lut_load(void)
{
	unsigned int n;
	char *buff = tcon_rmem.demura_lut_rmem.mem_vaddr;
	unsigned int data_cnt;
	unsigned char data_checksum, data_lrc, temp_checksum, temp_lrc;

	if (tcon_rmem.demura_lut_rmem.mem_size == 0 || !buff)
		return -1;

	data_cnt = (buff[0] |
		    (buff[1] << 8) |
		    (buff[2] << 16) |
		    (buff[3] << 24));
	if (data_cnt == 0) {
		LCDPR("%s: demura_lut data_cnt is zero\n", __func__);
		return -1;
	}
	data_checksum = buff[4];
	data_lrc = buff[5];
	temp_checksum = lcd_tcon_checksum(&buff[8], data_cnt);
	temp_lrc = lcd_tcon_lrc(&buff[8], data_cnt);
	if (data_checksum != temp_checksum) {
		LCDERR("%s: demura_lut checksum error\n", __func__);
		return -1;
	}
	if (data_lrc != temp_lrc) {
		LCDERR("%s: demura_lut lrc error\n", __func__);
		return -1;
	}
	if (buff[6] != 0x55 || buff[7] != 0xaa) {
		LCDERR("%s: demura_lut pattern error\n", __func__);
		return -1;
	}

	if (lcd_debug_print_flag & LCD_DBG_PR_ADV) {
		for (n = 0; n < 30; n++)
			LCDPR("%s: demura_lut[%d]: 0x%x\n",
			      __func__, n, buff[n]);
	}

	return 0;
}

static int lcd_tcon_acc_lut_load(void)
{
	unsigned int n;
	char *buff = tcon_rmem.acc_lut_rmem.mem_vaddr;
	unsigned int data_cnt;
	unsigned char data_checksum, data_lrc, temp_checksum, temp_lrc;

	if (tcon_rmem.acc_lut_rmem.mem_size == 0 || !buff)
		return -1;

	data_cnt = (buff[0] |
		    (buff[1] << 8) |
		    (buff[2] << 16) |
		    (buff[3] << 24));
	if (data_cnt == 0) {
		LCDPR("%s: acc_lut data_cnt is zero\n", __func__);
		return -1;
	}
	data_checksum = buff[4];
	data_lrc = buff[5];
	temp_checksum = lcd_tcon_checksum(&buff[8], data_cnt);
	temp_lrc = lcd_tcon_lrc(&buff[8], data_cnt);
	if (data_checksum != temp_checksum) {
		LCDERR("%s: acc_lut checksum error\n", __func__);
		return -1;
	}
	if (data_lrc != temp_lrc) {
		LCDERR("%s: acc_lut lrc error\n", __func__);
		return -1;
	}
	if (buff[6] != 0x55 || buff[7] != 0xaa) {
		LCDERR("%s: acc_lut pattern error\n", __func__);
		return -1;
	}

	if (lcd_debug_print_flag & LCD_DBG_PR_ADV) {
		for (n = 0; n < 30; n++)
			LCDPR("%s: acc_lut[%d]: 0x%x\n",
			      __func__, n, buff[n]);
	}

	return 0;
}

static inline void lcd_tcon_data_list_add(struct tcon_data_multi_s *data_multi,
		struct tcon_data_list_s *data_list)
{
	struct tcon_data_list_s *temp_list;

	if (!data_multi || !data_list)
		return;

	//multi list add
	if (!data_multi->list_header) { /* new list */
		data_multi->list_header = data_list;
	} else {
		temp_list = data_multi->list_header;
		if (temp_list->id == data_list->id) {
			data_multi->list_header = data_list;
			data_multi->list_remove = temp_list;
			goto lcd_tcon_data_list_add_end;
		}
		while (temp_list->next) {
			if (temp_list->next->id == data_list->id) {
				temp_list->next = data_list;
				data_multi->list_remove = temp_list->next;
				goto lcd_tcon_data_list_add_end;
			}
			temp_list = temp_list->next;
		}
		temp_list->next = data_list;
	}
	data_multi->list_cnt++;

lcd_tcon_data_list_add_end:
	if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
		LCDPR("%s: block[%d]: %s\n",
		      __func__, data_list->id, data_list->block_name);
	}
}

static inline void lcd_tcon_data_list_remove(struct tcon_data_multi_s *data_multi)
{
	struct tcon_data_list_s *cur_list, *next_list;

	if (!data_multi)
		return;

	/* add to exist list */
	cur_list = data_multi->list_header;
	while (cur_list) {
		next_list = cur_list->next;
		kfree(cur_list->ctrl_data);
		kfree(cur_list);
		cur_list = next_list;
	}

	data_multi->block_type = LCD_TCON_DATA_BLOCK_TYPE_MAX;
	data_multi->list_cnt = 0;
	data_multi->list_header = NULL;
	data_multi->list_cur = NULL;
	data_multi->list_update = NULL;
}

static int lcd_tcon_data_multi_add(struct aml_lcd_drv_s *pdrv,
				   struct tcon_mem_map_table_s *mm_table,
				   struct lcd_tcon_data_block_header_s *block_header,
				   unsigned int index)
{
	struct tcon_data_multi_s *data_multi = NULL;
	struct tcon_data_list_s *data_list;
	unsigned int frame_rate;
	unsigned long flags = 0;
	int i, ret;

	if (!mm_table->data_multi) {
		LCDERR("%s: data_multi is null\n", __func__);
		return -1;
	}
	if (mm_table->data_multi_cnt > 0) {
		if ((mm_table->data_multi_cnt + 1) >= mm_table->block_cnt) {
			LCDERR("%s: multi block %s invalid\n",
			       __func__, block_header->name);
			return -1;
		}
	}

	if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
		LCDPR("%s: block[%d]: %s\n", __func__, index, block_header->name);

	/* create list */
	data_list = kzalloc(sizeof(*data_list), GFP_KERNEL);
	if (!data_list)
		return -1;
	spin_lock_irqsave(&tcon_local_cfg.multi_list_lock, flags);
	data_list->block_vaddr = mm_table->data_mem_vaddr[index];
	data_list->block_paddr = mm_table->data_mem_paddr[index];
	data_list->next = NULL;
	data_list->id = index;
	data_list->block_name = block_header->name;

	for (i = 0; i < mm_table->data_multi_cnt; i++) {
		if (mm_table->data_multi[i].block_type == block_header->block_type) {
			data_multi = &mm_table->data_multi[i];
			break;
		}
	}

	if (!data_multi) { /* create new list */
		data_multi = &mm_table->data_multi[mm_table->data_multi_cnt];
		mm_table->data_multi_cnt++;
		data_multi->block_type = block_header->block_type;
		data_multi->list_header = NULL;
		data_multi->list_cur = NULL;
		data_multi->list_update = NULL;
		data_multi->list_remove = NULL;
		data_multi->list_cnt = 0;
		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
			LCDPR("%s: new multi[%d]: type=0x%x, data_multi_cnt=%d\n",
			      __func__, i, data_multi->block_type,
			      mm_table->data_multi_cnt);
		}
	}
	lcd_tcon_data_list_add(data_multi, data_list);
	lcd_tcon_data_multi_list_init(pdrv, data_list);

	frame_rate = pdrv->config.timing.act_timing.frame_rate;
	ret = lcd_tcon_data_multi_match_policy(pdrv, frame_rate, data_multi, data_list);
	if (ret == 0) {
		data_multi->list_cur = data_list;
		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
			LCDPR("%s: multi[%d]: type=0x%x, %s: id=%d, %s\n",
				__func__, i,
				data_multi->block_type,
				data_multi->dbg_str,
				data_multi->list_cur->id,
				data_multi->list_cur->block_name);
		}
	}

	spin_unlock_irqrestore(&tcon_local_cfg.multi_list_lock, flags);

	kfree(data_multi->list_remove);
	data_multi->list_remove = NULL;

	return 0;
}

static int lcd_tcon_data_multi_remvoe(struct tcon_mem_map_table_s *mm_table)
{
	unsigned long flags = 0;
	int i;

	if (!mm_table->data_multi)
		return 0;
	if (mm_table->data_multi_cnt == 0)
		return 0;

	spin_lock_irqsave(&tcon_local_cfg.multi_list_lock, flags);
	for (i = 0; i < mm_table->data_multi_cnt; i++)
		lcd_tcon_data_list_remove(&mm_table->data_multi[i]);
	spin_unlock_irqrestore(&tcon_local_cfg.multi_list_lock, flags);

	return 0;
}

static int lcd_tcon_data_multi_reset(struct tcon_mem_map_table_s *mm_table)
{
	unsigned long flags = 0;
	int i;

	if (!mm_table->data_multi)
		return 0;
	if (mm_table->data_multi_cnt == 0)
		return 0;

	spin_lock_irqsave(&tcon_local_cfg.multi_list_lock, flags);
	for (i = 0; i < mm_table->data_multi_cnt; i++)
		mm_table->data_multi[i].list_cur = NULL;
	spin_unlock_irqrestore(&tcon_local_cfg.multi_list_lock, flags);

	return 0;
}

static void lcd_tcon_data_complete_check(struct aml_lcd_drv_s *pdrv, int index)
{
	unsigned char *table = tcon_mm_table.core_reg_table;
	int i, n = 0;

	if (tcon_mm_table.data_complete)
		return;

	if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
		LCDPR("%s: index %d\n", __func__, index);

	tcon_mm_table.block_bit_flag |= (1 << index);
	for (i = 0; i < tcon_mm_table.block_cnt; i++) {
		if (tcon_mm_table.block_bit_flag & (1 << i))
			n++;
	}
	if (n < tcon_mm_table.block_cnt)
		return;

	tcon_mm_table.data_complete = 1;
	LCDPR("%s: data_complete: %d\n", __func__, tcon_mm_table.data_complete);

	/* specially check demura setting */
	if (pdrv->data->chip_type == LCD_CHIP_TL1 ||
		pdrv->data->chip_type == LCD_CHIP_TM2) {
		if (tcon_mm_table.demura_cnt < 2) {
			tcon_mm_table.lut_valid_flag &= ~LCD_TCON_DATA_VALID_DEMURA;
			if (table) {
				/* disable demura */
				table[0x178] = 0x38;
				table[0x17c] = 0x20;
				table[0x181] = 0x00;
				table[0x23d] &= ~(1 << 0);
			}
		}
	}
}

void lcd_tcon_data_block_regen_crc(unsigned char *data)
{
	unsigned int raw_crc32 = 0, new_crc32 = 0;
	struct lcd_tcon_data_block_header_s *header;

	if (!data)
		return;
	header = (struct lcd_tcon_data_block_header_s *)data;

	raw_crc32 = (data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
	new_crc32 = cal_crc32(0, data + 4, header->block_size - 4);
	if (raw_crc32 != new_crc32) {
		data[0] = (unsigned char)(new_crc32 & 0xff);
		data[1] = (unsigned char)((new_crc32 >> 8) & 0xff);
		data[2] = (unsigned char)((new_crc32 >> 16) & 0xff);
		data[3] = (unsigned char)((new_crc32 >> 24) & 0xff);
	}
}

void lcd_tcon_pdf_clean_data(struct list_head *head)
{
	struct lcd_tcon_pdf_data_s *pdf_item = NULL;
	struct lcd_tcon_pdf_data_s *next = NULL;

	list_for_each_entry_safe(pdf_item, next, head, list) {
		list_del(&pdf_item->list);
		kfree(pdf_item);
	}
}

int lcd_tcon_data_load(struct aml_lcd_drv_s *pdrv, unsigned char *data_buf, int index)
{
	struct lcd_tcon_data_block_header_s *block_header;
	struct lcd_tcon_init_block_header_s *init_header;
	struct lcd_tcon_config_s *tcon_conf = get_lcd_tcon_config();
	struct lcd_detail_timing_s match_timing;
	unsigned char *core_reg_table;

	if (!tcon_mm_table.data_size) {
		LCDERR("%s: data_size buf error\n", __func__);
		return -1;
	}

	block_header = (struct lcd_tcon_data_block_header_s *)data_buf;
	if (block_header->block_size < sizeof(struct lcd_tcon_data_block_header_s)) {
		LCDERR("%s: block[%d] size 0x%x error\n",
			__func__, index, block_header->block_size);
		return -1;
	}

	if (is_block_type_basic_init(block_header->block_type)) {
		if (!tcon_conf)
			return -1;

		init_header = (struct lcd_tcon_init_block_header_s *)data_buf;
		core_reg_table = data_buf + sizeof(struct lcd_tcon_init_block_header_s);

		if (tcon_conf->tcon_init_table_pre_proc)
			tcon_conf->tcon_init_table_pre_proc(core_reg_table);
		lcd_tcon_data_block_regen_crc(data_buf);

		match_timing.h_active = init_header->h_active;
		match_timing.v_active = init_header->v_active;
		lcd_tcon_init_setting_check(pdrv, &match_timing, core_reg_table);
	} else {
		tcon_mm_table.lut_valid_flag |= block_header->block_flag;
		if (block_header->block_flag == LCD_TCON_DATA_VALID_DEMURA)
			tcon_mm_table.demura_cnt++;
	}

	tcon_mm_table.data_size[index] = block_header->block_size;

	/* add data multi list */
	if (!is_block_type_basic_init(block_header->block_type) &&
		is_block_ctrl_multi(block_header->block_ctrl))
		lcd_tcon_data_multi_add(pdrv, &tcon_mm_table, block_header, index);

	if (block_header->block_type == LCD_TCON_DATA_BLOCK_TYPE_PDF) {
		if (!list_empty(&tcon_local_cfg.pdf_data_list)) {
			/* clean exist list */
			lcd_tcon_pdf_clean_data(&tcon_local_cfg.pdf_data_list);
			tcon_local_cfg.pdf_list_load_flag = 0;
			INIT_LIST_HEAD(&tcon_local_cfg.pdf_data_list);
		}
		if (!lcd_tcon_data_common_parse_set(pdrv,
				data_buf, (phys_addr_t)NULL, 1)) {
			if (!lcd_tcon_pdf_get_config(&tcon_local_cfg.pdf_data_list))
				tcon_local_cfg.pdf_list_load_flag = 1;
		}
	}

	if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
		LCDPR("%s block[%d]: size=0x%x, type=0x%02x, name=%s\n",
			__func__, index,
			block_header->block_size,
			block_header->block_type,
			block_header->name);
	}

	lcd_tcon_data_complete_check(pdrv, index);

	return 0;
}

static int lcd_tcon_reserved_mem_data_load(struct aml_lcd_drv_s *pdrv)
{
	unsigned char *table = tcon_mm_table.core_reg_table;
	int ret;

	if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
		LCDPR("%s: mm_table version: %d\n", __func__, tcon_mm_table.version);

	//only for old chip method, tcon_mm_table.version == 0
	if (pdrv->data->chip_type != LCD_CHIP_TL1 &&
	    pdrv->data->chip_type != LCD_CHIP_TM2)
		return 0;
	if (tcon_mm_table.version)
		return 0;

	if (!table) {
		LCDERR("%s: no tcon bin table\n", __func__);
		return -1;
	}

	ret = lcd_tcon_vac_load();
	if (ret == 0)
		tcon_mm_table.lut_valid_flag |= LCD_TCON_DATA_VALID_VAC;
	ret = lcd_tcon_demura_set_load();
	if (ret) {
		table[0x178] = 0x38;
		table[0x17c] = 0x20;
		table[0x181] = 0x00;
		table[0x23d] &= ~(1 << 0);
	} else {
		ret = lcd_tcon_demura_lut_load();
		if (ret)  {
			table[0x178] = 0x38;
			table[0x17c] = 0x20;
			table[0x181] = 0x00;
			table[0x23d] &= ~(1 << 0);
		} else {
			tcon_mm_table.lut_valid_flag |= LCD_TCON_DATA_VALID_DEMURA;
		}
	}

	ret = lcd_tcon_acc_lut_load();
	if (ret == 0)
		tcon_mm_table.lut_valid_flag |= LCD_TCON_DATA_VALID_ACC;

	tcon_mm_table.data_complete = 1;

	return 0;
}

static int lcd_tcon_bin_path_update(unsigned int size)
{
	unsigned char *mem_vaddr;
	unsigned int data_size, block_cnt;
	unsigned int data_crc32, temp_crc32;

	if (!tcon_rmem.bin_path_rmem.mem_vaddr) {
		LCDERR("%s: get mem error\n", __func__);
		return -1;
	}
	mem_vaddr = tcon_rmem.bin_path_rmem.mem_vaddr;
	data_size = mem_vaddr[4] |
		(mem_vaddr[5] << 8) |
		(mem_vaddr[6] << 16) |
		(mem_vaddr[7] << 24);
	if (data_size < 32) { /* header size */
		LCDERR("%s: tcon_bin_path data_size error\n", __func__);
		return -1;
	}
	block_cnt = mem_vaddr[16] |
		(mem_vaddr[17] << 8) |
		(mem_vaddr[18] << 16) |
		(mem_vaddr[19] << 24);
	if (block_cnt > 32) {
		LCDERR("%s: tcon_bin_path block_cnt error\n", __func__);
		return -1;
	}
	data_crc32 = mem_vaddr[0] |
		(mem_vaddr[1] << 8) |
		(mem_vaddr[2] << 16) |
		(mem_vaddr[3] << 24);
	temp_crc32 = cal_crc32(0, &mem_vaddr[4], (data_size - 4));
	if (data_crc32 != temp_crc32) {
		LCDERR("%s: tcon_bin_path data crc error\n", __func__);
		return -1;
	}

	tcon_mm_table.version = mem_vaddr[8] |
		(mem_vaddr[9] << 8) |
		(mem_vaddr[10] << 16) |
		(mem_vaddr[11] << 24);
	tcon_mm_table.data_load_level = mem_vaddr[12] |
		(mem_vaddr[13] << 8) |
		(mem_vaddr[14] << 16) |
		(mem_vaddr[15] << 24);
	tcon_mm_table.block_cnt = block_cnt;
	//tcon_mm_table.init_load = mem_vaddr[20];
	//LCDPR("%s: init_load: %d\n", __func__, tcon_mm_table.init_load);

	tcon_mm_table.bin_path_valid = 1;

	return 0;
}

static int lcd_tcon_mm_table_config_v0(void)
{
	unsigned int max_size;

	max_size = lcd_tcon_conf->axi_mem_size +
		lcd_tcon_conf->bin_path_size +
		lcd_tcon_conf->vac_size +
		lcd_tcon_conf->demura_set_size +
		lcd_tcon_conf->demura_lut_size +
		lcd_tcon_conf->acc_lut_size;
	if (tcon_rmem.rsv_mem_size < max_size) {
		LCDERR("%s: tcon mem size 0x%x is not enough, need 0x%x\n",
			__func__, tcon_rmem.rsv_mem_size, max_size);
		return -1;
	}

	if (tcon_mm_table.block_cnt != 4) {
		LCDERR("%s: tcon data block_cnt %d invalid\n",
		       __func__, tcon_mm_table.block_cnt);
		return -1;
	}

	tcon_rmem.vac_rmem.mem_size = lcd_tcon_conf->vac_size;
	tcon_rmem.vac_rmem.mem_paddr = tcon_rmem.bin_path_rmem.mem_paddr +
			tcon_rmem.bin_path_rmem.mem_size;
	tcon_rmem.vac_rmem.mem_vaddr =
		lcd_tcon_paddrtovaddr(tcon_rmem.vac_rmem.mem_paddr,
				      tcon_rmem.vac_rmem.mem_size);
	if ((lcd_debug_print_flag & LCD_DBG_PR_NORMAL) &&
	    tcon_rmem.vac_rmem.mem_size > 0)
		LCDPR("vac_mem paddr: 0x%08x, vaddr: 0x%p, size: 0x%x\n",
		      (unsigned int)tcon_rmem.vac_rmem.mem_paddr,
		      tcon_rmem.vac_rmem.mem_vaddr,
		      tcon_rmem.vac_rmem.mem_size);

	tcon_rmem.demura_set_rmem.mem_size = lcd_tcon_conf->demura_set_size;
	tcon_rmem.demura_set_rmem.mem_paddr = tcon_rmem.vac_rmem.mem_paddr +
			tcon_rmem.vac_rmem.mem_size;
	tcon_rmem.demura_set_rmem.mem_vaddr =
		lcd_tcon_paddrtovaddr(tcon_rmem.demura_set_rmem.mem_paddr,
				      tcon_rmem.demura_set_rmem.mem_size);
	if ((lcd_debug_print_flag & LCD_DBG_PR_NORMAL) &&
	    tcon_rmem.demura_set_rmem.mem_size > 0)
		LCDPR("demura_set_mem paddr: 0x%08x, vaddr: 0x%p, size: 0x%x\n",
		      (unsigned int)tcon_rmem.demura_set_rmem.mem_paddr,
		      tcon_rmem.demura_set_rmem.mem_vaddr,
		      tcon_rmem.demura_set_rmem.mem_size);

	tcon_rmem.demura_lut_rmem.mem_size = lcd_tcon_conf->demura_lut_size;
	tcon_rmem.demura_lut_rmem.mem_paddr =
			tcon_rmem.demura_set_rmem.mem_paddr +
			tcon_rmem.demura_set_rmem.mem_size;
	tcon_rmem.demura_lut_rmem.mem_vaddr =
		lcd_tcon_paddrtovaddr(tcon_rmem.demura_lut_rmem.mem_paddr,
				      tcon_rmem.demura_lut_rmem.mem_size);
	if ((lcd_debug_print_flag & LCD_DBG_PR_NORMAL) &&
	    tcon_rmem.demura_lut_rmem.mem_size > 0)
		LCDPR("demura_lut_mem paddr: 0x%08x, vaddr: 0x%p, size: 0x%x\n",
		      (unsigned int)tcon_rmem.demura_lut_rmem.mem_paddr,
		      tcon_rmem.demura_lut_rmem.mem_vaddr,
		      tcon_rmem.demura_lut_rmem.mem_size);

	tcon_rmem.acc_lut_rmem.mem_size = lcd_tcon_conf->acc_lut_size;
	tcon_rmem.acc_lut_rmem.mem_paddr = tcon_rmem.demura_lut_rmem.mem_paddr +
			tcon_rmem.demura_lut_rmem.mem_size;
	tcon_rmem.acc_lut_rmem.mem_vaddr =
		lcd_tcon_paddrtovaddr(tcon_rmem.acc_lut_rmem.mem_paddr,
				      tcon_rmem.acc_lut_rmem.mem_size);
	if ((lcd_debug_print_flag & LCD_DBG_PR_NORMAL) &&
	    tcon_rmem.acc_lut_rmem.mem_size > 0)
		LCDPR("acc_lut_mem paddr: 0x%08x, vaddr: 0x%p, size: 0x%x\n",
		      (unsigned int)tcon_rmem.acc_lut_rmem.mem_paddr,
		      tcon_rmem.acc_lut_rmem.mem_vaddr,
		      tcon_rmem.acc_lut_rmem.mem_size);

	return 0;
}

static int lcd_tcon_mm_table_config_v1(void)
{
	unsigned char *mem_vaddr;
	unsigned int cnt, data_size, n, i;

	if (tcon_mm_table.block_cnt > 32) {
		LCDERR("%s: tcon data block_cnt %d invalid\n",
		       __func__, tcon_mm_table.block_cnt);
		return -1;
	}

	if (tcon_mm_table.data_mem_vaddr)
		return 0;
	if (tcon_mm_table.block_cnt == 0) {
		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
			LCDPR("%s: block_cnt is zero\n", __func__);
		return 0;
	}

	cnt = tcon_mm_table.block_cnt;
	tcon_mm_table.data_mem_vaddr = kcalloc(cnt, sizeof(unsigned char *), GFP_KERNEL);
	tcon_mm_table.data_mem_paddr = kcalloc(cnt, sizeof(dma_addr_t), GFP_KERNEL);
	if (!tcon_mm_table.data_mem_vaddr || !tcon_mm_table.data_mem_paddr)
		return -1;

	tcon_mm_table.data_size = kcalloc(cnt, sizeof(unsigned int), GFP_KERNEL);
	if (!tcon_mm_table.data_size)
		return -1;

	tcon_mm_table.data_multi = kcalloc(cnt, sizeof(struct tcon_data_multi_s), GFP_KERNEL);
	if (!tcon_mm_table.data_multi)
		return -1;

	mem_vaddr = tcon_rmem.bin_path_rmem.mem_vaddr;
	for (i = 0; i < tcon_mm_table.block_cnt; i++) {
		n = 32 + (i * 256);
		data_size = mem_vaddr[n] |
			(mem_vaddr[n + 1] << 8) |
			(mem_vaddr[n + 2] << 16) |
			(mem_vaddr[n + 3] << 24);
		if (data_size == 0) {
			LCDERR("%s: block[%d] size is zero\n", __func__, i);
			continue;
		}
		tcon_mm_table.data_size[i] = data_size;
	}

	return 0;
}

static void lcd_tcon_axi_tbl_set_valid(unsigned int type, int valid)
{
	struct lcd_tcon_axi_mem_cfg_s *axi_cfg = NULL;
	int i = 0;

	if (lcd_tcon_conf->axi_tbl_len) {
		for (i = 0; i < lcd_tcon_conf->axi_tbl_len; i++) {
			axi_cfg = &lcd_tcon_conf->axi_mem_cfg_tbl[i];
			if (type == axi_cfg->mem_type) {
				if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
					LCDPR("%s [%d] type=%d, valid=%d\n", __func__,
						i, type, axi_cfg->mem_valid);
				}
				axi_cfg->mem_valid = valid;
			}
		}
	}
}

static int lcd_tcon_axi_tbl_check_valid(unsigned int type)
{
	struct lcd_tcon_axi_mem_cfg_s *axi_cfg = NULL;
	int i = 0;

	if (lcd_tcon_conf->axi_tbl_len && lcd_tcon_conf->axi_mem_cfg_tbl) {
		for (i = 0; i < lcd_tcon_conf->axi_tbl_len; i++) {
			axi_cfg = &lcd_tcon_conf->axi_mem_cfg_tbl[i];
			if (type == axi_cfg->mem_type)
				return axi_cfg->mem_valid;
		}
	} else {
		if (type == TCON_AXI_MEM_TYPE_OD)
			return (tcon_rmem.axi_bank > 0);
		else if (type == TCON_AXI_MEM_TYPE_DEMURA)
			return (tcon_rmem.axi_bank > 1);
	}

	return 0;
}

int lcd_tcon_mem_od_is_valid(void)
{
	return lcd_tcon_axi_tbl_check_valid(TCON_AXI_MEM_TYPE_OD);
}

int lcd_tcon_mem_demura_is_valid(void)
{
	return lcd_tcon_axi_tbl_check_valid(TCON_AXI_MEM_TYPE_DEMURA);
}

static void lcd_tcon_axi_mem_config_tl1(void)
{
	unsigned int size[3] = {4162560, 4162560, 1960440};
	unsigned int total_size = 0, temp_size = 0;
	int i;

	for (i = 0; i < tcon_rmem.axi_bank; i++)
		total_size += size[i];
	if (total_size > tcon_rmem.axi_mem_size) {
		LCDERR("%s: tcon axi_mem size 0x%x is not enough, need 0x%x\n",
			__func__, tcon_rmem.axi_mem_size, total_size);
		return;
	}

	tcon_rmem.axi_rmem =
		kcalloc(tcon_rmem.axi_bank, sizeof(struct tcon_rmem_config_s), GFP_KERNEL);
	if (!tcon_rmem.axi_rmem)
		return;

	for (i = 0; i < tcon_rmem.axi_bank; i++) {
		tcon_rmem.axi_rmem[i].mem_paddr =
			tcon_rmem.axi_mem_paddr + temp_size;
		tcon_rmem.axi_rmem[i].mem_vaddr = NULL;
		tcon_rmem.axi_rmem[i].mem_size = size[i];
		temp_size += size[i];
	}
	tcon_rmem.secure_axi_rmem.mem_paddr = tcon_rmem.axi_rmem[0].mem_paddr;
	tcon_rmem.secure_axi_rmem.mem_vaddr = tcon_rmem.axi_rmem[0].mem_vaddr;
	tcon_rmem.secure_axi_rmem.mem_size = size[0];
}

static void lcd_tcon_axi_mem_secure_tl1(void)
{
#if IS_ENABLED(CONFIG_AMLOGIC_TEE)
	lcd_tcon_mem_tee_protect(1);
#endif
}

/* default od secure */
static void lcd_tcon_axi_mem_secure_t3(void)
{
	unsigned int *data;

	if (!tcon_rmem.secure_cfg_rmem.mem_vaddr)
		return;

	/* only default protect od mem */
	data = (unsigned int *)tcon_rmem.secure_cfg_rmem.mem_vaddr;
	tcon_rmem.secure_axi_rmem.sec_protect = *data;
	tcon_rmem.secure_axi_rmem.sec_handle = *(data + 1);
	if (tcon_rmem.secure_axi_rmem.sec_protect == 0) {
		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
			LCDPR("%s: axi_rmem[0] is unprotect\n", __func__);
		return;
	}
	if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
		LCDPR("%s: axi_rmem[0] protect handle: %d\n",
			__func__, tcon_rmem.secure_axi_rmem.sec_handle);
	}
}

#if IS_ENABLED(CONFIG_AMLOGIC_TEE)
void lcd_tcon_mem_tee_get_status(void)
{
	char *print_buf;

	print_buf = kcalloc(PR_BUF_MAX, sizeof(char), GFP_KERNEL);
	if (!print_buf) {
		LCDERR("%s: buf malloc error\n", __func__);
		return;
	}

	lcd_tcon_axi_mem_print(&tcon_rmem, print_buf, 0);
	pr_info("%s\n", print_buf);
	kfree(print_buf);
}

int lcd_tcon_mem_tee_protect(int protect_en)
{
	int ret;

	if (tcon_rmem.flag == 0 || !tcon_rmem.axi_rmem) {
		LCDERR("%s: no axi_rmem\n", __func__);
		return -1;
	}

	if (protect_en) {
		if (tcon_rmem.secure_axi_rmem.sec_protect) {
			LCDPR("%s: secure_rmem is already protected\n", __func__);
			return 0;
		}

		ret = tee_protect_mem_by_type(TEE_MEM_TYPE_TCON,
					      tcon_rmem.secure_axi_rmem.mem_paddr,
					      tcon_rmem.secure_axi_rmem.mem_size,
					      &tcon_rmem.secure_axi_rmem.sec_handle);

		if (ret) {
			LCDERR("%s: protect failed! secure_rmem start:0x%08x, size:0x%x, ret:%d\n",
			       __func__,
			      (unsigned int)tcon_rmem.secure_axi_rmem.mem_paddr,
			       tcon_rmem.secure_axi_rmem.mem_size, ret);
			return -1;
		}
		tcon_rmem.secure_axi_rmem.sec_protect = 1;
		LCDPR("%s: protect OK. secure_rmem start: 0x%08x, size: 0x%x\n",
			__func__,
			(unsigned int)tcon_rmem.secure_axi_rmem.mem_paddr,
			tcon_rmem.secure_axi_rmem.mem_size);
	} else {
		if (tcon_rmem.secure_axi_rmem.sec_protect == 0)
			return 0;

		tee_unprotect_mem(tcon_rmem.secure_axi_rmem.sec_handle);
		tcon_rmem.secure_axi_rmem.sec_protect = 0;
		tcon_rmem.secure_axi_rmem.sec_handle = 0;
		LCDPR("%s: unprotect OK. secure_rmem start: 0x%08x, size: 0x%x\n",
			__func__,
			(unsigned int)tcon_rmem.secure_axi_rmem.mem_paddr,
			tcon_rmem.secure_axi_rmem.mem_size);
	}

	return 0;
}
#endif

static void lcd_tcon_axi_mem_config(void)
{
	struct lcd_tcon_axi_mem_cfg_s *axi_mem_cfg = NULL;
	unsigned int temp_size = 0;
	unsigned int mem_paddr = 0, od_mem_size = 0;
	unsigned char *mem_vaddr = NULL;
	int i;

	if (!lcd_tcon_conf->axi_tbl_len || !lcd_tcon_conf->axi_mem_cfg_tbl)
		return;

	tcon_rmem.axi_rmem = kcalloc(tcon_rmem.axi_bank,
		sizeof(struct tcon_rmem_config_s), GFP_KERNEL);
	if (!tcon_rmem.axi_rmem)
		return;

	tcon_rmem.axi_reg = kcalloc(tcon_rmem.axi_bank, sizeof(unsigned int), GFP_KERNEL);
	if (!tcon_rmem.axi_reg) {
		kfree(tcon_rmem.axi_rmem);
		return;
	}

	for (i = 0; i < lcd_tcon_conf->axi_tbl_len; i++) {
		axi_mem_cfg = &lcd_tcon_conf->axi_mem_cfg_tbl[i];
		tcon_rmem.axi_reg[i] = axi_mem_cfg->axi_reg;
		if (!axi_mem_cfg->mem_valid)
			continue;
		tcon_rmem.axi_rmem[i].mem_paddr = tcon_rmem.axi_mem_paddr + temp_size;
		tcon_rmem.axi_rmem[i].mem_vaddr = (unsigned char *)
			(unsigned long)tcon_rmem.axi_rmem[i].mem_paddr;
		tcon_rmem.axi_rmem[i].mem_size = axi_mem_cfg->mem_size;
		temp_size += axi_mem_cfg->mem_size;

		if (axi_mem_cfg->mem_type == TCON_AXI_MEM_TYPE_OD) {
			if (!mem_paddr) {
				mem_paddr = tcon_rmem.axi_rmem[i].mem_paddr;
				mem_vaddr = tcon_rmem.axi_rmem[i].mem_vaddr;
			}
			od_mem_size += tcon_rmem.axi_rmem[i].mem_size;
		}
	}
	tcon_rmem.secure_axi_rmem.mem_paddr = mem_paddr;
	tcon_rmem.secure_axi_rmem.mem_vaddr = mem_vaddr;
	tcon_rmem.secure_axi_rmem.mem_size = od_mem_size;
}

static int lcd_tcon_mem_config(void)
{
	unsigned int mem_size = 0, mem_od_size = 0, mem_dmr_size = 0;
	unsigned int axi_mem_size = 0;
	int ret = -1, i = 0;
	struct lcd_tcon_axi_mem_cfg_s *axi_cfg = NULL;

	if (tcon_rmem.flag == 0)
		return -1;

	tcon_rmem.axi_bank = lcd_tcon_conf->axi_bank;
	if (lcd_tcon_conf->axi_tbl_len && lcd_tcon_conf->axi_mem_cfg_tbl) {
		for (i = 0; i < lcd_tcon_conf->axi_tbl_len; i++) {
			axi_cfg = &lcd_tcon_conf->axi_mem_cfg_tbl[i];
			LCDPR("axi mem type=%d, size=%#x, reg=%#x, valid=%d\n",
				axi_cfg->mem_type, axi_cfg->mem_size,
				axi_cfg->axi_reg, axi_cfg->mem_valid);
			switch (axi_cfg->mem_type) {
			case TCON_AXI_MEM_TYPE_OD:
				mem_od_size += axi_cfg->mem_size;
				break;
			case TCON_AXI_MEM_TYPE_DEMURA:
				mem_dmr_size += axi_cfg->mem_size;
				break;
			default:
				LCDERR("Unsupport mem type=%d\n", axi_cfg->mem_type);
				break;
			}
		}

		/* check mem */
		axi_mem_size = mem_od_size + mem_dmr_size;
		mem_size = axi_mem_size + lcd_tcon_conf->bin_path_size +
			lcd_tcon_conf->secure_cfg_size;
		if (tcon_rmem.rsv_mem_size < mem_size) {
			axi_mem_size = mem_od_size;
			mem_size = axi_mem_size + lcd_tcon_conf->bin_path_size +
				lcd_tcon_conf->secure_cfg_size;
			if (tcon_rmem.rsv_mem_size < mem_size) {
				LCDERR("%s: tcon mem size 0x%x is not enough, need min 0x%x\n",
					__func__, tcon_rmem.rsv_mem_size, mem_size);
				return -1;
			}
			lcd_tcon_axi_tbl_set_valid(TCON_AXI_MEM_TYPE_OD, 1);
		} else {
			lcd_tcon_axi_tbl_set_valid(TCON_AXI_MEM_TYPE_OD, 1);
			lcd_tcon_axi_tbl_set_valid(TCON_AXI_MEM_TYPE_DEMURA, 1);
		}

		lcd_tcon_conf->axi_mem_size = axi_mem_size;
	} else {
		mem_size = lcd_tcon_conf->axi_mem_size + lcd_tcon_conf->bin_path_size
			+ lcd_tcon_conf->secure_cfg_size;

		if (tcon_rmem.rsv_mem_size < mem_size) {
			LCDERR("%s: tcon mem size 0x%x is not enough, need 0x%x\n",
			       __func__, tcon_rmem.rsv_mem_size, mem_size);
			return -1;
		}
	}

	tcon_rmem.axi_mem_size = lcd_tcon_conf->axi_mem_size;
	tcon_rmem.axi_mem_paddr = tcon_rmem.rsv_mem_paddr;
	tcon_rmem.sw_mem_paddr = tcon_rmem.axi_mem_paddr + tcon_rmem.axi_mem_size;
	if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
		LCDPR("%s: axi_mem paddr: 0x%08x, size: 0x%x, sw_mem paddr: 0x%08x\n",
			  __func__, (unsigned int)tcon_rmem.axi_mem_paddr,
			  tcon_rmem.axi_mem_size,
			  (unsigned int)tcon_rmem.sw_mem_paddr);
	}

	if (lcd_tcon_conf->tcon_axi_mem_config)
		lcd_tcon_conf->tcon_axi_mem_config();
	else
		lcd_tcon_axi_mem_config();

	tcon_rmem.bin_path_rmem.mem_size = lcd_tcon_conf->bin_path_size;
	tcon_rmem.bin_path_rmem.mem_paddr = tcon_rmem.sw_mem_paddr;
	tcon_rmem.bin_path_rmem.mem_vaddr =
		lcd_tcon_paddrtovaddr(tcon_rmem.bin_path_rmem.mem_paddr,
				      tcon_rmem.bin_path_rmem.mem_size);
	if ((lcd_debug_print_flag & LCD_DBG_PR_NORMAL) &&
	    tcon_rmem.bin_path_rmem.mem_size > 0) {
		LCDPR("tcon bin_path paddr: 0x%08x, vaddr: 0x%px, size: 0x%x\n",
		      (unsigned int)tcon_rmem.bin_path_rmem.mem_paddr,
		      tcon_rmem.bin_path_rmem.mem_vaddr,
		      tcon_rmem.bin_path_rmem.mem_size);
	}

	ret = lcd_tcon_bin_path_update(tcon_rmem.bin_path_rmem.mem_size);
	if (ret)
		return -1;

	tcon_rmem.secure_cfg_rmem.mem_size = lcd_tcon_conf->secure_cfg_size;
	tcon_rmem.secure_cfg_rmem.mem_paddr =
		tcon_rmem.bin_path_rmem.mem_paddr + tcon_rmem.bin_path_rmem.mem_size;
	if (tcon_rmem.secure_cfg_rmem.mem_size > 0) {
		tcon_rmem.secure_cfg_rmem.mem_vaddr =
			lcd_tcon_paddrtovaddr(tcon_rmem.secure_cfg_rmem.mem_paddr,
				      tcon_rmem.secure_cfg_rmem.mem_size);
		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
			LCDPR("tcon secure_cfg paddr: 0x%08x, vaddr: 0x%px, size: 0x%x\n",
				(unsigned int)tcon_rmem.secure_cfg_rmem.mem_paddr,
				tcon_rmem.secure_cfg_rmem.mem_vaddr,
				tcon_rmem.secure_cfg_rmem.mem_size);
		}
	} else {
		tcon_rmem.secure_cfg_rmem.mem_vaddr = NULL;
	}

	if (lcd_tcon_conf->tcon_axi_mem_secure && tcon_rmem.secure_cfg_rmem.mem_vaddr)
		lcd_tcon_conf->tcon_axi_mem_secure();

	if (tcon_mm_table.version == 0)
		ret = lcd_tcon_mm_table_config_v0();
	else
		ret = lcd_tcon_mm_table_config_v1();
	return ret;
}

static int lcd_tcon_reserved_memory_init(struct aml_lcd_drv_s *pdrv)
{
	struct device_node *mem_node;
	struct resource res;
	int ret;

	mem_node = of_parse_phandle(pdrv->dev->of_node, "memory-region", 0);
	if (!mem_node) {
		LCDERR("tcon get memory-region fail\n");
		return -1;
	}

	ret = of_address_to_resource(mem_node, 0, &res);
	if (ret) {
		LCDERR("tcon reserve memory source fail\n");
		return -1;
	}

	tcon_rmem.rsv_mem_paddr = res.start;
	tcon_rmem.rsv_mem_size = resource_size(&res);
	if (tcon_rmem.rsv_mem_paddr == 0) {
		LCDERR("tcon resv_mem paddr 0 error\n");
		tcon_rmem.rsv_mem_size = 0;
		return -1;
	}
	if (tcon_rmem.rsv_mem_size == 0) {
		LCDERR("tcon resv_mem size 0 error\n");
		tcon_rmem.rsv_mem_paddr = 0;
		return -1;
	}

	if (of_find_property(mem_node, "no-map", NULL))
		tcon_rmem.flag = 2;
	else
		tcon_rmem.flag = 1;
	LCDPR("tcon resv_mem flag:%d, paddr:0x%lx, size:0x%x\n",
		tcon_rmem.flag,
		(unsigned long)tcon_rmem.rsv_mem_paddr,
		tcon_rmem.rsv_mem_size);

	return 0;
}

static void lcd_tcon_reserved_memory_release(struct aml_lcd_drv_s *pdrv)
{
	struct device_node *mem_node;
	struct resource res;
	unsigned long end;
	unsigned int highmem_flag = 0;
	int ret;

	if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
		LCDPR("%s\n", __func__);

	mem_node = of_parse_phandle(pdrv->dev->of_node, "memory-region", 0);
	if (!mem_node) {
		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
			LCDPR("no tcon memory-region\n");
		return;
	}

	ret = of_address_to_resource(mem_node, 0, &res);
	if (ret) {
		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
			LCDPR("no tcon reserve memory source\n");
		return;
	}

	if (of_find_property(mem_node, "no-map", NULL)) {
		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
			LCDPR("no-map memory source, skip release\n");
		return;
	}

	end = PAGE_ALIGN(res.end);

	highmem_flag = PageHighMem(phys_to_page(res.start));
	if (!highmem_flag) {
		aml_free_reserved_area(__va(res.start), __va(end),
			0, "lcd_tcon_reserved");
	}
	if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
		if (highmem_flag) {
			LCDPR("Release memory need to confirm linear address\n");
		} else {
			LCDPR("free reserved area: paddr:0x%lx, size:0x%lx\n",
				(long)res.start, (long)resource_size(&res));
		}
	}
}

static irqreturn_t lcd_tcon_isr(int irq, void *dev_id)
{
	struct aml_lcd_drv_s *pdrv = aml_lcd_get_driver(0);
	unsigned int temp;

	if ((pdrv->status & LCD_STATUS_IF_ON) == 0)
		return IRQ_HANDLED;

	temp = lcd_tcon_read(pdrv, TCON_INTR_RO);
	if (temp & 0x2) {
		LCDPR("%s: tcon sw_reset triggered\n", __func__);
		lcd_tcon_core_update(pdrv);
	}
	if (temp & 0x40)
		LCDPR("%s: tcon ddr interface error triggered\n", __func__);

	return IRQ_HANDLED;
}

static void lcd_tcon_intr_init(struct aml_lcd_drv_s *pdrv)
{
	unsigned int tcon_irq = 0;

	if (!pdrv->res_tcon_irq) {
		LCDERR("res_tcon_irq is null\n");
		return;
	}
	tcon_irq = pdrv->res_tcon_irq->start;
	if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
		LCDPR("tcon_irq: %d\n", tcon_irq);

	if (request_irq(tcon_irq, lcd_tcon_isr, IRQF_SHARED,
		"lcd_tcon", (void *)"lcd_tcon")) {
		LCDERR("can't request lcd_tcon irq\n");
	}

	lcd_tcon_write(pdrv, TCON_INTR_MASKN, TCON_INTR_MASKN_VAL);
}

static int lcd_tcon_load_init_data_from_unifykey(struct aml_lcd_drv_s *pdrv)
{
	struct lcd_tcon_config_s *tcon_conf = get_lcd_tcon_config();
	int key_len, data_len, ret;

	data_len = tcon_mm_table.core_reg_table_size;
	ret = lcd_unifykey_get_size("lcd_tcon", &key_len);
	if (ret)
		return -1;
	tcon_mm_table.core_reg_table = kcalloc(key_len, sizeof(unsigned char), GFP_KERNEL);
	if (!tcon_mm_table.core_reg_table)
		return -1;
	ret = lcd_unifykey_get_no_header("lcd_tcon", tcon_mm_table.core_reg_table, key_len);
	if (ret)
		goto lcd_tcon_load_init_data_err;

	memset(tcon_local_cfg.bin_ver, 0, TCON_BIN_VER_LEN);
	if (tcon_conf && tcon_conf->tcon_init_table_pre_proc)
		tcon_conf->tcon_init_table_pre_proc(tcon_mm_table.core_reg_table);

	LCDPR("tcon: load init data len: %d\n", data_len);
	return 0;

lcd_tcon_load_init_data_err:
	kfree(tcon_mm_table.core_reg_table);
	tcon_mm_table.core_reg_table = NULL;
	LCDERR("%s: tcon unifykey load error!!!\n", __func__);
	return -1;
}

void lcd_tcon_init_data_version_update(char *data_buf)
{
	if (!data_buf)
		return;

	memcpy(tcon_local_cfg.bin_ver, data_buf, LCD_TCON_INIT_BIN_VERSION_SIZE);
	tcon_local_cfg.bin_ver[TCON_BIN_VER_LEN - 1] = '\0';
}

static int lcd_tcon_load_init_data_from_unifykey_new(struct aml_lcd_drv_s *pdrv)
{
	int key_len, data_len;
	unsigned char *buf, *p;
	struct lcd_tcon_init_block_header_s *data_header;
	struct lcd_tcon_config_s *tcon_conf = get_lcd_tcon_config();
	int ret;

	data_len = tcon_mm_table.core_reg_table_size + LCD_TCON_DATA_BLOCK_HEADER_SIZE;
	ret = lcd_unifykey_get_size("lcd_tcon", &key_len);
	if (ret)
		return -1;
	if (key_len < data_len) {
		LCDERR("%s: key_len %d is not enough, need %d\n",
			__func__, key_len, data_len);
		return -1;
	}
	buf = kcalloc(key_len, sizeof(unsigned char), GFP_KERNEL);
	if (!buf)
		return -1;
	data_header = kzalloc(LCD_TCON_DATA_BLOCK_HEADER_SIZE, GFP_KERNEL);
	if (!data_header)
		goto lcd_tcon_load_init_data_new_err;
	tcon_mm_table.core_reg_header = data_header;

	ret = lcd_unifykey_get_tcon("lcd_tcon", buf, key_len);
	if (ret)
		goto lcd_tcon_load_init_data_new_err;
	memcpy(data_header, buf, LCD_TCON_DATA_BLOCK_HEADER_SIZE);
	if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL) {
		LCDPR("unifykey header:\n");
		LCDPR("crc32             = 0x%08x\n", data_header->crc32);
		LCDPR("block_size        = %d\n", data_header->block_size);
		LCDPR("chipid            = %d\n", data_header->chipid);
		LCDPR("resolution        = %dx%d\n",
			data_header->h_active, data_header->v_active);
		LCDPR("block_ctrl        = 0x%x\n", data_header->block_ctrl);
		LCDPR("name              = %s\n", data_header->name);
	}
	lcd_tcon_init_data_version_update(data_header->version);

	data_len = tcon_mm_table.core_reg_table_size;
	tcon_mm_table.core_reg_table = kcalloc(data_len, sizeof(unsigned char), GFP_KERNEL);
	if (!tcon_mm_table.core_reg_table)
		goto lcd_tcon_load_init_data_new_err;
	p = buf + LCD_TCON_DATA_BLOCK_HEADER_SIZE;
	memcpy(tcon_mm_table.core_reg_table, p, data_len);
	if (tcon_conf && tcon_conf->tcon_init_table_pre_proc)
		tcon_conf->tcon_init_table_pre_proc(tcon_mm_table.core_reg_table);

	kfree(buf);

	tcon_local_cfg.cur_core_reg_table = tcon_mm_table.core_reg_table;
	lcd_tcon_init_setting_check(pdrv, &pdrv->config.timing.dft_timing,
			tcon_mm_table.core_reg_table);

	LCDPR("tcon: load init data len: %d, ver: %s\n",
	      data_len, tcon_local_cfg.bin_ver);
	return 0;

lcd_tcon_load_init_data_new_err:
	kfree(buf);
	LCDERR("%s: tcon unifykey load error!!!\n", __func__);
	return -1;
}

static int lcd_tcon_get_config(struct aml_lcd_drv_s *pdrv)
{
	tcon_mm_table.data_init = NULL;
	tcon_mm_table.core_reg_table_size = lcd_tcon_conf->reg_table_len;
	INIT_LIST_HEAD(&tcon_local_cfg.pdf_data_list);
	if (lcd_tcon_conf->core_reg_ver)
		lcd_tcon_load_init_data_from_unifykey_new(pdrv);
	else
		lcd_tcon_load_init_data_from_unifykey(pdrv);

	lcd_tcon_pdf_init(pdrv);
	//lcd_swpdf_init(pdrv);

	lcd_tcon_reserved_mem_data_load(pdrv);
	pdrv->tcon_status = tcon_mm_table.lut_valid_flag;
	tcon_mm_table.multi_lut_update = 1;

	lcd_tcon_intr_init(pdrv);

	lcd_tcon_fw_prepare(pdrv, lcd_tcon_conf);

	lcd_tcon_debug_file_add(pdrv, &tcon_local_cfg);

	return 0;
}

static int lcd_tcon_probe_retry_cnt;
static void lcd_tcon_get_config_work(struct work_struct *p_work)
{
	struct delayed_work *d_work;
	struct aml_lcd_drv_s *pdrv;
	bool is_init;

	d_work = container_of(p_work, struct delayed_work, work);
	pdrv = container_of(d_work, struct aml_lcd_drv_s, config_probe_dly_work);

	is_init = lcd_unifykey_init_get();
	if (!is_init) {
		if (lcd_tcon_probe_retry_cnt++ < LCD_UNIFYKEY_WAIT_TIMEOUT) {
			lcd_queue_delayed_work(&pdrv->tcon_config_dly_work,
				LCD_UNIFYKEY_RETRY_INTERVAL);
			return;
		}
		LCDERR("tcon: key_init_flag=%d, exit\n", is_init);
		return;
	}

	lcd_tcon_get_config(pdrv);
}

/* **********************************
 * tcon match data
 * **********************************
 */
static struct lcd_tcon_axi_mem_cfg_s axi_mem_cfg_tbl_t5[] = {
	{ TCON_AXI_MEM_TYPE_OD,     0x00800000, 0x261, 0 },  /* 8M */
	{ TCON_AXI_MEM_TYPE_DEMURA, 0x00100000, 0x1a9, 0 },  /* 1M */
};

static struct lcd_tcon_axi_mem_cfg_s axi_mem_cfg_tbl_t5d[] = {
	{ TCON_AXI_MEM_TYPE_OD,     0x00400000, 0x261, 0 },  /* 4M */
};

static struct lcd_tcon_axi_mem_cfg_s axi_mem_cfg_tbl_t3x[] = {
	{ TCON_AXI_MEM_TYPE_OD,     0x00400000, 0x261, 0 },  /* 4M */
	{ TCON_AXI_MEM_TYPE_OD,     0x00400000, 0x26d, 0 },  /* 4M */
	{ TCON_AXI_MEM_TYPE_DEMURA, 0x00100000, 0x1a9, 0 },  /* 1M */
};

static struct lcd_tcon_axi_mem_cfg_s axi_mem_cfg_tbl_txhd2[] = {
	{ TCON_AXI_MEM_TYPE_OD,     0x00400000, 0x261, 0 },  /* 4M */
	{ TCON_AXI_MEM_TYPE_DEMURA, 0x00100000, 0x19b, 0 },  /* 1M */
};

static struct lcd_tcon_config_s tcon_data_tl1 = {
	.tcon_valid = 0,
	.tcon_is_busy = 0,

	.core_reg_ver = 0,
	.core_reg_width = LCD_TCON_CORE_REG_WIDTH_TL1,
	.reg_table_width = LCD_TCON_TABLE_WIDTH_TL1,
	.reg_table_len = LCD_TCON_TABLE_LEN_TL1,
	.core_reg_start = TCON_CORE_REG_START_TL1,
	.top_reg_base = TCON_TOP_BASE,

	.reg_top_ctrl = TCON_TOP_CTRL,
	.bit_en = BIT_TOP_EN_TL1,

	.reg_core_od = REG_CORE_OD_TL1,
	.bit_od_en = BIT_OD_EN_TL1,

	.reg_ctrl_timing_base = REG_LCD_TCON_MAX,
	.ctrl_timing_offset = CTRL_TIMING_OFFSET_TL1,
	.ctrl_timing_cnt = CTRL_TIMING_CNT_TL1,

	.axi_bank = LCD_TCON_AXI_BANK_TL1,

	/*rsv_mem(12M)    axi_mem(10M)   bin_path(10K)
	 *             |----------------|-------------|
	 */
	.rsv_mem_size    = 0x00c00000,
	.axi_mem_size    = 0x00a00000,
	.bin_path_size   = 0x00002800,
	.secure_cfg_size = 0x00000040,
	.vac_size        = 0x00002000, /* 8k */
	.demura_set_size = 0x00001000, /* 4k */
	.demura_lut_size = 0x00120000, /* 1152k */
	.acc_lut_size    = 0x00001000, /* 4k */

	.tcon_axi_mem_config = lcd_tcon_axi_mem_config_tl1,
	.tcon_axi_mem_secure = lcd_tcon_axi_mem_secure_tl1,
	.tcon_init_table_pre_proc = NULL,
	.tcon_global_reset = NULL,
	.tcon_top_init = lcd_tcon_top_set_tl1,
	.tcon_enable = lcd_tcon_enable_tl1,
	.tcon_disable = lcd_tcon_disable_tl1,
	.tcon_reload = NULL,
	.tcon_reload_pre = NULL,
	.lut_dma_update = NULL,
	.tcon_check = NULL,
};

static struct lcd_tcon_config_s tcon_data_t5 = {
	.tcon_valid = 0,
	.tcon_is_busy = 0,

	.core_reg_ver = 1, /* new version with header */
	.core_reg_width = LCD_TCON_CORE_REG_WIDTH_T5,
	.reg_table_width = LCD_TCON_TABLE_WIDTH_T5,
	.reg_table_len = LCD_TCON_TABLE_LEN_T5,
	.core_reg_start = TCON_CORE_REG_START_T5,
	.top_reg_base = TCON_TOP_BASE,

	.reg_top_ctrl = REG_LCD_TCON_MAX,
	.bit_en = BIT_TOP_EN_T5,

	.reg_core_od = REG_CORE_OD_T5,
	.bit_od_en = BIT_OD_EN_T5,

	.reg_ctrl_timing_base = REG_LCD_TCON_MAX,
	.ctrl_timing_offset = CTRL_TIMING_OFFSET_T5,
	.ctrl_timing_cnt = CTRL_TIMING_CNT_T5,

	.axi_bank = LCD_TCON_AXI_BANK_T5,

	/*rsv_mem(12M)    axi_mem(10M)   bin_path(10K) secure_cfg(64byte)
	 *             |----------------|-------------|-------------|
	 */
	.rsv_mem_size    = 0x00a02840,
	.axi_mem_size    = 0x00a00000,
	.bin_path_size   = 0x00002800,
	.secure_cfg_size = 0x00000040,
	.vac_size        = 0,
	.demura_set_size = 0,
	.demura_lut_size = 0,
	.acc_lut_size    = 0,

	.axi_tbl_len = ARRAY_SIZE(axi_mem_cfg_tbl_t5),
	.axi_mem_cfg_tbl = axi_mem_cfg_tbl_t5,

	.tcon_axi_mem_secure = lcd_tcon_axi_mem_secure_t3,
	.tcon_init_table_pre_proc = lcd_tcon_init_table_pre_proc,
	.tcon_global_reset = lcd_tcon_global_reset_t5,
	.tcon_top_init = lcd_tcon_top_set_t5,
	.tcon_enable = lcd_tcon_enable_t5,
	.tcon_disable = lcd_tcon_disable_t5,
	.tcon_reload = NULL,
	.tcon_reload_pre = NULL,
	.lut_dma_update = NULL,
	.tcon_check = lcd_tcon_setting_check_t5,
};

static struct lcd_tcon_config_s tcon_data_t5d = {
	.tcon_valid = 0,
	.tcon_is_busy = 0,

	.core_reg_ver = 1, /* new version with header */
	.core_reg_width = LCD_TCON_CORE_REG_WIDTH_T5D,
	.reg_table_width = LCD_TCON_TABLE_WIDTH_T5D,
	.reg_table_len = LCD_TCON_TABLE_LEN_T5D,
	.core_reg_start = TCON_CORE_REG_START_T5D,

	.reg_top_ctrl = REG_LCD_TCON_MAX,
	.bit_en = BIT_TOP_EN_T5D,

	.reg_core_od = REG_CORE_OD_T5D,
	.bit_od_en = BIT_OD_EN_T5D,

	.reg_ctrl_timing_base = REG_LCD_TCON_MAX,
	.ctrl_timing_offset = CTRL_TIMING_OFFSET_T5D,
	.ctrl_timing_cnt = CTRL_TIMING_CNT_T5D,

	.axi_bank = LCD_TCON_AXI_BANK_T5D,

	.rsv_mem_size    = 0x00402840, /* 4M more */
	.axi_mem_size    = 0x00400000, /* 4M */
	.bin_path_size   = 0x00002800, /* 10K */
	.secure_cfg_size = 0x00000040,
	.vac_size        = 0,
	.demura_set_size = 0,
	.demura_lut_size = 0,
	.acc_lut_size    = 0,

	.axi_tbl_len = ARRAY_SIZE(axi_mem_cfg_tbl_t5d),
	.axi_mem_cfg_tbl = axi_mem_cfg_tbl_t5d,

	.tcon_axi_mem_secure = lcd_tcon_axi_mem_secure_t3,
	.tcon_init_table_pre_proc = lcd_tcon_init_table_pre_proc,
	.tcon_global_reset = lcd_tcon_global_reset_t5,
	.tcon_top_init = lcd_tcon_top_set_t5,
	.tcon_enable = lcd_tcon_enable_t5,
	.tcon_disable = lcd_tcon_disable_t5,
	.tcon_reload = NULL,
	.tcon_reload_pre = NULL,
	.lut_dma_update = NULL,
	.tcon_check = lcd_tcon_setting_check_t5d,
};

static struct lcd_tcon_config_s tcon_data_t3 = {
	.tcon_valid = 0,
	.tcon_is_busy = 0,

	.core_reg_ver = 1, /* new version with header */
	.core_reg_width = LCD_TCON_CORE_REG_WIDTH_T5,
	.reg_table_width = LCD_TCON_TABLE_WIDTH_T5,
	.reg_table_len = LCD_TCON_TABLE_LEN_T5,
	.core_reg_start = TCON_CORE_REG_START_T5,
	.top_reg_base = TCON_TOP_BASE,

	.reg_top_ctrl = REG_LCD_TCON_MAX,
	.bit_en = BIT_TOP_EN_T5,

	.reg_core_od = REG_CORE_OD_T5,
	.bit_od_en = BIT_OD_EN_T5,

	.reg_ctrl_timing_base = REG_LCD_TCON_MAX,
	.ctrl_timing_offset = CTRL_TIMING_OFFSET_T5,
	.ctrl_timing_cnt = CTRL_TIMING_CNT_T5,

	.axi_bank = LCD_TCON_AXI_BANK_T5,

	/*rsv_mem(12M)    axi_mem(10M)   bin_path(10K) secure_cfg(64byte)
	 *             |----------------|-------------|-------------|
	 */
	.rsv_mem_size    = 0x00a02840,
	.axi_mem_size    = 0x00a00000,
	.bin_path_size   = 0x00002800,
	.secure_cfg_size = 0x00000040,
	.vac_size        = 0,
	.demura_set_size = 0,
	.demura_lut_size = 0,
	.acc_lut_size    = 0,

	.axi_tbl_len = ARRAY_SIZE(axi_mem_cfg_tbl_t5),
	.axi_mem_cfg_tbl = axi_mem_cfg_tbl_t5,

	.tcon_axi_mem_secure = lcd_tcon_axi_mem_secure_t3,
	.tcon_init_table_pre_proc = lcd_tcon_init_table_pre_proc,
	.tcon_global_reset = lcd_tcon_global_reset_t3,
	.tcon_top_init = lcd_tcon_top_set_t5,
	.tcon_enable = lcd_tcon_enable_t5,
	.tcon_disable = lcd_tcon_disable_t5,
	.tcon_reload = lcd_tcon_reload_t3,
	.tcon_reload_pre = lcd_tcon_reload_pre_t3,
	.lut_dma_update = NULL,
	.tcon_check = lcd_tcon_setting_check_t5,
};

static struct lcd_tcon_config_s tcon_data_t5m = {
	.tcon_valid = 0,
	.tcon_is_busy = 0,

	.core_reg_ver = 1, /* new version with header */
	.core_reg_width = LCD_TCON_CORE_REG_WIDTH_T5,
	.reg_table_width = LCD_TCON_TABLE_WIDTH_T5,
	.reg_table_len = LCD_TCON_TABLE_LEN_T5,
	.core_reg_start = TCON_CORE_REG_START_T5,
	.top_reg_base = TCON_TOP_BASE,

	.reg_top_ctrl = REG_LCD_TCON_MAX,
	.bit_en = BIT_TOP_EN_T5,

	.reg_core_od = REG_CORE_OD_T5,
	.bit_od_en = BIT_OD_EN_T5,

	.reg_ctrl_timing_base = REG_LCD_TCON_MAX,
	.ctrl_timing_offset = CTRL_TIMING_OFFSET_T5,
	.ctrl_timing_cnt = CTRL_TIMING_CNT_T5,

	.axi_bank = LCD_TCON_AXI_BANK_T5,

	/*rsv_mem(12M)    axi_mem(10M)   bin_path(10K) secure_cfg(64byte)
	 *             |----------------|-------------|-------------|
	 */
	.rsv_mem_size    = 0x00a02840,
	.axi_mem_size    = 0x00a00000,
	.bin_path_size   = 0x00002800,
	.secure_cfg_size = 0x00000040,
	.vac_size        = 0,
	.demura_set_size = 0,
	.demura_lut_size = 0,
	.acc_lut_size    = 0,

	.axi_tbl_len = ARRAY_SIZE(axi_mem_cfg_tbl_t5),
	.axi_mem_cfg_tbl = axi_mem_cfg_tbl_t5,

	.tcon_axi_mem_secure = lcd_tcon_axi_mem_secure_t3,
	.tcon_init_table_pre_proc = lcd_tcon_init_table_pre_proc,
	.tcon_global_reset = lcd_tcon_global_reset_t3,
	.tcon_top_init = lcd_tcon_top_set_t5,
	.tcon_enable = lcd_tcon_enable_t5,
	.tcon_disable = lcd_tcon_disable_t5,
	.tcon_reload = lcd_tcon_reload_t3,
	.tcon_reload_pre = lcd_tcon_reload_pre_t3,
	.lut_dma_update = lcd_tcon_lut_dma_update,
	.lut_dma_mif_set = lcd_tcon_lut_dma_mif_set_t5m,
	.lut_dma_enable = lcd_tcon_lut_dma_enable_t5m,
	.lut_dma_disable = lcd_tcon_lut_dma_disable_t5m,
	.tcon_check = lcd_tcon_setting_check_t5,
};
static struct lcd_tcon_config_s tcon_data_t5w = {
	.tcon_valid = 0,
	.tcon_is_busy = 0,

	.core_reg_ver = 1, /* new version with header */
	.core_reg_width = LCD_TCON_CORE_REG_WIDTH_T5,
	.reg_table_width = LCD_TCON_TABLE_WIDTH_T5,
	.reg_table_len = LCD_TCON_TABLE_LEN_T5,
	.core_reg_start = TCON_CORE_REG_START_T5,
	.top_reg_base = TCON_TOP_BASE,

	.reg_top_ctrl = REG_LCD_TCON_MAX,
	.bit_en = BIT_TOP_EN_T5,

	.reg_core_od = REG_CORE_OD_T5,
	.bit_od_en = BIT_OD_EN_T5,

	.reg_ctrl_timing_base = REG_LCD_TCON_MAX,
	.ctrl_timing_offset = CTRL_TIMING_OFFSET_T5,
	.ctrl_timing_cnt = CTRL_TIMING_CNT_T5,

	.axi_bank = LCD_TCON_AXI_BANK_T5,

	/*rsv_mem(12M)    axi_mem(10M)   bin_path(10K)
	 *             |----------------|-------------|
	 */
	.rsv_mem_size    = 0x00a02840,
	.axi_mem_size    = 0x00a00000,
	.bin_path_size   = 0x00002800,
	.secure_cfg_size = 0x00000040,
	.vac_size        = 0,
	.demura_set_size = 0,
	.demura_lut_size = 0,
	.acc_lut_size    = 0,

	.axi_tbl_len = ARRAY_SIZE(axi_mem_cfg_tbl_t5),
	.axi_mem_cfg_tbl = axi_mem_cfg_tbl_t5,

	.tcon_axi_mem_secure = lcd_tcon_axi_mem_secure_t3,
	.tcon_init_table_pre_proc = lcd_tcon_init_table_pre_proc,
	.tcon_global_reset = lcd_tcon_global_reset_t5,
	.tcon_top_init = lcd_tcon_top_set_t5,
	.tcon_enable = lcd_tcon_enable_t5,
	.tcon_disable = lcd_tcon_disable_t5,
	.tcon_reload = lcd_tcon_reload_t3,
	.tcon_reload_pre = lcd_tcon_reload_pre_t3,
	.lut_dma_update = NULL,
	.tcon_check = lcd_tcon_setting_check_t5,
};

static struct lcd_tcon_config_s tcon_data_t3x = {
	.tcon_valid = 0,
	.tcon_is_busy = 0,

	.core_reg_ver = 1, /* new version with header */
	.core_reg_width = LCD_TCON_CORE_REG_WIDTH_T5,
	.reg_table_width = LCD_TCON_TABLE_WIDTH_T5,
	.reg_table_len = LCD_TCON_TABLE_LEN_T3X,
	.core_reg_start = TCON_CORE_REG_START_T5,
	.top_reg_base = TCON_TOP_BASE,

	.reg_top_ctrl = REG_LCD_TCON_MAX,
	.bit_en = BIT_TOP_EN_T5,

	.reg_core_od = REG_CORE_OD_T5,
	.bit_od_en = BIT_OD_EN_T5,

	.reg_ctrl_timing_base = REG_LCD_TCON_MAX,
	.ctrl_timing_offset = CTRL_TIMING_OFFSET_T5,
	.ctrl_timing_cnt = CTRL_TIMING_CNT_T5,

	.axi_bank = LCD_TCON_AXI_BANK_T3X,

	/*rsv_mem(12M)    axi_mem(10M)   bin_path(10K) secure_cfg(64byte)
	 *             |----------------|-------------|-------------|
	 */
	.rsv_mem_size    = 0x00a02840,
	.axi_mem_size    = 0x00a00000,
	.bin_path_size   = 0x00002800,
	.secure_cfg_size = 0x00000040,
	.vac_size        = 0,
	.demura_set_size = 0,
	.demura_lut_size = 0,
	.acc_lut_size    = 0,

	.axi_tbl_len = ARRAY_SIZE(axi_mem_cfg_tbl_t3x),
	.axi_mem_cfg_tbl = (axi_mem_cfg_tbl_t3x),

	.tcon_axi_mem_secure = lcd_tcon_axi_mem_secure_t3,
	.tcon_init_table_pre_proc = lcd_tcon_init_table_pre_proc,
	.tcon_global_reset = lcd_tcon_global_reset_t3x,
	.tcon_top_init = lcd_tcon_top_set_t5,
	.tcon_enable = lcd_tcon_enable_t5,
	.tcon_disable = lcd_tcon_disable_t5,
	.tcon_reload = lcd_tcon_reload_t3,
	.tcon_reload_pre = lcd_tcon_reload_pre_t3,
	.lut_dma_update = NULL,
	.tcon_check = lcd_tcon_setting_check_t5,
};

static struct lcd_tcon_config_s tcon_data_txhd2 = {
	.tcon_valid = 0,

	.core_reg_ver = 1, /* new version with header */
	.core_reg_width = LCD_TCON_CORE_REG_WIDTH_T5D,
	.reg_table_width = LCD_TCON_TABLE_WIDTH_T5D,
	.reg_table_len = LCD_TCON_TABLE_LEN_TXHD2,
	.core_reg_start = TCON_CORE_REG_START_T5D,

	.reg_top_ctrl = REG_LCD_TCON_MAX,
	.bit_en = BIT_TOP_EN_T5D,

	.reg_core_od = REG_CORE_OD_T5D,
	.bit_od_en = BIT_OD_EN_T5D,

	.reg_ctrl_timing_base = REG_LCD_TCON_MAX,
	.ctrl_timing_offset = CTRL_TIMING_OFFSET_T5D,
	.ctrl_timing_cnt = CTRL_TIMING_CNT_T5D,

	.axi_bank = LCD_TCON_AXI_BANK_TXHD2,

	.rsv_mem_size    = 0x00502840,
	.axi_mem_size    = 0x00500000, /* 5M*/
	.bin_path_size   = 0x00002800, /* 10K */
	.secure_cfg_size = 0x00000040, /* 64byte */
	.vac_size        = 0,
	.demura_set_size = 0,
	.demura_lut_size = 0,
	.acc_lut_size    = 0,

	.axi_tbl_len = ARRAY_SIZE(axi_mem_cfg_tbl_txhd2),
	.axi_mem_cfg_tbl = axi_mem_cfg_tbl_txhd2,

	.tcon_axi_mem_secure = lcd_tcon_axi_mem_secure_t3,
	.tcon_init_table_pre_proc = lcd_tcon_init_table_pre_proc,
	.tcon_global_reset = lcd_tcon_global_reset_t5,
	.tcon_top_init = lcd_tcon_top_set_t5,
	.tcon_enable = lcd_tcon_enable_t5,
	.tcon_disable = lcd_tcon_disable_t5,
	.tcon_reload = NULL,
	.tcon_reload_pre = NULL,
	.lut_dma_update = NULL,
	.tcon_check = lcd_tcon_setting_check_t5d,
};

int lcd_tcon_probe(struct aml_lcd_drv_s *pdrv)
{
	// struct cma *cma;
	// unsigned int mem_size;
	int ret = 0;

	lcd_tcon_conf = NULL;
	switch (pdrv->data->chip_type) {
	case LCD_CHIP_TL1:
	case LCD_CHIP_TM2:
		lcd_tcon_conf = &tcon_data_tl1;
		break;
	case LCD_CHIP_T5:
		lcd_tcon_conf = &tcon_data_t5;
		break;
	case LCD_CHIP_T5D:
		lcd_tcon_conf = &tcon_data_t5d;
		break;
	case LCD_CHIP_T3:
		lcd_tcon_conf = &tcon_data_t3;
		break;
	case LCD_CHIP_T5M:
		lcd_tcon_conf = &tcon_data_t5m;
		break;
	case LCD_CHIP_T5W:
		lcd_tcon_conf = &tcon_data_t5w;
		break;
	case LCD_CHIP_T3X:
		lcd_tcon_conf = &tcon_data_t3x;
		break;
	case LCD_CHIP_TXHD2:
		lcd_tcon_conf = &tcon_data_txhd2;
		break;
	default:
		break;
	}
	if (!lcd_tcon_conf)
		return 0;

	switch (pdrv->config.basic.lcd_type) {
	case LCD_MLVDS:
		lcd_tcon_conf->tcon_valid = 1;
		break;
	case LCD_P2P:
		if (pdrv->data->chip_type == LCD_CHIP_T5D ||
		    pdrv->data->chip_type == LCD_CHIP_TXHD2)
			lcd_tcon_conf->tcon_valid = 0;
		else
			lcd_tcon_conf->tcon_valid = 1;
		break;
	default:
		break;
	}
	if (lcd_tcon_conf->tcon_valid == 0) {
		if (lcd_debug_print_flag & LCD_DBG_PR_NORMAL)
			LCDPR("%s: invalid tcon for current lcd_type\n", __func__);
		lcd_tcon_reserved_memory_release(pdrv);
		return 0;
	}

	mutex_init(&lcd_tcon_dbg_mutex);

	lcd_tcon_reserved_memory_init(pdrv);
	lcd_tcon_mem_config();

#ifdef TCON_DBG_TIME
	dbg_cnt_0 = 120 * 60 * 7;
	dbg_vsync_time = kcalloc(dbg_cnt_0, sizeof(unsigned long long), GFP_KERNEL);
	if (!dbg_vsync_time)
		LCDERR("%s: dbg_vsync_time error\n", __func__);
	dbg_cnt_1 = 120 * 60 * 5;
	dbg_list_trave_time = kcalloc(dbg_cnt_1, sizeof(unsigned long long), GFP_KERNEL);
	if (!dbg_list_trave_time)
		LCDERR("%s: dbg_list_trave_time error\n", __func__);
#endif

	spin_lock_init(&tcon_local_cfg.multi_list_lock);
	INIT_DELAYED_WORK(&pdrv->tcon_config_dly_work, lcd_tcon_get_config_work);

	if (lcd_unifykey_init_get())
		ret = lcd_tcon_get_config(pdrv);
	else
		lcd_queue_delayed_work(&pdrv->tcon_config_dly_work, 0);

	return ret;
}

int lcd_tcon_remove(struct aml_lcd_drv_s *pdrv)
{
	int i;
	struct lcd_tcon_data_block_header_s *block_header;

	lcd_tcon_debug_file_remove(&tcon_local_cfg);

	kfree(tcon_mm_table.core_reg_table);
	tcon_mm_table.core_reg_table = NULL;
	if (tcon_mm_table.version > 0 && tcon_mm_table.version < 0xff) {
		tcon_mm_table.data_init = NULL;
		tcon_mm_table.lut_valid_flag = 0;
		tcon_mm_table.data_complete = 0;
		tcon_mm_table.block_bit_flag = 0;
		lcd_tcon_data_multi_remvoe(&tcon_mm_table);
		if (tcon_mm_table.data_mem_vaddr) {
			for (i = 0; i < tcon_mm_table.block_cnt; i++) {
				if (!tcon_mm_table.data_mem_vaddr[i])
					continue;

				block_header = (struct lcd_tcon_data_block_header_s *)
							tcon_mm_table.data_mem_vaddr[i];
				if (!is_block_type_basic_init(block_header->block_type) &&
					is_block_ctrl_dma(block_header->block_ctrl)) {
					dma_free_coherent(&pdrv->pdev->dev,
						block_header->block_size,
						tcon_mm_table.data_mem_vaddr[i],
						tcon_mm_table.data_mem_paddr[i]);
				} else {
					kfree(tcon_mm_table.data_mem_vaddr[i]);
				}
				tcon_mm_table.data_mem_vaddr[i] = NULL;
				tcon_mm_table.data_mem_paddr[i] = 0;
			}
			kfree(tcon_mm_table.data_mem_vaddr);
			tcon_mm_table.data_mem_vaddr = NULL;

			kfree(tcon_mm_table.data_mem_paddr);
			tcon_mm_table.data_mem_paddr = NULL;
		}
	}

	if (tcon_rmem.flag == 1) {
		if (tcon_mm_table.version == 0) {
			lcd_unmap_phyaddr(tcon_rmem.vac_rmem.mem_vaddr);
			lcd_unmap_phyaddr(tcon_rmem.demura_set_rmem.mem_vaddr);
			lcd_unmap_phyaddr(tcon_rmem.demura_lut_rmem.mem_vaddr);
			lcd_unmap_phyaddr(tcon_rmem.acc_lut_rmem.mem_vaddr);
			tcon_rmem.vac_rmem.mem_vaddr = NULL;
			tcon_rmem.demura_set_rmem.mem_vaddr = NULL;
			tcon_rmem.demura_lut_rmem.mem_vaddr = NULL;
			tcon_rmem.acc_lut_rmem.mem_vaddr = NULL;
		}
		lcd_unmap_phyaddr(tcon_rmem.bin_path_rmem.mem_vaddr);
	} else {
		LCDPR("tcon free memory: base:0x%lx, size:0x%x\n",
			(unsigned long)tcon_rmem.rsv_mem_paddr,
			tcon_rmem.rsv_mem_size);
		memunmap(tcon_rmem.rsv_mem_vaddr);
		tcon_rmem.rsv_mem_vaddr = NULL;
		tcon_rmem.rsv_mem_paddr = 0;
	}

	cancel_delayed_work(&pdrv->tcon_config_dly_work);

	if (lcd_tcon_conf) {
		/* lcd_tcon_conf == NULL; */
		lcd_tcon_conf->tcon_valid = 0;
	}

	return 0;
}
