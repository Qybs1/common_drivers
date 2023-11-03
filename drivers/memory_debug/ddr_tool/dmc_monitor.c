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
#include <linux/of_device.h>
#include <linux/irqreturn.h>
#include <linux/module.h>
#include <linux/mm.h>

#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/kallsyms.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/amlogic/page_trace.h>
#include <linux/amlogic/cpu_version.h>
#include <linux/arm-smccc.h>
#include <linux/highmem.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include "dmc_monitor.h"
#include "ddr_port.h"
#include <linux/amlogic/gki_module.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/fault.h>
#include <asm/module.h>
#include <linux/mmzone.h>
#include <trace/hooks/traps.h>
#include <linux/amlogic/dmc_dev_access.h>

#define CREATE_TRACE_POINTS
#include "dmc_trace.h"

// #define DEBUG
struct dmc_monitor *dmc_mon;
struct black_dev_list black_dev;

static unsigned long init_dev_mask;
static unsigned long init_start_addr;
static unsigned long init_end_addr;
static unsigned long init_dmc_debug;

static bool dmc_reserved_flag;
static int dmc_original_debug;

static int early_dmc_param(char *buf)
{
	unsigned long s_addr, e_addr, mask, debug = 0;
	/*
	 * Patten:  dmc_monitor=[start_addr],[end_addr],[mask]
	 * Example: dmc_monitor=0x00000000,0x20000000,0x7fce
	 * debug: cat /sys/class/dmc_monitor/debug
	 */
	if (!buf)
		return -EINVAL;

	if (sscanf(buf, "%lx,%lx,%lx,%lx", &s_addr, &e_addr, &mask, &debug) != 4) {
		if (sscanf(buf, "%lx,%lx,%lx", &s_addr, &e_addr, &mask) != 3)
			return -EINVAL;
	}

	init_start_addr = s_addr;
	init_end_addr   = e_addr;
	init_dev_mask   = mask;
	init_dmc_debug = debug;

	pr_info("%s, buf:%s, %lx-%lx, %lx, %lx\n",
		__func__, buf, s_addr, e_addr, mask, debug);

	return 0;
}
__setup("dmc_monitor=", early_dmc_param);

static int early_dmc_dev_blacklist(char *buf)
{
	const char c = ',';
	int i = 0, off_s = 0, off_e = 0;

	while (i < strlen(buf)) {
		if (buf[i] == c) {
			off_e = i;
			memcpy(black_dev.device[black_dev.num++], buf + off_s, off_e - off_s);
			off_s = off_e + 1;
		}
		i++;
		if (black_dev.num >= 10)
			black_dev.num = 0;
	}
	if (off_s < strlen(buf))
		memcpy(black_dev.device[black_dev.num++], buf + off_s, strlen(buf) - off_s);

	pr_info("dmc_dev_blacklist, buf:%s\n", buf);

	return 0;
}
__setup("dmc_dev_blacklist=", early_dmc_dev_blacklist);

#if IS_BUILTIN(CONFIG_AMLOGIC_PAGE_TRACE)
#ifdef CONFIG_AMLOGIC_PAGE_TRACE_INLINE
struct page_trace *dmc_find_page_base(struct page *page)
{
	struct page_trace *trace;

	trace = (struct page_trace *)&page->trace;
	return trace;
}
#else
struct page_trace *dmc_trace_buffer;
static unsigned long _kernel_text;
static unsigned int dmc_trace_step;

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
static unsigned long module_alloc_base_dmc;
static int once_flag = 1;

void get_page_trace_buf_hook(void *data, unsigned int migratetype,
		bool *bypass)
{
	struct pagetrace_vendor_param *param;

	if (migratetype != 1024 || dmc_trace_buffer)
		return;

	param = (struct pagetrace_vendor_param *)bypass;
	dmc_trace_buffer = param->trace_buf;
	_kernel_text = param->text;
	dmc_trace_step = param->trace_step;
	module_alloc_base_dmc = param->ip;
	pr_info("dmc_trace_buf: %px, maddr:%lx\n",
		dmc_trace_buffer, module_alloc_base_dmc);
}
#endif

static struct pglist_data *next_online_pgdat_dmc(struct pglist_data *pgdat)
{
	int nid = next_online_node(pgdat->node_id);

	if (nid == MAX_NUMNODES)
		return NULL;
	/*
	 * This code is copy from upstream, please ignore the problem.
	 */
	/* coverity[dead_error_line:SUPPRESS] */
	return NODE_DATA(nid);
}

/*
 * next_zone - helper magic for for_each_zone()
 */
static struct zone *next_zone_dmc(struct zone *zone)
{
	pg_data_t *pgdat = zone->zone_pgdat;

	if (zone < pgdat->node_zones + MAX_NR_ZONES - 1) {
		zone++;
	} else {
		pgdat = next_online_pgdat_dmc(pgdat);
		if (pgdat)
			zone = pgdat->node_zones;
		else
			zone = NULL;
	}
	return zone;
}

struct page_trace *dmc_find_page_base(struct page *page)
{
	unsigned long pfn, zone_offset = 0, offset;
	struct zone *zone;
	struct page_trace *p;

	if (!dmc_trace_buffer)
		return NULL;

	pfn = page_to_pfn(page);
	for (zone = (NODE_DATA(first_online_node))->node_zones; zone;
			zone = next_zone_dmc(zone)) {
		if (!populated_zone(zone)) {
			; /* do nothing */
		} else {
			/* pfn is in this zone */
			if (pfn <= zone_end_pfn(zone) &&
				pfn >= zone->zone_start_pfn) {
				offset = pfn - zone->zone_start_pfn;
				p = dmc_trace_buffer;
				return p + ((offset + zone_offset) * dmc_trace_step);
			}
			/* next zone */
			zone_offset += zone->spanned_pages;
		}
	}
	return NULL;
}
#endif

static unsigned long dmc_unpack_ip(struct page_trace *trace)
{
	unsigned long text;

	if (trace->order == IP_INVALID)
		return 0;

	if (trace->module_flag)
#ifdef CONFIG_RANDOMIZE_BASE
		text = module_alloc_base_dmc;
#else
		text = MODULES_VADDR;
#endif
	else
		text = (unsigned long)_kernel_text;
	return text + ((trace->ret_ip) << 2);
}

