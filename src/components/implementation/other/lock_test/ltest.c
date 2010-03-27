#include <cos_component.h>
#include <print.h>

#include <mem_mgr.h>
#include <cos_synchronization.h>
#include <sched.h>
#include <cos_alloc.h>
#include <timed_blk.h>

#define ITER (1024)
#define CONTENDED 1

cos_lock_t lock;
#define LOCK_TAKE()    lock_take(&lock)
#define LOCK_RELEASE() lock_release(&lock)

static void test_uncontended_lock(void)
{
	int i;
	unsigned long long start, end, tot, avg;
	
	rdtscll(start);
	for (i = 0 ; i < ITER ; i++) {
		LOCK_TAKE();
		LOCK_RELEASE();
	}
	rdtscll(end);
	tot = end-start;
	avg = tot/ITER;
	printc(">>> uncontended avg cost: %lld\n", avg);
}

volatile int spin = 1;

static void test_contended_lock_hp(void)
{
	int i;
	unsigned long long start, end, tot = 0, avg;
	
	for (i = 0 ; i < ITER ; i++) {
		timed_event_block(cos_spd_id(), 1);
		spin = 0;
	
		rdtscll(start);
		LOCK_TAKE();
		rdtscll(end);
		LOCK_RELEASE();
		tot += end-start;
//		printc(">>> %lld\n", end-start);
	}
	avg = tot/ITER;
	printc(">>> contended avg cost: %lld\n", avg);
}

static void test_contended_lock_lp(void)
{
	while (1) {
		LOCK_TAKE();
		while (spin);
		spin = 1;
		LOCK_RELEASE();
	}
}

void cos_init(void)
{
#ifdef CONTENDED
	static int first = 1;

	if (first) {
		struct cos_array *data;
		short int hp_thd;
		first = 0;

		lock_static_init(&lock);

		data = cos_argreg_alloc(sizeof(struct cos_array) + 4);
		assert(data);
		strcpy(&data->mem[0], "r-1");
		data->sz = 4;
		if (0 > (hp_thd = sched_create_thread(cos_spd_id(), data))) BUG();
		cos_argreg_free(data);

		test_contended_lock_lp();
	} else {
		test_contended_lock_hp();
	}
#else
	test_uncontended_lock();
#endif
}

void bin (void)
{
	sched_block(cos_spd_id(), 0);
}

