
/**
 * \file
 * PTRHASH (PH) - A simple hash table
 */

#ifndef _PTR_HASH_H
#define _PTR_HASH_H

#include <stddef.h>
#include <stdint.h>

#define PH_ENTRY_DELETED ((void *)0x1)

struct ph_table {
	size_t mask;
	size_t count;
	void *buckets[0];
};

void hashlittle2( 
  const void *key,       /* the key to hash */
  size_t      length,    /* length of the key */
  uint32_t   *pc,        /* IN: primary initval, OUT: primary hash */
  uint32_t   *pb);       /* IN: secondary initval, OUT: secondary hash */

#define __ph_hash(key, len, h1, h2) hashlittle2(key, len, h1, h2)


/** 
 * Add an element to hash table, using a field as a key.
 * @param table: hash table pointer.
 * @param eptr: pointer to the element (struct) to add.
 * @param keyfield: name of the field containing the key (size is computed
 * automatically)
 * @return 0: if element was added, 1: if already present, -1: if table is full.
 */ 
#define ph_add(table, eptr, keyfield) \
	ph_add_generic(table, eptr, offsetof(typeof(*eptr), keyfield), sizeof((*eptr).keyfield))

/** 
 * Get an element from the hash table.
 * @param table: hash table pointer.
 * @param key: pointer to key bytes
 * @param keyfield: name of the field containing the key (size is computed
 * automatically)
 * @param out: pointer to the found element
 */ 
#define ph_get(table, key, keyfield, out) ({ \
	out = ph_get_generic(table, key, offsetof(typeof(*out), keyfield), sizeof((*out).keyfield)); \
})

/** 
 * Remove an element from the hash table.
 * @param table: hash table pointer.
 * @param key: pointer to key bytes
 * @param keyfield: name of the field containing the key (size is computed
 * automatically)
 * @param out: pointer to the removed element
 */ 
#define ph_remove(table, key, keyfield, out) ({ \
	out = ph_remove_generic(table, key, offsetof(typeof(*out), keyfield), sizeof((*out).keyfield)); \
})

struct ph_table *ph_init(size_t size);

/** 
 * Add an element to hash table, using key embedded in 'value' structure.
 * @param h: hash table pointer.
 * @param value: pointer to the element (struct) to add.
 * @param keyoff: offset in bytes of the key to use, in the *value structure.
 * @param keylen: length in bytes of the key.
 * @return 0 if element was added, 1 if already present, -1 if table is full.
 */ 
int ph_add_generic(struct ph_table *h, void *value, size_t keyoff, size_t keylen);

/** 
 * Get an element from hash table, using the specified key.
 * @param h: hash table pointer.
 * @param key: pointer to the key bytes.
 * @param keyoff: offset in bytes of the key to use, in the *ptr structure.
 * @param keylen: length in bytes of the key.
 * @return element pointer or NULL if not found.
 */ 
void *ph_get_generic(struct ph_table *h, const void *key, size_t keyoff, size_t keylen);

/** 
 * Remove an element from hash table, using the specified key.
 * @param h: hash table pointer.
 * @param key: pointer to the key bytes.
 * @param keyoff: offset in bytes of the key in the value structure.
 * @param keylen: length in bytes of the key.
 * @return element pointer or NULL if not found.
 */ 
void *ph_remove_generic(struct ph_table *h, const void *key, size_t keyoff, size_t keylen);

/** 
 * Returns the number of elements of the hash table.
 * @param h: hash table pointer.
 * @return number of elements currently stored
 */ 
static inline size_t ph_count(const struct ph_table *h)
{
	return h->count;
}

#endif
