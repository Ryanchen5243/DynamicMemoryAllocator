/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include <errno.h>

// some constants
#define ROW_SIZE 8 // 8 bytes
#define MIN_BLOCK_SIZE 32 // minimum size of block

/* Read and write a word at address p */
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t*)(p) = (val)) // write 8 bytes long long

// pack size and info into a word
#define PACK(size,inQkList,prvAlloc,alloc) (size | inQkList | prvAlloc | alloc)
#define GET_SIZE(p) (GET(p) & ~0x7) // mask bits of header to get size
#define GET_ALLOC(p) (GET(p) & 0x1) // mask bits to get alloc status
#define GET_IN_QUICK_LIST(p) ((GET(p) & IN_QUICK_LIST) == IN_QUICK_LIST) // get quick list status
#define GET_PREV_ALLOC(p) ((GET(p) & PREV_BLOCK_ALLOCATED) == PREV_BLOCK_ALLOCATED)

static size_t getFreeListSizeClassUpperBound(int index);
static sf_block* findQuickListFit(size_t size); // return index of quick list for given size
static sf_block* findMainFreeListFit(size_t size);
static int isMainFreeListEmpty(sf_block* block); // if free list is empty
static void insertIntoMainFreeList(sf_block* block); // add free block to main free lists
static void* coalesce(void*bp);
static void removeFreeBlockFromMainList(sf_block* block); // used in coalesce

