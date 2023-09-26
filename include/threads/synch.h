#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore {
	unsigned value;             /* Current value. */ // 동시에 리소스에 접근할 수 있는 스레드 갯수. 0인경우 해당 리소스가 사용중.
	// 세마포어는 동시성 제어를 위한 프로그래밍 구조, 여러 프로세스나 스레드가 공유 자원에 접근하는것을 조정하는데 사용
	//  waiters : 세마포어를 통해 공유 자원에 접근하려고 하지만 현재 그 자원이
	//다른 스레드나 프로세스에 의해 사용중이라서 대기 상태인 프로세스나 스레드를 말함
	// 이들은 세마포어값이 증가하여(공유 자원이 사용 가능해짐을 나타내는 시그널을 받으면서)
	// 그들이 대기하고 있는 작업을 진행할 수 있게됨
	struct list waiters;        /* List of waiting threads. */ // 연결리스트에있는 노드가 쓰레드다.
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition {
	struct list waiters;        /* List of waiting threads. */
};

void cond_init (struct condition *);

void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
