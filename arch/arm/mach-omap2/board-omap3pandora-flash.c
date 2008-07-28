/*
 * board-omap3pandora-flash.c
 *
 * Copyright (c) 2008 Texas Instruments
 *
 * Modified from board-omap3evm-flash.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/types.h>
#include <linux/io.h>

#include <asm/mach/flash.h>
#include <asm/arch/board.h>
#include <asm/arch/gpmc.h>
#include <asm/arch/nand.h>

#define GPMC_CS0_BASE  0x60
#define GPMC_CS_SIZE   0x30

static struct mtd_partition omap3pandora_nand_partitions[] = {
	/* All the partition sizes are listed in terms of NAND block size */
	{
		.name		= "X-Loader",
		.offset		= 0,
		.size		= 4*(64 * 2048),
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size		= 15*(64 * 2048),
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot Env",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x260000 */
		.size		= 1*(64 * 2048),
	},
	{
		.name		= "Kernel",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x280000 */
		.size		= 32*(64 * 2048),
	},
	{
		.name		= "File System",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x680000 */
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct omap_nand_platform_data omap3pandora_nand_data = {
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


void __init omap3pandora_flash_init(void)
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
