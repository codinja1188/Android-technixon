/*
 * LTC2955 (PowerPath) driver
 *
 * Copyright (C) 2014, Xsens Technologies BV <info@xsens.com>
 * Maintainer: René Moll <linux@r-moll.nl>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ----------------------------------------
 * - Description
 * ----------------------------------------
 *
 * This driver is to be used with an external PowerPath Controller (LTC2955).
 * Its function is to determine when a external shut down is triggered
 * and react by properly shutting down the system.
 *
 * This driver expects a device tree with a LTC2955 entry for pin mapping.
 *
 * ----------------------------------------
 * - GPIO
 * ----------------------------------------
 *
 * The following GPIOs are used:
 * - trigger (input)
 *     A level change indicates the shut-down trigger. If it's state reverts
 *     within the time-out defined by trigger_delay, the shut down is not
 *     executed. If no pin is assigned to this input, the driver will start the
 *     watchdog toggle immediately. The chip will only power off the system if
 *     it is requested to do so through the kill line.
 *
 * - kill (output)
 *     The last action during shut down is triggering this signalling, such
 *     that the PowerPath Control will power down the hardware.
 *
 * ----------------------------------------
 * - Interrupts
 * ----------------------------------------
 *
 * The driver requires a non-shared, edge-triggered interrupt on the trigger
 * GPIO.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/gpio/consumer.h>
#include <linux/reboot.h>

struct LTC2955_poweroff {
	struct hrtimer timer_trigger;

	ktime_t trigger_delay;

	struct device *dev;

	struct gpio_desc *gpio_trigger;
	struct gpio_desc *gpio_kill;
	struct input_dev *input;
	struct mutex disable_lock;
	bool kernel_panic;
	struct notifier_block panic_notifier;
};

#define to_LTC2955(p, m) container_of(p, struct LTC2955_poweroff, m)

/*
 * This global variable is only needed for pm_power_off. We should
 * remove it entirely once we don't need the global state anymore.
 */
static struct LTC2955_poweroff *LTC2955_data;

/**
 * LTC2955_poweroff_timer_wde - Timer callback
 * Toggles the watchdog reset signal each wde_interval
 *
 * @timer: corresponding timer
 *
 * Returns HRTIMER_RESTART for an infinite loop which will only stop when the
 * machine actually shuts down
 */

static enum hrtimer_restart
LTC2955_poweroff_timer_trigger(struct hrtimer *timer)
{
	struct LTC2955_poweroff *data = to_LTC2955(timer, timer_trigger);
	dev_info(data->dev, "executing shutdown\n");
	orderly_poweroff(true);

	return HRTIMER_NORESTART;
}

/**
 * LTC2955_poweroff_handler - Interrupt handler
 * Triggered each time the trigger signal changes state and (de)activates a
 * time-out (timer_trigger). Once the time-out is actually reached the shut
 * down is executed.
 *
 * @irq: IRQ number
 * @dev_id: pointer to the main data structure
 */
static irqreturn_t LTC2955_poweroff_handler(int irq, void *dev_id)
{
	struct LTC2955_poweroff *data = dev_id;

	if (data->kernel_panic ) {
		/* shutdown is already triggered, nothing to do any more */
		return IRQ_HANDLED;
	}
	input_event(data->input, EV_KEY, KEY_F9, 1);
	input_event(data->input, EV_KEY, KEY_F9, 0);
        input_sync(data->input);

	if (gpiod_get_value(data->gpio_trigger)) {
        	input_sync(data->input);
		hrtimer_start(&data->timer_trigger, data->trigger_delay,
			      HRTIMER_MODE_REL);
	} else {
		hrtimer_cancel(&data->timer_trigger);
	}

	return IRQ_HANDLED;
}

static int LTC2955_poweroff_open(struct input_dev *dev)
{
        return 0;
}

static void LTC2955_poweroff_close(struct input_dev *dev)
{

}

static void LTC2955_poweroff_kill(void)
{
	gpiod_set_value(LTC2955_data->gpio_kill, 1);
}

