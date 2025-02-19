// SPDX-License-Identifier: GPL-2.0
/*
 * Amlogic loopback ASoc driverloopback_ioctl
 *
 * Copyright (C) 2019 Amlogic, Inc. All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#include <sound/pcm_params.h>

#include <linux/amlogic/pm.h>

#include "loopback.h"
#include "loopback_hw.h"
#include "loopback_match_table.h"
#include "ddr_mngr.h"
#include "tdm_hw.h"
#include "pdm_hw.h"

#include "vad.h"
#include "pdm.h"
#include "tdm.h"
#include "audio_controller.h"

#define DRV_NAME "loopback"

//#define DEBUG

struct lb_src_table {
	enum datalb_src src;
	char *name;
};

struct data_src_table {
	enum datain_src src;
	char *name;
};

struct data_src_table loopback_data_table[DATAIN_MAX] = {
	{DATAIN_TDMA,	   "tdmin_a"},
	{DATAIN_TDMB,	   "tdmin_b"},
	{DATAIN_TDMC,	   "tdmin_c"},
	{DATAIN_SPDIF,	   "spdifin"},
	{DATAIN_PDM,	   "pdmin"},
	{DATAIN_TDMD,	   "tdmin_d"},
	{DATAIN_PDMB,	   "pdmin_b"}
};

struct lb_src_table tdmin_lb_src_table[TDMINLB_SRC_MAX] = {
	{TDMINLB_TDMOUTA,	   "tdmout_a"},
	{TDMINLB_TDMOUTB,	   "tdmout_b"},
	{TDMINLB_TDMOUTC,	   "tdmout_c"},
	{TDMINLB_PAD_TDMINA,   "tdmin_a"},
	{TDMINLB_PAD_TDMINB,   "tdmin_b"},
	{TDMINLB_PAD_TDMINC,   "tdmin_c"},
	{TDMINLB_PAD_TDMINA_D, "tdmind_a"},
	{TDMINLB_PAD_TDMINB_D, "tdmind_b"},
	{TDMINLB_PAD_TDMINC_D, "tdmind_c"},
	{TDMINLB_HDMIRX,	   "hdmirx"},
	{TDMINLB_ACODEC,	   "acodec_adc"},
	{SPDIFINLB_SPDIFOUTA,  "spdifout_a"},
	{SPDIFINLB_SPDIFOUTB,  "spdifout_b"},
	{TDMINLB_TDMOUTD,	   "tdmout_d"},

};

static char *tdmin_lb_src2str(enum datalb_src src)
{
	int i = 0;

	if (src >= TDMINLB_SRC_MAX)
		src = TDMINLB_TDMOUTA;
	for (i = 0; i < TDMINLB_SRC_MAX; i++) {
		if (tdmin_lb_src_table[i].src == src)
			break;
	}
	if (i >= TDMINLB_SRC_MAX)
		i =  0;
	return tdmin_lb_src_table[i].name;
}

static char *loopback_src_table(enum datain_src src)
{
	int i = 0;

	if (src >= DATAIN_MAX)
		src = DATAIN_PDM;
	for (i = 0; i < DATAIN_MAX; i++) {
		if (loopback_data_table[i].src == src)
			break;
	}
	if (i >= DATAIN_MAX)
		i = 0;
	return loopback_data_table[i].name;
}
struct loopback {
	struct device *dev;
	struct aml_audio_controller *actrl;
	unsigned int id;

	/*
	 * datain
	 */

	/* PDM clocks */
	struct clk *pdm_clk_gate;
	struct clk *pdm_sysclk_srcpll;
	struct clk *pdm_dclk_srcpll;
	struct clk *pdm_sysclk;
	struct clk *pdm_dclk;
	unsigned int dclk_idx;

	/* datain info */
	enum datain_src datain_src;
	unsigned int datain_chnum;
	unsigned int datain_chmask;
	unsigned int datain_lane_mask; /* related with data lane */

	/*
	 * datalb
	 */

	/* TDMIN_LB clocks */
	struct clk *tdminlb_mpll;
	struct clk *tdminlb_mclk;
	unsigned int mclk_fs_ratio;

	/* datalb info */
	enum datalb_src datalb_src;
	unsigned int datalb_chnum;
	unsigned int datalb_chmask;
	unsigned int datalb_lane_mask; /* related with data lane */
	unsigned int lb_format;
	unsigned int lb_lane_chmask;
	unsigned int sysclk_freq;

	struct toddr *tddr;

	struct loopback_chipinfo *chipinfo;
	bool vad_running;

	/* Hibernation for vad, suspended or not */
	bool vad_hibernation;
	/* whether vad buffer is used, for xrun */
	bool vad_buf_occupation;
	bool vad_buf_recovery;

	void *mic_src;
	enum trigger_state loopback_trigger_state;
	unsigned int syssrc_clk_rate;
	void *ref_src;
	/*tdm_in lb select mclk*/
	int tdmlb_mclk_sel;
	int data_lb_rate;
};

static struct loopback *loopback_priv[2];

/* this works because the caller resample probe later than loopback */
unsigned int loopback_get_lb_channel(int id)
{
	/* default lb stereo input */
	if (id < 0 || id > 1 || !loopback_priv[id])
		return 2;

	return loopback_priv[id]->datalb_chnum;
}

#define LOOPBACK_BUFFER_BYTES (512 * 1024)
#define LOOPBACK_RATES		(SNDRV_PCM_RATE_8000_192000)
#define LOOPBACK_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE |\
						SNDRV_PCM_FMTBIT_S24_LE |\
						SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_pcm_hardware loopback_hardware = {
	.info =
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_PAUSE,

	.formats = LOOPBACK_FORMATS,

	.period_bytes_max = 256 * 64,
	.buffer_bytes_max = LOOPBACK_BUFFER_BYTES,
	.period_bytes_min = 64,
	.periods_min = 1,
	.periods_max = 1024,

	.rate_min = 8000,
	.rate_max = 192000,
	.channels_min = 1,
	.channels_max = 32,
};

static irqreturn_t loopback_ddr_isr(int irq, void *data)
{
	struct snd_pcm_substream *ss = (struct snd_pcm_substream *)data;
	struct snd_soc_pcm_runtime *rtd = ss->private_data;
	struct loopback *p_loopback = (struct loopback *)
		snd_soc_dai_get_drvdata(asoc_rtd_to_cpu(rtd, 0));
	unsigned int status;
	bool vad_running = vad_lb_is_running(p_loopback->id);

	if (!snd_pcm_running(ss))
		return IRQ_NONE;

	if (p_loopback->vad_running != vad_running) {
		if (p_loopback->vad_running)
			snd_pcm_stop_xrun(ss);

		p_loopback->vad_running = vad_running;
	}

	status = aml_toddr_get_status(p_loopback->tddr) & MEMIF_INT_MASK;
	if (status & MEMIF_INT_COUNT_REPEAT) {
		snd_pcm_period_elapsed(ss);
		aml_toddr_ack_irq(p_loopback->tddr, MEMIF_INT_COUNT_REPEAT);
	} else {
		dev_dbg(p_loopback->dev, "unexpected irq - STS 0x%02x\n",
			status);
	}

	return !status ? IRQ_NONE : IRQ_HANDLED;
}

