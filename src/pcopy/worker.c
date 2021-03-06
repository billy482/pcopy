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
// alphasort, dirent
#include <dirent.h>
// mknod, open
#include <fcntl.h>
// gettext
#include <libintl.h>
// pthread_mutex_lock, pthread_mutex_unlock
#include <pthread.h>
// sem_init, sem_post, sem_wait
#include <semaphore.h>
// asprintf
#include <stdio.h>
// calloc, free
#include <stdlib.h>
// strcmp, strdup, strlen, strrchr
#include <string.h>
// fstat, chmod, lstat, mkdir, mkfifo, mknod, open
#include <sys/stat.h>
// fstat, lseek, lstat, mkdir, mkfifo, mknod, open
#include <sys/types.h>
// access, chown, fchown, fstat, lseek, lstat, mknod, readlink, symlink
#include <unistd.h>

#include "checksum.h"
#include "log.h"
#include "option.h"
#include "thread.h"
#include "util.h"
#include "worker.h"

#include "pcopy.version"

static char ** worker_inputs = NULL;
static unsigned int worker_nb_inputs = 0;
static const char * worker_output = NULL;
static size_t worker_output_length = 0;

static bool worker_running = false;
static sem_t worker_jobs;
static unsigned long worker_n_jobs = 0;

static struct worker * workers = NULL;
static unsigned int worker_nb_workers = 0;

static pthread_mutex_t worker_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static void worker_process_checksum(void * arg);
static void worker_process_copy(void * arg);
static void worker_process_do(void * arg);
static int worker_process_do2(const char * partial_path, const char * full_path, const struct pcopy_option * option);


bool worker_finished() {
	return !worker_running;
}

struct worker * worker_get(unsigned int * nb_working_workers, unsigned int * nb_total_workers) {
	pthread_mutex_lock(&worker_lock);

	unsigned int i, nb_working = 0;
	for (i = 0; i < worker_nb_workers; i++)
		if (workers[i].status == worker_status_running)
			nb_working++;

	if (nb_working_workers != NULL)
		*nb_working_workers = nb_working;
	if (nb_total_workers != NULL)
		*nb_total_workers = worker_nb_workers;

	return workers;
}

void worker_process(char * inputs[], unsigned int nb_inputs, const char * output, struct pcopy_option * option) {
	worker_inputs = inputs;
	worker_nb_inputs = nb_inputs;
	worker_output = output;
	worker_output_length = strlen(worker_output);

	thread_pool_run("main worker", worker_process_do, option);
}

static void worker_process_checksum(void * arg) {
	struct worker * worker = arg;

	struct checksum_driver * chck_dr = checksum_get_default();

	log_write(gettext("#%lu # recompute %s of '%s'"), worker->job, chck_dr->name, worker->src_file);

	int fd_in = open(worker->src_file, O_RDONLY);
	if (fd_in < 0) {
		log_write(gettext("#%lu ! error fatal, failed to open '%s' for reading because %m"), worker->job, worker->src_file);
		goto checksum_finished;
	}

	struct stat info;
	if (fstat(fd_in, &info) != 0) {
		log_write(gettext("#%lu ! error fatal, failed to get information of '%s' because %m"), worker->job, worker->src_file);
		close(fd_in);
		goto checksum_finished;
	}

	struct checksum * chck = chck_dr->new_checksum();

	char buffer[16384];
	ssize_t nb_read, nb_total_read = 0;
	while (nb_read = read(fd_in, buffer, 16384), nb_read > 0) {
		chck->ops->update(chck, buffer, nb_read);

		nb_total_read += nb_read;

		float done = nb_total_read;
		worker->pct = done / info.st_size;

		util_check_load_average(worker, worker->option->load_average);
	}

	if (nb_read < 0)
		log_write(gettext("#%lu ! error fatal, error while reading from '%s' because %m"), worker->job, worker->src_file);

	close(fd_in);

	char * computed = chck->ops->digest(chck);
	chck->ops->free(chck);

	if (strcmp(computed, worker->digest) == 0)
		log_write(gettext("#%lu = digests match (digest: %s) '%s'"), worker->job, computed, worker->src_file);
	else
		log_write(gettext("#%lu ≠ digests mismatch between 'src file'[%s] and '%s'[%s]"), worker->job, worker->digest, worker->src_file, computed);

	free(computed);

checksum_finished:
	worker->status = worker_status_finished;

	pthread_mutex_lock(&worker_lock);
	pthread_mutex_unlock(&worker_lock);
	sem_post(&worker_jobs);
}

