/*
 * linux/arch/arm/mach-omap2/pm.c
 *
 * OMAP Power Management Common Routines
 *
 * Copyright (C) 2005 Texas Instruments, Inc.
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * Written by:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Tony Lindgren
 * Juha Yrjola
 * Amit Kucheria <amit.kucheria@nokia.com>
 * Igor Stoppa <igor.stoppa@nokia.com>
 * Jouni Hogander
 *
 * Based on pm.c for omap1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/suspend.h>
#include <linux/time.h>

#include <asm/arch/cpu.h>
#include <asm/mach/time.h>
#include <asm/atomic.h>

#include <asm/arch/pm.h>
#include "pm.h"

unsigned short enable_dyn_sleep;
unsigned short clocks_off_while_idle;
atomic_t sleep_block = ATOMIC_INIT(0);

static ssize_t idle_show(struct kobject *, struct kobj_attribute *, char *);
static ssize_t idle_store(struct kobject *k, struct kobj_attribute *,
			  const char *buf, size_t n);

static struct kobj_attribute sleep_while_idle_attr =
	__ATTR(sleep_while_idle, 0644, idle_show, idle_store);

static struct kobj_attribute clocks_off_while_idle_attr =
	__ATTR(clocks_off_while_idle, 0644, idle_show, idle_store);

static ssize_t idle_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	if (attr == &sleep_while_idle_attr)
		return sprintf(buf, "%hu\n", enable_dyn_sleep);
	else if (attr == &clocks_off_while_idle_attr)
		return sprintf(buf, "%hu\n", clocks_off_while_idle);
	else
		return -EINVAL;
}

static ssize_t idle_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t n)
{
	unsigned short value;

	if (sscanf(buf, "%hu", &value) != 1 ||
	    (value != 0 && value != 1)) {
		printk(KERN_ERR "idle_store: Invalid value\n");
		return -EINVAL;
	}

	if (attr == &sleep_while_idle_attr)
		enable_dyn_sleep = value;
	else if (attr == &clocks_off_while_idle_attr)
		clocks_off_while_idle = value;
	else
		return -EINVAL;

	return n;
}

void omap2_block_sleep(void)
{
	atomic_inc(&sleep_block);
}

void omap2_allow_sleep(void)
{
	int i;

	i = atomic_dec_return(&sleep_block);
	BUG_ON(i < 0);
}

static int __init omap_pm_init(void)
{
	int error = -1;

	if (cpu_is_omap24xx())
		error = omap2_pm_init();
	if (cpu_is_omap34xx())
		error = omap3_pm_init();
	if (error) {
		printk(KERN_ERR "omap2|3_pm_init failed: %d\n", error);
		return error;
	}

	/* disabled till drivers are fixed */
	enable_dyn_sleep = 0;
	error = sysfs_create_file(power_kobj, &sleep_while_idle_attr.attr);
	if (error)
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
	error = sysfs_create_file(power_kobj,
				  &clocks_off_while_idle_attr.attr);
	if (error)
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);

	return error;
}

late_initcall(omap_pm_init);
