/* Ordinary Edax move ordering. GPL-3.0-or-later. */
#include "search-core-hooks.h"
#if !defined(__AVX2__) || defined(EDAX_PORTABLE_ORDERING)
static int movelist_mobility_score(EDAX_BITBOARD P, EDAX_BITBOARD O, EDAX_BITBOARD *moves)
{
  #if defined(EDAX_ORDER_MOBILITY)
 return EDAX_ORDER_MOBILITY(P,O,moves);
  #elif defined(hasSSE2) && !defined(POPCOUNT) && !defined(EDAX_PORTABLE_ORDERING)
	__m128i MM = bit_weighted_count_sse((*moves = get_moves(P, O)), get_potential_moves(P, O));
	int score = (36 - _mm_extract_epi16(MM, 4)) * w_potential_mobility; // potential mobility
	return score + (36 - _mm_cvtsi128_si32(MM)) * w_mobility; // real mobility
  #elif defined(__ARM_NEON) && !defined(EDAX_PORTABLE_ORDERING)
	uint64x2_t MM = bit_weighted_count_neon((*moves = get_moves(P, O)), get_potential_moves(P, O));
	int score = (36 - vgetq_lane_u32(vreinterpretq_u32_u64(MM), 2)) * w_potential_mobility; // potential mobility
	return score + (36 - vgetq_lane_u32(vreinterpretq_u32_u64(MM), 0)) * w_mobility; // real mobility
  #else
	int score = (36 - bit_weighted_count(get_potential_moves(P, O))) * w_potential_mobility; // potential mobility
	return score + (36 - bit_weighted_count(*moves = get_moves(P, O))) * w_mobility; // real mobility
  #endif
}
#endif

EDAX_CORE_LINKAGE void movelist_evaluate_fast(MoveList *movelist, Search *search, const unsigned char hashmove[2])
{
	Move	*move;
	int	score, parity_weight;
#ifdef EDAX_FAST_ORDER_CONTEXT
 #if !defined(EDAX_FAST_ORDER_PREPARE) || !defined(EDAX_FAST_ORDER_BONUS)
  #error "fast ordering evaluation needs context, prepare and bonus hooks"
 #endif
	EDAX_FAST_ORDER_CONTEXT order_eval = {0};
	EDAX_FAST_ORDER_PREPARE(&order_eval, search);
#endif

	// if (search->eval.n_empties < 21)	// mostly true
		parity_weight = (search->eval.n_empties < 12) ? w_low_parity : w_mid_parity;
	// else	parity_weight = (search->eval.n_empties < 30) ? w_high_parity : 0;

	move = movelist->move[0].next;
	do {
		// move_evaluate(move, search, hash_data, sort_alpha, -1);
		if (move_wipeout(move, &search->board)) score = (1 << 30);
		else if (move->x == hashmove[0]) score = (1 << 29);
		else if (move->x == hashmove[1]) score = (1 << 28);
		else {
			EDAX_BITBOARD moves;
#if defined(__AVX2__) && !defined(EDAX_PORTABLE_ORDERING)
			__m128i PO = _mm_xor_si128(*(__m128i *) &search->board,
				_mm_or_si128(_mm_set1_epi64x(move->flipped), _mm_loadl_epi64((__m128i *) &X_TO_BIT[move->x])));
			EDAX_BITBOARD O = _mm_cvtsi128_si64(PO);
			__m128i MM = get_moves_and_potential(_mm256_broadcastq_epi64(_mm_unpackhi_epi64(PO, PO)), _mm256_broadcastq_epi64(PO));
			score  = (36 - bit_weighted_count(_mm_extract_epi64(MM, 1))) * w_potential_mobility; // potential mobility
			score += (36 - bit_weighted_count(moves = _mm_cvtsi128_si64(MM))) * w_mobility; // real mobility
#else
			EDAX_BITBOARD O = EDAX_PLAYER(search->board) ^ (move->flipped | x_to_bit(move->x));
			EDAX_BITBOARD P = EDAX_OPPONENT(search->board) ^ move->flipped;
			score  = movelist_mobility_score(P, O, &moves);
#endif
			score += get_corner_stability(O) * w_corner_stability; // corner stability
			score += EDAX_SQUARE_VALUES[move->x]; // square type
			score += (search->eval.parity & EDAX_QUADRANT_IDS[move->x]) ? parity_weight : 0; // parity
#ifdef EDAX_FAST_ORDER_CONTEXT
			score += EDAX_FAST_ORDER_BONUS(&order_eval, search, move, moves);
#endif
			SEARCH_UPDATE_ALL_NODES(search->n_nodes);
		}
		move->score = score;
	} while ((move = move->next));
}

/**
 * @brief Evaluate a list of move in order to sort it.
 *
 * Evaluate the moves to sort them. Evaluation is based on, in order of importance:
 *   - wipeout move    : 1 << 30
 *   - first hash move : 1 << 29
 *   - second hash move: 1 << 28
 *   - shallow search  : 1 << 22 to 1 << 14
 *   - opponent mobility:                 1 << 15            32768...1048576
 *   - player stability near the corner:  1 << 11             2048...24576
 *   - opponent potential mobility:       1 << 5                32...1024
 *   - square value                       1 << 1:               2 ...18
 *   - parity:                            1 << 0:               0 ... 1
 *
 * @param movelist List of moves to sort.
 * @param search Position to evaluate.
 * @param hash_data   Position (maybe) stored in the hashtable.
 * @param alpha   Alpha bound to evaluate moves.
 * @param depth   depth for the shallow search
 */
