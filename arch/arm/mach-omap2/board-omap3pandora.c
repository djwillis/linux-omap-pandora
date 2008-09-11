/*
 * linux/arch/arm/mach-omap2/board-omap3pandora.c (Pandora Handheld Console)
 *
 * Initial Pandora code: David-John Willis <source@distant-earth.com>
 *
 * Portions Copyright (C) 2008 Texas Instruments
 *
 * Modified from mach-omap2/board-omap3evm.c and mach-omap2/board-overo.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <asm/arch/gpio.h>
#include <asm/arch/keypad.h>
#include <asm/arch/board.h>
#include <asm/arch/usb-musb.h>
#include <asm/arch/usb-ehci.h>
#include <asm/arch/hsmmc.h>
#include <asm/arch/common.h>
#include <asm/arch/gpmc.h>
#include <asm/arch/nand.h>
#include <asm/arch/mcspi.h>

#include "sdram-micron-mt46h32m32lf-6.h"

#define GPMC_CS0_BASE  0x60
#define GPMC_CS_SIZE   0x30
#define NAND_BLOCK_SIZE                SZ_128K

static struct mtd_partition omap3pandora_nand_partitions[] = {
	/* All the partition sizes are listed in terms of NAND block size */
	{
		.name		= "X-Loader",
		.offset		= 0,
		.size		= 4 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE,		/* force read-only */
	},
	{
		.name		= "U-Boot",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size		= 15 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE,		/* force read-only */
	},
	{
		.name		= "U-Boot Env",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x260000 */
		.size		= 1 * NAND_BLOCK_SIZE,
	},
	{
		.name		= "Kernel",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x280000 */
		.size		= 32 * NAND_BLOCK_SIZE,
	},
	{
		.name		= "File System",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x680000 */
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct omap_nand_platform_data omap3pandora_nand_data = {
	.options	= NAND_BUSWIDTH_16,
	.parts		= omap3pandora_nand_partitions,
	.nr_parts	= ARRAY_SIZE(omap3pandora_nand_partitions),
	.dma_channel	= -1,		/* disable DMA in OMAP NAND driver */
	.nand_setup	= NULL,
	.dev_ready	= NULL,
};

static struct resource omap3pandora_nand_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device omap3pandora_nand_device = {
	.name		= "omap2-nand",
	.id		= -1,
	.dev		= {
		.platform_data	= &omap3pandora_nand_data,
	},
	.num_resources	= 1,
	.resource	= &omap3pandora_nand_resource,
};

static struct omap_uart_config omap3_pandora_uart_config __initdata = {
	.enabled_uarts	= ((1 << 0) | (1 << 1) | (1 << 2)),
};

static int __init omap3pandora_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, NULL, 0);
	omap_register_i2c_bus(2, 400, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static void __init omap3_pandora_init_irq(void)
{
	omap2_init_common_hw(mt46h32m32lf6_sdrc_params);
	omap_init_irq();
	omap_gpio_init();
}

static struct omap_mmc_config omap3pandora_mmc_config __initdata = {
	.mmc [0] = {
		.enabled	= 1,
		.wire4		= 1,
	},
	.mmc [1] = {
		.enabled	= 1,
		.wire4		= 1,
	},
};

static struct platform_device omap3_pandora_twl4030rtc_device = {
	.name           = "twl4030_rtc",
	.id             = -1,
};

static void ads7846_dev_init(void)
{
	if (omap_request_gpio(OMAP3_PANDORA_TS_GPIO) < 0)
		printk(KERN_ERR "Can't get ADS7846 Pen Down GPIO\n");

	omap_set_gpio_direction(OMAP3_PANDORA_TS_GPIO, 1);

	omap_set_gpio_debounce(OMAP3_PANDORA_TS_GPIO, 1);
	omap_set_gpio_debounce_time(OMAP3_PANDORA_TS_GPIO, 0xa);
}

static int ads7846_get_pendown_state(void)
{
	return !omap_get_gpio_datain(OMAP3_PANDORA_TS_GPIO);
}

struct ads7846_platform_data ads7846_config = {
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
};

static struct omap2_mcspi_device_config ads7846_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,  /* 0: slave, 1: master */
};