unsigned long dmc_get_page_trace(struct page *page)
{
	struct page_trace *trace;

	trace = dmc_find_page_base(page);
	if (trace)
		return dmc_unpack_ip(trace);

	return 0;
}
#elif IS_MODULE(CONFIG_AMLOGIC_PAGE_TRACE)
struct page_trace *dmc_find_page_base(struct page *page)
{
	return find_page_base(page);
}

unsigned long dmc_get_page_trace(struct page *page)
{
	return get_page_trace(page);
}
#endif

unsigned long read_violation_mem(unsigned long addr, char rw)
{
	struct page *page;
	unsigned long *p, *q;
	unsigned long val;

#if defined(CONFIG_ARM64) || IS_ENABLED(CONFIG_AMLOGIC_DMC_MONITOR_BREAK_GKI)
	if (!pfn_is_map_memory(__phys_to_pfn(addr)))
#else
	if (!pfn_valid(__phys_to_pfn(addr)))
#endif
		return 0xdeaddead;

	if (rw == 'r')
		return 0x0;

	page = phys_to_page(addr);
	p = kmap_atomic(page);

	if (!p)
		return 0xdeaddead;

	q = p + ((addr & (PAGE_SIZE - 1)) / sizeof(*p));
	val = *q;

	kunmap_atomic(p);

	return val;
}

static void get_page_flags(struct page *page, char *buf)
{
	sprintf(buf, "bd:%d sb:%d lru:%d", PageBuddy(page), PageSlab(page), PageLRU(page));
}

void show_violation_mem_printk(char *title, unsigned long addr, unsigned long status,
				int port, int sub_port, char rw)
{
	struct page *page;
	char buffer[32] = {0};

	page = phys_to_page(addr);
	get_page_flags(page, buffer);

	pr_crit(DMC_TAG "%s addr=%09lx val=%016lx s=%08lx port=%s sub=%s rw:%c %s a:%ps\n",
		title, addr, read_violation_mem(addr, rw),
		status, to_ports(port),
		to_sub_ports_name(port, sub_port, rw),
		rw, buffer, (void *)dmc_get_page_trace(page));
}

void show_violation_mem_trace_event(unsigned long addr, unsigned long status,
				    int port, int sub_port, char rw)
{
	trace_dmc_violation(addr, status, port, sub_port, rw);
}

unsigned long dmc_prot_rw(void  __iomem *base,
			  unsigned long off, unsigned long value, int rw)
{
	if (base) {
		if (rw == DMC_WRITE) {
			writel(value, base + off);
			return 0;
		} else {
			return readl(base + off);
		}
	} else {
		return dmc_rw(off + dmc_mon->io_base, value, rw);
	}
}

static inline int dmc_dev_is_byte(struct dmc_monitor *mon)
{
	return mon->configs & DMC_DEVICE_8BIT;
}

static int dev_name_to_id(const char *dev_name)
{
	int i, len;

	for (i = 0; i < dmc_mon->port_num; i++) {
		if (dmc_mon->port[i].port_id >= PORT_MAJOR &&
		    !dmc_dev_is_byte(dmc_mon))
			return -1;
		len = strlen(dmc_mon->port[i].port_name);
		if (!strncmp(dmc_mon->port[i].port_name, dev_name, len))
			break;
	}
	if (i >= dmc_mon->port_num)
		return -1;
	return dmc_mon->port[i].port_id;
}

char *to_ports(int id)
{
	static char *name;
	static int last_id = -1;
	int i;

	if (id == last_id)
		return name;

	for (i = 0; i < dmc_mon->port_num; i++) {
		if (dmc_mon->port[i].port_id == id) {
			name =  dmc_mon->port[i].port_name;
			last_id = id;
			return name;
		}
	}
	return NULL;
}

char *to_sub_ports_name(int mid, int sid, char rw)
{
	static char id_str[MAX_NAME];
	static int last_mid, last_sid;
	static char *name;
	int i, s_port = 0;

	if (mid == last_mid && sid == last_sid)
		return name;

	if (strstr(to_ports(mid), "VPU")) {
		name = vpu_to_sub_port(to_ports(mid), rw, sid, id_str);
	} else if (strstr(to_ports(mid), "DEVICE")) {
		if (strstr(to_ports(mid), "DEVICE1"))
			s_port = sid + PORT_MAJOR + 8;
		else
			s_port = sid + PORT_MAJOR;

		for (i = 0; i < dmc_mon->port_num; i++) {
			if (dmc_mon->port[i].port_id == s_port) {
				name =  dmc_mon->port[i].port_name;
				break;
			}
		}
	} else {
		sprintf(id_str, "%2d", sid);
		name = id_str;
	}

	return name;
}

unsigned int get_all_dev_mask(void)
{
	unsigned int ret = 0;
	int i;

	if (dmc_dev_is_byte(dmc_mon))	/* not supported */
		return 0;

	for (i = 0; i < PORT_MAJOR; i++) {
		if (dmc_mon->port[i].port_id >= PORT_MAJOR)
			break;
		ret |= (1 << dmc_mon->port[i].port_id);
	}
	return ret;
}

static unsigned int get_other_dev_mask(void)
{
	unsigned int ret = 0;
	int i;

	for (i = 0; i < PORT_MAJOR; i++) {
		if (dmc_mon->port[i].port_id >= PORT_MAJOR)
			break;

		/*
		 * we don't want id with arm mali and device
		 * because these devices can access all ddr range
		 * and generate value-less report
		 */
		if (strstr(dmc_mon->port[i].port_name, "ARM")  ||
		    strstr(dmc_mon->port[i].port_name, "MALI") ||
		    strstr(dmc_mon->port[i].port_name, "DEVICE") ||
		    strstr(dmc_mon->port[i].port_name, "USB") ||
		    strstr(dmc_mon->port[i].port_name, "ETH") ||
		    strstr(dmc_mon->port[i].port_name, "EMMC"))
			continue;

		ret |= (1 << dmc_mon->port[i].port_id);
	}
	return ret;
}

