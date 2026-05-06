/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "bl_ipc_compat.h"

#include "ke_config.h"
#include "co_list.h"

/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */
void co_list_init(struct co_list *list)
{
    list->first = NULL;
    list->last = NULL;
}

void co_list_pool_init(struct co_list *list, void *pool,
                          size_t elmt_size, uint32_t elmt_cnt,
                          void *default_value)
{
    uint32_t i;

    // initialize the free list relative to the pool
    co_list_init(list);

    // Add each element of the pool to this list, and init them one by one
    for (i = 0; i < elmt_cnt; i++)
    {
        if (default_value)
        {
            memcpy(pool, default_value, elmt_size);
        }
        co_list_push_back(list, (struct co_list_hdr *) pool);

        // move to the next pool element
        pool = (void *)((uint8_t *)pool + (uint32_t)elmt_size);
    }
}

void co_list_push_back(struct co_list *list,
                             struct co_list_hdr *list_hdr)
{
    // Sanity check
    ASSERT_ERR(list_hdr != NULL);

    // check if list is empty
    if (co_list_is_empty(list))
    {
        // list empty => pushed element is also head
        list->first = list_hdr;
    }
    else
    {
        // list not empty => update next of last
        list->last->next = list_hdr;
    }

    // add element at the end of the list
    list->last = list_hdr;
    list_hdr->next = NULL;
}

void co_list_push_back_sublist(struct co_list *list, 
                                       struct co_list_hdr *first_hdr, 
                                       struct co_list_hdr *last_hdr)
{
    // Sanity check
    ASSERT_ERR(first_hdr != NULL);
    ASSERT_ERR(last_hdr != NULL);

    // check if list is empty
    if (co_list_is_empty(list))
    {
        // list empty => pushed element is also head
        list->first = first_hdr;
    }
    else
    {
        // list not empty => update next of last
        list->last->next = first_hdr;
    }

    // Update last pointer
    list->last = last_hdr;
    last_hdr->next = NULL;
}

void co_list_push_front(struct co_list *list,
                             struct co_list_hdr *list_hdr)
{
    // Sanity check
    ASSERT_ERR(list_hdr != NULL);

    // check if list is empty
    if (co_list_is_empty(list))
    {
        // list empty => pushed element is also head
        list->last = list_hdr;
    }

    // add element at the beginning of the list
    list_hdr->next = list->first;
    list->first = list_hdr;
}

struct co_list_hdr *co_list_pop_front(struct co_list *list)
{
    struct co_list_hdr *element;

    // check if list is empty
    element = list->first;
    if (element != NULL)
    {
        // The list isn't empty : extract the first element
        list->first = list->first->next;
        if (list->last == element)
            list->last = NULL;
    }

    return element;
}

bool co_list_extract(struct co_list *list, struct co_list_hdr *list_hdr)
{
    struct co_list_hdr *scan_list;

    // sanity check
    ASSERT_ERR(list != NULL);

    scan_list = list->first;

    // Check if list is empty or not
    if (scan_list == NULL)
        return false;

    // check if searched element is first
    if (scan_list == list_hdr)
    {
        // Extract first element
        list->first = scan_list->next;
        if (list->last == scan_list)
            list->last = NULL;

        return true;
    }
    else
    {
        // Look for the element in the list
        while ((scan_list->next != NULL) && (scan_list->next != list_hdr))
        {
            scan_list = scan_list->next;
        }

        // Check if element was found in the list
        if (scan_list->next != NULL)
        {
            // check if the removed element is the last in the list
            if (list->last == list_hdr)
            {
                // Last element will be extracted
                list->last = scan_list;
            }
            // Extract the element from the list
            scan_list->next = list_hdr->next;

            return true;
        }
    }

    return false;
}

void co_list_extract_after(struct co_list *list, 
                               struct co_list_hdr *elt_ref_hdr, 
                               struct co_list_hdr *elt_to_rem_hdr)
{
    // sanity check
    ASSERT_ERR(list != NULL);
    ASSERT_ERR(elt_to_rem_hdr != NULL);

    // Check if the element is first
    if(elt_ref_hdr == NULL)
    {
        ASSERT_ERR(elt_to_rem_hdr == list->first);

        // The list isn't empty : extract the first element
        list->first = list->first->next;
    }
    else
    {
        ASSERT_ERR(elt_to_rem_hdr == elt_ref_hdr->next);

        // Extract element
        elt_ref_hdr->next = elt_to_rem_hdr->next;
    }

    // Check if the element is last
    if(elt_to_rem_hdr == list->last)
    {
        // Update last pointer
        list->last = elt_ref_hdr;
    }
}

void co_list_extract_sublist(struct co_list *list, 
                                  struct co_list_hdr *ref_hdr, 
                                  struct co_list_hdr *last_hdr)
{
    // sanity check
    ASSERT_ERR(list != NULL);
    ASSERT_ERR(last_hdr != NULL);

    // Check if the element is first
    if(ref_hdr == NULL)
    {
        // Extract the elements
        list->first = last_hdr->next;
    }
    else
    {
        // Extract the elements
        ref_hdr->next = last_hdr->next;
    }

    // Check if the element is last
    if(last_hdr == list->last)
    {
        // Reference element becomes last
        list->last = ref_hdr;
    }

    // Unlink the last extracted element
    last_hdr->next = NULL;
}

