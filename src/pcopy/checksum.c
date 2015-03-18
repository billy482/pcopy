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
// memmove, strcmp, strchr, strdup
#include <string.h>
// open
#include <sys/stat.h>
// lseek, open
#include <sys/types.h>
// lseek
#include <unistd.h>

#include "checksum.h"
#include "checksum/digest.h"

static int checksum_fd = -1;

static struct checksum_driver checksum_drivers[] = {
	{ "md5", checksum_md5_new_checksum },

	{ NULL, NULL },
};

static struct checksum_driver * checksum_default_driver = checksum_drivers;


void checksum_add(const char * digest, const char * path) {
	if (checksum_fd >= 0)
		dprintf(checksum_fd, "%s  %s\n", digest, path);
}

void checksum_create(const char * filename) {
	checksum_fd = open(filename, O_RDWR | O_TRUNC | O_CREAT, 0644);
}

struct checksum_driver * checksum_digests() {
	return checksum_drivers;
}

struct checksum_driver * checksum_get_default() {
	return checksum_default_driver;
}

bool checksum_has_checksum_file() {
	return checksum_fd > -1;
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

		end = strchr(buffer, '\n');
		if (end == NULL)
			break;
	}

	return false;
}

void checksum_rewind() {
	lseek(checksum_fd, 0, SEEK_SET);
}

bool checksum_set_default(const char * checksum) {
	if (checksum == NULL)
		return false;

	struct checksum_driver * driver = checksum_drivers;
	for (; driver->name != NULL; driver++)
		if (strcmp(checksum, driver->name) == 0) {
			checksum_default_driver = driver;
			return true;
		}

	return false;
}