void *sf_malloc(size_t size) {
    // TO BE IMPLEMENTED
    if (size == 0){
        return NULL;
    }

    // initialize heap
    if(sf_mem_start()==sf_mem_end()){
        if(sf_mem_grow() == NULL){ // extend heap
            sf_errno = ENOMEM;
            return NULL;
        }

        // write prologue block
        (*((sf_block*)sf_mem_start())).header = PACK(MIN_BLOCK_SIZE,0,0,THIS_BLOCK_ALLOCATED);
        // write epilogue block
        *((sf_header*)(sf_mem_end()-sizeof(sf_header))) = PACK(0,0,0,THIS_BLOCK_ALLOCATED);

        // initialize main free lists
        for (int i = 0; i < NUM_FREE_LISTS; i++){
            // initialize sentinal nodes
            sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
            sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        }
        // initialize quick lists
        for (int i = 0; i < NUM_QUICK_LISTS;i++){
            sf_quick_lists[i].length = 0;
            sf_quick_lists[i].first = NULL;
        }


        // determine size of free block
        size_t freeBlockSize = (sf_mem_end()-sizeof(sf_header)) - (sf_mem_start() + GET_SIZE(sf_mem_start()));

        // write free block header
        ((sf_block*) (sf_mem_start() + GET_SIZE(sf_mem_start())))->header = PACK(freeBlockSize,0,PREV_BLOCK_ALLOCATED,0);
        // write free block footer
        *((sf_footer*)(sf_mem_end()-sizeof(sf_header)-sizeof(sf_footer))) = PACK(freeBlockSize,0,PREV_BLOCK_ALLOCATED,0);

        // add free block to main free list
        int fListIndex = NUM_FREE_LISTS - 1; // default to largest size class
        for(int i = 0; i < NUM_FREE_LISTS; i++){
            if (freeBlockSize <= getFreeListSizeClassUpperBound(i)){
                fListIndex = i;
                break;
            }
        }

        // update main free list pointers
        sf_free_list_heads[fListIndex].body.links.next = sf_mem_start() + GET_SIZE(sf_mem_start());
        sf_free_list_heads[fListIndex].body.links.prev = sf_mem_start() + GET_SIZE(sf_mem_start());
        // update free block pointers
        ((sf_block*) (sf_mem_start()+GET_SIZE(sf_mem_start())))->body.links.next = &sf_free_list_heads[fListIndex];
        ((sf_block*) (sf_mem_start()+GET_SIZE(sf_mem_start())))->body.links.prev = &sf_free_list_heads[fListIndex];

    }
    /* Adjust block size to include overhead and alignment reqs */
    size_t adjustedSize = size + sizeof(sf_header);
    if (adjustedSize < MIN_BLOCK_SIZE){
        // printf("size of %zu is smaller than required block size\n",adjustedSize);
        adjustedSize = MIN_BLOCK_SIZE;
    } else {
        adjustedSize = ((adjustedSize + (ROW_SIZE-1)) / ROW_SIZE) * ROW_SIZE;
    }

    // search quick lists for exact fit block
    sf_block* quickListBlock = findQuickListFit(adjustedSize);
    // requested block size is large block
    if (adjustedSize > (size_t)(MIN_BLOCK_SIZE + (NUM_QUICK_LISTS-1)*ROW_SIZE)){
        goto search_main_free_list;
    } else if(quickListBlock == NULL){ // if no exact fit found
        goto search_main_free_list;
    }

    // mask out quicklist status
    quickListBlock->header = quickListBlock->header & ~IN_QUICK_LIST;
    // update prevalloc = true for proceeding block
    sf_block* blockAfterQuickBlock = (sf_block*)((void*)quickListBlock + GET_SIZE(quickListBlock));
    // update header
    ((sf_block*)(blockAfterQuickBlock))->header = ((sf_block*)(blockAfterQuickBlock))->header | PREV_BLOCK_ALLOCATED;
    // if in main free list, also update footer
    if(!GET_ALLOC(blockAfterQuickBlock) && !GET_IN_QUICK_LIST(blockAfterQuickBlock)){
        sf_footer* blockAfterQuickFooter = ((sf_footer*)((void*)blockAfterQuickBlock+GET_SIZE(blockAfterQuickBlock)-sizeof(sf_footer)));
       *blockAfterQuickFooter = *((sf_header*)(blockAfterQuickBlock));
    }
    return quickListBlock->body.payload;

    /* Search the main free list for a fit */
    search_main_free_list:
    sf_block* mainFreeBlock = findMainFreeListFit(adjustedSize);
    if(mainFreeBlock == NULL){
        goto getMoreMemory;
    }
    return mainFreeBlock->body.payload;

    /* No fit found. Get more memory and place the block */
    getMoreMemory:

    // while there are no sufficiently large free block
    while((mainFreeBlock = findMainFreeListFit(adjustedSize)) == NULL){
        sf_block* newBlkPtr;// ptr to start of new block
        // request more memory
        if((newBlkPtr = sf_mem_grow()) == NULL) { // unsuccessful allocation
            sf_errno= ENOMEM;
            return NULL;
        }
        size_t newBlkSize = (sf_mem_end()-sizeof(sf_header)) - ((void*)newBlkPtr - sizeof(sf_header));

        newBlkPtr = (void*)newBlkPtr - sizeof(sf_header); // old epilogue

        // write header for block (overwrite epilogue)
        if(GET_PREV_ALLOC(newBlkPtr)){
            ((sf_block*)newBlkPtr)->header = PACK(newBlkSize,0,PREV_BLOCK_ALLOCATED,0);
        } else {
            ((sf_block*)newBlkPtr)->header = PACK(newBlkSize,0,0,0);
        }

        // write footer for block
        *((sf_footer*)((void*)newBlkPtr+GET_SIZE(newBlkPtr)-sizeof(sf_footer))) = ((sf_block*)newBlkPtr)->header;

        // write epilogue
        *((sf_header*)(sf_mem_end()-sizeof(sf_header))) = PACK(0,0,0,THIS_BLOCK_ALLOCATED);
        coalesce(newBlkPtr);
    }

    return mainFreeBlock->body.payload;
}

