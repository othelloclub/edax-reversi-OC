/**
 * @file bookhash.h
 *
 * Shared definition of the `.hidx` sidecar index that lets edax serve a book
 * straight from disk instead of loading it onto the heap.
 *
 * Written by tools/bookindex.c, read by src/book.c's lazy mode. Both sides must
 * agree on the hash, so it lives here rather than being duplicated.
 *
 *
 * WHY
 *
 * edax's normal `book load` copies every position onto the heap at ~94 bytes
 * each. A 254M-position book is ~24GB of anonymous memory -- impossible on the
 * 2GB server, and it makes startup take minutes even where it fits. With the
 * sidecar, the book and the index are both mmap'd read-only: anonymous memory
 * is ~0, the pages are clean so the kernel can evict them for free, and startup
 * is immediate.
 *
 *
 * LAYOUT
 *
 *   header        32 bytes
 *   directory     (1 << dir_bits) + 1  u32   start index into the entry array
 *   entries       n_positions          6B    { u8 tag; u40 book offset }
 *
 * A lookup hashes the symmetry-unique board, takes the top `dir_bits` as the
 * bucket, and linear-scans that bucket's entries comparing the 8-bit tag. A tag
 * hit reads the book record and compares the board for real, so the tag only
 * has to be selective enough to keep spurious reads rare (1 in 256 over a
 * ~16-entry bucket is ~0.06 extra reads per lookup).
 *
 * Buckets are not sorted internally -- there is no need, the scan is one cache
 * line or two.
 *
 * Cost per probe: one directory read, one or two entry reads, one record read.
 *
 *
 * SIZE
 *
 * dir_bits targets ~8 entries per bucket, clamped to [10, 24], so the directory
 * stays proportionate on small books instead of dwarfing them:
 *
 *   11.8M positions (depth 30)   dir_bits 20   4MB dir  +   71MB entries
 *    254M positions (depth 40)   dir_bits 24  67MB dir  + 1.53GB entries
 */

#ifndef BOOKHASH_H
#define BOOKHASH_H

#include <stdint.h>

#define BOOKIDX_MAGIC    0x58444942u    /* "BIDX" */
#define BOOKIDX_VERSION  1u
#define BOOKIDX_HDR      32
#define BOOKIDX_ENTRY    6              /* u8 tag + u40 offset */
#define BOOKIDX_SUFFIX   ".hidx"

/* header, little-endian, packed:
 *   0  u32 magic
 *   4  u32 version
 *   8  u64 n_positions
 *  16  u64 book_size      -- guards against an index paired with the wrong book
 *  24  u32 dir_bits
 *  28  u32 entry_bytes                                                        */

static inline uint64_t bookidx_splitmix64(uint64_t x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

/* A single-layer mix such as `p ^ (o + f(p))` collides badly on boards, which
 * are two disjoint, highly structured bitmaps -- measured at 1368 collisions
 * over 18.9M positions where the birthday bound predicts 1e-5. Cascading the
 * halves through separate finalisers puts it back on the theoretical curve. */
static inline uint64_t bookidx_hash(uint64_t player, uint64_t opponent)
{
	return bookidx_splitmix64(bookidx_splitmix64(player) ^ opponent);
}

static inline uint64_t bookidx_bucket(uint64_t h, unsigned dir_bits)
{
	return h >> (64 - dir_bits);
}

static inline unsigned char bookidx_tag(uint64_t h, unsigned dir_bits)
{
	return (unsigned char)((h >> (56 - dir_bits)) & 0xFFu);
}

static inline uint64_t bookidx_get_offset(const unsigned char *e)
{
	return (uint64_t)e[1] | ((uint64_t)e[2] << 8) | ((uint64_t)e[3] << 16)
	     | ((uint64_t)e[4] << 24) | ((uint64_t)e[5] << 32);
}

static inline void bookidx_put_offset(unsigned char *e, uint64_t off)
{
	e[1] = (unsigned char)(off);
	e[2] = (unsigned char)(off >> 8);
	e[3] = (unsigned char)(off >> 16);
	e[4] = (unsigned char)(off >> 24);
	e[5] = (unsigned char)(off >> 32);
}

#endif /* BOOKHASH_H */
