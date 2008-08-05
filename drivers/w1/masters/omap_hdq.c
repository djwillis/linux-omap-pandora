/*
 * drivers/w1/masters/omap_hdq.c
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/hardware.h>

#include "../w1.h"
#include "../w1_int.h"

#define	MOD_NAME	"OMAP_HDQ:"

#define OMAP_HDQ_REVISION			0x00
#define OMAP_HDQ_TX_DATA			0x04
#define OMAP_HDQ_RX_DATA			0x08
#define OMAP_HDQ_CTRL_STATUS			0x0c
#define OMAP_HDQ_CTRL_STATUS_INTERRUPTMASK	(1<<6)
#define OMAP_HDQ_CTRL_STATUS_CLOCKENABLE	(1<<5)
#define OMAP_HDQ_CTRL_STATUS_GO			(1<<4)
#define OMAP_HDQ_CTRL_STATUS_INITIALIZATION	(1<<2)
#define OMAP_HDQ_CTRL_STATUS_DIR		(1<<1)
#define OMAP_HDQ_CTRL_STATUS_MODE		(1<<0)
#define OMAP_HDQ_INT_STATUS			0x10
#define OMAP_HDQ_INT_STATUS_TXCOMPLETE		(1<<2)
#define OMAP_HDQ_INT_STATUS_RXCOMPLETE		(1<<1)
#define OMAP_HDQ_INT_STATUS_TIMEOUT		(1<<0)
#define OMAP_HDQ_SYSCONFIG			0x14
#define OMAP_HDQ_SYSCONFIG_SOFTRESET		(1<<1)
#define OMAP_HDQ_SYSCONFIG_AUTOIDLE		(1<<0)
#define OMAP_HDQ_SYSSTATUS			0x18
#define OMAP_HDQ_SYSSTATUS_RESETDONE		(1<<0)

#define OMAP_HDQ_FLAG_CLEAR			0
#define OMAP_HDQ_FLAG_SET			1
#define OMAP_HDQ_TIMEOUT			(HZ/5)

#define OMAP_HDQ_MAX_USER			4

DECLARE_WAIT_QUEUE_HEAD(hdq_wait_queue);
int W1_ID;

struct hdq_data {
	resource_size_t		hdq_base;
	struct	semaphore	hdq_semlock;
	int			hdq_usecount;
	struct	clk		*hdq_ick;
	struct	clk		*hdq_fck;
	u8			hdq_irqstatus;
	spinlock_t		hdq_spinlock;
};

static struct hdq_data *hdq_data;

static int omap_hdq_get(void);
static int omap_hdq_put(void);
static int omap_hdq_break(void);

static int __init omap_hdq_probe(struct platform_device *pdev);
static int omap_hdq_remove(struct platform_device *pdev);

static struct platform_driver omap_hdq_driver = {
	.probe = omap_hdq_probe,
	.remove = omap_hdq_remove,
	.suspend = NULL,
	.resume = NULL,
	.driver = {
		.name = "omap_hdq",
	},
};

static u8 omap_w1_read_byte(void *data);
static void omap_w1_write_byte(void *data, u8 byte);
static u8 omap_w1_reset_bus(void *data);
static void omap_w1_search_bus(void *data, u8 search_type,
	w1_slave_found_callback slave_found);

static struct w1_bus_master omap_w1_master = {
	.read_byte	= omap_w1_read_byte,
	.write_byte	= omap_w1_write_byte,
	.reset_bus	= omap_w1_reset_bus,
	.search		= omap_w1_search_bus,
};

/*
 * HDQ register I/O routines
 */
static inline u8
hdq_reg_in(u32 offset)
{
	return omap_readb(hdq_data->hdq_base + offset);
}

static inline u8
hdq_reg_out(u32 offset, u8 val)
{
	omap_writeb(val, hdq_data->hdq_base + offset);
	return val;
}

static inline u8
hdq_reg_merge(u32 offset, u8 val, u8 mask)
{
	u8 new_val = (omap_readb(hdq_data->hdq_base + offset) & ~mask)
			| (val & mask);
	omap_writeb(new_val, hdq_data->hdq_base + offset);
	return new_val;
}

