#include <tslib.h>

struct cal_data {
	int	x;	/* framebuffer position */
	int	y;
	int	i;	/* touchscreen reading for point */
	int	j;
};

struct cal_result {
	int a[6];
	int shift;
};

#define PT_LEFT_TOP     0
#define PT_RIGHT_TOP    1
#define PT_RIGHT_BOTTOM 2
#define PT_LEFT_BOTTOM  3
#define PT_CENTER       4

#define QUAD_MAIN   0
#define QUAD_TOP    1
#define QUAD_LEFT   2
#define QUAD_BOTTOM 3
#define QUAD_RIGHT  4

TSAPI extern int perform_q_calibration(struct cal_data *cal,  struct cal_result *res);
