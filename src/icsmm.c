/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 * If you want to make helper functions, put them in helpers.c
 */
#include "icsmm.h"
#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

ics_free_header *freelist_head = NULL;
void *pro = NULL;

void *ics_malloc(size_t size) { 
    // If size is greater than 4 pages of memory return NULL
    if (size > (4096 * 4)) {
        return NULL;
    }
    // If this is the first request, initialize the memory with prologue, large block, epilogue
    if (freelist_head == NULL) {
        // Get a page of memory
        void *new_page = ics_inc_brk();
        pro = new_page;
        // Create prologue: 8 bytes, ics_footer, block_size = 0, a = 1
        ((ics_footer*)(new_page))->block_size = 0;
        ((ics_footer*)(new_page))->block_size |= 1;
        ((ics_footer*)(new_page))->fid = FID;
        // Create epilogue: 8 bytes, ics_header, block_size = 0, padding_amount = 0
        ((ics_header*)(new_page + (4096 - 8)))->block_size = 0;
        ((ics_header*)(new_page + (4096 - 8)))->padding_amount = 0;
        ((ics_header*)(new_page + (4096 - 8)))->block_size |= 1;
        ((ics_header*)(new_page + (4096 - 8)))->hid = HID;
        // Set freelist_head to block after prologue and create one large block
        freelist_head = new_page + sizeof(ics_footer);
        freelist_head->header.block_size = 4096 - 16;
        freelist_head->header.hid = HID;
        freelist_head->header.padding_amount = 0;
        freelist_head->prev = NULL;
        freelist_head->next = NULL;
        // Create footer
        ((ics_footer*)((void*)freelist_head + 4096 - 16 - sizeof(ics_footer)))->block_size = 4096 - 16;
        ((ics_footer*)((void*)freelist_head + 4096 - 16 - sizeof(ics_footer)))->fid = FID;
    }
    int overhead_size = 16;
    // Search for best fit block in freelist
    ics_free_header *iter = freelist_head;
    while (iter) {
        size_t rounded_size = roundPayloadSize16(size);
        int padding = rounded_size - size;
        uint16_t actual_block_size = iter->header.block_size & -4;
        if ((iter->header.block_size & -4) >= (rounded_size + overhead_size)) {
             // Check next block if better fit
            if ((iter->next != NULL) && ((iter->next->header.block_size & -4) >= rounded_size + overhead_size)) {
                 iter  = iter->next;
                 continue;
            }
            // Remove from list - check if next, prev are NULL
            if (freelist_head == iter) {
                freelist_head = iter->next;
            }
            if ((iter->next == NULL) && (iter->prev != NULL)) {
                iter->prev->next = NULL;
            } else if ((iter->prev == NULL) && (iter->next != NULL)) {
                iter->next->prev = NULL;
            } else if ((iter->next != NULL) && (iter->prev != NULL)) {
                iter->next->prev = iter->prev;
                iter->prev->next = iter->next;
            } else {
                freelist_head = NULL;
            }
            // Update head of list
            ics_free_header *iter3 = freelist_head;
            while (iter3) {
                freelist_head = iter3;
                iter3 = iter3->prev;
            }
             // Checking if block can be split without splinters
            if (iter->header.block_size - (rounded_size + overhead_size) >= 32) {
                // Split block and add new block to the free list 
                // Find starting address of new block
                void *new_block = (void*)iter + (rounded_size + overhead_size);
                // Update block_size in header and footer for block to be alloced
                iter->header.block_size = rounded_size + overhead_size;
                ((ics_footer*)((void*)iter + ((rounded_size + overhead_size) - sizeof(ics_footer))))->block_size = rounded_size + overhead_size;
                // Create header and footer in new_block - set padding to 0 - ignore a&p bits (0 by default)
                ((ics_header*)new_block)->block_size = actual_block_size - (rounded_size + overhead_size);
                ((ics_header*)new_block)->hid = HID;
                ((ics_header*)new_block)->padding_amount = 0;
                ((ics_footer*)(new_block + ((ics_header*)new_block)->block_size - sizeof(ics_footer)))->block_size = ((ics_header*)new_block)->block_size;
                ((ics_footer*)(new_block + ((ics_header*)new_block)->block_size - sizeof(ics_footer)))->fid = FID;
                // Find place for new block in free list - largest to smallest block_size
                ((ics_free_header*)new_block)->next = NULL;
                ((ics_free_header*)new_block)->prev = NULL;
                if (freelist_head == NULL) {
                    freelist_head = ((ics_free_header*)new_block);
                } else {
                    ics_free_header *iter2 = freelist_head;
                    while (1) {
                        if ((((ics_header*)new_block)->block_size & -4) >= (iter2->header.block_size & -4)) {
                            // Insert new_block into free list
                            ((ics_free_header*)new_block)->prev = iter2->prev;
                            ((ics_free_header*)new_block)->next = iter2;
                            if (iter2->prev != NULL) {
                                iter2->prev->next = ((ics_free_header*)new_block);
                            }
                            iter2->prev = ((ics_free_header*)new_block);
                            break;
                        }
                        if (iter2->next == NULL) {
                            iter2->next = ((ics_free_header*)new_block);
                            ((ics_free_header*)new_block)->prev = iter2;
                            ((ics_free_header*)new_block)->next = NULL;
                            break;
                        }
                        iter2 = iter2->next;
                    }
                    // Update head of list if needed
                    iter2 = freelist_head;
                    while (iter2) {
                        freelist_head = iter2;
                        iter2 = iter2->prev;
                    }
                }
            }
            actual_block_size = iter->header.block_size & -4;
            /* Update header and footer for alloc block, delete from list, and return address */
            // Update header padding
            iter->header.padding_amount = padding;
            // Set allocated bit
            iter->header.block_size |= 1;
            // Set padding bit if padding
            if (padding > 0) {
                iter->header.block_size |= 2;
            }
            // Create/update footer
            ((ics_footer*)((void*)iter + (actual_block_size - sizeof(ics_footer))))->block_size = actual_block_size;
            ((ics_footer*)((void*)iter + (actual_block_size - sizeof(ics_footer))))->fid = FID;
            ((ics_footer*)((void*)iter + (actual_block_size - sizeof(ics_footer))))->block_size |= 1;
            if (padding > 0) {
                ((ics_footer*)((void*)iter + (actual_block_size - sizeof(ics_footer))))->block_size |= 2;
            }
            // Return payload address
            return ((void*)iter + sizeof(ics_header));
        }
        iter = iter->next;
    } // Loop end


    // Request more memory
    void *more_mem = ics_inc_brk();
    if (more_mem < 0) {
        errno = ENOMEM;
        return NULL;
    }
    // Find epilogue
    void *epilogue = more_mem - sizeof(ics_header);
    // Check if block before epilogue is free
    if (((ics_footer*)(epilogue - sizeof(ics_footer)))->block_size & 1) {  // Isnt free
        // Make more_mem a new block and insert in free list
        // Create header
        ((ics_header*)more_mem)->block_size = 4096;
        ((ics_header*)more_mem)->hid = HID;
        ((ics_header*)more_mem)->padding_amount = 0;
        // Create footer
        ((ics_footer*)(more_mem + (4096 - sizeof(ics_footer))))->block_size = 4096;
        ((ics_footer*)(more_mem + (4096 - sizeof(ics_footer))))->fid = FID;
        // Insert into list
        ics_free_header *iter2 = freelist_head;
        while (iter2) {
            if (((ics_header*)more_mem)->block_size >= (iter2->header.block_size & -4)) {
                // Insert new_block into free list
                ((ics_free_header*)more_mem)->prev = iter2->prev;
                ((ics_free_header*)more_mem)->next = iter2;
                iter2->prev = ((ics_free_header*)more_mem);
            }
            iter2 = iter2->next;
        }
        // Update head of list if needed
        iter2 = freelist_head;
        while (iter2) {
            freelist_head = iter2;
            iter2 = iter2->prev;
        }
    } else {    // Is free
        // Coalesce with last free block in heap
        // Create epilogue: 8 bytes, ics_header, block_size = 0, padding_amount = 0
        ((ics_header*)(more_mem + (4096 - 8)))->block_size = 0;
        ((ics_header*)(more_mem + (4096 - 8)))->padding_amount = 0;
        ((ics_header*)(more_mem + (4096 - 8)))->block_size |= 1;
        ((ics_header*)(more_mem + (4096 - 8)))->hid = HID;
        // Find last free block
        ics_header *last_free_block = epilogue - ((ics_footer*)(epilogue - sizeof(ics_footer)))->block_size;
        // Update header
        last_free_block->block_size = last_free_block->block_size + 4096;
        // Update footer
        ((ics_footer*)(((void*)last_free_block) + last_free_block->block_size - sizeof(ics_footer)))->block_size = last_free_block->block_size;
        ((ics_footer*)(((void*)last_free_block) + last_free_block->block_size - sizeof(ics_footer)))->fid = FID;
        // Remove from list - check if next, prev are NULL
        if (freelist_head == (ics_free_header*)last_free_block) {
            freelist_head = ((ics_free_header*)last_free_block)->next;
        }
        if ((((ics_free_header*)last_free_block)->next == NULL) && (((ics_free_header*)last_free_block)->prev != NULL)) {
            ((ics_free_header*)last_free_block)->prev->next = NULL;
        } else if ((((ics_free_header*)last_free_block)->prev == NULL) && (((ics_free_header*)last_free_block)->next != NULL)) {
            ((ics_free_header*)last_free_block)->next->prev = NULL;
        } else if ((((ics_free_header*)last_free_block)->next != NULL) && (((ics_free_header*)last_free_block)->prev != NULL)) {
            ((ics_free_header*)last_free_block)->next->prev = ((ics_free_header*)last_free_block)->prev;
            ((ics_free_header*)last_free_block)->prev->next = ((ics_free_header*)last_free_block)->next;
        } else {
            freelist_head = NULL;
        }
        // Reinsert into free list
        if (freelist_head == NULL) {
            freelist_head = ((ics_free_header*)last_free_block);
        } else {
            ics_free_header *iter2 = freelist_head;
            while (iter2) {
                if (last_free_block->block_size >= (iter2->header.block_size & -4)) {
                    // Insert new_block into free list
                    ((ics_free_header*)last_free_block)->prev = iter2->prev;
                    ((ics_free_header*)last_free_block)->next = iter2;
                    iter2->prev = ((ics_free_header*)last_free_block);
                }
                iter2 = iter2->next;
            }
            // Update head of list if needed
            iter2 = freelist_head;
            while (iter2) {
                freelist_head = iter2;
                iter2 = iter2->prev;
            }
        }
    }
    return ics_malloc(size);
}

