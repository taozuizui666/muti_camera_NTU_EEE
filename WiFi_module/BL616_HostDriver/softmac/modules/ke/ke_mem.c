/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "co_math.h"

#include "ke_config.h"
#include "ke_env.h"
#include "ke_mem.h"

#include "softmac.h"

/*
 * DEFINES
 ****************************************************************************************
 */
#define KE_HEAP_SIZE     NX_HEAP_SIZE

/// Free memory block delimiter structure (size must be word multiple)
struct mblock_free
{
    struct mblock_free *next;   ///< Pointer to the next block
    #if CPU_WORD_SIZE == 8
    uint64_t size;              ///< Size of the current free block (including delimiter)
    #elif CPU_WORD_SIZE == 4
    uint32_t size;              ///< Size of the current free block (including delimiter)
    #elif CPU_WORD_SIZE == 2
    uint16_t size;              ///< Size of the current free block (including delimiter)
    #endif
};

/// Used memory block delimiter structure (size must be word multiple)
struct mblock_used
{
#if KE_PROFILING
    struct co_list_hdr hdr;
    uint32_t ra;
#endif
#if CPU_WORD_SIZE == 8
    uint64_t size;              ///< Size of the current free block (including delimiter)
#elif CPU_WORD_SIZE == 4
    uint32_t size;              ///< Size of the current free block (including delimiter)
#elif CPU_WORD_SIZE == 2
    uint16_t size;              ///< Size of the current free block (including delimiter)
#endif
};

/*
 * GLOBAL VARIABLES
 ****************************************************************************************
 */
/// Data area reserved for the kernel heap
uint8_t ke_mem_heap[KE_HEAP_SIZE];

/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */
struct mblock_free *ke_mem_init(void)
{
    struct mblock_free *first;

    // align first free descriptor to word boundary
    first = (struct mblock_free*)CO_ALIGN4_HI((PTR2UINT)ke_mem_heap);

    // initialize the first block
    //  + compute the size from the last aligned word before heap_end
    first->size = ((PTR2UINT)&ke_mem_heap[KE_HEAP_SIZE] & (~3)) - (PTR2UINT)first;
    first->next = NULL;

    dbg("%s, mblock first:0x%pK, 0x%x\n", __func__, first, first->size);

    // save the pointer to the first free block
    return first;
}

void ke_loop_print(void)
{
    struct mblock_free *node;
    struct mblock_free *last_node;
    PTR2UINT totalfreesize = 0;

    dbg_f("%s, loop\r\n", __func__);
    
    node = ke_env.mblock_first;
    // go through free memory blocks list
    while (node != NULL)
    {
        dbg_f("node:0x%pK\n", node);
        dbg_f("node->size:0x%x\n", node->size);

        if ((PTR2UINT)node < (PTR2UINT)ke_mem_heap || 
            (PTR2UINT)node > (PTR2UINT)ke_mem_heap+KE_HEAP_SIZE ||
            (PTR2UINT)node+node->size > (PTR2UINT)ke_mem_heap+KE_HEAP_SIZE || 
            node->size > KE_HEAP_SIZE) 
        {
            dbg_f("%s, invalid node:0x%pK exceed ke_mem_heap, size:0x%x, ke_mem_heap:0x%pK, KE_HEAP_SIZE:0x%x\n", 
                  __func__, node, node->size, ke_mem_heap, KE_HEAP_SIZE);
            
            dump_stack();
        }
    
        totalfreesize += node->size;

        last_node = node;
        // move to next block
        node = node->next;

        if (node!=NULL && (PTR2UINT)last_node + last_node->size > (PTR2UINT)node)
        {
            dbg_f("%s, last_node->size 0x%pK + 0x%x = 0x%x > 0x%pK, exceed cur node\n",
                   __func__,
                   last_node, last_node->size, 
                   (PTR2UINT)last_node + last_node->size,
                   node);

            dump_stack();
        }
    }
    
    dbg_f("%s, totalfreesize:0x%x\n", __func__, totalfreesize);

    ASSERT_ERR(totalfreesize <= KE_HEAP_SIZE);
}

void ke_mem_deinit(void)
{
    dbg("%s, call loop\n", __func__);
    //ke_loop_print();
}

void ke_check_node(struct mblock_free *node)
{
    dbg("%s, loop check, node:0x%pK, size:0x%x, ke_mem_heap:0x%pK, KE_HEAP_SIZE:0x%x\r\n", 
          __func__,
          node, node->size, ke_mem_heap, KE_HEAP_SIZE);

    if ((PTR2UINT)node < (PTR2UINT)ke_mem_heap || 
        (PTR2UINT)node > (PTR2UINT)ke_mem_heap+KE_HEAP_SIZE ||
        (PTR2UINT)node+node->size > (PTR2UINT)ke_mem_heap+KE_HEAP_SIZE || 
        node->size > KE_HEAP_SIZE) 
    {
        dbg_f("invalid node:0x%pK, size:0x%x, ke_mem_heap:0x%pK, KE_HEAP_SIZE:0x%x\n", 
              node, node->size, ke_mem_heap, KE_HEAP_SIZE);
        
        dump_stack();

        ke_loop_print();
    }
}

