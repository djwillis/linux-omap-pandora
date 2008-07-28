/*
 * ALSA SoC TWL4030 codec driver
 *
 * Author:      Steve Sakoman, <steve@sakoman.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl4030.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "twl4030.h"

/*
 * twl4030 register cache & default register settings
 */
static const u8 twl4030_reg[TWL4030_CACHEREGNUM] = {
	0x00, // this register not used
	0x93, // REG_CODEC_MODE		(0x1)
	0xc3, // REG_OPTION		(0x2)
	0x00, // REG_UNKNOWN		(0x3)
	0x00, // REG_MICBIAS_CTL	(0x4)
	0x34, // REG_ANAMICL		(0x5)
	0x14, // REG_ANAMICR		(0x6)
	0x0a, // REG_AVADC_CTL		(0x7)
	0x00, // REG_ADCMICSEL		(0x8)
	0x00, // REG_DIGMIXING		(0x9)
	0x0c, // REG_ATXL1PGA		(0xA)
	0x0c, // REG_ATXR1PGA		(0xB)
	0x00, // REG_AVTXL2PGA		(0xC)
	0x00, // REG_AVTXR2PGA		(0xD)
	0x01, // REG_AUDIO_IF		(0xE)
	0x00, // REG_VOICE_IF		(0xF)
	0x00, // REG_ARXR1PGA		(0x10)
	0x00, // REG_ARXL1PGA		(0x11)
	0x6c, // REG_ARXR2PGA		(0x12)
	0x6c, // REG_ARXL2PGA		(0x13)
	0x00, // REG_VRXPGA		(0x14)
	0x00, // REG_VSTPGA		(0x15)
	0x00, // REG_VRX2ARXPGA		(0x16)
	0x0c, // REG_AVDAC_CTL		(0x17)
	0x00, // REG_ARX2VTXPGA		(0x18)
	0x00, // REG_ARXL1_APGA_CTL	(0x19)
	0x00, // REG_ARXR1_APGA_CTL	(0x1A)
	0x4b, // REG_ARXL2_APGA_CTL	(0x1B)
	0x4b, // REG_ARXR2_APGA_CTL	(0x1C)
	0x00, // REG_ATX2ARXPGA		(0x1D)
	0x00, // REG_BT_IF		(0x1E)
	0x00, // REG_BTPGA		(0x1F)
	0x00, // REG_BTSTPGA		(0x20)
	0x00, // REG_EAR_CTL		(0x21)
	0x24, // REG_HS_SEL		(0x22)
	0x0a, // REG_HS_GAIN_SET	(0x23)
	0x00, // REG_HS_POPN_SET	(0x24)
	0x00, // REG_PREDL_CTL		(0x25)
	0x00, // REG_PREDR_CTL		(0x26)
	0x00, // REG_PRECKL_CTL		(0x27)
	0x00, // REG_PRECKR_CTL		(0x28)
	0x00, // REG_HFL_CTL		(0x29)
	0x00, // REG_HFR_CTL		(0x2A)
	0x00, // REG_ALC_CTL		(0x2B)
	0x00, // REG_ALC_SET1		(0x2C)
	0x00, // REG_ALC_SET2		(0x2D)
	0x00, // REG_BOOST_CTL		(0x2E)
	0x01, // REG_SOFTVOL_CTL	(0x2F)
	0x00, // REG_DTMF_FREQSEL	(0x30)
	0x00, // REG_DTMF_TONEXT1H	(0x31)
	0x00, // REG_DTMF_TONEXT1L	(0x32)
	0x00, // REG_DTMF_TONEXT2H	(0x33)
	0x00, // REG_DTMF_TONEXT2L	(0x34)
	0x00, // REG_DTMF_TONOFF	(0x35)
	0x00, // REG_DTMF_WANONOFF	(0x36)
	0x00, // REG_I2S_RX_SCRAMBLE_H	(0x37)
	0x00, // REG_I2S_RX_SCRAMBLE_M	(0x38)
	0x00, // REG_I2S_RX_SCRAMBLE_L	(0x39)
	0x16, // REG_APLL_CTL		(0x3A)
	0x00, // REG_DTMF_CTL		(0x3B)
	0x00, // REG_DTMF_PGA_CTL2	(0x3C)
	0x00, // REG_DTMF_PGA_CTL1	(0x3D)
	0x00, // REG_MISC_SET_1		(0x3E)
	0x00, // REG_PCMBTMUX		(0x3F)
	0x00, // REG_RX_PATH_SEL	(0x43)
	0x00, // REG_VDL_APGA_CTL	(0x44)
	0x00, // REG_VIBRA_CTL		(0x45)
	0x00, // REG_VIBRA_SET		(0x46)
	0x00, // REG_VIBRA_PWM_SET	(0x47)
	0x00, // REG_ANAMIC_GAIN	(0x48)
	0x00, // REG_MISC_SET_2		(0x49)
};

