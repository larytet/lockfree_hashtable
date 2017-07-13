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

#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

//#define __USE_UNIX98    /* Needed for PTHREAD_PRIO_INHERIT */
#include <pthread.h>

#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <semaphore.h>
#include <pwd.h>
#include <grp.h>
#include <netdb.h>
#include <ifaddrs.h>

#include <syslog.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <net/if.h>
#include <termios.h> /* POSIX terminal control definitions */
#include <netinet/in.h>
#include <arpa/inet.h>

// Another tradeoff - do not use boost file system in Unix
#include <dirent.h>
#include <wordexp.h>

#include "linux_utils.h"

void linux_redirect_stdio(void) {
	int res;

	// Close stdin. stdout and stderr
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	// Point STDIN, STDOUT, STDERR to /dev/null
	int fd = open("/dev/null", O_RDWR);
	if (fd != STDIN_FILENO) {
		linux_log(LINUX_LOG_ERROR,
				"Got wrong file descriptor for STDIN: %d instead of %d ", fd,
				STDIN_FILENO);
	}

	res = dup2(STDIN_FILENO, STDOUT_FILENO);
	if (res != STDOUT_FILENO) {
		linux_log(LINUX_LOG_ERROR,
				"Got wrong file descriptor for STDOUT: %d instead of %d ", res,
				STDOUT_FILENO);
	}

	res = dup2(STDIN_FILENO, STDERR_FILENO);
	if (res != STDERR_FILENO) {
		linux_log(LINUX_LOG_ERROR,
				"Got wrong file descriptor for STDERR: %d instead of %d ", res,
				STDERR_FILENO);
	}

}

int linux_fork(void) {
	// fork this process and exit the main thread
	pid_t pid;

	pid = fork();

	if (pid == 0) // this is a child
			{

		//unmask the file mode
		umask(0);

		//set new session
		setsid();

		int res = chdir("/");
		if (res != 0) {
			linux_log(LINUX_LOG_ERROR,
					"Failed to change directory to root: %s (%d)",
					strerror(errno), errno);
		}

		linux_redirect_stdio();

		return 0;
	} else {
		exit(0);
	}
}

static int linux_log_syslog = 0;

/**
 * Force using syslog instead of stdout
 */
void linux_log_use_syselog() {
	openlog("SECDO", LOG_PID, LOG_USER);
	linux_log_syslog = 1;
}

void linux_log(enum linux_log_type log_flags, const char *fmt, ...) {
	va_list ap;
	char buffer[512];

	static const char *LINUX_LOG_PREFIX[LINUX_LOG_LAST] = { "INFO", "INFOE",
			"WARN", "ERR" };
	static const int LINUX_LOG_SYSLOG[LINUX_LOG_LAST] = { LOG_INFO, LOG_DEBUG,
			LOG_WARNING, LOG_ERR };

	va_start(ap, fmt);
	vsnprintf(&buffer[0], sizeof(buffer), fmt, ap);
	va_end(ap);

	if (linux_log_syslog) {
		syslog(LINUX_LOG_SYSLOG[log_flags], "%s", buffer);
	} else {
		printf("%s %s\r\n", LINUX_LOG_PREFIX[log_flags], buffer);
	}
}

/**
 * Implements loop forever
 */
static void *linux_thread_main(void *args) {
	linux_task_state_t *state = (linux_task_state_t*) args;
	int res;

	linux_log(LINUX_LOG_INFO_EXT, "Thread '%s' is running",
			state->properties.name);
	while (state->runtime.exit_flag == 0) {
		res = state->properties.task(state->properties.task_arg);
		if (res != 1) {
			break;
		}
	}

	return NULL;
}

/**
 * Call task in loop forever or until return value is not 1
 */
int linux_thread_start(linux_task_state_t *state) {
	int res = 0;

	do {
		// pthread_t is "unsigned long int" at least in Linux
		//thread = malloc(sizeof(pthread_t));

		memset(&state->runtime, 0, sizeof(state->runtime));
		state->runtime.exit_flag = 0;
		/* create a second thread which executes inc_x(&x) */
		res = pthread_create(&state->runtime.thread, NULL, linux_thread_main,
				state);
		// pthread_create() returns zero if Ok
		if (res) {
			linux_log(LINUX_LOG_ERROR, "Failed to create task %s: %s (%d)",
					state->properties.name, strerror(errno), errno);
			res = 0;
			break;
		}

		res = 1;
	} while (0);

	return res;
}

/**
 * Start all tasks in the list
 * End of list is name = NULL
 * @return 1 if Ok
 */
int linux_thread_start_all(linux_task_state_t *state) {
	int res = 0;

	while (state->properties.name != NULL) {
		res = linux_thread_start(state);
		if (res != 1) {
			break;
		}
		state++;
	}

	return res;
}

/**
 * Break the task loop
 */
