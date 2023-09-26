#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

// ticks에 도달하지 않은 스레드를 담을 연결 리스트 sleep_list 선언, thread_init에서 초기화
static struct list sleep_list;

// lock 에 등록을 하고싶은 쓰레드의 리스트
static struct list wait_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
// 프로세스가 cpu를 할당받아 실행하는 시간의 단위.
// 각 프로세스가 일정한 시간 동안(time slice)만 cpu를 점유하고, 그 시간이 지나면 다음 프로세스에게
// cpu를 넘겨주게 됨. 모든 프로세스가 공정하게 CPU시간을 할당받을 수 있음.
// 각 작업에 할당되는 시간 단위
#define TIME_SLICE 4		  /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */

//    쓰레드 시스템 초기화
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF); // Intr가 disabld 상태인지 확인, 꺼져있으면 thread init을 실행시킨다.

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the globla thread context */
	lock_init(&tid_lock);		 // tid 생성기에 접근,, 한명만 들어올 수 있도록. 다른 스레드가 들어오지 못하도록 자물쇠는 거는것. 임계영역= 다른 스레드가 접근하면 안되는 곳. 한번에 여러 스레드가 접근하면 안되기때문에
	list_init(&ready_list);		 // 레디큐초기화
	list_init(&sleep_list);		 // 슬립 리스트 초기화
	list_init(&wait_list);		 // lock 대기 리스트 초기화
	list_init(&destruction_req); // 파괴요청이 들어오는 스레드들 을 집어넣는 리스트

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread(); // 현재 돌아가고있는 스레드의 구조체가 저장될 주를 반환 받음.
	// running thread함수 => 그림에서 밑에 부분의 밑으ㅏㅣ부분...
	init_thread(initial_thread, "main", PRI_DEFAULT); // 쓰레드가 초기화 됨. 이름을 main으로
	initial_thread->status = THREAD_RUNNING;		  // 쓰레드상태를 러닝으로 바꿈 초기상태:blocked. 아직 돌아가는 상태는 아님
	initial_thread->tid = allocate_tid();			  // tid 부여받음. 쓰레드의 아이디값.
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void)
{
	/* Create the idle thread. */
	// 메인쓰레드가 제일 먼저 생성.  무조건 만들어짐.
	struct semaphore idle_started;
	sema_init(&idle_started, 0);						 // 우선순위가 젤 낮은것(idle 쓰레드를 우선순위 0, 우선순위가 제일 낮다) start
	thread_create("idle", PRI_MIN, idle, &idle_started); // idle = 공회전, cpu 가 멈추면 안되니까. idle 을 만든다. main 내에서 계속 돌아야 되는데 무한루프 되니깐 ,, idle 이 있으면 idle 내부로 돌게 됨.

	/* Start preemptive thread scheduling. */
	intr_enable(); // 인터럽트 활성화 = 선점을 하겠다..
	// 인터럽트를 건다는것 자체가 선점한다는것.
	// 다른 쓰레드가 cpu 쓰고있어도 내가 치우고 들어간다.
	// 비선점 = 선점할 수 없다. 비킬 수 없다
	// 선점이면 비킬 수 있다.
	// context switching

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
	// 초기화 되기전에 다음꺼 실행할 수 없어서,, 이렇게 세마포어를 사용했다.
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++; // 맨처음 스레드 생성햇을때 가장 먼저 생성된것. 아이들 = 스ㄹㅔ드 = 공회전
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++; // main

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.(지금 멈춰있는것)
	 Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);

	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO); // 페이지를 할당받아서 스레드에 가리키게 만든다.
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	// Tf = 스위칭할때 필요한것
	t->tf.rip = (uintptr_t)kernel_thread; // rip = 인스트럭션 포인터. 명령어 실행될 주소
	// 만들었고, 호출할때 선점 상태로 만들어주는것.
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock(t); // 블록을 해제한다 = 레디큐에 넣어준다.
	// priority 순서대로.. 정렬하는 과정 필요
	// 현재 실행중인 스레드와, 새로 삽입된 스레드의 우선순위를 비교한다.
	// 새로 들어오는 스레드의 우선순위가 더 높으면 CPU를 양보한다.

	// running thread 와 ready_list 의 가장 앞 thread 의 priority 를 비교하고, 만약 ready_list 의 스레드가 더 높은 priority 를 가진다면
	//  thread_yield () 를 실행하여 CPU 점유권을 넘겨주는 함수를 만들었다. 이 함수를 thread_create() 와 thread_set_priority() 에 추가하여 준다.
	// f(!list_empty(&ready_list) && thread_current()->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority)
	if (!list_empty(&ready_list) && thread_current()->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority)
	{
		thread_yield();
		// todo : Schedule 해줘야 하는거 아닌가...?
		// schedule();
	}

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
//  러닝 쓰레드의 상태를 블럭으로 바꾸고 스케줄 조정해주기.
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t)); // 스레드의 매직값이 같은지 아닌지 확인하기. 같지 않으면 오버플로우. 같으면 사용 가능.

	old_level = intr_disable(); // 인터럽트 비활성화 시키기. 다른 인터럽트가 발생하면 안되므로.
	ASSERT(t->status == THREAD_BLOCKED);
	// list_push_back (&ready_list, &t->elem);
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);
	t->status = THREAD_READY;  // 레디 상태로 만들어주기.
	intr_set_level(old_level); // 이전 상태로 만들어줌.
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void)
{							 // destroy. = kill
	ASSERT(!intr_context()); // 외부 인터럽트가 아닐때,

#ifdef USERPROG
	process_exit();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void)
{ // 뺏긴다. 현재 돌고있는 쓰레드 = 선점 쓰레드. 뺏기는 과정 = 두 스케줄
	struct thread *curr = thread_current();
	enum intr_level old_level;
	// 외부 인터럽트  = 하드 인터럽트
	// 외부 인터럽트 처리중에는 true  그외의 모든 시간엔 false..
	ASSERT(!intr_context());

	old_level = intr_disable(); // 인터럽트 비활성화. 올드레벨은 이전 상태를 받아옴
	if (curr != idle_thread)	// 현재 스레드가 아이들 스레드 아닐때, 레디 중인 스레드가 없다.
		// list_push_back (&ready_list, &curr->elem);  // 레디큐에 넣는다.
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL);
	do_schedule(THREAD_READY); // 뺏기는 과정. 현재 러닝중인 스레들을 인자로 넣어준 것으로 바꾸고, 레드큐에 있던걸 러닝 쓰레드로 바꿔줌. 현재 러닝 스레드를 레디큐에 넣어주기 위해서 레디라는 상태로 넣어줌 . 양보당하는애가 레디 상태로 되고, 두 스케줄 함수 = 현재 러닝중인 쓰레드를 인자로 넣어준 상태로 바꾸고 레디큐에 있는것을 러닝 스레드로 바꾼다
	intr_set_level(old_level);
}

