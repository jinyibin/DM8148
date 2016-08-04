/*
 * pwm-fan.c - Hwmon driver for fans connected to timer lines.
 *
 * Copyright (C) 2010 LaCie
 *
 * Author: Simon Guinot <sguinot@lacie.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/hwmon.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <plat/dmtimer.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/pwm-fan.h>

struct pwm_fan_data {
	struct platform_device	*pdev;
	int timer_id;
	struct device		*hwmon_dev;
	struct mutex		lock; /* lock GPIOs operations. */
	int			num_ctrl;
	unsigned		*ctrl;
	int			num_speed;
	struct pwm_fan_speed	*speed;
	int			speed_index;
#ifdef CONFIG_PM
	int			resume_speed;
#endif
	bool			pwm_enable;
	struct pwm_fan_alarm	*alarm;
	struct work_struct	alarm_work;
};

#define TIMER2_BASE_ADDR	0x48040000
#define TIMER_OFF_ADDR 		0x2000
#define TIMER8_BASE_ADDR 	0x481C1000

#define TIMER_CONTROL_REG_OFF 	0x38
#define TIMER_MATCH_REG_OFF 	0x4C

//#define TIMER_ID 7
#define TIMER_PWM_PERIOD_NS	5000000

static struct omap_dm_timer *timer_ptr;

static int dm8148_timer_pwm_init(int timer_id) {
	u32 gt_rate;
	u32 clk_src;
	u32 l;
	u32 timer_base_addr;

	timer_ptr = omap_dm_timer_request_specific(timer_id);
	if (timer_ptr == NULL) {
		pr_err("Failed to reserve timer%d\n", timer_id);
		return -1;
	}

#if defined(CONFIG_OMAP_32K_TIMER)
	clk_src = OMAP_TIMER_SRC_32_KHZ;
#else
	clk_src = OMAP_TIMER_SRC_SYS_CLK;
#endif
	if (IS_ERR_VALUE(omap_dm_timer_set_source(timer_ptr, clk_src)))
		pr_err("Failed to set timer source\n");
	
	omap_dm_timer_set_prescaler(timer_ptr, 0);

	gt_rate = clk_get_rate(omap_dm_timer_get_fclk(timer_ptr));
	pr_info("Timer%d will run at at %u Hz\n", timer_id, gt_rate);

	omap_dm_timer_set_pwm(timer_ptr, 1, 1, 2);
	
	/* only for timer2 to timer 7 */
	timer_base_addr = TIMER2_BASE_ADDR + TIMER_OFF_ADDR * (timer_id - 2);
	/* enable match mode */
	l = omap_readl(timer_base_addr + TIMER_CONTROL_REG_OFF);
	omap_writel(l | 0x40, timer_base_addr + TIMER_CONTROL_REG_OFF);

	return 0;
}

static int dm8148_timer_pwm_config(int timer_id, int duty_ns, int period_ns)
{
	u32 compare_val, autoload_val, compare_temp, autoload_temp;
	u32 rate, rate_mhz;

	omap_dm_timer_stop(timer_ptr);

	rate = clk_get_rate(omap_dm_timer_get_fclk(timer_ptr));
	rate_mhz = rate / 1000000;

	compare_temp = duty_ns * rate_mhz / 1000;
	autoload_temp = period_ns * rate_mhz / 1000;
	compare_val = 0xFFFFFFFF - compare_temp;
	autoload_val = 0xFFFFFFFF - autoload_temp;

	if (duty_ns == 0)
		compare_val = autoload_val - 1;

	omap_dm_timer_set_load(timer_ptr, 1, autoload_val);

	omap_writel(compare_val, TIMER2_BASE_ADDR + (timer_id - 2) * TIMER_OFF_ADDR + TIMER_MATCH_REG_OFF);

	return 0;
}

/*
 * Alarm GPIO.
 */

static void fan_alarm_notify(struct work_struct *ws)
{
	struct pwm_fan_data *fan_data =
		container_of(ws, struct pwm_fan_data, alarm_work);

	sysfs_notify(&fan_data->pdev->dev.kobj, NULL, "fan1_alarm");
	kobject_uevent(&fan_data->pdev->dev.kobj, KOBJ_CHANGE);
}

static irqreturn_t fan_alarm_irq_handler(int irq, void *dev_id)
{
	struct pwm_fan_data *fan_data = dev_id;

	schedule_work(&fan_data->alarm_work);

	return IRQ_NONE;
}