static int loopback_open(struct snd_soc_component *component, struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct snd_soc_pcm_runtime *rtd = ss->private_data;
	struct loopback *p_loopback = (struct loopback *)
		snd_soc_dai_get_drvdata(asoc_rtd_to_cpu(rtd, 0));
	int ret = 0;
	struct device *dev = p_loopback->dev;

	snd_soc_set_runtime_hwparams(ss, &loopback_hardware);
	snd_pcm_lib_preallocate_pages(ss, SNDRV_DMA_TYPE_DEV,
		dev, LOOPBACK_BUFFER_BYTES / 2, LOOPBACK_BUFFER_BYTES);

	p_loopback->tddr = aml_audio_register_toddr(dev,
		loopback_ddr_isr, ss);
	if (!p_loopback->tddr) {
		ret = -ENXIO;
		dev_err(dev, "failed to claim to ddr\n");
		goto err_ddr;
	}

	runtime->private_data = p_loopback;

	return 0;

err_ddr:
	snd_pcm_lib_free_pages(ss);
	return ret;
}

static int loopback_close(struct snd_soc_component *component, struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct loopback *p_loopback = runtime->private_data;

	aml_audio_unregister_toddr(p_loopback->dev, ss);

	runtime->private_data = NULL;
	snd_pcm_lib_free_pages(ss);

	return 0;
}

static int loopback_hw_params(struct snd_soc_component *component,
			      struct snd_pcm_substream *ss,
			      struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(ss, params_buffer_bytes(hw_params));
}

static int loopback_hw_free(struct snd_soc_component *component, struct snd_pcm_substream *ss)
{
	return snd_pcm_lib_free_pages(ss);
}

static int loopback_prepare(struct snd_soc_component *component, struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct loopback *p_loopback = runtime->private_data;
	unsigned int start_addr, end_addr, int_addr;
	unsigned int period, threshold;
	struct toddr *to = p_loopback->tddr;

	if (p_loopback->loopback_trigger_state == TRIGGER_START_ALSA_BUF ||
	    p_loopback->loopback_trigger_state == TRIGGER_START_VAD_BUF) {
		pr_err("%s, trigger state is %d\n", __func__,
			p_loopback->loopback_trigger_state);
		return 0;
	}
	start_addr = runtime->dma_addr;
	end_addr = start_addr + runtime->dma_bytes - FIFO_BURST;
	period	 = frames_to_bytes(runtime, runtime->period_size);
	int_addr = period / FIFO_BURST;

	/*
	 * Contrast minimum of period and fifo depth,
	 * and set the value as half.
	 */
	threshold = min(period, to->chipinfo->fifo_depth);
	threshold /= 2;
	aml_toddr_set_fifos(to, threshold);

	aml_toddr_set_buf(to, start_addr, end_addr);
	aml_toddr_set_intrpt(to, int_addr);

	return 0;
}

static snd_pcm_uframes_t loopback_pointer(struct snd_soc_component *component,
					  struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct loopback *p_loopback = runtime->private_data;
	unsigned int addr, start_addr;
	snd_pcm_uframes_t frames = 0;

	start_addr = runtime->dma_addr;
	addr = aml_toddr_get_position(p_loopback->tddr);

	frames = bytes_to_frames(runtime, addr - start_addr);
	if (frames > runtime->buffer_size)
		frames = 0;

	return frames;
}

static int loopback_mmap(struct snd_soc_component *component,
			struct snd_pcm_substream *substream,
			struct vm_area_struct *vma)
{
	return snd_pcm_lib_default_mmap(substream, vma);
}

