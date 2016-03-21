#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

/*MACROS*/
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)
#define MINSIZE 24

#define MAX(x, y) ((x) > (y)? (x) : (y))
/* pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

 /* read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/*get size and allocation from header*/
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

 /*comopute address of header/footer from blick ptr*/
#define HDRP(bp) ((void *)(bp) - WSIZE)
#define FTRP(bp) ((void *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/*find address of next/prev blocks from block ptr*/
#define NEXT_BLKP(bp) ((void *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((void *)(bp) - GET_SIZE(HDRP(bp) - WSIZE))

/*find address of next/prev free blocks from block ptr*/
#define PREV_FREE(bp) (*(void **)(bp))
#define NEXT_FREE(bp) (*(void **)(bp + DSIZE))

/*function declaration*/
static void *extend_heap(size_t words);
static void *find_fit(unsigned int asize);
static void remove_free_bp(void *bp);
void insert_free_block(void *bp);
int mm_init(void);
void *mm_malloc(size_t size);
static void remove_free_bp(void *bp);
void mm_free(void *bp);
static void place(void *bp, unsigned int asize);
static void *coalesce(void *bp);
int check_block(void *bp);

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

/*Global variables*/
static void *heap_listp = 0;
static void *free_listp = 0;
unsigned int count = 0;
unsigned int full_count = 0;

/*
 * initializes the dynamic storage allocator (allocate initial heap space)
 * arguments: none
 * returns: 0, if successful
 *         -1, if an error occurs
 */
int mm_init(void) {
	//reset global variables
	heap_listp = 0;
	free_listp = 0;
	count = 0;

	//create initial empty heap
	if((heap_listp = mem_sbrk(MINSIZE + ALIGNMENT)) == (void *)-1) {
		return -1;
	}

	PUT(heap_listp, 0); //alighment padding

	PUT(heap_listp + WSIZE, PACK(MINSIZE, 1)); //prologue header of size 24
	PUT(heap_listp + MINSIZE, PACK(MINSIZE, 1)); //prologue footer of size 24
	PUT(heap_listp + MINSIZE + WSIZE, PACK(0,1)); //epilogue header
	
	//pointers
	free_listp = heap_listp + DSIZE;

	//extend heap to create first empty block
	if(extend_heap(CHUNKSIZE/WSIZE) == NULL){
		return -1;
	}
	
	return 0;
}


/*
 * allocates a block of memory and returns a pointer to that block's payload
 * arguments: size: the desired payload size for the block
 * returns: a pointer to the newly-allocated blpock's payload (whose size
 *          is a multiple of ALIGNMENT), or NULL if an error
 *          occurred
 */
void *mm_malloc(size_t size) {
	unsigned int asize;
	size_t extendsize;
	void *bp;
	//ignore suprious retquests
	if(size <= 0){
		return NULL;
	}
	//adjust block size to include overhead and alignment reqs
	asize = MAX(ALIGN(size) + ALIGNMENT, MINSIZE);
	if((bp = find_fit(asize))){
		place(bp, asize);
		return bp;
	} 
	//no fit found, extend heap
	extendsize = MAX(asize, CHUNKSIZE);
	if((bp = extend_heap(extendsize/WSIZE)) == NULL) {
		return NULL;
	}
	place(bp, asize);
	return bp;
}

static void *find_fit(unsigned int asize) {
	void *bp;
	//first fit search
	for(bp = free_listp; GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_FREE(bp)) {
		if (asize <= (unsigned int)GET_SIZE(HDRP(bp))) {
			return bp;
		}
	}
	//if there was no fit
	return NULL;
}

/* remove_free_bp
 * removes a free block from the free list
 * and reset's it's pointer's pointers
 *
 */
static void remove_free_bp(void *bp) {
	// printf("removefree\n");
	
	//if there is a previous pointer (not NULL)
	if(PREV_FREE(bp)) {
		//set prev's next to bp's next
		NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
		//set next's prev to bp's prev
		PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
	} else {
		//set head of free list to next
		free_listp = NEXT_FREE(bp);
		//set next's prev to bp's prev
		PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
	}
	//decrement free count
	count -= 1;
}

