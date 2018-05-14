/*
 *  tslib/src/ts_open.c
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This file is placed under the LGPL.  Please see the file
 * COPYING for more details.
 *
 * SPDX-License-Identifier: LGPL-2.1
 *
 *
 * Open a touchscreen device.
 */
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/fcntl.h>
#include <stdio.h>
#include <linux/fb.h>
#include <linux/input.h>

#include "tslib-private.h"
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#ifdef DEBUG
#include <stdio.h>
#endif

extern struct tslib_module_info __ts_raw;

#ifdef DEBUG
/* verify we use the correct OS specific code at runtime.
 * If we use it, test it here!
 */
static void print_host_os(void)
{
	printf("%s ", tslib_version());

#if defined (__linux__)
	printf("Host OS: Linux");
#elif defined (__FreeBSD__)
	printf("Host OS: FreeBSD");
#elif defined (__OpenBSD__)
	printf("Host OS: OpenBSD");
#elif defined (__GNU__) && defined (__MACH__)
	printf("Host OS: Hurd");
#elif defined (__HAIKU__)
	printf("Host OS: Haiku");
#elif defined (__BEOS__)
	printf("Host OS: BeOS");
#elif defined (WIN32)
	printf("Host OS: Windows");
#elif defined (__APPLE__) && defined (__MACH__)
	printf("Host OS: Darwin");
#else
	printf("Host OS: unknown");
#endif
}
#endif /* DEBUG */

int (*ts_open_restricted)(const char *path, int flags, void *user_data) = NULL;

static struct tsdev *ts_open_xy(const char *name, int nonblock, int xres, int yres)
{
	struct tsdev *ts;
	int flags = O_RDWR;

#ifdef DEBUG
	print_host_os();
	printf(", trying to open %s\n", name);
#endif

	if (nonblock) {
	#ifndef WIN32
		flags |= O_NONBLOCK;
	#endif
	}

	ts = malloc(sizeof(struct tsdev));
	if (!ts)
		return NULL;

	memset(ts, 0, sizeof(struct tsdev));
	ts->xres = xres;
	ts->yres = yres;

	ts->eventpath = strdup(name);
	if (!ts->eventpath)
		goto free;

	printf("%s:%s:screen resolution = %dx%d\n", __func__, name, ts->xres, ts->yres);
	if (ts_open_restricted) {
		ts->fd = ts_open_restricted(name, flags, NULL);
		if (ts->fd == -1)
			goto free;

		return ts;
	}

	ts->fd = open(name, flags);
	/*
	 * Try again in case file is simply not writable
	 * It will do for most drivers
	 */
	if (ts->fd == -1 && errno == EACCES) {
	#ifndef WIN32
		flags = nonblock ? (O_RDONLY | O_NONBLOCK) : O_RDONLY;
	#else
		flags = O_RDONLY;
	#endif
		ts->fd = open(name, flags);
	}
	if (ts->fd == -1)
		goto free;

	return ts;

free:
	if (ts->eventpath)
		free(ts->eventpath);

	free(ts);
	return NULL;
}

static const char * const input_choices[] = {"/dev/input/ts",
	"/dev/input/event0", "/dev/input/event1",
	"/dev/input/event2", "/dev/touchscreen/ucb1x00",
};

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define BIT(nr)                 (1UL << (nr))
#define BIT_MASK(nr)            (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)            ((nr) / BITS_PER_LONG)
#define BITS_PER_BYTE           8
#define BITS_PER_LONG           (sizeof(long) * BITS_PER_BYTE)
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))
#ifndef EV_CNT
#define EV_CNT	(EV_MAX+1)
#endif

static int is_touchscreen(struct tsdev *ts)
{
	int ret;
	long evbit[BITS_TO_LONGS(EV_CNT)];
	long absbit[BITS_TO_LONGS(ABS_CNT)];

	ret = ioctl(ts->fd, EVIOCGBIT(0, sizeof(evbit)), evbit);
	if ( (ret < 0) || !(evbit[BIT_WORD(EV_ABS)] & BIT_MASK(EV_ABS)) ||
		!(evbit[BIT_WORD(EV_KEY)] & BIT_MASK(EV_KEY)) ) {
		return 0;
	}

	ret = ioctl(ts->fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit);
	if ( (ret < 0) || !(absbit[BIT_WORD(ABS_X)] & BIT_MASK(ABS_X)) ||
		!(absbit[BIT_WORD(ABS_Y)] & BIT_MASK(ABS_Y))) {
		return 0;
	}
	return 1;
}

struct tsdev *ts_try_dev(const char *p, int nonblock, int xres, int yres)
{
	struct tsdev *ts;

	if (!p)
		return NULL;

	ts = ts_open_xy(p, nonblock, xres, yres);
	if (!ts)
		return ts;
	if (is_touchscreen(ts))
		return ts;
	ts_close(ts);
	return NULL;
}

struct tsdev *ts_try_open(int nonblock, int xres, int yres, int index, const char **pp)
{
	const char *p = NULL;

	if (index == -1) {
		p = getenv("TSLIB_TSDEVICE");
	} else if (index < (int)ARRAY_SIZE(input_choices)) {
		p = input_choices[index];
	}
	*pp = p;
	return ts_try_dev(p, nonblock, xres, yres);
}

struct tsdev *ts_open_config(int nonblock, int xres, int yres)
{
	struct tsdev *ts;
	const char *p;
	int i;

	for (i = -1; i < (int)ARRAY_SIZE(input_choices); i++) {
		ts = ts_try_open(nonblock, xres, yres, i, &p);
		if (ts) {
			if (!ts_config(ts)) {
				printf("opened:%s\n", p);
				return ts;
			}
			perror("ts_config");
			ts_close(ts);
		}
	}
	return NULL;
}

static char *defaultfbdevice = "/dev/fb0";

struct tsdev *ts_open(const char *name, int nonblock)
{
	struct fb_var_screeninfo var;
	struct tsdev *ts;
	char *fbdevice;
	const char *p;
	int fb;
	int xres = 0;
	int yres = 0;
	int i;

	fbdevice = getenv("TSLIB_FBDEVICE");
	if (!fbdevice)
		fbdevice = defaultfbdevice;

	fb = open(fbdevice, O_RDWR);
	if (fb == -1) {
		perror("open fbdevice");
		goto c1;
	}

	if (ioctl(fb, FBIOGET_VSCREENINFO, &var) < 0) {
		perror("ioctl FBIOGET_VSCREENINFO");
		goto c1;
	}

	xres = var.xres;
	yres = var.yres;
c1:
	close(fb);

	ts = ts_try_dev(name, nonblock, xres, yres);
	if (ts) {
		printf("opened:%s\n", p);
		return ts;
	}
	for (i = -1; i < (int)ARRAY_SIZE(input_choices); i++) {
		ts = ts_try_open(nonblock, xres, yres, i, &p);
		if (ts) {
			printf("opened:%s\n", p);
			return ts;
		}
	}
	return NULL;
}