static ssize_t show_fan_alarm(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct pwm_fan_data *fan_data = dev_get_drvdata(dev);
	struct pwm_fan_alarm *alarm = fan_data->alarm;
	int value = gpio_get_value(alarm->gpio);

	if (alarm->active_low)
		value = !value;

	return sprintf(buf, "%d\n", value);
}

static DEVICE_ATTR(fan1_alarm, S_IRUGO, show_fan_alarm, NULL);

static int fan_alarm_init(struct pwm_fan_data *fan_data,
			  struct pwm_fan_alarm *alarm)
{
	int err;
	int alarm_irq;
	struct platform_device *pdev = fan_data->pdev;

	fan_data->alarm = alarm;

	err = gpio_request(alarm->gpio, "GPIO fan alarm");
	if (err)
		return err;

	err = gpio_direction_input(alarm->gpio);
	if (err)
		goto err_free_gpio;

	err = device_create_file(&pdev->dev, &dev_attr_fan1_alarm);
	if (err)
		goto err_free_gpio;

	/*
	 * If the alarm GPIO don't support interrupts, just leave
	 * without initializing the fail notification support.
	 */
	alarm_irq = gpio_to_irq(alarm->gpio);
	if (alarm_irq < 0)
		return 0;

	INIT_WORK(&fan_data->alarm_work, fan_alarm_notify);
	set_irq_type(alarm_irq, IRQ_TYPE_EDGE_BOTH);
	err = request_irq(alarm_irq, fan_alarm_irq_handler, IRQF_SHARED,
			  "GPIO fan alarm", fan_data);
	if (err)
		goto err_free_sysfs;

	return 0;

err_free_sysfs:
	device_remove_file(&pdev->dev, &dev_attr_fan1_alarm);
err_free_gpio:
	gpio_free(alarm->gpio);

	return err;
}

static void fan_alarm_free(struct pwm_fan_data *fan_data)
{
	struct platform_device *pdev = fan_data->pdev;
	int alarm_irq = gpio_to_irq(fan_data->alarm->gpio);

	if (alarm_irq >= 0)
		free_irq(alarm_irq, fan_data);
	device_remove_file(&pdev->dev, &dev_attr_fan1_alarm);
	gpio_free(fan_data->alarm->gpio);
}

/*
 * Control GPIOs.
 */

/* Must be called with fan_data->lock held, except during initialization. */
static void __set_fan_ctrl(struct pwm_fan_data *fan_data, int ctrl_val)
{
	int duty_ns;
	
	duty_ns = 200000 * ctrl_val;

	dm8148_timer_pwm_config(fan_data->timer_id, duty_ns, TIMER_PWM_PERIOD_NS);
	omap_dm_timer_start(timer_ptr);
}

static int __get_fan_ctrl(struct pwm_fan_data *fan_data)
{
	int i;
	int ctrl_val = 0;

	for (i = 0; i < fan_data->num_ctrl; i++) {
		int value;

		value = gpio_get_value(fan_data->ctrl[i]);
		ctrl_val |= (value << i);
	}
	return ctrl_val;
}

/* Must be called with fan_data->lock held, except during initialization. */
static void set_fan_speed(struct pwm_fan_data *fan_data, int speed_index)
{
	int duty_ns;

	if (fan_data->speed_index == speed_index)
		return;

	duty_ns = (TIMER_PWM_PERIOD_NS / 250) * speed_index;

	dm8148_timer_pwm_config(fan_data->timer_id, duty_ns, TIMER_PWM_PERIOD_NS);
	omap_dm_timer_start(timer_ptr);
#if 0
	__set_fan_ctrl(fan_data, fan_data->speed[speed_index].ctrl_val);
#endif
	fan_data->speed_index = speed_index;
}

static int get_fan_speed_index(struct pwm_fan_data *fan_data)
{
	int ctrl_val = __get_fan_ctrl(fan_data);
	int i;

	for (i = 0; i < fan_data->num_speed; i++)
		if (fan_data->speed[i].ctrl_val == ctrl_val)
			return i;

	dev_warn(&fan_data->pdev->dev,
		 "missing speed array entry for GPIO value 0x%x\n", ctrl_val);

	return -EINVAL;
}

static int rpm_to_speed_index(struct pwm_fan_data *fan_data, int rpm)
{
	struct pwm_fan_speed *speed = fan_data->speed;
	int i;

	for (i = 0; i < fan_data->num_speed; i++)
		if (speed[i].rpm >= rpm)
			return i;

	return fan_data->num_speed - 1;
}

