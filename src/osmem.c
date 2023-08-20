// SPDX-License-Identifier: BSD-3-Clause

#include <unistd.h>
#include <string.h>
#include "osmem.h"
#include "helpers.h"

void *list_head;
int heap_allocated;

// request new space on the heap using sbrk
struct block_meta *new_small_space(struct block_meta *last, size_t size)
{
	// current break address
	void *request = sbrk(0);
	request = sbrk(ALIGN(size) + ALIGNED_META_SIZE);

	DIE(request == MAP_FAILED, "sbrk failed at new_space");

	if (request == MAP_FAILED) {
		return NULL;
	}

	struct block_meta *new_block = request;

	// add the new block to the list
	if (last) {
		last->next = new_block;
	}

	new_block->size = ALIGN(size);
	new_block->status = STATUS_ALLOC;
	new_block->next = NULL;

	return new_block;
}

// request space using mmap
struct block_meta *new_big_space(struct block_meta *last, size_t size)
{
	struct block_meta *new_block;
	size_t aligned_size = ALIGN(size) + ALIGNED_META_SIZE;
	new_block = mmap(0, aligned_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	DIE(new_block == MAP_FAILED, "mmap failed at new_space");

	if (last) {
		last->next = new_block;
	}

	new_block->size = ALIGN(size);
	new_block->status = STATUS_MAPPED;
	new_block->next = NULL;
	return new_block;
}

// expand the last free block
void expand_block(struct block_meta *block, size_t size)
{
	size_t aligned_needed_size = ALIGN(size) - ALIGN(block->size);
	sbrk(aligned_needed_size);
	block->size = ALIGN(size);
}

// preallocate 128kB of memory to reduce the number of sbrk calls
struct block_meta *prealloc(void)
{
	struct block_meta *new_block = new_small_space(NULL, SIZE_THRESHOLD - ALIGNED_META_SIZE);

	if (new_block == NULL) {
		return NULL;
	}

