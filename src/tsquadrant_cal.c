#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include "tsquadrant_cal.h"
#define DEBUG
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

TSAPI extern int perform_n_point_calibration(struct cal_data *cal,
		int num_points, u32 xmax, u32 ymax, u32 imax, u32 jmax,
		struct cal_result *res)
{
	s32 a[2][6];
	utype r[2][6];
	utype s[6 * 6];
	stype d[6 * 6];
	unsigned row;
	unsigned col;
	stype det;
	int p;
	unsigned mask = 0x3f;

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

	if (num_points < 6)
		mask &= 0x7;

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

#define DEBUG

#if 1
typedef double sxx_t;
typedef double uxx_t;
#define UXDECL(name, n) uxx_t name
#define SXDECL(name, n) sxx_t name
#define xx_neg(s) s = -s;
#define xx_reciprocal(s, b, tmp) s = 1/b;
#define xx_uset(s, n) s = n;
#define xx_cmp(s, n) ((s == n) ? 0 : (s < n) ? -1 : 1)
#define xx_uadd2(s, b) s += b
#define xx_usub2(s, b) s -= b
#define xx_umul_sum(s, a, b) s += a * b
#define xx_sumul_sum(s, a, b) s += a * b
#define xx_sumul(s, a, b) s = a * b
#define xx_umul(s, a, b) s = a * b
#define xx_usub3(s, a, b) s = a - b
#define xx_get_val(s, pshift, tmp)	_xx_get_val(s, pshift)
int _xx_get_val(sxx_t s, int *pshift)
{
	int sign = 1;
	int shift = 0;
	if (s < 0) {
		sign = -1;
		s = -s;
	}
	while (s >= (unsigned)0x80000000) {
		s = s / 2;
		shift++;
	}
	while (s < 0x40000000) {
		s = s * 2;
		shift--;
	}
	*pshift = shift;
	if (sign < 0)
		s = -s;
	return (int)s;
}

#else
#if 1
typedef unsigned sxx_t;
typedef unsigned uxx_t;
typedef unsigned long long udxx_t;
#else
/* For debugging code */
typedef unsigned char sxx_t;
typedef unsigned char uxx_t;
typedef unsigned short udxx_t;
#endif

#define XX_GROUP (sizeof(uxx_t) * 8)
#define XUNIT (32 / XX_GROUP)
#define UXDECL(name, n) uxx_t name[n*XUNIT]
#define SXDECL(name, n) sxx_t name[n*XUNIT]

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define xx_neg(s)		_xx_usub3(s, ARRAY_SIZE(s), 0, 0, b, ARRAY_SIZE(b))
#define xx_reciprocal(s, b, rem) _xx_reciprocal(s, ARRAY_SIZE(s), b, ARRAY_SIZE(b), rem, ARRAY_SIZE(rem))
#define xx_get_val(s, pshift, shr)	_xx_get_val(s, ARRAY_SIZE(s), pshift, shr)
#define xx_uset(s, n)		_xx_uset(s, ARRAY_SIZE(s), n)
#define xx_cmp(s, n)		_xx_cmp(s, ARRAY_SIZE(s), n)
#define xx_uadd2(s, b)		_xx_uadd2(s, ARRAY_SIZE(s), b, ARRAY_SIZE(b))
#define xx_usub2(s, b)		_xx_usub2(s, ARRAY_SIZE(s), b, ARRAY_SIZE(b))
#define xx_umul_sum(s, a, b)	_xx_umul_sum(s, ARRAY_SIZE(s), a, ARRAY_SIZE(a), b, ARRAY_SIZE(b))
#define xx_sumul_sum(s, a, b)	_xx_sumul_sum(s, ARRAY_SIZE(s), a, ARRAY_SIZE(a), b, ARRAY_SIZE(b))
#define xx_umul(s, a, b)	xx_uset(s, 0); xx_umul_sum(s, a, b)
#define xx_sumul(s, a, b)	xx_uset(s, 0); xx_sumul_sum(s, a, b)
#define xx_usub3(s, a, b)	_xx_usub3(s, ARRAY_SIZE(s), a, ARRAY_SIZE(a), b, ARRAY_SIZE(b))

void _xx_uset(uxx_t *s, unsigned sn, unsigned val)
{
	unsigned i;
	for (i = 0; i < sn; i++) {
		*s++ = (uxx_t)val;
		val >>= XX_GROUP;
	}
}

int _xx_cmp(sxx_t *s, unsigned sn, unsigned val)
{
	int i = sn - 1;
	{
		int a0 = s[i];
		if (a0 >> (XX_GROUP - 1))
			return -1;
	}
	for (; i >= 0; i--) {
		uxx_t a0 = s[i];
		uxx_t b0 = (uxx_t)(val >> (XX_GROUP * i));
		if (a0 < b0)
			return -1;
		if (a0 > b0)
			return 1;
	}
	return 0;
}

int _xx_ucmp(uxx_t *s, unsigned sn, uxx_t *b, unsigned bn)
{
	int i;
	while (sn > bn) {
		if (s[sn - 1])
			break;
		sn--;
	}
	while (bn > sn) {
		if (b[bn - 1])
			break;
		bn--;
	}
	if (sn != bn) {
		if (sn > bn)
			return 1;
		return -1;
	}
	for (i = sn - 1; i >= 0; i--) {
		if (s[i] > b[i])
			return 1;
		if (s[i] < b[i])
			return -1;
	}
	return 0;
}

int _xx_uadd2(uxx_t *s, unsigned sn, uxx_t *b, unsigned bn)
{
	unsigned i;
	unsigned min = (sn < bn) ? sn : bn;
	uxx_t carry = 0;

	for (i = 0; i < min; i++) {
		uxx_t s0;
		uxx_t a0 = *s;
		uxx_t b0 = *b++;
		s0 = a0 + carry;
		carry = (s0 < a0) ? 1 : 0;
		a0 = s0;
		s0 += b0;
		if (s0 < a0)
			carry++;
		*s++ = s0;
	}
	if (carry) {
		for (; i < sn; i++) {
			(*s)++;
			if (*s++)
				break;
		}
	}
	return carry;
}

int _xx_usub2(uxx_t *s, unsigned sn, uxx_t *b, unsigned bn)
{
	unsigned i;
	unsigned min = (sn < bn) ? sn : bn;
	uxx_t borrow = 0;

	for (i = 0; i < min; i++) {
		uxx_t s0;
		uxx_t a0 = *s;
		uxx_t b0 = *b++;
		s0 = a0 - borrow;
		borrow = (s0 > a0) ? 1 : 0;
		a0 = s0;
		s0 -= b0;
		if (s0 > a0)
			borrow++;
		*s++ = s0;
	}
	if (borrow) {
		for (; i < sn; i++) {
			if (*s) {
				(*s)--;
				break;
			}
			(*s)--;
			s++;
		}
	}
	return 0;
}

int _xx_usub3(sxx_t *s, unsigned sn, uxx_t *a, unsigned an, uxx_t *b, unsigned bn)
{
	unsigned i;
	uxx_t borrow = 0;
	for (i = 0; i < sn; i++) {
		uxx_t s0;
		uxx_t a0 = 0;
		uxx_t b0 = 0;
		if (i < an)
			a0 = *a++;
		if (i < bn)
			b0 = *b++;
		s0 = a0 - borrow;
		borrow = (s0 > a0) ? 1 : 0;
		a0 = s0;
		s0 -= b0;
		if (s0 > a0)
			borrow++;
		*s++ = s0;
	}
	return 0;
}

int _xx_reciprocal(uxx_t *s, unsigned sn, uxx_t *b, unsigned bn, uxx_t *rem, unsigned rem_n)
{
	unsigned i;
	unsigned rn;
	uxx_t t = 0;
	_xx_uset(s, sn, 0);
	_xx_uset(rem, rem_n, 0);
	while (bn) {
		t = b[bn - 1];
		if (t)
			break;
		bn--;
	}
	rn = bn;
	if (t >> (XX_GROUP - 1))
		rn++;

	if (rem_n < rn)
		return -1;	/* rem array is too small */
	rem[0] = 1;
	for (i = 0; i < sn * XX_GROUP; i++) {
		_xx_uadd2(rem, rn, rem, rn);
		_xx_uadd2(s, sn, s, sn);
		if (_xx_ucmp(rem, rn, b, bn) >= 0) {
			_xx_usub2(rem, rn, b, bn);
			s[0] |= 1;
		}
	}
	return 0;
}

int _xx_get_val(sxx_t *s, unsigned sn, int *pshift, int shr)
{
	int shift = 0;
	unsigned i;
	int val;
	unsigned sign_mask = 1 << (XX_GROUP - 1);
	sxx_t a =  (s[sn - 1] & sign_mask) ? -1 : 0;
	while (sn) {
		if (s[sn - 1] != a)
			break;
		if (sn >= 2)
			if ((s[sn - 2] ^ a) & sign_mask)
				break;
		sn--;
	}

	if (!sn) {
		*pshift = -31 - shr;
		return (a << 31);
	}
	for (;;) {
		unsigned s0 = s[sn - 1];
		if ((s0 ^ (s0 << 1)) >> (XX_GROUP - 1))
			break;
		_xx_uadd2(s, sn, s, sn);
		shift--;
	}
	sn--;
	val = s[sn];
	for (i = 1; i < XUNIT; i++) {
		unsigned s0 = 0;
		if (sn) {
			sn--;
			s0 = s[sn];
		} else {
			shift -= XX_GROUP;
		}
		val <<= XX_GROUP;
		val |= s0;
	}
	shift += (sn * XX_GROUP);
	*pshift = shift - shr;
	return val;
}

int _xx_umul_sum(uxx_t *s, unsigned sn, uxx_t *a, unsigned an, uxx_t *b, unsigned bn)
{
	unsigned i, j;
	int overflow = 0;
	uxx_t carry = 0;
#if 0 //def DEBUG
	printf("%s: sn=%d an=%d bn=%d\n", __func__, sn, an, bn);
	printf("s=");
	for (i = sn; i > 0; i--) {
		printf("%x ", s[i - 1]);
	}
	printf("\na=");
	for (i = an; i > 0; i--) {
		printf("%x ", a[i - 1]);
	}
	printf("\nb=");
	for (i = bn; i > 0; i--) {
		printf("%x ", b[i - 1]);
	}
	printf("\n");
#endif
	for (i = 0; i < an; i++) {
		uxx_t _a = a[i];
		if (!_a)
			continue;
		for (j = 0; j < bn; j++) {
			udxx_t v;
			uxx_t d, t, high;
			uxx_t _b = b[j];
			if (!_b) {
				if (carry) {
					s[i + j + 1]++;
					if (s[i + j + 1])
						carry = 0;
				}
				continue;
			}
			v = ((udxx_t)_a) * _b;
			if ((i + j) >= sn) {
				overflow = 1;
				break;
			}
			d = s[i + j];
			t = d + (uxx_t)v;
			s[i + j] = t;
			high = (uxx_t)(v >> XX_GROUP);
			if (t < d)
				carry++; /* detect overflow */
			if (!(carry | high))
				continue;
			if ((i + j + 1) >= sn) {
				overflow = 1;
				break;
			}
			d = s[i + j + 1];
			t = d + carry;
			carry = (t < d) ? 1 : 0; /* detect overflow */
			d = t;
			t += high;
			s[i + j + 1] = t;
			if (t < d)
				carry++;
		}
		if (!carry)
			continue;
		for (j = i + bn + 1; j < sn; j++) {
			s[j]++;
			if (s[j]) {
				carry = 0;
				break;
			}
		}
		if (carry) {
			overflow = 1;
			carry = 0;
		}
	}
#if 0 //def DEBUG
	printf("    result %s s=", overflow ? "overflow" : "");
	for (i = sn; i > 0; i--) {
		printf("%x ", s[i - 1]);
	}
	printf("\n");
#endif
	return overflow;
}

int _xx_sumul_sum(sxx_t *s, unsigned sn, sxx_t *a, unsigned an, uxx_t *b, unsigned bn)
{
#if 0 //def DEBUG
	printf("%s\n", __func__);
#endif
	_xx_umul_sum(s, sn, (uxx_t *)a, an, b, bn);
	if (sn > an) {
		if (a[an - 1] >> (XX_GROUP - 1)) {
			_xx_usub2(&s[an], sn - an, b, bn);
#if 0 //def DEBUG
			{
				unsigned i;
				printf("***sresult  s=");
				for (i = sn; i > 0; i--) {
					printf("%x ", s[i - 1]);
				}
				printf("\n");
			}
#endif
		}
	}
	return 0;
}
#endif

/*
x = a1 i  +  a2 j + a3
y = b1 i  +  b2 j + b3

where x,y are screen coordinates and i,j are measured touch screen readings
| x |  =  | a1  a2  a3 | | i |
| y |     | b1  b2  b3 | | j |
                         | 1 |

| x1  x2  x3 |  =  | a1  a2  a3 | | i1 i2 i3 |
| y1  y2  y3 |     | b1  b2  b3 | | j1 j2 j3 |
                                  | 1   1  1 |
where xn,yn are screen coordinates of where touched
and   in,jn are measured readings when touched

so

| x1  x2  x3 |  | i1  i2  i3 | -1  =  | a1  a2  a3 |
| y1  y2  y3 |  | j1  j2  j3 |        | b1  b2  b3 |
                | 1   1    1 |

any three non-linear points will determine a1,a2,a3, b1.b2,b3


For multiple points, _ means summation
| _x _xi _xj| = | a1 a2 a3 | | _i   _ii  _ij  |
| _y _yi _yj|   | b1 b2 b3 | | _j   _ij  _jj  |
                             |  n   _i   _j   |


| _x _xi _xj|  | _i   _ii   _ij  | -1 = | a1 a2 a3 |
| _y _yi _yj|  | _j   _ij   _jj  |      | b1 b2 b3 |
               |  n   _i    _j   |

in general when matrix
A = | a11 a12 a13 |
    | a21 a22 a23 |
    | a31 a32 a33 |
then
A ^ -1 = 1/|A|  | |a22 a23| |a13 a12| |a12 a13| |
		| |a32 a33| |a33 a32| |a22 a23| |
		|				|
		| |a23 a21| |a11 a13| |a13 a11| |
		| |a33 a31| |a31 a33| |a23 a21| |
		|				|
		| |a21 a22| |a12 a11| |a11 a12| |
		| |a31 a32| |a32 a31| |a21 a22| |
or for us

A ^ -1 = 1/|A|  | |_ij  _jj| |_ij  _ii| |_ii _ij| |
		| |_i   _j | |_j   _i | |_ij _jj| |
		|				  |
		| |_jj  _j|  |_i _ij|   |_ij _i|  |
		| |_j    n|  | n _j |   |_jj _j|  |
		|				  |
		| |_j  _ij|   |_ii _i|  |_i _ii|  |
		| | n  _i |   |_i   n|  |_j _ij|  |

or
A ^ -1 = 1/|A|  | _ij*_j - _jj*_i,  _ij*_i - _ii*_j,  _ii*_jj - _ij*_ij	|
		|			 	   			|
		| n*_jj - _j*_j,    _i*_j - n*_ij,    _ij*_j - _i*_jj	|
		|				  			|
		| _j*_i - n*_ij,    n*_ii - _i*_i,   _i*_ij - _ii*_j	|
*/

int perform_calibration(struct cal_data *cal, int num_points, struct cal_result *res) {
	int p;
	int det_sign;
	UXDECL(n, 1);
	UXDECL(_x, 2);
	UXDECL(_y, 2);
	UXDECL(_i, 2);
	UXDECL(_j, 2);
	UXDECL(_xi, 3);
	UXDECL(_xj, 3);
	UXDECL(_yi, 3);
	UXDECL(_yj, 3);
	UXDECL(_ii, 3);
	UXDECL(_jj, 3);
	UXDECL(_ij, 3);
	UXDECL(_i_jj, 5);
	UXDECL(_i_ij, 5);
	UXDECL(_j_ij, 5);
	UXDECL(_j_ii, 5);
	UXDECL(_ii_jj, 6);
	UXDECL(_ij_ij, 6);
	SXDECL(a, 6);
	SXDECL(b, 5);
	SXDECL(c, 5);
	SXDECL(d, 6);
	SXDECL(f, 5);
	SXDECL(g, 7);
	SXDECL(det, 9);
//	UXDECL(tmp, 10);
	UXDECL(stmp, 10);
	UXDECL(idet, 10);
	UXDECL(stmp2, 20);

	UXDECL(n_ii, 4);
	UXDECL(n_ij, 4);
	UXDECL(n_jj, 4);
	UXDECL(_i_i, 4);
	UXDECL(_i_j, 4);
	UXDECL(_j_j, 4);
	int shift[6];
	int val[6];
	int max_shift;

	xx_uset(n, num_points);
	xx_uset(_x, 0);
	xx_uset(_y, 0);
	xx_uset(_i, 0);
	xx_uset(_j, 0);

	xx_uset(_xi, 0);
	xx_uset(_xj, 0);
	xx_uset(_yi, 0);
	xx_uset(_yj, 0);
	xx_uset(_ii, 0);
	xx_uset(_jj, 0);
	xx_uset(_ij, 0);

// Get sums for matrix
	for (p = 0; p < num_points; p++) {
		struct cal_data *d = &cal[p];
		UXDECL(x, 1);
		UXDECL(y, 1);
		UXDECL(i, 1);
		UXDECL(j, 1);
		xx_uset(x, d->x);
		xx_uset(y, d->y);
		xx_uset(i, d->i);
		xx_uset(j, d->j);
		xx_uadd2(_x, x);
		xx_uadd2(_y, y);
		xx_uadd2(_i, i);
		xx_uadd2(_j, j);
		xx_umul_sum(_xi, x, i);
		xx_umul_sum(_xj, x, j);
		xx_umul_sum(_yi, y, i);
		xx_umul_sum(_yj, y, j);
		xx_umul_sum(_ii, i, i);
		xx_umul_sum(_jj, j, j);
		xx_umul_sum(_ij, i, j);
	}
/*
 * find determinant of
 * | _i   _ii  _ij  |
 * | _j   _ij  _jj  |
 * |  n   _i   _j   |
 */
	xx_umul(_i_jj, _i, _jj);
	xx_umul(_i_ij, _i, _ij);
	xx_umul(_j_ij, _j, _ij);
	xx_umul(_j_ii, _j, _ii);
	xx_umul(_ii_jj, _ii, _jj);
	xx_umul(_ij_ij, _ij, _ij);
	xx_usub3(a, _j_ij, _i_jj);
	xx_usub3(d, _i_ij, _j_ii);
	xx_usub3(g, _ii_jj, _ij_ij);

	xx_sumul(det, a, _i);
	xx_sumul_sum(det, d, _j);
	xx_sumul_sum(det, g, n);

	det_sign = xx_cmp(det, 0);
	if (!det_sign) {
		printf("ts_calibrate: determinant is zero\n");
		return -1;
	}
	if (det_sign < 0)
		xx_neg(det);
	xx_reciprocal(idet, det, tmp);

/*
 * Get elements of inverse matrix
 *	A ^ -1 = 1/|A|  | a(h) = _j*_ij - _i*_jj,  d(i) = _i*_ij - _j*_ii,  g = _ii*_jj - _ij*_ij	|
 *			| b = n*_jj - _j*_j,       e(c) = _i*_j - n*_ij,    h(a) =  _j*_ij - _i*_jj	|
 *			| c(e) = _i*_j - n*_ij,    f = n*_ii - _i*_i,       i(d) =  _i*_ij - _j*_ii	|
 */
	xx_umul(n_ii, n, _ii);
	xx_umul(n_ij, n, _ij);
	xx_umul(n_jj, n, _jj);
	xx_umul(_i_i, _i, _i);
	xx_umul(_i_j, _i, _j);
	xx_umul(_j_j, _j, _j);

	xx_usub3(b, n_jj, _j_j);
	xx_usub3(c, _i_j, n_ij);
	xx_usub3(f, n_ii, _i_i);

// Now multiply out to get the calibration for framebuffer x coord
	xx_sumul(stmp, a, _x);
	xx_sumul_sum(stmp, b, _xi);
	xx_sumul_sum(stmp, c, _xj);	/* a*_x + b*_xi + c*_xj */
	xx_sumul(stmp2, stmp, idet);
	val[0] = xx_get_val(stmp2, &shift[0], sizeof(idet) * 8);

	xx_sumul(stmp, d, _x);
	xx_sumul_sum(stmp, c, _xi);
	xx_sumul_sum(stmp, f, _xj);	/* d*_x + c*_xi + f*_xj */
	xx_sumul(stmp2, stmp, idet);
	val[1] = xx_get_val(stmp2, &shift[1], sizeof(idet) * 8);

	xx_sumul(stmp, g, _x);
	xx_sumul_sum(stmp, a, _xi);
	xx_sumul_sum(stmp, d, _xj);	/* g*_x + a*_xi + d*_xj */
	xx_sumul(stmp2, stmp, idet);
	val[2] = xx_get_val(stmp2, &shift[2], sizeof(idet) * 8);

// Now multiply out to get the calibration for framebuffer y coord
	xx_sumul(stmp, a, _y);
	xx_sumul_sum(stmp, b, _yi);
	xx_sumul_sum(stmp, c, _yj);	/* a*_y + b*_yi + c*_yj */
	xx_sumul(stmp2, stmp, idet);
	val[3] = xx_get_val(stmp2, &shift[3], sizeof(idet) * 8);

	xx_sumul(stmp, d, _y);
	xx_sumul_sum(stmp, c, _yi);
	xx_sumul_sum(stmp, f, _yj);	/* d*_y + c*_yi + f*_yj */
	xx_sumul(stmp2, stmp, idet);
	val[4] = xx_get_val(stmp2, &shift[4], sizeof(idet) * 8);

	xx_sumul(stmp, g, _y);
	xx_sumul_sum(stmp, a, _yi);
	xx_sumul_sum(stmp, d, _yj);	/* g*_y + a*_yi + d*_yj */
	xx_sumul(stmp2, stmp, idet);
	val[5] = xx_get_val(stmp2, &shift[5], sizeof(idet) * 8);

	max_shift = -20;
	for (p = 0; p < 6; p++) {
		if (max_shift < shift[p])
			max_shift = shift[p];
	}
	for (p = 0; p < 6; p++) {
		int sh = max_shift - shift[p];
		int v = val[p] >> sh;
		if (det_sign < 0)
			v = -v;
		res->a[p] = v;
	}
#ifdef DEBUG
	{
		int shift;
		int val = xx_get_val(det, &shift, 0);
		printf("max_shift=%d det=%x shl %i det_sign=%i\n", max_shift, val, shift, det_sign);
	}
#endif
#ifdef DEBUG
	{
		int shr = -max_shift;
		int a = res->a[0];
		int b = res->a[1];
		int c = res->a[2];
		char as = ' ';
		char bs = ' ';
		char cs = ' ';
		if (shr < 0)
			shr = 0;
		if (a < 0) {
			as = '-';
			a = -a;
		}
		if (b < 0) {
			bs = '-';
			b = -b;
		}
		if (c < 0) {
			cs = '-';
			c = -c;
		}
		printf("%c%x(%c%i.%03i) %c%x(%c%i.%03i) %c%x(%c%i.%03i)\n",
			as, a, as, a >> shr, ((a - ((a >> shr) << shr)) * 1000) >> shr,
			bs, b, bs, b >> shr, ((b - ((b >> shr) << shr)) * 1000) >> shr,
			cs, c, cs, c >> shr, ((c - ((c >> shr) << shr)) * 1000) >> shr);
		a = res->a[3];
		b = res->a[4];
		c = res->a[5];
		as = ' ';
		bs = ' ';
		cs = ' ';
		if (a < 0) {
			as = '-';
			a = -a;
		}
		if (b < 0) {
			bs = '-';
			b = -b;
		}
		if (c < 0) {
			cs = '-';
			c = -c;
		}
		printf("%c%x(%c%i.%03i) %c%x(%c%i.%03i) %c%x(%c%i.%03i)\n",
			as, a, as, a >> shr, ((a - ((a >> shr) << shr)) * 1000) >> shr,
			bs, b, bs, b >> shr, ((b - ((b >> shr) << shr)) * 1000) >> shr,
			cs, c, cs, c >> shr, ((c - ((c >> shr) << shr)) * 1000) >> shr);
	}
#endif
	res->shift = max_shift;
	return 0;
}

int perform_q_calibration(struct cal_data *cal,  struct cal_result *res)
{
	int r;
	struct cal_data c3[3];
	r = perform_calibration(cal, 5, &res[QUAD_MAIN]);
	c3[0] = cal[PT_MM];
	c3[2] = cal[PT_RT];
	c3[1] = cal[PT_LT];
	if (r >= 0)
		r = perform_calibration(c3, 3, &res[QUAD_TOP]);
	c3[2] = cal[PT_LB];
	if (r >= 0)
		r = perform_calibration(c3, 3, &res[QUAD_LEFT]);
	c3[1] = cal[PT_RB];
	if (r >= 0)
		r = perform_calibration(c3, 3, &res[QUAD_BOTTOM]);
	c3[2] = cal[PT_RT];
	if (r >= 0)
		r = perform_calibration(c3, 3, &res[QUAD_RIGHT]);
	return r;
}