static ssize_t show_pwm(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct pwm_fan_data *fan_data = dev_get_drvdata(dev);
	u8 pwm = fan_data->speed_index;

	return sprintf(buf, "%d\n", pwm);
}

static ssize_t set_pwm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct pwm_fan_data *fan_data = dev_get_drvdata(dev);
	unsigned long pwm;
	int speed_index;
	int ret = count;

	if (strict_strtoul(buf, 10, &pwm) || pwm > 250)
		return -EINVAL;
	mutex_lock(&fan_data->lock);

	if (!fan_data->pwm_enable) {
		ret = -EPERM;
		goto exit_unlock;
	}

	speed_index = pwm;
	set_fan_speed(fan_data, speed_index);

exit_unlock:
	mutex_unlock(&fan_data->lock);

	return ret;
}

static ssize_t show_pwm_enable(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct pwm_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", fan_data->pwm_enable);
}

static ssize_t set_pwm_enable(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct pwm_fan_data *fan_data = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val) || val > 1)
		return -EINVAL;

	if (fan_data->pwm_enable == val)
		return count;

	mutex_lock(&fan_data->lock);

	fan_data->pwm_enable = val;

	/* Disable manual control mode: set fan at full speed. */
	if (val == 0)
		set_fan_speed(fan_data, 0);

	mutex_unlock(&fan_data->lock);

	return count;
}

static ssize_t show_pwm_mode(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0\n");
}

static ssize_t show_rpm_min(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct pwm_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", fan_data->speed[0].rpm);
}

static ssize_t show_rpm_max(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct pwm_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n",
		       fan_data->speed[fan_data->num_speed - 1].rpm);
}

static ssize_t show_rpm(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct pwm_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", fan_data->speed[fan_data->speed_index].rpm);
}

static ssize_t set_rpm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct pwm_fan_data *fan_data = dev_get_drvdata(dev);
	unsigned long rpm;
	int ret = count;

	if (strict_strtoul(buf, 10, &rpm))
		return -EINVAL;

	mutex_lock(&fan_data->lock);

	if (!fan_data->pwm_enable) {
		ret = -EPERM;
		goto exit_unlock;
	}

	set_fan_speed(fan_data, rpm_to_speed_index(fan_data, rpm));

exit_unlock:
	mutex_unlock(&fan_data->lock);

	return ret;
}

static DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, show_pwm, set_pwm);
static DEVICE_ATTR(pwm1_enable, S_IRUGO | S_IWUSR,
		   show_pwm_enable, set_pwm_enable);
static DEVICE_ATTR(pwm1_mode, S_IRUGO, show_pwm_mode, NULL);
static DEVICE_ATTR(fan1_min, S_IRUGO, show_rpm_min, NULL);
static DEVICE_ATTR(fan1_max, S_IRUGO, show_rpm_max, NULL);
static DEVICE_ATTR(fan1_input, S_IRUGO, show_rpm, NULL);
static DEVICE_ATTR(fan1_target, S_IRUGO | S_IWUSR, show_rpm, set_rpm);

static struct attribute *pwm_fan_ctrl_attributes[] = {
	&dev_attr_pwm1.attr,
	&dev_attr_pwm1_enable.attr,
	&dev_attr_pwm1_mode.attr,
	&dev_attr_fan1_input.attr,
	&dev_attr_fan1_target.attr,
	&dev_attr_fan1_min.attr,
	&dev_attr_fan1_max.attr,
	NULL
};

static const struct attribute_group pwm_fan_ctrl_group = {
	.attrs = pwm_fan_ctrl_attributes,
};

static int fan_ctrl_init(struct pwm_fan_data *fan_data,
			 struct pwm_fan_platform_data *pdata)
{
	struct platform_device *pdev = fan_data->pdev;
	int num_ctrl = pdata->num_ctrl;
	unsigned *ctrl = pdata->ctrl;
	int  err;

	fan_data->num_ctrl = num_ctrl;
	fan_data->ctrl = ctrl;
	fan_data->speed = pdata->speed;
	fan_data->pwm_enable = true; /* Enable manual fan speed control. */

	set_fan_speed(fan_data, 50);

	err = sysfs_create_group(&pdev->dev.kobj, &pwm_fan_ctrl_group);
	if (err)
		goto err_free_gpio;

	return 0;

err_free_gpio:

	return err;
}

