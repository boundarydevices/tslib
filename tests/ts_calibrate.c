/*
 *  tslib/tests/ts_calibrate.c
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This file is placed under the GPL.  Please see the file
 * COPYING for more details.
 *
 * SPDX-License-Identifier: GPL-2.0+
 *
 *
 * Graphical touchscreen calibration tool. This writes the configuration
 * file used by tslib's "linear" filter plugin module to transform the
 * touch samples according to the calibration.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/fb.h>
#include <linux/input.h>
#include "tslib-private.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>

#include "tslib.h"

#include "fbutils.h"
#include "testutils.h"
#include "ts_calibrate.h"

#define u64 unsigned long long
#define s32 int
#define u32 unsigned int
#define s64 long long

struct cal_data {
	u32	x;	/* framebuffer position */
	u32	y;
	u32	i;	/* touchscreen reading for point */
	u32	j;
};

struct cal_result {
	int shift;
	s32 a[12];
};

static int palette[] = {
	0x000000, 0xffe080, 0xffffff, 0xe0c0a0
};
#define NR_COLORS (sizeof(palette) / sizeof(palette[0]))

static void sig(int sig)
{
	close_framebuffer();
	fflush(stderr);
	printf("signal %d caught\n", sig);
	fflush(stdout);
	exit(1);
}

static void get_sample(struct tsdev *ts, struct cal_data *cal,
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
			put_cross(last_x >> 16, last_y >> 16, 2 | XORMODE);
			usleep(1000);
			put_cross(last_x >> 16, last_y >> 16, 2 | XORMODE);
			last_x += dx;
			last_y += dy;
		}
	}

	put_cross(x, y, 2 | XORMODE);
	getxy(ts, (int *)&cal->i, (int *)&cal->j);
	put_cross(x, y, 2 | XORMODE);

	last_x = cal->x = x;
	last_y = cal->y = y;

	printf("%s: (%4d, %4d)->(%4d, %4d)\n", name, x, y, cal->i, cal->j);
}

static void clearbuf(struct tsdev *ts)
{
	int fd = ts_fd(ts);
	fd_set fdset;
	struct timeval tv;
	int nfds;
	struct ts_sample sample;

	while (1) {
		FD_ZERO(&fdset);
		FD_SET(fd, &fdset);

		tv.tv_sec = 0;
		tv.tv_usec = 0;

		nfds = select(fd + 1, &fdset, NULL, NULL, &tv);
		if (nfds == 0)
			break;

		if (ts_read_raw(ts, &sample, 1) < 0) {
			perror("ts_read_raw");
			exit(1);
		}
	}
}

/*
let i,j be numbers between 0 and <1 (touchscreen reading/max res)
let x,y be numbers between 0 and <1 (screen pos/max screen dimension)

x = a1 + a2*i + a3*j + a4*i*j + a5*i*i + a6*j*j

|x| = |a1 a2 a3 a4 a5 a6| | 1   |
|y|   |b1 b2 b3 b4 b5 b6| | i   |
			  | j   |
			  | i*j |
			  | i*i |
			  | j*j |


| x1  x2  x3 x4 x5 x6|  =  | a1  a2  a3  a4 a5 a6| | 1      1       1      1      1      1    |
| y1  y2  y3 y4 y5 y6|     | b1  b2  b3  b4 b5 b6| | i1     i2      i3     i4     i5     i6   |
						   | j1     j2      j3     j4     j5     j6   |
						   | i1*j1  i2*j2   i3*j3  i4*j4  i5*j5  i6*j6|
						   | i1*i1  i2*i2   i3*i3  i4*i4  i5*i5  i6*i6|
						   | j1*j1  j2*j2   j3*j3  j4*j4  j5*j5  j6*j6|

For multiple points, _ means summation
| _x _xi _xj  _xij _xi2 _xj2| = | a1 a2 a3 a4 a5 a6|	| n    _i      _j     _ij    _i2    _j2  |
| _y _yi _yj  _yij _yi2 _yj2|   | b1 b2 b3 b4 b5 b6|	|_i    _i2     _ij    _i2j   _i3    _ij2 |
							|_j    _ij     _j2    _ij2   _i2j   _j3  |
							|_ij   _i2j    _ij2   _i2j2  _i3j   _ij3 |
							|_i2   _i3     _i2j   _i3j   _i4    _i2j2|
							|_j2   _ij2    _j3    _ij3   _i2j2  _j4  |


Numbers by i or j mean ** (squared or cubed or 4th power)

| _x _xi _xj  _xij _xi2 _xj2|	| n    _i     _j    _ij    _i2    _j2  | -1 = | a1 a2 a3 a4 a5 a6 |
| _y _yi _yj  _yij _yi2 _yj2|	|_i    _i2    _ij   _i2j   _i3    _ij2 |      | b1 b2 b3 b4 b5 b6 |
				|_j    _ij    _j2   _ij2   _i2j   _j3  |
				|_ij   _i2j   _ij2  _i2j2  _i3j   _ij3 |
				|_i2   _i3    _i2j  _i3j   _i4    _i2j2|
				|_j2   _ij2   _j3   _ij3   _i2j2  _j4  |
(A)-1 = 1/det(A) * adj(A)
adj(A)ij = (-1)**(i+j) det(minor_ji(A))
 */
