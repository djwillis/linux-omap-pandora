/*
 * omap3pandora.c  --  SoC audio for OMAP3 Pandora
 *
 * Author: David-John Willis
 *
 * Based on omap3pandora.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <asm/arch/hardware.h>
#include <asm/arch/gpio.h>
#include <asm/arch/mcbsp.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"
#include "../codecs/twl4030.h"

static int omap3pandora_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_cpu_dai *cpu_dai = rtd->dai->cpu_dai;
	int ret;

	/* Set codec DAI configuration */
	ret = codec_dai->dai_ops.set_fmt(codec_dai,
					 SND_SOC_DAIFMT_I2S |
					 SND_SOC_DAIFMT_NB_NF |
					 SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
	printk(KERN_INFO "can't set codec DAI configuration\n");
		return ret;
	}

	/* Set cpu DAI configuration */
	ret = cpu_dai->dai_ops.set_fmt(cpu_dai,
				       SND_SOC_DAIFMT_I2S |
				       SND_SOC_DAIFMT_NB_NF |
				       SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
	printk(KERN_INFO "can't set cpu DAI configuration\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops omap3pandora_ops = {
	.hw_params = omap3pandora_hw_params,
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link omap3pandora_dai = {
	.name = "TWL4030",
	.stream_name = "TWL4030",
	.cpu_dai = &omap_mcbsp_dai[0],
	.codec_dai = &twl4030_dai,
	.ops = &omap3pandora_ops,
};

/* Audio machine driver */
static struct snd_soc_machine snd_soc_machine_omap3pandora = {
	.name = "omap3pandora",
	.dai_link = &omap3pandora_dai,
	.num_links = 1,
};

/* Audio subsystem */
static struct snd_soc_device omap3pandora_snd_devdata = {
	.machine = &snd_soc_machine_omap3pandora,
	.platform = &omap_soc_platform,
	.codec_dev = &soc_codec_dev_twl4030,
};

static struct platform_device *omap3pandora_snd_device;

static int __init omap3pandora_soc_init(void)
{
	int ret;

	printk(KERN_INFO "OMAP3 Pandora SoC init\n");
	if (!machine_is_omap3_pandora()) {
		printk(KERN_INFO "Not OMAP3 Pandora!\n");
		return -ENODEV;
	}

	omap3pandora_snd_device = platform_device_alloc("soc-audio", -1);
	if (!omap3pandora_snd_device) {
		printk(KERN_INFO "Platform device allocation failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(omap3pandora_snd_device, &omap3pandora_snd_devdata);
	omap3pandora_snd_devdata.dev = &omap3pandora_snd_device->dev;
	*(unsigned int *)omap3pandora_dai.cpu_dai->private_data = 1; /* McBSP2 */

	ret = platform_device_add(omap3pandora_snd_device);
	if (ret)
		goto err1;

	return 0;

err1:
	printk(KERN_INFO "Unable to add platform device\n");
	platform_device_put(omap3pandora_snd_device);

	return ret;
}

static void __exit omap3pandora_soc_exit(void)
{
	printk(KERN_INFO "OMAP3 Pandora SoC exit\n");
	platform_device_unregister(omap3pandora_snd_device);
}

module_init(omap3pandora_soc_init);
module_exit(omap3pandora_soc_exit);

MODULE_AUTHOR("David-John Willis");
MODULE_DESCRIPTION("ALSA SoC OMAP3 Pandora");
MODULE_LICENSE("GPL");
