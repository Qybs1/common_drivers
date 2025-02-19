// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/irqreturn.h>
#include <linux/module.h>
#include <linux/mm.h>

#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/kallsyms.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/sched/clock.h>
#include <linux/amlogic/page_trace.h>
#include "ddr_port.h"
#include "dmc_monitor.h"

#define DMC_PROT0_STA			(0x00e0 << 2)
#define DMC_PROT0_EDA			(0x00e1 << 2)
#define DMC_PROT0_CTRL			(0x00e2 << 2)
#define DMC_PROT0_CTRL1			(0x00e3 << 2)
#define DMC_PROT0_CTRL2			(0x00ed << 2)

#define DMC_PROT1_STA			(0x00e4 << 2)
#define DMC_PROT1_EDA			(0x00e5 << 2)
#define DMC_PROT1_CTRL			(0x00e6 << 2)
#define DMC_PROT1_CTRL1			(0x00e7 << 2)
#define DMC_PROT1_CTRL2			(0x00ee << 2)

#define DMC_PROT_VIO_0			(0x00e8 << 2)
#define DMC_PROT_VIO_1			(0x00e9 << 2)
#define DMC_PROT_VIO_2			(0x00ea << 2)
#define DMC_PROT_VIO_3			(0x00eb << 2)
#define DMC_VIO_PROT0			BIT(6)
#define DMC_VIO_PROT1			BIT(7)

#define DMC_PROT_IRQ_CTRL_STS		(0x00ec << 2)

#define DMC_SEC_STATUS			(0x051a << 2)
#define DMC_VIO_ADDR0			(0x051b << 2)
#define DMC_VIO_ADDR1			(0x051c << 2)
#define DMC_VIO_ADDR2			(0x051d << 2)
#define DMC_VIO_ADDR3			(0x051e << 2)

static size_t s7_dmc_dump_reg(char *buf)
{
	size_t sz = 0, i;
	unsigned long val;
	void *io = dmc_mon->mon_comm[0].io_mem;

	for (i = 0; i < 2; i++) {
		val = dmc_prot_rw(io, DMC_PROT0_STA + (i * 16), 0, DMC_READ);
		sz += sprintf(buf + sz, "DMC_PROT%zu_STA:%lx\n", i, val);
		val = dmc_prot_rw(io, DMC_PROT0_EDA + (i * 16), 0, DMC_READ);
		sz += sprintf(buf + sz, "DMC_PROT%zu_EDA:%lx\n", i, val);
		val = dmc_prot_rw(io, DMC_PROT0_CTRL + (i * 16), 0, DMC_READ);
		sz += sprintf(buf + sz, "DMC_PROT%zu_CTRL:%lx\n", i, val);
		val = dmc_prot_rw(io, DMC_PROT0_CTRL1 + (i * 16), 0, DMC_READ);
		sz += sprintf(buf + sz, "DMC_PROT%zu_CTRL1:%lx\n", i, val);
	}
	for (i = 0; i < 4; i++) {
		val = dmc_prot_rw(io, DMC_PROT_VIO_0 + (i << 2), 0, DMC_READ);
		sz += sprintf(buf + sz, "DMC_PROT_VIO_%zu:%lx\n", i, val);
	}

	val = dmc_prot_rw(io, DMC_PROT_IRQ_CTRL_STS, 0, DMC_READ);
	sz += sprintf(buf + sz, "DMC_PROT_IRQ_CTRL_STS:%lx\n", val);

	return sz;
}

