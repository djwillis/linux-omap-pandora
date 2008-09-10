/*
 * linux/arch/arm/mach-omap2/board-omap3pandora-hsmmc.c
 *
 * Author: David-John Willis
 *
 * This code is based on linux/arch/arm/mach-omap2/hsmmc.c, which is:
 *
 * Copyright (C) 2007-2008 Texas Instruments
 * Copyright (C) 2008 Nokia Corporation
 * Author: Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/i2c/twl4030.h>
#include <asm/hardware.h>
#include <asm/arch/mmc.h>
#include <asm/arch/board.h>
#include <asm/arch/gpio.h>

#if defined(CONFIG_MMC_OMAP_HS) || defined(CONFIG_MMC_OMAP_HS_MODULE)

#define VMMC1_DEV_GRP		0x27
#define P1_DEV_GRP		0x20
#define VMMC1_DEDICATED		0x2A
#define VSEL_3V			0x02
#define VSEL_18V		0x00
#define TWL_GPIO_PUPDCTR1	0x13
#define TWL_GPIO_IMR1A		0x1C
#define TWL_GPIO_ISR1A		0x19
#define LDO_CLR			0x00
#define VSEL_S2_CLR		0x40
#define GPIO_0_BIT_POS		(1 << 0)
#define GPIO_1_BIT_POS		(1 << 1)
#define MMC1_CD_IRQ		0
#define MMC2_CD_IRQ		1

#define VMMC2_DEV_GRP		0x2B
#define VMMC2_DEDICATED		0x2E

#define OMAP2_CONTROL_DEVCONF0	0x48002274
#define OMAP2_CONTROL_DEVCONF1	0x490022E8

#define OMAP2_CONTROL_DEVCONF0_LBCLK	(1 << 24)
#define OMAP2_CONTROL_DEVCONF1_ACTOV	(1 << 31)

#define OMAP2_CONTROL_PBIAS_VMODE	(1 << 0)
#define OMAP2_CONTROL_PBIAS_PWRDNZ	(1 << 1)
#define OMAP2_CONTROL_PBIAS_SCTRL	(1 << 2)

static const int slot_switch_gpio = 96;

static int hsmmc_card_detect(int irq)
{
	return twl4030_get_gpio_datain(irq - TWL4030_GPIO_IRQ_BASE);
}

/*
 * MMC Slot Initialization.
 */
static int hsmmc_late_init(struct device *dev)
{
	int ret = 0;

	/*
	 * Configure TWL4030 GPIO parameters for MMC hotplug irq
	 */
	ret = twl4030_request_gpio(MMC1_CD_IRQ);
	if (ret)
		goto err;

	ret = twl4030_set_gpio_edge_ctrl(MMC1_CD_IRQ,
			TWL4030_GPIO_EDGE_RISING | TWL4030_GPIO_EDGE_FALLING);
	if (ret)
		goto err;

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_GPIO, 0x02,
						TWL_GPIO_PUPDCTR1);
	if (ret)
		goto err;

	ret = twl4030_set_gpio_debounce(MMC1_CD_IRQ, TWL4030_GPIO_IS_ENABLE);
	if (ret)
		goto err;

	return ret;

err:
	dev_err(dev, "Failed to configure TWL4030 GPIO IRQ\n");
	return ret;
}

/*
 * MMC2 Slot Initialization.
 */
static int hsmmc2_late_init(struct device *dev) {
	int ret = 0;

	/*
	 * Configure TWL4030 GPIO parameters for MMC2 hotplug irq
	 */
	ret = twl4030_request_gpio(MMC2_CD_IRQ);
	if (ret != 0)
		goto err;

	ret = twl4030_set_gpio_edge_ctrl(MMC2_CD_IRQ,
			TWL4030_GPIO_EDGE_RISING | TWL4030_GPIO_EDGE_FALLING);
	if (ret != 0)
		goto err;

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_GPIO, 0x02,
						TWL_GPIO_PUPDCTR1);
	if (ret != 0)
		goto err;

	ret = twl4030_set_gpio_debounce(MMC2_CD_IRQ, TWL4030_GPIO_IS_ENABLE);
	if (ret != 0)
		goto err;

	return ret;
err:
	dev_err(dev, "Failed to configure TWL4030 GPIO IRQ\n");

	return ret;
}

static void hsmmc_cleanup(struct device *dev)
{
	int ret = 0;

	ret = twl4030_free_gpio(MMC1_CD_IRQ);
	if (ret)
		dev_err(dev, "Failed to configure TWL4030 GPIO IRQ\n");
}

static void hsmmc2_cleanup(struct device *dev) {
	int ret = 0;

	ret = twl4030_free_gpio(MMC2_CD_IRQ);
	if (ret != 0)
		dev_err(dev, "Failed to configure TWL4030 GPIO IRQ\n");
}


#ifdef CONFIG_PM

/*
 * To mask and unmask MMC Card Detect Interrupt
 * mask : 1
 * unmask : 0
 */