void *ke_malloc(uint32_t size)
{
    struct mblock_free *node, *found;
    struct mblock_used *alloc;
    uint32_t totalsize;
    unsigned long flags;

    #if KE_PROFILING
    uint32_t totalfreesize = 0;
    #endif //KE_PROFILING

    // initialize the pointers
    found = NULL;

    // compute overall block size (including requested size PLUS descriptor size)
    totalsize = CO_ALIGN4_HI(size) + sizeof(struct mblock_used);

    ke_msg_check();

    // sanity check: the totalsize should be large enough to hold free block descriptor
    ASSERT_ERR(totalsize >= sizeof(struct mblock_free));

    dbg_ke("ke_malloc totalsize:0x%x\n", totalsize);

    spin_lock_irqsave(&ke_env.bl_hw->ke_mem_lock, flags);

    node = ke_env.mblock_first;
    // go through free memory blocks list
    while (node != NULL)
    {
        dbg("node:0x%pK\n", node);
        dbg("node->size:0x%x\n", node->size);

        ke_check_node(node);

        #if KE_PROFILING
        totalfreesize += node->size;
        #endif //KE_PROFILING

        // check if there is enough room in this free block
        if (node->size >= (totalsize + sizeof(struct mblock_free)))
        {
            // if a match was already found, check if this one is smaller
            if ((found == NULL) || (found->size > node->size))
            {
                found = node;
            }
        }
        
        // move to next block
        node = node->next;
    }

    // sanity check: allocation should always succeed
    if (found == NULL) {
        dbg_f("ke_malloc found is NULL, expect size:0x%x, call loop\n", size);

        bl_dump(ke_mem_heap, sizeof(ke_mem_heap));

        ke_loop_print();

        ASSERT_ERR(found != NULL);
    
        spin_unlock_irqrestore(&ke_env.bl_hw->ke_mem_lock, flags);

        return found;
    }

    ke_msg_check();

    #if KE_PROFILING
    if(ke_env.max_heap_used <= KE_HEAP_SIZE - totalfreesize)
        ke_env.max_heap_used = KE_HEAP_SIZE - totalfreesize;
    #endif //KE_PROFILING

    // found a free block that matches, subtract the allocation size from the
    // free block size. If equal, the free block will be kept with 0 size... but
    // moving it out of the linked list is too much work.
    found->size -= totalsize;

    // compute the pointer to the beginning of the free space
    alloc = (struct mblock_used*) ((PTR2UINT)found + found->size);
    // save the size of the allocated block (use low bit to indicate mem type)
    alloc->size = totalsize;

    dbg_ke("%s, done alloc:0x%pK, size:0x%x, found:0x%pK, found->size:0x%x, sizeof:%d %d %d\n",
          __func__, alloc, totalsize, found, found->size,
          sizeof(alloc), sizeof(found), sizeof(found->size));

    ke_check_node(((struct mblock_free *)(alloc+1)) - 1);

    //dbg("%s, call loop\r\n", __func__);
    //ke_loop_print();

#if KE_PROFILING
    alloc->ra = 0;
    co_list_push_back(&ke_env.heap_used, &alloc->hdr);
#endif

    // move to the user memory space
    alloc++;

    spin_unlock_irqrestore(&ke_env.bl_hw->ke_mem_lock, flags);

    return (void*)alloc;
}

void *ke_malloc_check(uint32_t size)
{
    struct mblock_free *node, *found;
    uint32_t totalsize;
    #if KE_PROFILING
    struct mblock_used *used_node;
    #endif
    uint32_t cnt_bigger = 0;
    uint32_t size_bigger = 0;
    unsigned long flags;

    // initialize the pointers
    found = NULL;

    ke_msg_check();

    // compute overall block size (including requested size PLUS descriptor size)
    totalsize = CO_ALIGN4_HI(size) + sizeof(struct mblock_used);
    // sanity check: the totalsize should be large enough to hold free block descriptor
    ASSERT_ERR(totalsize >= sizeof(struct mblock_free));

    dbg("ke_malloc_check totalsize:0x%x\n", totalsize);
    //dump_stack();

    spin_lock_irqsave(&ke_env.bl_hw->ke_mem_lock, flags);

    node = ke_env.mblock_first;
    // go through free memory blocks list
    while (node != NULL)
    {
        dbg("node:0x%pK\n", node);
        
        ke_check_node(node);
        
        dbg("node->size:0x%x\n", node->size);

        // check if there is enough room in this free block
        if (node->size >= (totalsize + sizeof(struct mblock_free)))
        {
            // if a match was already found, check if this one is smaller
            if ((found == NULL) || (found->size > node->size))
            {
                found = node;
            }

            size_bigger += node->size;
            cnt_bigger++;
        }

        // move to next block
        node = node->next;
    }

    ke_msg_check();

    ke_loop_print();

    if (found == NULL || size_bigger < 1500) {
        found = NULL;
        node = ke_env.mblock_first;

        dbg("%s, want size:0x%x, 0x%x, 0x%x\r\n", __func__,
              size, cnt_bigger, size_bigger);
        
        // go through free memory blocks list
        while (node != NULL)
        {
            dbg("0x%pK:%d\r\n", node, node->size);
            // move to next block
            node = node->next;
        }

        #if KE_PROFILING
        dbg("used\r\n");
        
        used_node = &ke_env.heap_used;
        while (used_node != NULL) {
            dbg("used_node:0x%pK, size:%d, ra:0x%x\r\n",
                 used_node, used_node->size, used_node->ra);
            used_node = used_node->hdr.next;
        }
        #endif
    }

    spin_unlock_irqrestore(&ke_env.bl_hw->ke_mem_lock, flags);

    return (void*)found;
}