int linux_thread_exit(linux_task_state_t *state) {
	state->runtime.exit_flag = 1;

	return 1;
}

int linux_thread_exit_all(linux_task_state_t *state) {
	int res = 0;

	while (state->properties.name != NULL) {
		res = linux_thread_exit(state);
		if (res != 1) {
			break;
		}
		state++;
	}

	return res;
}

/**
 * Wait for task termination
 */
int linux_thread_join(const linux_task_state_t *state) {
	int res;

	res = pthread_join(state->runtime.thread, NULL);
	if (res) {
		return 0;
	}

	return 1;
}

int linux_thread_yield(void) {
	sched_yield();
	return 1;
}

int linux_thread_join_all(linux_task_state_t *state) {
	int res = 0;

	while (state->properties.name != NULL) {
		res = linux_thread_join(state);
		if (res != 1) {
			break;
		}
		state++;
	}

	return res;
}

/**
 * Break the task loop
 */
int linux_thread_force_exit(linux_task_state_t *state) {
	return 0;
}

void linux_ms_sleep(unsigned long ms) {
	usleep(1000 * ms);
}

void linux_micro_sleep(unsigned long micro) {
	usleep(micro);
}

int linux_mutex_init(linux_mutex_t *mutex) {
	int res = 0, err;
	do {
		err = pthread_mutexattr_init(&mutex->fd_mutex_attr);
		if (err != 0) {
			linux_log(LINUX_LOG_ERROR,
					"Failed to initialize mutex attributes %s: %s %d",
					mutex->name, strerror(errno), errno);
			break;
		}

#if 0
		if (pthread_mutexattr_setprotocol (&fd_mutex_attr[mutex], PTHREAD_PRIO_INHERIT) != 0 )
		{
			break;
		}
#endif
		err = pthread_mutex_init(&mutex->fd_mutex, &mutex->fd_mutex_attr);
		if (err != 0) {
			linux_log(LINUX_LOG_ERROR, "Failed to initialize mutex %s: %s %d",
					mutex->name, strerror(errno), errno);
			break;
		}

		res = 1;

	} while (0);

	return res;
}

int linux_mutex_init_all(linux_mutex_t *mutex) {
	int res = 0;

	while (mutex->name != NULL) {
		res = linux_mutex_init(mutex);
		if (res != 1)
			break;
	}

	return res;
}

int linux_semaphore_init(linux_semaphore_t *semaphore) {
	int err = sem_init(&semaphore->semaphore, 0, 0);
	if (err == 0) {
		linux_log(LINUX_LOG_INFO, "Semaphore %s %p ok", semaphore->name,
				semaphore);
	} else {
		linux_log(LINUX_LOG_ERROR, "Failed to initialize semaphore %s: %s %d",
				semaphore->name, strerror(errno), errno);
	}

	return (err == 0);
}

int linux_file_exists(const char *path) {
	struct stat buffer;
	int res = stat(path, &buffer);
	return (res == 0);
}

uint64_t linux_time_seconds() {
	return time(NULL);
}

uint64_t linux_time_ms() {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &tp);

	uint64_t res = tp.tv_nsec/(1000*1000) + 1000*tp.tv_sec;
	return res;
}

int linux_scan_folder(const char *folder, linux_scan_folder_test_t test, linux_scan_folder_process_t process)
{
    int res = 0;
    DIR *dir;
    struct dirent *entry;

    dir = opendir (folder);
    if (dir != NULL)
    {
        /* print all the files and directories within directory */
        while ((entry = readdir (dir)) != NULL)
        {
            const char *filename = entry->d_name;
            bool test_res = true;
            if (test != NULL)
                test_res = test(res, filename);
            if (test_res)
            {
            	if (process != NULL)
            		process(res, filename);
                res++;
            }
        }
        closedir (dir);
    }

    return res;

}

int linux_set_priority()
{
	struct sched_param param;
	int my_pid = 0;
	int res = 0;
	int sched_res;
	do
	{
		int high_priority = sched_get_priority_max(SCHED_FIFO);
		if (high_priority < 0)
		{
			linux_log(LINUX_LOG_ERROR,
					"Failed to sched_get_priority_max(): %s (%d)",
					strerror(errno), errno);
			break;
		}

		sched_res = sched_getparam(my_pid, &param);
		if (sched_res)
		{
			linux_log(LINUX_LOG_ERROR,
					"Failed to sched_getparam(): %s (%d)",
					strerror(errno), errno);
			break;
		}

		param.sched_priority = high_priority;
		sched_res = sched_setscheduler(my_pid, SCHED_FIFO, &param);
		if (sched_res < 0)
		{
			linux_log(LINUX_LOG_ERROR,
					"Failed to sched_setscheduler(): %s (%d)",
					strerror(errno), errno);
			break;
		}

		res = 1;

	}
	while (false);


	return res;
}