int dmc_violation_ignore(char *title, unsigned long addr, unsigned long status,
				int port, int subport, char rw)
{
	int is_ignore = 0, i;
	struct page *page;
	struct page_trace *trace;

	/* ignore cma driver pages */
	page = phys_to_page(addr);
	trace = dmc_find_page_base(page);
	if (trace && trace->order != IP_INVALID && trace->migrate_type == MIGRATE_CMA) {
		if (dmc_mon->debug & DMC_DEBUG_CMA) {
			sprintf(title, "%s", "_CMA");
			is_ignore = 0;
		} else {
			is_ignore = 1;
		}
	}

	/* ignore violation on same page/same port */
	if ((addr & PAGE_MASK) == dmc_mon->last_addr &&
	     status == dmc_mon->last_status &&
	     trace && trace->ip_data == dmc_mon->last_trace) {
		if (dmc_mon->debug & DMC_DEBUG_SAME) {
			sprintf(title, "%s", "_SAME");
			is_ignore = 0;
		} else {
			is_ignore = 1;
		}
	}

	/* only console print filter */
	if (!(dmc_mon->debug & DMC_DEBUG_TRACE)) {
		for (i = 0; i < black_dev.num; i++) {
			if (strstr(to_ports(port), black_dev.device[i])) {
				is_ignore = 1;
				break;
			} else if (strstr(to_sub_ports_name(port, subport, rw),
								black_dev.device[i])) {
				is_ignore = 1;
				break;
			}
		}
	}

	dmc_mon->last_trace = trace ? trace->ip_data : 0xdeaddead;
	dmc_mon->last_addr = addr & PAGE_MASK;
	dmc_mon->last_status = status;

	return is_ignore;
}

static void dmc_set_default(struct dmc_monitor *mon)
{
	if (dmc_dev_is_byte(mon)) {
		strcpy(black_dev.device[black_dev.num++], "USB");
		strcpy(black_dev.device[black_dev.num++], "ETH");
		strcpy(black_dev.device[black_dev.num++], "EMMC");

		switch (mon->chip) {
	#ifdef CONFIG_AMLOGIC_DMC_MONITOR_T7
		case DMC_TYPE_T7:
			mon->device = 0x02030405;
			mon->debug &= ~DMC_DEBUG_INCLUDE;
			break;
		case DMC_TYPE_T3:
			mon->device = 0x02041c11;
			mon->debug &= ~DMC_DEBUG_INCLUDE;
			break;
		case DMC_TYPE_P1:
			mon->device = 0x02324c52;
			mon->debug &= ~DMC_DEBUG_INCLUDE;
			break;
	#endif
	#ifdef CONFIG_AMLOGIC_DMC_MONITOR_C3
		case DMC_TYPE_C3:
			mon->device = 0x02495051;
			mon->debug &= ~DMC_DEBUG_INCLUDE;
			break;
	#endif
	#ifdef CONFIG_AMLOGIC_DMC_MONITOR_T5M
		case DMC_TYPE_T5M:
			mon->device = 0x0204253a;
			mon->debug &= ~DMC_DEBUG_INCLUDE;
			break;
	#endif
	#ifdef CONFIG_AMLOGIC_DMC_MONITOR_S5
		case DMC_TYPE_S5:
			mon->device = 0x02040809;
			mon->debug &= ~DMC_DEBUG_INCLUDE;
			break;
		case DMC_TYPE_T3X:
			mon->device = 0x02040809;
			mon->debug &= ~DMC_DEBUG_INCLUDE;
			break;
	#endif
		}
	} else {
		mon->device = get_other_dev_mask();
	}

	pr_emerg("set dmc default: device=%llx, debug=%x\n", mon->device, mon->debug);

	if (dmc_mon->addr_start < dmc_mon->addr_end && dmc_mon->ops &&
	     dmc_mon->ops->set_monitor)
		dmc_mon->ops->set_monitor(dmc_mon);
}

static void dmc_clear_reserved_memory(struct dmc_monitor *mon)
{
	if (dmc_reserved_flag) {
		mon->debug = dmc_original_debug;
		mon->device = 0;
		mon->addr_start = 0;
		mon->addr_end = 0;
		dmc_reserved_flag = 0;
	}
}

static void dmc_enabled_reserved_memory(struct platform_device *pdev, struct dmc_monitor *mon)
{
	struct device_node *node;
	struct reserved_mem *rmem;

	node = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!node)
		return;

	rmem = of_reserved_mem_lookup(node);
	if (!rmem) {
		pr_info("no such reserved mem of dmc_monitor\n");
		return;
	}

	if (!rmem->size)
		return;

	mon->addr_start = rmem->base;
	mon->addr_end = rmem->base + rmem->size - 1;

	dmc_original_debug = mon->debug;

	if (dmc_dev_is_byte(mon)) {
		mon->device = 0x0201;
		mon->debug &= ~DMC_DEBUG_INCLUDE;
	} else {
		mon->device = get_all_dev_mask();
#ifdef CONFIG_ARM64
		mon->device &= 0xfffffffffffffffe;
#endif
	}

	mon->debug &= ~DMC_DEBUG_TRACE;
	mon->debug |= DMC_DEBUG_CMA;
	mon->debug |= DMC_DEBUG_SAME;
	dmc_reserved_flag = 1;

	if (mon->addr_start < mon->addr_end && mon->ops && mon->ops->set_monitor) {
		pr_emerg("DMC DEBUG MEMORY ENABLED: range:%lx-%lx, device=%llx, debug=%x\n",
				mon->addr_start, mon->addr_end, mon->device, mon->debug);
		mon->ops->set_monitor(mon);
	}
}

size_t dump_dmc_reg(char *buf)
{
	size_t sz = 0, i;
	unsigned char dev;
	u64 devices;

	if (dmc_mon->ops && dmc_mon->ops->dump_reg)
		sz += dmc_mon->ops->dump_reg(buf);
	sz += sprintf(buf + sz, "IO_BASE:0x%lx\n", dmc_mon->io_base);
	sz += sprintf(buf + sz, "DMC_DEBUG:0x%x\n", dmc_mon->debug);
	sz += sprintf(buf + sz, "RANGE:0x%lx - 0x%lx\n",
		      dmc_mon->addr_start, dmc_mon->addr_end);
	sz += sprintf(buf + sz, "MONITOR DEVICE(%s):\n",
		dmc_mon->debug & DMC_DEBUG_INCLUDE ? "include" : "exclude");
	if (!dmc_mon->device)
		return sz;

	if (dmc_dev_is_byte(dmc_mon)) {
		devices = dmc_mon->device;
		for (i = 0; i < sizeof(dmc_mon->device); i++) {
			dev = devices & 0xff;
			devices >>= 8ULL;
			if (dev)
				sz += sprintf(buf + sz, "    %s\n", to_ports(dev));
		}
	} else {
		for (i = 0; i < sizeof(dmc_mon->device) * 8; i++) {
			if (dmc_mon->device & (1ULL << i))
				sz += sprintf(buf + sz, "    %s\n", to_ports(i));
		}
	}

	return sz;
}

