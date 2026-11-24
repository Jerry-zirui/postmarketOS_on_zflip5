// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Jerry-zirui (https://github.com/Jerry-zirui/postmarketOS_on_zflip5)
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define TLMM_DEV_NODE "f100000.pinctrl"
#define COVER_RESET_OFFSET 42

/* Hold the cover OLED (B5_S6E3FC5, TLMM GPIO 42) in hardware reset (LOW). */
static int find_tlmm_dev(char *out_path, size_t out_sz) {
	DIR *d = opendir("/sys/bus/gpio/devices");
	struct dirent *ent;

	if (!d)
		return -1;

	while ((ent = readdir(d)) != NULL) {
		char sys_path[PATH_MAX];
		char *real_path;

		if (strncmp(ent->d_name, "gpiochip", 8) != 0)
			continue;
		snprintf(sys_path, sizeof(sys_path), "/sys/bus/gpio/devices/%s", ent->d_name);
		real_path = realpath(sys_path, NULL);
		if (!real_path)
			continue;
		if (strstr(real_path, TLMM_DEV_NODE)) {
			free(real_path);
			snprintf(out_path, out_sz, "/dev/%s", ent->d_name);
			closedir(d);
			return 0;
		}
		free(real_path);
	}
	closedir(d);
	errno = ENODEV;
	return -1;
}

int main(int argc, char **argv) {
	int hold = (argc > 1 && strcmp(argv[1], "--hold") == 0);
	char dev_path[512];
	int chip_fd;
	struct gpio_v2_line_request req;
	struct gpio_v2_line_values vals;

	if (find_tlmm_dev(dev_path, sizeof(dev_path)) < 0) {
		perror("find_tlmm_dev");
		return 1;
	}

	chip_fd = open(dev_path, O_RDWR | O_CLOEXEC);
	if (chip_fd < 0) {
		perror("open gpiochip");
		return 1;
	}

	memset(&req, 0, sizeof(req));
	req.offsets[0] = COVER_RESET_OFFSET;
	req.num_lines = 1;
	snprintf(req.consumer, sizeof(req.consumer), "b5q-cover-off");
	req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
	req.config.num_attrs = 1;
	req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
	req.config.attrs[0].attr.values = 0;
	req.config.attrs[0].mask = 1;

	if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
		perror("GPIO_V2_GET_LINE_IOCTL");
		close(chip_fd);
		return 1;
	}

	memset(&vals, 0, sizeof(vals));
	vals.mask = 1;
	vals.bits = 0;
	if (ioctl(req.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) < 0) {
		perror("GPIO_V2_LINE_SET_VALUES_IOCTL");
		close(req.fd);
		close(chip_fd);
		return 1;
	}

	if (hold)
		pause();

	close(req.fd);
	close(chip_fd);
	return 0;
}
