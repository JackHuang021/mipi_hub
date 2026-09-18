// SPDX-License-Identifier: GPL-2.0-only
/* Samsung S6D6FT0 integrated touch (TL060FVXS07).
 * Protocol derived from Samsung sec_ts g_6ft0.v00 in mipi_hub.
 * No firmware upload, calibration, NVM access, or hardware reset: reset and
 * power are shared with the display on this adapter and are owned elsewhere.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm.h>
#include "s6d6ft0_event.h"

#define S6_ID 0x52
#define S6_BOOT 0x55
#define S6_FUNCTIONS_READ 0x64
#define S6_EVENT 0x71
#define S6_SENSE_ON 0x40
#define S6_SENSE_OFF 0x41
#define S6_CLEAR 0x60
#define S6_FUNCTIONS 0x63
#define S6_SLOTS 10
#define S6_DRAIN_LIMIT 128

struct s6d6ft0 {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *irq_gpio;
	struct touchscreen_properties props;
	struct mutex lock;
	int irq;
	u8 id[3], last[8];
	u64 events, contacts, errors, invalid;
	bool irq_fault, suspended, stopped;
};

/* Separate STOP and >=100us gap are required by the vendor probe protocol. */
static int s6_read(struct s6d6ft0 *ts, u8 reg, void *buf, int len)
{
	int ret = i2c_master_send(ts->client, &reg, 1);

	if (ret != 1)
		return ret < 0 ? ret : -EIO;
	usleep_range(100, 200);
	ret = i2c_master_recv(ts->client, buf, len);
	return ret == len ? 0 : ret < 0 ? ret : -EIO;
}

static int s6_command(struct s6d6ft0 *ts, u8 command)
{
	int ret = i2c_master_send(ts->client, &command, 1);

	return ret == 1 ? 0 : ret < 0 ? ret : -EIO;
}

static void s6_release_all(struct s6d6ft0 *ts)
{
	int i;

	for (i = 0; i < S6_SLOTS; i++) {
		input_mt_slot(ts->input, i);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}
	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
}

static int s6_start(struct s6d6ft0 *ts)
{
	u8 functions, buf[2];
	int ret;

	ret = s6_read(ts, S6_FUNCTIONS_READ, &functions, 1);
	if (ret)
		return ret;
	buf[0] = S6_FUNCTIONS;
	buf[1] = functions | BIT(0); /* Preserve existing modes; enable mutual touch. */
	ret = i2c_master_send(ts->client, buf, sizeof(buf));
	if (ret != sizeof(buf))
		return ret < 0 ? ret : -EIO;
	ret = s6_command(ts, S6_CLEAR);
	if (ret)
		return ret;
	return s6_command(ts, S6_SENSE_ON);
}

static void s6_coordinate(struct s6d6ft0 *ts, const u8 e[8])
{
	struct s6d6ft0_contact c;
	bool active;
	int tool;

	if (!s6d6ft0_decode(e, &c)) {
		ts->invalid++;
		return;
	}
	active = c.action != 3;
	/* Report normal/glove/palm, not vendor hover/proximity as finger touches. */
	if (active && c.type != 0 && c.type != 3 && c.type != 6)
		return;
	tool = c.type == 6 ? MT_TOOL_PALM : MT_TOOL_FINGER;
	input_mt_slot(ts->input, c.slot);
	input_mt_report_slot_state(ts->input, tool, active);
	if (active) {
		c.x = min(c.x, ts->props.max_x);
		c.y = min(c.y, ts->props.max_y);
		touchscreen_report_pos(ts->input, &ts->props, c.x, c.y, true);
		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, c.major);
		input_report_abs(ts->input, ABS_MT_TOUCH_MINOR, c.minor);
		/* This panel reports z=0 even while a finger is down. Do not
		 * advertise a pressure axis: Android treats zero raw pressure
		 * as hover regardless of BTN_TOUCH and the active tracking ID.
		 */
	}
	/* Delta events, not full-slot snapshots. Never use DROP_UNUSED here.
	 * Sync each coordinate so a queued press+release cannot erase a short tap.
	 */
	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
	ts->contacts++;
}

static int s6_drain(struct s6d6ft0 *ts)
{
	u8 e[8];
	int i, ret;

	for (i = 0; i < S6_DRAIN_LIMIT; i++) {
		ret = s6_read(ts, S6_EVENT, e, sizeof(e));
		if (ret)
			return ret;
		if (!e[0])
			return 0;
		memcpy(ts->last, e, sizeof(e));
		ts->events++;
		if ((e[0] >> 6) == 1)
			s6_coordinate(ts, e);
		else if ((e[0] >> 6) == 0 && e[1] == 0x0c)
			s6_release_all(ts); /* Boot notification; never pulse shared reset. */
	}
	return -EOVERFLOW;
}

static irqreturn_t s6_irq_thread(int irq, void *data)
{
	struct s6d6ft0 *ts = data;
	int ret;

	mutex_lock(&ts->lock);
	ret = s6_drain(ts);
	if (ret) {
		ts->errors++;
		s6_release_all(ts);
		if (!ts->irq_fault) {
			ts->irq_fault = true;
			disable_irq_nosync(irq);
		}
		dev_err_ratelimited(&ts->client->dev,
			"event read failed (%d); IRQ stopped, shared reset untouched\n", ret);
	}
	mutex_unlock(&ts->lock);
	return IRQ_HANDLED;
}