static void LTC2955_poweroff_default(struct LTC2955_poweroff *data)
{
	data->trigger_delay = ktime_set(2, 500L*1E6L);

	hrtimer_init(&data->timer_trigger, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	data->timer_trigger.function = LTC2955_poweroff_timer_trigger;
}

	
static int LTC2955_poweroff_init(struct platform_device *pdev)
{
	int ret;
	struct LTC2955_poweroff *data = platform_get_drvdata(pdev);
	

	LTC2955_poweroff_default(data);

	data->gpio_kill = devm_gpiod_get(&pdev->dev, "kill", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpio_kill)) {
		ret = PTR_ERR(data->gpio_kill);
		dev_err(&pdev->dev, "unable to claim gpio \"kill\"\n");
		return ret;
	}

	data->gpio_trigger = devm_gpiod_get_optional(&pdev->dev, "trigger",
						     GPIOD_IN);
	if (IS_ERR(data->gpio_trigger)) {
		/*
		 * It's not a problem if the trigger gpio isn't available, but
		 * it is worth a warning if its use was defined in the device
		 * tree.
		 */
		dev_err(&pdev->dev, "unable to claim gpio \"trigger\"\n");
		data->gpio_trigger = NULL;
	}

	if (devm_request_irq(&pdev->dev, gpiod_to_irq(data->gpio_trigger),
			     LTC2955_poweroff_handler,
			     (IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING),
			     "LTC2955-poweroff",
			     data)) {
		/*
		 * Some things may have happened:
		 * - No trigger input was defined
		 * - Claiming the GPIO failed
		 * - We could not map to an IRQ
		 * - We couldn't register an interrupt handler
		 *
		 * None of these really are problems, but all of them
		 * disqualify the push button from controlling the power.
		 *
		 * It is therefore important to note that if the LTC2955
		 * detects a button press for long enough, it will still start
		 * its own powerdown window and cut the power on us
		 */
		if (data->gpio_trigger) {
			dev_warn(&pdev->dev,
				 "unable to configure the trigger interrupt\n");
			devm_gpiod_put(&pdev->dev, data->gpio_trigger);
			data->gpio_trigger = NULL;
		}
		dev_info(&pdev->dev,
			 "power down trigger input will not be used\n");
	}

	return 0;
}

static int LTC2955_poweroff_notify_panic(struct notifier_block *nb,
					 unsigned long code, void *unused)
{
	struct LTC2955_poweroff *data = to_LTC2955(nb, panic_notifier);

	data->kernel_panic = true;
	return NOTIFY_DONE;
}

static int LTC2955_poweroff_probe(struct platform_device *pdev)
{
	int ret;
	struct LTC2955_poweroff *data;
	struct input_dev *input;
	if (pm_power_off) {
		dev_err(&pdev->dev, "pm_power_off already registered");
		return -EBUSY;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	input = devm_input_allocate_device(&pdev->dev);
        if (!input) {
                dev_err(&pdev->dev, "failed to allocate input device\n");
                return -ENOMEM;
        }

	data->dev = &pdev->dev;
	data->input = input;
	platform_set_drvdata(pdev, data);
	input_set_drvdata(input, data);

	 input->name = "power-event";	
	input->id.bustype = BUS_HOST;
        input->id.vendor = 0x0002;
        input->id.product = 0x0002;
        input->id.version = 0x0200;
	input->dev.parent = &pdev->dev;

	input->open = LTC2955_poweroff_open;
        input->close = LTC2955_poweroff_close;
	
	//__set_bit(EV_REP, input->evbit);
	//input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	input->evbit[0] = BIT_MASK(EV_KEY);
	input->keybit[BIT_WORD(KEY_F9)] = BIT_MASK(KEY_F9);


	ret = LTC2955_poweroff_init(pdev);
	if (ret)
		return ret;

	/* TODO: remove LTC2955_data */
	LTC2955_data = data;
	pm_power_off = LTC2955_poweroff_kill;
	
	ret = input_register_device(input);
        if (ret) {
                dev_err(&pdev->dev, "Unable to register input device, error: %d\n",
                        ret);
		return ret;
        }
	

	data->panic_notifier.notifier_call = LTC2955_poweroff_notify_panic;
	atomic_notifier_chain_register(&panic_notifier_list,
				       &data->panic_notifier);
	dev_info(&pdev->dev, "probe successful\n");

	return 0;
}

static int LTC2955_poweroff_remove(struct platform_device *pdev)
{
	struct LTC2955_poweroff *data = platform_get_drvdata(pdev);

	pm_power_off = NULL;
	hrtimer_cancel(&data->timer_trigger);
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &data->panic_notifier);
	return 0;
}

static const struct of_device_id of_LTC2955_poweroff_match[] = {
	{ .compatible = "lltc,LTC2955"},
	{},
};
MODULE_DEVICE_TABLE(of, of_LTC2955_poweroff_match);

static struct platform_driver LTC2955_poweroff_driver = {
	.probe = LTC2955_poweroff_probe,
	.remove = LTC2955_poweroff_remove,
	.driver = {
		.name = "LTC2955-poweroff",
		.of_match_table = of_LTC2955_poweroff_match,
	},
};

module_platform_driver(LTC2955_poweroff_driver);

MODULE_AUTHOR("vasu");
MODULE_DESCRIPTION("LTC PowerPath power-off driver");
MODULE_LICENSE("GPL v2");