#define PT_LT		0
#define PT_RT		1
#define PT_RB		2
#define PT_LB		3
#define PT_MM		4
#define PT_MT		5
#define PT_MB		6
#define PT_LM		7
#define PT_RM		8
#define utype long double
#define stype long double
#define DFORMAT "%20.10Lf"
#define FIXED_FORMAT "%20.10Lf"

/* Symmetrical matrix, only half filled in */
static utype get_element(utype *s, unsigned row, unsigned col)
{
	if (row <= col)
		return s[row * 6 + col];
	return s[col * 6 + row];
}

/* Symmetrical matrix, only half filled in */
static stype get_adj_element(stype *d, unsigned row, unsigned col)
{
	if (row <= col)
		return d[row * 6 + col];
	return d[col * 6 + row];
}

static stype mull(stype m, utype e)
{
	return m * e;
}

static stype determinate(utype *s, unsigned row_mask, unsigned col_mask)
{
	unsigned col = ffs(col_mask) - 1;
	unsigned row = ffs(row_mask) - 1;
	stype det = 0;
	int neg = 0;
	unsigned r_mask = row_mask & ~(1 << row);

	if (!r_mask)
		return get_element(s, row, col);

	while (col < 6) {
		if (col_mask & (1 << col)) {
			unsigned c_mask = col_mask & ~(1 << col);
			stype m = determinate(s, r_mask, c_mask);
			utype e = get_element(s, row, col);

#ifdef DEBUG
//			printf("%s: m = " DFORMAT " e= " DFORMAT "\n", __func__, m, e);
#endif
			m = mull(m, e);
			if (neg)
				m = -m;
			det += m;
			neg ^= 1;
		}
		col++;
	}
#ifdef DEBUG
//	printf("%s: det = " DFORMAT "\n", __func__, det);
#endif
	return det;
}

utype cval(u32 n) {
	return (utype)n;
}

utype mval(utype a, utype b) {
	return a * b;
}

TSAPI int perform_n_point_calibration(struct cal_data *cal,
		int num_points, u32 xmax, u32 ymax, u32 imax, u32 jmax,
		struct cal_result *res, int n_coefs)
{
	s32 a[2][6];
	utype r[2][6];
	utype s[6 * 6];
	stype d[6 * 6];
	unsigned row;
	unsigned col;
	stype det;
	int p;
	unsigned mask = (1 << n_coefs) - 1;

	printf("xmax=%d, ymax=%d imax=%d, jmax=%d\n", xmax, ymax, imax, jmax);

	memset(a, 0, sizeof(a));
	memset(r, 0, sizeof(r));
	memset(s, 0, sizeof(s));

	s[0 * 6 + 0] = cval(num_points);