static ssize_t stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6d6ft0 *ts = dev_get_drvdata(dev);
	ssize_t len;

	mutex_lock(&ts->lock);
	len = sysfs_emit(buf, "id=%*ph events=%llu contacts=%llu errors=%llu invalid=%llu irq_fault=%u last=%*ph\n",
		3, ts->id, ts->events, ts->contacts, ts->errors, ts->invalid,
		ts->irq_fault, 8, ts->last);
	mutex_unlock(&ts->lock);
	return len;
}
static DEVICE_ATTR_RO(stats);
static struct attribute *s6_attrs[] = { &dev_attr_stats.attr, NULL };
static const struct attribute_group s6_group = { .attrs = s6_attrs };

static int s6_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct s6d6ft0 *ts;
	u8 boot;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;
	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;
	ts->client = client;
	mutex_init(&ts->lock);
	i2c_set_clientdata(client, ts);
	ts->irq_gpio = devm_gpiod_get_optional(dev, "irq", GPIOD_IN);
	if (IS_ERR(ts->irq_gpio))
		return dev_err_probe(dev, PTR_ERR(ts->irq_gpio), "IRQ GPIO\n");
	ts->irq = client->irq;
	if (ts->irq_gpio)
		ts->irq = gpiod_to_irq(ts->irq_gpio);
	if (ts->irq <= 0)
		return dev_err_probe(dev, ts->irq ?: -EINVAL, "missing IRQ\n");
	ret = s6_read(ts, S6_ID, ts->id, sizeof(ts->id));
	if (ret)
		return dev_err_probe(dev, ret, "device ID\n");
	if (ts->id[0] != 0xac)
		return dev_err_probe(dev, -ENODEV, "not running touch application; no firmware update attempted\n");
	ret = s6_read(ts, S6_BOOT, &boot, 1);
	if (ret)
		return ret;
	ts->input = devm_input_allocate_device(dev);
	if (!ts->input)
		return -ENOMEM;
	ts->input->name = "Samsung S6D6FT0 Touchscreen";
	ts->input->phys = devm_kasprintf(dev, GFP_KERNEL, "%s/input0", dev_name(dev));
	if (!ts->input->phys)
		return -ENOMEM;
	ts->input->id.bustype = BUS_I2C;
	ts->input->id.product = (ts->id[1] << 8) | ts->id[2];
	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, 1079, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, 2159, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	touchscreen_parse_properties(ts->input, true, &ts->props);
	ret = input_mt_init_slots(ts->input, S6_SLOTS, INPUT_MT_DIRECT);
	if (ret)
		return ret;
	ret = input_register_device(ts->input);
	if (ret)
		return ret;
	ret = devm_request_threaded_irq(dev, ts->irq, NULL, s6_irq_thread,
		IRQF_ONESHOT | IRQF_TRIGGER_LOW | IRQF_NO_AUTOEN, dev_name(dev), ts);
	if (ret)
		return dev_err_probe(dev, ret, "request IRQ\n");
	ret = devm_device_add_group(dev, &s6_group);
	if (ret)
		return ret;
	ret = s6_start(ts);
	if (ret) {
		s6_command(ts, S6_SENSE_OFF);
		return dev_err_probe(dev, ret, "start sensing\n");
	}
	enable_irq(ts->irq);
	dev_info(dev, "ID=%*ph boot=%02x irq=%d; no firmware upload or shared reset\n",
		3, ts->id, boot, ts->irq);
	return 0;
}

static void s6_stop(struct i2c_client *client)
{
	struct s6d6ft0 *ts = i2c_get_clientdata(client);

	if (ts->stopped)
		return;
	disable_irq(ts->irq);
	mutex_lock(&ts->lock);
	ts->stopped = true;
	s6_command(ts, S6_SENSE_OFF);
	s6_release_all(ts);
	mutex_unlock(&ts->lock);
}

static int s6_suspend(struct device *dev)
{
	struct s6d6ft0 *ts = dev_get_drvdata(dev);
	int ret;

	disable_irq(ts->irq);
	mutex_lock(&ts->lock);
	ret = s6_command(ts, S6_SENSE_OFF);
	if (!ret) {
		ts->suspended = true;
		s6_release_all(ts);
	}
	mutex_unlock(&ts->lock);
	if (ret)
		enable_irq(ts->irq);
	return ret;
}

static int s6_resume(struct device *dev)
{
	struct s6d6ft0 *ts = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&ts->lock);
	ret = s6_start(ts);
	if (!ret) {
		ts->suspended = false;
		/* Balance the additional disable from a previous event fault. */
		if (ts->irq_fault) {
			ts->irq_fault = false;
			enable_irq(ts->irq);
		}
	}
	mutex_unlock(&ts->lock);
	if (!ret)
		enable_irq(ts->irq);
	return ret;
}
static DEFINE_SIMPLE_DEV_PM_OPS(s6_pm, s6_suspend, s6_resume);

static const struct of_device_id s6_of_match[] = {
	{ .compatible = "samsung,s6d6ft0" }, {}
};
MODULE_DEVICE_TABLE(of, s6_of_match);
static const struct i2c_device_id s6_ids[] = { { "s6d6ft0", 0 }, {} };
MODULE_DEVICE_TABLE(i2c, s6_ids);
static struct i2c_driver s6_driver = {
	.driver = { .name = "s6d6ft0", .of_match_table = s6_of_match,
		.pm = pm_sleep_ptr(&s6_pm) },
	.probe = s6_probe,
	.remove = s6_stop,
	.shutdown = s6_stop,
	.id_table = s6_ids,
};
module_i2c_driver(s6_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung S6D6FT0 touch input without firmware/reset control");