static int loopback_ioctl(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream,
			  unsigned int cmd, void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int datain_pdm_startup(struct loopback *p_loopback)
{
	int ret;

	/* enable clock gate */
	ret = clk_prepare_enable(p_loopback->pdm_clk_gate);

	/* enable clock */
	ret = clk_prepare_enable(p_loopback->pdm_sysclk_srcpll);
	if (ret) {
		pr_err("Can't enable pcm pdm_sysclk_srcpll clock: %d\n", ret);
		goto err;
	}

	ret = clk_prepare_enable(p_loopback->pdm_dclk_srcpll);
	if (ret) {
		pr_err("Can't enable pcm pdm_dclk_srcpll clock: %d\n", ret);
		goto err;
	}

	ret = clk_prepare_enable(p_loopback->pdm_sysclk);
	if (ret) {
		pr_err("Can't enable pcm pdm_sysclk clock: %d\n", ret);
		goto err;
	}

	ret = clk_prepare_enable(p_loopback->pdm_dclk);
	if (ret) {
		pr_err("Can't enable pcm pdm_dclk clock: %d\n", ret);
		goto err;
	}

	return 0;
err:
	pr_err("failed enable pdm clock\n");
	return -EINVAL;
}

static void datain_pdm_shutdown(struct loopback *p_loopback)
{
	/* disable clock and gate */
	clk_disable_unprepare(p_loopback->pdm_dclk);
	clk_disable_unprepare(p_loopback->pdm_sysclk);
	clk_disable_unprepare(p_loopback->pdm_sysclk_srcpll);
	clk_disable_unprepare(p_loopback->pdm_dclk_srcpll);
	clk_disable_unprepare(p_loopback->pdm_clk_gate);
}

static int loopback_dai_startup(struct snd_pcm_substream *ss,
	struct snd_soc_dai *dai)
{
	struct loopback *p_loopback = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	pr_info("%s\n", __func__);

	/* datain */
	if (p_loopback->datain_chnum > 0) {
		switch (p_loopback->datain_src) {
		case DATAIN_TDMA:
		case DATAIN_TDMB:
		case DATAIN_TDMC:
		case DATAIN_TDMD:
			break;
		case DATAIN_SPDIF:
			break;
		case DATAIN_PDM:
		case DATAIN_PDMB:
			ret = datain_pdm_startup(p_loopback);
			if (ret < 0)
				goto err;
			break;
		case DATAIN_LOOPBACK:
			break;
		default:
			break;
		}
	}
	/* datalb */
	if (p_loopback->datalb_chnum > 0) {
		switch (p_loopback->datalb_src) {
		case TDMINLB_TDMOUTA ... TDMINLB_PAD_TDMINC_D:
			/*tdminlb_startup(p_loopback);*/
			break;
		case SPDIFINLB_SPDIFOUTA ... SPDIFINLB_SPDIFOUTB:
			break;
		default:
			break;
		}
	}
	return ret;
err:
	pr_err("Failed to enable datain clock\n");
	return ret;
}

static void loopback_dai_shutdown(struct snd_pcm_substream *ss,
	struct snd_soc_dai *dai)
{
	struct loopback *p_loopback = snd_soc_dai_get_drvdata(dai);

	pr_info("%s\n", __func__);

	/* datain */
	if (p_loopback->datain_chnum > 0) {
		switch (p_loopback->datain_src) {
		case DATAIN_TDMA:
		case DATAIN_TDMB:
		case DATAIN_TDMC:
		case DATAIN_TDMD:
			break;
		case DATAIN_SPDIF:
			break;
		case DATAIN_PDM:
		case DATAIN_PDMB:
			datain_pdm_shutdown(p_loopback);
			break;
		case DATAIN_LOOPBACK:
			break;
		default:
			break;
		}
	}
	/* datalb */
	if (p_loopback->datalb_chnum > 0) {
		switch (p_loopback->datalb_src) {
		case TDMINLB_TDMOUTA ... TDMINLB_PAD_TDMINC_D:
			/*tdminlb_shutdown(p_loopback);*/
			break;
		case SPDIFINLB_SPDIFOUTA ... SPDIFINLB_SPDIFOUTB:
			break;
		default:
			break;
		}
	}
}

static void loopback_set_clk(struct loopback *p_loopback, bool enable)
{
	tdminlb_set_clk(p_loopback->datalb_src, p_loopback->tdmlb_mclk_sel, enable);
}

static int loopback_set_ctrl(struct loopback *p_loopback, int bitwidth)
{
	unsigned int datain_toddr_type, datalb_toddr_type;
	unsigned int datain_msb, datain_lsb, datalb_msb, datalb_lsb;
	struct data_cfg datain_cfg;
	struct data_cfg datalb_cfg;
	char *src_str = NULL;
	struct mux_conf *conf = NULL;

	if (!p_loopback)
		return -EINVAL;

	datain_cfg.ch_ctrl_switch = p_loopback->chipinfo->ch_ctrl;
	datalb_cfg.ch_ctrl_switch = p_loopback->chipinfo->ch_ctrl;

	if (p_loopback->datain_chnum > 0) {
		lb_set_mode(p_loopback->id, MIC_RATE);

		switch (p_loopback->datain_src) {
		case DATAIN_TDMA:
		case DATAIN_TDMB:
		case DATAIN_TDMC:
		case DATAIN_TDMD:
		case DATAIN_PDM:
		case DATAIN_PDMB:
			datain_toddr_type = 0;
			datain_msb = 32 - 1;
			datain_lsb = 0;
			break;
		case DATAIN_SPDIF:
			datain_toddr_type = 3;
			datain_msb = 27;
			datain_lsb = 4;
			if (bitwidth <= 24)
				datain_lsb = 28 - bitwidth;
			else
				datain_lsb = 4;
			break;
		default:
			pr_err("unsupport data in source:%d\n",
				   p_loopback->datain_src);
			return -EINVAL;
		}

		datain_cfg.ext_signed = 0;
		datain_cfg.chnum = p_loopback->datain_chnum;
		datain_cfg.chmask = p_loopback->datain_chmask;
		datain_cfg.type = datain_toddr_type;
		datain_cfg.m = datain_msb;
		datain_cfg.n = datain_lsb;
		datain_cfg.src = p_loopback->datain_src;
		datain_cfg.srcs = p_loopback->chipinfo->srcs;

		lb_set_datain_cfg(p_loopback->id, &datain_cfg);
		/*p1 add tdm d and pdm b*/
		src_str = loopback_src_table(p_loopback->datain_src);
		conf = p_loopback->chipinfo->srcs;
		for (; conf->name[0]; conf++) {
			if (strncmp(conf->name, src_str, strlen(src_str)) == 0)
				break;
		}
		pr_info("data name:%s\n", conf->name);
		if (p_loopback->chipinfo->multi_bits_lbsrcs == 1)
			loopback_src_set(p_loopback->id, conf, 1);
		else
			loopback_src_set(p_loopback->id, conf, 0);
	}

	if (p_loopback->datalb_chnum > 0) {
		/* if src num is 0, set the lb out rate to lb rate */
		if (p_loopback->datain_chnum == 0)
			lb_set_mode(p_loopback->id, LB_RATE);

		switch (p_loopback->datalb_src) {
		case TDMINLB_TDMOUTA ... TDMINLB_PAD_TDMINC_D:
			if (bitwidth == 24) {
				datalb_toddr_type = 4;
				datalb_msb = 32 - 1;
				datalb_lsb = 32 - bitwidth;
			} else {
				datalb_toddr_type = 0;
				datalb_msb = 32 - 1;
				datalb_lsb = 0;
			}
			break;
		default:
			pr_err("unsupport data lb source:%d\n",
				   p_loopback->datalb_src);
			return -EINVAL;
		}

		datalb_cfg.ext_signed  = 0;
		datalb_cfg.chnum = p_loopback->datalb_chnum;
		datalb_cfg.chmask = p_loopback->datalb_chmask;
		datalb_cfg.type = datalb_toddr_type;
		datalb_cfg.m = datalb_msb;
		datalb_cfg.n = datalb_lsb;
		datalb_cfg.loopback_src = 0; /* todo: tdmin_LB */
		datalb_cfg.tdmin_lb_srcs = p_loopback->chipinfo->tdmin_lb_srcs;
		/* get resample B status and resample B is only available for loopbacka */
		if (p_loopback->id == 0)
			datalb_cfg.resample_enable =
				(unsigned int)get_resample_enable(RESAMPLE_B);
		else
			datalb_cfg.resample_enable = 0;

		lb_set_datalb_cfg(p_loopback->id,
			&datalb_cfg,
			p_loopback->chipinfo->multi_bits_lbsrcs,
			p_loopback->chipinfo->use_resamplea);
	}

	tdminlb_set_format(p_loopback->lb_format == SND_SOC_DAIFMT_I2S);
	tdminlb_set_lanemask_and_chswap(0x76543210,
		p_loopback->datalb_lane_mask,
		p_loopback->lb_lane_chmask);
	tdminlb_set_slot_num(p_loopback->datalb_chnum,
			p_loopback->lb_format == SND_SOC_DAIFMT_I2S);

	src_str = tdmin_lb_src2str(p_loopback->datalb_src);
	conf = p_loopback->chipinfo->tdmin_lb_srcs;
	for (; conf->name[0]; conf++) {
		if (strncmp(conf->name, src_str, strlen(src_str)) == 0)
			break;
	}
	tdminlb_set_ctrl(conf->val);

	return 0;
}

static void datatin_pdm_cfg(struct snd_pcm_runtime *runtime,
	struct loopback *p_loopback)
{
	unsigned int bit_depth = snd_pcm_format_width(runtime->format);
	unsigned int osr;
	int pdm_id = 0;
	struct pdm_info info;
	struct aml_pdm *pdm = (struct aml_pdm *)p_loopback->mic_src;
	int gain_index = 0;

	if (pdm)
		gain_index = pdm->pdm_gain_index;
	if (pdm && pdm->chipinfo)
		pdm_id = pdm->chipinfo->id;
	if (p_loopback->datain_src == DATAIN_PDM)
		pdm_id = 0;
	else if (p_loopback->datain_src == DATAIN_PDMB)
		pdm_id = 1;
	info.bitdepth = bit_depth;
	info.channels = p_loopback->datain_chnum;
	info.lane_masks = p_loopback->datain_lane_mask;
	info.dclk_idx = p_loopback->dclk_idx;
	info.bypass = 0;
	info.sample_count = pdm_get_sample_count(0, p_loopback->dclk_idx);

	if (pdm) {
		aml_pdm_ctrl(&info, pdm_id);
		/* filter for pdm */
		osr = pdm_get_ors(p_loopback->dclk_idx, runtime->rate);
		aml_pdm_filter_ctrl(gain_index, osr, pdm->lpf_filter_mode,
				pdm->hpf_filter_mode, pdm_id);
	}
}

static int loopback_dai_prepare(struct snd_pcm_substream *ss,
	struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct loopback *p_loopback = snd_soc_dai_get_drvdata(dai);
	unsigned int bit_depth = snd_pcm_format_width(runtime->format);

	struct toddr *to = p_loopback->tddr;
	unsigned int msb = 32 - 1;
	unsigned int lsb = 32 - bit_depth;
	unsigned int toddr_type;
	struct toddr_fmt fmt;
	unsigned int src;

	if (p_loopback->loopback_trigger_state == TRIGGER_START_ALSA_BUF ||
	    p_loopback->loopback_trigger_state == TRIGGER_START_VAD_BUF) {
		pr_err("%s, trigger state is %d\n", __func__,
			p_loopback->loopback_trigger_state);
		return 0;
	}

	if (p_loopback->id == 0)
		src = LOOPBACK_A;
	else
		src = LOOPBACK_B;

	pr_info("%s Expected toddr src:%s\n",
		__func__,
		toddr_src_get_str(src));

	switch (bit_depth) {
	case 8:
	case 16:
	case 32:
		toddr_type = 0;
		break;
	case 24:
		toddr_type = 4;
		break;
	default:
		pr_err("invalid bit_depth: %d\n", bit_depth);
		return -EINVAL;
	}

	fmt.type = toddr_type;
	fmt.msb = msb;
	fmt.lsb = lsb;
	fmt.endian  = 0;
	fmt.bit_depth = bit_depth;
	fmt.ch_num = runtime->channels;
	fmt.rate = runtime->rate;

	aml_toddr_select_src(to, src);
	aml_toddr_set_format(to, &fmt);

	if (p_loopback->datain_chnum > 0) {
		switch (p_loopback->datain_src) {
		case DATAIN_TDMA:
		case DATAIN_TDMB:
		case DATAIN_TDMC:
		case DATAIN_TDMD:
			aml_tdmin_set_src(p_loopback->mic_src);
			break;
		case DATAIN_SPDIF:
			break;
		case DATAIN_PDM:
		case DATAIN_PDMB:
			datatin_pdm_cfg(runtime, p_loopback);
			break;
		case DATAIN_LOOPBACK:
			break;
		default:
			pr_err("loopback unexpected datain src 0x%02x\n",
				   p_loopback->datain_src);
			return -EINVAL;
		}
	}

	if (p_loopback->datalb_chnum > 0) {
		switch (p_loopback->datalb_src) {
		case TDMINLB_TDMOUTA:
		case TDMINLB_TDMOUTB:
		case TDMINLB_TDMOUTC:
		case TDMINLB_TDMOUTD:
			break;
		case TDMINLB_PAD_TDMINA:
		case TDMINLB_PAD_TDMINB:
		case TDMINLB_PAD_TDMINC:
		case TDMINLB_PAD_TDMIND:
			aml_tdmin_set_src(p_loopback->ref_src);
			break;
		case TDMINLB_PAD_TDMINA_D:
		case TDMINLB_PAD_TDMINB_D:
		case TDMINLB_PAD_TDMINC_D:
			break;
		case SPDIFINLB_SPDIFOUTA:
		case SPDIFINLB_SPDIFOUTB:
			break;
		default:
			pr_err("loopback unexpected datalb src 0x%02x\n",
				   p_loopback->datalb_src);
			return -EINVAL;
		}
	}
	/* config for loopback, datain, datalb */
	loopback_set_ctrl(p_loopback, bit_depth);

	return 0;
}

static void loopback_mic_src_trigger(struct loopback *p_loopback,
				int stream,
				bool enable)
{
	switch (p_loopback->datain_src) {
	case DATAIN_TDMA:
	case DATAIN_TDMB:
	case DATAIN_TDMC:
	case DATAIN_TDMD:
		aml_tdm_trigger(p_loopback->mic_src,
			stream, enable);
		break;

	case DATAIN_SPDIF:
		break;
	case DATAIN_PDM:
		pdm_enable(enable, 0);
		break;
	case DATAIN_PDMB:
		pdm_enable(enable, 1);
		break;
	case DATAIN_LOOPBACK:
		break;
	default:
		dev_err(p_loopback->dev,
			"unexpected datain src 0x%02x\n",
			p_loopback->datain_src);
	}
}

static void loopback_ref_src_trigger(struct loopback *p_loopback,
				int stream, bool enable)
{
	switch (p_loopback->datalb_src) {
	case TDMINLB_PAD_TDMINA:
	case TDMINLB_PAD_TDMINB:
	case TDMINLB_PAD_TDMINC:
		aml_tdm_trigger(p_loopback->ref_src,
		stream, enable);
		break;
	default:
		break;
	}
}

static void loopback_mic_src_fifo_reset(struct loopback *p_loopback,
				   int stream)
{
	switch (p_loopback->datain_src) {
	case DATAIN_TDMA:
	case DATAIN_TDMB:
	case DATAIN_TDMC:
	case DATAIN_TDMD:
		aml_tdm_fifo_reset(p_loopback->actrl,
			stream, p_loopback->datain_src, 0);
		break;

	case DATAIN_SPDIF:
		break;
	case DATAIN_PDM:
		pdm_fifo_reset(0);
		break;
	case DATAIN_PDMB:
		pdm_fifo_reset(1);
		break;
	case DATAIN_LOOPBACK:
		break;
	default:
		dev_err(p_loopback->dev,
			"unexpected datain src 0x%02x\n",
			p_loopback->datain_src);
	}
}

static void loopback_ref_src_fifo_reset(struct loopback *p_loopback, int stream)
{
	int index;

	switch (p_loopback->datalb_src) {
	case TDMINLB_PAD_TDMINA:
		index = 0;
		break;
	case TDMINLB_PAD_TDMINB:
		index = 1;
		break;
	case TDMINLB_PAD_TDMINC:
		index = 2;
		break;
	default:
		index = 0;
		break;
	}
	if (p_loopback->ref_src)
		aml_tdm_fifo_reset(p_loopback->actrl,
		stream, index, 0);
}
static int loopback_dai_trigger(struct snd_pcm_substream *ss,
		int cmd, struct snd_soc_dai *dai)
{
	struct loopback *p_loopback = snd_soc_dai_get_drvdata(dai);
	bool toddr_stopped = false;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dev_info(ss->pcm->card->dev, "Loopback Capture enable\n");
		if (p_loopback->loopback_trigger_state ==
		    TRIGGER_START_VAD_BUF) {
			/* VAD switch to alsa buffer */
			vad_update_buffer(false);
			audio_toddr_irq_enable(p_loopback->tddr, true);
		} else {
			if (p_loopback->datain_chnum > 0)
				loopback_mic_src_fifo_reset(p_loopback,
							    ss->stream);
			if (p_loopback->datalb_chnum > 0)
				loopback_ref_src_fifo_reset(p_loopback, ss->stream);

			tdminlb_fifo_enable(true);

			aml_toddr_enable(p_loopback->tddr, true);
			/* loopback */
			if (p_loopback->chipinfo && p_loopback->chipinfo->orig_channel_sync) {
				loopback_data_orig_channel_sync(p_loopback->id,
					p_loopback->datain_chnum, true);
				loopback_data_insert_channel_sync(p_loopback->id,
					p_loopback->datalb_chnum, true);
			}
			if (p_loopback->chipinfo)
				lb_enable(p_loopback->id,
					  true,
					  p_loopback->chipinfo->chnum_en);
			else
				lb_enable(p_loopback->id, true, true);
			/* tdminLB */
			tdminlb_enable(p_loopback->datalb_src, true);
			/* pdm */
			if (p_loopback->datain_chnum > 0)
				loopback_mic_src_trigger(p_loopback,
					ss->stream, true);
			if (p_loopback->datalb_chnum > 0)
				loopback_ref_src_trigger(p_loopback, ss->stream, true);
			}
		p_loopback->loopback_trigger_state = TRIGGER_START_ALSA_BUF;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dev_info(ss->pcm->card->dev, "Loopback Capture disable\n");
		if (vad_lb_is_running(p_loopback->id) &&
		    pm_audio_is_suspend() &&
		    p_loopback->loopback_trigger_state ==
			TRIGGER_START_ALSA_BUF) {
			/* switch to VAD buffer */
			vad_update_buffer(true);
			audio_toddr_irq_enable(p_loopback->tddr, false);
			p_loopback->loopback_trigger_state =
				TRIGGER_START_VAD_BUF;
			break;
		}
		if (p_loopback->loopback_trigger_state == TRIGGER_STOP)
			break;

		if (p_loopback->datain_chnum > 0)
			loopback_mic_src_trigger(p_loopback,
				ss->stream, false);
		if (p_loopback->datalb_chnum > 0)
			loopback_ref_src_trigger(p_loopback, ss->stream, false);

		if (p_loopback->chipinfo && p_loopback->chipinfo->orig_channel_sync) {
			loopback_data_orig_channel_sync(p_loopback->id,
					p_loopback->datain_chnum, false);
			loopback_data_insert_channel_sync(p_loopback->id,
					p_loopback->datalb_chnum, false);
		}
		/* loopback */
		if (p_loopback->chipinfo)
			lb_enable(p_loopback->id,
				  false,
				  p_loopback->chipinfo->chnum_en);
		else
			lb_enable(p_loopback->id, false, true);
		/* tdminLB */
		tdminlb_fifo_enable(false);
		tdminlb_enable(p_loopback->datalb_src, false);

		toddr_stopped =
			aml_toddr_burst_finished(p_loopback->tddr);
		if (toddr_stopped)
			aml_toddr_enable(p_loopback->tddr, false);
		else
			pr_err("%s(), toddr may be stuck\n", __func__);
		p_loopback->loopback_trigger_state = TRIGGER_STOP;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void datain_pdm_set_clk(struct loopback *p_loopback)
{
	unsigned int pdm_sysclk_srcpll_freq, pdm_dclk_srcpll_freq;
	char *clk_name = NULL;

	pdm_sysclk_srcpll_freq = clk_get_rate(p_loopback->pdm_sysclk_srcpll);
	pdm_dclk_srcpll_freq = clk_get_rate(p_loopback->pdm_dclk_srcpll);

#ifdef __PTM_PDM_CLK__
	clk_set_rate(p_loopback->pdm_sysclk, 133333351);
	clk_set_rate(p_loopback->pdm_dclk_srcpll, 24576000 * 15); /* 350m */
	clk_set_rate(p_loopback->pdm_dclk, 3072000);
#else

	clk_name = (char *)__clk_get_name(p_loopback->pdm_dclk_srcpll);
	if (!strcmp(clk_name, "hifi_pll") || !strcmp(clk_name, "t5_hifi_pll")) {
		if ((aml_return_chip_id() != CLK_NOTIFY_CHIP_ID) &&
			(aml_return_chip_id() != CLK_NOTIFY_CHIP_ID_T3X)) {
			pr_info("hifipll set 1806336*1000\n");
			if (p_loopback->syssrc_clk_rate)
				clk_set_rate(p_loopback->pdm_dclk_srcpll,
						p_loopback->syssrc_clk_rate);
			else
				clk_set_rate(p_loopback->pdm_dclk_srcpll, 1806336 * 1000);
		} else if (!strcmp(__clk_get_name(clk_get_parent(p_loopback->pdm_dclk)),
			clk_name)) {
			/* T5M use clock notify, if parent changed to hifi1, no need set */
			if (p_loopback->syssrc_clk_rate)
				clk_set_rate(p_loopback->pdm_dclk_srcpll,
						p_loopback->syssrc_clk_rate);
			else
				clk_set_rate(p_loopback->pdm_dclk_srcpll,
								1806336 * 1000);
		}
	} else {
		if (pdm_dclk_srcpll_freq == 0)
			clk_set_rate(p_loopback->pdm_dclk_srcpll, 24576000 * 20);
		else
			pr_info("pdm pdm_dclk_srcpll:%lu\n",
				clk_get_rate(p_loopback->pdm_dclk_srcpll));
	}
#endif

	clk_set_rate(p_loopback->pdm_sysclk, 133333351);
	clk_set_rate(p_loopback->pdm_dclk,
		pdm_dclkidx2rate(p_loopback->dclk_idx));

	pr_info("pdm pdm_sysclk:%lu pdm_dclk:%lu\n",
		clk_get_rate(p_loopback->pdm_sysclk),
		clk_get_rate(p_loopback->pdm_dclk));
}

static int loopback_dai_hw_params(struct snd_pcm_substream *ss,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct loopback *p_loopback = snd_soc_dai_get_drvdata(dai);
	unsigned int rate, channels;
	snd_pcm_format_t format;
	int ret = 0;
	rate = params_rate(params);
	channels = params_channels(params);
	format = params_format(params);

	pr_debug("%s:rate:%d, sysclk:%d\n",
		__func__,
		rate,
		p_loopback->sysclk_freq);

	if (p_loopback->datain_chnum > 0) {
		switch (p_loopback->datain_src) {
		case DATAIN_TDMA:
		case DATAIN_TDMB:
		case DATAIN_TDMC:
		case DATAIN_TDMD:
			aml_tdm_hw_setting_init(p_loopback->mic_src,
				rate, p_loopback->datain_chnum, ss->stream);
			break;
		case DATAIN_SPDIF:
			break;
		case DATAIN_PDM:
		case DATAIN_PDMB:
			datain_pdm_set_clk(p_loopback);
			break;
		case DATAIN_LOOPBACK:
			break;
		default:
			break;
		}
	}

	/* datalb */
	if (p_loopback->datalb_chnum > 0) {
		switch (p_loopback->datalb_src) {
		case SPDIFINLB_SPDIFOUTA ... SPDIFINLB_SPDIFOUTB:
			break;
		case TDMINLB_PAD_TDMINA:
		case TDMINLB_PAD_TDMINB:
		case TDMINLB_PAD_TDMINC:
			if (p_loopback->data_lb_rate != rate && p_loopback->data_lb_rate > 0)
				rate = p_loopback->data_lb_rate;
			aml_tdm_hw_setting_init(p_loopback->ref_src, rate,
				p_loopback->datalb_chnum, ss->stream);
			break;
		default:
			break;
		}
	}
	loopback_set_clk(p_loopback, true);

	return ret;
}

int loopback_dai_hw_free(struct snd_pcm_substream *ss,
	struct snd_soc_dai *dai)
{
	struct loopback *p_loopback = snd_soc_dai_get_drvdata(dai);


	if (p_loopback->datain_chnum > 0) {
		switch (p_loopback->datain_src) {
		case DATAIN_TDMA:
		case DATAIN_TDMB:
		case DATAIN_TDMC:
		case DATAIN_TDMD:
			aml_tdm_hw_setting_free(p_loopback->mic_src, ss->stream);
			break;
		case DATAIN_SPDIF:
			break;
		case DATAIN_PDM:
			break;
		case DATAIN_LOOPBACK:
			break;
		default:
			break;
		}
	}
	if (p_loopback->datalb_chnum > 0) {
		switch (p_loopback->datalb_src) {
		case TDMINLB_PAD_TDMINA:
		case TDMINLB_PAD_TDMINB:
		case TDMINLB_PAD_TDMINC:
			aml_tdm_hw_setting_free(p_loopback->ref_src,
						ss->stream);
			break;
		default:
			break;
		}
	}
	return 0;
}

static int loopback_dai_set_fmt(struct snd_soc_dai *dai,
	unsigned int fmt)
{
	struct loopback *p_loopback = snd_soc_dai_get_drvdata(dai);

	pr_info("asoc %s, %#x, %p\n", __func__, fmt, p_loopback);
	if (p_loopback->datain_chnum > 0) {
		switch (p_loopback->datain_src) {
		case DATAIN_TDMA:
		case DATAIN_TDMB:
		case DATAIN_TDMC:
		case DATAIN_TDMD:
			if (p_loopback->mic_src)
				aml_tdm_set_fmt(p_loopback->mic_src, fmt, 1);
		break;
		case DATAIN_SPDIF:
		break;
		case DATAIN_PDM:
		break;
		case DATAIN_LOOPBACK:
		break;
		default:
		break;
		}
	}
	return 0;
}

static int loopback_dai_set_sysclk(struct snd_soc_dai *dai,
	int clk_id, unsigned int freq, int dir)
{
	struct loopback *p_loopback = snd_soc_dai_get_drvdata(dai);

	p_loopback->sysclk_freq = freq;
	pr_info("\n%s, %d, %d, %d\n",
		__func__,
		clk_id, freq, dir);

	return 0;
}

static int loopback_dai_mute_stream(struct snd_soc_dai *dai,
				int mute, int stream)
{
	struct loopback *p_loopback = snd_soc_dai_get_drvdata(dai);

	if (p_loopback->datain_chnum > 0) {
		switch (p_loopback->datain_src) {
		case DATAIN_TDMA:
		case DATAIN_TDMB:
		case DATAIN_TDMC:
		case DATAIN_TDMD:
			tdm_mute_capture(p_loopback->mic_src, mute);
			break;
		case DATAIN_SPDIF:
			break;
		case DATAIN_PDM:
			break;
		case DATAIN_LOOPBACK:
			break;
		default:
			break;
		}
	}
	if (p_loopback->datalb_chnum > 0) {
		switch (p_loopback->datalb_src) {
		case TDMINLB_PAD_TDMINA:
		case TDMINLB_PAD_TDMINB:
		case TDMINLB_PAD_TDMINC:
			tdm_mute_capture(p_loopback->ref_src, mute);
			break;
		default:
			break;
		}
	}
	return 0;
}

static struct snd_soc_dai_ops loopback_dai_ops = {
	.startup	= loopback_dai_startup,
	.shutdown	= loopback_dai_shutdown,
	.prepare	= loopback_dai_prepare,
	.trigger	= loopback_dai_trigger,
	.hw_params	= loopback_dai_hw_params,
	.hw_free	= loopback_dai_hw_free,
	.set_fmt	= loopback_dai_set_fmt,
	.set_sysclk = loopback_dai_set_sysclk,
	.mute_stream = loopback_dai_mute_stream,
};

static struct snd_soc_dai_driver loopback_dai[] = {
	{
		.name = "LOOPBACK-A",
		.id = 0,
		.capture = {
			 .channels_min = 1,
			 .channels_max = 32,
			 .rates = LOOPBACK_RATES,
			 .formats = LOOPBACK_FORMATS,
		},
		.ops = &loopback_dai_ops,
	},
	{
		.name = "LOOPBACK-B",
		.id = 1,
		.capture = {
			 .channels_min = 1,
			 .channels_max = 32,
			 .rates = LOOPBACK_RATES,
			 .formats = LOOPBACK_FORMATS,
		},
		.ops = &loopback_dai_ops,
	},
};

static const char *const datain_src_texts[] = {
	"TDMIN_A",
	"TDMIN_B",
	"TDMIN_C",
	"SPDIFIN",
	"PDMIN",
};

static const struct soc_enum datain_src_enum =
	SOC_ENUM_SINGLE(EE_AUDIO_LB_CTRL0, 0, ARRAY_SIZE(datain_src_texts),
	datain_src_texts);

static int datain_src_get_enum(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct loopback *p_loopback = dev_get_drvdata(component->dev);

	if (!p_loopback)
		return 0;

	ucontrol->value.enumerated.item[0] = p_loopback->datain_src;

	return 0;
}

static int datain_src_set_enum(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct loopback *p_loopback = dev_get_drvdata(component->dev);

	if (!p_loopback)
		return 0;

	p_loopback->datain_src = ucontrol->value.enumerated.item[0];

	lb_set_datain_src(p_loopback->id, p_loopback->datain_src);

	return 0;
}

static const char *const datalb_tdminlb_texts[] = {
	"TDMOUT_A",
	"TDMOUT_B",
	"TDMOUT_C",
	"TDMIN_A",
	"TDMIN_B",
	"TDMIN_C",
	"TDMIN_A_D",
	"TDMIN_B_D",
	"TDMIN_C_D",
};

static const struct soc_enum datalb_tdminlb_enum =
	SOC_ENUM_SINGLE(EE_AUDIO_TDMIN_LB_CTRL,
		20,
		ARRAY_SIZE(datalb_tdminlb_texts),
		datalb_tdminlb_texts);

static int datalb_tdminlb_get_enum(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct loopback *p_loopback = dev_get_drvdata(component->dev);

	if (!p_loopback)
		return 0;

	ucontrol->value.enumerated.item[0] = p_loopback->datalb_src;

	return 0;
}

static int datalb_tdminlb_set_enum(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct loopback *p_loopback = dev_get_drvdata(component->dev);

	if (!p_loopback)
		return 0;

	p_loopback->datalb_src = ucontrol->value.enumerated.item[0];

	tdminlb_set_src(p_loopback->datalb_src);

	return 0;
}

static const struct snd_kcontrol_new snd_loopback_controls[] = {
	/* loopback data in source */
	SOC_ENUM_EXT("Loopback datain source",
		datain_src_enum,
		datain_src_get_enum,
		datain_src_set_enum),

	/* loopback data tdmin lb */
	SOC_ENUM_EXT("Loopback tdmin lb source",
		datalb_tdminlb_enum,
		datalb_tdminlb_get_enum,
		datalb_tdminlb_set_enum),
};

static const struct snd_kcontrol_new snd_loopbackb_controls[] = {
	/* loopback data in source */
	SOC_ENUM_EXT("Loopbackb datain source",
		datain_src_enum,
		datain_src_get_enum,
		datain_src_set_enum),

	/* loopback data tdmin lb */
	SOC_ENUM_EXT("Loopbackb tdmin lb source",
		datalb_tdminlb_enum,
		datalb_tdminlb_get_enum,
		datalb_tdminlb_set_enum),
};

static const struct snd_soc_component_driver loopback_component[] = {
	{
		.name		  = DRV_NAME,
		.controls	  = snd_loopback_controls,
		.num_controls = ARRAY_SIZE(snd_loopback_controls),

		.open	   = loopback_open,
		.close	   = loopback_close,
		.ioctl	   = loopback_ioctl,
		.hw_params = loopback_hw_params,
		.hw_free   = loopback_hw_free,
		.prepare   = loopback_prepare,
		.pointer   = loopback_pointer,
		.mmap	   = loopback_mmap,
	},
	{
		.name		  = DRV_NAME,
		.controls	  = snd_loopbackb_controls,
		.num_controls = ARRAY_SIZE(snd_loopbackb_controls),

		.open	   = loopback_open,
		.close	   = loopback_close,
		.ioctl	   = loopback_ioctl,
		.hw_params = loopback_hw_params,
		.hw_free   = loopback_hw_free,
		.prepare   = loopback_prepare,
		.pointer   = loopback_pointer,
		.mmap	   = loopback_mmap,
	}
};

#ifdef CONFIG_AMLOGIC_LEGACY_EARLY_SUSPEND
static void loopback_platform_early_suspend(struct early_suspend *h)
{
	struct platform_device *pdev = h->param;

	if (pdev) {
		struct loopback *p_loopback = dev_get_drvdata(&pdev->dev);

		if (!p_loopback)
			return;

		p_loopback->vad_hibernation = true;
	}
}

static void loopback_platform_late_resume(struct early_suspend *h)
{
	struct platform_device *pdev = h->param;

	if (pdev) {
		struct loopback *p_loopback = dev_get_drvdata(&pdev->dev);

		if (!p_loopback)
			return;

		p_loopback->vad_hibernation    = false;
		p_loopback->vad_buf_occupation = false;
		p_loopback->vad_buf_recovery   = false;
	}
};

#define loopback_es_hdr(idx) \
static struct early_suspend loopback_es_hdr_##idx = { \
	.suspend = loopback_platform_early_suspend, \
	.resume  = loopback_platform_late_resume,   \
}

loopback_es_hdr(0);
loopback_es_hdr(1);

static void loopback_register_early_suspend_hdr(int idx, void *pdev)
{
	struct early_suspend *pes = NULL;

	if (idx == LOOPBACKA)
		pes = &loopback_es_hdr_0;
	else if (idx == LOOPBACKB)
		pes = &loopback_es_hdr_1;
	else
		return;

	pes->param	 = pdev;
	register_early_suspend(pes);
}
#endif

static int datain_pdm_parse_of(struct device *dev,
	struct loopback *p_loopback)
{
	int ret;

	/* clock gate */
	p_loopback->pdm_clk_gate = devm_clk_get(dev, "pdm_gate");
	if (IS_ERR(p_loopback->pdm_clk_gate)) {
		dev_err(dev,
			"Can't get pdm gate\n");
		return PTR_ERR(p_loopback->pdm_clk_gate);
	}

	p_loopback->pdm_sysclk_srcpll = devm_clk_get(dev, "pdm_sysclk_srcpll");
	if (IS_ERR(p_loopback->pdm_sysclk_srcpll)) {
		dev_err(dev,
			"Can't retrieve pll clock\n");
		ret = PTR_ERR(p_loopback->pdm_sysclk_srcpll);
		goto err;
	}

	p_loopback->pdm_dclk_srcpll = devm_clk_get(dev, "pdm_dclk_srcpll");
	if (IS_ERR(p_loopback->pdm_dclk_srcpll)) {
		dev_err(dev,
			"Can't retrieve data src clock\n");
		ret = PTR_ERR(p_loopback->pdm_dclk_srcpll);
		goto err;
	}

	p_loopback->pdm_sysclk = devm_clk_get(dev, "pdm_sysclk");
	if (IS_ERR(p_loopback->pdm_sysclk)) {
		dev_err(dev,
			"Can't retrieve pdm_sysclk clock\n");
		ret = PTR_ERR(p_loopback->pdm_sysclk);
		goto err;
	}

	p_loopback->pdm_dclk = devm_clk_get(dev, "pdm_dclk");
	if (IS_ERR(p_loopback->pdm_dclk)) {
		dev_err(dev,
			"Can't retrieve pdm_dclk clock\n");
		ret = PTR_ERR(p_loopback->pdm_dclk);
		goto err;
	}

	ret = clk_set_parent(p_loopback->pdm_sysclk,
			p_loopback->pdm_sysclk_srcpll);
	if (ret) {
		dev_err(dev,
			"Can't set pdm_sysclk parent clock\n");
		ret = PTR_ERR(p_loopback->pdm_sysclk);
		goto err;
	}

	ret = clk_set_parent(p_loopback->pdm_dclk,
			p_loopback->pdm_dclk_srcpll);
	if (ret) {
		dev_err(dev,
			"Can't set pdm_dclk parent clock\n");
		ret = PTR_ERR(p_loopback->pdm_dclk);
		goto err;
	}

	return 0;

err:
	return ret;
}

static int datain_parse_of(struct device_node *node,
	struct loopback *p_loopback)
{
	struct platform_device *pdev;
	int ret;

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		dev_err(&pdev->dev,
			"failed to find platform device\n");
		return -EINVAL;
	}

	if (p_loopback->datain_chnum > 0) {
		switch (p_loopback->datain_src) {
		case DATAIN_TDMA:
		case DATAIN_TDMB:
		case DATAIN_TDMC:
		case DATAIN_TDMD:
			break;
		case DATAIN_SPDIF:
			break;
		case DATAIN_PDM:
		case DATAIN_PDMB:
			ret = datain_pdm_parse_of(&pdev->dev, p_loopback);
			if (ret < 0)
				goto err;
			break;
		case DATAIN_LOOPBACK:
			break;
		default:
			break;
		}
	}
	return 0;
err:
	pr_err("%s, error:%d\n", __func__, ret);
	return -EINVAL;
}

