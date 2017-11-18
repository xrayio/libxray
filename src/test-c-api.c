/*
 * xray-c.c
 *
 *  Created on: 28 Aug 2017
 *      Author: gregory
 */

#include "xray.h"

#include <stdio.h>
#include <unistd.h>

typedef struct ctest {
	int id;
	int rate;
} ctest_t;

struct ctest test_inst[] = {{0, 0},{1, 1},{2, 2},{3, 3},{4, 4}};

int64_t add_one(void *row, xray_vslot_args_t *vslot_args) {
	struct ctest *r = (struct ctest *)row;
	return r->rate + 1;
}

#include <time.h>

int main() {
	xray_init("04BE197BCBC14D8E");
	void *type = xray_create_type(ctest_t, NULL);
	xray_add_slot(type, ctest_t, id, int, XRAY_FLAG_PK);
	xray_add_slot(type, ctest_t, rate, int, XRAY_FLAG_RATE);
	xray_add_vslot(type, "b", add_one);
	xray_register(ctest_t, &test_inst, "/a/c", sizeof(test_inst)/sizeof(test_inst[0]), NULL);
	xray_register(ctest_t, &test_inst, "/rate", 1, NULL);

	while(1) {
		test_inst[0].rate += 10;
		sleep(1);
	}
}


