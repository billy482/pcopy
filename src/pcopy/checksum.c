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

// open
#include <fcntl.h>
// dprintf
#include <stdio.h>
// memmove, strchr, strdup
#include <string.h>
// open
#include <sys/stat.h>
// lseek, open
#include <sys/types.h>
// lseek
#include <unistd.h>

#include "checksum.h"

static int checksum_fd = -1;


void checksum_add(const char * digest, const char * path) {
	dprintf(checksum_fd, "%s  %s\n", digest, path);
}

void checksum_create(const char * filename) {
	checksum_fd = open(filename, O_RDWR | O_TRUNC | O_CREAT, 0644);
}

bool checksum_parse(char ** digest, char ** path) {
	static char buffer[16384];
	static ssize_t nb_buffer_used = 0;

	char * end = NULL;
	if (nb_buffer_used > 0)
		end = strchr(buffer, '\n');

	for (;;) {
		if (end != NULL) {
			char * space = strchr(buffer, ' ');
			if (space == NULL)
				return false;

			*space = '\0';
			*digest = strdup(buffer);

			space += 2;
			*end = '\0';
			*path = strdup(space);

			end++;
			nb_buffer_used -= end - buffer;
			memmove(buffer, end, nb_buffer_used);

			return true;
		}

		ssize_t nb_read = read(checksum_fd, buffer + nb_buffer_used, 16384 - nb_buffer_used);
		if (nb_read < 0)
			return false;

		if (nb_read == 0)
			end = buffer + nb_buffer_used;
		else
			nb_buffer_used += nb_read;
	}

	return false;
}

void checksum_rewind() {
	lseek(checksum_fd, 0, SEEK_SET);
}