static void worker_process_copy(void * arg) {
	struct worker * worker = arg;

	log_write(gettext("#%lu @ copy regular file from '%s' to '%s'"), worker->job, worker->src_file, worker->dest_file);

	struct checksum_driver * chck_dr = checksum_get_default();
	bool differ_checksum = checksum_has_checksum_file();

	int fd_in = open(worker->src_file, O_RDONLY);
	if (fd_in < 0) {
		log_write(gettext("#%lu ! error fatal, failed to open '%s' for reading because %m"), worker->job, worker->src_file);
		goto copy_finished;
	}

	struct stat info;
	if (fstat(fd_in, &info) != 0) {
		log_write(gettext("#%lu ! error fatal, failed to get information of '%s' because %m"), worker->job, worker->src_file);
		close(fd_in);
		goto copy_finished;
	}

	int fd_out = open(worker->dest_file, O_RDWR | O_CREAT | O_TRUNC, info.st_mode);
	if (fd_out < 0) {
		log_write(gettext("#%lu ! error fatal, failed to open '%s' for writing because %m"), worker->job, worker->dest_file);
		close(fd_in);
		goto copy_finished;
	}

	if (fchown(fd_out, info.st_uid, info.st_gid) != 0)
		log_write(gettext("#%lu ! warning, failed to change owner and group of '%s' because %m"), worker->job, worker->dest_file);

	struct checksum * chck = chck_dr->new_checksum();

	char buffer[16384];
	ssize_t nb_read, nb_total_read = 0;
	while (nb_read = read(fd_in, buffer, 16384), nb_read > 0) {
		ssize_t nb_write = write(fd_out, buffer, nb_read);
		if (nb_write < 0) {
			log_write(gettext("#%lu ! error fatal, error while writing to '%s' because %m"), worker->job, worker->dest_file);
			close(fd_in);
			close(fd_out);
			goto copy_finished;
		}

		nb_total_read += nb_read;

		float done = nb_total_read;
		if (!differ_checksum)
			done /= 2;
		worker->pct = done / info.st_size;

		chck->ops->update(chck, buffer, nb_read);

		util_check_load_average(worker, worker->option->load_average);
	}

	char * computed = chck->ops->digest(chck);
	log_write(gettext("#%lu # %s's sum of '%s' is %s"), worker->job, chck_dr->name, worker->src_file, computed);
	chck->ops->free(chck);

	if (differ_checksum)
		checksum_add(computed, worker->dest_file);

	if (nb_read < 0) {
		log_write(gettext("#%lu ! error fatal, error while reading from '%s' because %m"), worker->job, worker->src_file);
		close(fd_in);
		close(fd_out);
		goto copy_finished;
	}

	pthread_mutex_lock(&worker_lock);

	char * old_description = worker->description;
	int size = asprintf(&worker->description, gettext("flushing data of '%s'"), worker->dest_file);
	if (size > 0)
		free(old_description);
	else
		worker->description = old_description;

	pthread_mutex_unlock(&worker_lock);

	log_write(gettext("#%lu > flushing file '%s'"), worker->job, worker->dest_file);
	if (fsync(fd_out) != 0) {
		log_write(gettext("#%lu ! error while flushing file from '%s' because %m"), worker->job, worker->dest_file);
		close(fd_in);
		close(fd_out);
		goto copy_finished;
	}

	close(fd_in);

	if (differ_checksum) {
		close(fd_out);
		goto copy_finished;
	}

	off_t begin = lseek(fd_out, 0, SEEK_SET);
	if (begin == (off_t) -1) {
		log_write(gettext("#%lu ! error while repositioning file '%s' at it beginning"), worker->job, worker->dest_file);
		close(fd_out);
		goto copy_finished;
	}

	chck = chck_dr->new_checksum();
	nb_total_read = 0;

	while (nb_read = read(fd_out, buffer, 16384), nb_read > 0) {
		chck->ops->update(chck, buffer, nb_read);

		nb_total_read += nb_read;

		float done = nb_total_read;
		done /= 2;
		worker->pct = 0.5 + done / info.st_size;

		util_check_load_average(worker, worker->option->load_average);
	}

	if (nb_read < 0)
		log_write(gettext("#%lu ! warning, error while reading from '%s' because %m"), worker->job, worker->dest_file);

	close(fd_out);

	char * recomputed = chck->ops->digest(chck);
	chck->ops->free(chck);

	if (strcmp(computed, recomputed) == 0) {
		log_write(gettext("#%lu = digests match (digest: %s) '%s'"), worker->job, computed, worker->src_file);

		free(computed);
		free(recomputed);
	} else {
		log_write(gettext("#%lu ≠ digests mismatch between '%s'[%s] and '%s'[%s]"), worker->job, worker->src_file, computed, worker->dest_file, recomputed);

		free(computed);
		free(recomputed);
	}

copy_finished:
	worker->status = worker_status_finished;

	pthread_mutex_lock(&worker_lock);
	pthread_mutex_unlock(&worker_lock);
	sem_post(&worker_jobs);
}

