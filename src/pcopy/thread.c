/****************************************************************************\
*                                 _____                                      *
*                           ___  / ___/__  ___  __ __                        *
*                          / _ \/ /__/ _ \/ _ \/ // /                        *
*                         / .__/\___/\___/ .__/\_, /                         *
*                        /_/            /_/   /___/                          *
*  ------------------------------------------------------------------------  *
*  This file is a part of pCopy                                              *
*                                                                            *
*  pCopy is free software; you can redistribute it and/or                    *
*  modify it under the terms of the GNU General Public License               *
*  as published by the Free Software Foundation; either version 3            *
*  of the License, or (at your option) any later version.                    *
*                                                                            *
*  This program is distributed in the hope that it will be useful,           *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of            *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
*  GNU General Public License for more details.                              *
*                                                                            *
*  You should have received a copy of the GNU General Public License         *
*  along with this program; if not, write to the Free Software               *
*  Foundation, Inc., 51 Franklin Street, Fifth Floor,                        *
*  Boston, MA  02110-1301, USA.                                              *
*                                                                            *
*  You should have received a copy of the GNU General Public License         *
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.     *
*                                                                            *
*  ------------------------------------------------------------------------  *
*  Copyright (C) 2015, Guillaume Clercin <clercin.guillaume@gmail.com>       *
\****************************************************************************/

#define _GNU_SOURCE
// open
#include <fcntl.h>
// gettext
#include <libintl.h>
// pthread_attr_destroy, pthread_attr_init, pthread_attr_setdetachstate
// pthread_cond_init, pthread_cond_signal, pthread_cond_timedwait
// pthread_create, pthread_join, pthread_mutex_init, pthread_mutex_lock
// pthread_mutex_unlock
#include <pthread.h>
// free, malloc, realloc
#include <stdlib.h>
// asprintf
#include <stdio.h>
// strdup
#include <string.h>
// open
#include <sys/stat.h>
// syscall
#include <sys/syscall.h>
// gettimeofday, setpriority
#include <sys/time.h>
// pid_t, open
#include <sys/types.h>
// close, getpid, syscall, write
#include <unistd.h>

#include "log.h"
#include "thread.h"
#include "util.h"


struct thread_pool_thread {
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t wait;

	char * name;
	thread_pool_f function;
	void * arg;

	volatile enum {
		thread_pool_state_exited,
		thread_pool_state_running,
		thread_pool_state_waiting,
	} state;
};

static struct thread_pool_thread ** thread_pool_threads = NULL;
static unsigned int thread_pool_nb_threads = 0;
static pthread_mutex_t thread_pool_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static pid_t thread_pool_pid = -1;

static void thread_pool_exit(void) __attribute__((destructor));
static void thread_pool_init(void) __attribute__((constructor));
static void thread_pool_set_name(pid_t tid, const char * name);
static void * thread_pool_work(void * arg);


static void thread_pool_exit() {
	unsigned int i;
	for (i = 0; i < thread_pool_nb_threads; i++) {
		struct thread_pool_thread * th = thread_pool_threads[i];

		pthread_mutex_lock(&th->lock);
		if (th->state == thread_pool_state_waiting)
			pthread_cond_signal(&th->wait);
		pthread_mutex_unlock(&th->lock);

		pthread_join(th->thread, NULL);

		free(th);
	}

	free(thread_pool_threads);
	thread_pool_threads = NULL;
	thread_pool_nb_threads = 0;
}

static void thread_pool_init() {
	thread_pool_pid = getpid();
}

int thread_pool_run(const char * thread_name, void (*function)(void * arg), void * arg) {
	pthread_mutex_lock(&thread_pool_lock);
	unsigned int i;
	for (i = 0; i < thread_pool_nb_threads; i++) {
		struct thread_pool_thread * th = thread_pool_threads[i];

		if (th->state == thread_pool_state_waiting) {
			pthread_mutex_lock(&th->lock);

			th->name = NULL;
			if (thread_name != NULL)
				th->name = strdup(thread_name);
			th->function = function;
			th->arg = arg;
			th->state = thread_pool_state_running;

			pthread_cond_signal(&th->wait);
			pthread_mutex_unlock(&th->lock);

			pthread_mutex_unlock(&thread_pool_lock);

			return 0;
		}
	}

	for (i = 0; i < thread_pool_nb_threads; i++) {
		struct thread_pool_thread * th = thread_pool_threads[i];

		if (th->state == thread_pool_state_exited) {
			th->name = NULL;
			if (thread_name != NULL)
				th->name = strdup(thread_name);
			th->function = function;
			th->arg = arg;
			th->state = thread_pool_state_running;

			pthread_attr_t attr;
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

			pthread_create(&th->thread, &attr, thread_pool_work, th);

			pthread_mutex_unlock(&thread_pool_lock);

			pthread_attr_destroy(&attr);

			return 0;
		}
	}

	void * new_addr = realloc(thread_pool_threads, (thread_pool_nb_threads + 1) * sizeof(struct so_thread_pool_thread *));
	if (new_addr == NULL) {
		log_write(gettext("thread_pool_run: error, not enought memory to start new thread"));
		return 1;
	}

	thread_pool_threads = new_addr;
	struct thread_pool_thread * th = thread_pool_threads[thread_pool_nb_threads] = malloc(sizeof(struct thread_pool_thread));
	thread_pool_nb_threads++;

	th->name = NULL;
	if (thread_name != NULL)
		th->name = strdup(thread_name);
	th->function = function;
	th->arg = arg;
	th->state = thread_pool_state_running;

	pthread_mutex_init(&th->lock, NULL);
	pthread_cond_init(&th->wait, NULL);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	pthread_create(&th->thread, &attr, thread_pool_work, th);

	pthread_mutex_unlock(&thread_pool_lock);

	pthread_attr_destroy(&attr);

	return 0;
}

static void thread_pool_set_name(pid_t tid, const char * name) {
	char * path;
	asprintf(&path, "/proc/%d/task/%d/comm", thread_pool_pid, tid);

	int fd = open(path, O_WRONLY);
	if (fd < 0) {
		free(path);
		return;
	}

	char * th_name = strdup(name);
	util_string_middle_elipsis(th_name, 15);

	write(fd, th_name, strlen(th_name) + 1);
	close(fd);
	free(path);
	free(th_name);
}

static void * thread_pool_work(void * arg) {
	struct thread_pool_thread * th = arg;

	pid_t tid = syscall(SYS_gettid);

	do {
		if (th->name != NULL)
			thread_pool_set_name(tid, th->name);
		else {
			char buffer[16];
			snprintf(buffer, 16, "thread %p", th->function);
			thread_pool_set_name(tid, buffer);
		}

		th->function(th->arg);

		thread_pool_set_name(tid, "idle");

		free(th->name);
		th->name = NULL;

		pthread_mutex_lock(&th->lock);

		th->function = NULL;
		th->arg = NULL;
		th->state = thread_pool_state_waiting;

		struct timeval now;
		struct timespec timeout;
		gettimeofday(&now, NULL);
		timeout.tv_sec = now.tv_sec + 300;
		timeout.tv_nsec = now.tv_usec * 1000;

		pthread_cond_timedwait(&th->wait, &th->lock, &timeout);

		if (th->state != thread_pool_state_running)
			th->state = thread_pool_state_exited;

		pthread_mutex_unlock(&th->lock);
	} while (th->state == thread_pool_state_running);

	return NULL;
}

