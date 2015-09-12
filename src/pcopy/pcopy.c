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

// getopt_long
#include <getopt.h>
// bindtextdomain, gettext, textdomain
#include <libintl.h>
// setlocale
#include <locale.h>
// curs_set, halfdelay, has_colors, keypad, initscr, init_pair,
// newwin, noecho, nonl, start_color
#include <ncurses.h>
// signal
#include <signal.h>
// printf, snprintf
#include <stdio.h>
// free
#include <stdlib.h>
// memset, strdup, strlen
#include <string.h>
// localtime_r, strftime, time
#include <time.h>
// exit
#include <unistd.h>

#include "checksum.h"
#include "log.h"
#include "option.h"
#include "util.h"
#include "worker.h"

#include "pcopy.version"

static WINDOW * mainScreen = NULL;
static WINDOW * headerWindow = NULL;

static unsigned int row, col;

static void display(void);
static void quit(int signal);
static void show_help(void);


static void display() {
	unsigned int nb_logs = 0;
	struct log * logs = log_get(&nb_logs);

	unsigned int nb_working_workers = 0, nb_total_workers = 0;
	struct worker * workers = worker_get(&nb_working_workers, &nb_total_workers);

	unsigned int show_nb_logs = row - nb_working_workers - 1;
	while (nb_logs > show_nb_logs) {
		logs = logs->next;
		nb_logs--;
	}

	char line[col + 1];
	memset(line, ' ', col);
	line[col] = '\0';

	unsigned int offset = show_nb_logs - nb_logs, i;
	for (i = 0; i < nb_logs; i++) {
		mvprintw(i + offset, 0, line);

		if (util_string_length(logs->message) > col) {
			char * message = strdup(logs->message);
			util_string_middle_elipsis2(message, col);
			mvprintw(i + offset, 0, "%s", message);
			free(message);
		} else
			mvprintw(i + offset, 0, "%s", logs->message);

		logs = logs->next;
	}
	log_release();

	offset = row - nb_working_workers - 1;

	size_t buffer_length = 4 * col;
	char * buffer = malloc(buffer_length + 1);

	unsigned int j;
	for (i = 0, j = 0; i < nb_working_workers; i++, j++) {
		struct worker * worker = workers + j;
		while (worker->status != worker_status_running)
			worker = workers + ++j;

		mvprintw(i + offset, 0, line);

		memset(buffer, ' ', buffer_length);
		buffer[buffer_length] = '\0';

		ssize_t nb_write;
		if (worker->paused)
			nb_write = snprintf(buffer, buffer_length, "#%lu [%3.0f%% P] : %s", worker->job, 100 * worker->pct, worker->description);
		else
			nb_write = snprintf(buffer, buffer_length, "#%lu [%3.0f%% A] : %s", worker->job, 100 * worker->pct, worker->description);

		if (util_string_length(buffer) > col)
			util_string_middle_elipsis2(buffer, col);
		else {
			buffer[nb_write] = ' ';
			buffer[util_string_length2(buffer, col)] = '\0';
		}


		int width = col * worker->pct;
		int wwidth = util_string_length2(buffer, width);

		if (worker->paused)
			attron(COLOR_PAIR(2));
		else
			attron(COLOR_PAIR(4));
		mvprintw(i + offset, 0, "%*s", wwidth, buffer);

		if (worker->paused)
			attroff(COLOR_PAIR(2));
		else
			attroff(COLOR_PAIR(4));
		mvprintw(i + offset, width, "%s", buffer + wwidth);
	}
	worker_release();

	free(buffer);

	attron(COLOR_PAIR(3));

	mvprintw(row - 1, 0, line);
	mvprintw(row - 1, 1, "pCopy " PCOPY_VERSION);

	time_t now = time(NULL);

	struct tm lnow;
	localtime_r(&now, &lnow);

	char buf[16];
	ssize_t nb_write = strftime(buf, 16, "%X", &lnow);

	mvprintw(row - 1, col - nb_write - 1, "%s", buf);

	attroff(COLOR_PAIR(3));

	refresh();
}