/*
 * frees a block of memory, enabling it to be reused later
 * arguments: ptr: pointer to the allocated block to free
 * returns: nothing
 */
void mm_free(void *bp) {
	unsigned int size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	coalesce(bp);
}

/*
 * reallocates a memory block to update it with a new given size
 * arguments: ptr: a pointer to the memory block
 *            size: the desired new block size
 * returns: a pointer to the new memory block
 */
void *mm_realloc(void *ptr, size_t size) {
	(void) (ptr);
	(void)(size);
	return NULL;
}

/* check_block
 * Checks indivitual blocks as they are added or changed
 * 	for valid next and free pointers and for valid allocation 
 *  This is for debugging purposes
 */
int check_block(void *bp){
	//if there is more than one free block in the list, check its attributes
	if(count > 1){
		//check next points
		if(NEXT_FREE(bp) < mem_heap_lo() || NEXT_FREE(bp) > mem_heap_hi()){
			void *next = NEXT_FREE(bp);
			printf("Error: next pointer out of bounds\n:%zu\n", (size_t)next);
		}
	}
	if(count > 1){
		//check prev pointers
		if(PREV_FREE(bp) < mem_heap_lo() || PREV_FREE(bp) > mem_heap_hi()){
			void *prev = PREV_FREE(bp);
			printf("Error: prev pointer out of bounds\n:%zu\n", (size_t)prev);
		}
	}
	//check allocation of pointers
	if(GET_ALLOC(HDRP(bp))) {
		printf("Error: bp is allocated but in free list\n");
		if (NEXT_FREE(bp)){
			if(GET_ALLOC(HDRP(NEXT_FREE(bp)))) {
				printf("Error: next pointer in free list is allocated\n\n");
			}
		}
		if (PREV_FREE(bp)){
			if((GET_ALLOC(HDRP(PREV_FREE(bp))))) {
				printf("Error: pointer in free list is allocated\n\n");
			}
		}
	}
	return 0;
}
/*
 * checks the state of the heap for internal consistency
 * arguments: none
 * returns: 0, if successful
 *          nonzero, if the heap is not consistent
 */

/* check_heap
 * 	checks the entire heap for consistency
 *	checks if all blocks in the free list are free
 *  checks for pointer validity
 */ 
int mm_check_heap(void) {
	void *bp;
	unsigned int temp1 = count;
	unsigned int temp2 = full_count;

	if(count > 1) {
		for (bp = free_listp; temp1 > 0; bp = NEXT_FREE(bp)) {
			temp1 -= 1;
			//if bp has a next pointer (not NULL)
			if(NEXT_FREE(bp)) {
				//check if it is in bounds
				if(NEXT_FREE(bp) < mem_heap_lo() || NEXT_FREE(bp) > mem_heap_hi()) {
						printf("\nError: next pointer out of bounds\n\n");
				}
				//check its allocation
				if (GET_ALLOC(HDRP(NEXT_FREE(bp)))) {
					printf("Error: pointer in free list is allocated\n");
				}
			}
			//check bp's allocations
			if(GET_ALLOC(HDRP(bp))) {
				printf("Error: pointer in free list is allocated\n");
			}
			//if bp has a prev
			if(PREV_FREE(bp)) {
				//check allocation
				if (GET_ALLOC(HDRP(PREV_FREE(bp)))) {
					printf("\nError: pointer in free list is allocated\n\n");
				}
				//check if pointers in bounds
				if((PREV_FREE(bp) < mem_heap_lo()) || (PREV_FREE(bp) > mem_heap_hi())) {
					printf("\nError: prev pointer out of bounds\n\n");
				}
			}
		}
	}
	//print each block in heap
	for(bp = heap_listp; temp2 > 0; bp = NEXT_BLKP(bp)) {
		char *status;
		size_t next;
		temp2 -= 1;
		unsigned int alloc = GET_ALLOC(bp);
		unsigned int size = GET_SIZE(HDRP(bp));
		size_t location = (size_t)HDRP(bp);
		
		if(alloc) {
			status = "allocated";
		} else {
			status = "free";
		}
		
		if(!status) {
			next = (size_t)NEXT_FREE(bp);
			printf("%s block at %zu, size %d, Next: %zu\n", status, location, size, next);
		} else {
			printf("%s block at %zu, size %d\n", status, location, size);
		}
	}
    return 0;
}

