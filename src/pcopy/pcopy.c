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
// exit
#include <unistd.h>

#include "checksum.h"
#include "log.h"
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
			util_string_middle_elipsis(message, col);
			mvprintw(i + offset, 0, "%s", message);
			free(message);
		} else
			mvprintw(i + offset, 0, "%s", logs->message);

		logs = logs->next;
	}
	log_release();

	offset = row - nb_working_workers - 1;
	unsigned int j;
	for (i = 0, j = 0; i < nb_working_workers; i++, j++) {
		struct worker * worker = workers + j;
		while (worker->status != worker_status_running)
			worker = workers + ++j;

		mvprintw(i + offset, 0, line);

		char buffer[col + 1];
		memset(buffer, ' ', col);
		buffer[col] = '\0';
		ssize_t nb_write = snprintf(buffer, col - 1, "#%lu %.0f%%", worker->job, 100 * worker->pct);
		buffer[nb_write] = ' ';

		int width = col * worker->pct;

		attron(COLOR_PAIR(2));
		mvprintw(i + offset, 0, "%*s", width, buffer);
		attroff(COLOR_PAIR(2));
		mvprintw(i + offset, width, "%s", buffer + width);
	}
	worker_release();

	mvprintw(row - 1, 0, line);
	mvprintw(row - 1, 1, "pCopy %ux%u", row, col);

	refresh();
}

int main(int argc, char * argv[]) {
	setlocale(LC_ALL, "");
	bindtextdomain("pcopy", "/usr/share/locale/");
	textdomain("pcopy");

	enum {
		OPT_CHECKSUM = 'c',
		OPT_HELP     = 'h',
	};

	static struct option op[] = {
		{ "checksum", 1, 0, OPT_CHECKSUM },
		{ "help",     0, 0, OPT_HELP },

		{ NULL, 0, 0, 0 },
	};

	static int lo;
	for (;;) {
		int c = getopt_long(argc, argv, "c:h?", op, &lo);
		if (c == -1)
			break;

		switch (c) {
			case OPT_CHECKSUM:
				if (!strcmp(optarg, "help")) {
					struct checksum_driver * drivers = checksum_digests();

					printf(gettext("Available checksums: "));
					unsigned int i;
					for (i = 0; drivers->name != NULL; i++, drivers++) {
						if (i > 0)
							printf(", ");
						printf("'%s'", drivers->name);
					}
					printf("\n");

					return 0;
				}

				if (!checksum_set_default(optarg)) {
					printf("Error: hash function '%s' not found\n", optarg);
					return 1;
				}
				break;

			case OPT_HELP:
				show_help();
				return 0;
		}
	}

	if (optind + 2 > argc) {
		return 1;
	}

	worker_process(&argv[optind], argc - optind - 1, argv[argc - 1]);

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
		init_pair(3, COLOR_YELLOW, COLOR_BLUE);
	}

	log_reserve_message(row);

	signal(SIGINT, quit);

	mvprintw(row - 1, 1, "pCopy");
	refresh();

	for (;;) {
		sleep(2);
		display();
	}

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
	printf(gettext("  -c, --checksum <hash> : Use <hash> as hash function,\n"));
	printf(gettext("                          Use 'help' to show available hash functions\n"));
	printf(gettext("  -h, --help            : Show this and exit\n\n"));
}
