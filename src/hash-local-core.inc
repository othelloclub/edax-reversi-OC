/* Actual ordinary one-way local hash store; board/rule identity is adapted.
 * Original authors: Richard Delorme, Toshihiko Okuhara. GPL-3.0-or-later. */
#include "search-core-hooks.h"
#ifndef EDAX_LOCAL_HASH_WRITE
#define EDAX_LOCAL_HASH_WRITE(entry,value) ((entry)->board=(value).board)
#endif
EDAX_CORE_LINKAGE void hash_store_local(Hash *hash, VBoard vboard, int alpha, int beta, int score, int move)
{
	if (EDAX_LOCAL_HASH_MATCH(hash, vboard)) {	// data_update
		HASH_STATS(++statistics.n_local_hash_update;)
		if (score < beta && score < hash->data.upper) hash->data.upper = score;
		if (score > alpha) {
			if (score > hash->data.lower) hash->data.lower = score;
			hash->data.move[0] = move;
		}

	} else {	// data_new
		HASH_STATS(++statistics.n_local_hash_new;)
		int upper = EDAX_SCORE_MAX;
		if (score < beta) upper = score;
		if (score <= alpha) {
			score = EDAX_SCORE_MIN;
			move = EDAX_NOMOVE;
		}
		EDAX_LOCAL_HASH_WRITE(hash, vboard);
		hash->data.upper = upper;
		hash->data.lower = score;
		hash->data.move[0] = move;
	}
}

