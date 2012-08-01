#include <cos_component.h>
#include <print.h>
#include <cos_component.h>
#include <res_spec.h>
#include <sched.h>
#include <pong.h>

#define THREAD1 11
#define NUM 10

static void
ping_pong_test()
{
	int i = 0;
	while(i++ <= NUM) {
		if (i == 4) {
			printc("to fail....");
		}
		pong();
	}
	return;
}

void 
cos_init(void)
{
	static int first = 0;
	union sched_param sp;

	if(first == 0){
		first = 1;

		sp.c.type = SCHEDP_PRIO;
		sp.c.value = THREAD1;
		sched_create_thd(cos_spd_id(), sp.v, 0, 0);
	} else {
		ping_pong_test();
	}
	
	return;
}