#include <cos_component.h>
#include <print.h>
#include <res_spec.h>
#include <sched.h>
#include <mem_mgr.h>

#include <periodic_wake.h>
#include <timed_blk.h>

#include <test_monitor.h>
#include <lmon_ser1.h>

int high, low, med;
int warm;

#define ITER 5

#ifdef MEAS_LOG_OVERHEAD
#define RUN 100
#define RUNITER 10000
#endif

#define US_PER_TICK 10000

void test_iploop()
{
	vaddr_t ip;
	
	printc("set eip back testing....\n");

	ip = cos_sched_introspect(COS_SCHED_THD_IP_LFT, cos_spd_id(), med);
	assert(ip);
	timed_event_block(cos_spd_id(), 5);
	return;
}

void 
cos_init(void)
{
	static int first = 0;
	union sched_param sp;
	int i, j, k;
	
	if(first == 0){
		first = 1;

		sp.c.type = SCHEDP_PRIO;
		sp.c.value = 10;
		warm = sched_create_thd(cos_spd_id(), sp.v, 0, 0);

		sp.c.type = SCHEDP_PRIO;
		sp.c.value = 11;
		high = sched_create_thd(cos_spd_id(), sp.v, 0, 0);

		sp.c.type = SCHEDP_PRIO;
		sp.c.value = 15;
		med = sched_create_thd(cos_spd_id(), sp.v, 0, 0);

		sp.c.type = SCHEDP_PRIO;
		sp.c.value = 20;
		low = sched_create_thd(cos_spd_id(), sp.v, 0, 0);

	} else {
#ifdef MEAS_LOG_OVERHEAD  // only 1 cli and 1 ser
		if (cos_get_thd_id() == warm) {
			timed_event_block(cos_spd_id(), 2);
			unsigned long long start, end;
			printc("<<< SIMPLE PPONG TEST BEGIN! >>>\n");
			
			for (k = 0; k < RUN; k++){
				for (i = 0; i< RUNITER; i++) {
					rdtscll(start);
					lmon_ser1_test();
					rdtscll(end);
					printc("invocation cost: %llu\n", (end-start));

				}
			}
			printc("\n<<< PPONG TEST END! >>>\n");
		}
#elif defined EXAMINE_PI   // 1 cli and 1 ser, also depends on lock, scheduler (maybe period if we need periodic task)
		if (cos_get_thd_id() == high) {
			timed_event_block(cos_spd_id(), 6);
			printc("<<<high thd %d>>>\n", cos_get_thd_id());
			test_iploop();
			return;

			printc("<<<high thd %d>>>\n", cos_get_thd_id());
			periodic_wake_create(cos_spd_id(), 13);
			try_cs_hp();
		}
		
		if (cos_get_thd_id() == med) {
			printc("<<<med thd %d>>>\n", cos_get_thd_id());
			periodic_wake_create(cos_spd_id(), 21);
			/* timed_event_block(cos_spd_id(), 3); */
			try_cs_mp();
		}

		if (cos_get_thd_id() == low) {
			return;
			printc("<<<low thd %d>>>\n", cos_get_thd_id());
			periodic_wake_create(cos_spd_id(), 37);
			try_cs_lp();
		}
#elif defined EXAMINE_DEADLINE   //  periodic task
		if (cos_get_thd_id() == high) {
			printc("<<<high thd %d 10>>>\n", cos_get_thd_id());
			int this_ticks = 10;
			periodic_wake_create(cos_spd_id(), this_ticks);  // c/t =  2/10   (20%)
			timed_event_block(cos_spd_id(), 5);
			lmon_ser1_test();
		}

		if (cos_get_thd_id() == med) {
			printc("<<<med thd %d 22>>>\n", cos_get_thd_id());
			int this_ticks = 22;
			periodic_wake_create(cos_spd_id(), this_ticks);   // c/t = 4/22 (18%)
			timed_event_block(cos_spd_id(), 2);
			lmon_ser1_test();
		}

		if (cos_get_thd_id() == low) {
			printc("<<<low thd %d 56>>>\n", cos_get_thd_id());
			int this_ticks = 56;
			periodic_wake_create(cos_spd_id(), this_ticks);     // c/t = 12/56 (21%)
			lmon_ser1_test();
		}
#elif defined NORM  // first example, 2 clients and 1 ser1 and 1 ser2
		if (cos_get_thd_id() == high) {
			timed_event_block(cos_spd_id(), 50);
			j = 0;
			while(j++ < ITER) lmon_ser1_test();
		}

		if (cos_get_thd_id() == low) {
			timed_event_block(cos_spd_id(), 50);
			j = 0;
			while(j++ < ITER) lmon_ser1_test();
		}

		if (cos_get_thd_id() == warm) {
			printc("<<<warm thd %d>>>\n", cos_get_thd_id());
			i = 0;
			while(i++ < ITER) lmon_ser1_test();
		}
#endif		
	}

	return;
}
