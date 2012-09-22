#ifndef CBOX_STM_H
#define CBOX_STM_H

#include <malloc.h>
#include <string.h>

static inline void **stm_array_clone_insert(void **old_array, int old_count, int index, void *data)
{
    size_t ps = sizeof(void *);
    if (index == -1)
        index = old_count;
    void **new_array = malloc(ps * (old_count + 1));
    memcpy(&new_array[0], &old_array[0], ps * index);
    new_array[index] = data;
    if (index != old_count)
        memcpy(&new_array[index + 1], &old_array[index], ps * (old_count - index));
    return new_array;
}

static inline void **stm_array_clone_remove(void **old_array, int old_count, int index)
{
    size_t ps = sizeof(void *);
    if (old_count == 1)
        return NULL;
    void **new_array = malloc(ps * (old_count - 1));
    memcpy(&new_array[0], &old_array[0], ps * index);
    memcpy(&new_array[index], &old_array[index + 1], ps * (old_count - index - 1));
    return new_array;
}

#define STM_ARRAY_FREE(old_array, count, destructor) \
    for (int i = 0; i < (count); i++) \
        destructor((old_array)[i]); \
    free(old_array);

#define STM_ARRAY_FREE_OBJS(old_array, count) \
    do { \
        for (int i = 0; i < (count); i++) \
            cbox_object_destroy(&(old_array)[i]->_obj_hdr); \
        free(old_array); \
    } while(0)

#endif