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

// free, malloc
#include <stdlib.h>
// sha512_digest, sha512_init, sha512_update
#include <nettle/sha2.h>
// strdup
#include <string.h>

#include "digest.h"

struct checksum_sha512 {
	struct sha512_ctx sha512;
	char digest[SHA512_DIGEST_SIZE * 2 + 1];
};

static char * checksum_sha512_digest(struct checksum * checksum);
static void checksum_sha512_free(struct checksum * checksum);
static ssize_t checksum_sha512_update(struct checksum * checksum, const void * data, ssize_t length);

static struct checksum_ops checksum_sha512_ops = {
	.digest = checksum_sha512_digest,
	.free   = checksum_sha512_free,
	.update = checksum_sha512_update,
};


static char * checksum_sha512_digest(struct checksum * checksum) {
	if (checksum == NULL)
		return NULL;

	struct checksum_sha512 * self = checksum->data;
	if (self->digest[0] != '\0')
		return strdup(self->digest);

	struct sha512_ctx sha512 = self->sha512;
	unsigned char digest[SHA512_DIGEST_SIZE];
	sha512_digest(&sha512, SHA512_DIGEST_SIZE, digest);

	digest_convert_to_hex(digest, SHA512_DIGEST_SIZE, self->digest);

	return strdup(self->digest);
}

static void checksum_sha512_free(struct checksum * checksum) {
	if (checksum == NULL)
		return;

	struct checksum_sha512 * self = checksum->data;

	unsigned char digest[SHA512_DIGEST_SIZE];
	sha512_digest(&self->sha512, SHA512_DIGEST_SIZE, digest);

	free(self);

	checksum->data = NULL;
	checksum->ops = NULL;

	free(checksum);
}

struct checksum * checksum_sha512_new_checksum() {
	struct checksum * checksum = malloc(sizeof(struct checksum));
	checksum->ops = &checksum_sha512_ops;

	struct checksum_sha512 * self = malloc(sizeof(struct checksum_sha512));
	sha512_init(&self->sha512);
	*self->digest = '\0';

	checksum->data = self;
	return checksum;
}

static ssize_t checksum_sha512_update(struct checksum * checksum, const void * data, ssize_t length) {
	if (checksum == NULL || data == NULL || length < 1)
		return -1;

	struct checksum_sha512 * self = checksum->data;
	sha512_update(&self->sha512, length, data);
	return length;
}

