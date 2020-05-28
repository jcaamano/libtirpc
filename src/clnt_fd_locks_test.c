
#include <sys/time.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int dtbsize = 1024;
int __rpc_dtbsize()
{
	return dtbsize;
}
#define MAX_FDLOCKS_PREALLOC 1024
#include "clnt_fd_locks.h"

/* time a function */
#define time(f, d)							\
	do {								\
		struct timespec start, end;				\
		double t = 0.0;						\
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);	\
		f;							\
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);		\
		t += end.tv_sec - start.tv_sec;				\
		t += (end.tv_nsec - start.tv_nsec) * 1e-9;		\
		d += t;							\
	} while (0)

/* average time of a function */
#define timea(str, iter, pre, loop, pay, post)				\
	do {								\
		typeof(iter) i = 0;					\
		double d = 0.0;						\
		do {							\
			pre;						\
			loop						\
				time(pay, d);				\
			post;						\
		} while (++i < iter);					\
		d /= iter;						\
		printf("%s average on %d iters %.9f secs\n", str, iter, d);	\
	} while (0)


void time_fd_locks_prealloc() {
	int iter = 100;
	fd_locks_t *fd_locks[iter];
	fd_lock_t fd_lock;

	timea("fd_locks prealloc initialization",
	      iter,
	      ,
	      ,
	      fd_locks[i] = fd_locks_init(),
	     );

	timea("fd_locks prealloc access",
	      iter,
	      fd_locks_t *fdl = fd_locks[i],
	      ,
	      fd_lock_create(i, fdl),
	     );

	for (int i = 0; i < iter; ++i)
		fd_locks_destroy(fd_locks[i]);
}

void time_fd_locks_dynamic(int items) {
	int iter = 100;
	fd_locks_t *fd_locks[iter];
	fd_lock_t *fd_lock;
	char str[256];
	int start = dtbsize;
	int end = dtbsize + items;
	int last = end -1;

	for(int i = 0; i < iter; ++i)
		fd_locks[i] = fd_locks_init();

	sprintf(str, "%d dynamic fd_locks allocation & access", items);
        timea(str,
	      iter,
	      fd_locks_t *fdl = fd_locks[i],
	      for(int fd = start; fd < end; fd++),
	      fd_lock_create(fd, fdl),
	     );

	sprintf(str, "%d dynamic fd_lock access", items);
	timea(str,
	      iter,
	      fd_locks_t *fdl = fd_locks[i],
	      ,
	      fd_lock = fd_lock_create(last, fdl),
	     );

	/* just some crosschecks */
	assert((to_fd_lock_item(fd_lock))->fd == last);
	assert((to_fd_lock_item(fd_lock))->refs == 2);

	for (int i = 0; i < iter; ++i)
		fd_locks_destroy(fd_locks[i]);
}

void test_fd_locks_init() {
	const cond_t cond_init = PTHREAD_COND_INITIALIZER;
	fd_locks_t *fd_locks = fd_locks_init();

#ifdef MAX_FDLOCKS_PREALLOC
	for (int i = 0; i < MAX_FDLOCKS_PREALLOC; ++i) {
		fd_lock_t *fd_lock = &fd_locks->fd_lock_array[i];
		assert(fd_lock->active == FALSE);
		assert(memcmp(&fd_lock->cv, &cond_init, sizeof(cond_t)) == 0);
	}
#endif

	assert(TAILQ_EMPTY(to_fd_lock_list(fd_locks)));

	fd_locks_destroy(fd_locks);

	printf("test_fd_locks_init...OK\n");
}

int FREED = 0;
void __real_free(void *ptr);
void __wrap_free(void *ptr) {
	++FREED;
	static void *lastptr = NULL;
	if (lastptr) assert(lastptr != ptr);
	assert(ptr != NULL);
	__real_free(ptr);
	lastptr = ptr;
}

void test_fd_locks_destroy() {
	fd_locks_t *fd_locks;
	fd_lock_t *fd_lock1, *fd_lock2;
	int freed_array, freed_item;;
#ifdef MAX_FDLOCKS_PREALLOC
	int fd = MAX_FDLOCKS_PREALLOC;
#else
	int fd = 0;
#endif

	FREED = 0;
	fd_locks = fd_locks_init();

	fd_locks_destroy(fd_locks);

#ifdef MAX_FDLOCKS_PREALLOC
	assert(FREED == 2);
#else
	assert(FREED == 1);
#endif

	freed_array = FREED;
	FREED = 0;
	fd_locks = fd_locks_init();
	fd_lock1 = fd_lock_create(fd, fd_locks);

	fd_locks_destroy(fd_locks);

	assert(FREED > freed_array);

	freed_item = FREED - freed_array;
	FREED = 0;
	fd_locks = fd_locks_init();
	fd_lock1 = fd_lock_create(fd, fd_locks);
	fd_lock2 = fd_lock_create(fd+1, fd_locks);

	fd_locks_destroy(fd_locks);

	assert(FREED = freed_array + freed_item*2);

	printf("test_fd_locks_destroy...OK\n");
}

void test_fd_lock_create_low() {
	fd_locks_t *fd_locks = fd_locks_init();

#ifdef MAX_FDLOCKS_PREALLOC
	for (int i = 0; i < MAX_FDLOCKS_PREALLOC-1; ++i) {
		fd_lock_t *fd_lock1 = fd_lock_create(i, fd_locks);
		fd_lock_t *fd_lock2 = fd_lock_create(i, fd_locks);
		fd_lock_t *fd_lock3 = fd_lock_create(i+1, fd_locks);
		fd_lock_t *fd_lock4 = fd_lock_create(i+1, fd_locks);
		assert(fd_lock1 == fd_lock2);
		assert(fd_lock3 == fd_lock4);
		assert(fd_lock3 - fd_lock1 == 1);
	}
#endif

	fd_locks_destroy(fd_locks);

	printf("test_fd_lock_create_low...OK\n");
}