/* place
 * 	
 * sets the header and footer with its new size
 * splits the block in two if it is large enough
 *
 */
static void place(void *bp, unsigned int asize) {
	unsigned int csize = GET_SIZE(HDRP(bp));

	//split block if large enough
	if ((csize - asize) >= MINSIZE) {
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		void *temp = NEXT_BLKP(bp);
		remove_free_bp(bp);
		//create new empty block	
		bp = temp;
		full_count += 1;
		PUT(HDRP(bp), PACK(csize-asize, 0));
		PUT(FTRP(bp), PACK(csize-asize, 0));
		//coalesce new block
		coalesce(bp);
	} else {
		//fill empty block
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
		//remove from free list
		remove_free_bp(bp);
	}
}

/* insert free block
 * adds a block to the free list
 * sets previous and currents pointers
 *
*/
void insert_free_block(void *bp) {
	NEXT_FREE(bp) = free_listp;
	PREV_FREE(free_listp) = bp;
	PREV_FREE(bp) = NULL;
	free_listp = bp;
	#ifdef DEBUG2
	if(count) {
		void *curr_next = NEXT_FREE(bp);
		void *next_next = NEXT_FREE(curr_next);
		void *next_prev = PREV_FREE(curr_next);
		printf("bp is: %d\nnext is: %d, next->next is %d, next->prev is %d\n", GET(bp), GET(curr_next), GET(next_next), GET(next_prev));
	}
	printf("checking from insert\n");
	#endif
	count += 1;
}

/* extend heap
* 	calls msbrk and increments the heap size,
*	creating a new empty block
*
*/
static void *extend_heap(size_t words) {
	void *bp;
	unsigned int size;
	full_count += 1;

	//allocate an even number of words to maintin alignment
	size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
	if(size < MINSIZE) {
		size = MINSIZE;
	}

	if((long)(bp = mem_sbrk(size)) == -1) {
		return NULL;
	}
	//initialize free block header and footer and epilogue header
	PUT(HDRP(bp), PACK(size, 0)); //free block header
	PUT(FTRP(bp), PACK(size, 0)); //free block footer
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); //new epilogue header

	return coalesce(bp);
}

/* coalesce
 * combines adjacent blocks with bp if they are empty 
 *
 */
static void *coalesce(void *bp) {
	size_t prev_alloc;
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	//if prev pointer is in bp, use that value insead of searching in header
	if(PREV_BLKP(bp) == bp) {
		prev_alloc = (size_t)PREV_BLKP(bp);
	} else {
		prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
	}

	//check neighbors
	if (!prev_alloc && !next_alloc) {
		//both blocks are empty		
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
		//remove both from free list
		remove_free_bp(NEXT_BLKP(bp));
		remove_free_bp(PREV_BLKP(bp));
		//set prev as head of new block
		bp = PREV_BLKP(bp);
		//set size of new block
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	} else if (prev_alloc && !next_alloc) {	
		//only next is free		
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		//remove next from free list
		remove_free_bp(NEXT_BLKP(bp));
		//set size of new block
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	} else if (!prev_alloc && next_alloc) {
		//only prev is free
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		//set prev as new block head
		bp = PREV_BLKP(bp);
		//remove free from free list
		remove_free_bp(bp);
		//set size of new block
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	//add new block to free list
	insert_free_block(bp);
	return bp;
}