bool co_list_find(struct co_list *list, struct co_list_hdr *list_hdr)
{
    struct co_list_hdr *tmp_list_hdr;

    // Go through the list to find the element
    tmp_list_hdr = list->first;
    while((tmp_list_hdr != list_hdr) && (tmp_list_hdr != NULL))
    {
        tmp_list_hdr = tmp_list_hdr->next;
    }

    return (tmp_list_hdr == list_hdr);
}

void co_list_merge(struct co_list *list1, struct co_list *list2)
{
    // Sanity check: list2 is not supposed to be empty
    ASSERT_ERR(!co_list_is_empty(list2));

    // just copy list elements
    if(co_list_is_empty(list1))
    {
        list1->first = list2->first;
        list1->last  = list2->last;
    }
    // merge lists
    else
    {
        // Append list2 to list1
        list1->last->next = list2->first;
        list1->last = list2->last;

    }

    // Empty list2
    list2->first = NULL;
}

uint32_t co_list_cnt(const struct co_list *const list)
{
    uint32_t cnt = 0;
    struct co_list_hdr *elt = co_list_pick(list);

    // Go through the list to count the number of elements
    while (elt != NULL)
    {
        cnt++;
        elt = co_list_next(elt);
    }

    return(cnt);
}

/**
 ****************************************************************************************
 * @brief Insert an element in a sorted list.
 *
 * This primitive use a comparison function from the parameter list to select where the
 * element must be inserted.
 *
 * @param[in]  list     Pointer to the list.
 * @param[in]  element  Pointer to the element to insert.
 * @param[in]  cmp      Comparison function (return true if first element has to be inserted
 *                      before the second one).
 *
 * @return              Pointer to the element found and removed (NULL otherwise).
 ****************************************************************************************
 */
void co_list_insert(struct co_list * const list,
                       struct co_list_hdr * const element,
                       bool (*cmp)(struct co_list_hdr const *elementA,
                                struct co_list_hdr const *elementB))
{
    struct co_list_hdr *prev = NULL;
    struct co_list_hdr *scan = list->first;

    for(;;)
    {
        // scan the list until the end or cmp() returns true
        if (scan)
        {
            if (cmp(element, scan))
            {
                // insert now
                break;
            }
            prev = scan;
            scan = scan->next;
        }
        else
        {
            // end of list
            list->last = element;
            break;
        }
    }

    element->next = scan;

    if (prev)
    {
        // second or more
        prev->next = element;
    }
    else
    {
        // first message
        list->first = element;
    }
}

void co_list_insert_after(struct co_list * const list,
                              struct co_list_hdr * const prev_element,
                              struct co_list_hdr * const element)
{
    struct co_list_hdr *scan = list->first;

    if (prev_element == NULL)
    {
        // Insert the element in front on the list
        co_list_push_front(list, element);
    }
    else
    {
        // Look for prev_element in the list
        while (scan)
        {
            if (scan == prev_element)
            {
                break;
            }

            // Get next element
            scan = scan->next;
        }

        // If prev_element has been found, insert element
        if (scan)
        {
            element->next = prev_element->next;
            prev_element->next = element;

            if (list->last == prev_element)
            {
                list->last = element;
            }
        }
    }
}

void co_list_insert_after_fast(struct co_list * const list,
                                    struct co_list_hdr * const prev_element,
                                    struct co_list_hdr * const element)
{
    if (prev_element == NULL)
    {
        // Insert the element in front on the list
        co_list_push_front(list, element);
    }
    else
    {
        element->next = prev_element->next;
        prev_element->next = element;

        if (list->last == prev_element)
            list->last = element;
    }
}

void co_list_insert_before(struct co_list * const list,
                                struct co_list_hdr * const next_element,
                                struct co_list_hdr * const element)
{
    if (next_element == NULL)
    {
        // Insert the element at the end of the list
        co_list_push_back(list, element);
    }
    else if (next_element == list->first)
    {
        // Insert the element in front of the list
        co_list_push_front(list, element);
    }
    else
    {
        struct co_list_hdr *scan = list->first;

        // Look for next_element in the list
        while (scan)
        {
            if (scan->next == next_element)
            {
                break;
            }

            // Get next element
            scan = scan->next;
        }

        // Insert element after scan
        if (scan)
        {
            element->next = next_element;
            scan->next = element;
        }
    }
}

void co_list_concat(struct co_list *list1, struct co_list *list2)
{
    // If list2 is empty, don't do anything
    if (list2->first != NULL)
    {
        // Check if list1 is empty
        if (list1->first == NULL)
        {
            // If list1 is empty, list1 becomes list2
            *list1 = *list2;
        }
        else
        {
            // Otherwise, append list2 to list1
            list1->last->next = list2->first;
            list1->last = list2->last;
        }
        // Clear list2
        list2->first = NULL;
    }
}

void co_list_remove(struct co_list *list,
                         struct co_list_hdr *prev_element,
                         struct co_list_hdr *element)
{
    // sanity check
    ASSERT_ERR(list != NULL);
    ASSERT_ERR((prev_element == NULL) || (prev_element->next == element));
    ASSERT_ERR(element != NULL);


    if (prev_element == NULL)
    {
        list->first = element->next;
    }
    else
    {
        prev_element->next = element->next;
    }
    if (list->last == element)
        list->last = prev_element;

    element->next = NULL;
}


