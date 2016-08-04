#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <plat/dmtimer.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pwm.h>

#define TIMER2_BASE_ADDR	0x48040000
#define TIMER_OFF_ADDR 		0x2000
#define TIMER8_BASE_ADDR 	0x481C1000

#define TIMER_CONTROL_REG_OFF 	0x38
#define TIMER_MATCH_REG_OFF 	0x4C

static struct omap_dm_timer *timer_ptr;

struct pwm_device {
	struct device *dev;
	struct list_head node;
	const char *label;
	unsigned int pwm_id;
};

static LIST_HEAD(pwm_list);

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

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	dm8148_timer_pwm_config(pwm->pwm_id, duty_ns, period_ns);

	return 0;
}
EXPORT_SYMBOL(pwm_config);

int pwm_enable(struct pwm_device *pwm)
{
	omap_dm_timer_start(timer_ptr);	
	return 0;
}
EXPORT_SYMBOL(pwm_enable);

void pwm_disable(struct pwm_device *pwm)
{	
	omap_dm_timer_start(timer_ptr);
}
EXPORT_SYMBOL(pwm_disable);

struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	struct pwm_device *pwm;

	list_for_each_entry(pwm, &pwm_list, node) {
		if (pwm->pwm_id == pwm_id) {
			pwm->label = label;
			pwm->pwm_id = pwm_id;
			return pwm;
		}
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(pwm_request);

void pwm_free(struct pwm_device *pwm)
{
	pwm_disable(pwm);
}
EXPORT_SYMBOL(pwm_free);

static int __devinit dm8148_pwm_probe(struct platform_device *pdev)
{
	struct pwm_device *pwm;
	/*
	 * Nothing to be done in probe, this is required to get the
	 * device which is required for ab8500 read and write
	 */

	pwm = kzalloc(sizeof(struct pwm_device), GFP_KERNEL);
	if (pwm == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	pwm->dev = &pdev->dev;
	pwm->pwm_id = pdev->id;
	list_add_tail(&pwm->node, &pwm_list);
	platform_set_drvdata(pdev, pwm);
	dev_dbg(pwm->dev, "pwm probe successful\n");

	dm8148_timer_pwm_init(pwm->pwm_id);

	return 0;
}

static int __devexit dm8148_pwm_remove(struct platform_device *pdev)
{
	struct pwm_device *pwm = platform_get_drvdata(pdev);
	list_del(&pwm->node);
	dev_dbg(&pdev->dev, "pwm driver removed\n");
	kfree(pwm);
	return 0;
}

static struct platform_driver dm8148_pwm_driver = {
	.driver = {
		.name = "dm8148-pwm",
		.owner = THIS_MODULE,
	},
	.probe = dm8148_pwm_probe,
	.remove = __devexit_p(dm8148_pwm_remove),
};

static int __init dm8148_pwm_init(void)
{
	return platform_driver_register(&dm8148_pwm_driver);
}

static void __exit dm8148_pwm_exit(void)
{
	platform_driver_unregister(&dm8148_pwm_driver);
}

subsys_initcall(dm8148_pwm_init);
module_exit(dm8148_pwm_exit);

MODULE_AUTHOR("vefone <vefone2lemon@gmail.com>");
MODULE_DESCRIPTION("DM8148 timer pwm Driver");
MODULE_ALIAS("DM8148 PWM driver");
MODULE_LICENSE("GPL");
