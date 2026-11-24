// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Jerry-zirui (https://github.com/Jerry-zirui/postmarketOS_on_zflip5)
/*
 * b5q-touchd: Hardware-IRQ-driven Synaptics S3916A (Samsung SEC 0xC1 16-byte protocol)
 * userspace uinput driver for Samsung Galaxy Z Flip 5 (SM-F7310) over i2c-gpio.
 *
 * Uses one-shot I2C reads on i2c-tsp (GPIO 44 SDA / GPIO 45 SCL) + GPIO v2 falling-edge
 * IRQ + active-low level drain on TLMM GPIO 19 (ATTN), completely bypassing SM8550
 * geni_i2c + gpi_dma1 ring exhaustion and S3916A split-read 0x36 prefix framing.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define S3916_I2C_ADDR		0x20
#define S3916_ATTN_GPIO		19

#define TCM_MARKER		0xA5
#define TCM_STATUS_OK		0x01
#define TCM_STATUS_CONTINUED	0x03

#define TCM_CMD_RESET		0x04
#define TCM_CMD_ENABLE_REPORT	0x05
#define TCM_CMD_DISABLE_REPORT	0x06
#define TCM_CMD_REZERO		0x27

#define REPORT_IDENTIFY		0x10
#define REPORT_TOUCH		0x11
#define REPORT_SEC_GESTURE	0xC0
#define REPORT_SEC_COORD	0xC1
#define REPORT_SEC_STATUS	0xC2

#define MAX_SLOTS		10
#define MAX_X			4095
#define MAX_Y			4095
#define MAX_PAYLOAD		512
#define ONESHOT_READ_LEN	37	/* 4B hdr + 32B (2 fingers) + 1B end marker */

static volatile sig_atomic_t g_stop = 0;
static bool g_debug = false;

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static int find_sysfs_child_prefix(const char *dir_path, const char *prefix, char *out, size_t out_sz)
{
	DIR *d = opendir(dir_path);
	struct dirent *de;
	size_t plen = strlen(prefix);

	if (!d)
		return -1;
	while ((de = readdir(d)) != NULL) {
		if (strncmp(de->d_name, prefix, plen) == 0) {
			snprintf(out, out_sz, "/dev/%s", de->d_name);
			closedir(d);
			return 0;
		}
	}
	closedir(d);
	return -1;
}

/* Locate card*-DSI-1 DPMS sysfs attribute so wakeups track OLED panel power-on. */
static int find_dsi_dpms_path(char *out, size_t out_sz)
{
	DIR *d = opendir("/sys/class/drm");
	struct dirent *de;
	const char *suffix = "-DSI-1";
	size_t slen = strlen(suffix);

	if (!d)
		return -1;
	while ((de = readdir(d)) != NULL) {
		size_t len = strlen(de->d_name);
		if (strncmp(de->d_name, "card", 4) == 0 && len > slen &&
		    strcmp(de->d_name + len - slen, suffix) == 0) {
			char cand[512];
			int n = snprintf(cand, sizeof(cand), "/sys/class/drm/%s/dpms", de->d_name);
			if (n > 0 && (size_t)n < sizeof(cand) && access(cand, R_OK) == 0) {
				snprintf(out, out_sz, "%s", cand);
				closedir(d);
				return 0;
			}
		}
	}
	closedir(d);
	return -1;
}

static bool read_display_on(const char *dpms_path)
{
	char buf[32];
	int fd;
	ssize_t n;

	if (!dpms_path || !dpms_path[0])
		return true;
	fd = open(dpms_path, O_RDONLY);
	if (fd < 0)
		return true;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return true;
	buf[n] = '\0';
	if (strncmp(buf, "Off", 3) == 0 || strncmp(buf, "Standby", 7) == 0 ||
	    strncmp(buf, "Suspend", 7) == 0)
		return false;
	return true;
}

static void emit_ev(int fd, uint16_t type, uint16_t code, int32_t value)
{
	struct input_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.code = code;
	ev.value = value;
	if (write(fd, &ev, sizeof(ev)) != sizeof(ev)) {
		/* Ignore transient write errors */
	}
}

