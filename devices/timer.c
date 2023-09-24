#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */
//원하는 빈도로 타이머 설정하기

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */

	   //인터럽트 되는 코드. 하드웨어 적인 코드. 그래서 어셈블리어로 되어있나 ?
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

// 0x43 = 타이머 깨우기.
// PIT (가상의 타이머) 를 초기화하는것.
	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */ 
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then; // 현재의 틱스 - start(인자로 들어오는것)
}

//스레드를 틱 타이머 동안 실행을 일시 중단
/* Suspends execution for approximately TICKS timer ticks. */
void
timer_sleep (int64_t ticks) {
	//현재시간 + 잠들어있어라시간 = 일어나야하는시간
	int64_t wake_tick = timer_ticks() + ticks; // 현재 틱스. 1초에 5틱이다. 커서 깜빡. cpu점유할수있는 시간.
	ASSERT (intr_get_level () == INTR_ON); // 인터럽트 활성화 상태인가? 그래야 일드가 가능함.
	// while 문을 삭제하고, readyque에 들어가기전에 blocked list를 만들어서, blocked list 에 tic이 낮은 순서대로 입력되도록 해야함.
	//tic 이 낮은 순서를 확인하려면 쓰레드 구조체에 현재 틱을 확인 할 수 있는 변수를 추가해야 함.
	// 타이머를 만드는 이유 = 커서 깜빡이는걸 좀더 빨리...

	//busy-waiting 방식으로 구현된 기존의 코드
	// ( 현재시간 - 시작시간 < 틱 ) 인경우, =아직 틱에 도달하지 않았으면 thread_yield() 함수를 호출해서, 다른 스레드에게 CPU를 양보한다.
	// 스레드가 잠들면, 스케줄 대기열인 ready_list에 추가하고 있어서,
	//아직 깰 시간이 되지않은, ticks에 도달하지않은 스레드가 깨워지는 일 발생.
	// 🪰 **해야할일 **
	// 잠든 스레드가 깰 시간(ticks)에 도달할때까진 ready_list에 추가하지않고, 깰시간에 도달한 경우에만 ready_list에 추가하는 방식으로
	//아직 ticks에 도달하지 않은 쓰레드가 꺠워지는 일은 없도록 만들기 
	// while (timer_elapsed (start) < ticks) // timer_elapsed (start) start로부터 시간이 얼마나 지났냐, 처음엔 timer_elapsed (start) 는 0에 가깝다.
		// thread_yield (); // 잠든 스레드가 ready-list에 삽입된다.
	//block list로 빼기
	//block 함수

	//틱이란 = 일정 시간 간격으로 발생하는 시스템의 기본적인 시간 단위
	//운영체제는 이러한 틱을 사용해서 시스템 상태를 유지하고 다양한 작업을 스케줄링 한다.
	// 사용하는 이유 = 운영체제가 특정 작업을 실행하기 위해 기다리는 시간을 정확히 계산할 수 있다.
	// 틱 단위로 시간을 추적함으로써 시스템의 부하를 줄이고 일관성 유지
	// 틱을 사용해서 CPU 사용률을 조절할 수 있다. - 틱의 간격을 더 작게 조정하면, 시스템은 더 자주 인터럽트를 처리하여 작업을 스케줄링 할 수 있게 된다.
	thread_sleep(wake_tick);
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick();
	thread_wake(ticks);
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
