/* Ordinary Edax endgame control flow. Rule-specific last-four kernels remain hooks. GPL-3.0-or-later. */
#include "search-core-hooks.h"
#if defined(EDAX_ENDGAME_EVAL_UPDATE) && (!defined(EDAX_ENDGAME_EVAL_RESTORE) || !defined(EDAX_ENDGAME_EVAL_PASS))
 #error "incremental endgame ordering needs update, restore and pass hooks"
#endif
static void endgame_evaluate_fast(MoveList *list,Search *search,const unsigned char hashmove[2])
{
#ifdef EDAX_ENDGAME_ORDER_BEGIN
 EDAX_ENDGAME_ORDER_BEGIN(search);
#endif
 movelist_evaluate_fast(list,search,hashmove);
#ifdef EDAX_ENDGAME_ORDER_END
 EDAX_ENDGAME_ORDER_END(search);
#endif
}
/* Read a cached first move before paying to rank moves that may be cut off. */
static bool endgame_defer_order(MoveList *list,Search *search,const unsigned char hashmove[2])
{
 Move *move;bool found=false;
 if(hashmove[0]>=EDAX_PASS)return false;
 foreach_move(move,*list){
  if(move_wipeout(move,&search->board))return false;
  if(move->x==hashmove[0])found=true;
 }
 if(!found)return false;
 foreach_move(move,*list)move->score=move->x==hashmove[0]?(1<<29):0;
 return true;
}
static void endgame_order_tail(MoveList *list,Search *search,Move *first,const unsigned char hashmove[2],unsigned int parity)
{
 Move *head=list->move[0].next;unsigned int old_parity=search->eval.parity;
 list->move[0].next=first->next;--list->n_moves;
 ++search->eval.n_empties;search->eval.parity=parity;
 if(list->n_moves>1)endgame_evaluate_fast(list,search,hashmove);
 --search->eval.n_empties;search->eval.parity=old_parity;
 ++list->n_moves;list->move[0].next=head;
}
static int search_shallow(Search *search, const int alpha, bool pass1)
{
	EDAX_BITBOARD moves, prioritymoves;
	int x, prev, score, bestscore;
	// const int beta = alpha + 1;
	VBoard board0;
	unsigned int parity0;

	assert(EDAX_SCORE_MIN <= alpha && alpha <= EDAX_SCORE_MAX);
	assert(0 <= search->eval.n_empties && search->eval.n_empties <= DEPTH_TO_SHALLOW_SEARCH);

	SEARCH_STATS(++statistics.n_NWS_shallow);
	SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);

	// stability cutoff (try 8%, cut 7%)
	if (search_SC_NWS(search, alpha, &score)) return score;

	board0.board = search->board;
	moves = vboard_get_moves(board0);
	if (moves == 0) {	// pass (2%)
		if (pass1)	// gameover (1%)
			return search_solve(search);

		search_pass(search);
		bestscore = -search_shallow(search, ~alpha, true);
		// search_pass(search);
		return bestscore;
	}

	bestscore = -EDAX_SCORE_INF;
	parity0 = search->eval.parity;
	prioritymoves = moves & EDAX_QUADRANT_MASKS[parity0];
	if (prioritymoves == 0)	// all even
		prioritymoves = moves;

	--search->eval.n_empties;	// for next depth
	do {
		moves ^= prioritymoves;
		x = EDAX_NOMOVE;
		do {
			do {
				x = search->empties[prev = x].next;
			} while (!(prioritymoves & x_to_bit(x)));	// (57%)

			prioritymoves &= ~x_to_bit(x);
			search->empties[prev].next = search->empties[x].next;	// remove - maintain single link only
			vboard_next(board0, x, &search->board);
			if (search->eval.n_empties == 4) {	// transfer to search_solve_n, no longer uses parity
				score = search_solve_4(search, alpha);
			} else {
				search->eval.parity = parity0 ^ EDAX_QUADRANT_IDS[x];
				score = -search_shallow(search, ~alpha, false);
			}
			search->empties[prev].next = x;	// restore

			if (score > alpha) {	// (40%)
				++search->eval.n_empties;
				// search->board = board0.board;
				// search->eval.parity = parity0;
				return score;

			} else if (score > bestscore)
				bestscore = score;
		} while (prioritymoves);	// (54%)
	} while ((prioritymoves = moves));	// (23%)
	++search->eval.n_empties;
	// search->board = board0.board;	// restore in caller
	// search->eval.parity = parity0;

 	assert(EDAX_SCORE_MIN <= bestscore && bestscore <= EDAX_SCORE_MAX);
	return bestscore;	// (33%)
}

