/**
 *   YALAS is a Linux audit system based on SystemTap
 *   Copyright (C) <2017>  Arkady Miasnikov
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * Functions and code chunks which do not fit anywhere else and loosely related to the Linux API
 */

#pragma once

#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


#if  !__KERNEL__
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;
#endif


/**
 * !!(X) forces the value of X to be 0 OR 1 and nothing else without any
 * performance hit
 */
#ifndef likely
# define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
# define unlikely(x) __builtin_expect(!!(x), 0)
#endif

enum linux_log_type {
	LINUX_LOG_INFO_EXT,
	LINUX_LOG_INFO,
	LINUX_LOG_WARNING,
	LINUX_LOG_ERROR,
	LINUX_LOG_LAST,
};

extern int linux_fork(void);
extern void linux_redirect_stdio(void);
extern void linux_log(enum linux_log_type log_flags, const char *fmt, ...);
extern void linux_log_use_syselog(void);

#define LINUX_LOG_LINE() linux_log(LINUX_LOG_ERROR, "%d", __LINE__)

typedef int (*linux_task_t)(void*);

typedef struct {
	/**
	 * Name of the task
	 */
	const char *name;

	/**
	 * Original task pointer
	 */
	linux_task_t task;

	/**
	 * Argument to forward to the task
	 */
	void * task_arg;
} linux_task_properties_t;

typedef struct {
	/**
	 * Set to non-zero to force exit
	 */
	volatile int exit_flag;

	/**
	 * reference for created pthread
	 */
	pthread_t thread;
} linux_task_runtime_state_t;

typedef struct {
	/**
	 * This field is set by application
	 */
	linux_task_properties_t properties;

	/**
	 * Used by task manager
	 */
	linux_task_runtime_state_t runtime;
} linux_task_state_t;

/**
 * Call task in loop forever or until return value is not 1
 * @return 1 if Ok
 */
extern int linux_thread_start(linux_task_state_t *state);

/**
 * Start all tasks in the list
 * End of list is name = NULL
 * @return 1 if Ok
 */
extern int linux_thread_start_all(linux_task_state_t *state);

/**
 * Wait for task termination
 * @return 1 if Ok
 */
extern int linux_thread_join(const linux_task_state_t *state);

extern int linux_thread_yield(void);

/**
 * Wait for all tasks to complete
 * End of list is name = NULL
 * @return 1 if Ok
 */
extern int linux_thread_join_all(linux_task_state_t *state);

/**
 * Break the task loop
 * @return 1 if Ok
 */
extern int linux_thread_exit(linux_task_state_t *state);

/**
 * Stop all tasks in the list
 */
extern int linux_thread_exit_all(linux_task_state_t *state);

/**
 * Break the task loop
 */
extern int linux_thread_force_exit(linux_task_state_t *state);

static inline uint32_t linux_inc_index(uint32_t index, uint32_t max_index) {
	(index)++;
	if (index > max_index) {
		index = 0;
	}
	return index;
}

extern void linux_ms_sleep(unsigned long ms);
extern void linux_micro_sleep(unsigned long micro);

typedef struct {
	const char *name;
	pthread_mutex_t fd_mutex;
	pthread_mutexattr_t fd_mutex_attr;
} linux_mutex_t;

int linux_mutex_init_all(linux_mutex_t *mutex);
int linux_mutex_init(linux_mutex_t *mutex);

/**
 * There is a potential to implement something similar to "Benaphore"
 * and save CPU cycles in the non-congested systems in some Unix systems
 * See http://www.mr-edd.co.uk/blog/sad_state_of_osx_pthread_mutex_t
 */
static inline int linux_mutex_lock(linux_mutex_t *mutex) {
	int err;

	err = pthread_mutex_lock(&mutex->fd_mutex);
	if (err != 0) {
		linux_log(LINUX_LOG_ERROR, "Failed to lock mutex %s: %s %d",
				mutex->name, strerror(errno), errno);
		return 0;
	}

	return 1;
}

static inline int linux_mutex_unlock(linux_mutex_t *mutex) {
	int err;

	err = pthread_mutex_unlock(&mutex->fd_mutex);

	if (err != 0) {
		linux_log(LINUX_LOG_ERROR, "Failed to unlock mutex %s: %s %d",
				mutex->name, strerror(errno), errno);
		return 0;
	}

	return 1;
}

typedef struct {
	const char *name;
	sem_t semaphore;
} linux_semaphore_t;

int linux_semaphore_init(linux_semaphore_t *semaphore);

static inline int linux_semaphore_wait(linux_semaphore_t *semaphore) {
	int res = sem_wait(&semaphore->semaphore);
	return (res == 0);
}

static inline int linux_semaphore_wait(linux_semaphore_t *semaphore,
		const size_t timeout) {
	if (timeout > 0) {
#if __gnu_linux__
		struct timespec abs_timeout;
		int res = clock_gettime(CLOCK_REALTIME_COARSE, &abs_timeout);
		if (res < 0)
		{
			linux_log(LINUX_LOG_ERROR, "clock_gettime failed for %s(%p), timeout=%lu, errno=%s(%d)", semaphore->name, semaphore, timeout, strerror(errno), errno);
		}
		static const uint64_t NANOS = 1000*1000*1000;
		static const uint64_t MILLIS = 1000*1000;
		uint64_t nsec = abs_timeout.tv_nsec + (uint64_t)timeout*MILLIS;
		abs_timeout.tv_nsec = nsec % NANOS;
		abs_timeout.tv_sec += nsec / NANOS;
		res = sem_timedwait(&semaphore->semaphore, &abs_timeout);
		if ((res == -1) && (errno != ETIMEDOUT))
		{
			linux_log(LINUX_LOG_ERROR, "sem_timedwait failed for %s(%p), timeout=%lu, errno=%s(%d)", semaphore->name, semaphore, timeout, strerror(errno), errno);
		}
		else
		{
			//linux_log(LINUX_LOG_INFO, "sem_timedwait for %s(%p), timeout=%lu, res=%d, errno=%s(%d)", semaphore->name, semaphore, timeout, res, strerror(errno), errno);
		}
#else   // Patch for compilation in MacOS
		// In MacOS/BSD use dispatch_semaphore_XX()
//#warning Patch for non Linux operating systems
		int res = sem_trywait(&semaphore->semaphore);
#endif
		return (res == 0);
	}
	if (timeout == 0) {
		int res = sem_trywait(&semaphore->semaphore);
		return (res == 0);
	}
	return 0;
}

static inline int linux_semaphore_post(linux_semaphore_t *semaphore) {
#if __gnu_linux__
	int res = sem_post(&semaphore->semaphore);
	if (res == -1)
	linux_log(LINUX_LOG_ERROR, "sem_timedwait failed for %s(%p), errno=%s(%d)", semaphore->name, semaphore, strerror(errno), errno);
	return (res != 0);
#else
	return 1;
#endif
}

extern int linux_file_exists(const char *path);
extern uint64_t linux_time_seconds();
extern uint64_t linux_time_ms();

class MeasureTime {
public:
	MeasureTime() {
		entry = linux_time_ms();
	}

	uint64_t getEntry() {
		return entry;
	}

	uint64_t diff() {
		return (linux_time_ms() - entry);
	}

	uint64_t current() {
		return linux_time_ms();
	}

protected:
	uint64_t entry;
};

typedef bool (*linux_scan_folder_test_t)(int index, const char *name);
typedef void (*linux_scan_folder_process_t)(int index, const char *name);
int linux_scan_folder(const char *folder, linux_scan_folder_test_t test, linux_scan_folder_process_t process);
int linux_set_priority();