	for (p = 0; p < num_points; p++) {
		struct cal_data *d = &cal[p];
		utype x = cval(d->x) / xmax;
		utype y = cval(d->y) / ymax;
		utype i = cval(d->i) / imax;
		utype j = cval(d->j) / jmax;
		utype ij = mval(i, j);
		utype i2 = mval(i, i);
		utype j2 = mval(j, j);

		r[0][0] += x;
		r[0][1] += mval(x, i);
		r[0][2] += mval(x, j);
		r[0][3] += mval(x, ij);
		r[0][4] += mval(x, i2);
		r[0][5] += mval(x, j2);

		r[1][0] += y;
		r[1][1] += mval(y, i);
		r[1][2] += mval(y, j);
		r[1][3] += mval(y, ij);
		r[1][4] += mval(y, i2);
		r[1][5] += mval(y, j2);
/*		   0     1     2     3      4      5
		0| n    _i     _j    _ij    _i2    _j2  |
		1|_i    _i2    _ij   _i2j   _i3    _ij2 |
		2|_j    _ij    _j2   _ij2   _i2j   _j3  |
		3|_ij   _i2j   _ij2  _i2j2  _i3j   _ij3 |
		4|_i2   _i3    _i2j  _i3j   _i4    _i2j2|
		5|_j2   _ij2   _j3   _ij3   _i2j2  _j4  |
*/

		s[0 * 6 + 1] += i;
		s[1 * 6 + 1] += i2;

		s[0 * 6 + 2] += j;
		s[1 * 6 + 2] += ij;
		s[2 * 6 + 2] += j2;

		s[1 * 6 + 3] += mval(i, ij);	/* i2j */
		s[2 * 6 + 3] += mval(j, ij);	/* ij2 */
		s[3 * 6 + 3] += mval(ij, ij);	/* i2j2 */

		s[1 * 6 + 4] += mval(i, i2);	/* i3 */
		s[3 * 6 + 4] += mval(ij, i2);	/* i3j */
		s[4 * 6 + 4] += mval(i2, i2);	/* i4 */

		s[2 * 6 + 5] += mval(j, j2);	/* j3 */
		s[3 * 6 + 5] += mval(ij, j2);	/* ij3 */
		s[5 * 6 + 5] += mval(j2, j2);	/* j4 */
	}
	s[0 * 6 + 3] = s[1 * 6 + 2];	/* ij */

	s[0 * 6 + 4] = s[1 * 6 + 1];	/* i2 */
	s[2 * 6 + 4] = s[1 * 6 + 3];	/* i2j */

	s[0 * 6 + 5] = s[2 * 6 + 2];	/* j2 */
	s[1 * 6 + 5] = s[2 * 6 + 3];	/* ij2 */
	s[4 * 6 + 5] = s[3 * 6 + 3];	/* i2j2 */

	det = determinate(s, mask, mask);
	if ((det > -.000000001) && (det < .000000001)) {
		printf("ts_calibrate: determinant is " DFORMAT "\n", det);
		return -1;
	}
#ifdef DEBUG
	printf("input: det = " DFORMAT "\n", det);

	for (row = 0; row < 6; row++) {
		if (mask & (1 << row)) {
			for (col = 0; col <= row; col++) {
				if (mask & (1 << col)) {
					printf(FIXED_FORMAT " ", get_element(s, row, col));
				}
			}
			printf("\n");
		}
	}
#endif

	/* calculate inverse of matrix s */
	for (row = 0; row < 6; row++) {
		if (mask & (1 << row)) {
			int neg = 0;
			for (col = row; col < 6; col++) {
				if (mask & (1 << col)) {
					stype det1 = determinate(s, mask ^ (1 << row), mask ^(1 << col));

					if (neg)
						det1 = -det1;
					d[row * 6 + col] = det1;
					neg ^= 1;
				}
			}
		}
	}

#ifdef DEBUG
	printf("Adj\n");
	for (row = 0; row < 6; row++) {
		if (mask & (1 << row)) {
			for (col = 0; col <= row; col++) {
				if (mask & (1 << col)) {
					printf(FIXED_FORMAT " ", get_adj_element(d, row, col));
				}
			}
			printf("\n");
		}
	}
#endif

	for (row = 0; row < 2; row ++) {
		for (col = 0; col < 6; col++) {
			if (mask & (1 << col)) {
				stype sum = 0;
				int w;
				for (w = 0; w < 6; w++) {
					if (mask & (1 << w))
						sum += mull(get_adj_element(d, w, col), r[row][w]);
				}
				sum /= det;
				a[row][col] = (int)(sum * 65536);
#ifdef DEBUG
				printf("a=%d (" DFORMAT ")\n", a[row][col], sum);
#endif
			}
		}
	}
	res->a[0] = a[0][0];
	res->a[1] = a[0][1];
	res->a[2] = a[0][2];
	res->a[3] = a[0][3];
	res->a[4] = a[0][4];
	res->a[5] = a[0][5];
	res->a[6] = a[1][0];
	res->a[7] = a[1][1];
	res->a[8] = a[1][2];
	res->a[9] = a[1][3];
	res->a[10] = a[1][4];
	res->a[11] = a[1][5];
	res->shift = 16;
	printf("x: %d, %d i, %d j, %d ij, %d i2, %d j2\n", a[0][0], a[0][1], a[0][2], a[0][3], a[0][4], a[0][5]);
	printf("y: %d, %d i, %d j, %d ij, %d i2, %d j2\n", a[1][0], a[1][1], a[1][2], a[1][3], a[1][4], a[1][5]);
	return 0;
}

