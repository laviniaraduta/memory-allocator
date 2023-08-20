# Raduta Lavinia-Maria 333CA
# Operating Systems - Homework 2
# Memory allocator
### April 2023

1. List of blocks
- to keep track of all the allocated blocks of memory I used a singly linked 
list of struct block_meta
- I discovered later that the blocks allocated with mmap didn't need to be 
kept in that list, but I wanted to keep track of all of them in one place
- I keep a pointer to the head of the list all the time void *list_head, 
that is initially NULL to know whether it is the first time malloc/calloc is 
called in the program or not.
- using this list, coleasing the free blocks is easier
- all the blocks allocated in the heap will be kept in the fist part of the 
list, and ordered ascending by their start address

1. Malloc & Calloc
- both functions work the same, the only differences are:
    - before returning the pointer in calloc, the memory is set to 0
    - calloc has the threshold at page_size (4096 B) while malloc has it at 
    128kB until the space is allocated with brk. after that treshold, it is 
    mapped using mmap
- this the reason I chose to do a single function that does all the work and 
both of them call it with correct parameters
- if it is the first allocation of memory in the program we have 2 cases:
    - the size (+ the size of the metadata struct) is smaller than the 
    threshold then I allocate a bigger space (128kB) of memory on the heap 
    to reeduce number of brk calls
        - after this allocation, if there is enough space left for another 
        block after this one (meta size + 1 aligned byte) I split the big 
        block in 2, mark the first one as allocated and the other one as free
        - then the head list pointer will point at the beeginning of the 
        first block
    - if the size is bigger than the threashold, just call mmap and add the 
    block to the list
- if it is not the first time:
    - if the size is smaller than the threshold I applied the best fit 
    heuristic to find the best space for the new allocated block
        - first of all, I go through all the list of blocks in the heap and 
        coalesce the free blocks that are next to echother in a bigger one
        - then, I go through it one more time to find the best fit - the 
        smallest block bigger (or hopefully the same size) than the size I 
        want to allocate
        - if I find this kind of block, I check to see if it can be split 
        (keep the size I need, and set the other part as free) then return 
        the start address of the main memory allocated (jump over sizeof 
        meta block from the beginning of the found block)
        - if the search was not successfull (all free blocks were too 
        small) I try to expand the last block in the heap, if it is free. 
        This means that I move the program break with the difference untill 
        I have the wanted size.
        - lastly, if nothing worked, it is time to move ethe program break 
        with the size I need + the size of a meta block and add the 
        reference of this new block at the end of the list
    - if the size is bigger than the threshold, request more space from the 
    operting system with mmap

2. Realloc
- this was the most difficlt part, as realloc has a lot of corner cases that 
I (hopeffully discovered)
- if the block that needs to be reallocated was previously mapped, then I 
perform malloc with the new size (malloc takes care of all the other 
sub-cases), copy all the info from the old block to the new one and then 
free the old memory
- if the block was previously allocated with brk:
    - if the new size is smaller, then I just perform a split like I do in 
    malloc
    - if the new size is bigger, but still smaller than the threshold, 
    realloc prioritises the expansion of the current block, rather than 
    finding a new one
        - the free blocks that follow are colesced with the current one, one 
        by one until the size is enough for the request.
        - when colescending a block we get to a bigger size than the desired 
        one, the block is split (if possible)
        - if the current block couldn't be expanded, malloc is called, and 
        all the steps (colescing the free block)
    - if the size is bigger and also bigger than the threshold, a new 
    blocked is mapped and added to the list, all the content is copied at 
    the new destination and the old block is marked as free

3. Free 
- if the block is allocated with brk, the block is just marked as free
- if the block is mapped, then the node is removed from the list and the 
memory is unmapped using munmap

Fore more infos about malloc I used the documents provided in the README of the
homework.

** about coding style warnings. some of them are very annoying so, I considered
that having a consistent coding style through all my code was more important
than solving all the absurde warnings