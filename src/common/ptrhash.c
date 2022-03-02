/**
 * PTRHASH (PH) - Simple hash table implemented as array of pointers
 *
 * Copyright 2018 Giulio Picierro <giulio.picierro@uniroma2.it>
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Rationale:
 * - Open addressing collision resolution scheme with Double Hashing.
 *   Given two hash functions h1 and h2, compute both them on the key,
 *   and insert element at index h = h1 + i*h2. Initially i=0, incremented each
 *   time a collision is found. Full table coverage can be guaranteed if h2 is
 *   odd.
 *
 *   For simplicty and efficiency we use Lookup3 from Bob Jenkins as hash
 *   function, that provides two 32-bit hashes at price of one.
 *
 * - Key is a field of the 'value'. That means that key is at certain offset of
 *   the element to be inserted (typically a structure). In the provided C
 *   functions you must specify both key offset and length.
 *
 *   Helper macros are also provided to avoid compute manually the offset.
 *   I haven't found a better way to do this without degenerate to MACRO'S HELL.
 *   Nice ideas are welcome.
 *
 * - Size of the hash table must be power of two: there is not a valid reason
 *   to not do that, as provides advantadges in hash masking and bit extraction.
 *
 * - Size of the hash table is fixed once and for all. For now the table
 *   doesn't support dynamic resize. 
 *
 * - Removal of elements is handled by 'tombstones':
 *   Due to collisions you cannot simply remove elements by put NULL (0), since
 *   you can broke searching for elements that had collisions, making an
 *   element unreacheable. So we need to mark entry 'as deleted'. This is
 *   done by setting pointer to PH_ENTRY_DELETED that expands to (void*)0x1.
 *   The choice of this value is due to the fact that addresses are always
 *   word-aligned and thus lower 2-3 bits are always 0.
 *   This helps also because we can catch errors on de-referencing 'deleted'
 *   entries.
 */

#include "include/ptrhash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ph_table *ph_init(size_t size)
{
	struct ph_table *p;

	/* size must be power of 2 */
	if (size == 0 || (size & (size-1)) != 0) {
		fprintf(stderr, "ph_init: size must be power of two.\n");
		return NULL;
	}

	p = malloc(sizeof(*p) + size*sizeof(void*));

	if (!p) {
		fprintf(stderr, "ph_init: unable to allocate memory.\n");
		return NULL;
	}

	p->mask = size-1;
	p->count = 0;
	memset(p->buckets, 0, size*sizeof(void*));

	return p;
}

int ph_add_generic(struct ph_table *h, void *value, size_t keyoff, size_t keylen)
{
	uint32_t i, h0 = 1u, h1 = 3u;
	const size_t mask = h->mask;

	if (h->count >= mask) // hash table full
		return -1;

	__ph_hash(value + keyoff, keylen, &h0, &h1);

	/* Since we implement double hashing mechanism in which at collision i the
 	 * value of the hash is: h = h0 + i*h1, to cover the whole table it is
 	 * required that h1 is odd, simply or with 1 to obtain that property.
	 */
	h1 |= 1u;

	while (1) {
		i = h0 & mask;

		if (!h->buckets[i] || h->buckets[i] == PH_ENTRY_DELETED) {
			/* free slot found */
			h->buckets[i] = value;
			h->count++;
			break;
		}
		else if (!memcmp(h->buckets[i] + keyoff, value + keyoff, keylen)) {
			/* element already present */
			return 1;
		}
		else {
			/* collision */
			h0 += h1;
		}
	}

	return 0;
}

void *ph_get_generic(struct ph_table *h, const void *key, size_t keyoff, size_t keylen)
{
	uint32_t i, h0 = 1u, h1 = 3u;
	void *entry;
	int64_t first_grave = -1;
	const size_t mask = h->mask;

	__ph_hash(key, keylen, &h0, &h1);
	h1 |= 1u;

	while (1) {
		i = h0 & mask;

		entry = h->buckets[i];

		if (entry) {
			if (entry == PH_ENTRY_DELETED) {
				/* entry marked as deleted but we must proceed the search, if
 				 * this is the first 'grave' note that to move an entry there */
				if (first_grave == -1)
					first_grave = i;
			}
			else if (!memcmp(entry + keyoff, key, keylen)) {
				/* found */
				if (first_grave != -1) {
					/* move entry in an old grave to optimize next accesses */
					h->buckets[first_grave] = entry;
					h->buckets[i] = PH_ENTRY_DELETED;
				}
				return entry;
			}

			/* collision, go ahead */
			h0 += h1;
		}
		else /* not found */
			break;
	}

	return NULL;
}

void *ph_remove_generic(struct ph_table *h, const void *key, size_t keyoff, size_t keylen)
{
	void *entry;
	uint32_t i, h0 = 1u, h1 = 3u;
	const size_t mask = h->mask;

	__ph_hash(key, keylen, &h0, &h1);
	h1 |= 1u;

	while (1) {
		i = h0 & mask;

		entry = h->buckets[i];

		if (entry) {
			if (entry != PH_ENTRY_DELETED && !memcmp(entry + keyoff, key, keylen)) {
				/* found */
				h->buckets[i] = PH_ENTRY_DELETED;
				--h->count;
				return entry;
			}
			/* collision, go ahead */

			h0 += h1;
		}
		else /* not found */
			break;
	}

	return NULL;
}