static int datalb_tdminlb_parse_of(struct device_node *node,
	struct loopback *p_loopback)
{
	struct platform_device *pdev;
	int ret;

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		dev_err(&pdev->dev,
			"failed to find platform device\n");
		return -EINVAL;
	}

	/* mpll used for tdmin_lb */
	p_loopback->tdminlb_mpll = devm_clk_get(&pdev->dev, "tdminlb_mpll");
	if (IS_ERR(p_loopback->tdminlb_mpll)) {
		dev_err(&pdev->dev,
			"Can't retrieve tdminlb_mpll clock\n");
		return PTR_ERR(p_loopback->tdminlb_mpll);
	}
	p_loopback->tdminlb_mclk = devm_clk_get(&pdev->dev, "tdminlb_mclk");
	if (IS_ERR(p_loopback->tdminlb_mclk)) {
		dev_err(&pdev->dev,
			"Can't retrieve tdminlb_mclk clock\n");
		return PTR_ERR(p_loopback->tdminlb_mclk);
	}

	ret = clk_set_parent(p_loopback->tdminlb_mclk,
			p_loopback->tdminlb_mpll);
	if (ret) {
		dev_err(&pdev->dev,
			"Can't set tdminlb_mclk parent clock\n");
		ret = PTR_ERR(p_loopback->tdminlb_mpll);
		goto err;
	}

	ret = of_property_read_u32(node, "mclk-fs",
		&p_loopback->mclk_fs_ratio);
	if (ret) {
		pr_warn_once("failed to get mclk-fs node, set it default\n");
		p_loopback->mclk_fs_ratio = 256;
		ret = 0;
	}

	ret = of_property_read_u32(node, "data_lb_rate",
		&p_loopback->data_lb_rate);
	if (ret)
		pr_warn("failed to get data_lb_ratec\n");

	return 0;
