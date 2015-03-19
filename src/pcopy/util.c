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

// dirent
#include <dirent.h>
// open
#include <fcntl.h>
// sscanf
#include <stdio.h>
// memmove, strlen
#include <string.h>
// open
#include <sys/stat.h>
// open
#include <sys/types.h>
// close, read
#include <unistd.h>

#include "util.h"

static int util_string_valid_utf8_char(const char * string);
static int util_string_valid_utf8_char2(const unsigned char * ptr, unsigned short length);


int util_basic_filter(const struct dirent * file) {
	if (file->d_name[0] != '.')
		return 1;

	if (file->d_name[1] == '\0')
		return 0;

	return file->d_name[1] != '.' || file->d_name[2] != '\0';
}

unsigned int util_nb_cpus() {
	int fd = open("/sys/devices/system/cpu/present", O_RDONLY);
	if (fd < 0)
		return 1;

	char buffer[16];
	ssize_t nb_read = read(fd, buffer, 16);
	close(fd);

	if (nb_read < 0)
		return 1;

	buffer[nb_read] = '\0';

	unsigned int first, last;
	int nb_parsed = sscanf(buffer, "%u-%u", &first, &last);

	return nb_parsed == 2 ? last - first + 1 : 1;
}

size_t util_string_length(const char * string) {
	if (string == NULL)
		return 0;

	size_t length = 0;
	while (*string != '\0') {
		int char_length = util_string_valid_utf8_char(string);
		if (char_length == 0)
			break;

		length++;
		string += char_length;
	}

	return length;
}

size_t util_string_length2(const char * string, size_t nb_character) {
	if (string == NULL)
		return 0;

	size_t i, l;
	for (i = 0, l = 0; i < nb_character; i++) {
		int char_length = util_string_valid_utf8_char(string);
		if (char_length == 0)
			break;

		l += char_length;
		string += char_length;
	}

	return l;
}

void util_string_middle_elipsis(char * string, size_t length) {
	size_t str_length = strlen(string);
	if (str_length <= length)
		return;

	length--;

	size_t used = 0;
	char * ptrA = string;
	char * ptrB = string + str_length;
	while (used < length) {
		int char_length = util_string_valid_utf8_char(ptrA);
		if (char_length == 0)
			return;

		if (used + char_length > length)
			break;

		used += char_length;
		ptrA += char_length;

		int offset = 1;
		while (char_length = util_string_valid_utf8_char(ptrB - offset), ptrA < ptrB - offset && char_length == 0)
			offset++;

		if (char_length == 0)
			return;

		if (used + char_length > length)
			break;

		used += char_length;
		ptrB -= char_length;
	}

	*ptrA = '~';
	memmove(ptrA + 1, ptrB, strlen(ptrB) + 1);
}

void util_string_middle_elipsis2(char * string, size_t length) {
	size_t str_length = strlen(string);
	if (str_length <= length)
		return;

	length--;

	size_t used = 0;
	char * ptrA = string;
	char * ptrB = string + str_length;
	while (used < length) {
		int char_length = util_string_valid_utf8_char(ptrA);
		if (char_length == 0)
			return;

		if (used + char_length > length)
			break;

		used++;
		ptrA += char_length;

		int offset = 1;
		while (char_length = util_string_valid_utf8_char(ptrB - offset), ptrA < ptrB - offset && char_length == 0)
			offset++;

		if (char_length == 0)
			return;

		if (used + char_length > length)
			break;

		used++;
		ptrB -= char_length;
	}

	*ptrA = '~';
	memmove(ptrA + 1, ptrB, strlen(ptrB) + 1);
}

static int util_string_valid_utf8_char(const char * string) {
	const unsigned char * ptr = (const unsigned char *) string;
	if ((*ptr & 0x7F) == *ptr)
		return 1;
	else if ((*ptr & 0xBF) == *ptr)
		return 0;
	else if ((*ptr & 0xDF) == *ptr)
		return ((ptr[1] & 0xBF) != ptr[1] || ((ptr[1] & 0x80) != 0x80)) ? 0 : 2;
	else if ((*ptr & 0xEF) == *ptr)
		return util_string_valid_utf8_char2(ptr, 2) ? 0 : 3;
	else if ((*ptr & 0xF7) == *ptr)
		return util_string_valid_utf8_char2(ptr, 3) ? 0 : 4;
	else
		return 0;
}

static int util_string_valid_utf8_char2(const unsigned char * ptr, unsigned short length) {
	unsigned short i;
	for (i = 1; i <= length; i++)
		if ((ptr[i] & 0xBF) != ptr[i] || ((ptr[i] & 0x80) != 0x80))
			return 1;
	return 0;
}

