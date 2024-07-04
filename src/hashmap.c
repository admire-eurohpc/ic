/**
 * Small hashmap implementation, inspired from
 * https://benhoyt.com/writings/hash-table-in-c/.
 *
 * Definitely NOT thread-safe, protect with a rw-lock.  Both key and
 * value are allocated on hm_set, and can be freed by the caller
 * afterwards. On the other hand, memory returned by hm_get should not
 * be modified.
 */

#include "hashmap.h"

#include <stdlib.h>             /* malloc */
#include <stdint.h>             /* uint64_t */
#include <string.h>             /* strdup */
#include <stdio.h>
#include <inttypes.h>


#define INITIAL_NSLOTS 32       /* initial capacity of the hashmap */

#define SIZE 512

typedef struct {
  const char *key;              /* NULL if slot is empty */
  void       *value;
  size_t     size; /* Specify the size of the memory allocated */
} hm_item;

struct hashmap {
  hm_item  *items;
  size_t   nitems;              /* current length */
  size_t   nslots;              /* total capacity */
};


static uint64_t hash_key(const char *key);
static struct hashmap *hm_expand(struct hashmap *map);
static int hm_set_internal(hm_item *items, size_t size, const char *k, void *v, size_t datatype_size);


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
hm_free(struct hashmap *map)
{
  hm_item item;

  for (size_t i = 0; i < map->nslots; i++) {
    item = map->items[i];
    if (item.key) {
      free((char *)item.key);
      free(item.value);
    }
  }

  free(map->items);
  free(map);
}


const void *
hm_get(struct hashmap* map, const char *key)
{
  uint64_t hash;
  size_t index;
  
  fprintf(stderr, "HM_GET: key = %s\n", key);

  hash = hash_key(key);
  index = hash % map->nslots;

  while (map->items[index].key != NULL) {
    if (!strcmp(key, map->items[index].key)) {
      fprintf(stderr, "HM_GET: key found %s\n", key);
      return map->items[index].value;     /* key found */
    }

    index = (index + 1) % map->nslots;    /* key not found, linear probe + wrap */
  }

  /* warning: the loop is only exited on reaching the right key or
     NULL, so the map must never be completely full, otherwise an
     inexistent key will trigger an infinite loop */

  fprintf(stderr, "HM_GET: key not found %s\n", key);
  return NULL;
}


int
hm_set(struct hashmap *map, const char *key, void *value, size_t size)
{
  int rc;
  void *val;

  if (value == NULL) {
    return -1;
  }

  fprintf(stderr, "HM_SET: Key %s\n", key);

  if (map->nitems >= map->nslots / 2) {
    fprintf(stderr, "HM_SET: nitems >= nslots\n");
    if (hm_expand(map) == NULL)   /* memory error */
      return -1;
  }

  val = malloc(size);
  if (val == NULL) {
    return -1;
  }
  memcpy(val, value, size);

  fprintf(stderr, "HM_SET (call to hm_set_internal): Key %s, value %hu, size %lu\n", key, *(uint16_t*)val, size);
  rc = hm_set_internal(map->items, map->nslots, key, val, size);
  fprintf(stderr, "HM_SET_INTERNAL: Return %d\n", rc); 


  /* ALBERTO - Debug with hm_get to check if the value can be obtained later */
  //void * query = (void*)hm_get(map, key);

  // 0 if an existing item was updated, 1 if a new item was created and -1 in case of a memory error.
  if (rc != -1) {
    map->nitems += rc;
  }

  fprintf(stderr, "HM_SET: end\n");
  return rc;
}


size_t
hm_length(struct hashmap *map)
{
    return map->nitems;
}