static void twl4030_dump_registers(void)
{
	int i = 0;
	u8 data;

	printk(KERN_INFO "TWL 4030 Register dump for Audio Module\n");

	for (i = REG_CODEC_MODE; i <= REG_MISC_SET_2; i++) {
		twl4030_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE, &data, i);
		printk(KERN_INFO "Register[0x%02x]=0x%02x\n", i, data);
	}
}

struct twl4030_priv {
	unsigned int dummy;
};

/*
 * read twl4030 register cache
 */
static inline unsigned int twl4030_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u8 *cache = codec->reg_cache;

	return cache[reg];
}

/*
 * write twl4030 register cache
 */
static inline void twl4030_write_reg_cache(struct snd_soc_codec *codec,
						u8 reg, u8 value)
{
	u8 *cache = codec->reg_cache;

	if (reg >= TWL4030_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the twl4030 register space
 */
static int twl4030_write(struct snd_soc_codec *codec,
			unsigned int reg, unsigned int value)
{
	twl4030_write_reg_cache(codec, reg, value);
	return twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE, value, reg);
}

static void twl4030_init_chip(void)
{
	unsigned char byte;
	int i;

	twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
		twl4030_reg[REG_CODEC_MODE] & 0xfd, REG_CODEC_MODE);

	udelay(10); /* delay for power settling */

	for (i = REG_OPTION; i <= REG_MISC_SET_2; i++) {
		twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE, twl4030_reg[i], i);
	}

	twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
		twl4030_reg[REG_CODEC_MODE], REG_CODEC_MODE);

	udelay(10); /* delay for power settling */

	/* initiate offset cancellation */
	twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
		twl4030_reg[REG_ANAMICL] | 0x80, REG_ANAMICL);

	/* wait for offset cancellation to complete */
	twl4030_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE, &byte, REG_ANAMICL);
	while ((byte & 0x80) == 0x80)
		twl4030_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE, &byte, REG_ANAMICL);

	twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
		twl4030_reg[REG_MISC_SET_1] | 0x02, REG_MISC_SET_1);

}

static const struct snd_kcontrol_new twl4030_snd_controls[] = {
	SOC_DOUBLE_R("Master Playback Volume",
		 REG_ARXL2PGA, REG_ARXR2PGA,
		0, 127, 0),
	SOC_DOUBLE_R("Capture Volume",
		 REG_ATXL1PGA, REG_ATXR1PGA,
		0, 127, 0),
};

/* add non dapm controls */
static int twl4030_add_controls(struct snd_soc_codec *codec)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(twl4030_snd_controls); i++) {
		err = snd_ctl_add(codec->card,
				  snd_soc_cnew(&twl4030_snd_controls[i],
						codec, NULL));
		if (err < 0)
			return err;
	}

	return 0;
}

static const struct snd_soc_dapm_widget twl4030_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("INL"),
	SND_SOC_DAPM_INPUT("INR"),

	SND_SOC_DAPM_OUTPUT("OUTL"),
	SND_SOC_DAPM_OUTPUT("OUTR"),

	SND_SOC_DAPM_DAC("DACL", "Left Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DACR", "Right Playback", SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_ADC("ADCL", "Left Capture", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADCR", "Right Capture", SND_SOC_NOPM, 0, 0),
};

static const char *intercon[][3] = {
	/* outputs */
	{"OUTL", NULL, "DACL"},
	{"OUTR", NULL, "DACR"},

	/* inputs */
	{"ADCL", NULL, "INL"},
	{"ADCR", NULL, "INR"},

	/* terminator */
	{NULL, NULL, NULL},
};

static int twl4030_add_widgets(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(twl4030_dapm_widgets); i++)
		snd_soc_dapm_new_control(codec, &twl4030_dapm_widgets[i]);

	/* set up audio path interconnects */
	for (i = 0; intercon[i][0] != NULL; i++)
		snd_soc_dapm_connect_input(codec, intercon[i][0],
			intercon[i][1], intercon[i][2]);

	snd_soc_dapm_new_widgets(codec);
	return 0;
}

static int twl4030_dapm_event(struct snd_soc_codec *codec, int event)
{

	printk(KERN_INFO "TWL4030 Audio Codec dapm event\n");
	switch (event) {
	case SNDRV_CTL_POWER_D0: /* full On */
		break;
	case SNDRV_CTL_POWER_D1: /* partial On */
	case SNDRV_CTL_POWER_D2: /* partial On */
		break;
	case SNDRV_CTL_POWER_D3hot: /* off, with power */
		break;
	case SNDRV_CTL_POWER_D3cold: /* off, without power */
		break;
	}
	codec->dapm_state = event;

	return 0;
}

