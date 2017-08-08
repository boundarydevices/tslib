/*
 *  tslib/plugins/linear.c
 *
 *  Copyright (C) 2001 Russell King.
 *  Copyright (C) 2005 Alberto Mardegan <mardy@sourceforge.net>
 *
 * This file is placed under the LGPL.  Please see the file
 * COPYING for more details.
 *
 *
 * Linearly scale touchscreen values
 */
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <linux/input.h>

#include "config.h"
#include "tslib-private.h"
#include "tslib-filter.h"

struct tslib_linear {
	struct tslib_module_info module;
	int	swap_xy;

// Linear scaling and offset parameters for pressure
	int	p_offset;
	int	p_mult;
	int	p_div;

	int	xMax;
	int	yMax;
	int	iMax;
	int	jMax;
// Linear scaling and offset parameters for x,y (can include rotation)
	int	a[12];
};

#define u64 unsigned long long
#define s32 int
#define u32 unsigned int
#define s64 long long

static void transform6(struct tslib_linear *lin, struct ts_sample *samp)
{
	u32 s[6];
	s64 xsum, ysum;
	s32 cx, cy;
	int xMax = lin->xMax;
	int yMax = lin->yMax;
	int i;

#ifdef DEBUG
	fprintf(stderr,"BEFORE CALIB--------------------> %d %d %d\n",samp->x, samp->y, samp->pressure);
#endif /*DEBUG*/

	cx = samp->x;
	cy = samp->y;
	if ((cx >= lin->iMax) || (cy >= lin->jMax))
		printf("!!!%s:i=%d imax=%d, j=%d jmax=%d\n", __func__,
				cx, lin->iMax, cy, lin->jMax);
	s[0] = 1 << 16;
	s[1] = (cx << 16) / lin->iMax;
	s[2] = (cy << 16) / lin->jMax;
	s[3] = (s[1] * s[2]) >> 16;
	s[4] = (s[1] * s[1]) >> 16;
	s[5] = (s[2] * s[2]) >> 16;

	xsum = 0;
	ysum = 0;
	for (i = 0; i < 6 ; i++) {
		xsum += (lin->a[i] * (s64)s[i]);
		ysum += (lin->a[i + 6] * (s64)s[i]);
	}
	cx = (s32)((xsum * xMax) >> 32);
	cy = (s32)((ysum * yMax) >> 32);
	if (cx < 0)
		cx = 0;
	if (cy < 0)
		cy = 0;
	if (cx >= xMax)
		cx = xMax - 1;
	if (cy >= yMax)
		cy = yMax - 1;
#ifdef DEBUG
	printf("%s: max(%d,%d) (%d,%d)->(%d,%d)\n", __func__, xMax, yMax, samp->x, samp->y, cx, cy);
#endif
	samp->x = cx;
	samp->y = cy;
}

static int
linear_read(struct tslib_module_info *info, struct ts_sample *samp, int nr)
{
	struct tslib_linear *lin = (struct tslib_linear *)info;
	int ret;

	ret = info->next->ops->read(info->next, samp, nr);
	if (ret >= 0) {
		int nr;

		for (nr = 0; nr < ret; nr++, samp++) {
#ifdef DEBUG
			fprintf(stderr,"BEFORE CALIB--------------------> %d %d %d\n",samp->x, samp->y, samp->pressure);
#endif /*DEBUG*/
			transform6(lin, samp);

			samp->pressure = ((samp->pressure + lin->p_offset)
						 * lin->p_mult) / lin->p_div;
			if (lin->swap_xy) {
				int tmp = samp->x;
				samp->x = samp->y;
				samp->y = tmp;
			}
		}
	}

	return ret;
}

static int linear_fini(struct tslib_module_info *info)
{
	free(info);
	return 0;
}

static const struct tslib_ops linear_ops =
{
	.read	= linear_read,
	.fini	= linear_fini,
};

static int linear_xyswap(struct tslib_module_info *inf, char *str, void *data)
{
	struct tslib_linear *lin = (struct tslib_linear *)inf;

	lin->swap_xy = 1;
	return 0;
}