size_t
hm_next(struct hashmap *map, size_t cursor, const char **key, const void **value)
{
  for (size_t i = cursor; i < map->nslots; i++) {
    if (map->items[i].key) {
      *key = map->items[i].key;
      if (value) {
        *value = map->items[i].value;
      }
      //return (i + 1) % map->nslots;
      return (i + 1);
    }
  }

  /* we reached the end of the map */
  return 0;
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
/*hm_set_internal(hm_item *items, size_t size, const char *key, void *value)*/
hm_set_internal(hm_item *items, size_t size, const char *key, void *value, size_t datatype_size)
{
  uint64_t hash;
  size_t index;


  hash = hash_key(key);
  index = hash % size;

  fprintf(stderr, "HM_SET_INTERNAL: Key = %s, index = %ld, ", key, index);
  fprintf(stderr, "hash = %" PRIu64 "\n", hash); 

  while (items[index].key != NULL) {
    fprintf(stderr, "HM_SET_INTERNAL: Looking for = %s; items[index(%ld)].key = %s \n", key, index, items[index].key);
    if (!strcmp(key, items[index].key)) {  /* key found */
      fprintf(stderr, "HM_SET_INTERNAL: Key = %s found\n", key);
      free(items[index].value); 
      items[index].value = value;
      items[index].size = datatype_size;
      fprintf(stderr, "HM_SET_INTERNAL: Key %s, value %hu, size %lu\n", key, *(uint16_t*)items[index].value, items[index].size);
      return 0;
    }

    index = (index + 1) % size;            /* key not found, linear probe + wrap */
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
  items[index].size = datatype_size;

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
  if (newsize < map->nslots) {  /* overflow */
    return NULL;
  }

  newitems = calloc(newsize, sizeof(*newitems));
  if (newitems == NULL) {
    return NULL;
  }

  for (size_t i = 0; i < map->nslots; i++) {
    hm_item item = map->items[i];
    if (item.key != NULL) {
      rc = hm_set_internal(newitems, newsize, item.key, item.value, item.size);
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

/*ALBERTO - Functions to store and load the hashmaps*/
/**
 * Function to serialize a hashmap to a file
 */
void serialize_hashmap(const struct hashmap *map, const char *filename) {
    int count = 0; 
    char *buffer = (char*)calloc(SIZE, 1);

    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        perror("Error opening file for writing");
        exit(EXIT_FAILURE);
    }

    // Write nitems and nslots
    fwrite(&(map->nitems), sizeof(size_t), 1, file);
    count += snprintf(&(buffer[count]), SIZE-count, "%ld::", map->nitems);
    fwrite(&(map->nslots), sizeof(size_t), 1, file);
    count += snprintf(&(buffer[count]), SIZE-count, "%ld::", map->nslots);

    // Write the items array
    //fwrite(map->items, sizeof(hm_item), map->nitems, file);

    // Write the string data: loop until i < nslots not items because keys are stored based on a hash key.
    for (size_t i = 0; i < map->nslots; ++i) {
        size_t key_len = (map->items[i].key != NULL) ? strlen(map->items[i].key) + 1 : 0;
        fwrite(&key_len, sizeof(size_t), 1, file);
        count += snprintf(&(buffer[count]), SIZE-count, "%ld::", key_len);

        if (key_len > 0) {
            fwrite(map->items[i].key, sizeof(char), key_len, file);
            count += snprintf(&(buffer[count]), SIZE-count, "%s::", map->items[i].key);

            if (map->items[i].size > 0){
              fwrite(&map->items[i].size, sizeof(size_t), 1, file);
              count += snprintf(&(buffer[count]), SIZE-count, "%ld::", map->items[i].size);
              fwrite(map->items[i].value, map->items[i].size, 1, file);
              count += snprintf(&(buffer[count]), SIZE-count, "%hu::", *(uint16_t*)(map->items[i].value));
            }
        } 
    }
    fprintf(stderr, "SERIALIZED HASHMAP: %s\n", buffer);
    fprintf(stderr, "SERIALIZED HASHMAP: Name = %s, Count = %d\n", filename, count);

    free(buffer);
    fclose(file);
}

/** 
 * Function to deserialize a hashmap from a file and return a pointer to it
 */
struct hashmap *deserialize_hashmap(const char *filename) {
    int count = 0; 
    char *buffer = (char*)calloc(SIZE, 1);
    struct hashmap *map = hm_create();
    if (map == NULL) {
        perror("Memory allocation error");
        exit(EXIT_FAILURE);
    }

    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file for reading");
        free(map);
        exit(EXIT_FAILURE);
    }

    // Read nitems and nslots
    fread(&(map->nitems), sizeof(size_t), 1, file);
    count += snprintf(&(buffer[count]), SIZE-count, "%ld::", map->nitems);
    fread(&(map->nslots), sizeof(size_t), 1, file);
    count += snprintf(&(buffer[count]), SIZE-count, "%ld::", map->nslots);

    // Allocate memory for the items array
    //free(map->items); // free original allocation in creation
    map->items = calloc(map->nslots, sizeof(*map->items));
    if (map->items == NULL) {
        perror("Memory allocation error");
        free(map);
        fclose(file);
        exit(EXIT_FAILURE);
    }

    // Read the items array
    //fread(map->items, sizeof(hm_item), map->nitems, file);

    // Read the string data: loop until i < nslots not items because keys are stored based on a hash key.
    for (size_t i = 0; i < map->nslots; ++i) {
        size_t key_len;
        fread(&key_len, sizeof(size_t), 1, file);
        count += snprintf(&(buffer[count]), SIZE-count, "%ld::", key_len);

        if (key_len > 0) {
            map->items[i].key = calloc(key_len, 1);
            if (map->items[i].key == NULL) {
                perror("Memory allocation error");
                hm_free(map);
                fclose(file);
                exit(EXIT_FAILURE);
            }
            fread((char*)map->items[i].key, sizeof(char), key_len, file);
            count += snprintf(&(buffer[count]), SIZE-count, "%s::", map->items[i].key);

            fread(&map->items[i].size, sizeof(size_t), 1, file);
            count += snprintf(&(buffer[count]), SIZE-count, "%ld::", map->items[i].size);

            if (map->items[i].size > 0) {
                map->items[i].value = calloc(map->items[i].size, 1);
                if (map->items[i].value == NULL) {
                    perror("Memory allocation error");
                    hm_free(map);
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
                
                fread(map->items[i].value, map->items[i].size, 1, file);
                count += snprintf(&(buffer[count]), SIZE-count, "%hu::", *(uint16_t*)(map->items[i].value));
            } else {
                map->items[i].value = NULL;
            }
            
        } else {
            map->items[i].key = NULL;
        }
    }

    //serialize_hashmap(map, "/tmp/loaded_hashmap");

    fprintf(stderr, "DESERIALIZED HASHMAP: %s\n", buffer);
    fprintf(stderr, "DESERIALIZED HASHMAP: Name = %s, Count = %d\n", filename, count);

    free(buffer);
    fclose(file);

    return map;
}