void *ics_realloc(void *ptr, size_t size) {
    /* Check if ptr is alloced space in heap */
    ptr -= sizeof(ics_header);
    // Check ptr is in between epilogue and prologue
    void *epilogue = pro;
    while (epilogue) {
        if ((((ics_header*)epilogue)->hid == HID) && ((((ics_header*)epilogue)->block_size & -4) == 0) && ((((ics_header*)epilogue)->block_size) & 1)) {
            break;
        }
        epilogue++;
    }
    void *prologue = pro;
    if (!((ptr < epilogue) && (ptr > prologue))) {
        errno = EINVAL;
        return NULL;
    }
    // Check hid and fid in header and footer
    if ((((ics_header*)ptr)->hid != HID) || (((ics_footer*)(ptr + (((ics_header*)ptr)->block_size & -4) - sizeof(ics_footer)))->fid != FID)) {
        errno = EINVAL;
        return NULL;
    }
    // Check if block_size in header and footer are equal (includes alloc bit and padding bit)
    if (((ics_header*)ptr)->block_size != ((ics_footer*)(ptr + (((ics_header*)ptr)->block_size & -4) - sizeof(ics_footer)))->block_size) {
        errno = EINVAL;
        return NULL;
    }
    // Check if padding is > 0 if padding bit is set
    if ((((ics_header*)ptr)->block_size & 2) && ((ics_header*)ptr)->padding_amount <= 0) {
        errno = EINVAL;
        return NULL;
    }
    // Free ptr and return NULL if size is 0
    if (size == 0) {
        ics_free(ptr + sizeof(ics_header));
        return NULL;
    }
    // Checks if size is less than ptr size
    if (size < ((((ics_header*)ptr)->block_size & -4) - 16)) {  // Requesting less
        // Check if size reduction would cause splinters
        if ((((ics_header*)ptr)->block_size & -4) - size >= 32) {
            // Create new free block and add to list
            int old_size = ((ics_header*)ptr)->block_size;
            // Update current block header and footer
            ((ics_header*)ptr)->block_size = size;
            ((ics_header*)ptr)->block_size |= 1;
            if (((ics_header*)ptr)->padding_amount > 0) {
                ((ics_header*)ptr)->block_size |= 2;
            }
            ((ics_footer*)(ptr + size - sizeof(ics_footer)))->block_size = size;
            ((ics_footer*)(ptr + size - sizeof(ics_footer)))->block_size |= 1;
            if (((ics_header*)ptr)->padding_amount > 0) {
                ((ics_footer*)(ptr + size - sizeof(ics_footer)))->block_size |= 2;
            }
            ((ics_footer*)(ptr + size - sizeof(ics_footer)))->fid = FID;
            // Create new block
            void *new_block = ptr + size;
            // Create header and footer for new_block
            ((ics_header*)new_block)->block_size = old_size - size;
            ((ics_header*)new_block)->hid = HID;
            ((ics_footer*)(new_block + old_size - size - sizeof(ics_footer)))->block_size = old_size - size;
            ((ics_footer*)(new_block + old_size - size - sizeof(ics_footer)))->fid = FID;
            ics_free(new_block);
        }
        return ptr + sizeof(ics_header);
    } else {    // Requesting more
        // Request more memory from malloc
        void *new_block = ics_malloc(size);
        // Copy data from ptr to new_block
        memcpy(new_block, ptr + sizeof(ics_header), size);
        ics_free(ptr + sizeof(ics_header));
        return new_block;
    }
}

