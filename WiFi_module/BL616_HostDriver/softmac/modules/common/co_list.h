#ifndef _CO_LIST_H_
#define _CO_LIST_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "ke_config.h"

/*
 * DEFINES
 ****************************************************************************************
 */

/*
 * MACROS
 ****************************************************************************************
 */

/*
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */
struct co_list_hdr
{
    /// Pointer to the next element in the list
    struct co_list_hdr *next;
};
typedef struct co_list_hdr co_list_hdr_t;

/// structure of a list
struct co_list
{
    /// pointer to first element of the list
    struct co_list_hdr *first;
    /// pointer to the last element
    struct co_list_hdr *last;
};
typedef struct co_list co_list_t;

/*
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief Initialize a list to defaults values.
 * @param[in] list           Pointer to the list structure.
 ****************************************************************************************
 */
void co_list_init(struct co_list *list);

/**
 ****************************************************************************************
 * @brief Initialize a pool to default values, and initialize the relative free list.
 *
 * @param[in] list           Pointer to the list structure
 * @param[in] pool           Pointer to the pool to be initialized
 * @param[in] elmt_size      Size of one element of the pool
 * @param[in] elmt_cnt       Nb of elements available in the pool
 * @param[in] default_value  Pointer to the default value of each element (may be NULL)
 ****************************************************************************************
 */
void co_list_pool_init(struct co_list *list, void *pool, size_t elmt_size,
                          uint32_t elmt_cnt, void *default_value);

/**
 ****************************************************************************************
 * @brief Add an element as last on the list.
 *
 * @param[in] list           Pointer to the list structure
 * @param[in] list_hdr       Pointer to the header to add at the end of the list
 ****************************************************************************************
 */
void co_list_push_back(struct co_list *list, struct co_list_hdr *list_hdr);

/**
 ****************************************************************************************
 * @brief Append a sequence of elements at the end of a list.
 *
 * Note: the elements to append shall be linked together
 *
 * @param list           Pointer to the list structure
 * @param first_hdr      Pointer to the first element to append
 * @param last_hdr       Pointer to the last element to append
 ****************************************************************************************
 */
void co_list_push_back_sublist(struct co_list *list, 
                 struct co_list_hdr *first_hdr, struct co_list_hdr *last_hdr);

/**
 ****************************************************************************************
 * @brief Add an element as first on the list.
 *
 * @param[in] list           Pointer to the list structure
 * @param[in] list_hdr       Pointer to the header to add at the beginning of the list
 ****************************************************************************************
 */
void co_list_push_front(struct co_list *list, struct co_list_hdr *list_hdr);
/**
 ****************************************************************************************
 * @brief Extract the first element of the list.
 *
 * @param[in] list           Pointer to the list structure
 *
 * @return The pointer to the element extracted, and NULL if the list is empty.
 ****************************************************************************************
 */
struct co_list_hdr *co_list_pop_front(struct co_list *list);

/**
 ****************************************************************************************
 * @brief Search for a given element in the list, and extract it if found.
 *
 * @param[in] list           Pointer to the list structure
 * @param[in] list_hdr       Pointer to the searched element
 ****************************************************************************************
 */
bool co_list_extract(struct co_list *list, struct co_list_hdr *list_hdr);

/**
 ****************************************************************************************
 * @brief Extract an element when the previous element is known
 *
 * Note: the element to remove shall follow immediately the reference within the list
 *
 * @param list           Pointer to the list structure
 * @param elt_ref_hdr    Pointer to the referenced element (NULL if element to extract is the first in the list)
 * @param elt_to_rem_hdr Pointer to the element to be extracted
 ****************************************************************************************
 */
void co_list_extract_after(struct co_list *list, 
                                struct co_list_hdr *elt_ref_hdr, 
                                struct co_list_hdr *elt_to_rem_hdr);

/**
 ****************************************************************************************
 * @brief Extract a sub-list when the previous element is known
 *
 * Note: the elements to remove shall be linked together and  follow immediately the reference element
 *
 * @param[in]  list           Pointer to the list structure
 * @param[in]  ref_hdr        Pointer to the referenced element (NULL if first element to extract is first in the list)
 * @param[in]  last_hdr       Pointer to the last element to extract ()
 ****************************************************************************************
 */
void co_list_extract_sublist(struct co_list *list, 
                     struct co_list_hdr *ref_hdr, struct co_list_hdr *last_hdr);

/**
 ****************************************************************************************
 * @brief Searched a given element in the list.
 *
 * @param[in] list           Pointer to the list structure
 * @param[in] list_hdr       Pointer to the searched element
 *
 * @return true if the element is found in the list, false otherwise
 ****************************************************************************************
 */
bool co_list_find(struct co_list *list, struct co_list_hdr *list_hdr);

