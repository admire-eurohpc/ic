#ifndef _ADMIRE_HASHMAP_H
#define _ADMIRE_HASHMAP_H

#include <stddef.h>

typedef struct hashmap hm_t;

/**
 * Create and return a new hashmap.
 */
hm_t *hm_create(void);


/**
 * Deallocate MAP.
 */
void hm_destroy(hm_t *map);


/**
 * Return value associated with NULL-terminated string KEY in MAP, or
 * NULL if not present.
 */
void *hm_get(hm_t *map, const char *key);


/**
 * Associate VALUE to NULL-terminated string KEY in MAP. VALUE cannot
 * be NULL. KEY is copied and allocated as necessary.
 *
 * Return 0 if an item was updated, 1 if a new item was created or -1
 * in case of error.
 */
int hm_set(hm_t *map, const char *key, void* value);


/**
 * Return the number of elements present in MAP.
 */
size_t hm_length(hm_t *map);


#endif