int ics_free(void *ptr) {
    /* Check if ptr is alloced space in heap */
    ptr -= sizeof(ics_header);
    // Check ptr is in between epilogue and prologue
    void *epilogue = pro;
    while (epilogue) {
        if ((((ics_header*)epilogue)->hid == HID) && ((((ics_header*)epilogue)->block_size & -4) == 0) && ((((ics_header*)epilogue)->block_size) & 1)) {
            break;
        }
        epilogue++;
    }
    void *prologue = pro;
    if (!((ptr < epilogue) && (ptr > prologue))) {
        errno = EINVAL;
        return -1;
    }
    // Check hid and fid in header and footer
    if ((((ics_header*)ptr)->hid != HID) || (((ics_footer*)(ptr + (((ics_header*)ptr)->block_size & -4) - sizeof(ics_footer)))->fid != FID)) {
        errno = EINVAL;
        return -1;
    }
    // Check if block_size in header and footer are equal (includes alloc bit and padding bit)
    if (((ics_header*)ptr)->block_size != ((ics_footer*)(ptr + (((ics_header*)ptr)->block_size & -4) - sizeof(ics_footer)))->block_size) {
        errno = EINVAL;
        return -1;
    }
    // Check if padding is > 0 if padding bit is set
    if ((((ics_header*)ptr)->block_size & 2) && ((ics_header*)ptr)->padding_amount <= 0) {
        errno = EINVAL;
        return -1;
    }
    ics_free_header *new_block;
    // Check if previous block is free
    if (((ics_footer*)(ptr - sizeof(ics_footer)))->block_size & 1) {  // Not free
        /* Make block free and add to free list */
        new_block = ((ics_free_header*)ptr);
        // Reset alloc bit in header
        new_block->header.block_size &= -2;
        // Reset alloc bit in footer
        ((ics_footer*)(ptr + (new_block->header.block_size & -4) - sizeof(ics_footer)))->block_size &= -2;
    } else {    // Free
        /* Coalesce with previous block in memory */
        // Take the previous block out of the list
        void *prev_block = ptr - (((ics_footer*)(ptr - sizeof(ics_footer)))->block_size & -4);
        // Make new_block = prev_block
        new_block = ((ics_free_header*)prev_block);
        if (freelist_head == new_block) {
            freelist_head = new_block->next;
        }
        if ((new_block->next == NULL) && (new_block->prev != NULL)) {
            new_block->prev->next = NULL;
        } else if ((new_block->prev == NULL) && (new_block->next != NULL)) {
            new_block->next->prev = NULL;
        } else if ((new_block->next != NULL) && (new_block->prev != NULL)) {
            new_block->next->prev = new_block->prev;
            new_block->prev->next = new_block->next;
        } else {
            freelist_head = NULL;
        }
        // Update head of list
        ics_free_header *iter2 = freelist_head;
        while (iter2) {
            freelist_head = iter2;
            iter2 = iter2->prev;
        }
        // Coalesce prev_block and ptr
        // Update header
        new_block->header.block_size = (new_block->header.block_size & -4) + (((ics_header*)ptr)->block_size & -4);
        if ((new_block->header.padding_amount > 0) || (((ics_header*)ptr)->padding_amount > 0)) {
            ((ics_header*)prev_block)->block_size |= 2;
        }
        // Update footer
        ((ics_footer*)(prev_block + (new_block->header.block_size & -4) - sizeof(ics_footer)))->block_size = new_block->header.block_size;
        ((ics_footer*)(prev_block + (new_block->header.block_size & -4) - sizeof(ics_footer)))->fid = FID;
    }
    new_block->next = NULL;
    new_block->prev = NULL;
    // Insert into free list
    if (freelist_head == NULL) {
        freelist_head = new_block;
    } else {
        ics_free_header *iter = freelist_head;
        while (1) {
            if ((new_block->header.block_size & -4) >= (iter->header.block_size & -4)) {
                // Insert new_block into free list
                new_block->prev = iter->prev;
                new_block->next = iter;
                if (iter->prev != NULL) {
                    iter->prev->next = new_block;
                }
                iter->prev = new_block;
                break;
            }
            if (iter->next == NULL) {
                iter->next = new_block;
                new_block->prev = iter;
                new_block->next = NULL;
                break;
            }
            iter = iter->next;
        }
        // Update head of list if needed
        iter = freelist_head;
        while (iter) {
            freelist_head = iter;
            iter = iter->prev;
        }
    }

    return 0;
}