static void set_abs(int fd, uint16_t code, int32_t minv, int32_t maxv)
{
	struct uinput_abs_setup abs;
	memset(&abs, 0, sizeof(abs));
	abs.code = code;
	abs.absinfo.minimum = minv;
	abs.absinfo.maximum = maxv;
	ioctl(fd, UI_ABS_SETUP, &abs);
}

static int setup_uinput(void)
{
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("open /dev/uinput");
		return -1;
	}

	ioctl(fd, UI_SET_EVBIT, EV_SYN);
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_ABS);
	ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
	ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

	set_abs(fd, ABS_X, 0, MAX_X);
	set_abs(fd, ABS_Y, 0, MAX_Y);
	set_abs(fd, ABS_PRESSURE, 0, 63);
	set_abs(fd, ABS_MT_SLOT, 0, MAX_SLOTS - 1);
	set_abs(fd, ABS_MT_TRACKING_ID, 0, 65535);
	set_abs(fd, ABS_MT_POSITION_X, 0, MAX_X);
	set_abs(fd, ABS_MT_POSITION_Y, 0, MAX_Y);
	set_abs(fd, ABS_MT_TOUCH_MAJOR, 0, 255);
	set_abs(fd, ABS_MT_TOUCH_MINOR, 0, 255);
	set_abs(fd, ABS_MT_PRESSURE, 0, 63);

	struct uinput_setup us;
	memset(&us, 0, sizeof(us));
	us.id.bustype = BUS_I2C;
	us.id.vendor = 0x06cb;
	us.id.product = 0x3916;
	us.id.version = 1;
	snprintf(us.name, sizeof(us.name), "Synaptics S3916A Touchscreen");

	if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
		perror("UI_DEV_CREATE");
		close(fd);
		return -1;
	}
	return fd;
}

static int setup_gpio_irq(const char *chip_path, uint32_t offset)
{
	int chip_fd = open(chip_path, O_RDONLY);
	if (chip_fd < 0) {
		perror(chip_path);
		return -1;
	}

	struct gpio_v2_line_request req;
	memset(&req, 0, sizeof(req));
	req.offsets[0] = offset;
	req.num_lines = 1;
	req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_FALLING | GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
	snprintf(req.consumer, sizeof(req.consumer), "b5q-touchd");

	if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
		perror("GPIO_V2_GET_LINE_IOCTL");
		close(chip_fd);
		return -1;
	}
	close(chip_fd);
	return req.fd;
}

static int gpio_read_level(int line_fd)
{
	struct gpio_v2_line_values vals;
	memset(&vals, 0, sizeof(vals));
	vals.mask = 1;
	if (ioctl(line_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &vals) < 0)
		return 1;
	return (int)(vals.bits & 1);
}

