// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synaptics S3916A (TCM v1 + Samsung SEC 16-byte coordinate event 0xC1)
 * Mainline Linux I2C multi-touch driver for Samsung Galaxy Z Flip5 (SM-F7310 b5q).
 *
 * Protocol verified against live SM-F7310 hardware (S3916AA1A0, 4096x4096, 10 slots):
 *  - Startup: disable 0x11, enable 0xC0 / 0xC1 (SEC coordinate) / 0xC2 (SEC status)
 *  - IRQ (TLMM GPIO 19, active-low): read 4-byte TCM v1 header [0xA5, code, len_lo, len_hi],
 *    then continued-read [0xA5, 0x03, payload..., 0x5A].
 *  - Report 0xC1 payload consists of N * 16-byte events:
 *      eid    = b[0] & 0x03 (0 = coordinate)
 *      slot   = (b[0] >> 2) & 0x0F (0..9)
 *      action = (b[0] >> 6) & 0x03 (1 = press, 2 = move, 3 = release)
 *      x      = (b[1] << 4) | (b[3] >> 4)
 *      y      = (b[2] << 4) | (b[3] & 0x0F)
 *      major  = b[4], minor = b[5], z = b[6] & 0x3F
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#define TCM_MARKER		0xA5
#define TCM_END_MARKER		0x5A

#define TCM_STATUS_IDLE		0x00
#define TCM_STATUS_OK		0x01
#define TCM_STATUS_CONTINUED	0x03

#define TCM_CMD_ENABLE_REPORT	0x05
#define TCM_CMD_DISABLE_REPORT	0x06

#define REPORT_IDENTIFY		0x10
#define REPORT_TOUCH		0x11
#define REPORT_SEC_GESTURE	0xC0
#define REPORT_SEC_COORD	0xC1
#define REPORT_SEC_STATUS	0xC2

#define MAX_SLOTS		10
#define MAX_X			4095
#define MAX_Y			4095
#define MAX_PAYLOAD		512

struct s3916_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct regulator *vdd;
	struct regulator *avdd;
	struct mutex lock;
	u8 rx_buf[MAX_PAYLOAD + 8];
};

static int s3916_i2c_read(struct s3916_ts *ts, u8 *buf, size_t len)
{
	int ret = i2c_master_recv(ts->client, buf, len);

	if (ret < 0)
		return ret;
	if (ret != len)
		return -EIO;
	return 0;
}

static int s3916_read_message(struct s3916_ts *ts, u8 *code, u16 *plen, u8 *payload)
{
	u8 hdr[4];
	u16 len;
	int ret;

	ret = s3916_i2c_read(ts, hdr, sizeof(hdr));
	if (ret)
		return ret;

	if (hdr[0] != TCM_MARKER)
		return -EAGAIN;

	*code = hdr[1];
	len = hdr[2] | (hdr[3] << 8);
	*plen = len;

	if (len == 0) {
		/* Drain trailing 0x5A end marker safely */
		u8 tail[3];
		s3916_i2c_read(ts, tail, sizeof(tail));
		return 0;
	}

	if (len > MAX_PAYLOAD) {
		dev_warn_ratelimited(&ts->client->dev, "oversized packet code=0x%02x len=%u\n",
				     *code, len);
		return -EOVERFLOW;
	}

	/* Continued read: on i2c-gpio, S3916A prefixes [0xXX, 0xA5, 0x03, payload..., 0x5A]
	 * (or [0xA5, 0x03, payload..., 0x5A]). Read len + 4 bytes and align to 0xA5 0x03.
	 */
	ret = s3916_i2c_read(ts, ts->rx_buf, len + 4);
	if (ret)
		return ret;

	{
		int off = 0, i;

		for (i = 0; i < 4; i++) {
			if (ts->rx_buf[i] == TCM_MARKER &&
			    ts->rx_buf[i + 1] == TCM_STATUS_CONTINUED) {
				off = i + 2;
				break;
			}
		}
		memcpy(payload, &ts->rx_buf[off], len);
	}

	return 0;
}

static int s3916_send_cmd(struct s3916_ts *ts, u8 cmd, const u8 *arg, u16 arg_len)
{
	u8 tx[16];
	u8 code = 0;
	u16 plen = 0;
	u8 dummy[MAX_PAYLOAD];
	int ret, tries;

	if (arg_len + 3 > sizeof(tx))
		return -EINVAL;

	tx[0] = cmd;
	tx[1] = arg_len & 0xFF;
	tx[2] = (arg_len >> 8) & 0xFF;
	if (arg_len)
		memcpy(&tx[3], arg, arg_len);

	ret = i2c_master_send(ts->client, tx, arg_len + 3);
	if (ret < 0)
		return ret;

	for (tries = 0; tries < 25; tries++) {
		msleep(12);
		ret = s3916_read_message(ts, &code, &plen, dummy);
		if (ret)
			continue;
		if (code == TCM_STATUS_IDLE || code == 0xFF)
			continue;
		if (code == TCM_STATUS_OK)
			return 0;
		if (code < 0x10) {
			dev_warn(&ts->client->dev, "cmd 0x%02x returned status 0x%02x\n",
				 cmd, code);
			return -EIO;
		}
	}
	return -ETIMEDOUT;
}

static int s3916_set_report(struct s3916_ts *ts, u8 report_id, bool enable)
{
	u8 cmd = enable ? TCM_CMD_ENABLE_REPORT : TCM_CMD_DISABLE_REPORT;

	return s3916_send_cmd(ts, cmd, &report_id, 1);
}