static size_t str_end_char(const char *s, size_t count, int c)
{
	size_t len = count, offset = count;

	while (count--) {
		if (*s == (char)c)
			offset = len - count;
		if (*s++ == '\0') {
			offset = len - count;
			break;
		}
	}
	return offset;
}

static void serror_dump_dmc_reg(void)
{
	int len = 0, i = 0, offset = 0;
	static char buf[2048] = {0};

	if (dmc_mon->ops && dmc_mon->ops->reg_control) {
		len = dmc_mon->ops->reg_control(NULL, 'd', buf);
		while (i < len) {
			offset = str_end_char(buf + i, 512, '\n');
			if (offset > 1)
				pr_emerg("%.*s", offset, buf + i);
			i += offset;
		}
		pr_emerg("\n");
	}
}

static irqreturn_t __nocfi dmc_monitor_irq_handler(int irq, void *dev_instance)
{
	if (dmc_mon->ops && dmc_mon->ops->handle_irq)
		dmc_mon->ops->handle_irq(dmc_mon, dev_instance);

	return IRQ_HANDLED;
}

static int dmc_irq_set(struct device_node *node, int save_irq, int request)
{
	static int irq[4], save_irq_num, request_num;
	int r = 0, i;

	pr_info("%s: save_irq=%d, request=%d\n", __func__, save_irq, request);
	if (!save_irq && !save_irq_num)
		return -EINVAL;
	if (!save_irq && ((request && request_num) || (!request && !request_num)))
		return -EINVAL;

	for (i = 0; i < dmc_mon->mon_number; i++) {
		if (save_irq) {
			irq[i] = of_irq_get(node, i);
			save_irq_num++;
			if (!request)
				continue;
		}

		if (request) {
			switch (i) {
			case 0:
				if (dmc_mon->io_mem1)
					r = request_irq(irq[i], dmc_monitor_irq_handler,
							IRQF_SHARED, "dmc_monitor",
							dmc_mon->io_mem1);
				else
					r = request_irq(irq[i], dmc_monitor_irq_handler,
							IRQF_SHARED, "dmc_monitor", dmc_mon);
				break;
			case 1:
				r = request_irq(irq[i], dmc_monitor_irq_handler,
						IRQF_SHARED, "dmc_monitor", dmc_mon->io_mem2);
				break;
			case 2:
				r = request_irq(irq[i], dmc_monitor_irq_handler,
						IRQF_SHARED, "dmc_monitor", dmc_mon->io_mem3);
				break;
			case 3:
				r = request_irq(irq[i], dmc_monitor_irq_handler,
						IRQF_SHARED, "dmc_monitor", dmc_mon->io_mem4);
				break;
			default:
				break;
			}
			if (r < 0) {
				pr_err("request irq failed:%d, r:%d\n", irq[i], r);
				request_num = 0;
				dmc_mon = NULL;
				return -EINVAL;
			}
			request_num++;
		} else {
			switch (i) {
			case 0:
				if (dmc_mon->io_mem1)
					free_irq(irq[i], dmc_mon->io_mem1);
				else
					free_irq(irq[i], dmc_mon);
				break;
			case 1:
				free_irq(irq[i], dmc_mon->io_mem2);
				break;
			case 2:
				free_irq(irq[i], dmc_mon->io_mem3);
				break;
			case 3:
				free_irq(irq[i], dmc_mon->io_mem4);
				break;
			default:
				break;
			}
			request_num--;
		}
	}
	return 0;
}

static int dmc_regulation_dev(unsigned long dev, int add)
{
	unsigned char *p, cur;
	int i, set;

	if (dmc_dev_is_byte(dmc_mon)) {
		/* dev is a set of 8 bit user id index */
		while (dev) {
			cur = dev & 0xff;
			set = 0;
			p   = (unsigned char *)&dmc_mon->device;
			for (i = 0; i < sizeof(dmc_mon->device); i++) {
				if (p[i] == cur) {	/* already set */
					if (add)
						set = 1;
					else		/* clear it */
						p[i] = 0;
					break;
				}

				if (p[i] == 0 && add) {	/* find empty one */
					p[i] = (dev & 0xff);
					set = 1;
					break;
				}
			}
			if (i == sizeof(dmc_mon->device) && !set && add) {
				pr_err("%s, monitor device full\n", __func__);
				return -EINVAL;
			}
			dev >>= 8;
		}
	} else {
		if (add) /* dev is bit mask */
			dmc_mon->device |= dev;
		else
			dmc_mon->device &= ~(dev);
	}
	return 0;
}

int dmc_set_monitor(unsigned long start, unsigned long end,
		    unsigned long dev_mask, int en)
{
	if (!dmc_mon)
		return -EINVAL;

	dmc_clear_reserved_memory(dmc_mon);
	dmc_mon->addr_start = start;
	dmc_mon->addr_end   = end;
	dmc_regulation_dev(dev_mask, en);
	if (start < end && dmc_mon->ops && dmc_mon->ops->set_monitor)
		return dmc_mon->ops->set_monitor(dmc_mon);
	return -EINVAL;
}
EXPORT_SYMBOL(dmc_set_monitor);

int dmc_set_monitor_by_name(unsigned long start, unsigned long end,
			    const char *port_name, int en)
{
	long id;

	id = dev_name_to_id(port_name);
	if (id >= 0 && dmc_dev_is_byte(dmc_mon))
		return dmc_set_monitor(start, end, id, en);
	else if (id < 0 || id >= BITS_PER_LONG)
		return -EINVAL;
	return dmc_set_monitor(start, end, 1UL << id, en);
}
EXPORT_SYMBOL(dmc_set_monitor_by_name);

void dmc_monitor_disable(void)
{
	if (dmc_mon->ops && dmc_mon->ops->disable)
		return dmc_mon->ops->disable(dmc_mon);
}
EXPORT_SYMBOL(dmc_monitor_disable);

static ssize_t range_show(struct class *cla,
			  struct class_attribute *attr, char *buf)
{
	if (!dmc_mon->ops) {
		pr_err("Can't find ops for chip:%x\n", dmc_mon->chip);
		return 0;
	}

	return sprintf(buf, "%08lx - %08lx\n",
		       dmc_mon->addr_start, dmc_mon->addr_end);
}