void sf_free(void *pp) {
    // TO BE IMPLEMENTED

    // pointer to start of block
    sf_block* ptr = pp - sizeof(sf_header);

    if((pp == NULL) ||
        (((size_t)(pp) & 0x7) != 0) || // ptr not 8 byte aligned
        (GET_SIZE(ptr) < 32) || // block size less than 32
        ((GET_SIZE(ptr) &0x7) != 0) || // block size not mult of 8
        ((void*)ptr < sf_mem_start() || ((void*)ptr+GET_SIZE(ptr) > sf_mem_end())) ||// invalid header start or footer end
        (!GET_ALLOC(ptr)) || // allocated bit is 0
        (GET_IN_QUICK_LIST(ptr)) || // quick list bit is 1
        (!GET_PREV_ALLOC(ptr) && GET_ALLOC((void*)ptr-sizeof(sf_footer)))//prevalloc is 0 but alloc in prev is not 0
    ) {
        abort();
    }
    size_t blockSize = GET_SIZE(ptr);

    int quickListIndex = -1;
    // if small block -> insert into quick list and flush if necessary
    for(int i = 0; i < NUM_QUICK_LISTS; i++){
        size_t quickSize = MIN_BLOCK_SIZE + ROW_SIZE * i;
        if(blockSize == quickSize){
            quickListIndex = i;
            break;
        }
    }

    if(quickListIndex == -1) goto INSERT_INTO_MAIN;

    // alloc and prev alloc status remain same, set inquicklist
    ptr->header = ptr->header | IN_QUICK_LIST;

    if(sf_quick_lists[quickListIndex].length != QUICK_LIST_MAX){
        // insert into quick list
        sf_block* proceedingBlk = sf_quick_lists[quickListIndex].first;
        sf_quick_lists[quickListIndex].first = ptr;
        ptr->body.links.next = proceedingBlk;
        sf_quick_lists[quickListIndex].length +=1;
    } else {
        // flush out quick list and insert into block
        sf_block* qlPtr = sf_quick_lists[quickListIndex].first;
        while(qlPtr != NULL){
            // remove block from quick list
            sf_quick_lists[quickListIndex].first = (sf_quick_lists[quickListIndex].first)->body.links.next;
            // update fields
            qlPtr->header = qlPtr->header & ~(IN_QUICK_LIST | THIS_BLOCK_ALLOCATED);
            ((sf_block*)((void*)qlPtr + GET_SIZE(qlPtr)))->header =
                ((sf_block*)((void*)qlPtr + GET_SIZE(qlPtr)))->header & (~PREV_BLOCK_ALLOCATED);
            coalesce(qlPtr);
            qlPtr = sf_quick_lists[quickListIndex].first;
        }
        sf_quick_lists[quickListIndex].first = ptr;
        ptr->body.links.next = NULL;
        sf_quick_lists[quickListIndex].length = 1; // set len to 1
    }

    return;
    INSERT_INTO_MAIN: // otherwise add to main list and coalesce
    ptr->header = ptr->header & ~THIS_BLOCK_ALLOCATED; // update allocation status to free
    ((sf_block*)((void*)ptr+GET_SIZE(ptr)))->header = // update prev allocation status for following blk
        ((sf_block*)((void*)ptr+GET_SIZE(ptr)))->header & ~PREV_BLOCK_ALLOCATED;
    coalesce(ptr);

    return;
}

