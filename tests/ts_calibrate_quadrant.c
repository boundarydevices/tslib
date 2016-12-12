/*
 *  tslib/tests/ts_calibrate.c
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This file is placed under the GPL.  Please see the file
 * COPYING for more details.
 *
 * $Id: ts_calibrate.c,v 1.8 2004/10/19 22:01:27 dlowder Exp $
 *
 * Basic test program for touchscreen library.
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <getopt.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/fb.h>
#include <linux/input.h>
#include "tslib-private.h"

#include "tslib.h"

#include "fbutils.h"
#include "testutils.h"
#include "tsquadrant_cal.h"

static int palette [] =
{
	0x000000, 0xffe080, 0xffffff, 0xe0c0a0
};
#define NR_COLORS (sizeof (palette) / sizeof (palette [0]))

static void sig(int sig)
{
	close_framebuffer ();
	fflush (stderr);
	printf ("signal %d caught\n", sig);
	fflush (stdout);
	exit (1);
}

static void get_sample (struct tsdev *ts, struct cal_data *cal,
		int x, int y, char *name)
{
	static int last_x = -1, last_y;

	if (last_x != -1) {
#define NR_STEPS 10
		int dx = ((x - last_x) << 16) / NR_STEPS;
		int dy = ((y - last_y) << 16) / NR_STEPS;
		int i;
		last_x <<= 16;
		last_y <<= 16;
		for (i = 0; i < NR_STEPS; i++) {
			put_cross (last_x >> 16, last_y >> 16, 2 | XORMODE);
			usleep (1000);
			put_cross (last_x >> 16, last_y >> 16, 2 | XORMODE);
			last_x += dx;
			last_y += dy;
		}
	}

	put_cross(x, y, 2 | XORMODE);
	getxy(ts, (int*)&cal->i, (int*)&cal->j);
	put_cross(x, y, 2 | XORMODE);

	last_x = cal->x = x;
	last_y = cal->y = y;

	printf("%s : X = %4d Y = %4d\n", name, cal->i, cal->j);
}

struct opts {
	int xinput_format;
};

void print_usage(void)
{
	printf("Usage: ts_calibrate_quadrant [OPTIONS...]\n"
		"Where OPTIONS are\n"
		"   -h --help		Show this help\n"
		"   -x --xinput		xinput output format\n"
		"   -r --rotate180	screen is upside down\n"
		"   -R --rotate_right	rotate 90 degrees right(cw)\n"
		"   -L --rotate_left	rotate 90 degrees left(ccw)\n"
		"   -m --rotate_mode n	0 - normal, 1 - vflip, 2 - hflip, 3 - 180,\n"
		"\t\t4 - swap x/y, 5 - right 90(cw), 6 - left 90(ccw), 7 - swap x/y 180\n"
		"\n");
}

int parse_opts(int argc, char * const *argv, struct opts *opts)
{
	int c;

	static struct option long_options[] = {
		{"help",	no_argument, 		0, 'h' },
		{"rotate180",   no_argument,            0, 'r' },
		{"xinput",	no_argument, 		0, 'x' },
		{"rotate_right", no_argument,		0, 'R' },
		{"rotate_left", no_argument,		0, 'L' },
		{"rotate_mode", required_argument,	0, 'm' },
		{0,		0,			0, 0 },
	};

	while ((c = getopt_long(argc, argv, "+hxrRLm:", long_options, NULL)) != -1) {
		switch (c)
		{
		case 'x':
			opts->xinput_format = 1;
			break;
		case 'r':
			rotate_mode = ROTATE_180;
			break;
		case 'R':
			rotate_mode = ROTATE_90_RIGHT;
			break;
		case 'L':
			rotate_mode = ROTATE_90_LEFT;
			break;
		case 'm' :
			sscanf(optarg, "%i", &rotate_mode);
			if (rotate_mode > 7)
				rotate_mode = 0;
			break;
		case 'h':
		case '?':
		default:
			print_usage();
			return -1;
		}
	}
	return 0;
}

int main(int argc, char * const argv[])
{
	struct tsdev *ts;
	struct cal_data cal[9];
	struct cal_result res[5];
	int cal_fd;
	char cal_buffer[256];
	char device_name[260];
	char *calfile = NULL;
	unsigned int i;
	int r;
	int dx, dy;
	unsigned nconst = 6;
	unsigned ngroups = 5;
	unsigned npoints;
	struct input_absinfo abs;
	int iMax = 2048;
	int jMax = 2048;
	struct opts opts;
	int err;

	memset(&opts, 0, sizeof(struct opts));
	err = parse_opts(argc, argv, &opts);
	if (err)
		exit(1);

	signal(SIGSEGV, sig);
	signal(SIGINT, sig);
	signal(SIGTERM, sig);

	/* get xres, yres */
	if (open_framebuffer()) {
		close_framebuffer();
		exit(1);
	}

	ts = ts_open_config(0, xres, yres);
	if (!ts) {
		perror("ts_open_config");
		exit(1);
	}

	if (ioctl(ts->fd, EVIOCGABS(0), &abs) == 0) {
		iMax = abs.maximum + 1;
		printf("iMax = %d\n", iMax);
	} else {
		printf("iMax read error, defaulting to 2048\n");
	}
	if (ioctl(ts->fd, EVIOCGABS(1), &abs) == 0) {
		jMax = abs.maximum + 1;
		printf("jMax = %d\n", jMax);
	} else {
		printf("jMax read error, defaulting to 2048\n");
	}
	i = ioctl(ts->fd, EVIOCGNAME(256), device_name);
	if (i > 0) {
		device_name[i] = 0;
		printf("device_name = %s\n", device_name);
	} else {
		printf("device_name read error\n");
		device_name[0] = 0;
	}

	for (i = 0; i < NR_COLORS; i++)
		setcolor (i, palette [i]);

	put_string_center (xres / 2, yres / 4,
			   "TSLIB calibration utility", 1);
	put_string_center (xres / 2, yres / 4 + 20,
			   "Touch crosshair to calibrate", 2);

	printf("xres = %d, yres = %d\n", xres, yres);