static ssize_t range_store(struct class *cla,
			   struct class_attribute *attr,
			   const char *buf, size_t count)
{
	int ret;
	unsigned long start, end;

	if (!dmc_mon->ops) {
		pr_err("Can't find ops for chip:%x\n", dmc_mon->chip);
		return count;
	}

	ret = sscanf(buf, "%lx %lx", &start, &end);
	if (ret != 2) {
		pr_info("%s, bad input:%s\n", __func__, buf);
		return count;
	}
	dmc_set_monitor(start, end, dmc_mon->device, 1);
	return count;
}
static CLASS_ATTR_RW(range);

static ssize_t device_store(struct class *cla,
			    struct class_attribute *attr,
			    const char *buf, size_t count)
{
	int i;

	if (!dmc_mon->ops) {
		pr_err("Can't find ops for chip:%x\n", dmc_mon->chip);
		return count;
	}

	dmc_clear_reserved_memory(dmc_mon);

	if (!strncmp(buf, "none", 4)) {
		dmc_monitor_disable();
		return count;
	}

	if (!strncmp(buf, "default", 7)) {
		dmc_set_default(dmc_mon);
		return count;
	}

	if (dmc_dev_is_byte(dmc_mon)) {
		i = dev_name_to_id(buf);
		if (i < 0) {
			pr_info("bad device:%s\n", buf);
			return -EINVAL;
		}
		if (dmc_regulation_dev(i, 1))
			return -EINVAL;
	} else {
		if (!strncmp(buf, "all", 3)) {
			dmc_mon->device = get_all_dev_mask();
		} else {
			i = dev_name_to_id(buf);
			if (i < 0) {
				pr_info("bad device:%s\n", buf);
				return -EINVAL;
			}
			dmc_regulation_dev(1UL << i, 1);
		}
	}
	if (dmc_mon->addr_start < dmc_mon->addr_end && dmc_mon->ops &&
	     dmc_mon->ops->set_monitor)
		dmc_mon->ops->set_monitor(dmc_mon);

	return count;
}

static ssize_t device_show(struct class *cla,
			   struct class_attribute *attr, char *buf)
{
	int i, s = 0;

	if (!dmc_mon->ops) {
		pr_err("Can't find ops for chip:%x\n", dmc_mon->chip);
		return 0;
	}

	s += sprintf(buf + s, "supported device:\n");
	for (i = 0; i < dmc_mon->port_num; i++) {
		if (dmc_mon->port[i].port_id >= PORT_MAJOR &&
		   !dmc_dev_is_byte(dmc_mon))
			break;
		s += sprintf(buf + s, "%2d : %s\n",
			dmc_mon->port[i].port_id,
			dmc_mon->port[i].port_name);
	}
	return s;
}
static CLASS_ATTR_RW(device);

static ssize_t dump_show(struct class *cla,
			 struct class_attribute *attr, char *buf)
{
#if (IS_BUILTIN(CONFIG_AMLOGIC_PAGE_TRACE)		&& \
	!IS_ENABLED(CONFIG_AMLOGIC_PAGE_TRACE_INLINE)	&& \
	IS_ENABLED(CONFIG_TRACEPOINTS)			&& \
	IS_ENABLED(CONFIG_ANDROID_VENDOR_HOOKS))
	if (once_flag && dmc_trace_buffer) {
		pr_info("%s, %d: got pagetrace buffer.\n", __func__, __LINE__);
		unregister_trace_android_vh_cma_drain_all_pages_bypass(get_page_trace_buf_hook,
				NULL);
		once_flag = 0;
	}
#endif
	return dump_dmc_reg(buf);
}
static CLASS_ATTR_RO(dump);

static ssize_t debug_store(struct class *cla,
			  struct class_attribute *attr,
			  const char *buf, size_t count)
{
	int val;
	char string[128];

	if (sscanf(buf, "%s %d", string, &val) != 2) {
		pr_info("invalid input:%s\n", buf);
		return count;
	}

	if (strstr(string, "write")) {
		if (val)
			dmc_mon->debug |= DMC_DEBUG_WRITE;
		else
			dmc_mon->debug &= ~DMC_DEBUG_WRITE;
	} else if (strstr(string, "read")) {
		if (val)
			dmc_mon->debug |= DMC_DEBUG_READ;
		else
			dmc_mon->debug &= ~DMC_DEBUG_READ;
	} else if (strstr(string, "cma")) {
		if (val)
			dmc_mon->debug |= DMC_DEBUG_CMA;
		else
			dmc_mon->debug &= ~DMC_DEBUG_CMA;
	} else if (strstr(string, "same")) {
		if (val)
			dmc_mon->debug |= DMC_DEBUG_SAME;
		else
			dmc_mon->debug &= ~DMC_DEBUG_SAME;
	} else if (strstr(string, "include")) {
		if (dmc_dev_is_byte(dmc_mon)) {
			if (val)
				dmc_mon->debug |= DMC_DEBUG_INCLUDE;
			else
				dmc_mon->debug &= ~DMC_DEBUG_INCLUDE;
		} else {
			pr_emerg("dmc not support include set\n");
		}
	} else if (strstr(string, "trace")) {
		if (val)
			dmc_mon->debug |= DMC_DEBUG_TRACE;
		else
			dmc_mon->debug &= ~DMC_DEBUG_TRACE;
	} else if (strstr(string, "suspend")) {
		if (val) {
			dmc_mon->debug |= DMC_DEBUG_SUSPEND;
			if (dmc_irq_set(NULL, 0, 0) < 0)
				pr_emerg("free dmc irq error\n");
		} else {
			dmc_mon->debug &= ~DMC_DEBUG_SUSPEND;
			if (dmc_irq_set(NULL, 0, 1) < 0)
				pr_emerg("request dmc irq error\n");
		}

	} else if (strstr(string, "serror")) {
		if (val) {
			dmc_mon->debug |= DMC_DEBUG_SERROR;
			if (dmc_mon->ops && dmc_mon->ops->reg_control)
				dmc_mon->ops->reg_control(NULL, 'c', NULL);
			serror_dump_dmc_reg();
		} else {
			dmc_mon->debug &= ~DMC_DEBUG_SERROR;
		}
	} else {
		pr_info("invalid param name:%s\n", buf);
		return count;
	}

	dmc_set_monitor(dmc_mon->addr_start, dmc_mon->addr_end, dmc_mon->device, 1);

	return count;
}