err:
	pr_err("%s, error:%d\n", __func__, ret);
	return -EINVAL;
}

static unsigned int loopback_parse_format(struct device_node *node)
{
	unsigned int format = 0;
	int ret = 0;
	const char *str;
	struct {
		char *name;
		unsigned int val;
	} fmt_table[] = {
		{"i2s", SND_SOC_DAIFMT_I2S},
		{"dsp_a", SND_SOC_DAIFMT_DSP_A},
		{"dsp_b", SND_SOC_DAIFMT_DSP_B}
	};

	ret = of_property_read_string(node, "datalb-format", &str);
	if (ret == 0) {
		int i;

		for (i = 0; i < ARRAY_SIZE(fmt_table); i++) {
			if (strcmp(str, fmt_table[i].name) == 0) {
				format |= fmt_table[i].val;
				break;
			}
		}
	}

	/* default format is I2S */
	if (format == 0)
		format = SND_SOC_DAIFMT_I2S;

	return format;
}

static int loopback_parse_of(struct device_node *node,
	struct loopback *p_loopback)
{
	struct platform_device *pdev;
	int ret;

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		dev_err(&pdev->dev, "failed to find platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	ret = of_property_read_u32(node, "datain_src",
		&p_loopback->datain_src);
	if (ret) {
		pr_err("failed to get datain_src\n");
		ret = -EINVAL;
		goto fail;
	}
	ret = of_property_read_u32(node, "datain_chnum",
		&p_loopback->datain_chnum);
	if (ret)
		pr_debug("datain_chnum = 0, only record output data\n");
	ret = of_property_read_u32(node, "datain_chmask",
		&p_loopback->datain_chmask);
	if (ret) {
		pr_err("failed to get datain_chmask\n");
		ret = -EINVAL;
		goto fail;
	}
	ret = snd_soc_of_get_slot_mask(node, "datain-lane-mask-in",
		&p_loopback->datain_lane_mask);
	if (ret < 0) {
		ret = -EINVAL;
		dev_err(&pdev->dev, "datain lane-slot-mask should be set\n");
		goto fail;
	}