static int linear_p_offset(struct tslib_module_info *inf, char *str, void *data)
{
	struct tslib_linear *lin = (struct tslib_linear *)inf;

	unsigned long offset = strtoul(str, NULL, 0);

	if(offset == ULONG_MAX && errno == ERANGE)
		return -1;

	lin->p_offset = offset;
	return 0;
}

static int linear_p_mult(struct tslib_module_info *inf, char *str, void *data)
{
	struct tslib_linear *lin = (struct tslib_linear *)inf;
	unsigned long mult = strtoul(str, NULL, 0);

	if(mult == ULONG_MAX && errno == ERANGE)
		return -1;

	lin->p_mult = mult;
	return 0;
}

static int linear_p_div(struct tslib_module_info *inf, char *str, void *data)
{
	struct tslib_linear *lin = (struct tslib_linear *)inf;
	unsigned long div = strtoul(str, NULL, 0);

	if(div == ULONG_MAX && errno == ERANGE)
		return -1;

	lin->p_div = div;
	return 0;
}

static const struct tslib_vars linear_vars[] =
{
	{ "xyswap",	(void *)1, linear_xyswap },
        { "pressure_offset", NULL , linear_p_offset},
        { "pressure_mul", NULL, linear_p_mult},
        { "pressure_div", NULL, linear_p_div},
};

#define NR_VARS (sizeof(linear_vars) / sizeof(linear_vars[0]))

TSAPI struct tslib_module_info *linear_mod_init(struct tsdev *ts, const char *params)
{

	struct tslib_linear *lin;
	struct stat sbuf;
	FILE *pcal_fd;
	int index;
	char *calfile;
	struct input_absinfo abs;

	lin = malloc(sizeof(struct tslib_linear));
	if (lin == NULL)
		return NULL;

	lin->module.ops = &linear_ops;

// Use default values that leave ts numbers unchanged after transform
	lin->a[0] = 0;
	lin->a[1] = 65536;
	lin->a[2] = 0;
	lin->a[3] = 0;
	lin->a[4] = 0;
	lin->a[5] = 0;

	lin->a[6] = 0;
	lin->a[7] = 0;
	lin->a[8] = 65536;
	lin->a[9] = 0;
	lin->a[10] = 0;
	lin->a[11] = 0;
	lin->p_offset = 0;
	lin->p_mult   = 1;
	lin->p_div    = 1;
	lin->swap_xy  = 0;
	lin->xMax = ts->xres;
	lin->yMax = ts->yres;
#ifdef DEBUG
	printf("screen resolution = %dx%d\n", lin->xMax, lin->yMax);
#endif
	if (ioctl(ts->fd, EVIOCGABS(0), &abs) == 0) {
		lin->iMax = abs.maximum + 1;
		printf("iMax = %d\n", lin->iMax);
	} else {
		printf("iMax read error, defaulting to 2048\n");
	}
	if (ioctl(ts->fd, EVIOCGABS(1), &abs) == 0) {
		lin->jMax = abs.maximum + 1;
		printf("jMax = %d\n", lin->jMax);
	} else {
		printf("jMax read error, defaulting to 2048\n");
	}
	/*
	 * Check calibration file
	 */
	if( (calfile = getenv("TSLIB_CALIBFILE")) == NULL) calfile = TS_POINTERCAL;
	if (stat(calfile, &sbuf)==0) {
		pcal_fd = fopen(calfile, "r");
		for (index = 0; index < 12; index++)
			if (fscanf(pcal_fd, "%d", &lin->a[index]) != 1) break;
#ifdef DEBUG
		printf("Linear calibration constants: ");
		for (index = 0; index < 12; index++) printf("%d ",lin->a[index]);
		printf("\n");
#endif /*DEBUG*/
		fclose(pcal_fd);
	}
		
		
	/*
	 * Parse the parameters.
	 */
	if (tslib_parse_vars(&lin->module, linear_vars, NR_VARS, params)) {
		free(lin);
		return NULL;
	}

	return &lin->module;
}

#ifndef TSLIB_STATIC_LINEAR_MODULE
	TSLIB_MODULE_INIT(linear_mod_init);
#endif
