/*
 * xray-c.c
 *
 *  Created on: 28 Aug 2017
 *      Author: gregory
 */

#include "xray.h"

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

/* should exit the app */
static int exit_test = 0;

/* Utils */
#define ARR_DIM(arr) (sizeof(arr) / sizeof((arr)[0]))


/* structs */
typedef struct basic_types {
    int8_t int8;
    uint8_t uint8;
    int16_t int16;
    uint16_t uint16;
    int32_t int32;
    uint32_t uint32;
    int64_t int64;
    uint64_t uint64;
    char str[8];
    char *p_str;
} basic_types_t;

struct itable_example {
	int a;
	int b;
	int c;
};

typedef struct ctest {
	int id;
	int rate;
} ctest_t;

typedef struct is_up {
    const char *is_up;
} is_up_t;

struct ctest test_inst[] = {{0, 0},{1, 1},{2, 2},{3, 3},{4, 4}};

int64_t add_one(void *row, xray_vslot_args_t *vslot_args) {
	struct ctest *r = (struct ctest *)row;
	return r->rate + 1;
}

static void
register_basic_types()
{
    char *str = "ABCDEFG";
    static basic_types_t basic_types[] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        { 0xff, 0xff,
          0xffff, 0xffff,
          0xffffffff, 0xffffffff,
          0xffffffffffffffff,
          0xffffffffffffffff,
          "ABCDEFG",
          0
        }
    };

    basic_types[1].p_str = str;

    xray_create_type(basic_types_t, NULL);
	xray_add_slot(basic_types_t, int8, int8_t, 0);
	xray_add_slot(basic_types_t, uint8, uint8_t, 0);
	xray_add_slot(basic_types_t, int16, int16_t, 0);
	xray_add_slot(basic_types_t, uint16, uint16_t, 0);
	xray_add_slot(basic_types_t, int32, int32_t, 0);
	xray_add_slot(basic_types_t, uint32, uint32_t, 0);
	xray_add_slot(basic_types_t, int64, int64_t, 0);
	xray_add_slot(basic_types_t, uint64, uint64_t, 0);
	xray_add_slot(basic_types_t, str, c_string_t, 0);
	xray_add_slot(basic_types_t, p_str, c_p_string_t, 0);

    xray_register(basic_types_t, &basic_types, "/basic_types", ARR_DIM(basic_types), NULL);
}

static void
register_itable()
{
    	static struct itable_example element = {
		.a = 1,
		.b = 2,
		.c = 3,
    	};
    	xray_create_type(struct itable_example, NULL);
	xray_add_slot(struct itable_example, a, int, 0);
	xray_add_slot(struct itable_example, b, int, 0);
	xray_add_slot(struct itable_example, c, int, 0);

    	xray_register_struct(struct itable_example, &element, "/itable");
}


static void
register_test()
{
    xray_create_type(ctest_t, NULL);
	xray_add_slot(ctest_t, id, int, XRAY_FLAG_PK);
	xray_add_slot(ctest_t, rate, int, XRAY_FLAG_RATE);
	xray_add_vslot(ctest_t, "b", add_one);
	xray_register(ctest_t, &test_inst, "/a/c", sizeof(test_inst)/sizeof(test_inst[0]), NULL);
	xray_register(ctest_t, &test_inst, "/rate", 1, NULL);

    xray_register(ctest_t, &test_inst, "/test", 1, NULL);
}

static void
register_is_up()
{
    static is_up_t is_up = {.is_up = "UP"};
    xray_create_type(is_up_t, NULL);
	xray_add_slot(is_up_t, is_up, c_p_string_t, 0);

    xray_register(is_up_t, &is_up, "/is_up", 1, NULL);
}

static void
test_unregister() {
	xray_register(ctest_t, &test_inst, "/this/is/a/test/path", 1, NULL);
	xray_unregister("/this/is/a/test/path");

	xray_register(ctest_t, &test_inst, "/bthis/bis/b/btest/bpath", 1, NULL);
	xray_register(ctest_t, &test_inst, "/bthis/bis/b/ctest/", 1, NULL);
	xray_unregister("/bthis/bis/b/btest/bpath");

}

static void
signal_handler(int signum, siginfo_t *siginfo, void *context)
{
	printf("Terminating signal called, exiting...\n");
    exit_test = 1;
}

static void
register_signal_term()
{
    struct sigaction handler =
	{
		.sa_sigaction = signal_handler,
		.sa_flags = SA_SIGINFO,
	};
	sigfillset(&handler.sa_mask);
	sigaction(SIGTERM, &handler, NULL);
}

#include "pthread.h"
void *
xray_loop(void *Args)
{
	while(1){
		xray_handle_loop();
		sleep(1);
	}
	return NULL;
}

int main() {
	register_signal_term();
	xray_init("04BE197BCBC14D8E", 1);

//	pthread_t inc_x_thread;
//	pthread_create(&inc_x_thread, NULL, xray_loop, NULL);

    register_is_up();
    register_test();
    register_basic_types();
    register_itable();
    test_unregister();

	while(exit_test == 0) {
		test_inst[0].rate += 10;
		sleep(1);
	}
}


