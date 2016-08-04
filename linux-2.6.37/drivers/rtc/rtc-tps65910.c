/*
 * rtc-tps65910.c -- TPS65910 Real Time Clock interface
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 * Author: Venu Byravarasu <vbyravarasu@nvidia.com>
 *
 * Based on original TI driver rtc-twl.c
 *   Copyright (C) 2007 MontaVista Software, Inc
 *   Author: Alexandre Rusev <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#include <linux/mfd/tps65910.h>

struct tps65910_rtc {
	struct rtc_device	*rtc;
	int irq;
};

/* Total number of RTC registers needed to set time*/
#define NUM_TIME_REGS	(TPS65910_YEARS - TPS65910_SECONDS + 1)

static int tps65910_rtc_alarm_irq_enable(struct device *dev, unsigned enabled)
{
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	u8 val = 0;

	if (enabled)
		val = TPS65910_RTC_INTERRUPTS_IT_ALARM;

	return tps->write(tps, TPS65910_RTC_INTERRUPTS, 1, &val);
}

/*
 * Gets current tps65910 RTC time and date parameters.
 *
 * The RTC's time/alarm representation is not what gmtime(3) requires
 * Linux to use:
 *
 *  - Months are 1..12 vs Linux 0-11
 *  - Years are 0..99 vs Linux 1900..N (we assume 21st century)
 */
static int tps65910_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char rtc_data[NUM_TIME_REGS];
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	int ret;

	/* Copy RTC counting registers to static registers or latches */
	ret = tps65910_set_bits(tps, TPS65910_RTC_CTRL,
		TPS65910_RTC_CTRL_GET_TIME);
	if (ret < 0) {
		dev_err(dev, "RTC CTRL reg update failed with err:%d\n", ret);
		return ret;
	}

	ret = tps->read(tps, TPS65910_SECONDS, NUM_TIME_REGS, rtc_data);
	if (ret < 0) {
		dev_err(dev, "reading from RTC failed with err:%d\n", ret);
		return ret;
	}

	tm->tm_sec = bcd2bin(rtc_data[0]);
	tm->tm_min = bcd2bin(rtc_data[1]);
	tm->tm_hour = bcd2bin(rtc_data[2]);
	tm->tm_mday = bcd2bin(rtc_data[3]);
	tm->tm_mon = bcd2bin(rtc_data[4]) - 1;
	tm->tm_year = bcd2bin(rtc_data[5]) + 100;

	return ret;
}

static int tps65910_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char rtc_data[NUM_TIME_REGS];
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	int ret;

	rtc_data[0] = bin2bcd(tm->tm_sec);
	rtc_data[1] = bin2bcd(tm->tm_min);
	rtc_data[2] = bin2bcd(tm->tm_hour);
	rtc_data[3] = bin2bcd(tm->tm_mday);
	rtc_data[4] = bin2bcd(tm->tm_mon + 1);
	rtc_data[5] = bin2bcd(tm->tm_year - 100);

	/* Stop RTC while updating the RTC time registers */
	ret = tps65910_clear_bits(tps, TPS65910_RTC_CTRL,
		~TPS65910_RTC_CTRL_STOP_RTC);
	if (ret < 0) {
		dev_err(dev, "RTC stop failed with err:%d\n", ret);
		return ret;
	}

	/* update all the time registers in one shot */
	ret = tps->write(tps, TPS65910_SECONDS, NUM_TIME_REGS, rtc_data);
	if (ret < 0) {
		dev_err(dev, "rtc_set_time error %d\n", ret);
		return ret;
	}

	/* Start back RTC */
	ret = tps65910_set_bits(tps, TPS65910_RTC_CTRL,
		TPS65910_RTC_CTRL_STOP_RTC);
	if (ret < 0)
		dev_err(dev, "RTC start failed with err:%d\n", ret);

	return ret;
}

/*
 * Gets current tps65910 RTC alarm time.
 */
static int tps65910_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char alarm_data[NUM_TIME_REGS];
	u32 int_val;
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	int ret;

	ret = tps->read(tps, TPS65910_SECONDS, NUM_TIME_REGS, alarm_data);
	if (ret < 0) {
		dev_err(dev, "rtc_read_alarm error %d\n", ret);
		return ret;
	}

	alm->time.tm_sec = bcd2bin(alarm_data[0]);
	alm->time.tm_min = bcd2bin(alarm_data[1]);
	alm->time.tm_hour = bcd2bin(alarm_data[2]);
	alm->time.tm_mday = bcd2bin(alarm_data[3]);
	alm->time.tm_mon = bcd2bin(alarm_data[4]) - 1;
	alm->time.tm_year = bcd2bin(alarm_data[5]) + 100;

	ret = tps->read(tps, TPS65910_RTC_INTERRUPTS, 1, &int_val);
	if (ret < 0)
		return ret;

	if (int_val & TPS65910_RTC_INTERRUPTS_IT_ALARM)
		alm->enabled = 1;

	return ret;
}