/*
 * Wait for one or more bits in flag change.
 * HDQ_FLAG_SET: wait until any bit in the flag is set.
 * HDQ_FLAG_CLEAR: wait until all bits in the flag are cleared.
 * return 0 on success and -ETIMEDOUT in the case of timeout.
 */
static int
hdq_wait_for_flag(u32 offset, u8 flag, u8 flag_set, u8 *status)
{
	int ret = 0;
	unsigned long timeout = jiffies + OMAP_HDQ_TIMEOUT;

	if (flag_set == OMAP_HDQ_FLAG_CLEAR) {
		/* wait for the flag clear */
		while (((*status = hdq_reg_in(offset)) & flag)
			&& time_before(jiffies, timeout)) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		}
		if (unlikely(*status & flag))
			ret = -ETIMEDOUT;
	} else if (flag_set == OMAP_HDQ_FLAG_SET) {
		/* wait for the flag set */
		while (!((*status = hdq_reg_in(offset)) & flag)
			&& time_before(jiffies, timeout)) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		}
		if (unlikely(!(*status & flag)))
			ret = -ETIMEDOUT;
	} else
		return -EINVAL;

	return ret;
}

/*
 * write out a byte and fill *status with HDQ_INT_STATUS
 */
static int
hdq_write_byte(u8 val, u8 *status)
{
	int ret;
	u8 tmp_status;
	unsigned long irqflags;

	*status = 0;

	spin_lock_irqsave(&hdq_data->hdq_spinlock, irqflags);
	/* clear interrupt flags via a dummy read */
	hdq_reg_in(OMAP_HDQ_INT_STATUS);
	/* ISR loads it with new INT_STATUS */
	hdq_data->hdq_irqstatus = 0;
	spin_unlock_irqrestore(&hdq_data->hdq_spinlock, irqflags);

	hdq_reg_out(OMAP_HDQ_TX_DATA, val);

	/* set the GO bit */
	hdq_reg_merge(OMAP_HDQ_CTRL_STATUS, OMAP_HDQ_CTRL_STATUS_GO,
		OMAP_HDQ_CTRL_STATUS_DIR | OMAP_HDQ_CTRL_STATUS_GO);
	/* wait for the TXCOMPLETE bit */
	ret = wait_event_interruptible_timeout(hdq_wait_queue,
		hdq_data->hdq_irqstatus, OMAP_HDQ_TIMEOUT);
	if (unlikely(ret < 0)) {
		pr_debug("wait interrupted");
		return -EINTR;
	}

	spin_lock_irqsave(&hdq_data->hdq_spinlock, irqflags);
	*status = hdq_data->hdq_irqstatus;
	spin_unlock_irqrestore(&hdq_data->hdq_spinlock, irqflags);
	/* check irqstatus */
	if (!(*status & OMAP_HDQ_INT_STATUS_TXCOMPLETE)) {
		pr_debug("timeout waiting for TXCOMPLETE/RXCOMPLETE, %x",
			*status);
		return -ETIMEDOUT;
	}

	/* wait for the GO bit return to zero */
	ret = hdq_wait_for_flag(OMAP_HDQ_CTRL_STATUS, OMAP_HDQ_CTRL_STATUS_GO,
		OMAP_HDQ_FLAG_CLEAR, &tmp_status);
	if (ret) {
		pr_debug("timeout waiting GO bit return to zero, %x",
			tmp_status);
		return ret;
	}

	return ret;
}

/*
 * HDQ Interrupt service routine.
 */
static irqreturn_t
hdq_isr(int irq, void *arg)
{
	unsigned long irqflags;

	spin_lock_irqsave(&hdq_data->hdq_spinlock, irqflags);
	hdq_data->hdq_irqstatus = hdq_reg_in(OMAP_HDQ_INT_STATUS);
	spin_unlock_irqrestore(&hdq_data->hdq_spinlock, irqflags);
	pr_debug("hdq_isr: %x", hdq_data->hdq_irqstatus);

	if (hdq_data->hdq_irqstatus &
		(OMAP_HDQ_INT_STATUS_TXCOMPLETE | OMAP_HDQ_INT_STATUS_RXCOMPLETE
		| OMAP_HDQ_INT_STATUS_TIMEOUT)) {
		/* wake up sleeping process */
		wake_up_interruptible(&hdq_wait_queue);
	}

	return IRQ_HANDLED;
}