void *sf_realloc(void *pp, size_t rsize) {
    // TO BE IMPLEMENTED
    // validate pointer

    // pointer to start of block
    sf_block* ptr = pp - sizeof(sf_header);
    if((pp == NULL) ||
        (((size_t)(pp) & 0x7) != 0) || // ptr not 8 byte aligned
        (GET_SIZE(ptr) < 32) || // block size less than 32
        ((GET_SIZE(ptr) &0x7) != 0) || // block size not mult of 8
        ((void*)ptr < sf_mem_start() || ((void*)ptr+GET_SIZE(ptr) > sf_mem_end())) ||// invalid header start or footer end
        (!GET_ALLOC(ptr)) || // allocated bit is 0
        (GET_IN_QUICK_LIST(ptr)) || // quick list bit is 1
        (!GET_PREV_ALLOC(ptr) && GET_ALLOC((void*)ptr-sizeof(sf_footer)))//prevalloc is 0 but alloc in prev is not 0
    ) {
        sf_errno = EINVAL;
        return NULL;
    }
    if(rsize == 0){
        sf_free(pp);
        return NULL;
    }
    size_t requestedBlockSize = rsize + sizeof(sf_header);
    if(requestedBlockSize < MIN_BLOCK_SIZE){
        requestedBlockSize = MIN_BLOCK_SIZE;
    } else {
        requestedBlockSize = ((requestedBlockSize + (ROW_SIZE-1)) / ROW_SIZE) * ROW_SIZE;
    }

    if(GET_SIZE(ptr) < requestedBlockSize) {
        sf_block* newBlkPtr = sf_malloc(rsize);// obtain larger block
        if(newBlkPtr == NULL) {
            // no memory available
            sf_errno = ENOMEM;
            return NULL;
        }
        size_t payloadSize = GET_SIZE(ptr)-sizeof(sf_header);
        memcpy(newBlkPtr,(void*)ptr+sizeof(sf_header),payloadSize);
        sf_free((void*)ptr+sizeof(sf_header));
        return newBlkPtr;
    } else {
        // determine if split or not
        int toSplit = (GET_SIZE(ptr) - requestedBlockSize) >= 32;
        if(toSplit){
            size_t block1Size = requestedBlockSize;
            size_t block2Size = GET_SIZE(ptr) - requestedBlockSize;
            // update fields for first block
            if(GET_PREV_ALLOC(ptr)){
                ptr->header = PACK(block1Size,0,PREV_BLOCK_ALLOCATED,THIS_BLOCK_ALLOCATED);
            } else {
                ptr->header = PACK(block1Size,0,0,THIS_BLOCK_ALLOCATED);
            }
            // update fields for second block and insert into free list
            sf_block* block2ptr = (void*) ptr + block1Size;
            block2ptr->header = PACK(block2Size,0,PREV_BLOCK_ALLOCATED,0);
            *((sf_footer*)((void*)block2ptr+block2Size-sizeof(sf_footer)))=block2ptr->header;
            // update prevalloc for proceedin block
            sf_block* nextAdjBlk = (sf_block*)((void*)ptr+(block1Size+block2Size));
            nextAdjBlk->header |= PREV_BLOCK_ALLOCATED;
            if(!GET_ALLOC(nextAdjBlk)){ // update footer too
                *((sf_footer*)((void*)nextAdjBlk + GET_SIZE(nextAdjBlk)-sizeof(sf_footer))) = nextAdjBlk->header;
            }
            // coalesce free block
            coalesce(block2ptr);

        }
        return pp;

    }

    return NULL;
}

void *sf_memalign(size_t size, size_t align) {
    // TO BE IMPLEMENTED
    int powOfTwo = (align & (align-1)) == 0;
    if(align < MIN_BLOCK_SIZE || !powOfTwo){
        sf_errno = EINVAL;
        return NULL;
    }
    size_t reqPayloadSize = size + align + MIN_BLOCK_SIZE;
    sf_block* blockPtr = sf_malloc(reqPayloadSize); // ptr to payload
    // printf("Block ptr returned: %p\n",blockPtr);
    // printf("block returned by malloc is %p\n",(void*)blockPtr-sizeof(sf_header));
    // sf_show_heap();
    // printf("Mem start is %p\n",sf_mem_start());
    // printf("Mem end is %p\n",sf_mem_end());
    void* blockStart = (void*)blockPtr - sizeof(sf_header);
    int aligned = ((size_t)blockPtr % align) ==0;
    if(aligned){ // no work needs to be done
        return blockStart;
    } else {
        size_t offset = align - ((size_t)blockPtr% align); // offset to align by

        // printf("%zu\n",offset);
        // printf("Total block size is %zu\n",GET_SIZE(blockStart));
        // debug("testing ptr %p",(void*)blockPtr + offset);

        sf_block* block1 = blockStart; // ptr to start of b1, to free
        sf_block* block2 = (void*)blockStart + offset; // ptr to start of b2, alloc and return
        // debug("Block start %p\n",blockStart);
        // debug("Block end %p\n",(void*)blockStart+GET_SIZE(blockStart));
        // debug("Block 2: %p\n",block2);

        size_t size1 = offset;
        size_t size2 = GET_SIZE(blockStart) - offset;
        int b1pa = GET_PREV_ALLOC(block1);
        // update fields for b1
        if(b1pa){
            block1->header = PACK(size1,0,PREV_BLOCK_ALLOCATED,0);
        } else{
            block1->header = PACK(size1,0,0,0);
        }
        *((sf_footer*)((void*)block1 + size1 -sizeof(sf_footer))) = block1->header;

        // update header for block2
        block2->header = PACK(size2,0,0,THIS_BLOCK_ALLOCATED);

        // coalesce free block
        coalesce(block1);
        return block2->body.payload;
    }
}