static void twl4030_power_up (struct snd_soc_codec *codec, u8 mode)
{
	u8 popn, hsgain;

	twl4030_write(codec, REG_CODEC_MODE, mode & ~CODECPDZ);
	twl4030_write(codec, REG_CODEC_MODE, mode | CODECPDZ);
	udelay(10);

	popn = twl4030_read_reg_cache(codec, REG_HS_POPN_SET);
	popn &= RAMP_DELAY;
	popn |=	VMID_EN | RAMP_DELAY_161MS;
	twl4030_write(codec, REG_HS_POPN_SET, popn);

	hsgain = HSR_GAIN_0DB| HSL_GAIN_0DB;
	twl4030_write(codec, REG_HS_GAIN_SET, hsgain);

	popn |= RAMP_EN;
	twl4030_write(codec, REG_HS_POPN_SET, popn);
}

static void twl4030_power_down (struct snd_soc_codec *codec)
{
	u8 popn, hsgain, mode;

	popn = twl4030_read_reg_cache(codec, REG_HS_POPN_SET);
	popn &= ~RAMP_EN;
	twl4030_write(codec, REG_HS_POPN_SET, popn);

	hsgain = HSR_GAIN_PWR_DOWN | HSL_GAIN_PWR_DOWN;
	twl4030_write(codec, REG_HS_GAIN_SET, hsgain);

	popn &= ~VMID_EN;
	twl4030_write(codec, REG_HS_POPN_SET, popn);

	mode = twl4030_read_reg_cache(codec, REG_CODEC_MODE);
	mode &= ~CODECPDZ;
	twl4030_write(codec, REG_CODEC_MODE, mode);
	udelay(10);
}


static int twl4030_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->codec;
	struct twl4030_priv *twl4030 = codec->private_data;
	u8 mode, old_mode, format, old_format;


	/* bit rate */
	old_mode = twl4030_read_reg_cache(codec, REG_CODEC_MODE) & ~CODECPDZ;
	mode = old_mode;
	mode &= ~APLL_RATE;
	switch (params_rate(params)) {
	case 44100:
		mode |= APLL_RATE_44100;
		break;
	case 48000:
		mode |= APLL_RATE_48000;
		break;
	default:
		printk(KERN_INFO "TWL4030 hw params: unknown rate %d\n", params_rate(params));
		return -EINVAL;
	}

	if (mode != old_mode) {
		/* change rate and turn codec back on */
		twl4030_write(codec, REG_CODEC_MODE, mode);
		mode |= CODECPDZ;
		twl4030_write(codec, REG_CODEC_MODE, mode);
	}

	/* sample size */
	old_format = twl4030_read_reg_cache(codec, REG_AUDIO_IF);
	format = old_format;
	format &= ~DATA_WIDTH;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		format |= DATA_WIDTH_16S_16W;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		format |= DATA_WIDTH_32S_24W;
		break;
	default:
		printk(KERN_INFO "TWL4030 hw params: unknown format %d\n", params_format(params));
		return -EINVAL;
	}

	if (format != old_format) {

		/* turn off codec before changing format */
		mode = twl4030_read_reg_cache(codec, REG_CODEC_MODE);
		mode &= ~CODECPDZ;
		twl4030_write(codec, REG_CODEC_MODE, mode);

		/* change format */
		twl4030_write(codec, REG_AUDIO_IF, format);
	
		/* turn on codec */
		mode |= CODECPDZ;
		twl4030_write(codec, REG_CODEC_MODE, mode);
	}
	return 0;
}

static int twl4030_mute(struct snd_soc_codec_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;

	u8 ldac_reg = twl4030_read_reg_cache(codec, REG_ARXL2PGA);
	u8 rdac_reg = twl4030_read_reg_cache(codec, REG_ARXR2PGA);

	if (mute) {
		/* printk(KERN_INFO "TWL4030 Audio Codec mute\n"); */
		twl4030_write(codec, REG_ARXL2PGA, 0x00);
		twl4030_write(codec, REG_ARXR2PGA, 0x00);
		twl4030_write_reg_cache(codec, REG_ARXL2PGA, ldac_reg);
		twl4030_write_reg_cache(codec, REG_ARXR2PGA, rdac_reg);
	}
	else {
		/* printk(KERN_INFO "TWL4030 Audio Codec unmute: %02x/%02x\n", ldac_reg, rdac_reg); */
		twl4030_write(codec, REG_ARXL2PGA, ldac_reg);
		twl4030_write(codec, REG_ARXR2PGA, rdac_reg);
	}

	return 0;
}