static void worker_process_do(void * arg) {
	const struct pcopy_option * option = arg;

	int nb_cpus = option->nb_jobs > 0 ? option->nb_jobs : util_nb_cpus();
	sem_init(&worker_jobs, 0, nb_cpus);

	worker_running = true;

	log_write(gettext("Start pCopy %s (build: %s %s)"), PCOPY_VERSION, __DATE__, __TIME__);

	workers = calloc(nb_cpus, sizeof(struct worker));
	worker_nb_workers = nb_cpus;

	unsigned int i;
	int failed = 0;
	for (i = 0; i < worker_nb_inputs && failed == 0; i++) {
		const char * inputs = worker_inputs[i];
		char * src_input = strrchr(inputs, '/');
		failed = worker_process_do2(src_input, inputs, option);
	}

	int free_job = 0;
	while (nb_cpus != free_job) {
		sleep(1);
		sem_getvalue(&worker_jobs, &free_job);
	}

	if (checksum_has_checksum_file()) {
		checksum_rewind();

		char * digest = NULL, * filename = NULL;
		while (checksum_parse(&digest, &filename)) {
			unsigned long i_job = ++worker_n_jobs;

			sem_wait(&worker_jobs);
			pthread_mutex_lock(&worker_lock);

			struct worker * worker = NULL;
			unsigned int i;
			for (i = 0; i < worker_nb_workers && worker == NULL; i++)
				if (workers[i].status == worker_status_init)
					worker = workers + i;

			for (i = 0; i < worker_nb_workers && worker == NULL; i++)
				if (workers[i].status == worker_status_finished) {
					worker = workers + i;

					free(worker->src_file);
					free(worker->dest_file);
					free(worker->digest);
					free(worker->description);
					worker->src_file = worker->dest_file = worker->digest = NULL;
					worker->description = NULL;
				}

			struct checksum_driver * chck_dr = checksum_get_default();

			worker->job = i_job;
			worker->status = worker_status_running;
			worker->src_file = filename;
			worker->dest_file = NULL;
			worker->digest = digest;
			int size = asprintf(&worker->description, gettext("recompute %s of '%s'"), chck_dr->name, filename);
			worker->pct = 0;
			worker->option = option;

			if (size < 0)
				break;

			pthread_mutex_unlock(&worker_lock);

			char * name;
			size = asprintf(&name, "worker #%lu", i_job);

			if (size < 0)
				break;

			int error = thread_pool_run(name, worker_process_checksum, worker);

			if (error != 0)
				log_write(gettext("#%lu ! error, failed to create new thread"), i_job);

			free(name);
		}

		free_job = 0;
		while (nb_cpus != free_job) {
			sleep(1);
			sem_getvalue(&worker_jobs, &free_job);
		}
	}

	log_write(gettext("Process finished"));

	worker_running = false;
}