// 잠든 스레드를 sleep_list에 삽입하는 함수
//  sleep_list에 ticks가 작은 스레드가 앞부분에 위치하도록 정렬하여 삽입한다.
void thread_sleep(int64_t wake_tick)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;
	ASSERT(!intr_context());
	old_level = intr_disable();
	if (curr != idle_thread)
	{
		// 이시간에 일어나렴
		curr->wakeup_tick = wake_tick;
		list_insert_ordered(&sleep_list, &curr->elem, cmp_thread_ticks, NULL);
		// 리스트 푸시백
		// 레디리스트는 우선순위 정렬이 안되어있어서 뒤에 푸시한다.
	}

	do_schedule(THREAD_BLOCKED);
	intr_set_level(old_level);
}

void thread_wake(int64_t elapsed)
{

	while (!list_empty(&sleep_list) && list_entry(list_front(&sleep_list), struct thread, elem)->wakeup_tick <= elapsed)
	{
		struct list_elem *front_elem = list_pop_front(&sleep_list);
		thread_unblock(list_entry(front_elem, struct thread, elem));
	}
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{
	// set priority of the current thread;
	//  priority change..
	thread_current()->priority = new_priority;
	if (!list_empty(&ready_list) && thread_current()->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority)
	{
		thread_yield();
		// todo : Schedule 해줘야 하는거 아닌가...?
		// schedule();
	}
	// reorder the ready list ???????
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
	return thread_max_priority(thread_current());
}

int thread_max_priority(struct thread *t)
{
	int max_donated = 0;

	for (int i = 0; i < 64; i++)
	{
		if (t->donate_list[i] > 0)
		{
			max_donated = i;
		}
	}
	return t->priority > max_donated ? t->priority : max_donated;
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED)
{
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;
	idle_thread = thread_current();
	sema_up(idle_started); //

	for (;;)
	{
		/* Let someone else run. */
		intr_disable();
		thread_block();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);						   // 0으로초기화
	t->status = THREAD_BLOCKED;						   // 블록 상태로
	strlcpy(t->name, name, sizeof t->name);			   // 인자로 받은 이름을 스레드 이름으로 하는것
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *); // 스레드의 스택포인터 설정. tf=>스위칭 정보 들어있는 구조체
	t->priority = priority;
	t->magic = THREAD_MAGIC; // 스택오버플로우 여부 확인
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
// 현재 핀토스는 ready list에 push 는 맨 뒤에, pop은 맨앞에서 하는 round-robin 형식
// 쓰레드간의 우선순위 없이 REAdy_list에  들어온 순서대로 실행함.
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"
		"movq 0(%%rsp),%%r15\n"
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n"
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq"
		: : "g"((uint64_t)tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		: : "g"(tf_cur), "g"(tf) : "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{ // 레디큐가 빌때까지
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}

	thread_current()->status = status;
	schedule();
}

static void
schedule(void)
{
	struct thread *curr = running_thread();
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING); // 러닝 상태가 아니면 다음으로 넘어감(이 함수 호출 직전에 블럭 함수으로 만들어주었던 것을 확인)
	ASSERT(is_thread(next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	// tick = 시간 얼마나 썼나 ? 다음으로 넘어갔으니까.
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next);
#endif

	if (curr != next)
	{
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		// 죽었다고 마킹한 스레드들이 모여있는 곳으로 보낸다.
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{ // initial thred = main thread
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

bool cmp_thread_ticks(struct list_elem *a_, struct list_elem *b_, void *aux UNUSED)
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);
	return (a->wakeup_tick < b->wakeup_tick);
}

// 우선순위가 높은것이 먼저와야 하기 때문에.
bool cmp_priority(struct list_elem *a_, struct list_elem *b_, void *aux UNUSED)
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);
	return (thread_max_priority(a) > thread_max_priority(b));
}