	ret = of_property_read_u32(node, "datalb_src",
		&p_loopback->datalb_src);
	if (ret) {
		pr_err("failed to get datalb_src\n");
		ret = -EINVAL;
		goto fail;
	}
	ret = of_property_read_u32(node, "datalb_chnum",
		&p_loopback->datalb_chnum);
	if (ret) {
		pr_err("failed to get datalb_chnum\n");
		ret = -EINVAL;
		goto fail;
	}
	ret = of_property_read_u32(node, "datalb_chmask",
		&p_loopback->datalb_chmask);
	if (ret) {
		pr_err("failed to get datalb_chmask\n");
		ret = -EINVAL;
		goto fail;
	}

	ret = snd_soc_of_get_slot_mask(node, "datalb-lane-mask-in",
		&p_loopback->datalb_lane_mask);
	if (ret < 0) {
		ret = -EINVAL;
		pr_err("datalb lane-slot-mask should be set\n");
		goto fail;
	}

	p_loopback->lb_format = loopback_parse_format(node);
	snd_soc_of_get_slot_mask
		(node,
		"datalb-channels-mask",
		&p_loopback->lb_lane_chmask);
	if (p_loopback->lb_lane_chmask == 0) {
		/* default format is I2S and mask two channels */
		p_loopback->lb_lane_chmask = 0x3;
	}
	pr_debug("\tdatain_src:%d, datain_chnum:%d, datain_chumask:%x\n",
		p_loopback->datain_src,
		p_loopback->datain_chnum,
		p_loopback->datain_chmask);
	pr_debug("\tdatalb_src:%d, datalb_chnum:%d, datalb_chmask:%x\n",
		p_loopback->datalb_src,
		p_loopback->datalb_chnum,
		p_loopback->datalb_chmask);
	pr_debug("\tdatain_lane_mask:0x%x, datalb_lane_mask:0x%x\n",
		p_loopback->datain_lane_mask,
		p_loopback->datalb_lane_mask);
	pr_debug("datalb_format: %d, chmask for lanes: %#x\n",
		p_loopback->lb_format, p_loopback->lb_lane_chmask);