/*
 * HDQ Mode: always return success.
 */
static u8 omap_w1_reset_bus(void *data)
{
	return 0;
}

/*
 * W1 search callback function.
 */
static void omap_w1_search_bus(void *data, u8 search_type,
	w1_slave_found_callback slave_found)
{
	u64 module_id, rn_le, cs, id;

	if (W1_ID)
		module_id = W1_ID;
	else
		module_id = 0x1;

	rn_le = cpu_to_le64(module_id);
	/*
	 * HDQ might not obey truly the 1-wire spec.
	 * So calculate CRC based on module parameter.
	 */
	cs = w1_calc_crc8((u8 *)&rn_le, 7);
	id = (cs << 56) | module_id;

	slave_found(data, id);
}

static int
_omap_hdq_reset(void)
{
	int ret;
	u8 tmp_status;

	hdq_reg_out(OMAP_HDQ_SYSCONFIG, OMAP_HDQ_SYSCONFIG_SOFTRESET);
	/*
	 * Select HDQ mode & enable clocks.
	 * It is observed that INT flags can't be cleared via a read and GO/INIT
	 * won't return to zero if interrupt is disabled. So we always enable
	 * interrupt.
	 */
	hdq_reg_out(OMAP_HDQ_CTRL_STATUS,
		OMAP_HDQ_CTRL_STATUS_CLOCKENABLE |
		OMAP_HDQ_CTRL_STATUS_INTERRUPTMASK);

	/* wait for reset to complete */
	ret = hdq_wait_for_flag(OMAP_HDQ_SYSSTATUS,
		OMAP_HDQ_SYSSTATUS_RESETDONE, OMAP_HDQ_FLAG_SET, &tmp_status);
	if (ret)
		pr_debug("timeout waiting HDQ reset, %x", tmp_status);
	else {
		hdq_reg_out(OMAP_HDQ_CTRL_STATUS,
			OMAP_HDQ_CTRL_STATUS_CLOCKENABLE |
			OMAP_HDQ_CTRL_STATUS_INTERRUPTMASK);
		hdq_reg_out(OMAP_HDQ_SYSCONFIG, OMAP_HDQ_SYSCONFIG_AUTOIDLE);
	}

	return ret;
}

/*
 * Issue break pulse to the device.
 */
static int
omap_hdq_break()
{
	int ret;
	u8 tmp_status;
	unsigned long irqflags;

	ret = down_interruptible(&hdq_data->hdq_semlock);
	if (ret < 0)
		return -EINTR;

	if (!hdq_data->hdq_usecount) {
		up(&hdq_data->hdq_semlock);
		return -EINVAL;
	}

	spin_lock_irqsave(&hdq_data->hdq_spinlock, irqflags);
	/* clear interrupt flags via a dummy read */
	hdq_reg_in(OMAP_HDQ_INT_STATUS);
	/* ISR loads it with new INT_STATUS */
	hdq_data->hdq_irqstatus = 0;
	spin_unlock_irqrestore(&hdq_data->hdq_spinlock, irqflags);

	/* set the INIT and GO bit */
	hdq_reg_merge(OMAP_HDQ_CTRL_STATUS,
		OMAP_HDQ_CTRL_STATUS_INITIALIZATION | OMAP_HDQ_CTRL_STATUS_GO,
		OMAP_HDQ_CTRL_STATUS_DIR | OMAP_HDQ_CTRL_STATUS_INITIALIZATION |
		OMAP_HDQ_CTRL_STATUS_GO);

	/* wait for the TIMEOUT bit */
	ret = wait_event_interruptible_timeout(hdq_wait_queue,
		hdq_data->hdq_irqstatus, OMAP_HDQ_TIMEOUT);
	if (unlikely(ret < 0)) {
		pr_debug("wait interrupted");
		up(&hdq_data->hdq_semlock);
		return -EINTR;
	}

	spin_lock_irqsave(&hdq_data->hdq_spinlock, irqflags);
	tmp_status = hdq_data->hdq_irqstatus;
	spin_unlock_irqrestore(&hdq_data->hdq_spinlock, irqflags);
	/* check irqstatus */
	if (!(tmp_status & OMAP_HDQ_INT_STATUS_TIMEOUT)) {
		pr_debug("timeout waiting for TIMEOUT, %x", tmp_status);
		up(&hdq_data->hdq_semlock);
		return -ETIMEDOUT;
	}
	/*
	 * wait for both INIT and GO bits rerurn to zero.
	 * zero wait time expected for interrupt mode.
	 */
	ret = hdq_wait_for_flag(OMAP_HDQ_CTRL_STATUS,
			OMAP_HDQ_CTRL_STATUS_INITIALIZATION |
			OMAP_HDQ_CTRL_STATUS_GO, OMAP_HDQ_FLAG_CLEAR,
			&tmp_status);
	if (ret)
		pr_debug("timeout waiting INIT&GO bits return to zero, %x",
			tmp_status);

	up(&hdq_data->hdq_semlock);
	return ret;
}