/**
 * @brief Evaluate an endgame position with a Null Window Search algorithm,
 * with thread-local lockfree hash. (7..10 empties)

 * Lightweight transposition table with thread local (thus lockfree) 1-way hash
 * is used for 7..10 empties (and occasionally less from PVS).
 * http://www.amy.hi-ho.ne.jp/okuhara/edaxopt.htm#localhash
 *
 * @param search Search.
 * @param alpha Alpha bound.
 * @return The final score, as a disc difference.
 */

static int NWS_endgame_local(Search *search, const int alpha)
{
	int score, ofssolid, bestmove, bestscore;
	// const int beta = alpha + 1;
	Move *move;
	VBoard board0, hashboard;
	unsigned int parity0;
	MoveList movelist;
	bool defer_order=false;

	assert(EDAX_EMPTY_COUNT(search->board) < DEPTH_TO_USE_LOCAL_HASH);
	assert(EDAX_SCORE_MIN <= alpha && alpha <= EDAX_SCORE_MAX);

	SEARCH_STATS(++statistics.n_NWS_endgame);
	SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);

	// stability cutoff
	board0.board = hashboard.board = search->board;
	ofssolid = 0;
#ifdef USE_SOLID
	if (USE_SC && alpha >= NWS_STABILITY_SOLID_THRESHOLD) {	// (7%)
		EDAX_BITBOARD full[5];

		CUTOFF_STATS(++statistics.n_stability_try;)
		score = EDAX_SCORE_MAX - 2 * EDAX_STABILITY_FULLS(board0, full);
		if (score <= alpha) {	// (3%)
			CUTOFF_STATS(++statistics.n_stability_low_cutoff;)
			return score;

		} else {
			// Improvement of Serch by Reducing Redundant Information in a Position of Othello
			// Hidekazu Matsuo, Shuji Narazaki
			// http://id.nii.ac.jp/1001/00156359/
  #if defined(hasSSE2) && defined(POPCOUNT) && !defined(EDAX_VARIANT_ADAPTER)
			__m128i solid = _mm_and_si128(hashboard.v2, _mm_loadl_epi64((__m128i *) &full[4]));
			hashboard.v2 = _mm_xor_si128(hashboard.v2, _mm_unpacklo_epi64(solid, solid));
			ofssolid = bit_count_si64(solid) * 2;
  #else
			EDAX_BITBOARD solid = full[4] & EDAX_PLAYER(hashboard.board);	// full[4] = all full
			if (solid) {	// (72%)
				EDAX_PLAYER(hashboard.board) ^= solid;	// normalize solid to opponent
				EDAX_OPPONENT(hashboard.board) ^= solid;
				ofssolid = EDAX_SOLID_OFFSET(solid);	// hash score is ofssolid smaller than real
			}
  #endif
		}
	}
#else
	if (search_SC_NWS(search, alpha, &score)) return score;