static int mask_cd_interrupt(int mask)
{
	u8 reg = 0, ret = 0;

	ret = twl4030_i2c_read_u8(TWL4030_MODULE_GPIO, &reg, TWL_GPIO_IMR1A);
	if (ret)
		goto err;

	reg = (mask == 1) ? (reg | GPIO_0_BIT_POS) : (reg & ~GPIO_0_BIT_POS);

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_GPIO, reg, TWL_GPIO_IMR1A);
	if (ret)
		goto err;

	ret = twl4030_i2c_read_u8(TWL4030_MODULE_GPIO, &reg, TWL_GPIO_ISR1A);
	if (ret)
		goto err;

	reg = (mask == 1) ? (reg | GPIO_0_BIT_POS) : (reg & ~GPIO_0_BIT_POS);

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_GPIO, reg, TWL_GPIO_ISR1A);
	if (ret)
		goto err;

err:
	return ret;
}

static int hsmmc_suspend(struct device *dev, int slot)
{
	int ret = 0;

	disable_irq(TWL4030_GPIO_IRQ_NO(MMC1_CD_IRQ));
	ret = mask_cd_interrupt(1);

	return ret;
}

/*
 * To mask and unmask MMC Card Detect Interrupt
 * mask : 1
 * unmask : 0
 */
static int mask_cd2_interrupt(int mask) {
	u8 reg = 0, ret = 0;

	ret = twl4030_i2c_read_u8(TWL4030_MODULE_GPIO, &reg, TWL_GPIO_IMR1A);
	if (ret != 0)
		goto err;

	reg = (mask == 1) ? (reg | GPIO_1_BIT_POS) : (reg & ~GPIO_1_BIT_POS);

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_GPIO, reg, TWL_GPIO_IMR1A);
	if (ret != 0)
		goto err;

	ret = twl4030_i2c_read_u8(TWL4030_MODULE_GPIO, &reg, TWL_GPIO_ISR1A);
	if (ret != 0)
		goto err;

	reg = (mask == 1) ? (reg | GPIO_1_BIT_POS) : (reg & ~GPIO_1_BIT_POS);

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_GPIO, reg, TWL_GPIO_ISR1A);
	if (ret != 0)
		goto err;
err:
	return ret;
}

static int hsmmc2_suspend(struct device *dev, int slot) {
	int ret = 0;

	disable_irq(TWL4030_GPIO_IRQ_NO(MMC2_CD_IRQ));
	ret = mask_cd2_interrupt(1);

	return ret;
}

static int hsmmc_resume(struct device *dev, int slot)
{
	int ret = 0;

	enable_irq(TWL4030_GPIO_IRQ_NO(MMC1_CD_IRQ));
	ret = mask_cd_interrupt(0);

	return ret;
}

static int hsmmc2_resume(struct device *dev, int slot) {
	int ret = 0;

	enable_irq(TWL4030_GPIO_IRQ_NO(MMC2_CD_IRQ));
	ret = mask_cd2_interrupt(0);

	return ret;
}

#endif

static int hsmmc_set_power(struct device *dev, int slot, int power_on,
				int vdd)
{
	u32 vdd_sel = 0, devconf = 0, reg = 0;
	int ret = 0;

	/* REVISIT: Using address directly till the control.h defines
	 * are settled.
	 */
#if defined(CONFIG_ARCH_OMAP2430)
	#define OMAP2_CONTROL_PBIAS 0x490024A0
#else
	#define OMAP2_CONTROL_PBIAS 0x48002520
#endif

	if (power_on) {
		if (cpu_is_omap24xx())
			devconf = omap_readl(OMAP2_CONTROL_DEVCONF1);
		else
			devconf = omap_readl(OMAP2_CONTROL_DEVCONF0);

		switch (1 << vdd) {
		case MMC_VDD_33_34:
		case MMC_VDD_32_33:
			vdd_sel = VSEL_3V;
			if (cpu_is_omap24xx())
				devconf |= OMAP2_CONTROL_DEVCONF1_ACTOV;
			break;
		case MMC_VDD_165_195:
			vdd_sel = VSEL_18V;
			if (cpu_is_omap24xx())
				devconf &= ~OMAP2_CONTROL_DEVCONF1_ACTOV;
		}

		if (cpu_is_omap24xx())
			omap_writel(devconf, OMAP2_CONTROL_DEVCONF1);
		else
			omap_writel(devconf | OMAP2_CONTROL_DEVCONF0_LBCLK,
				    OMAP2_CONTROL_DEVCONF0);

		reg = omap_readl(OMAP2_CONTROL_PBIAS);
		reg |= OMAP2_CONTROL_PBIAS_SCTRL;
		omap_writel(reg, OMAP2_CONTROL_PBIAS);

		reg = omap_readl(OMAP2_CONTROL_PBIAS);
		reg &= ~OMAP2_CONTROL_PBIAS_PWRDNZ;
		omap_writel(reg, OMAP2_CONTROL_PBIAS);

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						P1_DEV_GRP, VMMC1_DEV_GRP);
		if (ret)
			goto err;

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						vdd_sel, VMMC1_DEDICATED);
		if (ret)
			goto err;

		msleep(100);
		reg = omap_readl(OMAP2_CONTROL_PBIAS);
		reg |= (OMAP2_CONTROL_PBIAS_SCTRL |
			OMAP2_CONTROL_PBIAS_PWRDNZ);
		if (vdd_sel == VSEL_18V)
			reg &= ~OMAP2_CONTROL_PBIAS_VMODE;
		else
			reg |= OMAP2_CONTROL_PBIAS_VMODE;
		omap_writel(reg, OMAP2_CONTROL_PBIAS);

		return ret;

	} else {
		/* Power OFF */

		/* For MMC1, Toggle PBIAS before every power up sequence */
		reg = omap_readl(OMAP2_CONTROL_PBIAS);
		reg &= ~OMAP2_CONTROL_PBIAS_PWRDNZ;
		omap_writel(reg, OMAP2_CONTROL_PBIAS);

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						LDO_CLR, VMMC1_DEV_GRP);
		if (ret)
			goto err;

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						VSEL_S2_CLR, VMMC1_DEDICATED);
		if (ret)
			goto err;

		/* 100ms delay required for PBIAS configuration */
		msleep(100);
		reg = omap_readl(OMAP2_CONTROL_PBIAS);
		reg |= (OMAP2_CONTROL_PBIAS_VMODE |
			OMAP2_CONTROL_PBIAS_PWRDNZ |
			OMAP2_CONTROL_PBIAS_SCTRL);
		omap_writel(reg, OMAP2_CONTROL_PBIAS);
	}

	return 0;