static int hdq_read_byte(u8 *val)
{
	int ret;
	u8 status;
	unsigned long irqflags;

	ret = down_interruptible(&hdq_data->hdq_semlock);
	if (ret < 0)
		return -EINTR;

	if (!hdq_data->hdq_usecount) {
		up(&hdq_data->hdq_semlock);
		return -EINVAL;
	}

	if (!(hdq_data->hdq_irqstatus & OMAP_HDQ_INT_STATUS_RXCOMPLETE)) {
		hdq_reg_merge(OMAP_HDQ_CTRL_STATUS,
			OMAP_HDQ_CTRL_STATUS_DIR | OMAP_HDQ_CTRL_STATUS_GO,
			OMAP_HDQ_CTRL_STATUS_DIR | OMAP_HDQ_CTRL_STATUS_GO);
		/*
		 * The RX comes immediately after TX. It
		 * triggers another interrupt before we
		 * sleep. So we have to wait for RXCOMPLETE bit.
		 */
		{
			unsigned long timeout = jiffies + OMAP_HDQ_TIMEOUT;
			while (!(hdq_data->hdq_irqstatus
				& OMAP_HDQ_INT_STATUS_RXCOMPLETE)
				&& time_before(jiffies, timeout)) {
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(1);
			}
		}
		hdq_reg_merge(OMAP_HDQ_CTRL_STATUS, 0,
			OMAP_HDQ_CTRL_STATUS_DIR);
		spin_lock_irqsave(&hdq_data->hdq_spinlock, irqflags);
		status = hdq_data->hdq_irqstatus;
		spin_unlock_irqrestore(&hdq_data->hdq_spinlock, irqflags);
		/* check irqstatus */
		if (!(status & OMAP_HDQ_INT_STATUS_RXCOMPLETE)) {
			pr_debug("timeout waiting for RXCOMPLETE, %x", status);
			up(&hdq_data->hdq_semlock);
			return -ETIMEDOUT;
		}
	}
	/* the data is ready. Read it in! */
	*val = hdq_reg_in(OMAP_HDQ_RX_DATA);
	up(&hdq_data->hdq_semlock);

	return 0;

}

/*
 * Enable clocks and set the controller to HDQ mode.
 */