void test_fd_lock_create_high() {
	fd_lock_t *fd_lock1, *fd_lock2, *fd_lock3, *fd_lock4;
	fd_lock_item_t *item1, *item2, *item3, *item4;
#ifdef MAX_FDLOCKS_PREALLOC
	int fd = MAX_FDLOCKS_PREALLOC;
#else
	int fd = 0;
#endif

	fd_locks_t *fd_locks = fd_locks_init();

	fd_lock1 = fd_lock_create(fd, fd_locks);
	fd_lock2 = fd_lock_create(fd+1, fd_locks);
	fd_lock3 = fd_lock_create(fd+2, fd_locks);
	fd_lock4 = fd_lock_create(fd+2, fd_locks);

	item1 = to_fd_lock_item(fd_lock1);
	item2 = to_fd_lock_item(fd_lock2);
	item3 = to_fd_lock_item(fd_lock3);
	item4 = to_fd_lock_item(fd_lock4);

	assert(item3 == item4);
	assert(item3->fd == fd+2);
	assert(item3->refs == 2);
	assert(TAILQ_FIRST(to_fd_lock_list(fd_locks)) == item3);
	assert(TAILQ_NEXT(item3, link) == item2);
	assert(TAILQ_NEXT(item2, link) == item1);
	assert(TAILQ_NEXT(item1, link) == NULL);

	fd_locks_destroy(fd_locks);

	printf("test_fd_lock_create_high...OK\n");
}

void test_fd_lock_destroy() {
	fd_lock_t *fd_lock1, *fd_lock2, *fd_lock3, *fd_lock4;
	fd_lock_item_t *item1, *item2, *item3, *item4;
	int freed_item;
#ifdef MAX_FDLOCKS_PREALLOC
	int fd = MAX_FDLOCKS_PREALLOC;
#else
	int fd = 0;
#endif

	fd_locks_t *fd_locks = fd_locks_init();

	fd_lock1 = fd_lock_create(fd, fd_locks);
	fd_lock2 = fd_lock_create(fd+1, fd_locks);
	fd_lock3 = fd_lock_create(fd+2, fd_locks);
	fd_lock4 = fd_lock_create(fd+2, fd_locks);

	item1 = to_fd_lock_item(fd_lock1);
	item2 = to_fd_lock_item(fd_lock2);
	item3 = to_fd_lock_item(fd_lock3);
	item4 = to_fd_lock_item(fd_lock4);

	assert(item3->refs == 2);
	assert(TAILQ_FIRST(to_fd_lock_list(fd_locks)) == item3);

	FREED = 0;

	fd_lock_destroy(fd+2, fd_lock3, fd_locks);

	assert(item3->refs == 1);
	assert(TAILQ_FIRST(to_fd_lock_list(fd_locks)) == item3);
	assert(FREED == 0);

	fd_lock_destroy(fd+2, fd_lock3, fd_locks);

	assert(TAILQ_FIRST(to_fd_lock_list(fd_locks)) == item2);
	assert(FREED > 0);

	freed_item = FREED;
	FREED = 0;

	fd_lock_destroy(fd, fd_lock1, fd_locks);

	assert(FREED == freed_item);

	fd_locks_destroy(fd_locks);

	printf("test_fd_lock_destroy...OK\n");
}

size_t FAIL_CALLOC = 0;
void *__real_calloc(size_t nmemb, size_t size);
void *__wrap_calloc(size_t nmemb, size_t size) {
	if (FAIL_CALLOC && size == FAIL_CALLOC) {
		return NULL;
	}

	return __real_calloc(nmemb, size);
}

void test_fd_locks_init_fail_alloc1() {
	FAIL_CALLOC = sizeof(fd_locks_t);

	fd_locks_t *fd_locks = fd_locks_init();

	assert(fd_locks == (fd_locks_t *) NULL);
	assert(errno = ENOMEM);

	FAIL_CALLOC = 0;
	printf("test_fd_locks_init_fail_alloc1...OK\n");
}

void test_fd_locks_init_fail_alloc2() {
#ifdef MAX_FDLOCKS_PREALLOC
	FAIL_CALLOC = sizeof(fd_lock_t) * MAX_FDLOCKS_PREALLOC;
#else
	FAIL_CALLOC = 0;
#endif

	fd_locks_t *fd_locks = fd_locks_init();
	assert(!FAIL_CALLOC || fd_locks == (fd_locks_t *) NULL);
	assert(!FAIL_CALLOC || errno == ENOMEM);

	FAIL_CALLOC = 0;
	printf("test_fd_locks_init_fail_alloc2...OK\n");
}


int main() {
	printf("\ntests...\n");
	test_fd_locks_init();
	test_fd_locks_init_fail_alloc1();
	test_fd_locks_init_fail_alloc2();
	test_fd_locks_destroy();
	test_fd_lock_create_low();
	test_fd_lock_create_high();
	test_fd_lock_destroy();

	/* some time measures */
	printf("\ntime checks...\n");
	time_fd_locks_prealloc();
	time_fd_locks_dynamic(2);
	time_fd_locks_dynamic(16);
	time_fd_locks_dynamic(32);
	time_fd_locks_dynamic(64);
	time_fd_locks_dynamic(128);
	time_fd_locks_dynamic(256);
	time_fd_locks_dynamic(512);
	time_fd_locks_dynamic(1024);

	printf("\ndone\n");
}