	new_block->status = STATUS_FREE;
	return new_block;
}

// first time memory is allocated in the program
void *first_small_alloc(size_t size)
{
	// heap preallocation - first allocate 128kB of memory and then split it
	list_head = prealloc();

	if (list_head == NULL) {
		return NULL;
	}

	struct block_meta *new_block = list_head;

	// split the first block in 2 (if posible)
	// the first one contains the info, the second in free to use
	if (SIZE_THRESHOLD >= ALIGN(size) + 2 * ALIGNED_META_SIZE + ALIGN(1)) {
		void *addr = (void *)((char *)new_block + ALIGNED_META_SIZE + ALIGN(size));
		struct block_meta *new_next_block = (struct block_meta *)addr;
		new_next_block->size = ALIGN(SIZE_THRESHOLD - ALIGN(size) - 2 * ALIGNED_META_SIZE);
		new_next_block->status = STATUS_FREE;
		new_next_block->next = NULL;
		new_block->next = new_next_block;
		new_block->size = ALIGN(size);
	}
	new_block->status = STATUS_ALLOC;

	void *return_address = (void *)((char *)new_block + ALIGNED_META_SIZE);
	return return_address;
}

// the best block is the one that is the smallest and can fit the requested size
// to find it:
// - all the free blocks in the heap are coalesced
// - the smallest block that can fit the requested size is selected
// - if the block is bigger than the requested size it is split in 2 (if possible)
// - if there is no block that can fit the requested size:
//		- first try to expand the last free block
//		- if the last free block can't be expanded, allocate a new block
void *find_best_fit(size_t size)
{
	struct block_meta *curr = list_head;
	struct block_meta *prev = NULL;
	struct block_meta *best_fit = NULL;
	struct block_meta *last_free_block = NULL;
	void *return_address = NULL;
	size_t best_size = 0;

	// coalesce all the free blocks
	while (curr) {
		// coalesce the curr and prev blocks in the prev one
		if (curr->status == STATUS_FREE && prev != NULL && prev->status == STATUS_FREE) {
			prev->size = ALIGN(prev->size) + ALIGNED_META_SIZE + ALIGN(curr->size);
			prev->next = curr->next;
			curr = curr->next;
		} else {
			prev = curr;
			curr = curr->next;
		}
	}

	// go through the list and find the best fit
	curr = list_head;
	prev = NULL;
	while (curr) {
		if (curr->status == STATUS_FREE && curr->size >= size) {
			if (best_fit == NULL || curr->size < best_size) {
				best_fit = curr;
				best_size = curr->size;
			}
		}

		if (curr->status == STATUS_FREE) {
			last_free_block = curr;
		}

		prev = curr;
		curr = curr->next;
	}

	// now we know the best fit. We need to split it if possible
	if (best_fit != NULL) {
		// if the size of the free block is big enough, we split it
		// enough = there is space for a new block_meta and at least 1 byte of data
		if (ALIGN(best_fit->size) >= ALIGN(size) + ALIGNED_META_SIZE + ALIGN(1)) {
			void *addr = (void *)((char *)best_fit + ALIGNED_META_SIZE + ALIGN(size));
			struct block_meta *new_next_block = (struct block_meta *)addr;
			new_next_block->size = ALIGN(best_fit->size) - ALIGN(size) - ALIGNED_META_SIZE;
			new_next_block->status = STATUS_FREE;
			new_next_block->next = best_fit->next;
			best_fit->next = new_next_block;
			// change the size only if the block is split
			best_fit->size = ALIGN(size);
		}

		best_fit->status = STATUS_ALLOC;
		return_address = (void *)((char *)best_fit + ALIGNED_META_SIZE);
		return return_address;
	}

	// if we do not have a best fit (aka all the blocks are too small) we try
	// to expand the last free block
	if (last_free_block && (last_free_block->next == NULL || last_free_block->next->status == STATUS_MAPPED)) {
		expand_block(last_free_block, size);
		last_free_block->status = STATUS_ALLOC;
		return_address = (void *)((char *)last_free_block + ALIGNED_META_SIZE);
		return return_address;
	}

	// lastly, if nothing worked, we allocate a new block
	struct block_meta *new_block = new_small_space(prev, size);
	if (new_block == NULL) {
		return NULL;
	}
	return_address = (void *)((char *)new_block + ALIGNED_META_SIZE);
	return return_address;
}

// used by malloc and calloc to allocate memory in all posible cases:
// - first time malloc/calloc is called in the program
// - the block needs to be allocated with mmap
// - to find the best fit for later allocations
void *allocate_memory(size_t size, size_t size_treshold)
{
	if (list_head == NULL || heap_allocated == 0) {
		// first time malloc/calloc is called in the program
		if (size + ALIGNED_META_SIZE < size_treshold) {
			heap_allocated = 1;
			return first_small_alloc(size);

		} else {
			struct block_meta *new_block = new_big_space(NULL, size);
			if (new_block == NULL) {
				return NULL;
			}
			list_head = new_block;
			void *return_address = (void *)((char *)new_block + ALIGNED_META_SIZE);
			return return_address;
		}
	} else {
		if (size + ALIGNED_META_SIZE >= size_treshold) {
			// the block needs to be allocated with mmap
			struct block_meta *prev = list_head;
			while (prev->next) {
				prev = prev->next;
			}

			struct block_meta *new_block = new_big_space(prev, size);
			if (new_block == NULL) {
				return NULL;
			}
			void *return_address = (void *)((char *)new_block + ALIGNED_META_SIZE);
			return return_address;

		} else {
			void *return_address = find_best_fit(size);
			return return_address;
		}
	}
	return NULL;
}

// used by realloc in case that the block cannot be expanded,
// blocks cannot be coalesced or the memory was allocated with mmap
void *replace(void *ptr, size_t old_size, size_t new_size)
{
	void *new_mem = os_malloc(new_size);
	if (new_mem == NULL) {
		return NULL;
	}
	memcpy(new_mem, ptr, old_size);
	os_free(ptr);
	return new_mem;
}

void *os_malloc(size_t size)
{
	if (size <= 0) {
		return NULL;
	}

	void *return_address = allocate_memory(size, SIZE_THRESHOLD);
	return return_address;
}

void os_free(void *ptr)
{
	if (ptr == NULL) {
		return;
	}
	// get the block information
	struct block_meta *info = (struct block_meta *) ((char *)ptr - ALIGNED_META_SIZE);
	if (info->status == STATUS_MAPPED) {
		if (info == list_head) {
			list_head = info->next;
		}

		// remove the block from the list
		struct block_meta *curr = list_head;
		struct block_meta *prev = NULL;
		while (curr) {
			if (curr == info) {
				if (prev != NULL) {
					prev->next = curr->next;
				}
				break;
			}
			prev = curr;
			curr = curr->next;
		}

		int result = munmap(info, ALIGN(info->size) + ALIGNED_META_SIZE);
		DIE(result == -1, "munmap failed at os_free");
	} else if (info->status == STATUS_ALLOC) {
		// just mark the block as free and usable
		info->status = STATUS_FREE;
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	if (nmemb == 0 || size == 0) {
		return NULL;
	}

	size_t total_size = nmemb * size;

	// check for overflow
	if (total_size / nmemb != size) {
		return NULL;
	}

	size_t page_size = sysconf(_SC_PAGESIZE);
	void *return_address = allocate_memory(total_size, page_size);
	memset(return_address, 0, total_size);
	return return_address;
}

void *os_realloc(void *ptr, size_t size)
{
	if (ptr == NULL) {
		return os_malloc(size);
	}

	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	struct block_meta *info = (struct block_meta *)((char *)ptr - ALIGNED_META_SIZE);

	if (info->status == STATUS_FREE) {
		return NULL;
	} else if (info->status == STATUS_ALLOC) {
		// the size is smaller, so the block should be truncated
		if (info->size >= size) {
			// check if the block can be split
			if (ALIGN(info->size) >= ALIGN(size) + ALIGNED_META_SIZE + ALIGN(1)) {
				void *addr = (void *)((char *)info + ALIGNED_META_SIZE + ALIGN(size));
				struct block_meta *new_block = (struct block_meta *)addr;
				new_block->status = STATUS_FREE;
				new_block->size = ALIGN(info->size) - ALIGN(size) - ALIGNED_META_SIZE;
				new_block->next = info->next;
				info->next = new_block;
				info->size = ALIGN(size);
			}
			return ptr;
		} else if (info->size < size && size < SIZE_THRESHOLD) {
			// first, try to expand the block, by coalescing the next free blocks
			struct block_meta *next_block = info->next;

			if (next_block == NULL || next_block->status == STATUS_MAPPED) {
				// the block is the last one on the heap, so we need to expand this one
				void *new_block = sbrk(ALIGN(size) - ALIGN(info->size));
				if (new_block == NULL) {
					return NULL;
				}
				info->size = ALIGN(size);
				return ptr;
			}

			// expand the block by coalescing the next free blocks
			while (next_block) {
				if (next_block->status == STATUS_FREE) {
					info->size = ALIGN(info->size) + ALIGN(next_block->size) + ALIGNED_META_SIZE;
					info->next = next_block->next;

					// verify the size condition for each coalesced block
					if (info->size < size) {
						next_block = next_block->next;
						continue;
					} else if (ALIGN(info->size) >= ALIGN(size) + ALIGNED_META_SIZE + ALIGN(1)) {
						// the block expanded enough, now we should check if needs to be truncated
						void *addr = (void *)((char *)info + ALIGNED_META_SIZE + ALIGN(size));
						struct block_meta *new_block = (struct block_meta *)addr;

						new_block->status = STATUS_FREE;
						new_block->size = ALIGN(info->size) -  ALIGN(size) - ALIGNED_META_SIZE;
						new_block->next = next_block->next;
						info->next = new_block;
						info->size = ALIGN(size);
					}
					return ptr;
					next_block = next_block->next;
					continue;
				}
				break;
			}

			// if the block can't be expanded, then it should be moved to the heap
			return replace(ptr, info->size, size);
		} else if (info->size < size && size >= SIZE_THRESHOLD) {
			// the new memory should be allocated with mmap
			return replace(ptr, info->size, size);
		}
	} else if (info->status == STATUS_MAPPED) {
		return replace(ptr, size, size);
	}
	return NULL;
}
