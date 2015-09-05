/*
 *  tslib/plugins/linear.c
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This file is placed under the LGPL.  Please see the file
 * COPYING for more details.
 *
 * $Id: linear.c,v 1.10 2005/02/26 01:47:23 kergoth Exp $
 *
 * Linearly scale touchscreen values
 */
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <linux/input.h>

#include "tslib.h"
#include "tslib-filter.h"
#include "tsquadrant_cal.h"
#include "tslib-private.h"

struct tslib_linear {
	struct tslib_module_info module;

// Linear scaling and offset parameters for x,y (can include rotation)
	struct cal_result res[5];
	int xMax;
	int yMax;
	int iMax;
	int jMax;
	int ncoeffs;
	int nregions;
};

static void transform(struct tslib_linear *lin, struct ts_sample *samp)
{
	int x, y, cx, cy;
	int xMax = lin->xMax;
	int yMax = lin->yMax;
	int q = QUAD_MAIN;

#ifdef DEBUG
	fprintf(stderr,"BEFORE CALIB--------------------> %d %d %d\n",samp->x, samp->y, samp->pressure);
#endif /*DEBUG*/
	for (;;) {
		long long int t1, t2;
		struct cal_result *r = &lin->res[q];
		x = samp->x;
		y = samp->y;
		t1 = r->a[0];
		t1 *= x;
		t2 = r->a[1];
		t2 *= y;
		cx = (int)(t1 + t2 + r->a[2]);
#ifdef DEBUG
		printf("t1 %x %x, t2 %x %x, a0 %x, a1 %x, a2 %x, shift %d\n",
				(unsigned)(t1 >> 32), (unsigned)t1,
				(unsigned)(t2 >> 32), (unsigned)t2,
				r->a[0], r->a[1], r->a[2], r->shift);
#endif
		t1 = r->a[3];
		t1 *= x;
		t2 = r->a[4];
		t2 *= y;
		cy = (int)((t1 + t2 + r->a[5]));
		if (r->shift < 0) {
			cx >>= -r->shift;
			cy >>= -r->shift;
		} else {
			cx <<= r->shift;
			cy <<= r->shift;
		}
		if (cx < 0)
			cx = 0;
		if (cy < 0)
			cy = 0;
		if (!xMax || !yMax)
			break;
		if (cx >= xMax)
			cx = xMax - 1;
		if (cy >= yMax)
			cy = yMax - 1;
		if (q != QUAD_MAIN)
			break;
		if (cx) {
			int yMaxCx = yMax * cx;
			int xMaxCy = xMax * cy;
			int xMaxRevCy = (yMax - cy) * xMax;
			/* if (cy/cx >= yMax/xMax) */
			if (xMaxCy >= yMaxCx) {
				/* if ((yMax - cy)/cx >= yMax/xMax) */
				q = (xMaxRevCy >= yMaxCx) ? QUAD_LEFT : QUAD_BOTTOM;
			} else {
				q = (xMaxRevCy >= yMaxCx) ? QUAD_TOP : QUAD_RIGHT;
			}
		} else {
			q = QUAD_LEFT;
		}
	}
	samp->x = cx;
	samp->y = cy;
#ifdef DEBUG
	printf("%s: %d,%d\n", __func__, cx, cy);
#endif
}

static void transform6(struct tslib_linear *lin, struct ts_sample *samp)
{
	u32 s[6];
	s64 xsum, ysum;
	s32 cx, cy;
	int xMax = lin->xMax;
	int yMax = lin->yMax;
	struct cal_result *r = &lin->res[0];
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
		xsum += (r->a[i] * (s64)s[i]);
		ysum += (r->a[i + 6] * (s64)s[i]);
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
	samp->x = cx;
	samp->y = cy;
#ifdef DEBUG
	printf("%s: %d,%d\n", __func__, cx, cy);
#endif
}

static int
linearq_read(struct tslib_module_info *info, struct ts_sample *samp, int nr)
{
	struct tslib_linear *lin = (struct tslib_linear *)info;
	int ret;

	ret = info->next->ops->read(info->next, samp, nr);
	if (ret >= 0) {
		int nr;
		for (nr = 0; nr < ret; nr++, samp++) {
			if (lin->xMax && lin->nregions == 1)
				transform6(lin, samp);
			else
				transform(lin, samp);
		}
	}
	return ret;
}

static int linearq_fini(struct tslib_module_info *info)
{
	free(info);
	return 0;
}

static const struct tslib_ops linear_ops =
{
	.read	= linearq_read,
	.fini	= linearq_fini,
};

const char *past_delim(const char* p, const char* delim, int count)
{
	for (;;) {
		char c = p[0];
		int i = 0;
		if (!c)
			return p;
		for (;;) {
			if (c == delim[i])
				break;
			i++;
			if (i >= count)
				return p;
		}
		p++;
	}
}

