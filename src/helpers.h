/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DIE(assertion, call_description)                                                                               \
	do {                                                                                                               \
		if (assertion) {                                                                                               \
			fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__);                                                         \
			perror(call_description);                                                                                  \
			exit(errno);                                                                                               \
		}                                                                                                              \
	} while (0)

/* Structure to hold memory block metadata */
struct block_meta {
	size_t size;
	int status;
	struct block_meta *next;
};
#define META_SIZE sizeof(struct block_meta)

/* Block metadata status values */
#define STATUS_FREE   0
#define STATUS_ALLOC  1
#define STATUS_MAPPED 2

#define MAP_FAILED ((void *) -1)
#define SIZE_THRESHOLD (128 * 1024)

/* Alignement information */
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define ALIGNED_META_SIZE ALIGN(META_SIZE)