static ssize_t debug_show(struct class *cla,
			 struct class_attribute *attr, char *buf)
{
	int s = 0;

	s += sprintf(buf + s, "debug value: 0x%02x\n", dmc_mon->debug);
	s += sprintf(buf + s, "write:   write monitor:%d\n",
			dmc_mon->debug & DMC_DEBUG_WRITE ? 1 : 0);
	s += sprintf(buf + s, "read:    read monitor :%d\n",
			dmc_mon->debug & DMC_DEBUG_READ ? 1 : 0);
	s += sprintf(buf + s, "cma:     cma access show :%d\n",
			dmc_mon->debug & DMC_DEBUG_CMA ? 1 : 0);
	s += sprintf(buf + s, "same:    same access show :%d\n",
			dmc_mon->debug & DMC_DEBUG_SAME ? 1 : 0);
	s += sprintf(buf + s, "include: include mode :%d\n",
			dmc_mon->debug & DMC_DEBUG_INCLUDE ? 1 : 0);
	s += sprintf(buf + s, "trace:   trace print :%d\n",
			dmc_mon->debug & DMC_DEBUG_TRACE ? 1 : 0);
	s += sprintf(buf + s, "suspend: suspend debug :%d\n",
			dmc_mon->debug & DMC_DEBUG_SUSPEND ? 1 : 0);
	s += sprintf(buf + s, "serror:  serror debug :%d\n",
			dmc_mon->debug & DMC_DEBUG_SERROR ? 1 : 0);

	return s;
}
static CLASS_ATTR_RW(debug);

#if IS_ENABLED(CONFIG_AMLOGIC_DMC_DEV_ACCESS)
static ssize_t dev_access_store(struct class *cla,
			  struct class_attribute *attr,
			  const char *buf, size_t count)
{
	int ret;
	unsigned long addr = 0, size = 0;
	int id = 0;

	ret = sscanf(buf, "%d %lx %lx", &id, &addr, &size);
	if (ret != 3) {
		pr_info("input param num should be 3 (id addr size)\n");
		return count;
	}
	dmc_dev_access(id, addr, size);

	return count;
}

static ssize_t dev_access_show(struct class *cla,
			 struct class_attribute *attr, char *buf)
{
	return show_dmc_notifier_list(buf);
}
static CLASS_ATTR_RW(dev_access);
#endif

static ssize_t cmdline_show(struct class *cla,
			 struct class_attribute *attr, char *buf)
{
	int count = 0;
	int i = 0;

	count += sprintf(buf + count,
			 "setenv initargs $initargs dmc_monitor=0x%lx,0x%lx,0x%llx,0x%x",
			 dmc_mon->addr_start, dmc_mon->addr_end, dmc_mon->device, dmc_mon->debug);
	if (black_dev.num) {
		count += sprintf(buf + count, " dmc_dev_blacklist=%s", black_dev.device[0]);
		for (i = 1; i < black_dev.num; i++)
			count += sprintf(buf + count, ",%s", black_dev.device[i]);
	}
	count += sprintf(buf + count, ";saveenv;reset;\n");
	return count;
}
static CLASS_ATTR_RO(cmdline);

static ssize_t dev_blacklist_store(struct class *cla,
			  struct class_attribute *attr,
			  const char *buf, size_t count)
{
	if (!strncmp(buf, "none", 4))
		memset(&black_dev, 0, sizeof(black_dev));
	else
		early_dmc_dev_blacklist((char *)buf);

	return count;
}

static ssize_t dev_blacklist_show(struct class *cla,
			 struct class_attribute *attr, char *buf)
{
	unsigned int count = 0;
	int i;

	count += sprintf(buf + count, "dev black list:\n");
	for (i = 0; i < black_dev.num; i++)
		count += sprintf(buf + count, "%s\n", black_dev.device[i]);

	return count;
}
static CLASS_ATTR_RW(dev_blacklist);

static ssize_t reg_analysis_store(struct class *cla,
			  struct class_attribute *attr,
			  const char *buf, size_t count)
{
	char info[1024];
	int ret = 0;

	if (dmc_mon->ops && dmc_mon->ops->reg_control)
		ret = dmc_mon->ops->reg_control((char *)buf, 'a', info);
	if (ret)
		pr_emerg("%s", info);

	return count;
}
static CLASS_ATTR_WO(reg_analysis);

static struct attribute *dmc_monitor_attrs[] = {
	&class_attr_range.attr,
	&class_attr_device.attr,
	&class_attr_dump.attr,
	&class_attr_debug.attr,
	&class_attr_cmdline.attr,
	&class_attr_dev_blacklist.attr,
	&class_attr_reg_analysis.attr,
#if IS_ENABLED(CONFIG_AMLOGIC_DMC_DEV_ACCESS)
	&class_attr_dev_access.attr,
#endif
	NULL
};
ATTRIBUTE_GROUPS(dmc_monitor);

static struct class dmc_monitor_class = {
	.name = "dmc_monitor",
	.class_groups = dmc_monitor_groups,
};