#endif
		// transposition cutoff
		// hash_get(&search->thread_hash, &hashboard.board, hash_code, &hash_data.data)
		unsigned char hashmove[2] = { EDAX_NOMOVE, EDAX_NOMOVE };
		Hash *hash_entry = search->thread_hash.hash + (board_get_hash_code(&hashboard.board) & search->thread_hash.hash_mask);
		HASH_STATS(++statistics.n_local_hash_search;)
		if (EDAX_LOCAL_HASH_MATCH(hash_entry, hashboard)) {	// (6%)
			HASH_STATS(++statistics.n_local_hash_found;)
			hashmove[0] = hash_entry->data.move[0];
			// search_TC_NWS(&hash_data.data, search->eval.n_empties, NO_SELECTIVITY, alpha, &score)
			if (USE_TC /* && (data->wl.c.selectivity >= NO_SELECTIVITY && data->wl.c.depth >= search->eval.n_empties) */) {
				int lower = hash_entry->data.lower + ofssolid;
				int upper = hash_entry->data.upper + ofssolid;
				CUTOFF_STATS(++statistics.n_hash_try;)
				if (alpha < lower) {
					HASH_STATS(++statistics.n_local_hash_cutoff;)
					CUTOFF_STATS(++statistics.n_hash_high_cutoff;)
					return lower;
				}
				if (alpha >= upper) {
					HASH_STATS(++statistics.n_local_hash_cutoff;)
					CUTOFF_STATS(++statistics.n_hash_low_cutoff;)
					return upper;
				}
			}
		}

	search_get_movelist(search, &movelist);

	if (movelist.n_moves > 0) {

		if (movelist.n_moves > 1) {
			defer_order=endgame_defer_order(&movelist,search,hashmove);
			if(!defer_order)endgame_evaluate_fast(&movelist, search, hashmove);
		}

		parity0 = search->eval.parity;
		bestscore = -EDAX_SCORE_INF;
		--search->eval.n_empties;	// for next move
		// loop over all moves
		foreach_best_move(move, movelist) {
			search->eval.parity = parity0 ^ EDAX_QUADRANT_IDS[move->x];
			vboard_update(&search->board, board0, move);
			if (search->eval.n_empties <= DEPTH_TO_SHALLOW_SEARCH) {
				search->empties[search->empties[move->x].previous].next = search->empties[move->x].next;	// remove - maintain single link only
				score = -search_shallow(search, ~alpha, false);
				search->empties[search->empties[move->x].previous].next = move->x;	// restore
			} else {
				empty_remove(search->empties, move->x);
				score = -NWS_endgame_local(search, ~alpha);
				empty_restore(search->empties, move->x);
			}
			search->board = board0.board;

			if (score > bestscore) {	// (63%)
				bestscore = score;
				bestmove = move->x;
				if (bestscore > alpha) break;	// (39%)
			}
			if(defer_order){endgame_order_tail(&movelist,search,move,hashmove,parity0);defer_order=false;}
		}
		++search->eval.n_empties;
		search->eval.parity = parity0;

		if (search->stop)	// (1%)
			return alpha;

		hash_store_local(hash_entry, hashboard, alpha - ofssolid, alpha - ofssolid + 1, bestscore - ofssolid, bestmove);

	} else {	// (1%)
		if (can_move(EDAX_OPPONENT(search->board), EDAX_PLAYER(search->board))) { // pass
			search_pass(search);
			bestscore = -NWS_endgame_local(search, ~alpha);
			search_pass(search);
		} else  { // game over
			bestscore = search_solve(search);
		}
	}

	if (SQUARE_STATS(1) + 0) {
		foreach_move(move, movelist)
			++statistics.n_played_square[search->eval.n_empties][SQUARE_TYPE[move->x]];
		if (bestscore > alpha)
			++statistics.n_good_square[search->eval.n_empties][SQUARE_TYPE[bestscore]];
	}
 	assert(EDAX_SCORE_MIN <= bestscore && bestscore <= EDAX_SCORE_MAX);
 	assert(EDAX_EVEN_SCORE(bestscore));
	return bestscore;
}

/**
 * @brief Evaluate an endgame position with a Null Window Search algorithm. (11..15 empties)
 *
 * This function is used when there are still many empty squares on the board. Move
 * ordering, hash table cutoff, enhanced transposition cutoff, etc. are used in
 * order to diminish the size of the tree to analyse, but at the expense of a
 * slower speed.
 *
 * @param search Search.
 * @param alpha Alpha bound.
 * @return The final score, as a disc difference.
 */