static int check_violation(struct dmc_monitor *mon, void *data)
{
	int ret = -1;
	unsigned long irqreg;
	struct dmc_mon_comm *mon_comm = (struct dmc_mon_comm *)data;

	/* irq write */
	irqreg = dmc_prot_rw(mon_comm->io_mem, DMC_PROT_IRQ_CTRL_STS, 0, DMC_READ);

	if (irqreg & DMC_WRITE_VIOLATION) {
		/* combine address */
		mon_comm->time = sched_clock();
		mon_comm->status = dmc_prot_rw(mon_comm->io_mem, DMC_PROT_VIO_1, 0, DMC_READ);
		mon_comm->addr = dmc_prot_rw(mon_comm->io_mem, DMC_PROT_VIO_0, 0, DMC_READ);
		mon_comm->rw = 'w';
		ret = 0;
	} else if (irqreg & DMC_READ_VIOLATION) {
		/* irq read */
		mon_comm->time = sched_clock();
		mon_comm->status = dmc_prot_rw(mon_comm->io_mem, DMC_PROT_VIO_3, 0, DMC_READ);
		/* combine address */
		mon_comm->addr = dmc_prot_rw(mon_comm->io_mem, DMC_PROT_VIO_2, 0, DMC_READ);
		mon_comm->rw = 'r';
		ret = 0;
	}

	if (!ret)
		dmc_vio_check_page(data);

	return ret;
}

static int s7_dmc_mon_irq(struct dmc_monitor *mon, void *data, char clear)
{
	unsigned long irqreg;
	struct dmc_mon_comm *mon_comm = (struct dmc_mon_comm *)data;

	if (clear) {
		/* clear irq */
		irqreg = dmc_prot_rw(mon_comm->io_mem, DMC_PROT_IRQ_CTRL_STS, 0, DMC_READ);
		if (dmc_mon->debug & DMC_DEBUG_SUSPEND)
			irqreg &= ~0x04;
		else
			irqreg |= 0x04;		/* en */
		dmc_prot_rw(mon_comm->io_mem, DMC_PROT_IRQ_CTRL_STS, irqreg, DMC_WRITE);
	} else {
		return check_violation(mon, data);
	}

	return 0;
}

static void s7_dmc_vio_to_port(void *data, unsigned long *vio_bit)
{
	int port = 0, subport = 0;
	struct dmc_mon_comm *mon_comm = (struct dmc_mon_comm *)data;

	*vio_bit = DMC_VIO_PROT1 | DMC_VIO_PROT0;
	port = (mon_comm->status >> 18) & 0xf;
	subport = (mon_comm->status >> 8) & 0x3ff;
	if (port == 0xa) {
		subport = subport & 0xf;
		if (subport == 0x3)
			subport |= ((mon_comm->status >> 16) & 0xc);
	}

	mon_comm->port.name = to_ports(port);
	if (!mon_comm->port.name)
		sprintf(mon_comm->port.id, "%d", port);

	mon_comm->sub.name = to_sub_ports_name(port, subport, mon_comm->rw);
	if (!mon_comm->sub.name)
		sprintf(mon_comm->sub.id, "%d", subport);
}

static int s7_dmc_mon_set(struct dmc_monitor *mon)
{
	unsigned long start, end, value;
	void *io = dmc_mon->mon_comm[0].io_mem;

	/* aligned to 4KB */
	start = mon->addr_start >> PAGE_SHIFT;
	end   = ALIGN(mon->addr_end, PAGE_SIZE);
	end   = end >> PAGE_SHIFT;

	dmc_prot_rw(io, DMC_PROT_IRQ_CTRL_STS, 0x3, DMC_WRITE);

	dmc_prot_rw(io, DMC_PROT0_STA, start, DMC_WRITE);
	dmc_prot_rw(io, DMC_PROT0_EDA, end, DMC_WRITE);

	dmc_prot_rw(io, DMC_PROT0_CTRL, mon->device, DMC_WRITE);

	value = 0xffff;
	if (dmc_mon->debug & DMC_DEBUG_WRITE)
		value |= (1 << 24);
	/* if set, will be crash when read access */
	if (dmc_mon->debug & DMC_DEBUG_READ)
		value |= (1 << 26);
	dmc_prot_rw(io, DMC_PROT0_CTRL1, value, DMC_WRITE);

	if (dmc_mon->debug & DMC_DEBUG_SUSPEND)
		value = 0X3;
	else
		value = 0X7;
	dmc_prot_rw(io, DMC_PROT_IRQ_CTRL_STS, value, DMC_WRITE);

	pr_emerg("range:%08lx - %08lx, device:%llx\n",
		 mon->addr_start, mon->addr_end, mon->device);
	return 0;
}