struct spi_board_info omap3pandora_spi_board_info[] = {
	[0] = {
		.modalias			= "ads7846",
		.bus_num			= 1,
		.chip_select		= 0,
		.max_speed_hz		= 1500000,
		.controller_data	= &ads7846_mcspi_config,
		.irq				= OMAP_GPIO_IRQ(OMAP3_PANDORA_TS_GPIO),
		.platform_data		= &ads7846_config,
	},
};

static int omap3_pandora_keymap[] = {
	KEY(0, 0, KEY_LEFT),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_A),
	KEY(0, 3, KEY_B),
	KEY(1, 0, KEY_DOWN),
	KEY(1, 1, KEY_UP),
	KEY(1, 2, KEY_E),
	KEY(1, 3, KEY_F),
	KEY(2, 0, KEY_ENTER),
	KEY(2, 1, KEY_I),
	KEY(2, 2, KEY_J),
	KEY(2, 3, KEY_K),
	KEY(3, 0, KEY_M),
	KEY(3, 1, KEY_N),
	KEY(3, 2, KEY_O),
	KEY(3, 3, KEY_P)
};

static struct omap_kp_platform_data omap3_pandora_kp_data = {
	.rows		= 4,
	.cols		= 4,
	.keymap 	= omap3_pandora_keymap,
	.keymapsize	= ARRAY_SIZE(omap3_pandora_keymap),
	.rep		= 1,
};

static struct platform_device omap3_pandora_kp_device = {
	.name		= "Pandora internal keyboard",
	.id		= -1,
	.dev		= {
					.platform_data = &omap3_pandora_kp_data,
				},
};

static struct platform_device omap3_pandora_lcd_device = {
	.name		= "omap3pandora_lcd",
	.id		= -1,
};

static struct omap_lcd_config omap3_pandora_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct gpio_led gpio_leds[] = {
	{
		.name			= "pandoraboard::led0",
		.default_trigger	= "mmc0",
		.gpio			= 128,
	},
	{
		.name			= "pandoraboard::led1",
		/* .default_trigger	= "mmc1", */
		.default_trigger	= "heartbeat",
		.gpio			= 129,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_led_info,
	},
};

static struct omap_board_config_kernel omap3_pandora_config[] __initdata = {
	{ OMAP_TAG_UART,	&omap3_pandora_uart_config },
	{ OMAP_TAG_MMC,		&omap3pandora_mmc_config },
	{ OMAP_TAG_LCD,		&omap3_pandora_lcd_config },
};

static struct platform_device *omap3_pandora_devices[] __initdata = {
	&omap3_pandora_lcd_device,
	&omap3_pandora_kp_device,
#ifdef CONFIG_RTC_DRV_TWL4030
	&omap3_pandora_twl4030rtc_device,
#endif
	&leds_gpio,
};

static void __init omap3pandora_flash_init(void)
{
	u8 cs = 0;
	u8 nandcs = GPMC_CS_NUM + 1;

	u32 gpmc_base_add = OMAP34XX_GPMC_VIRT;

	/* find out the chip-select on which NAND exists */
	while (cs < GPMC_CS_NUM) {
		u32 ret = 0;
		ret = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG1);

		if ((ret & 0xC00) == 0x800) {
			printk(KERN_INFO "Found NAND on CS%d\n", cs);
			if (nandcs > GPMC_CS_NUM)
				nandcs = cs;
		}
		cs++;
	}

	if (nandcs > GPMC_CS_NUM) {
		printk(KERN_INFO "NAND: Unable to find configuration "
				 "in GPMC\n ");
		return;
	}

	if (nandcs < GPMC_CS_NUM) {
		omap3pandora_nand_data.cs = nandcs;
		omap3pandora_nand_data.gpmc_cs_baseaddr = (void *)(gpmc_base_add +
			GPMC_CS0_BASE + nandcs * GPMC_CS_SIZE);
		omap3pandora_nand_data.gpmc_baseaddr = (void *) (gpmc_base_add);

		printk(KERN_INFO "Registering NAND on CS%d\n", nandcs);
		if (platform_device_register(&omap3pandora_nand_device) < 0)
			printk(KERN_ERR "Unable to register NAND device\n");
	}
}