const char delim_array[] = { ' ', 0x09, 0x0d, 0x0a, '(', ')', ','};

int get_linear_settings(struct tslib_linear *lin, char *p)
{
	int cal[6];
	int index;
	for (index = 0; index < 6; index++) {
		char *pend;
		p = (char *)past_delim(p, delim_array, sizeof(delim_array));
		if (!*p) {
			printf("Error, not enough numbers\n");
			return -1;
		}
		cal[index] = strtol(p, &pend, 10);
		p = pend;
	}
	for (index = 0; index < 6; index++)
		lin->res[QUAD_MAIN].a[index] = cal[index];
	lin->res[QUAD_MAIN].shift = -16;
	lin->xMax = 0;
	lin->yMax = 0;
#ifdef DEBUG
	printf("Linear calibration constants: ");
	for(index=0;index<7;index++) printf("%d ",lin->res[QUAD_MAIN].a[index]);
	printf("\n");
#endif /*DEBUG*/
	return 0;
}

int get_linearq_settings(struct tslib_linear *lin, char *p)
{
	int q;
	int r;
	struct cal_data cal[9];
	for (q = 0; q < 9; q++) {
		int index;
		for (index = 0; index < 4; index++) {
			char *pend;
			p = (char *)past_delim(p, delim_array, sizeof(delim_array));
			if (!*p) {
				if ((q == 5) && (index = 0))
					break;
				printf("Error, not enough numbers\n");
				return -1;
			}
			(&cal[q].x)[index] = strtol(p, &pend, 10);
			p = pend;
		}
	}
	lin->ncoeffs = (q == 5) ? 6 : 12;
	lin->nregions = (q == 5) ? 5 : 1;

	lin->xMax = cal[PT_MM].x * 2;
	lin->yMax = cal[PT_MM].y * 2;
	if (q == 5)
		r = perform_q_calibration(cal, lin->res);
	else
		r = perform_n_point_calibration(cal, q, lin->xMax, lin->yMax,
				lin->iMax, lin->jMax, lin->res);
#ifdef DEBUG
	printf("xMax=%d yMax=%d\n", lin->xMax, lin->yMax);
	printf("Linear calibration constants: ");
	for (q = 0; q < lin->nregions; q++) {
		int index;
		for(index = 0; index < lin->ncoeffs; index++)
			printf("%d ",lin->res[q].a[index]);
		printf("shift %d\n",lin->res[q].shift);
	}
#endif /*DEBUG*/
	return r;
}

TSAPI struct tslib_module_info *linear_quad_mod_init(struct tsdev *ts, const char *params)
{

	struct tslib_linear *lin;
	struct stat sbuf;
	int pcal_fd;
	char inputbuf[400];
	int q;
	int r;
	char *calfile=NULL;
	struct input_absinfo abs;

	lin = malloc(sizeof(struct tslib_linear));
	if (lin == NULL)
		return NULL;

	lin->module.ops = &linear_ops;
	lin->iMax = 2048;
	lin->jMax = 2048;

// Use default values that leave ts numbers unchanged after transform
	for (q = 0; q < 5; q++) {
		lin->res[q].a[0] = 1;
		lin->res[q].a[1] = 0;
		lin->res[q].a[2] = 0;
		lin->res[q].a[3] = 0;
		lin->res[q].a[4] = 1;
		lin->res[q].a[5] = 0;
		lin->res[q].shift = 0;
	}
	lin->xMax = lin->yMax = 0;


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
	calfile = getenv("TSLIB_CALIBFILE");
	if (!calfile)
		calfile = TS_POINTERCAL;
	if (stat(calfile, &sbuf)==0) {
		char *p = inputbuf;
		int size;
		pcal_fd = open(calfile,O_RDONLY);
		size = read(pcal_fd,p,sizeof(inputbuf) - 1);
		if ((unsigned)size >= sizeof(inputbuf)) {
			printf("read returned %d\n", size);
			goto err1;
		}
		p[size] = 0;

		if (p[0] != '(') {
			/* ts_calibrate was used instead of ts_calibrate_quadrant */
			printf("Warning: use ts_calibrate_quadrant to get benefit of this module\n");
			r = get_linear_settings(lin, p);
		} else {
			r = get_linearq_settings(lin, p);
		}
		if (r < 0) {
			printf("calibration error %d\n", r);
			goto err1;
		}
		close(pcal_fd);
	}
	return &lin->module;
err1:
	close(pcal_fd);
	free(lin);
	return NULL;
}

#ifndef TSLIB_STATIC_LINEAR_QUAD_MODULE
	TSLIB_MODULE_INIT(linear_quad_mod_init);
#endif