// Read a touchscreen event to clear the buffer
	//getxy(ts, 0, 0);

	dy = 50;
	dx = (dy * xres) / yres;
	npoints = opts.xinput_format ? 5 : 9;

	if (PT_LT < npoints)
		get_sample(ts, &cal[PT_LT], dx,            dy,            "left top");
	if (PT_MT < npoints)
		get_sample(ts, &cal[PT_MT], xres / 2,      dy,            "mid top ");
	if (PT_RT < npoints)
		get_sample(ts, &cal[PT_RT], xres - 1 - dx, dy,            "right top");

	if (PT_LM < npoints)
		get_sample(ts, &cal[PT_LM], dx,            yres / 2,      "left mid");
	if (PT_MM < npoints)
		get_sample(ts, &cal[PT_MM], xres / 2,      yres / 2,      "Center");
	if (PT_RM < npoints)
		get_sample(ts, &cal[PT_RM], xres - 1 - dx, yres / 2,      "right mid");

	if (PT_LB < npoints)
		get_sample(ts, &cal[PT_LB], dx,            yres - 1 - dy, "left bottom");
	if (PT_MB < npoints)
		get_sample(ts, &cal[PT_MB], xres / 2,      yres - 1 - dy, "mid bottom");
	if (PT_RB < npoints)
		get_sample(ts, &cal[PT_RB], xres - 1 - dx, yres - 1 - dy, "right bottom");

	r = perform_n_point_calibration(cal, npoints, xres, yres, iMax, jMax, res);
	if (r >= 0) {
		int ret;
		unsigned q;

		nconst = 12;
		ngroups = 1;

		printf ("Calibration constants: ");
		for (q = 0; q < ngroups; q++) {
			for (i = 0; i < nconst; i++)
				printf("%d ", res[q].a[i]);
			printf("shift %d\n", res[q].shift);
		}
		calfile = getenv("TSLIB_CALIBFILE");
		if (!calfile)
			calfile = TS_POINTERCAL;
		cal_fd = open(calfile, O_TRUNC | O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
		printf("calibrate file: %s\n", calfile);
		for (i = 0; i < npoints; i++) {
			struct cal_data *d = &cal[i];
			int length = sprintf(cal_buffer,"(%d,%d)(%d,%d)\n",
				 d->x, d->y, d->i, d->j);
			ret = write(cal_fd, cal_buffer, length);
//			printf("%s", cal_buffer);
			if (ret < length)
				printf("write returned %d, expected %d\n", ret, length);
		}
		close(cal_fd);

		if (ngroups == 1) {
			char *p = strncpy(cal_buffer, calfile, sizeof(cal_buffer) - 3);
			int nleft;
			int len;
			p[sizeof(cal_buffer) - 3] = 0;
			p += strlen(p);
			*p++ = '_';
			*p++ = opts.xinput_format ? 'x' : 'c';
			*p = 0;

			cal_fd = open(cal_buffer, O_TRUNC | O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
			printf("coefficient file: %s\n", cal_buffer);
			p = cal_buffer;
			nleft = sizeof(cal_buffer);
			if (opts.xinput_format) {
				float f[6];

				f[2] = (float)res[0].a[0] / 65536;
				f[0] = (float)res[0].a[1] / 65536;
				f[1] = (float)res[0].a[2] / 65536;
				f[5] = (float)res[0].a[6] / 65536;
				f[3] = (float)res[0].a[7] / 65536;
				f[4] = (float)res[0].a[8] / 65536;
				len = snprintf(p, nleft, "\"%s\" \"Coordinate Transformation Matrix\" "
						"%.5f %.5f %.5f %.5f %.5f %.5f 0 0 1\n",
						device_name, f[0], f[1], f[2], f[3], f[4], f[5]);
				if (nleft > len) {
					nleft -= len;
					p += len;
				}
			} else {
				for (i = 0; i < nconst; i++) {
					len = snprintf(p, nleft, (i != (nconst - 1)) ? "%d," : "%d\n", res[0].a[i]);

					if (len >= nleft)
						break;
					nleft -= len;
					p += len;
				}
			}
			len = sizeof(cal_buffer) - nleft;
			ret = write(cal_fd, cal_buffer, len);
			if (ret < len)
				printf("write returned %d, expected %d\n", ret, len);
			close(cal_fd);
		}
                i = 0;
	} else {
		printf("Calibration failed.\n");
		i = -1;
	}

	close_framebuffer();
	return i;
}