static void s3916_handle_sec_coord(struct s3916_ts *ts, const u8 *data, u16 len)
{
	int count = len / 16;
	int i;
	bool reported = false;

	for (i = 0; i < count; i++) {
		const u8 *b = data + (i * 16);
		u8 eid = b[0] & 0x03;
		u8 slot = (b[0] >> 2) & 0x0F;
		u8 action = (b[0] >> 6) & 0x03;
		u16 x, y;
		u8 major, minor, z;

		if (eid != 0 || slot >= MAX_SLOTS)
			continue;

		x = ((u16)b[1] << 4) | ((b[3] >> 4) & 0x0F);
		y = ((u16)b[2] << 4) | (b[3] & 0x0F);
		major = b[4];
		minor = b[5];
		z = b[6] & 0x3F;

		input_mt_slot(ts->input, slot);
		if (action == 1 || action == 2) {
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, true);
			input_report_abs(ts->input, ABS_MT_POSITION_X, x);
			input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
			input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, major);
			input_report_abs(ts->input, ABS_MT_TOUCH_MINOR, minor);
			input_report_abs(ts->input, ABS_MT_PRESSURE, z);
			reported = true;
		} else if (action == 3) {
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
			reported = true;
		}
	}

	if (reported) {
		input_mt_sync_frame(ts->input);
		input_sync(ts->input);
	}
}

static irqreturn_t s3916_irq_thread(int irq, void *dev_id)
{
	struct s3916_ts *ts = dev_id;
	u8 payload[MAX_PAYLOAD];
	u8 code = 0;
	u16 plen = 0;
	int ret;

	mutex_lock(&ts->lock);
	ret = s3916_read_message(ts, &code, &plen, payload);
	mutex_unlock(&ts->lock);

	if (ret)
		return IRQ_HANDLED;

	if (code == REPORT_SEC_COORD && plen >= 16)
		s3916_handle_sec_coord(ts, payload, plen);

	return IRQ_HANDLED;
}

static int s3916_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct s3916_ts *ts;
	u8 code = 0;
	u16 plen = 0;
	u8 dummy[MAX_PAYLOAD];
	int ret, i;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;
	mutex_init(&ts->lock);
	i2c_set_clientdata(client, ts);

	ts->vdd = devm_regulator_get_optional(dev, "vdd");
	if (!IS_ERR(ts->vdd))
		regulator_enable(ts->vdd);

	ts->avdd = devm_regulator_get_optional(dev, "avdd");
	if (!IS_ERR(ts->avdd))
		regulator_enable(ts->avdd);

	msleep(20);

	/* Drain any pending startup IDENTIFY report */
	for (i = 0; i < 3; i++)
		s3916_read_message(ts, &code, &plen, dummy);

	/* Samsung S3916A app_fw setup sequence (matches stock synaptics_ts_set_up_app_fw) */
	s3916_set_report(ts, REPORT_TOUCH, false);
	ret = s3916_set_report(ts, REPORT_SEC_GESTURE, true);
	if (ret)
		dev_warn(dev, "enable 0xC0 returned %d\n", ret);

	ret = s3916_set_report(ts, REPORT_SEC_COORD, true);
	if (ret) {
		dev_err(dev, "failed to enable SEC coordinate report (0xC1): %d\n", ret);
		return ret;
	}

	ret = s3916_set_report(ts, REPORT_SEC_STATUS, true);
	if (ret)
		dev_warn(dev, "enable 0xC2 returned %d\n", ret);

	ts->input = devm_input_allocate_device(dev);
	if (!ts->input)
		return -ENOMEM;

	ts->input->name = "Synaptics S3916A Touchscreen";
	ts->input->id.bustype = BUS_I2C;
	ts->input->id.vendor = 0x06cb;
	ts->input->id.product = 0x3916;
	ts->input->dev.parent = dev;

	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, MAX_X, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, MAX_Y, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_PRESSURE, 0, 63, 0, 0);

	ret = input_mt_init_slots(ts->input, MAX_SLOTS, INPUT_MT_DIRECT);
	if (ret)
		return ret;

	ret = input_register_device(ts->input);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(dev, client->irq, NULL, s3916_irq_thread,
					IRQF_ONESHOT, "synaptics-s3916", ts);
	if (ret) {
		dev_err(dev, "failed to request IRQ %d: %d\n", client->irq, ret);
		return ret;
	}

	dev_info(dev, "Synaptics S3916A SEC 0xC1 touchscreen initialized (IRQ %d, %dx%d)\n",
		 client->irq, MAX_X + 1, MAX_Y + 1);
	return 0;
}

static const struct of_device_id s3916_of_match[] = {
	{ .compatible = "samsung,b5q-synaptics-s3916" },
	{ }
};
MODULE_DEVICE_TABLE(of, s3916_of_match);

static const struct i2c_device_id s3916_id[] = {
	{ "synaptics-s3916" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s3916_id);

static struct i2c_driver s3916_driver = {
	.driver = {
		.name = "synaptics-s3916-sec",
		.of_match_table = s3916_of_match,
	},
	.probe = s3916_probe,
	.id_table = s3916_id,
};
module_i2c_driver(s3916_driver);

MODULE_DESCRIPTION("Synaptics S3916A SEC 0xC1 I2C Touchscreen Driver (SM-F7310 b5q)");
MODULE_LICENSE("GPL");
