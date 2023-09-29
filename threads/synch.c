/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{ // 세마포어로 들어갔음.
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters); // 밸류 넣어주고 웨터를 초기화 하고 끝
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */

// sema_down 은 인터럽트를 비활성화 한 상태에서 호출가능.
// 현재 러닝 중인 스레드를 블럭으로 만들어줌.
// 세마포어밸류 = 0이면, 현재 스레드를 대기 상태로 만들고, 다른 스레드가 세마포어를 해제할때까지 기다리는 역할.
// 여러 스레드 간에 공유 자원에 대한 접근을 제어하고 동기화할 수 있음.
// 다중 스레드 환경에서 상호배제 할떄 사용됨
void sema_down(struct semaphore *sema)
{ //  cspp 12장에 나온 P
	enum intr_level old_level;
	// 세마 포인터가 유효함?
	ASSERT(sema != NULL);
	// 현재 코드가 인터럽트 핸들러 컨텍스트에서 실행중인지 아닌지 확인
	ASSERT(!intr_context());

	// 언제 락을 해제하는지 ???

	old_level = intr_disable(); // 현재 인터럽트 상태를 저장하고, 인터럽트 비활성화.
	// 세마포어값이 0이면 어떤 스레드가 이 리소스를 사용중이란 ㄴ의미이므로, 다른 스레드가 세마포어를 해제할떄까지 기다림
	while (sema->value == 0)
	{ // value = 0일때 실행
		// 무한루프
		// 안에서 현재 실행중인 스레드 (자기 자신을 웨이트 리스트에 넣고 자기자신을 블락시킴, 해당 스레드는 블락된 상태로 되고 다른 스레드로 스케줄링이 됨. 다른거 실행된 후에 세마업 호출됨. 세마업에 가면
		//  언블록 시키고, 세마포어를 1 증가시킴 . 현재 블락된 스레드중에서 맨앞에 있는거 하나를 언블록으로 만들어줌. 블락된 스레드를 러닝상태로 만들어줌
		//  현재 실행중인 스레드를 세마포어의 대기자 목록에 추가함 = 스레드가 대기중
		//  todo : prority 순서대로 sort 되도록 해야 한다.
		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL);
		// list_push_back (&sema->waiters, &thread_current ()->elem); // 현재 러닝 중인 스레드를 맨 끝부분(웨이터리스트)에 넣고, 그 스레드를 블럭상태로 만들어. 웨이트에 들어갔으니까.
		//  현재 스레드를 블록 상태로 만든다. 스레드 스케줄러에 의해 다른 스레드가 실행될 때까지 기다릴것임.
		thread_block(); // 다른 인터럽트를 방지하기 위해서.
	}
	// 세마포어 값 감소, 스레드가 리소스를 사용중, 다른 스레드가 이 리소스를 사용하지 못하도록 함
	sema->value--;
	// 이전에 저장한 인터럽트 상태를 복원하여 인터럽트를 다시 활성화 함
	intr_set_level(old_level); // 다시 인터럽트복귀...
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	// todo: 추가공부 필요
	// unblock() 이후 unblock된 스레드가 running 스레드보다 우선순위가 높을 수 있어 thread_comp_ready() 실행 → CPU선점
	old_level = intr_disable();

	if (!list_empty(&sema->waiters))
	{
		list_sort(&sema->waiters, cmp_priority, 0);
		thread_unblock(list_entry(list_pop_front(&sema->waiters),struct thread, elem));
	}

	sema->value++;
	//실행되기 전 스케줄링을 위해.
	thread_comp_ready();
	intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock)
{

	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));
	// 내가 빨리가야됨.
	//  홀더가 나보다 낮은애가 나보다 방해할때,
	// 기부해야됨
	//  나의 우선순위를 홀더에게
	// this is donation:
	if (lock->holder)
	{
		// lock->holder->priority =  thread_current()->priority;
		lock->holder->donate_list[thread_current()->priority]++;
		for (int i = 0; i < 64; i++)
		{
			printf("🧶 %d:%d\n", i, lock->holder->donate_list[i]);
		}
		// printf("🧶 %d\n", donate_list_len);
		// printf("🧶 %d\n", lock->holder->donate_list[thread_current()->priority]);
	}
	sema_down(&lock->semaphore);
	// 반복문 탈출 후,
	lock->holder = thread_current();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	lock->holder = NULL;
	sema_up(&lock->semaphore);
	thread_yield();
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
//    조건변수를 사용하여 스레드의 동기화와 대기를 관리하는데 사용되는 함수
// 조건변수는 다중 스레드 환경에서 스레드간 통신과 스레드 스케줄링을 위한 도구로 사용됨
// 특정 조건이 충족될때까지 스레드를 대기 상태로 만들고, 조건이 충족되면 꺠운다.
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));
	// 대기 스레드를 위한 세마포어 : waiter.semaphore 초기화
	// 이 세마포어는 스레드가 조건이 충족될때까지 대기할떄 사용됨
	sema_init(&waiter.semaphore, 0);
	// 현재 스레드를 조건변수의 대기자 리스트에 추가함. 현재 스레드는 조건 변수의
	// 대기자 목록에 들어가게 되고, 조건이 충족되기 전까지 대기상태로 된다.
	// todo: prority 순대로 들어가도록 수정하기
	// list_push_back (&cond->waiters, &waiter.elem);
	list_insert_ordered(&cond->waiters, &thread_current()->elem, cmp_priority, NULL);
	// 현재 스레드가 소유한 락을 해제함. 다른 스레드가 락을 획득할 수 있음.
	lock_release(lock);
	// 스레드를 대기 상태로 만들고, 조건이 충족되기를 기다림.
	// 조건이 충족되지 않으면 스레드는 블록됨
	sema_down(&waiter.semaphore);
	// 스레드가 대기후에 락을 획득함. 이제 스레드는 락을 가지고 조건 변수와 함꼐
	// 원하는 작업 수행 가능
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */

// 락을 획득하려 했을때 donation이 발생함 (우선순위 비교)
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));
	// todo: sort the waiters list in order of priority
	if (!list_empty(&cond->waiters))
		sema_up(&list_entry(list_pop_front(&cond->waiters),
							struct semaphore_elem, elem)
					 ->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}