void s7_dmc_mon_disable(struct dmc_monitor *mon)
{
	void *io = dmc_mon->mon_comm[0].io_mem;

	dmc_prot_rw(io, DMC_PROT0_STA,   0, DMC_WRITE);
	dmc_prot_rw(io, DMC_PROT0_EDA,   0, DMC_WRITE);
	dmc_prot_rw(io, DMC_PROT1_STA,   0, DMC_WRITE);
	dmc_prot_rw(io, DMC_PROT1_EDA,   0, DMC_WRITE);
	dmc_prot_rw(io, DMC_PROT0_CTRL,  0, DMC_WRITE);
	dmc_prot_rw(io, DMC_PROT0_CTRL1, 0, DMC_WRITE);
	dmc_prot_rw(io, DMC_PROT1_CTRL,  0, DMC_WRITE);
	dmc_prot_rw(io, DMC_PROT1_CTRL1, 0, DMC_WRITE);
	mon->device     = 0;
	mon->addr_start = 0;
	mon->addr_end   = 0;
}

static int s7_reg_analysis(char *input, char *output)
{
	unsigned long status, vio_reg0, vio_reg1, vio_reg2, vio_reg3;
	int port, subport, count = 0;
	unsigned long addr;
	char rw = 'n';

	if (sscanf(input, "%lx %lx %lx %lx %lx",
			 &status, &vio_reg0, &vio_reg1,
			 &vio_reg2, &vio_reg3) != 5) {
		pr_emerg("%s parma input error, buf:%s\n", __func__, input);
		return 0;
	}

	if (status & 0x1) { /* read */
		addr = vio_reg2;
		port = (vio_reg3 >> 18) & 0xf;
		subport = (vio_reg3 >> 8) & 0x3ff;
		if (port == 0xa) {
			subport = subport & 0xf;
			if (subport == 0x3)
				subport |= ((vio_reg3 >> 16) & 0xc);
		}
		rw = 'r';

		count += sprintf(output + count, "DMC READ:");
		count += sprintf(output + count, "addr=%09lx port=%s sub=%s\n",
				 addr, to_ports(port), to_sub_ports_name(port, subport, rw));
	}

	if (status & 0x2) { /* write */
		addr = vio_reg0;
		port = (vio_reg1 >> 18) & 0xf;
		subport = (vio_reg1 >> 8) & 0x3ff;
		if (port == 0xa) {
			subport = subport & 0xf;
			if (subport == 0x3)
				subport |= ((vio_reg1 >> 16) & 0xc);
		}
		rw = 'w';
		count += sprintf(output + count, "DMC WRITE:");
		count += sprintf(output + count, "addr=%09lx port=%s sub=%s\n",
				 addr, to_ports(port), to_sub_ports_name(port, subport, rw));
	}
	return count;
}

static int s7_dmc_reg_control(char *input, char control, char *output)
{
	int count = 0, i;
	unsigned long val;

	switch (control) {
	case 'a':	/* analysis sec vio reg */
		count = s7_reg_analysis(input, output);
		break;
	case 'c':	/* clear sec statue reg */
		dmc_prot_rw(NULL, DMC_SEC_STATUS, 0x3, DMC_WRITE);
		break;
	case 'd':	/* dump sec vio reg */
		count += sprintf(output + count, "DMC SEC INFO:\n");
		val = dmc_prot_rw(NULL, DMC_SEC_STATUS, 0, DMC_READ);
		count += sprintf(output + count, "DMC_SEC_STATUS:%lx\n", val);
		for (i = 0; i < 4; i++) {
			val = dmc_prot_rw(NULL, DMC_VIO_ADDR0 + (i << 2), 0, DMC_READ);
			count += sprintf(output + count, "DMC_VIO_ADDR%d:%lx\n", i, val);
		}
		break;
	default:
		break;
	}

	return count;
}

struct dmc_mon_ops s7_dmc_mon_ops = {
	.handle_irq  = s7_dmc_mon_irq,
	.vio_to_port = s7_dmc_vio_to_port,
	.set_monitor = s7_dmc_mon_set,
	.disable     = s7_dmc_mon_disable,
	.dump_reg    = s7_dmc_dump_reg,
	.reg_control = s7_dmc_reg_control,
};
