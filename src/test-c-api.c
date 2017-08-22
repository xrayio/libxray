/*
 * xray-c.c
 *
 *  Created on: 28 Aug 2017
 *      Author: gregory
 */

#include "xray.h"

#include <stdio.h>
#include <unistd.h>

struct ctest {
	int a;
};

struct ctest test_inst[] = {1,2,3,4,5};

int64_t add_one(void *row) {
	struct ctest *r = (struct ctest *)row;
	return r->a + 1;
}

int main() {
	xray_init("04BE197BCBC14D8E");
	void *type = xray_create_type("ctest", sizeof(struct ctest));
	xray_add_slot(type, struct ctest, a, int, 0);
	xray_add_vslot(type, "b", add_one);
	xray_register("ctest", &test_inst, "/a/c", sizeof(test_inst)/sizeof(test_inst[0]), NULL);
	xray_register("ctest", &test_inst, "/b", sizeof(test_inst)/sizeof(test_inst[0]), NULL);



	while(1)
		sleep(1);
}