EDAX_CORE_LINKAGE int NWS_endgame(Search *search, const int alpha)
{
	int score, bestscore;
	unsigned long long hash_code;
	// const int beta = alpha + 1;
	HashStoreData hash_data;
	Move *move;
	long long nodes_org;
	VBoard board0;
	unsigned int parity0;
	MoveList movelist;
	bool defer_order=false;
#ifdef EDAX_ENDGAME_EVAL_UPDATE
	Eval eval0;
#endif

	assert(EDAX_EMPTY_COUNT(search->board) < DEPTH_MIDGAME_TO_ENDGAME);
	assert(EDAX_SCORE_MIN <= alpha && alpha <= EDAX_SCORE_MAX);

	EDAX_ASYNC_YIELD(search);
	if (search->stop) return alpha;

	if (search->eval.n_empties <= DEPTH_TO_USE_LOCAL_HASH)
		return NWS_endgame_local(search, alpha);

	SEARCH_STATS(++statistics.n_NWS_endgame);
	SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);

	hash_code = board_get_hash_code(&search->board);
	hash_prefetch(&search->hash_table, hash_code);
		// transposition cutoff
		if (hash_get(&search->hash_table, &search->board, hash_code, &hash_data.data))	// (6%)
			if (search_TC_NWS(&hash_data.data, search->eval.n_empties, NO_SELECTIVITY, alpha, &score))	// (6%)
				return score;


	// stability cutoff after a reusable transposition proof
	if (search_SC_NWS(search, alpha, &score)) return score;

	search_get_movelist(search, &movelist);
	board0.board = search->board;

	if (movelist.n_moves > 0) {	// (96%)

		if (movelist.n_moves > 1) {
			defer_order=endgame_defer_order(&movelist,search,hash_data.data.move);
			if(!defer_order)endgame_evaluate_fast(&movelist, search, hash_data.data.move);
		}

		nodes_org = search->n_nodes;
		parity0 = search->eval.parity;
		bestscore = -EDAX_SCORE_INF;
#ifdef EDAX_ENDGAME_EVAL_UPDATE
		eval0 = search->eval;
#endif
		// loop over all moves
		--search->eval.n_empties;	// for next move
		foreach_best_move(move, movelist) {
			search->eval.parity = parity0 ^ EDAX_QUADRANT_IDS[move->x];
			empty_remove(search->empties, move->x);
			vboard_update(&search->board, board0, move);
#ifdef EDAX_ENDGAME_EVAL_UPDATE
			EDAX_ENDGAME_EVAL_UPDATE(search, board0, move, &eval0);
#endif
			score = -NWS_endgame(search, ~alpha);
			empty_restore(search->empties, move->x);
			search->board = board0.board;
#ifdef EDAX_ENDGAME_EVAL_UPDATE
			EDAX_ENDGAME_EVAL_RESTORE(search, &eval0);
#endif

			if (score > bestscore) {	// (63%)
				bestscore = score;
				hash_data.data.move[0] = move->x;
				if (bestscore > alpha) break;	// (39%)
			}
			if(defer_order){endgame_order_tail(&movelist,search,move,hash_data.data.move,parity0);defer_order=false;}
		}
		++search->eval.n_empties;
		search->eval.parity = parity0;

		if (search->stop)	// (1%)
			return alpha;

		hash_data.data.wl.c.depth = search->eval.n_empties;
		hash_data.data.wl.c.selectivity = NO_SELECTIVITY;
		hash_data.data.wl.c.cost = last_bit(search->n_nodes - nodes_org);
		// hash_data.data.move[0] = bestmove;
		hash_data.alpha = alpha;
		hash_data.beta = alpha + 1;
		hash_data.score = bestscore;
		hash_store(&search->hash_table, &search->board, hash_code, &hash_data);

	} else {	// (1%)
		if (can_move(EDAX_OPPONENT(search->board), EDAX_PLAYER(search->board))) { // pass
			search_pass(search);
#ifdef EDAX_ENDGAME_EVAL_PASS
			EDAX_ENDGAME_EVAL_PASS(search);
#endif
			bestscore = -NWS_endgame(search, ~alpha);
			search_pass(search);
#ifdef EDAX_ENDGAME_EVAL_PASS
			EDAX_ENDGAME_EVAL_PASS(search);
#endif
		} else  { // game over
			bestscore = search_solve(search);
		}
	}

	if (SQUARE_STATS(1) + 0) {
		foreach_move(move, movelist)
			++statistics.n_played_square[search->eval.n_empties][SQUARE_TYPE[move->x]];
		if (bestscore > alpha)
			++statistics.n_good_square[search->eval.n_empties][SQUARE_TYPE[bestscore]];
	}
 	assert(EDAX_SCORE_MIN <= bestscore && bestscore <= EDAX_SCORE_MAX);
 	assert(EDAX_EVEN_SCORE(bestscore));
	return bestscore;
}