err:
	return 1;
}

static int hsmmc2_set_power(struct device *dev, int slot, int power_on, int vdd)
{
	u32 vdd_sel = 0, devconf = 0;
	int ret = 0;

	if (power_on == 1) {
		if (cpu_is_omap24xx())
			devconf = omap_readl(0x490022E8);
		else
			devconf = omap_readl(0x480022D8);

		switch (1 << vdd) {
		case MMC_VDD_33_34:
		case MMC_VDD_32_33:
			vdd_sel = VSEL_3V;
			if (cpu_is_omap24xx())
				devconf = (devconf | (1 << 31));
			break;
		case MMC_VDD_165_195:
			vdd_sel = VSEL_18V;
			if (cpu_is_omap24xx())
				devconf = (devconf & ~(1 << 31));
		}

		if (cpu_is_omap24xx())
			omap_writel(devconf, 0x490022E8);
		else
			omap_writel(devconf | 1 << 6, 0x480022D8);

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						P1_DEV_GRP, VMMC2_DEV_GRP);
		if (ret != 0)
			goto err;

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						0x05, VMMC2_DEDICATED);
		if (ret != 0)
			goto err;

		return ret;

	} else if (power_on == 0) {

		/* Power OFF */
		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						LDO_CLR, VMMC2_DEV_GRP);
		if (ret != 0)
			goto err;

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						VSEL_S2_CLR, VMMC2_DEDICATED);
		if (ret != 0)
			goto err;
	} else {
		ret = -1;
		goto err;
	}

	return 0;
err:
	return 1;
}

static int hsmmc_switch_slot(struct device *dev, int slot)
{
#ifdef CONFIG_MMC_DEBUG
	dev_dbg(dev, "Choose slot %d\n", slot + 1);
#endif
	if (slot == 0)
		omap_set_gpio_dataout(slot_switch_gpio, 0);
	else
		omap_set_gpio_dataout(slot_switch_gpio, 1);
	return 0;
}

static struct omap_mmc_platform_data hsmmc_data = {
	.nr_slots			= 1,
	.switch_slot		= NULL,
	.init				= hsmmc_late_init,
	.cleanup			= hsmmc_cleanup,
#ifdef CONFIG_PM
	.suspend			= hsmmc_suspend,
	.resume				= hsmmc_resume,
#endif
	.slots[0] = {
		.set_power		= hsmmc_set_power,
		.set_bus_mode		= NULL,
		.get_ro			= NULL,
		.get_cover_state	= NULL,
		.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34 |
						MMC_VDD_165_195,
		.name			= "mmcblk1",

		.card_detect_irq        = TWL4030_GPIO_IRQ_NO(MMC1_CD_IRQ),
		.card_detect            = hsmmc_card_detect,
	},
};

static struct omap_mmc_platform_data hsmmc2_data = {
	.nr_slots			= 1,
	.switch_slot		= NULL,
	.init				= hsmmc2_late_init,
	.cleanup			= hsmmc2_cleanup,
#ifdef CONFIG_PM
	.suspend			= hsmmc2_suspend,
	.resume				= hsmmc2_resume,
#endif
	.slots[0] = {
		.set_power		= hsmmc2_set_power,
		.set_bus_mode		= NULL,
		.get_ro			= NULL,
		.get_cover_state	= NULL,
		.ocr_mask		= MMC_VDD_165_195,
		.name			= "mmcblk2",

		.card_detect_irq        = TWL4030_GPIO_IRQ_NO(MMC2_CD_IRQ),
		.card_detect            = hsmmc_card_detect,
	},
};

void __init hsmmc_init(void)
{
	omap_set_mmc_info(1, &hsmmc_data);
	omap_set_mmc_info(2, &hsmmc2_data);
}

#else

void __init hsmmc_init(void)
{

}

#endif