static void __init get_dmc_ops(int chip, struct dmc_monitor *mon)
{
	/* set default parameters */
	mon->mon_number = 1;
	mon->debug |= DMC_DEBUG_INCLUDE;
	mon->debug |= DMC_DEBUG_WRITE;
#if (IS_ENABLED(CONFIG_EVENT_TRACING) && !IS_ENABLED(CONFIG_AMLOGIC_ZAPPER_CUT))
	mon->debug |= DMC_DEBUG_TRACE;
#endif

	switch (chip) {
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_G12
	case DMC_TYPE_G12A:
	case DMC_TYPE_G12B:
	case DMC_TYPE_SM1:
	case DMC_TYPE_TL1:
		mon->ops = &g12_dmc_mon_ops;
		break;
#endif
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_C1
	case DMC_TYPE_C1:
		mon->ops = &c1_dmc_mon_ops;
		break;
#endif
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_C2
	case DMC_TYPE_C2:
		mon->ops = &c2_dmc_mon_ops;
		break;
#endif
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_GX
	case DMC_TYPE_GXBB:
	case DMC_TYPE_GXTVBB:
	case DMC_TYPE_GXL:
	case DMC_TYPE_GXM:
	case DMC_TYPE_TXL:
	case DMC_TYPE_TXLX:
	case DMC_TYPE_AXG:
	case DMC_TYPE_GXLX:
	case DMC_TYPE_TXHD:
		mon->ops = &gx_dmc_mon_ops;
		break;
#endif
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_TM2
	case DMC_TYPE_TM2:
	/* cpuinfo build module not used is_meson_rev_b() */
	#ifdef CONFIG_AMLOGIC_CPU_INFO
		if (is_meson_rev_b())
			mon->ops = &tm2_dmc_mon_ops;
		else
	#endif
		#ifdef CONFIG_AMLOGIC_DMC_MONITOR_G12
			mon->ops = &g12_dmc_mon_ops;
		#else
			#error need support for revA
		#endif
		break;

	case DMC_TYPE_SC2:
		mon->ops = &tm2_dmc_mon_ops;
		break;

	case DMC_TYPE_T5:
	case DMC_TYPE_T5D:
		mon->ops = &tm2_dmc_mon_ops;
		break;
#endif
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_T7
	case DMC_TYPE_T7:
	case DMC_TYPE_T3:
		mon->ops = &t7_dmc_mon_ops;
		mon->configs |= DMC_DEVICE_8BIT;
		mon->mon_number = 2;
		break;
	case DMC_TYPE_P1:
		mon->ops = &t7_dmc_mon_ops;
		mon->configs |= DMC_DEVICE_8BIT;
		mon->mon_number = 4;
		break;
#endif
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_S4
	case DMC_TYPE_S4:
	case DMC_TYPE_T5W:
		mon->ops = &s4_dmc_mon_ops;
		break;
#endif
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_C3
	case DMC_TYPE_C3:
		mon->ops = &c3_dmc_mon_ops;
		mon->configs |= DMC_DEVICE_8BIT;
		break;
#endif
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_T5M
	case DMC_TYPE_T5M:
		mon->ops = &t5m_dmc_mon_ops;
		mon->configs |= DMC_DEVICE_8BIT;
		mon->mon_number = 2;
		break;
#endif
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_S5
	case DMC_TYPE_S5:
		mon->ops = &s5_dmc_mon_ops;
		mon->configs |= DMC_DEVICE_8BIT;
		mon->mon_number = 4;
		break;
	case DMC_TYPE_T3X:
		mon->ops = &s5_dmc_mon_ops;
		mon->configs |= DMC_DEVICE_8BIT;
		mon->mon_number = 2;
		break;
#endif
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_TXHD2
	case DMC_TYPE_TXHD2:
		mon->ops = &txhd2_dmc_mon_ops;
		break;
#endif
#ifdef CONFIG_AMLOGIC_DMC_MONITOR_S1A
	case DMC_TYPE_S1A:
		mon->ops = &s1a_dmc_mon_ops;
		break;
#endif
	default:
		pr_err("%s, Can't find ops for chip:%x\n", __func__, chip);
		break;
	}
}

#if defined(CONFIG_AMLOGIC_USER_FAULT) && \
	defined(CONFIG_TRACEPOINTS) && \
	defined(CONFIG_ANDROID_VENDOR_HOOKS)

#if CONFIG_AMLOGIC_KERNEL_VERSION >= 14515
/* Asynchronous Serror*/
static void arm64_serror_panic(void *data, struct pt_regs *regs, unsigned int esr)
{
	serror_dump_dmc_reg();
}

/* Synchronous Serror*/
static void do_sea(void *data, unsigned long addr, unsigned int esr, struct pt_regs *regs)
{
	serror_dump_dmc_reg();
}
#else
static void do_serror(void *data, struct pt_regs *regs, unsigned int esr, int *ret)
{
	serror_dump_dmc_reg();
}
#endif
#endif

static int __init dmc_monitor_probe(struct platform_device *pdev)
{
	int r = 0, ports, vpu_ports, i;
	unsigned int tmp;
	struct device_node *node;
	struct ddr_port_desc *desc = NULL;
	struct vpu_sub_desc *vpu_desc = NULL;
	struct resource *res;

#if (IS_BUILTIN(CONFIG_AMLOGIC_PAGE_TRACE)		&& \
	!IS_ENABLED(CONFIG_AMLOGIC_PAGE_TRACE_INLINE)	&& \
	IS_ENABLED(CONFIG_TRACEPOINTS)			&& \
	IS_ENABLED(CONFIG_ANDROID_VENDOR_HOOKS))
	if (!dmc_trace_buffer)
		register_trace_android_vh_cma_drain_all_pages_bypass(get_page_trace_buf_hook, NULL);
#endif
	pr_debug("%s, %d\n", __func__, __LINE__);
	dmc_mon = devm_kzalloc(&pdev->dev, sizeof(*dmc_mon), GFP_KERNEL);
	if (!dmc_mon)
		return -ENOMEM;

	tmp = (unsigned long)of_device_get_match_data(&pdev->dev);
	pr_debug("%s, chip type:%d\n", __func__, tmp);
	ports = ddr_find_port_desc(tmp, &desc);
	if (ports < 0) {
		pr_err("can't get port desc\n");
		dmc_mon = NULL;
		return -EINVAL;
	}
	dmc_mon->chip = tmp;
	dmc_mon->port_num = ports;
	dmc_mon->port = desc;
	get_dmc_ops(dmc_mon->chip, dmc_mon);

	vpu_ports = dmc_find_port_sub(tmp, &vpu_desc);
	if (vpu_ports < 0) {
		dmc_mon->vpu_port = NULL;
		dmc_mon->vpu_port_num = 0;
	}
	dmc_mon->vpu_port_num = vpu_ports;
	dmc_mon->vpu_port = vpu_desc;

	node = pdev->dev.of_node;
	r = of_property_read_u32(node, "reg_base", &tmp);
	if (r < 0) {
		pr_err("can't find iobase\n");
		dmc_mon = NULL;
		return -EINVAL;
	}

	dmc_mon->io_base = tmp;

	/* for register not in secure world */
	for (i = 0; i < dmc_mon->mon_number; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (res) {
			switch (i) {
			case 0:
				dmc_mon->io_mem1 = ioremap(res->start, res->end - res->start);
				break;
			case 1:
				dmc_mon->io_mem2 = ioremap(res->start, res->end - res->start);
				break;
			case 2:
				dmc_mon->io_mem3 = ioremap(res->start, res->end - res->start);
				break;
			case 3:
				dmc_mon->io_mem4 = ioremap(res->start, res->end - res->start);
				break;
			default:
				break;
			}
		}
	}

	r = class_register(&dmc_monitor_class);
	if (r) {
		pr_err("regist dmc_monitor_class failed\n");
		dmc_mon = NULL;
		return -EINVAL;
	}

	dmc_enabled_reserved_memory(pdev, dmc_mon);

	if (init_dmc_debug)
		dmc_mon->debug = init_dmc_debug;

	if (init_dev_mask) {
		dmc_set_monitor(init_start_addr,
				init_end_addr, init_dev_mask, 1);
	}