	ret = datain_parse_of(node, p_loopback);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to parse datain\n");
		return ret;
	}
	ret = datalb_tdminlb_parse_of(node, p_loopback);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to parse datalb\n");
		return ret;
	}

	return 0;

fail:
	return ret;
}

static int loopback_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct loopback *p_loopback = NULL;
	struct loopback_chipinfo *p_chipinfo = NULL;
	struct device_node *node_prt = NULL;
	struct platform_device *pdev_parent = NULL;
	struct device_node *np_src = NULL;
	struct platform_device *dev_src = NULL;
	struct aml_audio_controller *actrl = NULL;
	int ret = 0;

	p_loopback = devm_kzalloc(&pdev->dev,
			sizeof(struct loopback),
			GFP_KERNEL);
	if (!p_loopback)
		return -ENOMEM;

	np_src = of_parse_phandle(node, "mic-src", 0);
	if (np_src) {
		dev_src = of_find_device_by_node(np_src);
		of_node_put(np_src);
		p_loopback->mic_src = platform_get_drvdata(dev_src);
		pr_debug("%s(), mic_src found\n", __func__);
	}
	np_src = of_parse_phandle(node, "ref-src", 0);
	if (np_src) {
		dev_src = of_find_device_by_node(np_src);
		of_node_put(np_src);
		p_loopback->ref_src = platform_get_drvdata(dev_src);
		pr_debug("%s(), mic_src found\n", __func__);
	}

	/* match data */
	p_chipinfo = (struct loopback_chipinfo *)
		of_device_get_match_data(dev);
	p_loopback->chipinfo = p_chipinfo;
	p_loopback->id = p_chipinfo->id;

	loopback_priv[p_loopback->id] = p_loopback;

	ret = of_property_read_u32(dev->of_node, "src-clk-freq",
				   &p_loopback->syssrc_clk_rate);
	if (ret < 0)
		p_loopback->syssrc_clk_rate = 0;
	else
		pr_debug("%s sys-src clk rate from dts:%d\n",
			__func__, p_loopback->syssrc_clk_rate);

	ret = of_property_read_u32(dev->of_node, "tdmlb-mclk-sel",
				   &p_loopback->tdmlb_mclk_sel);
	if (ret < 0)
		p_loopback->tdmlb_mclk_sel = -1;
	else
		pr_info("%s tdmlb_mclk_sel :%d\n",
			__func__, p_loopback->tdmlb_mclk_sel);

	/* get audio controller */
	node_prt = of_get_parent(node);
	if (!node_prt)
		return -ENXIO;

	pdev_parent = of_find_device_by_node(node_prt);
	of_node_put(node_prt);
	actrl = (struct aml_audio_controller *)
				platform_get_drvdata(pdev_parent);
	p_loopback->actrl = actrl;

	ret = loopback_parse_of(dev->of_node, p_loopback);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to parse loopback info\n");
		return ret;
	}

	p_loopback->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, p_loopback);

	ret = devm_snd_soc_register_component(&pdev->dev,
				&loopback_component[p_loopback->id],
				&loopback_dai[p_loopback->id],
				1);
	if (ret) {
		dev_err(&pdev->dev,
			"snd_soc_register_component failed\n");
		return ret;
	}

	pr_debug("%s, p_loopback->id:%d register soc platform\n",
		__func__,
		p_loopback->id);