void ke_mem_check(void)
{
    unsigned long flags;

    dbg_f("ke_mem_check\n");
    
    spin_lock_irqsave(&ke_env.bl_hw->ke_mem_lock, flags);

    ke_loop_print();
    
    spin_unlock_irqrestore(&ke_env.bl_hw->ke_mem_lock, flags);
}

void ke_free(void* mem_ptr)
{
    struct mblock_used *freed;
    struct mblock_free *node, *prev_node, *next_node;
    uint32_t size;
    unsigned long flags;

    spin_lock_irqsave(&ke_env.bl_hw->ke_mem_lock, flags);

    // point to the block descriptor (before user memory so decrement)
    freed = ((struct mblock_used *)mem_ptr) - 1;

    // point to the first node of the free elements linked list
    size = freed->size;
    prev_node = NULL;

    ke_check_node(((struct mblock_free *)mem_ptr) - 1);
    //bl_dump_data(ke_mem_heap, sizeof(ke_mem_heap));

    // sanity checks
    ASSERT_ERR(mem_ptr != NULL);
    ASSERT_ERR((PTR2UINT)mem_ptr > (PTR2UINT)ke_env.mblock_first);
    ASSERT_ERR((PTR2UINT)mem_ptr < (PTR2UINT)ke_mem_heap+KE_HEAP_SIZE);

    dbg_ke("%s, free:0x%pK, size:0x%x\n", __func__, freed, size);

    node = ke_env.mblock_first;

#if KE_PROFILING
    co_list_extract(&ke_env.heap_used, freed);
#endif

    while (node != NULL)
    {
        // check if the freed block is right after the current block
        if ((PTR2UINT)freed == ((PTR2UINT)node + node->size))
        {
            dbg("%s, node:0x%pK, node->size:0x%x\n", __func__, node, node->size);
            
            // append the freed block to the current one
            node->size += size;

            // check if this merge made the link between free blocks
            if ((PTR2UINT)node->next == ((PTR2UINT)node + node->size))
            {
                dbg("%s, node:0x%pK, node->size:0x%x node->next->size:0x%x\n",
                      __func__, node, node->size, node->next->size);

                next_node = node->next;
                // add the size of the next node to the current node
                node->size += next_node->size;
                // update the next of the current node
                node->next = next_node->next;
            }
            goto free_end;
        }
        else if ((PTR2UINT)freed < (PTR2UINT)node)
        {
            // sanity check: can not happen before first node
            ASSERT_ERR(prev_node != NULL);

            // update the next pointer of the previous node
            prev_node->next = (struct mblock_free*)freed;

            if ((PTR2UINT)prev_node + prev_node->size > (PTR2UINT)freed)
            {
                dbg_f("%s, prev_node->size 0x%pK + 0x%x = 0x%x > 0x%pK, exceed freed node, call loop\n",
                       __func__,
                       prev_node, prev_node->size, 
                       (PTR2UINT)prev_node + prev_node->size,
                       freed);

                dump_stack();

                ke_loop_print();
            }

            // check if the released node is right before the free block
            if (((PTR2UINT)freed + size) == (PTR2UINT)node)
            {
                dbg("%s, node:0x%x, node->size:0x%x ((PTR2UINT)freed + size):0x%x\n",
                      __func__, (PTR2UINT)node, node->size, ((PTR2UINT)freed + size));

                // merge the two nodes
                ((struct mblock_free*)freed)->next = node->next;
                ((struct mblock_free*)freed)->size = node->size +
                                            (PTR2UINT)node - (PTR2UINT)freed;
            }
            else
            {
                dbg("%s, freed->next=node\n", __func__);
                
                // insert the new node
                ((struct mblock_free*)freed)->next = node;
                ((struct mblock_free*)freed)->size = size;
            }
            goto free_end;
        }

        // move to the next free block node
        prev_node = node;
        node = node->next;
    }
    
    // if reached here, freed block is after last free block and not contiguous
    prev_node->next = (struct mblock_free*)freed;
    ((struct mblock_free*)freed)->next = NULL;
    ((struct mblock_free*)freed)->size = size;

free_end:

    ke_msg_check();

    dbg("free done, call loop\n");    
    //ke_loop_print();

    spin_unlock_irqrestore(&ke_env.bl_hw->ke_mem_lock, flags);
}


