#include <tslib.h>

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

#define PT_LT		0
#define PT_RT		1
#define PT_RB		2
#define PT_LB		3
#define PT_MM		4
#define PT_MT		5
#define PT_MB		6
#define PT_LM		7
#define PT_RM		8

#define QUAD_MAIN   0
#define QUAD_TOP    1
#define QUAD_LEFT   2
#define QUAD_BOTTOM 3
#define QUAD_RIGHT  4

TSAPI extern int perform_q_calibration(struct cal_data *cal, struct cal_result *res);
TSAPI extern int perform_n_point_calibration(struct cal_data *cal,
		int num_points, u32 xmax, u32 ymax, u32 imax, u32 jmax,
		struct cal_result *res);