int main(int argc, char * argv[]) {
	setlocale(LC_ALL, "");
	bindtextdomain("pcopy", "locale/");
	textdomain("pcopy");

	static struct pcopy_option option = {
		.nb_jobs      = 0,
		.load_average = 0,
	};

	enum {
		OPT_CHECKSUM      = 'c',
		OPT_CHECKSUM_FILE = 'C',
		OPT_HELP          = 'h',
		OPT_JOB           = 'j',
		OPT_LOAD_AVERAGE  = 'l',
		OPT_LOG_FILE      = 'L',
		OPT_PAUSE         = 'p',
		OPT_VERSION       = 'V',
	};

	static struct option op[] = {
		{ "checksum",      1, 0, OPT_CHECKSUM },
		{ "checksum-file", 1, 0, OPT_CHECKSUM_FILE },
		{ "help",          0, 0, OPT_HELP },
		{ "jobs",          1, 0, OPT_JOB },
		{ "load-average",  1, 0, OPT_LOAD_AVERAGE },
		{ "pause",         0, 0, OPT_PAUSE },
		{ "version",       0, 0, OPT_VERSION },

		{ NULL, 0, 0, 0 },
	};

	bool pause = false;

	static int lo;
	for (;;) {
		int c = getopt_long(argc, argv, "c:C:h?j:l:L:pV", op, &lo);
		if (c == -1)
			break;

		switch (c) {
			case OPT_CHECKSUM:
				if (!strcmp(optarg, "help")) {
					struct checksum_driver * drivers = checksum_digests();

					printf(gettext("Available cryptographic hash functions: "));
					unsigned int i;
					for (i = 0; drivers->name != NULL; i++, drivers++) {
						if (i > 0)
							printf(", ");
						printf(gettext("'%s'"), drivers->name);
					}
					printf("\n");

					return 0;
				}

				if (!checksum_set_default(optarg)) {
					printf("Error: hash function '%s' not found\n", optarg);
					return 1;
				}
				break;

			case OPT_CHECKSUM_FILE:
				if (!checksum_create(optarg)) {
					printf(gettext("Error: failed to create checksum file '%s'\n"), optarg);
					return 1;
				}
				break;

			case OPT_HELP:
				show_help();
				return 0;

			case OPT_JOB:
				if (sscanf(optarg, "%u", &option.nb_jobs) < 1) {
					printf(gettext("Error: failed to parse argument for --jobs parameter, '%s' should be an positive integer\n"), optarg);
					return 1;
				}
				break;

			case OPT_LOAD_AVERAGE:
				if (sscanf(optarg, "%lf", &option.load_average) < 1 || option.load_average < 0.5) {
					printf(gettext("Error: failed to parse argument for --load-average parameter, '%s' should be an positive decimal greater than %.1f\n"), optarg, 0.5);
					return 1;
				}
				break;

			case OPT_LOG_FILE:
				if (!log_open_log_file(optarg)) {
					printf(gettext("Error: failed to create log file '%s'\n"), optarg);
					return 1;
				}
				break;

			case OPT_PAUSE:
				pause = true;
				break;

			case OPT_VERSION:
				printf(gettext("pCopy: parallel copying and checksumming\n"));
				printf(gettext("version: %s, build: %s %s\n"), PCOPY_VERSION, __DATE__, __TIME__);
				return 0;
		}
	}

	if (optind + 2 > argc)
		return 1;

	worker_process(&argv[optind], argc - optind - 1, argv[argc - 1], &option);

	mainScreen = initscr();
	getmaxyx(stdscr, row, col);
	keypad(stdscr, TRUE);
	curs_set(0);
	noecho();
	nonl();
	halfdelay(150);
	if (has_colors()) {
		start_color();

		init_pair(1, COLOR_WHITE, COLOR_BLUE);
		init_pair(2, COLOR_WHITE, COLOR_RED);
		init_pair(3, COLOR_WHITE, COLOR_BLUE);
		init_pair(4, COLOR_RED, COLOR_GREEN);
	}

	log_reserve_message(row);

	signal(SIGINT, quit);

	char line[col + 1];
	memset(line, ' ', col);
	line[col] = '\0';

	attron(COLOR_PAIR(3));
	mvprintw(row - 1, 1, "pCopy " PCOPY_VERSION);
	attroff(COLOR_PAIR(3));
	refresh();

	sleep(1);

	while (!worker_finished()) {
		sleep(1);
		display();
	}

	display();

	if (pause)
		getch();
	else
		sleep(5);

	endwin();

	return 0;
}

static void quit(int signal __attribute__((unused))) {
	if (headerWindow)
		delwin(headerWindow);
	headerWindow = 0;
	endwin();
	_exit(0);
}

static void show_help() {
	printf("pCopy (" PCOPY_VERSION ")\n");
	printf(gettext("Usage: pcopy [options] <src-files>... <dest-file>\n"));
	printf(gettext("  -c, --checksum <hash>      : Use <hash> as hash function,\n"));
	printf(gettext("                               Use 'help' to show available hash functions\n"));
	printf(gettext("  -C, --checksum-file <file> : Defer checksum checking after copy and write checksum into <file>\n"));
	printf(gettext("  -h, --help                 : Show this and exit\n"));
	printf(gettext("  -j, --jobs <jobs>          : Run <jobs> simultaneously, default value: number of cpus\n"));
	printf(gettext("  -l, --load-average <load>  : Do not copy while load average exceed <load> in the last minute\n"));
	printf(gettext("  -L, --log-file <file>      : Log also into <file>\n"));
	printf(gettext("  -p, --pause                : Pause at the end of copy\n\n"));
}

