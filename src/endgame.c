/**
 * @file endgame.c
 *
 * Search near the end of the game.
 *
 * @date 1998 - 2025
 * @author Richard Delorme
 * @author Toshihiko Okuhara
 * @version 4.5
 */

#include "search.h"

#include "bit.h"
#include "settings.h"
#include "stats.h"
#include "ybwc.h"

#include <assert.h>

#if COUNT_LAST_FLIP == COUNT_LAST_FLIP_32
	#include "count_last_flip_32.c"
#elif COUNT_LAST_FLIP == COUNT_LAST_FLIP_CARRY
	#include "count_last_flip_carry_64.c"
#elif COUNT_LAST_FLIP == COUNT_LAST_FLIP_BITSCAN
	#include "count_last_flip_bitscan.c"
#elif COUNT_LAST_FLIP == COUNT_LAST_FLIP_BMI
	#include "count_last_flip_bmi.c"
#elif COUNT_LAST_FLIP == COUNT_LAST_FLIP_PLAIN
	#include "count_last_flip_plain.c"
#elif COUNT_LAST_FLIP == COUNT_LAST_FLIP_BMI2
	#include "count_last_flip_bmi2.c"
#elif COUNT_LAST_FLIP == COUNT_LAST_FLIP_SSE
	#include "count_last_flip_sse.c"
#elif COUNT_LAST_FLIP == COUNT_LAST_FLIP_AVX_PPFILL
	#include "count_last_flip_avx_ppfill.c"
#elif COUNT_LAST_FLIP == COUNT_LAST_FLIP_AVX512
	#include "count_last_flip_avx512cd.c"
#elif COUNT_LAST_FLIP == COUNT_LAST_FLIP_NEON
	#include "count_last_flip_neon.c"
#elif COUNT_LAST_FLIP == COUNT_LAST_FLIP_SVE
	#include "count_last_flip_sve_lzcnt.c"
#else // COUNT_LAST_FLIP == COUNT_LAST_FLIP_KINDERGARTEN
	#include "count_last_flip_kindergarten.c"
#endif

/**
 * @brief Get the final score.
 *
 * Get the final score, when no move can be made.
 *
 * @param player Board.player
 * @param n_empties Number of empty squares remaining on the board.
 * @return The final score, as a disc difference.
 */
int board_solve(const unsigned long long player, const int n_empties)
{
	int score = bit_count(player) * 2 - SCORE_MAX;	// in case of opponents win
	int diff = score + n_empties;		// = n_discs_p - (64 - n_empties - n_discs_p)

	SEARCH_STATS(++statistics.n_search_solve);

	if (diff == 0)
		score = diff;
	else if (diff > 0)
		score = diff + n_empties;
	return score;
}

/**
 * @brief Get the final score.
 *
 * Get the final score, when no move can be made.
 *
 * @param search Search.
 * @return The final score, as a disc difference.
 */
int search_solve(const Search *search)
{
	return board_solve(search->board.player, search->eval.n_empties);
}

/**
 * @brief Get the final score.
 *
 * Get the final score, when the board is full.
 *
 * @param search Search.
 * @return The final score, as a disc difference.
 */
int search_solve_0(const Search *search)
{
	SEARCH_STATS(++statistics.n_search_solve_0);

	return 2 * bit_count(search->board.player) - SCORE_MAX;
}

#if (MOVE_GENERATOR >= MOVE_GENERATOR_SSE) && (MOVE_GENERATOR <= MOVE_GENERATOR_AVX512)
	#include "endgame_sse.c"	// vectorcall version
#elif (MOVE_GENERATOR >= MOVE_GENERATOR_NEON) && (MOVE_GENERATOR <= MOVE_GENERATOR_SVE)
	#include "endgame_neon.c"
#else
#include "last-four-core.inc"

#endif

/**
 * @brief  Evaluate a position using a shallow NWS. (5..6 empties)
 *
 * This function is used when there are few empty squares on the board. Here,
 * optimizations are in favour of speed instead of efficiency.
 * Move ordering is constricted to the hole parity and the type of squares.
 * No hashtable are used and anticipated cut-off is limited to stability cut-off.
 *
 * @param search Search. (breaks board and parity; caller has a copy)
 * @param alpha Alpha bound.
 * @return The final score, as a disc difference.
 */
#include "endgame-core.inc"