EDAX_CORE_LINKAGE void movelist_evaluate(MoveList *movelist, Search *search, const HashData *hash_data, const int alpha, const int depth)
{
	static const char min_depth_table[64] = {
		19, 18, 18, 18, 17, 17, 17, 16,	// (Never for empties < 14)
		16, 16, 15, 15, 15, 14, 14, 14,
		13, 13, 13, 12, 12, 12, 11, 11,
		11, 10, 10, 10,  9,  9,  9,  9,
		 9,  9,  9,  9,  9,  9,  9,  9,
		 9,  9,  9,  9,  9,  9,  9,  9,
		 9,  9,  9,  9,  9,  9,  9,  9,
		 9,  9,  9,  9,  9,  9,  9,  9
	};
	Move *move;
	int	sort_depth, min_depth, sort_alpha, score, empties, parity_weight;
	EDAX_BITBOARD moves;
	HashData dummy;
	Eval eval0;
	Board board0;

	empties = search->eval.n_empties;
	// min_depth = 9;
	// if (empties <= 27) min_depth += (30 - empties) / 3;
	min_depth = min_depth_table[EDAX_ORDERING_INDEX(empties)];

	if (depth >= min_depth) {
		if (empties < 21)
			parity_weight = (empties < 12) ? w_low_parity : w_mid_parity;
		else	parity_weight = (empties < 30) ? w_high_parity : 0;

		sort_depth = (depth - 15) / 3;
		if (hash_data->upper < alpha) sort_depth -= 2;
		if (empties >= 27) ++sort_depth;
		if (sort_depth < 0) sort_depth = 0;
		else if (sort_depth > 6) sort_depth = 6;

		board0 = search->board;
		eval0 = search->eval;
		sort_alpha = MAX(EDAX_SCORE_MIN, alpha - SORT_ALPHA_DELTA);

		move = movelist->move[0].next;
		do {
			// move_evaluate(move, search, hash_data, sort_alpha, sort_depth);
			if (move_wipeout(move, &search->board)) score = (1 << 30);
			else if (move->x == hash_data->move[0] && hash_data->wl.c.depth > sort_depth - 3) score = (1 << 29);	// https://github.com/eukaryo/edax-reversi-AVX-v446mod2
			else if (move->x == hash_data->move[1] && hash_data->wl.c.depth > sort_depth - 3) score = (1 << 28);
			else {
				search_update_midgame(search, move);
#if defined(__AVX2__) && !defined(EDAX_PORTABLE_ORDERING)
				__m128i MM =  get_moves_and_potential(_mm256_set1_epi64x(EDAX_PLAYER(search->board)), _mm256_set1_epi64x(EDAX_OPPONENT(search->board)));
				score  = (36 - bit_weighted_count(_mm_extract_epi64(MM, 1))) * w_potential_mobility; // potential mobility
				score += (36 - bit_weighted_count(moves = _mm_cvtsi128_si64(MM))) * w_mobility; // real mobility
#else
				score  = movelist_mobility_score(EDAX_PLAYER(search->board), EDAX_OPPONENT(search->board), &moves);
#endif
				score += get_opp_edge_stability(&search->board) * w_edge_stability; // edge stability
				switch (sort_depth) {
				case 0:
#ifdef EDAX_STATIC_EVAL_MOVES
					score += (EDAX_SCORE_MAX - EDAX_STATIC_EVAL_MOVES(search, moves)) * (w_eval >> 2);
#else
					score += (EDAX_SCORE_MAX - search_eval_0(search)) * (w_eval >> 2);	// 1 level score bonus
#endif
					break;
				case 1:
					score += (EDAX_SCORE_MAX + search_eval_1(search, sort_alpha, EDAX_SCORE_MAX, moves)) * (w_eval >> 1);	// 2 level score bonus
					break;
				case 2:
					score += (EDAX_SCORE_MAX - search_eval_2(search, EDAX_SCORE_MIN, -sort_alpha, moves)) * (w_eval >> 1);	// 3 level score bonus
					break;
				default:	// 3 to 6
					if (hash_get_from_board(&search->hash_table, &search->board, &dummy)) score += w_hash;	// bonus if the position leads to a position stored in the hash-table
					// org_selectivity = search->selectivity;
					// search->selectivity = NO_SELECTIVITY;	// No probcut in PVS_shallow
					score += ((EDAX_SCORE_MAX - PVS_shallow(search, EDAX_SCORE_MIN, -sort_alpha, sort_depth))) * w_eval;	// > 3 level bonus
					// search->selectivity = org_selectivity;
					break;
				}

				search_restore_midgame(search, move->x, &eval0);
				search->board = board0;
				score += EDAX_SQUARE_VALUES[move->x]; // square type
				score += (search->eval.parity & EDAX_QUADRANT_IDS[move->x]) ? parity_weight : 0; // parity
				SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);
			}
			move->score = score;
		} while ((move = move->next));

	} else	// sort_depth = -1
		movelist_evaluate_fast(movelist, search, hash_data->move);
}
