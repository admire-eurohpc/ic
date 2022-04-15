#ifndef ADMIRE_HASHMAP_H
#define ADMIRE_HASHMAP_H

#include <stddef.h>

typedef struct hashmap hm_t;

/**
 * Create and return a new hashmap.
 */
hm_t *hm_create(void);


/**
 * Free MAP.
 */
void hm_free(hm_t *map);


/**
 * Return the value associated with NULL-terminated string KEY in MAP,
 * or NULL if not present.
 */
const void *hm_get(hm_t *map, const char *key);


/**
 * Associate VALUE of size SIZE to NULL-terminated string KEY in
 * MAP. VALUE cannot be NULL. KEY and VALUE are copied and allocated
 * as necessary.
 *
 * Return 0 if an item was updated, 1 if a new item was created or -1
 * in case of error.
 */
int hm_set(hm_t *map, const char *key, void *value, size_t size);


/**
 * Get KEY and VALUE from the next item in MAP. This is a cursor based
 * iterator. The caller passes a 0 cursor at initialization and the
 * function returns an updated cursor. The iteration is over when the
 * returned cursor is 0.
 *
 * Return the updated cursor.
 */
size_t hm_next(hm_t *map, size_t cursor, const char **key, const void **value);


/**
 * Return the number of elements present in MAP.
 */
size_t hm_length(hm_t *map);


#endif
