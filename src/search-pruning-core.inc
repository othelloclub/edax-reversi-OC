/* Ordinary Edax pruning implementation. GPL-3.0-or-later. */
#include "search-core-hooks.h"
EDAX_CORE_LINKAGE bool search_SC_NWS(Search *search, const int alpha, int *score)
{
	if (USE_SC && alpha >= EDAX_NWS_STABILITY(search->eval.n_empties)) {
		CUTOFF_STATS(++statistics.n_stability_try;)
		*score = EDAX_SCORE_MAX - 2 * get_stability(EDAX_OPPONENT(search->board), EDAX_PLAYER(search->board));
		if (*score <= alpha) {
			CUTOFF_STATS(++statistics.n_stability_low_cutoff;)
			return true;
		}
	}
	return false;
}

// for 4 empties (min stage)
EDAX_CORE_LINKAGE bool search_SC_NWS_4(EDAX_BITBOARD player, EDAX_BITBOARD opponent, const int alpha, int *score)
{
	if (USE_SC && alpha < -EDAX_NWS_STABILITY(4)) {
		CUTOFF_STATS(++statistics.n_stability_try;)
		*score = 2 * get_stability(opponent, player) - EDAX_SCORE_MAX;
		if (*score > alpha) {
			CUTOFF_STATS(++statistics.n_stability_low_cutoff;)
			return true;
		}
	}
	return false;
}

#if 0	// unused
/**
 * @brief Transposition Cutoff (TC).
 *
 * @param data Transposition data.
 * @param depth Search depth.
 * @param selectivity Search selecticity level.
 * @param alpha Alpha bound, to adjust of necessary.
 * @param beta Beta bound, to adjust of necessary.
 * @param score Score to return in case of a cutoff is found.
 * @return 'true' if a cutoff is found, false otherwise.
 */
EDAX_CORE_LINKAGE bool search_TC_PVS(HashData *data, const int depth, const int selectivity, int *alpha, int *beta, int *score)
{
	if (USE_TC && (data->wl.c.selectivity >= selectivity && data->wl.c.depth >= depth)) {
		CUTOFF_STATS(++statistics.n_hash_try;)
		if (*alpha < data->lower) {
			*alpha = data->lower;
			if (*alpha >= *beta) {
				CUTOFF_STATS(++statistics.n_hash_high_cutoff;)
				*score = *alpha;
				return true;
			}
		}
		if (*beta > data->upper) {
			*beta = data->upper;
			if (*beta <= *alpha) {
				CUTOFF_STATS(++statistics.n_hash_low_cutoff;)
				*score = *beta;
				return true;
			}
		}
	}
	return false;
}
#endif

/**
 * @brief Transposition Cutoff (TC).
 *
 * @param data Transposition data.
 * @param depth Search depth.
 * @param selectivity Search selecticity level.
 * @param alpha Alpha bound.
 * @param score Score to return in case of a cutoff is found.
 * @return 'true' if a cutoff is found, false otherwise.
 */
EDAX_CORE_LINKAGE bool search_TC_NWS(HashData *data, const int depth, const int selectivity, const int alpha, int *score)
{
	if (USE_TC && (data->wl.c.selectivity >= selectivity && data->wl.c.depth >= depth)) {
		CUTOFF_STATS(++statistics.n_hash_try;)
		if (alpha < data->lower) {
			CUTOFF_STATS(++statistics.n_hash_high_cutoff;)
			*score = data->lower;
			return true;
		}
		if (alpha >= data->upper) {
			CUTOFF_STATS(++statistics.n_hash_low_cutoff;)
			*score = data->upper;
			return true;
		}
	}
	return false;
}

/**
 * @brief Enhanced Transposition Cutoff (ETC).
 *
 * Before looping over each moves to search at next ply, ETC looks if a cutoff
 * is available from the hashtable. This version also looks for a cutoff based
 * on stability.
 *
 * @param search Current position.
 * @param movelist List of moves for the current position.
 * @param hash_code Hashing code.
 * @param depth Search depth.
 * @param selectivity Search selectivity.
 * @param alpha Alpha bound.
 * @param score Score to return in case a cutoff is found.
 * @return 'true' if a cutoff is found, 'false' otherwise.
 */
EDAX_CORE_LINKAGE bool search_ETC_NWS(Search *search, MoveList *movelist, unsigned long long hash_code, const int depth, const int selectivity, const int alpha, int *score)
{
	if (USE_ETC && depth > ETC_MIN_DEPTH) {

		Move *move;
		Board next;
		HashData etc;
		HashStoreData hash_data;
		unsigned long long etc_hash_code;
		HashTable *hash_table = &search->hash_table;
		const int etc_depth = depth - 1;
		const int beta = alpha + 1;

		hash_data.data.wl.c.depth = depth;
		hash_data.data.wl.c.selectivity = selectivity;
		hash_data.data.wl.c.cost = 0;
		hash_data.alpha = alpha;
		hash_data.beta = beta;

		CUTOFF_STATS(++statistics.n_etc_try;)
		foreach_move (move, *movelist) {
			EDAX_ETC_NEXT(next, search->board, move);
			SEARCH_UPDATE_ALL_NODES(search->n_nodes);

			if (USE_SC && alpha <= -EDAX_NWS_STABILITY(search->eval.n_empties)) {
				*score = 2 * get_stability(EDAX_OPPONENT(next), EDAX_PLAYER(next)) - EDAX_SCORE_MAX;
				if (*score > alpha) {
					hash_data.score = *score;
					hash_data.data.move[0] = move->x;
					hash_store(hash_table, &search->board, hash_code, &hash_data);
					CUTOFF_STATS(++statistics.n_esc_high_cutoff;)
					return true;
				}
			}

			etc_hash_code = board_get_hash_code(&next);
			if (USE_TC && hash_get(hash_table, &next, etc_hash_code, &etc) && etc.wl.c.selectivity >= selectivity && etc.wl.c.depth >= etc_depth) {
				*score = -etc.upper;
				if (*score > alpha) {
					hash_data.score = *score;
					hash_data.data.move[0] = move->x;
					hash_store(hash_table, &search->board, hash_code, &hash_data);
					CUTOFF_STATS(++statistics.n_etc_high_cutoff;)
					return true;
				}
			}
		}
	}

	return false;
}
