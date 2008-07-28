/*
 * ALSA SoC TWL4030 codec driver
 *
 * Author:      Steve Sakoman, <steve@sakoman.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __TWL4030_AUDIO_H__
#define __TWL4030_AUDIO_H__

#define REG_CODEC_MODE		0x1
#define REG_OPTION		0x2
#define REG_UNKNOWN		0x3
#define REG_MICBIAS_CTL		0x4
#define REG_ANAMICL		0x5
#define REG_ANAMICR		0x6
#define REG_AVADC_CTL		0x7
#define REG_ADCMICSEL		0x8
#define REG_DIGMIXING		0x9
#define REG_ATXL1PGA		0xA
#define REG_ATXR1PGA		0xB
#define REG_AVTXL2PGA		0xC
#define REG_AVTXR2PGA		0xD
#define REG_AUDIO_IF		0xE
#define REG_VOICE_IF		0xF
#define REG_ARXR1PGA		0x10
#define REG_ARXL1PGA		0x11
#define REG_ARXR2PGA		0x12
#define REG_ARXL2PGA		0x13
#define REG_VRXPGA		0x14
#define REG_VSTPGA		0x15
#define REG_VRX2ARXPGA		0x16
#define REG_AVDAC_CTL		0x17
#define REG_ARX2VTXPGA		0x18
#define REG_ARXL1_APGA_CTL	0x19
#define REG_ARXR1_APGA_CTL	0x1A
#define REG_ARXL2_APGA_CTL	0x1B
#define REG_ARXR2_APGA_CTL	0x1C
#define REG_ATX2ARXPGA		0x1D
#define REG_BT_IF		0x1E
#define REG_BTPGA		0x1F
#define REG_BTSTPGA		0x20
#define REG_EAR_CTL		0x21
#define REG_HS_SEL		0x22
#define REG_HS_GAIN_SET		0x23
#define REG_HS_POPN_SET		0x24
#define REG_PREDL_CTL		0x25
#define REG_PREDR_CTL		0x26
#define REG_PRECKL_CTL		0x27
#define REG_PRECKR_CTL		0x28
#define REG_HFL_CTL		0x29
#define REG_HFR_CTL		0x2A
#define REG_ALC_CTL		0x2B
#define REG_ALC_SET1		0x2C
#define REG_ALC_SET2		0x2D
#define REG_BOOST_CTL		0x2E
#define REG_SOFTVOL_CTL		0x2F
#define REG_DTMF_FREQSEL	0x30
#define REG_DTMF_TONEXT1H	0x31
#define REG_DTMF_TONEXT1L	0x32
#define REG_DTMF_TONEXT2H	0x33
#define REG_DTMF_TONEXT2L	0x34
#define REG_DTMF_TONOFF		0x35
#define REG_DTMF_WANONOFF	0x36
#define REG_I2S_RX_SCRAMBLE_H	0x37
#define REG_I2S_RX_SCRAMBLE_M	0x38
#define REG_I2S_RX_SCRAMBLE_L	0x39
#define REG_APLL_CTL		0x3A
#define REG_DTMF_CTL		0x3B
#define REG_DTMF_PGA_CTL2	0x3C
#define REG_DTMF_PGA_CTL1	0x3D
#define REG_MISC_SET_1		0x3E
#define REG_PCMBTMUX		0x3F
#define REG_RX_PATH_SEL		0x43
#define REG_VDL_APGA_CTL	0x44
#define REG_VIBRA_CTL		0x45
#define REG_VIBRA_SET		0x46
#define REG_VIBRA_PWM_SET	0x47
#define REG_ANAMIC_GAIN		0x48
#define REG_MISC_SET_2		0x49

#define TWL4030_CACHEREGNUM	REG_MISC_SET_2 + 1

/* Bitfield Definitions */

/* CODEC_MODE (0x01) Fields */

#define APLL_RATE		0xF0
#define APLL_RATE_8000		0x00
#define APLL_RATE_11025		0x10
#define APLL_RATE_12000		0x20
#define APLL_RATE_16000		0x40
#define APLL_RATE_22050		0x50
#define APLL_RATE_24000		0x60
#define APLL_RATE_32000		0x80
#define APLL_RATE_44100		0x90
#define APLL_RATE_48000		0xa0
#define SEL_16K			0x04
#define CODECPDZ		0x02
#define OPT_MODE		0x01

/* AUDIO_IF (0x0E) Fields */

#define AIF_SLAVE_EN		0x80
#define DATA_WIDTH		0x60
#define DATA_WIDTH_16S_16W	0x00
#define DATA_WIDTH_32S_16W	0x40
#define DATA_WIDTH_32S_24W	0x60
#define AIF_FORMAT		0x18
#define AIF_FORMAT_CODEC	0x00
#define AIF_FORMAT_LEFT		0x08
#define AIF_FORMAT_RIGHT	0x10
#define AIF_FORMAT_TDM		0x18
#define AIF_TRI_EN		0x04
#define CLK256FS_EN		0x02
#define AIF_EN			0x01

/* HS_GAIN_SET (0x23) Fields */

#define HSR_GAIN		0x0c
#define HSR_GAIN_PWR_DOWN	0x00
#define HSR_GAIN_PLUS_6DB	0x04
#define HSR_GAIN_0DB		0x08
#define HSR_GAIN_MINUS_6DB	0x0c
#define HSL_GAIN		0x0c
#define HSL_GAIN_PWR_DOWN	0x00
#define HSL_GAIN_PLUS_6DB	0x01
#define HSL_GAIN_0DB		0x02
#define HSL_GAIN_MINUS_6DB	0x03

/* HS_POPN_SET (0x24) Fields */

#define VMID_EN			0x40
#define	EXTMUTE			0x20
#define RAMP_DELAY		0x1C
#define RAMP_DELAY_20MS		0x00
#define RAMP_DELAY_40MS		0x04
#define RAMP_DELAY_81MS		0x08
#define RAMP_DELAY_161MS	0x0c
#define RAMP_DELAY_323MS	0x10
#define RAMP_DELAY_645MS	0x14
#define RAMP_DELAY_1291MS	0x18
#define RAMP_DELAY_2581MS	0x1c
#define RAMP_EN			0x02

extern struct snd_soc_codec_dai twl4030_dai;
extern struct snd_soc_codec_device soc_codec_dev_twl4030;

#endif	/* End of __TWL4030_AUDIO_H__ */