int new_cal(struct tsdev *ts, struct cal_data *caln, unsigned npoints)
{
	int i;
	unsigned coeff;
	int r;
	int ret;
	unsigned nconst = 12;
	struct input_absinfo abs;
	int iMax = 2048;
	int jMax = 2048;

	struct cal_result res;
	int cal_fd;
	char cal_buffer[256];
	char device_name[260];
	char *calfile = NULL;
	unsigned n_coefs = 6;

	char *p;
	int nleft;
	int len;
	float f[6];

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

	if (n_coefs > npoints)
		n_coefs = 3;
	r = perform_n_point_calibration(caln, npoints, xres, yres, iMax, jMax, &res, n_coefs);
	if (r < 0) {
		printf("Calibration failed.\n");
		return -1;
	}

	calfile = getenv("TSLIB_CALIBFILE");
	if (!calfile)
		calfile = TS_POINTERCAL;

	printf ("Calibration constants: ");
	for (coeff = 0; coeff < nconst; coeff++)
		printf("%d%c", res.a[coeff], (coeff == nconst - 1) ? '\n' : ' ');

	cal_fd = open(calfile, O_CREAT | O_TRUNC | O_RDWR,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	len = sprintf(cal_buffer,"%d %d %d %d %d %d %d %d %d %d %d %d\n",
		              res.a[0], res.a[1], res.a[2],
		              res.a[3], res.a[4], res.a[5],
		              res.a[6], res.a[7], res.a[8],
		              res.a[9], res.a[10], res.a[11]);
	ret = write(cal_fd, cal_buffer, len);
	if (ret < len)
		printf("write returned %d, expected %d\n", ret, len);
	close(cal_fd);

	p = strncpy(cal_buffer, calfile, sizeof(cal_buffer) - 3);
	p[sizeof(cal_buffer) - 3] = 0;
	p += strlen(p);
	*p++ = '_';
	*p++ = 'e';
	*p = 0;

	cal_fd = open(cal_buffer, O_TRUNC | O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
	printf("coefficient file: %s\n", cal_buffer);
	p = cal_buffer;
	nleft = sizeof(cal_buffer);

	for (coeff = 0; coeff < nconst; coeff++) {
		len = snprintf(p, nleft, "%.5f%c",
				(float)res.a[coeff] / 65536,
				(coeff == nconst - 1) ? '\n' : ' ');
		if (nleft > len) {
			nleft -= len;
			p += len;
		}
	}
	len = sizeof(cal_buffer) - nleft;
	ret = write(cal_fd, cal_buffer, len);
	if (ret < len)
		printf("write returned %d, expected %d\n", ret, len);
	close(cal_fd);

	/*
	 * Now, create file in xinput format
	 */
	if (n_coefs > 3) {
		n_coefs = 3;
		r = perform_n_point_calibration(caln, npoints, xres, yres, iMax, jMax, &res, n_coefs);
		if (r < 0) {
			printf("Calibration failed.\n");
			return -1;
		}
	}
	p = strncpy(cal_buffer, calfile, sizeof(cal_buffer) - 3);
	p[sizeof(cal_buffer) - 3] = 0;
	p += strlen(p);
	*p++ = '_';
	*p++ = 'x';
	*p = 0;

	cal_fd = open(cal_buffer, O_TRUNC | O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
	p = cal_buffer;
	nleft = sizeof(cal_buffer);

	f[2] = (float)res.a[0] / 65536;
	f[0] = (float)res.a[1] / 65536;
	f[1] = (float)res.a[2] / 65536;
	f[5] = (float)res.a[6] / 65536;
	f[3] = (float)res.a[7] / 65536;
	f[4] = (float)res.a[8] / 65536;
	len = snprintf(p, nleft, "\"%s\" \"Coordinate Transformation Matrix\" "
			"%.5f %.5f %.5f %.5f %.5f %.5f 0 0 1\n",
			device_name, f[0], f[1], f[2], f[3], f[4], f[5]);
	if (nleft > len) {
		nleft -= len;
		p += len;
	}
	len = sizeof(cal_buffer) - nleft;
	ret = write(cal_fd, cal_buffer, len);
	if (ret < len)
		printf("write returned %d, expected %d\n", ret, len);
	close(cal_fd);

	return 0;
}

struct opts {
	int npoints;
};

static void help(void)
{
	ts_print_ascii_logo(16);
	print_version();

	printf("\n");
	printf("Usage: ts_calibrate [-r <rotate_value>] [--version]\n");
	printf("\n");
	printf("-r --rotate\n");
	printf("        <rotate_value> 0 ... no rotation; 0 degree (default)\n");
	printf("                       1 ... clockwise orientation; 90 degrees\n");
	printf("                       2 ... upside down orientation; 180 degrees\n");
	printf("                       3 ... counterclockwise orientation; 270 degrees\n");
	printf("-h --help\n");
	printf("                       print this help text\n");
	printf("-v --version\n");
	printf("                       print version information only\n");
	printf("-m --rotate_mode n	0 - normal, 1 - vflip, 2 - hflip, 3 - 180,\n");
	printf("\t\t4 - swap x/y, 5 - right 90(cw), 6 - left 90(ccw), 7 - swap x/y 180\n");
	printf("\n");
	printf("Example (Linux): ts_calibrate -r $(cat /sys/class/graphics/fbcon/rotate)\n");
	printf("\n");
}

unsigned char rotation_lookup[] = {
	0, ROTATE_90_RIGHT, ROTATE_180, ROTATE_90_LEFT
};

int main(int argc, char * const argv[])
{
	struct tsdev *ts;
	unsigned int i;
	int dx, dy;
	struct cal_data cal[9];
	struct opts opts;
	unsigned rotation;

	memset(&opts, 0, sizeof(struct opts));
	opts.npoints = 5;

	signal(SIGSEGV, sig);
	signal(SIGINT, sig);
	signal(SIGTERM, sig);

	while (1) {
		const struct option long_options[] = {
			{ "help",         no_argument,       NULL, 'h' },
			{ "rotate",       required_argument, NULL, 'r' },
			{ "version",      no_argument,       NULL, 'v' },
			{ "rotate_mode",  required_argument, NULL, 'm' },
		};

		int option_index = 0;
		int c = getopt_long(argc, argv, "hvr:m:", long_options, &option_index);

		errno = 0;
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			help();
			return 0;

		case 'v':
			print_version();
			return 0;

		case 'r':
			/* extern in fbutils.h */
			rotation = atoi(optarg);
			if (rotation > 3) {
				help();
				return 0;
			}
			rotate_mode = rotation_lookup[rotation];
			break;

		case 'm' :
			rotate_mode = atoi(optarg);
			if (rotate_mode > 7) {
				help();
				return 0;
			}
			break;
		default:
			help();
			return 0;
		}

		if (errno) {
			char str[9];

			sprintf(str, "option ?");
			str[7] = c & 0xff;
			perror(str);
		}
	}

	if (open_framebuffer()) {
		close_framebuffer();
		exit(1);
	}

	ts = ts_open_config(0, xres, yres);
	if (!ts) {
		perror("ts_open_config");
		exit(1);
	}

	for (i = 0; i < NR_COLORS; i++)
		setcolor(i, palette[i]);

	put_string_center(xres / 2, yres / 4,
			  "Touchscreen calibration utility", 1);
	put_string_center(xres / 2, yres / 4 + 20,
			  "Touch crosshair to calibrate", 2);

	printf("xres = %d, yres = %d\n", xres, yres);

	/* Clear the buffer */
	clearbuf(ts);
	dy = 50;
	dx = (dy * xres) / yres;

	if (PT_LT < opts.npoints)
		get_sample(ts, &cal[PT_LT], dx,            dy,            "left top");
	if (PT_MT < opts.npoints)
		get_sample(ts, &cal[PT_MT], xres / 2,      dy,            "mid top ");
	if (PT_RT < opts.npoints)
		get_sample(ts, &cal[PT_RT], xres - 1 - dx, dy,            "right top");

	if (PT_LM < opts.npoints)
		get_sample(ts, &cal[PT_LM], dx,            yres / 2,      "left mid");
	if (PT_MM < opts.npoints)
		get_sample(ts, &cal[PT_MM], xres / 2,      yres / 2,      "Center");
	if (PT_RM < opts.npoints)
		get_sample(ts, &cal[PT_RM], xres - 1 - dx, yres / 2,      "right mid");

	if (PT_LB < opts.npoints)
		get_sample(ts, &cal[PT_LB], dx,            yres - 1 - dy, "left bottom");
	if (PT_MB < opts.npoints)
		get_sample(ts, &cal[PT_MB], xres / 2,      yres - 1 - dy, "mid bottom");
	if (PT_RB < opts.npoints)
		get_sample(ts, &cal[PT_RB], xres - 1 - dx, yres - 1 - dy, "right bottom");

	i = new_cal(ts, cal, opts.npoints);

	fillrect(0, 0, xres - 1, yres - 1, 0);
	close_framebuffer();
	ts_close(ts);
	return i;
}