static void fan_ctrl_free(struct pwm_fan_data *fan_data)
{
	struct platform_device *pdev = fan_data->pdev;
	int i;

	sysfs_remove_group(&pdev->dev.kobj, &pwm_fan_ctrl_group);
	for (i = 0; i < fan_data->num_ctrl; i++)
		gpio_free(fan_data->ctrl[i]);
}

/*
 * Platform driver.
 */

static ssize_t show_name(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "pwm-fan\n");
}

static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static int __devinit pwm_fan_probe(struct platform_device *pdev)
{
	int err;
	struct pwm_fan_data *fan_data;
	struct pwm_fan_platform_data *pdata = pdev->dev.platform_data;

	if (!pdata)
		return -EINVAL;

	fan_data = kzalloc(sizeof(struct pwm_fan_data), GFP_KERNEL);
	if (!fan_data)
		return -ENOMEM;

	fan_data->pdev = pdev;
	fan_data->timer_id = pdata->timer_id;
	platform_set_drvdata(pdev, fan_data);
	dm8148_timer_pwm_init(fan_data->timer_id);
	mutex_init(&fan_data->lock);

	/* Configure alarm GPIO if available. */
	if (pdata->alarm) {
		err = fan_alarm_init(fan_data, pdata->alarm);
		if (err)
			goto err_free_data;
	}

	err = fan_ctrl_init(fan_data, pdata);
	if (err)
		goto err_free_alarm;
#if 0
	/* Configure control GPIOs if available. */
	if (pdata->ctrl && pdata->num_ctrl > 0) {
		if (!pdata->speed || pdata->num_speed <= 1) {
			err = -EINVAL;
			goto err_free_alarm;
		}
		err = fan_ctrl_init(fan_data, pdata);
		if (err)
			goto err_free_alarm;
	}
#endif
	err = device_create_file(&pdev->dev, &dev_attr_name);
	if (err)
		goto err_free_ctrl;

	/* Make this driver part of hwmon class. */
	fan_data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(fan_data->hwmon_dev)) {
		err = PTR_ERR(fan_data->hwmon_dev);
		goto err_remove_name;
	}

	dev_info(&pdev->dev, "PWM fan initialized\n");

	return 0;

err_remove_name:
	device_remove_file(&pdev->dev, &dev_attr_name);
err_free_ctrl:
	if (fan_data->ctrl)
		fan_ctrl_free(fan_data);
err_free_alarm:
	if (fan_data->alarm)
		fan_alarm_free(fan_data);
err_free_data:
	platform_set_drvdata(pdev, NULL);
	kfree(fan_data);

	return err;
}

static int __devexit pwm_fan_remove(struct platform_device *pdev)
{
	struct pwm_fan_data *fan_data = platform_get_drvdata(pdev);

	hwmon_device_unregister(fan_data->hwmon_dev);
	device_remove_file(&pdev->dev, &dev_attr_name);
	if (fan_data->alarm)
		fan_alarm_free(fan_data);
	if (fan_data->ctrl)
		fan_ctrl_free(fan_data);
	kfree(fan_data);

	return 0;
}

#ifdef CONFIG_PM
static int pwm_fan_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct pwm_fan_data *fan_data = platform_get_drvdata(pdev);

	if (fan_data->ctrl) {
		fan_data->resume_speed = fan_data->speed_index;
		set_fan_speed(fan_data, 0);
	}

	return 0;
}

static int pwm_fan_resume(struct platform_device *pdev)
{
	struct pwm_fan_data *fan_data = platform_get_drvdata(pdev);

	if (fan_data->ctrl)
		set_fan_speed(fan_data, fan_data->resume_speed);

	return 0;
}
#else
#define pwm_fan_suspend NULL
#define pwm_fan_resume NULL
#endif

static struct platform_driver pwm_fan_driver = {
	.probe		= pwm_fan_probe,
	.remove		= __devexit_p(pwm_fan_remove),
	.suspend	= pwm_fan_suspend,
	.resume		= pwm_fan_resume,
	.driver	= {
		.name	= "pwm-fan",
	},
};

static int __init pwm_fan_init(void)
{
	return platform_driver_register(&pwm_fan_driver);
}

static void __exit pwm_fan_exit(void)
{
	platform_driver_unregister(&pwm_fan_driver);
}

module_init(pwm_fan_init);
module_exit(pwm_fan_exit);

MODULE_AUTHOR("Simon Guinot <sguinot@lacie.com>");
MODULE_DESCRIPTION("PWM FAN driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pwm-fan");
