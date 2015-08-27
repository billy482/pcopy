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

#ifndef __PCOPY_CHECKSUM_H__
#define __PCOPY_CHECKSUM_H__

// bool
#include <stdbool.h>
// ssize_t
#include <sys/types.h>

struct checksum {
	struct checksum_ops {
		char * (*digest)(struct checksum * checksum) __attribute__((warn_unused_result));
		void (*free)(struct checksum * checksum);
		ssize_t (*update)(struct checksum * checksum, const void * data, ssize_t length);
	} * ops;

	void * data;
};

struct checksum_driver {
	char * name;
	struct checksum * (*new_checksum)(void) __attribute__((warn_unused_result));
};

void checksum_add(const char * digest, const char * path);
void checksum_create(const char * filename);
struct checksum_driver * checksum_digests(void);
struct checksum_driver * checksum_get_default(void);
bool checksum_has_checksum_file(void);
bool checksum_parse(char ** digest, char ** path);
void checksum_rewind(void);
bool checksum_set_default(const char * checksum);

#endif