static int twl4030_set_dai_fmt(struct snd_soc_codec_dai *codec_dai,
			     unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct twl4030_priv *twl4030 = codec->private_data;
	u8 mode, old_format, format;

	/* get format */
	old_format = twl4030_read_reg_cache(codec, REG_AUDIO_IF);
	format = old_format;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		/* printk(KERN_INFO "TWL4030 set dai fmt: master\n"); */
		format &= ~(AIF_SLAVE_EN);
		format |= CLK256FS_EN;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		/* printk(KERN_INFO "TWL4030 set dai fmt: slave\n"); */
		format &= ~(CLK256FS_EN);
 		format |= AIF_SLAVE_EN;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	format &= ~AIF_FORMAT;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* printk(KERN_INFO "TWL4030 set dai fmt: i2s\n"); */
		format |= AIF_FORMAT_CODEC;
		break;
	default:
		return -EINVAL;
	}

	if (format != old_format) {

		/* turn off codec before changing format */
		mode = twl4030_read_reg_cache(codec, REG_CODEC_MODE);
		mode &= ~CODECPDZ;
		twl4030_write(codec, REG_CODEC_MODE, mode);

		/* change format */
		twl4030_write(codec, REG_AUDIO_IF, format);
	
		mode |= CODECPDZ;
		twl4030_write(codec, REG_CODEC_MODE, mode);
	}

	return 0;
}

#define TWL4030_RATES	 SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000
#define TWL4030_FORMATS	 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FORMAT_S24_LE

struct snd_soc_codec_dai twl4030_dai = {
	.name = "twl4030",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TWL4030_RATES,
		.formats = TWL4030_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TWL4030_RATES,
		.formats = TWL4030_FORMATS,},
	.ops = {
		.hw_params = twl4030_hw_params,
	},
	.dai_ops = {
		.digital_mute = twl4030_mute,
		.set_fmt = twl4030_set_dai_fmt,
	}
};

EXPORT_SYMBOL_GPL(twl4030_dai);

static int twl4030_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;

	printk(KERN_INFO "TWL4030 Audio Codec suspend\n");
	twl4030_dapm_event(codec, SNDRV_CTL_POWER_D3cold);

	return 0;
}

static int twl4030_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;
	int i;
	u16 *cache = codec->reg_cache;

	printk(KERN_INFO "TWL4030 Audio Codec resume\n");
	/* Sync reg_cache with the hardware */
	for (i = REG_CODEC_MODE; i <= REG_MISC_SET_2; i++) {
		twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE, cache[i], i);
	}
	twl4030_dapm_event(codec, SNDRV_CTL_POWER_D3hot);
	twl4030_dapm_event(codec, codec->suspend_dapm_state);
	return 0;
}

/*
 * initialize the driver
 * register the mixer and dsp interfaces with the kernel
 */

static int twl4030_init(struct snd_soc_device *socdev)
{
	struct snd_soc_codec *codec = socdev->codec;
	int ret = 0;

	printk(KERN_INFO "TWL4030 Audio Codec init \n");

	codec->name = "twl4030";
	codec->owner = THIS_MODULE;
	codec->read = twl4030_read_reg_cache;
	codec->write = twl4030_write;
	codec->dapm_event = twl4030_dapm_event;
	codec->dai = &twl4030_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = sizeof(twl4030_reg);
	codec->reg_cache = kmemdup(twl4030_reg, sizeof(twl4030_reg), GFP_KERNEL);
	if (codec->reg_cache == NULL)
		return -ENOMEM;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "twl4030: failed to create pcms\n");
		goto pcm_err;
	}

	twl4030_add_controls(codec);
	twl4030_add_widgets(codec);

	ret = snd_soc_register_card(socdev);
	if (ret < 0) {
		printk(KERN_ERR "twl4030: failed to register card\n");
		goto card_err;
	}

	twl4030_init_chip();
	twl4030_power_up(codec, APLL_RATE_44100 | OPT_MODE);

	return ret;

card_err:
	printk(KERN_INFO "TWL4030 Audio Codec init card error\n");
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	printk(KERN_INFO "TWL4030 Audio Codec init pcm error\n");
	kfree(codec->reg_cache);
	return ret;
}

static struct snd_soc_device *twl4030_socdev;

static int twl4030_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	struct twl4030_priv *twl4030;

	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	twl4030 = kzalloc(sizeof(struct twl4030_priv), GFP_KERNEL);
	if (twl4030 == NULL) {
		kfree(codec);
		return -ENOMEM;
	}

	codec->private_data = twl4030;
	socdev->codec = codec;
	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	twl4030_socdev = socdev;
	twl4030_init(socdev);

	return 0;
}

static int twl4030_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;

	printk(KERN_INFO "TWL4030 Audio Codec remove\n");
	kfree(codec->private_data);
	kfree(codec);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_twl4030 = {
	.probe = twl4030_probe,
	.remove = twl4030_remove,
	.suspend = twl4030_suspend,
	.resume = twl4030_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_twl4030);

MODULE_DESCRIPTION("ASoC TWL4030 codec driver");
MODULE_AUTHOR("Steve Sakoman");
MODULE_LICENSE("GPL");