static int
omap_hdq_get()
{
	int ret = 0;

	ret = down_interruptible(&hdq_data->hdq_semlock);
	if (ret < 0)
		return -EINTR;

	if (OMAP_HDQ_MAX_USER == hdq_data->hdq_usecount) {
		pr_debug("attempt to exceed the max use count");
		up(&hdq_data->hdq_semlock);
		ret = -EINVAL;
	} else {
		hdq_data->hdq_usecount++;
		try_module_get(THIS_MODULE);
		if (1 == hdq_data->hdq_usecount) {
			if (clk_enable(hdq_data->hdq_ick)) {
				pr_debug("Can not enable ick\n");
				clk_put(hdq_data->hdq_ick);
				clk_put(hdq_data->hdq_fck);
				up(&hdq_data->hdq_semlock);
				return -ENODEV;
			}
			if (clk_enable(hdq_data->hdq_fck)) {
				pr_debug("Can not enable fck\n");
				clk_put(hdq_data->hdq_ick);
				clk_put(hdq_data->hdq_fck);
				up(&hdq_data->hdq_semlock);
				return -ENODEV;
			}

			/* make sure HDQ is out of reset */
			if (!(hdq_reg_in(OMAP_HDQ_SYSSTATUS) &
				OMAP_HDQ_SYSSTATUS_RESETDONE)) {
				ret = _omap_hdq_reset();
				if (ret)
					/* back up the count */
					hdq_data->hdq_usecount--;
			} else {
				/* select HDQ mode & enable clocks */
				hdq_reg_out(OMAP_HDQ_CTRL_STATUS,
					OMAP_HDQ_CTRL_STATUS_CLOCKENABLE |
					OMAP_HDQ_CTRL_STATUS_INTERRUPTMASK);
				hdq_reg_out(OMAP_HDQ_SYSCONFIG,
					OMAP_HDQ_SYSCONFIG_AUTOIDLE);
				hdq_reg_in(OMAP_HDQ_INT_STATUS);
			}
		}
	}
	up(&hdq_data->hdq_semlock);
	return ret;
}

/*
 * Disable clocks to the module.
 */
static int
omap_hdq_put()
{
	int ret = 0;

	ret = down_interruptible(&hdq_data->hdq_semlock);
	if (ret < 0)
		return -EINTR;

	if (0 == hdq_data->hdq_usecount) {
		pr_debug("attempt to decrement use count when it is zero");
		ret = -EINVAL;
	} else {
		hdq_data->hdq_usecount--;
		module_put(THIS_MODULE);
		if (0 == hdq_data->hdq_usecount) {
			clk_disable(hdq_data->hdq_ick);
			clk_disable(hdq_data->hdq_fck);
		}
	}
	up(&hdq_data->hdq_semlock);
	return ret;
}

/*
 * Used to control the call to omap_hdq_get and omap_hdq_put.
 * HDQ Protocol: Write the CMD|REG_address first, followed by
 * the data wrire or read.
 */
static int init_trans;

/*
 * Read a byte of data from the device.
 */
static u8 omap_w1_read_byte(void *data)
{
	u8 val;
	int ret;

	ret = hdq_read_byte(&val);
	if (ret) {
		init_trans = 0;
		omap_hdq_put();
		return -1;
	}

	/* Write followed by a read, release the module */
	if (init_trans) {
		init_trans = 0;
		omap_hdq_put();
	}

	return val;
}

/*
 * Write a byte of data to the device.
 */
static void omap_w1_write_byte(void *data, u8 byte)
{
	u8 status;

	/* First write to initialize the transfer */
	if (init_trans == 0)
		omap_hdq_get();

	init_trans++;

	hdq_write_byte(byte, &status);
	pr_debug("Ctrl status %x\n", status);

	/* Second write, data transfered. Release the module */
	if (init_trans > 1) {
		omap_hdq_put();
		init_trans = 0;
	}

	return;
}