#ifdef CONFIG_AMLOGIC_LEGACY_EARLY_SUSPEND
	loopback_register_early_suspend_hdr(p_loopback->id, pdev);
#endif

	return 0;
}

static int loopback_platform_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct loopback *p_loopback = dev_get_drvdata(&pdev->dev);

	/* whether in freeze */
	if (is_pm_s2idle_mode() && vad_lb_is_running(p_loopback->id)) {
		if (p_loopback->chipinfo)
			lb_set_chnum_en(p_loopback->id,
					true,
					p_loopback->chipinfo->chnum_en);
		else
			lb_set_chnum_en(p_loopback->id, true, true);
		vad_lb_force_two_channel(true);

		pr_debug("%s, Entry in freeze, p_loopback:%p\n",
			__func__, p_loopback);
	}

	return 0;
}

static int loopback_platform_resume(struct platform_device *pdev)
{
	struct loopback *p_loopback = dev_get_drvdata(&pdev->dev);

	/* whether in freeze mode */
	if (is_pm_s2idle_mode() && vad_lb_is_running(p_loopback->id)) {
		pr_debug("%s, Exist from freeze, p_loopback:%p\n",
			__func__, p_loopback);
		if (p_loopback->chipinfo)
			lb_set_chnum_en(p_loopback->id,
					false,
					p_loopback->chipinfo->chnum_en);
		else
			lb_set_chnum_en(p_loopback->id, false, true);
		vad_lb_force_two_channel(false);
	}

	return 0;
}

static void loopback_platform_shutdown(struct platform_device *pdev)
{
	struct loopback *p_loopback = dev_get_drvdata(&pdev->dev);

	/* whether in freeze */
	if (/* is_pm_freeze_mode() && */vad_lb_is_running(p_loopback->id)) {
		if (p_loopback->chipinfo)
			lb_set_chnum_en(p_loopback->id,
					true,
					p_loopback->chipinfo->chnum_en);
		else
			lb_set_chnum_en(p_loopback->id, true, true);
		vad_lb_force_two_channel(true);

		pr_debug("%s, Entry in freeze, p_loopback:%p\n",
			__func__, p_loopback);
	}
}

static struct platform_driver loopback_platform_driver = {
	.driver = {
		.name			= DRV_NAME,
		.owner			= THIS_MODULE,
		.of_match_table = of_match_ptr(loopback_device_id),
	},
	.probe	= loopback_platform_probe,
	.suspend = loopback_platform_suspend,
	.resume  = loopback_platform_resume,
	.shutdown = loopback_platform_shutdown,
};

int __init loopback_init(void)
{
	return platform_driver_register(&(loopback_platform_driver));
}

void __exit loopback_exit(void)
{
	platform_driver_unregister(&loopback_platform_driver);
}

#ifndef MODULE
module_init(loopback_init);
module_exit(loopback_exit);
MODULE_AUTHOR("AMLogic, Inc.");
MODULE_DESCRIPTION("Amlogic Loopback driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
#endif
