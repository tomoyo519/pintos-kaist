#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

// 스레드의 상태
/* States in a thread's life cycle. */
enum thread_status
{
	THREAD_RUNNING, /* Running thread. 실행중(딱 하나)*/
	THREAD_READY,	/* Not running but ready to run. (돌아가기만 하면 되는상태)*/
	THREAD_BLOCKED, /* Waiting for an event to trigger. 의도적으로 이벤트에 의해 멈춤. 꺠워주지않으면 멈춤 레디큐에 못들어가게 함. */
	THREAD_DYING	/* About to be destroyed. 죽였다고 마킹하는것. 실제로 죽이는건 모아서 따로*/
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;			  // 스레드의 아이디
#define TID_ERROR ((tid_t)-1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0	   /* Lowest priority. 우선순위가 젤 낮음. 아이들 시스템. 다른 스레드들이 없을 때 아이들 스레드를 돌린다. 아이들 = 공회전 */
#define PRI_DEFAULT 31 /* Default priority. 초기값(절반) */
#define PRI_MAX 63	   /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page(페이지).  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                    (커널스택)     |
 *           | 우리는 운영체제만들어서 여기           |
 *           +---------------------------------+
 *           |  magic-페이지가 더럽혀졋냐 아니냐 알게됨 -쓰레드냐 아니냐 판단하게됨 (쓰레드구조체  |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB. 너무커지면 스택공간을 침범. 최소화.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead. 커널스택도 너무 커지면 안됨. 스택이 오버플로우가 발생함.
 * 그래서 동적할당을 해야됨.
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread
{
	/* Owned by thread.c. */
	tid_t tid;				   /* Thread identifier. */
	enum thread_status status; /* Thread state. */
	// uint8_t *stack;  // saved stack pointer;
	// thread A에서 스케줄링이 발생하면, CPu가 실행중이던 상태 즉 현재 CPU register 들에 저장된 값들을 STack 에 저장
	// 후에 다시 Thread A가 실행되는 순간에 STack의 값들이 다시 CPU의 register로 로드되어 실행을 이어나갈 수 있다.
	char name[16];		 /* Name (for debugging purposes). */
	int priority;		 /* Priority. */
	int64_t wakeup_tick; // 일어날 시각 추가;
	// 현재 틱을 확인하는 변수를 추가해야됨.
	// tic++ 되는게 thread_tick 뭐가 실행될때마다 틱이 올라감.
	// 레디큐, 블락드에는 틱이 안올라감
	// 현재 실행중인것만 틱이 올라감
	// 틱 = 실행되고있는동안 얼마나 시간이 경과됬는지가 틱임
	// 콘솔에 찍히는게 틱이다.아하
	// 레디큐에 넣는것 자체가 꺠우는것이다.
	// 블락드 리스트에서 레디로 가는것을 스케줄링 하는것,
	// 조건을 달아서 언제 얘를 꺠울것이냐,
	// 그 기준이 틱인데
	// 현재 돌아가고있는 쓰레드의 틱과, 블락드 리스트의 가장 앞에 있는 스레드의 틱을 비교해서
	// 현재 스레드보다 틱이 낮은게 있으면 레디로 넣는다.(그떄 깨운다)
	// 현재 실행중인 스레드가 틱이 올라가다가, 블락드의 가장 앞에 잇는것<-가장 틱 낮은것
	// 어 ? 나보다 낮아졋네 ? 이제 레디로 가라.
	/* Shared between thread.c and synch.c. */
	struct list_elem elem; /* List element. 레디큐에 들어가는 요소 */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4; /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf; /* Information for switching 쓰레드간에 스위칭할때, 스위칭할때 저장해야하는 정보*/
	unsigned magic;		  /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);
void thread_sleep(int64_t ticks);

int thread_get_priority(void);
void thread_set_priority(int);
int thread_max_priority(struct thread *);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);
void thread_wake(int64_t ticks);

void do_iret(struct intr_frame *tf);

// 일어날 시간이 이른 스레드가 앞부분에 위치하도록 정렬할 때 사용할 정렬 함수를 새로 선언한다.
void sort_thread_ticks();

// 두 스레드의 wakeup_ticks를 비교해서 작으면 true를 반환하는 함수
bool cmp_thread_ticks(struct list_elem *a_, struct list_elem *b_, void *aux UNUSED);

bool cmp_priority(struct list_elem *a_, struct list_elem *b_, void *aux UNUSED);
#endif /* threads/thread.h */