static int __init omap_hdq_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret, irq;
	u8 rev;

	if (!pdev)
		return -ENODEV;

	hdq_data = kmalloc(sizeof(*hdq_data), GFP_KERNEL);
	if (!hdq_data)
		return -ENODEV;

	platform_set_drvdata(pdev, hdq_data);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		platform_set_drvdata(pdev, NULL);
		kfree(hdq_data);
		return -ENXIO;
	}

	hdq_data->hdq_base = res->start;

	/* get interface & functional clock objects */
	hdq_data->hdq_ick = clk_get(&pdev->dev, "hdq_ick");
	hdq_data->hdq_fck = clk_get(&pdev->dev, "hdq_fck");

	if (IS_ERR(hdq_data->hdq_ick) || IS_ERR(hdq_data->hdq_fck)) {
		pr_debug("Can't get HDQ clock objects\n");
		if (IS_ERR(hdq_data->hdq_ick)) {
			ret = PTR_ERR(hdq_data->hdq_ick);
			platform_set_drvdata(pdev, NULL);
			kfree(hdq_data);
			return ret;
		}
		if (IS_ERR(hdq_data->hdq_fck)) {
			ret = PTR_ERR(hdq_data->hdq_fck);
			platform_set_drvdata(pdev, NULL);
			kfree(hdq_data);
			return ret;
		}
	}

	hdq_data->hdq_usecount = 0;
	sema_init(&hdq_data->hdq_semlock, 1);

	if (clk_enable(hdq_data->hdq_ick)) {
		pr_debug("Can not enable ick\n");
		clk_put(hdq_data->hdq_ick);
		clk_put(hdq_data->hdq_fck);
		platform_set_drvdata(pdev, NULL);
		kfree(hdq_data);
		return -ENODEV;
	}

	if (clk_enable(hdq_data->hdq_fck)) {
		pr_debug("Can not enable fck\n");
		clk_disable(hdq_data->hdq_ick);
		clk_put(hdq_data->hdq_ick);
		clk_put(hdq_data->hdq_fck);
		platform_set_drvdata(pdev, NULL);
		kfree(hdq_data);
		return -ENODEV;
	}

	rev = hdq_reg_in(OMAP_HDQ_REVISION);
	pr_info("OMAP HDQ Hardware Revision %c.%c. Driver in %s mode.\n",
		(rev >> 4) + '0', (rev & 0x0f) + '0', "Interrupt");

	spin_lock_init(&hdq_data->hdq_spinlock);
	omap_hdq_break();

	irq = platform_get_irq(pdev, 0);
	if (irq	< 0) {
		platform_set_drvdata(pdev, NULL);
		kfree(hdq_data);
		return -ENXIO;
	}

	if (request_irq(irq, hdq_isr, IRQF_DISABLED, "OMAP HDQ",
		&hdq_data->hdq_semlock)) {
		pr_debug("request_irq failed\n");
		clk_disable(hdq_data->hdq_ick);
		clk_put(hdq_data->hdq_ick);
		clk_put(hdq_data->hdq_fck);
		platform_set_drvdata(pdev, NULL);
		kfree(hdq_data);
		return -ENODEV;
	}

	/* don't clock the HDQ until it is needed */
	clk_disable(hdq_data->hdq_ick);
	clk_disable(hdq_data->hdq_fck);

	ret = w1_add_master_device(&omap_w1_master);
	if (ret) {
		pr_debug("Failure in registering w1 master\n");
		clk_put(hdq_data->hdq_ick);
		clk_put(hdq_data->hdq_fck);
		platform_set_drvdata(pdev, NULL);
		kfree(hdq_data);
		return ret;
	}

	return 0;
}

static int omap_hdq_remove(struct platform_device *pdev)
{
	down_interruptible(&hdq_data->hdq_semlock);
	if (0 != hdq_data->hdq_usecount) {
		pr_debug("removed when use count is not zero\n");
		return -EBUSY;
	}
	up(&hdq_data->hdq_semlock);

	/* remove module dependency */
	clk_put(hdq_data->hdq_ick);
	clk_put(hdq_data->hdq_fck);
	free_irq(INT_24XX_HDQ_IRQ, &hdq_data->hdq_semlock);
	platform_set_drvdata(pdev, NULL);
	kfree(hdq_data);

	return 0;
}

static int __init
omap_hdq_init(void)
{
	return platform_driver_register(&omap_hdq_driver);
}

static void __exit
omap_hdq_exit(void)
{
	platform_driver_unregister(&omap_hdq_driver);
}

module_init(omap_hdq_init);
module_exit(omap_hdq_exit);

module_param(W1_ID, int, S_IRUSR);

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("HDQ driver Library");
MODULE_LICENSE("GPL");