static int tps65910_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char alarm_data[NUM_TIME_REGS];
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	int ret;

	ret = tps65910_rtc_alarm_irq_enable(dev, 0);
	if (ret)
		return ret;

	alarm_data[0] = bin2bcd(alm->time.tm_sec);
	alarm_data[1] = bin2bcd(alm->time.tm_min);
	alarm_data[2] = bin2bcd(alm->time.tm_hour);
	alarm_data[3] = bin2bcd(alm->time.tm_mday);
	alarm_data[4] = bin2bcd(alm->time.tm_mon + 1);
	alarm_data[5] = bin2bcd(alm->time.tm_year - 100);

	/* update all the alarm registers in one shot */
	ret = tps->write(tps, TPS65910_ALARM_SECONDS, NUM_TIME_REGS,
		alarm_data);
	if (ret) {
		dev_err(dev, "rtc_set_alarm error %d\n", ret);
		return ret;
	}

	if (alm->enabled)
		ret = tps65910_rtc_alarm_irq_enable(dev, 1);

	return ret;
}

#if 0
static irqreturn_t tps65910_rtc_interrupt(int irq, void *rtc)
{
	struct device *dev = rtc;
	unsigned long events = 0;
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	struct tps65910_rtc *tps_rtc = dev_get_drvdata(dev);
	int ret;
	u32 rtc_reg;

	ret = tps->read(tps, TPS65910_RTC_STATUS, 1, &rtc_reg);
	if (ret)
		return IRQ_NONE;

	if (rtc_reg & TPS65910_RTC_STATUS_ALARM)
		events = RTC_IRQF | RTC_AF;

	ret = tps->write(tps, TPS65910_RTC_STATUS, 1, &rtc_reg);
	if (ret)
		return IRQ_NONE;

	/* Notify RTC core on event */
	rtc_update_irq(tps_rtc->rtc, 1, events);

	return IRQ_HANDLED;
}
#endif

static const struct rtc_class_ops tps65910_rtc_ops = {
	.read_time	= tps65910_rtc_read_time,
	.set_time	= tps65910_rtc_set_time,
	.read_alarm	= tps65910_rtc_read_alarm,
	.set_alarm	= tps65910_rtc_set_alarm,
	.alarm_irq_enable = tps65910_rtc_alarm_irq_enable,
};

static int tps65910_rtc_probe(struct platform_device *pdev)
{
	struct tps65910 *tps65910 = NULL;
	struct tps65910_rtc *tps_rtc = NULL;
	int ret;
#if 0
	int irq;
#endif
	u32 rtc_reg;

	tps65910 = dev_get_drvdata(pdev->dev.parent);

	tps_rtc = devm_kzalloc(&pdev->dev, sizeof(struct tps65910_rtc),
			GFP_KERNEL);
	if (!tps_rtc)
		return -ENOMEM;

	/* Clear pending interrupts */
	ret = tps65910->read(tps65910, TPS65910_RTC_STATUS, 1, &rtc_reg);
	if (ret < 0)
		return ret;

	ret = tps65910->write(tps65910, TPS65910_RTC_STATUS, 1, &rtc_reg);
	if (ret < 0)
		return ret;

	dev_dbg(&pdev->dev, "Enabling rtc-tps65910.\n");

	/* Enable RTC digital power domain */
	ret = tps65910_clear_bits(tps65910, TPS65910_DEVCTRL,
		~DEVCTRL_RTC_PWDN_MASK);
	if (ret < 0)
		return ret;

	rtc_reg = TPS65910_RTC_CTRL_STOP_RTC;
	ret = tps65910->write(tps65910, TPS65910_RTC_CTRL, 1, &rtc_reg);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, tps_rtc);

#if 0
	irq  = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_warn(&pdev->dev, "Wake up is not possible as irq = %d\n",
			irq);
		return -ENXIO;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
		tps65910_rtc_interrupt, IRQF_TRIGGER_LOW,
		dev_name(&pdev->dev), &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "IRQ is not free.\n");
		return ret;
	}

	tps_rtc->irq = irq;
#endif
	device_set_wakeup_capable(&pdev->dev, 1);

	tps_rtc->rtc = rtc_device_register(pdev->name, &pdev->dev,
		&tps65910_rtc_ops, THIS_MODULE);
	if (IS_ERR(tps_rtc->rtc)) {
		ret = PTR_ERR(tps_rtc->rtc);
		dev_err(&pdev->dev, "RTC device register: err %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * Disable tps65910 RTC interrupts.
 * Sets status flag to free.
 */
static int tps65910_rtc_remove(struct platform_device *pdev)
{
	tps65910_rtc_alarm_irq_enable(&pdev->dev, 0);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tps65910_rtc_suspend(struct device *dev)
{
	struct tps65910_rtc *tps_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(tps_rtc->irq);
	return 0;
}

static int tps65910_rtc_resume(struct device *dev)
{
	struct tps65910_rtc *tps_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(tps_rtc->irq);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(tps65910_rtc_pm_ops, tps65910_rtc_suspend,
			tps65910_rtc_resume);

static struct platform_driver tps65910_rtc_driver = {
	.probe		= tps65910_rtc_probe,
	.remove		= tps65910_rtc_remove,
	.driver		= {
		.name	= "tps65910-rtc",
		.owner	= THIS_MODULE,
		.pm	= &tps65910_rtc_pm_ops,
	},
};

static int __init rtc_init(void)
{
	return platform_driver_probe(&tps65910_rtc_driver, tps65910_rtc_probe);
}
module_init(rtc_init);

static void __exit rtc_exit(void)
{
	platform_driver_unregister(&tps65910_rtc_driver);
}
module_exit(rtc_exit);

MODULE_ALIAS("platform:rtc-tps65910");
MODULE_AUTHOR("Venu Byravarasu <vbyravarasu@nvidia.com>");
MODULE_LICENSE("GPL");
