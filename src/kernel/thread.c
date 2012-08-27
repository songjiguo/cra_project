/**
 * Copyright 2007 by Gabriel Parmer, gabep1@cs.bu.edu
 *
 * Redistribution of this file is permitted under the GNU General
 * Public License v2.
 */

//#include <thread.h>
//#include <spd.h>
#include "include/thread.h"
#include "include/spd.h"
#include "include/page_pool.h"
#include "include/recovery.h"

struct thread threads[MAX_NUM_THREADS];
static struct thread *thread_freelist_head = NULL;

struct thread SRT_threads_head;	/* soft real time threads */
struct thread HRT_threads_head; /* hard real time threads */
struct thread BST_threads_head; /* best effort threads */

/* like "current" in linux */
struct thread *current_thread = NULL;

/* 
 * Return the depth into the stack were we are present or -1 for
 * error/not present.
 */
int thd_validate_spd_in_callpath(struct thread *t, struct spd *s)
{
	int i;

	assert(t->stack_ptr >= 0);

	for (i = t->stack_ptr ; i >= 0 ; i--) {
		struct thd_invocation_frame *f;

		f = &t->stack_base[i];
		assert(f && f->current_composite_spd);
		if (thd_spd_in_composite(f->current_composite_spd, s)) {
			return t->stack_ptr - i;
		}
	}
	return -1;
}

/* initialize the fault tolerance related DS */
static void thd_ft_init(struct thread *thd, int thd_id)  
{
	int i;

	if (!thd_id) thd->thread_id = 0; /* only for these heads */

	for (i = 0 ; i < MAX_SCHED_HIER_DEPTH; i++) {
		thd->sched_info[i].thread_user_prio = 0;
		thd->sched_info[i].thread_fn        = NULL;
		thd->sched_info[i].thread_dest      = NULL;
	}

	thd->sched_next = NULL;
	thd->sched_prev = NULL;

	thd->crt_tail_thd = NULL;
	thd->crt_next_thd = NULL;
	thd->crt_in_spd   = NULL;

	return;
}

void thd_init_all(struct thread *thds)
{
	int i;

	for (i = 0 ; i < MAX_NUM_THREADS ; i++) {
		/* adjust the thread id to avoid using thread 0 clear */
		thds[i].thread_id = i+1;
		thds[i].freelist_next  = (i == (MAX_NUM_THREADS-1)) ? NULL : &thds[i+1];
		
		thd_ft_init(&thds[i], 1);
	}

	thread_freelist_head = thds;

	thd_ft_init(&SRT_threads_head, 0);
	thd_ft_init(&HRT_threads_head, 0);
	thd_ft_init(&BST_threads_head, 0);

	return;
}

extern void *va_to_pa(void *va);
extern void thd_publish_data_page(struct thread *thd, vaddr_t page);

/* flag 1 means recording the thread in kernel, otherwise not */
struct thread *thd_alloc(struct spd *spd)
{
	struct thread *thd;
	unsigned short int id;
	void *page;

	thd = thread_freelist_head;
	if (thd == NULL) {
		printk("cos: Could not create thread.\n");
		return NULL;
	}
	
	page = cos_get_pg_pool();
	if (NULL == page) {
		printk("cos: Could not allocate the data page for new thread.\n");
		
	}
	thread_freelist_head = thread_freelist_head->freelist_next;
	
	id = thd->thread_id;
	memset(thd, 0, sizeof(struct thread));
	thd->thread_id = id;

	thd->data_region = page;
	*(int*)page = 4; /* HACK: sizeof(struct cos_argr_placekeeper) */
	thd->ul_data_page = COS_INFO_REGION_ADDR + (PAGE_SIZE * id);
	thd_publish_data_page(thd, (vaddr_t)page);

	/* Initialization */
	thd->stack_ptr = -1;
	/* establish this thread's base spd */
	thd_invocation_push(thd, spd, 0, 0);
	inv_frame_fault_cnt_update(thd, spd);

	thd->flags = 0;

	thd->thread_brand = NULL;
	thd->pending_upcall_requests = 0;
	thd->freelist_next = NULL;

	return thd;
}


void thd_free(struct thread *thd)
{
	if (NULL == thd) return;

	while (thd->stack_ptr > 0) {
		struct thd_invocation_frame *frame;

		/*
		 * FIXME: this should include upcalling into effected
		 * spds, to inform them of the deallocation.
		 */

		frame = &thd->stack_base[thd->stack_ptr];
		spd_mpd_ipc_release((struct composite_spd*)frame->current_composite_spd);

		thd->stack_ptr--;
	}

	if (NULL != thd->data_region) {
		cos_put_pg_pool((struct page_list*)thd->data_region);
	}

	thd->freelist_next = thread_freelist_head;
	thread_freelist_head = thd;

	return;
}

void thd_free_all(void)
{
	struct thread *t;
	int i;

	for (i = 0 ; i < MAX_NUM_THREADS ; i++) {
		t = &threads[i];

		/* is the thread active (not free)? */
		if (t->freelist_next == NULL) {
			thd_free(t);
		}
	}
}

void thd_init(void)
{
	thd_init_all(threads);
	current_thread = NULL;
}

extern int host_in_syscall(void);
extern int host_in_idle(void);
/*
 * Is the thread currently in an atomic section?  If so, rollback its
 * instruction pointer to the beginning of the section (the commit has
 * not yet happened).
 */
int thd_check_atomic_preempt(struct thread *thd)
{
	struct spd *spd = thd_get_thd_spd(thd);
	vaddr_t ip = thd_get_ip(thd);
	int i;
	
	assert(host_in_syscall() || host_in_idle() || 
	       thd->flags & THD_STATE_PREEMPTED);

	for (i = 0 ; i < COS_NUM_ATOMIC_SECTIONS/2 ; i+=2) {
		if (ip > spd->atomic_sections[i] && 
		    ip < spd->atomic_sections[i+1]) {
			thd->regs.ip = spd->atomic_sections[i];
			cos_meas_event(COS_MEAS_ATOMIC_RBK);
			return 1;
		}
	}
	
	return 0;
}

void thd_print_regs(struct thread *t) {
	struct pt_regs *r = &t->regs;
	struct spd *s = thd_get_thd_spd(t);

	printk("cos: spd %d, thd %d w/ regs: \ncos:\t\t"
	       "eip %10x, esp %10x, eax %10x, ebx %10x, ecx %10x,\ncos:\t\t"
	       "edx %10x, edi %10x, esi %10x, ebp %10x \n",
	       spd_get_index(s), thd_get_id(t), (unsigned int)r->ip, (unsigned int)r->sp, 
	       (unsigned int)r->ax, (unsigned int)r->bx, (unsigned int)r->cx, (unsigned int)r->dx, 
	       (unsigned int)r->di, (unsigned int)r->si, (unsigned int)r->bp);

	return;
}
