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
// sha256_digest, sha256_init, sha256_update
#include <nettle/sha2.h>
// strdup
#include <string.h>

#include "digest.h"

struct checksum_sha256 {
	struct sha256_ctx sha256;
	char digest[SHA256_DIGEST_SIZE * 2 + 1];
};

static char * checksum_sha256_digest(struct checksum * checksum);
static void checksum_sha256_free(struct checksum * checksum);
static ssize_t checksum_sha256_update(struct checksum * checksum, const void * data, ssize_t length);

static struct checksum_ops checksum_sha256_ops = {
	.digest = checksum_sha256_digest,
	.free   = checksum_sha256_free,
	.update = checksum_sha256_update,
};


static char * checksum_sha256_digest(struct checksum * checksum) {
	if (checksum == NULL)
		return NULL;

	struct checksum_sha256 * self = checksum->data;
	if (self->digest[0] != '\0')
		return strdup(self->digest);

	struct sha256_ctx sha256 = self->sha256;
	unsigned char digest[SHA256_DIGEST_SIZE];
	sha256_digest(&sha256, SHA256_DIGEST_SIZE, digest);

	digest_convert_to_hex(digest, SHA256_DIGEST_SIZE, self->digest);

	return strdup(self->digest);
}

static void checksum_sha256_free(struct checksum * checksum) {
	if (checksum == NULL)
		return;

	struct checksum_sha256 * self = checksum->data;

	unsigned char digest[SHA256_DIGEST_SIZE];
	sha256_digest(&self->sha256, SHA256_DIGEST_SIZE, digest);

	free(self);

	checksum->data = NULL;
	checksum->ops = NULL;

	free(checksum);
}

struct checksum * checksum_sha256_new_checksum() {
	struct checksum * checksum = malloc(sizeof(struct checksum));
	checksum->ops = &checksum_sha256_ops;

	struct checksum_sha256 * self = malloc(sizeof(struct checksum_sha256));
	sha256_init(&self->sha256);
	*self->digest = '\0';

	checksum->data = self;
	return checksum;
}

static ssize_t checksum_sha256_update(struct checksum * checksum, const void * data, ssize_t length) {
	if (checksum == NULL || data == NULL || length < 1)
		return -1;

	struct checksum_sha256 * self = checksum->data;
	sha256_update(&self->sha256, length, data);
	return length;
}