static int worker_process_do2(const char * partial_path, const char * full_path, const struct pcopy_option * option) {
	unsigned long i_job = ++worker_n_jobs;

	struct stat info;
	int error = lstat(full_path, &info), warning = 0;
	if (error != 0) {
		log_write(gettext("#%lu ! error fatal, failed to get information of '%s' because %m"), i_job, full_path);
		return 1;
	}

	char * output = NULL;
	int size = asprintf(&output, "%s%s", worker_output, partial_path);
	if (size < 0)
		return -2;

	if (S_ISBLK(info.st_mode)) {
		log_write(gettext("#%lu ~ create block device '%s', major: %d, minor: %d"), i_job, output, (int) info.st_rdev >> 8, (int) info.st_rdev & 0xFF);

		error = mknod(output, info.st_mode, info.st_rdev);
		if (error != 0)
			log_write(gettext("#%lu ! error, failed to create block device '%s' because %m"), i_job, output);
		else {
			warning = chown(output, info.st_uid, info.st_gid);
			if (warning != 0)
				log_write(gettext("#%lu ! warning, failed to change owner and group of '%s' because %m"), i_job, output);
		}
	} else if (S_ISCHR(info.st_mode)) {
		log_write(gettext("#%lu ~ create character device '%s', major: %d, minor: %d"), i_job, output, (int) info.st_rdev >> 8, (int) info.st_rdev & 0xFF);

		error = mknod(output, info.st_mode, info.st_rdev);
		if (error != 0)
			log_write(gettext("#%lu ! error, failed to create character device '%s' because %m"), i_job, output);
		else {
			warning = chown(output, info.st_uid, info.st_gid);
			if (warning != 0)
				log_write(gettext("#%lu ! warning, failed to change owner and group of '%s' because %m"), i_job, output);
		}
	} else if (S_ISDIR(info.st_mode)) {
		if (access(output, F_OK) != 0) {
			log_write(gettext("#%lu ~ create directory '%s'"), i_job, output);

			error = mkdir(output, info.st_mode);
			if (error != 0)
				log_write(gettext("#%lu ! error, failed to create directory '%s' because %m"), i_job, output);
			else {
				warning = chown(output, info.st_uid, info.st_gid);
				if (warning != 0)
					log_write(gettext("#%lu ! warning, failed to change owner and group of '%s' because %m"), i_job, output);
			}
		}

		if (error == 0) {
			struct dirent ** nl = NULL;
			int nb_files = scandir(full_path, &nl, util_basic_filter, alphasort);
			if (nb_files < 0) {
				error = -1;
				log_write(gettext("#%lu ! error, failed to list files from '%s' because %m"), i_job, output);
			} else {
				int length = strlen(full_path);
				char * last = strrchr(full_path, '/');
				if (last != NULL && last[1] == '\0') {
					while (length > 0 && *last == '/') {
						length--;
						last--;
					}
				}

				int i;
				for (i = 0; i < nb_files; i++) {
					if (error == 0) {
						char * sub_file;
						int size = asprintf(&sub_file, "%*s/%s", length, full_path, nl[i]->d_name);

						if (size >= 0)
							error = worker_process_do2(sub_file + (partial_path - full_path), sub_file, option);
						else
							error = 2;
					}

					free(nl[i]);
				}
				free(nl);
			}
		}
	} else if (S_ISFIFO(info.st_mode)) {
		log_write(gettext("#%lu ~ create fifo '%s'"), i_job, output);

		error = mkfifo(output, info.st_mode);
		if (error != 0)
			log_write(gettext("#%lu ! error, failed to create fifo '%s' because %m"), i_job, output);
		else {
			warning = chown(output, info.st_uid, info.st_gid);
			if (warning != 0)
				log_write(gettext("#%lu ! warning, failed to change owner and group of '%s' because %m"), i_job, output);
		}
	} else if (S_ISLNK(info.st_mode)) {
		char link[256];
		ssize_t nb_read = readlink(full_path, link, 256);
		if (nb_read < 0) {
			error = 1;
			log_write(gettext("#%lu ! error, failed to reading symbolic link of '%s' because %m"), i_job, output);
		} else
			link[nb_read] = '\0';

		/**
		 * TODO: fix link if link targets into src_path
		 */

		if (error == 0) {
			log_write(gettext("#%lu ~ create symbolic link '%s' => '%s'"), i_job, output, link);
			error = symlink(link, output);
			if (error != 0)
				log_write(gettext("#%lu ! error, failed to create symbolic link '%s' because %m"), i_job, output);
		}

		if (error == 0) {
			warning = chmod(output, info.st_mode);
			if (warning != 0)
				log_write(gettext("#%lu ! warning, failed to change permission of '%s' because %m"), i_job, output);

			warning = chown(output, info.st_uid, info.st_gid);
			if (warning != 0)
				log_write(gettext("#%lu ! warning, failed to change owner and group of '%s' because %m"), i_job, output);
		}
	} else if (S_ISREG(info.st_mode)) {
		sem_wait(&worker_jobs);
		pthread_mutex_lock(&worker_lock);

		struct worker * worker = NULL;
		unsigned int i;
		for (i = 0; i < worker_nb_workers && worker == NULL; i++)
			if (workers[i].status == worker_status_init)
				worker = workers + i;

		for (i = 0; i < worker_nb_workers && worker == NULL; i++)
			if (workers[i].status == worker_status_finished) {
				worker = workers + i;

				free(worker->src_file);
				free(worker->dest_file);
				free(worker->description);
				worker->src_file = worker->dest_file = NULL;
				worker->description = NULL;
			}

		worker->job = i_job;
		worker->status = worker_status_running;
		worker->src_file = strdup(full_path);
		worker->dest_file = strdup(output);
		worker->digest = NULL;
		int size = asprintf(&worker->description, gettext("copy from '%s' to '%s'"), full_path, output);
		worker->pct = 0;
		worker->option = option;

		pthread_mutex_unlock(&worker_lock);

		if (size < 0)
			return -2;

		char * name;
		size = asprintf(&name, "worker #%lu", i_job);

		if (size < 0)
			return -2;

		error = thread_pool_run(name, worker_process_copy, worker);

		if (error != 0)
			log_write(gettext("#%lu ! error, failed to create new thread"), i_job);

		free(name);
	}

	free(output);

	return error;
}

void worker_release() {
	pthread_mutex_unlock(&worker_lock);
}

