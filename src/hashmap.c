/**
 * Small hashmap implementation, inspired from
 * https://benhoyt.com/writings/hash-table-in-c/
 */

#include "hashmap.h"

#include <stdlib.h>             /* malloc */
#include <stdint.h>             /* uint64_t */
#include <string.h>             /* strdup */


#define INITIAL_NSLOTS 32       /* initial capacity of the hashmap */

typedef struct {
    const char *key;            /* NULL if slot is empty */
    void       *value;
} hm_item;

struct hashmap {
  hm_item  *items;
  size_t   nitems;              /* current length */
  size_t   nslots;              /* total capacity */
};


static uint64_t hash_key(const char *key);
static struct hashmap *hm_expand(struct hashmap *map);
static int hm_set_internal(hm_item *items, size_t size, const char *k, void *v);


struct hashmap *
hm_create(void)
{
  struct hashmap *map = malloc(sizeof(*map));
  if (map == NULL) {
    return NULL;
  }

  map->nslots = INITIAL_NSLOTS;
  map->nitems = 0;

  map->items = calloc(INITIAL_NSLOTS, sizeof(*map->items));
  if (map->items == NULL) {
    free(map);
    return NULL;
  }

  return map;
}


void
hm_destroy(struct hashmap *map)
{
  for (size_t i = 0; i < map->nslots; i++) {
    if (map->items[i].key != NULL) {
      free((char *)map->items[i].key);
    }
  }
  free(map->items);
  free(map);
}


void *
hm_get(struct hashmap* map, const char *key)
{
  uint64_t hash;
  size_t index;

  hash = hash_key(key);
  index = hash % map->nslots;

  while (map->items[index].key != NULL) {
    if (!strcmp(key, map->items[index].key)) {
      return map->items[index].value;     /* key found */
    }

    index++;                              /* key not found, linear probe */
    if (index >= map->nslots) {
      index = 0;                          /* wrap around */
    }
  }

  /* warning: the loop is only exited on reaching the rigth key or
     NULL, so the map must never be completely full, otherwise an
     inexistent key will trigger an infinite loop */

  return NULL;
}


int
hm_set(struct hashmap *map, const char *key, void *value)
{
  int rc;

  if (value == NULL) {
    return -1;
  }

  if (map->nitems >= map->nslots / 2) {
    if (hm_expand(map) == NULL)   /* memory error */
      return -1;
  }

  rc = hm_set_internal(map->items, map->nslots, key, value);
  if (rc != -1) {
    map->nitems += rc;
  }

  return rc;
}


size_t
hm_length(hm_t *map)
{
    return map->nitems;
}


#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

/**
 * Return the FNV-1a hash of the NULL terminated KEY.
 *
 * Description:
 * https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function
 *
 * FIXME: Should we use the more secure SipHash?
 */
static uint64_t
hash_key(const char *key)
{
  uint64_t hash = FNV_OFFSET;
  for (const char *p = key; *p; p++) {
    hash ^= (uint64_t)(unsigned char)(*p);
    hash *= FNV_PRIME;
  }
  return hash;
}


/**
 * Set KEY to VALUE in the item list pointed by ITEMS.
 *
 * Return 0 if an existing item was updated, 1 if a new item was
 * created and -1 in case of a memory error.
 */
static int
hm_set_internal(hm_item *items, size_t size, const char *key, void *value)
{
  uint64_t hash;
  size_t index;

  hash = hash_key(key);
  index = hash % size;

  while (items[index].key != NULL) {
    if (!strcmp(key, items[index].key)) {
      items[index].value = value;  /* key found */
      return 0;
    }

    index++;                              /* key not found, linear probe */
    if (index >= size) {
      index = 0;                          /* wrap around */
    }
  }

  /* XX FIXME: the following alloc could be avoided when expanding the
     hashmap, because keys have already been allocated */

  /* key is not present in the map, alloc and set value */
  key = strdup(key);
  if (key == NULL) {
    return -1;
  }

  items[index].key = key;
  items[index].value = value;

  return 1;
}


/**
 * Double the size of the hashmap.
 *
 * Return MAP or NULL in case of a memory error
 */
static struct hashmap *
hm_expand(struct hashmap *map)
{
  int rc;
  size_t newsize;
  hm_item *newitems;

  newsize = map->nslots * 2;

  newitems = calloc(newsize, sizeof(*newitems));
  if (newitems == NULL) {
    return NULL;
  }

  for (size_t i = 0; i < map->nslots; i++) {
    hm_item item = map->items[i];
    if (item.key != NULL) {
      rc = hm_set_internal(newitems, newsize, item.key, item.value);
      free((char *)item.key);
      if (rc == -1)             /* memory error */
        return NULL;
    }
  }

  free(map->items);
  map->items = newitems;
  map->nslots = newsize;

  return map;
}