// helper functions
/* Get upperbound for main free list via index */
static size_t getFreeListSizeClassUpperBound(int index) {
    if (index == 0){
        return MIN_BLOCK_SIZE;
    } else if(index == NUM_FREE_LISTS - 1){
        return 65535; // minimum of size_t max
    }
    return MIN_BLOCK_SIZE << index;

}
/* search quicklist for exact fit block */
static sf_block* findQuickListFit(size_t size){
    for (int i = 0; i < NUM_QUICK_LISTS;i++){
        size_t blockSize = MIN_BLOCK_SIZE + ROW_SIZE * i; // blocksize of given quicklist
        if (size == blockSize){
            if (sf_quick_lists[i].length == 0){ // no free blocks
                return NULL;
            } else {// there exists free block
                // update quicklist length
                sf_quick_lists[i].length -= 1;
                sf_block* freeBlk = sf_quick_lists[i].first;
                sf_quick_lists[i].first = (*(sf_quick_lists[i].first)).body.links.next;
                return freeBlk;
            }
        }
    }

    return NULL;
}


/* Scans main free lists for first fit, removes found block from list,
    performs splitting if necessary
 */
static sf_block* findMainFreeListFit(size_t size){
    sf_block* mainFreeBlk = NULL;
    // segragated fits with first fit
    for(int i = 0; i < NUM_FREE_LISTS;i++){
        if(size <= getFreeListSizeClassUpperBound(i)){
            if(isMainFreeListEmpty(&sf_free_list_heads[i])){
                continue;
            }
            // scan through list and look for first fit
            sf_block* ptr = &sf_free_list_heads[i]; // ptr to sentinel
            while(ptr->body.links.next != &sf_free_list_heads[i]){
                ptr = ptr->body.links.next; // go to next sf_block
                if(GET_SIZE(ptr) >= size){ // found sufficienly large block
                    mainFreeBlk = ptr;
                    removeFreeBlockFromMainList(ptr);
                    goto SPLIT_AND_RETURN_MAIN_FREE_BLOCK;
                }
            }
        }
    }
    if(mainFreeBlk == NULL) return NULL; // no found block

    SPLIT_AND_RETURN_MAIN_FREE_BLOCK:

    // split block
    if(GET_SIZE(mainFreeBlk) - size >= MIN_BLOCK_SIZE){
        size_t size1 = size;
        size_t size2 = GET_SIZE(mainFreeBlk)-size;

        if(GET_PREV_ALLOC(mainFreeBlk)){
            ((sf_block*) mainFreeBlk)->header = PACK(size1,0,PREV_BLOCK_ALLOCATED,THIS_BLOCK_ALLOCATED);
        } else {
            ((sf_block*) mainFreeBlk)->header = PACK(size1,0,0,THIS_BLOCK_ALLOCATED);
        }
        sf_block* otherHalfBlockPtr = (void*)mainFreeBlk + GET_SIZE(mainFreeBlk);
        // write header and footer of second split block
        otherHalfBlockPtr->header = PACK(size2,0,PREV_BLOCK_ALLOCATED,0);

        *((sf_footer*)((void*)otherHalfBlockPtr + GET_SIZE(otherHalfBlockPtr)-sizeof(sf_footer))) = PACK(size2,0,PREV_BLOCK_ALLOCATED,0);
        // add second half to main free list
        insertIntoMainFreeList(otherHalfBlockPtr);
        return mainFreeBlk;
    }

    /* Dont split block */
    // mark block allocated
    mainFreeBlk->header = mainFreeBlk->header | THIS_BLOCK_ALLOCATED;

    // update prev block allocation status for proceeding block
    sf_block* proceedingBlkPtr = (void*)mainFreeBlk + GET_SIZE(mainFreeBlk);
    // update proceeding block header
    proceedingBlkPtr->header = proceedingBlkPtr->header | PREV_BLOCK_ALLOCATED;
    // if main free block also update footer
    if(!GET_ALLOC(proceedingBlkPtr) && !GET_IN_QUICK_LIST(proceedingBlkPtr)){
        sf_footer* proceedingFooter = (sf_footer*)((void*)proceedingBlkPtr+GET_SIZE(proceedingBlkPtr)-sizeof(sf_footer));
        *proceedingFooter = proceedingBlkPtr->header;
    }

    return mainFreeBlk; // ptr to block to allocate

}

