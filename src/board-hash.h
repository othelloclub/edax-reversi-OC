/* Ordinary CRC32C board hashing; wide boards extend the same word stream.
 * GPL-3.0-or-later. Full board/rule equality is checked by the hash table. */
#ifndef EDAX_BOARD_HASH_H
#define EDAX_BOARD_HASH_H
#include "bit.h"

static inline unsigned long long edax_board_hash2(unsigned long long player,unsigned long long opponent)
{
	unsigned long long crc=crc32c_u64(0,player);
	return (crc<<32)|crc32c_u64(crc,opponent);
}

static inline unsigned long long edax_board_hash4(unsigned long long p0,unsigned long long p1,unsigned long long o0,unsigned long long o1)
{
	unsigned long long crc=crc32c_u64(crc32c_u64(0,p0),p1);
	return (crc<<32)|crc32c_u64(crc32c_u64(crc,o0),o1);
}
#endif