#if defined(CONFIG_AMLOGIC_USER_FAULT) && \
	defined(CONFIG_TRACEPOINTS) && \
	defined(CONFIG_ANDROID_VENDOR_HOOKS)
#if CONFIG_AMLOGIC_KERNEL_VERSION >= 14515
	register_trace_android_rvh_arm64_serror_panic(arm64_serror_panic, NULL);
	register_trace_android_rvh_do_sea(do_sea, NULL);
#else
	register_trace_android_rvh_do_serror(do_serror, NULL);
#endif
#endif

	if (dmc_mon->debug & DMC_DEBUG_SUSPEND) {
		if (dmc_irq_set(node, 1, 0) < 0)
			pr_emerg("get dmc irq failed\n");
	} else {
		if (dmc_irq_set(node, 1, 1) < 0)
			pr_emerg("request dmc irq failed\n");
	}

	if (dmc_mon->debug & DMC_DEBUG_SERROR) {
		if (dmc_mon->ops && dmc_mon->ops->reg_control)
			dmc_mon->ops->reg_control(NULL, 'c', NULL);
		serror_dump_dmc_reg();
	}

	return 0;
}

static int dmc_monitor_remove(struct platform_device *pdev)
{
	class_unregister(&dmc_monitor_class);
	dmc_mon = NULL;
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id dmc_monitor_match[] = {
#ifndef CONFIG_AMLOGIC_REMOVE_OLD
	{
		.compatible = "amlogic,dmc_monitor-gxl",
		.data = (void *)DMC_TYPE_GXL,	/* chip id */
	},
	{
		.compatible = "amlogic,dmc_monitor-gxm",
		.data = (void *)DMC_TYPE_GXM,
	},
	{
		.compatible = "amlogic,dmc_monitor-gxlx",
		.data = (void *)DMC_TYPE_GXLX,
	},
	{
		.compatible = "amlogic,dmc_monitor-axg",
		.data = (void *)DMC_TYPE_AXG,
	},
	{
		.compatible = "amlogic,dmc_monitor-txl",
		.data = (void *)DMC_TYPE_TXL,
	},
	{
		.compatible = "amlogic,dmc_monitor-txlx",
		.data = (void *)DMC_TYPE_TXLX,
	},
	{
		.compatible = "amlogic,dmc_monitor-txhd",
		.data = (void *)DMC_TYPE_TXHD,
	},
	{
		.compatible = "amlogic,dmc_monitor-tl1",
		.data = (void *)DMC_TYPE_TL1,
	},
	{
		.compatible = "amlogic,dmc_monitor-c1",
		.data = (void *)DMC_TYPE_C1,
	},
#endif
#ifndef CONFIG_AMLOGIC_ZAPPER_CUT
	{
		.compatible = "amlogic,dmc_monitor-c2",
		.data = (void *)DMC_TYPE_C2,
	},
	{
		.compatible = "amlogic,dmc_monitor-g12a",
		.data = (void *)DMC_TYPE_G12A,
	},
	{
		.compatible = "amlogic,dmc_monitor-sm1",
		.data = (void *)DMC_TYPE_SM1,
	},
	{
		.compatible = "amlogic,dmc_monitor-g12b",
		.data = (void *)DMC_TYPE_G12B,
	},
	{
		.compatible = "amlogic,dmc_monitor-tm2",
		.data = (void *)DMC_TYPE_TM2,
	},
	{
		.compatible = "amlogic,dmc_monitor-t5",
		.data = (void *)DMC_TYPE_T5,
	},
	{
		.compatible = "amlogic,dmc_monitor-t5d",
		.data = (void *)DMC_TYPE_T5D,
	},
	{
		.compatible = "amlogic,dmc_monitor-t5w",
		.data = (void *)DMC_TYPE_T5W,
	},
	{
		.compatible = "amlogic,dmc_monitor-t7",
		.data = (void *)DMC_TYPE_T7,
	},
	{
		.compatible = "amlogic,dmc_monitor-t3",
		.data = (void *)DMC_TYPE_T3,
	},
	{
		.compatible = "amlogic,dmc_monitor-p1",
		.data = (void *)DMC_TYPE_P1,
	},
#endif
#ifndef CONFIG_AMLOGIC_ZAPPER_CUT_C1A
	{
		.compatible = "amlogic,dmc_monitor-s4",
		.data = (void *)DMC_TYPE_S4,
	},
#endif
#ifndef CONFIG_AMLOGIC_ZAPPER_CUT
	{
		.compatible = "amlogic,dmc_monitor-sc2",
		.data = (void *)DMC_TYPE_SC2,
	},
	{
		.compatible = "amlogic,dmc_monitor-c3",
		.data = (void *)DMC_TYPE_C3,
	},
	{
		.compatible = "amlogic,dmc_monitor-t5m",
		.data = (void *)DMC_TYPE_T5M,
	},
	{
		.compatible = "amlogic,dmc_monitor-s5",
		.data = (void *)DMC_TYPE_S5,
	},
	{
		.compatible = "amlogic,dmc_monitor-t3x",
		.data = (void *)DMC_TYPE_T3X,
	},
	{
		.compatible = "amlogic,dmc_monitor-txhd2",
		.data = (void *)DMC_TYPE_TXHD2,
	},
#endif
	{
		.compatible = "amlogic,dmc_monitor-s1a",
		.data = (void *)DMC_TYPE_S1A,
	},
	{}
};
#endif

static struct platform_driver dmc_monitor_driver = {
	.driver = {
		.name  = "dmc_monitor",
		.owner = THIS_MODULE,
	},
	.remove = dmc_monitor_remove,
};

int __init dmc_monitor_init(void)
{
#ifdef CONFIG_OF
	const struct of_device_id *match_id;

	match_id = dmc_monitor_match;
	dmc_monitor_driver.driver.of_match_table = match_id;
#endif

	platform_driver_probe(&dmc_monitor_driver, dmc_monitor_probe);
	return 0;
}

void dmc_monitor_exit(void)
{
	platform_driver_unregister(&dmc_monitor_driver);
}
