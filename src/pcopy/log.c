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
// pthread_mutex_lock, pthread_mutex_unlock
#include <pthread.h>
// open
#include <fcntl.h>
// va_end, va_start
#include <stdarg.h>
// asprintf, dprintf, vasprintf
#include <stdio.h>
// free, malloc
#include <stdlib.h>
// open
#include <sys/stat.h>
// gettimeofday
#include <sys/time.h>
// open
#include <sys/types.h>
// localtime_r, strftime
#include <time.h>

#include "log.h"

static int log_fd = -1;
static pthread_mutex_t log_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static struct log * log_first = NULL, * log_last = NULL;
static unsigned int log_nb_messages = 0;
static unsigned int log_nb_reserved_messages = 16;


struct log * log_get(unsigned int * nb_messages) {
	pthread_mutex_lock(&log_lock);
	if (nb_messages != NULL)
		*nb_messages = log_nb_messages;
	return log_first;
}

void log_open_log_file(const char * filename) {
	log_fd = open(filename, O_RDWR | O_APPEND | O_CREAT, 0644);
}

void log_release() {
	pthread_mutex_unlock(&log_lock);
}

void log_reserve_message(unsigned int nb_messages) {
	pthread_mutex_lock(&log_lock);

	while (nb_messages < log_nb_messages) {
		free(log_first->message);
		struct log * old = log_first;
		log_first = old->next;
		free(old);

		log_nb_messages--;
	}

	if (log_first == NULL)
		log_last = NULL;

	log_nb_reserved_messages = nb_messages;

	pthread_mutex_unlock(&log_lock);
}

void log_write(const char * format, ...) {
	char * message;

	va_list va;
	va_start(va, format);
	int size = vasprintf(&message, format, va);
	va_end(va);

	if (size < 0)
		return;

	struct timeval now;
	gettimeofday(&now, NULL);
	struct tm tm;
	localtime_r(&now.tv_sec, &tm);
	char buffer[64];
	strftime(buffer, 64, "%c", &tm);

	pthread_mutex_lock(&log_lock);

	if (log_fd > -1)
		dprintf(log_fd, "[%s] %s\n", buffer, message);

	struct log * log;
	if (log_nb_messages == log_nb_reserved_messages) {
		log = log_first;
		log_first = log->next;
	} else {
		log = malloc(sizeof(struct log));
		log_nb_messages++;
	}

	size = asprintf(&log->message, "[%s] %s", buffer, message);

	if (size >= 0) {
		log->next = NULL;
		if (log_first == NULL)
			log_first = log_last = log;
		else
			log_last = log_last->next = log;
	}

	pthread_mutex_unlock(&log_lock);

	free(message);
}

