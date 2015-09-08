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

int main()
{
	struct tsdev *ts;
	struct cal_data cal[9];
	struct cal_result res[5];
	int cal_fd;
	char cal_buffer[256];
	char *tsdevice = NULL;
	char *calfile = NULL;
	unsigned int i;
	int r;
	int r1;
	int dx, dy;
	unsigned nconst = 6;
	unsigned ngroups = 5;
	unsigned npoints = 5;
	struct input_absinfo abs;
	int iMax = 2048;
	int jMax = 2048;

	signal(SIGSEGV, sig);
	signal(SIGINT, sig);
	signal(SIGTERM, sig);

	if( (tsdevice = getenv("TSLIB_TSDEVICE")) != NULL ) {
		ts = ts_open(tsdevice,0);
	} else {
		if (!(ts = ts_open("/dev/input/event0", 0)))
			ts = ts_open("/dev/touchscreen/ucb1x00", 0);
	}

	if (!ts) {
		perror("ts_open");
		exit(1);
	}
	if (ts_config(ts)) {
		perror("ts_config");
		exit(1);
	}

	/* get xres, yres */
	if (open_framebuffer()) {
		close_framebuffer();
		exit(1);
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
	get_sample(ts, &cal[PT_LT], dx,            dy,            "left top");
	get_sample(ts, &cal[PT_MT], xres / 2,      dy,            "mid top ");
	get_sample(ts, &cal[PT_RT], xres - 1 - dx, dy,            "right top");

	get_sample(ts, &cal[PT_LM], dx,            yres / 2,      "left mid");
	get_sample(ts, &cal[PT_MM], xres / 2,      yres / 2,      "Center");
	get_sample(ts, &cal[PT_RM], xres - 1 - dx, yres / 2,      "right mid");

	get_sample(ts, &cal[PT_LB], dx,            yres - 1 - dy, "left bottom");
	get_sample(ts, &cal[PT_MB], xres / 2,      yres - 1 - dy, "mid bottom");
	get_sample(ts, &cal[PT_RB], xres - 1 - dx, yres - 1 - dy, "right bottom");

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

	r = perform_q_calibration(cal, res);
	r1 = perform_n_point_calibration(cal, 9, xres, yres, iMax, jMax, res);
	if (r >= 0) {
		r = r1;
		nconst = 12;
		ngroups = 1;
		npoints = 9;
	}

	if (r >= 0) {
		int ret;
		unsigned q;

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
                i = 0;
	} else {
		printf("Calibration failed.\n");
		i = -1;
	}

	close_framebuffer();
	return i;
}