/**
 ****************************************************************************************
 * @brief Merge two lists in a single one.
 *
 * This function appends the list pointed by list2 to the list pointed by list1. Once the
 * merge is done, it empties list2.
 *
 * @param list1    Pointer to the destination list
 * @param list2    Pointer to the list to append to list1
 ****************************************************************************************
 */
void co_list_merge(struct co_list *list1, struct co_list *list2);

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
                                    struct co_list_hdr const *elementB));

/**
 ****************************************************************************************
 * @brief Insert an element in a list after the provided element.
 *
 * If @p prev_element is NULL then @p element is added in the front of the list.
 * Otherwise this primitive first ensure that @p prev_element is part of the list before
 * adding @p element, and does nothing if this is not the case.
 *
 * @param[in]  list           Pointer to the list.
 * @param[in]  prev_element   Pointer to the element to find in the list
 * @param[in]  element        Pointer to the element to insert.
 *
 ****************************************************************************************
 */
void co_list_insert_after(struct co_list * const list,
                              struct co_list_hdr * const prev_element,
                              struct co_list_hdr * const element);

/**
 ****************************************************************************************
 * @brief Insert an element in a list after the provided element.
 *
 * Same as @ref co_list_insert_after except that if @p prev_element is not NULL no check
 * is done to ensure it is part of the list.
 *
 * @param[in]  list           Pointer to the list.
 * @param[in]  prev_element   Pointer to the element after which the new element must
 *                            be added
 * @param[in]  element        Pointer to the element to insert.
 *
 ****************************************************************************************
 */
void co_list_insert_after_fast(struct co_list * const list,
                               struct co_list_hdr * const prev_element,
                               struct co_list_hdr * const element);

/**
 ****************************************************************************************
 * @brief Insert an element in a sorted list before the provided element.
 *
 * This primitive use a comparison function from the parameter list to select where the
 * element must be inserted.
 *
 * @param[in]  list           Pointer to the list.
 * @param[in]  next_element   Pointer to the element to find in the list
 * @param[in]  element        Pointer to the element to insert.
 *
 * If next_element is not found, the provided element is not inserted
 ****************************************************************************************
 */
void co_list_insert_before(struct co_list * const list,
                                struct co_list_hdr * const next_element,
                                struct co_list_hdr * const element);

/**
 ****************************************************************************************
 * @brief Concatenate two lists.
 * The resulting list is the list passed as the first parameter. The second list is
 * emptied.
 *
 * @param[in]  list1          First list (will get the result of the concatenation)
 * @param[in]  list2          Second list (will be emptied after the concatenation)
 ****************************************************************************************
 */
void co_list_concat(struct co_list *list1, struct co_list *list2);

/**
 ****************************************************************************************
 * @brief Remove the element in the list after the provided element.
 *
 * This primitive removes an element in the list. It is assume that element is part of
 * the list.
 *
 * @param[in] list          Pointer to the list.
 * @param[in] prev_element  Pointer to the previous element.
 *                          NULL if @p element is the first element in the list
 * @param[in] element       Pointer to the element to remove.
 *
 ****************************************************************************************
 */
void co_list_remove(struct co_list *list, struct co_list_hdr *prev_element,
                         struct co_list_hdr *element);
/**
 ****************************************************************************************
 * @brief Test if the list is empty.
 *
 * @param[in] list           Pointer to the list structure.
 *
 * @return true if the list is empty, false else otherwise.
 ****************************************************************************************
 */
__INLINE bool co_list_is_empty(const struct co_list *const list)
{
    bool listempty;
    listempty = (list->first == NULL);
    return (listempty);
}

/**
 ****************************************************************************************
 * @brief Return the number of element of the list.
 *
 * @param[in] list           Pointer to the list structure.
 *
 * @return The number of elements in the list.
 ****************************************************************************************
 */
uint32_t co_list_cnt(const struct co_list *const list);

/**
 ****************************************************************************************
 * @brief Pick the first element from the list without removing it.
 *
 * @param[in] list           Pointer to the list structure.
 *
 * @return First element address. Returns NULL pointer if the list is empty.
 ****************************************************************************************
 */
__INLINE struct co_list_hdr *co_list_pick(const struct co_list *const list)
{
    return(list->first);
}

/**
 ****************************************************************************************
 * @brief Pick the last element from the list without removing it.
 *
 * @param[in] list           Pointer to the list structure.
 *
 * @return Last element address. Returns invalid value if the list is empty.
 ****************************************************************************************
 */
__INLINE struct co_list_hdr *co_list_pick_last(const struct co_list *const list)
{
    return(list->last);
}

/**
 ****************************************************************************************
 * @brief Return following element of a list element.
 *
 * @param[in] list_hdr     Pointer to the list element.
 *
 * @return The pointer to the next element.
 ****************************************************************************************
 */
__INLINE struct co_list_hdr *co_list_next(const struct co_list_hdr *const list_hdr)
{
    return(list_hdr->next);
}




#endif // _CO_LIST_H_
