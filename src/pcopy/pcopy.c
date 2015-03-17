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
// printf
#include <stdio.h>
// exit
#include <unistd.h>

#include "pcopy.version"

static WINDOW * mainScreen = NULL;
static WINDOW * headerWindow = NULL;

static void quit(int signal);
static void show_help(void);


int main(int argc, char * argv[]) {
	setlocale(LC_ALL, "");
	bindtextdomain("pcopy", "/usr/share/locale/");
	textdomain("pcopy");

	enum {
		OPT_HELP = 'h',
	};

	static struct option op[] = {
		{ "help", 0, 0, OPT_HELP },

		{ 0, 0, 0, 0 },
	};

	static int lo;
	for (;;) {
		int c = getopt_long(argc, argv, "h?", op, &lo);
		if (c == -1)
			break;

		switch (c) {
			case OPT_HELP:
				show_help();
				return 0;
		}
	}

	mainScreen = initscr();
	keypad(stdscr, TRUE);
	curs_set(0);
	noecho();
	nonl();
	halfdelay(150);
	if (has_colors()) {
		start_color();

		init_pair(1, COLOR_WHITE, COLOR_BLUE);
		init_pair(2, COLOR_GREEN, COLOR_BLUE);
		init_pair(3, COLOR_YELLOW, COLOR_BLUE);
	}

	signal(SIGINT, quit);

	printw("foo");
	refresh();

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
	printf(gettext("  -h, --help : Show this and exit\n\n"));
}