/* Takes sentinel node of main free list as input and returns whether it is empty */
static int isMainFreeListEmpty(sf_block* block){
    return block == block->body.links.next && block == block->body.links.prev;
}

/* Inserts a free block into the right size class free list (main)*/
static void insertIntoMainFreeList(sf_block* block){
    size_t blockSize = GET_SIZE(block);
    int freeListIndex = NUM_FREE_LISTS-1;
    for(int i = 0; i < NUM_FREE_LISTS; i++){
        if(blockSize <= getFreeListSizeClassUpperBound(i)){
            freeListIndex = i;
            break;
        }
    }
    block->body.links.prev = &sf_free_list_heads[freeListIndex];
    block->body.links.next = sf_free_list_heads[freeListIndex].body.links.next;
    sf_block* oldfirst = sf_free_list_heads[freeListIndex].body.links.next;
    oldfirst->body.links.prev = block;
    sf_free_list_heads[freeListIndex].body.links.next = block;
}

/* Takes as input block to free, checks neighboring blocks for alloc, updates fields,
adjusts and adds coalesced block to main free list*/
static void* coalesce(void*bp) {
    size_t prevAlloc = GET_PREV_ALLOC(bp);
    size_t nextAlloc = GET_ALLOC(bp + GET_SIZE(bp));
    size_t size = GET_SIZE(bp);
    if(prevAlloc && nextAlloc){
        ((sf_block*)bp)->header = PACK(size,0,PREV_BLOCK_ALLOCATED,0);
        *((sf_footer*)(bp+size-sizeof(sf_footer)))=((sf_block*)bp)->header;
        insertIntoMainFreeList(bp);
        return bp;
    } else if(prevAlloc && !nextAlloc){
        removeFreeBlockFromMainList(bp+GET_SIZE(bp));
        size += GET_SIZE(bp + GET_SIZE(bp));
        ((sf_block*)bp)->header = PACK(size,0,PREV_BLOCK_ALLOCATED,0);
        *((sf_footer*)(bp+size-sizeof(sf_footer))) = ((sf_block*)bp)->header;
        insertIntoMainFreeList(bp);
    } else if(!prevAlloc && nextAlloc){
        size_t prevSize = GET_SIZE(bp-sizeof(sf_footer));
        removeFreeBlockFromMainList(bp-prevSize);
        bp = bp - prevSize;
        size += prevSize;
        ((sf_block*)bp)->header = PACK(size,0,PREV_BLOCK_ALLOCATED,0);
        *((sf_footer*)(bp+size-sizeof(sf_footer))) = ((sf_block*)bp)->header;
        insertIntoMainFreeList(bp);
    } else { // !prevAlloc && !nextALloc
        removeFreeBlockFromMainList(bp+GET_SIZE(bp));
        size_t prevSize = GET_SIZE(bp-sizeof(sf_footer));
        removeFreeBlockFromMainList(bp - prevSize);
        size_t nextSize = GET_SIZE(bp+size);
        size += prevSize;
        size += nextSize;
        bp = bp -prevSize;
        ((sf_block*)bp)->header = PACK(size,0,PREV_BLOCK_ALLOCATED,0);
        *((sf_footer*)(bp + size - sizeof(sf_footer))) = ((sf_block*)bp)->header;
        insertIntoMainFreeList(bp);
    }
    return bp;
}

/* Removes a free block from its main free list */
static void removeFreeBlockFromMainList(sf_block* block){
    sf_block* prevBlock = block->body.links.prev;
    prevBlock->body.links.next = block->body.links.next;
    sf_block* nextBlock = block->body.links.next;
    nextBlock->body.links.prev = prevBlock;
}