static int s3916_read_oneshot(int i2c_fd, uint8_t *code_out, uint16_t *len_out, uint8_t *payload)
{
	uint8_t hdr[5];
	ssize_t n = read(i2c_fd, hdr, 4);
	if (n != 4)
		return -1;

	/* Tolerate 1-byte leading residue (e.g. 0x5A/0x11/0x36 before 0xA5 on i2c-gpio) */
	uint8_t code;
	uint16_t plen;
	if (hdr[0] == TCM_MARKER) {
		code = hdr[1];
		plen = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
	} else if (hdr[1] == TCM_MARKER) {
		if (hdr[2] == TCM_STATUS_CONTINUED) {
			/* Stale zero-length continued trailer (e.g. 5a a5 03 00) — ignore cleanly */
			*code_out = TCM_STATUS_CONTINUED;
			*len_out = 0;
			return 0;
		}
		if (read(i2c_fd, &hdr[4], 1) != 1)
			return -1;
		code = hdr[2];
		plen = (uint16_t)hdr[3] | ((uint16_t)hdr[4] << 8);
	} else {
		if (g_debug) {
			fprintf(stderr, "  [hdr bad n=%zd] %02x%02x%02x%02x\n", n, hdr[0], hdr[1], hdr[2], hdr[3]);
			fflush(stderr);
		}
		return -1;
	}

	*code_out = code;
	*len_out = plen;

	if (code == 0x00 || code == 0xFF || code == TCM_STATUS_CONTINUED)
		return 0;

	if (plen == 0) {
		uint8_t tail[3];
		if (read(i2c_fd, tail, sizeof(tail)) < 0)
			return -1;
		if (g_debug && code != TCM_STATUS_OK) {
			fprintf(stderr, "  [hdr0] code=0x%02x plen=0\n", code);
			fflush(stderr);
		}
		return 0;
	}

	if (plen > MAX_PAYLOAD)
		return -1;

	/* Continued read: on i2c-gpio, S3916A prefixes [0xXX, 0xA5, 0x03, payload..., 0x5A]
	 * (or [0xA5, 0x03, payload..., 0x5A]). Read plen + 4 bytes and align to 0xA5 0x03.
	 */
	uint8_t cbuf[MAX_PAYLOAD + 8];
	ssize_t want = (ssize_t)plen + 4;
	ssize_t cn = read(i2c_fd, cbuf, want);
	if (cn < plen)
		return -1;

	int off = -1;
	for (int i = 0; i < 4 && i + 1 < cn; i++) {
		if (cbuf[i] == TCM_MARKER && cbuf[i + 1] == TCM_STATUS_CONTINUED) {
			off = i + 2;
			break;
		}
	}
	if (off < 0)
		off = 0;
	if (off + plen > cn)
		return -1;

	memcpy(payload, &cbuf[off], plen);

	if (g_debug) {
		fprintf(stderr, "  [msg] code=0x%02x plen=%u off=%d raw0..3=%02x%02x%02x%02x pay0..7=%02x%02x%02x%02x%02x%02x%02x%02x\n",
			code, plen, off, cbuf[0], cbuf[1], cbuf[2], cbuf[3],
			payload[0], plen > 1 ? payload[1] : 0, plen > 2 ? payload[2] : 0, plen > 3 ? payload[3] : 0,
			plen > 4 ? payload[4] : 0, plen > 5 ? payload[5] : 0, plen > 6 ? payload[6] : 0, plen > 7 ? payload[7] : 0);
		fflush(stderr);
	}
	return 0;
}

static bool s3916_send_cmd(int i2c_fd, uint8_t cmd, const uint8_t *arg, uint16_t arg_len)
{
	uint8_t tx[16];
	uint8_t code = 0, dummy[MAX_PAYLOAD];
	uint16_t plen = 0;

	tx[0] = cmd;
	tx[1] = arg_len & 0xFF;
	tx[2] = (arg_len >> 8) & 0xFF;
	if (arg_len)
		memcpy(&tx[3], arg, arg_len);

	if (write(i2c_fd, tx, arg_len + 3) != (ssize_t)(arg_len + 3))
		return false;

	for (int i = 0; i < 20; i++) {
		usleep(12000);
		if (s3916_read_oneshot(i2c_fd, &code, &plen, dummy) == 0 && code == TCM_STATUS_OK)
			return true;
	}
	return false;
}

static bool s3916_set_report(int i2c_fd, uint8_t rep, bool enable)
{
	return s3916_send_cmd(i2c_fd, enable ? TCM_CMD_ENABLE_REPORT : TCM_CMD_DISABLE_REPORT, &rep, 1);
}

static void drain_gpio_events(int gpio_fd)
{
	struct pollfd dp = { .fd = gpio_fd, .events = POLLIN };
	struct gpio_v2_line_event dgev;

	while (poll(&dp, 1, 0) > 0) {
		if (read(gpio_fd, &dgev, sizeof(dgev)) != (ssize_t)sizeof(dgev))
			break;
	}
}