static void __init omap3_pandora_init(void)
{
	omap3pandora_i2c_init();
	platform_add_devices(omap3_pandora_devices, ARRAY_SIZE(omap3_pandora_devices));
	omap_board_config = omap3_pandora_config;
	omap_board_config_size = ARRAY_SIZE(omap3_pandora_config);
	omap_serial_init();
	hsmmc_init();
	usb_musb_init();
	usb_ehci_init();
	ads7846_dev_init();
	omap3pandora_flash_init();

	/*
	if ((gpio_request(OMAP3_PANDORA_W2W_NRESET_GPIO,
		"OMAP3_PANDORA_W2W_NRESET_GPIO") == 0) &&
			(gpio_direction_output(OMAP3_PANDORA_W2W_NRESET_GPIO, 1) == 0)) {
				gpio_export(OMAP3_PANDORA_W2W_NRESET_GPIO, 0);
				gpio_set_value(OMAP3_PANDORA_W2W_NRESET_GPIO, 0);
				udelay(10);
				gpio_set_value(OMAP3_PANDORA_W2W_NRESET_GPIO, 1);
	} else {
                  printk(KERN_ERR "could not obtain gpio for OMAP3_PANDORA_W2W_NRESET_GPIO\n");
          }

          if ((gpio_request(OMAP3_PANDORA_BT_NRESET_GPIO, "OMAP3_PANDORA_BT_NRESET_GPIO") == 0) &&
              (gpio_direction_output(OMAP3_PANDORA_BT_NRESET_GPIO, 1) == 0)) {
                  gpio_export(OMAP3_PANDORA_BT_NRESET_GPIO, 0);
                  gpio_set_value(OMAP3_PANDORA_BT_NRESET_GPIO, 0);
                  mdelay(6);
                  gpio_set_value(OMAP3_PANDORA_BT_NRESET_GPIO, 1);
          } else {
                  printk(KERN_ERR "could not obtain gpio for OMAP3_PANDORA_BT_NRESET_GPIO\n");
          }

          if ((gpio_request(OMAP3_PANDORA_USBH_CPEN_GPIO, "OMAP3_PANDORA_USBH_CPEN_GPIO") == 0) &&
              (gpio_direction_output(OMAP3_PANDORA_USBH_CPEN_GPIO, 1) == 0))
                  gpio_export(OMAP3_PANDORA_USBH_CPEN_GPIO, 0);
          else
                  printk(KERN_ERR "could not obtain gpio for OMAP3_PANDORA_USBH_CPEN_GPIO\n");

          if ((gpio_request(OMAP3_PANDORA_USBH_NRESET_GPIO,
                            "OMAP3_PANDORA_USBH_NRESET_GPIO") == 0) &&
              (gpio_direction_output(OMAP3_PANDORA_USBH_NRESET_GPIO, 1) == 0))
                  gpio_export(OMAP3_PANDORA_USBH_NRESET_GPIO, 0);
          else
                  printk(KERN_ERR "could not obtain gpio for OMAP3_PANDORA_USBH_NRESET_GPIO\n");
	*/
}

static void __init omap3_pandora_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(OMAP3_PANDORA, "Pandora Handheld Console")
	/* Maintainer: John Willis - http://www.openpandora.org */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap3_pandora_map_io,
	.init_irq	= omap3_pandora_init_irq,
	.init_machine	= omap3_pandora_init,
	.timer		= &omap_timer,
MACHINE_END