static void s3916_configure_reports(int i2c_fd, int gpio_fd)
{
	uint8_t code = 0, payload[MAX_PAYLOAD];
	uint16_t plen = 0;

	/* Drain any pending startup/IDENTIFY report and enable Samsung SEC reports */
	for (int i = 0; i < 3; i++)
		s3916_read_oneshot(i2c_fd, &code, &plen, payload);

	s3916_set_report(i2c_fd, REPORT_TOUCH, false);
	s3916_set_report(i2c_fd, REPORT_SEC_GESTURE, true);
	s3916_set_report(i2c_fd, REPORT_SEC_COORD, true);
	s3916_set_report(i2c_fd, REPORT_SEC_STATUS, true);

	for (int i = 0; i < 4 && gpio_read_level(gpio_fd) == 0; i++)
		s3916_read_oneshot(i2c_fd, &code, &plen, payload);
	drain_gpio_events(gpio_fd);
}

static void s3916_on_display_wake(int i2c_fd, int gpio_fd, bool use_reset)
{
	uint8_t code = 0, dummy[MAX_PAYLOAD];
	uint16_t plen = 0;
	bool rezero_ok = false;

	/* Wait for OLED ELVDD/ELVSS + 120Hz TSP_SYNC to stabilize after DSI prepare/enable */
	usleep(320000);

	for (int i = 0; i < 4 && gpio_read_level(gpio_fd) == 0; i++)
		s3916_read_oneshot(i2c_fd, &code, &plen, dummy);

	if (!use_reset) {
		rezero_ok = s3916_send_cmd(i2c_fd, TCM_CMD_REZERO, NULL, 0);
		fprintf(stderr, "[b5q-touchd] wake: REZERO -> %s\n", rezero_ok ? "OK" : "FAIL");
	}

	if (use_reset || !rezero_ok) {
		bool rst_ok = s3916_send_cmd(i2c_fd, TCM_CMD_RESET, NULL, 0);
		usleep(200000);
		s3916_configure_reports(i2c_fd, gpio_fd);
		fprintf(stderr, "[b5q-touchd] wake: RESET (%s) + reconfig -> done\n",
			rst_ok ? "OK" : "timeout");
	}

	drain_gpio_events(gpio_fd);
	fflush(stderr);
}

struct mt_state {
	bool active[MAX_SLOTS];
	int x[MAX_SLOTS];
	int y[MAX_SLOTS];
	int pressure[MAX_SLOTS];
	int next_tid;
	int last_slot;
	bool btn_touch;
};

static void release_all_slots(int ufd, struct mt_state *st)
{
	bool reported = false;

	for (int s = 0; s < MAX_SLOTS; s++) {
		if (st->active[s]) {
			if (st->last_slot != s) {
				emit_ev(ufd, EV_ABS, ABS_MT_SLOT, s);
				st->last_slot = s;
			}
			st->active[s] = false;
			emit_ev(ufd, EV_ABS, ABS_MT_TRACKING_ID, -1);
			reported = true;
		}
	}
	if (st->btn_touch) {
		emit_ev(ufd, EV_KEY, BTN_TOUCH, 0);
		emit_ev(ufd, EV_ABS, ABS_PRESSURE, 0);
		st->btn_touch = false;
		reported = true;
	}
	if (reported)
		emit_ev(ufd, EV_SYN, SYN_REPORT, 0);
}

static void handle_sec_coord(int ufd, struct mt_state *st, const uint8_t *data, uint16_t len)
{
	int count = len / 16;
	bool reported = false;

	for (int i = 0; i < count; i++) {
		const uint8_t *b = data + (i * 16);
		uint8_t eid = b[0] & 0x03;
		uint8_t slot = (b[0] >> 2) & 0x0F;
		uint8_t action = (b[0] >> 6) & 0x03;
		uint16_t x = ((uint16_t)b[1] << 4) | ((b[3] >> 4) & 0x0F);
		uint16_t y = ((uint16_t)b[2] << 4) | (b[3] & 0x0F);
		uint8_t major = b[4];
		uint8_t minor = b[5];
		uint8_t z = b[6] & 0x3F;

		if (g_debug) {
			fprintf(stderr, "  [0xC1 #%d] eid=%u slot=%u act=%u x=%u y=%u maj=%u min=%u z=%u raw=%02x%02x%02x%02x%02x%02x%02x%02x\n",
				i, eid, slot, action, x, y, major, minor, z,
				b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
		}

		if (eid != 0 || slot >= MAX_SLOTS)
			continue;

		if (action == 1 || action == 2) {
			if (st->last_slot != slot) {
				emit_ev(ufd, EV_ABS, ABS_MT_SLOT, slot);
				st->last_slot = slot;
			}
			if (!st->active[slot]) {
				st->active[slot] = true;
				st->next_tid = (st->next_tid + 1) & 0xFFFF;
				emit_ev(ufd, EV_ABS, ABS_MT_TRACKING_ID, st->next_tid);
			}
			st->x[slot] = x;
			st->y[slot] = y;
			st->pressure[slot] = z;
			emit_ev(ufd, EV_ABS, ABS_MT_POSITION_X, x);
			emit_ev(ufd, EV_ABS, ABS_MT_POSITION_Y, y);
			emit_ev(ufd, EV_ABS, ABS_MT_TOUCH_MAJOR, major);
			emit_ev(ufd, EV_ABS, ABS_MT_TOUCH_MINOR, minor);
			emit_ev(ufd, EV_ABS, ABS_MT_PRESSURE, z);
			reported = true;
		} else if (action == 3 || action == 0) {
			if (st->active[slot]) {
				if (st->last_slot != slot) {
					emit_ev(ufd, EV_ABS, ABS_MT_SLOT, slot);
					st->last_slot = slot;
				}
				st->active[slot] = false;
				emit_ev(ufd, EV_ABS, ABS_MT_TRACKING_ID, -1);
				reported = true;
			}
		}
	}

	if (!reported)
		return;

	/* Compute pointer emulation (BTN_TOUCH + ABS_X/Y from primary active slot) */
	int primary = -1;
	for (int s = 0; s < MAX_SLOTS; s++) {
		if (st->active[s]) {
			primary = s;
			break;
		}
	}

	if (primary >= 0) {
		if (!st->btn_touch) {
			emit_ev(ufd, EV_KEY, BTN_TOUCH, 1);
			st->btn_touch = true;
		}
		emit_ev(ufd, EV_ABS, ABS_X, st->x[primary]);
		emit_ev(ufd, EV_ABS, ABS_Y, st->y[primary]);
		emit_ev(ufd, EV_ABS, ABS_PRESSURE, st->pressure[primary]);
	} else {
		if (st->btn_touch) {
			emit_ev(ufd, EV_KEY, BTN_TOUCH, 0);
			emit_ev(ufd, EV_ABS, ABS_PRESSURE, 0);
			st->btn_touch = false;
		}
	}

	emit_ev(ufd, EV_SYN, SYN_REPORT, 0);
}

int main(int argc, char **argv)
{
	bool wake_reset = false;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0)
			g_debug = true;
		else if (strcmp(argv[i], "--wake-reset") == 0)
			wake_reset = true;
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	char i2c_path[512], gpio_path[512], dpms_path[512];
	dpms_path[0] = '\0';
	if (find_sysfs_child_prefix("/sys/devices/platform/i2c-tsp", "i2c-", i2c_path, sizeof(i2c_path)) < 0) {
		fprintf(stderr, "[b5q-touchd] i2c-tsp device not found\n");
		return 1;
	}
	if (find_sysfs_child_prefix("/sys/devices/platform/soc@0/f100000.pinctrl", "gpiochip", gpio_path, sizeof(gpio_path)) < 0) {
		fprintf(stderr, "[b5q-touchd] TLMM gpiochip not found\n");
		return 1;
	}
	if (find_dsi_dpms_path(dpms_path, sizeof(dpms_path)) < 0) {
		fprintf(stderr, "[b5q-touchd] warn: DSI-1 dpms sysfs not found, wake recovery disabled\n");
	}

	int i2c_fd = open(i2c_path, O_RDWR);
	if (i2c_fd < 0) {
		perror(i2c_path);
		return 1;
	}
	if (ioctl(i2c_fd, I2C_SLAVE, S3916_I2C_ADDR) < 0) {
		perror("I2C_SLAVE");
		close(i2c_fd);
		return 1;
	}

	int gpio_fd = setup_gpio_irq(gpio_path, S3916_ATTN_GPIO);
	if (gpio_fd < 0) {
		close(i2c_fd);
		return 1;
	}

	int ufd = setup_uinput();
	if (ufd < 0) {
		close(gpio_fd);
		close(i2c_fd);
		return 1;
	}

	s3916_configure_reports(i2c_fd, gpio_fd);

	bool disp_on = read_display_on(dpms_path);
	fprintf(stderr, "[b5q-touchd] Ready on %s (0x%02x) + %s line %d dpms=%s (%s) -> uinput (4096x4096, 10 slots)\n",
		i2c_path, S3916_I2C_ADDR, gpio_path, S3916_ATTN_GPIO,
		dpms_path[0] ? dpms_path : "none", disp_on ? "On" : "Off");
	fflush(stderr);

	struct mt_state st;
	memset(&st, 0, sizeof(st));
	st.last_slot = -1;

	struct pollfd pfd = { .fd = gpio_fd, .events = POLLIN };
	struct gpio_v2_line_event gev;
	uint8_t code = 0, payload[MAX_PAYLOAD];
	uint16_t plen = 0;

	while (!g_stop) {
		/* Sleep in kernel until next falling edge on GPIO 19 or 100ms DPMS check */
		int pr = poll(&pfd, 1, 100);
		bool got_irq = false;
		if (pr > 0 && (pfd.revents & POLLIN)) {
			got_irq = true;
			while (read(gpio_fd, &gev, sizeof(gev)) == (ssize_t)sizeof(gev)) {
				struct pollfd cp = { .fd = gpio_fd, .events = POLLIN };
				if (poll(&cp, 1, 0) <= 0)
					break;
			}
		}

		if (dpms_path[0]) {
			bool now_on = read_display_on(dpms_path);
			if (!now_on && disp_on) {
				disp_on = false;
				release_all_slots(ufd, &st);
				fprintf(stderr, "[b5q-touchd] display -> Off\n");
				fflush(stderr);
			} else if (now_on && !disp_on) {
				disp_on = true;
				fprintf(stderr, "[b5q-touchd] display -> On\n");
				fflush(stderr);
				s3916_on_display_wake(i2c_fd, gpio_fd, wake_reset);
				got_irq = false;
			}
		}

		/* Always read at least once on IRQ, then drain while ATTN (GPIO 19) stays LOW */
		int guard = 0;
		while (!g_stop && (got_irq || gpio_read_level(gpio_fd) == 0) && guard++ < 64) {
			got_irq = false;
			if (s3916_read_oneshot(i2c_fd, &code, &plen, payload) == 0) {
				if (code == REPORT_SEC_COORD && plen >= 16) {
					handle_sec_coord(ufd, &st, payload, plen);
				} else if (code == REPORT_IDENTIFY) {
					fprintf(stderr, "[b5q-touchd] S3916A IDENTIFY (chip reset detected) -> reconfiguring reports\n");
					fflush(stderr);
					s3916_configure_reports(i2c_fd, gpio_fd);
				} else if (code == REPORT_SEC_STATUS && plen > 0) {
					fprintf(stderr, "[b5q-touchd] status 0xC2 len=%u:", plen);
					for (uint16_t k = 0; k < plen && k < 16; k++)
						fprintf(stderr, " %02x", payload[k]);
					fprintf(stderr, "\n");
					fflush(stderr);
				} else if (g_debug && code >= 0x10 && code != 0xFF) {
					fprintf(stderr, "  [pkt] code=0x%02x plen=%u\n", code, plen);
				}
			} else {
				usleep(500);
			}
		}
	}

	ioctl(ufd, UI_DEV_DESTROY);
	close(ufd);
	close(gpio_fd);
	close(i2c_fd);
	return 0;
}
