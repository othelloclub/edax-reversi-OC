/* Ordinary Edax midgame implementation, shared with rule adapters. GPL-3.0-or-later. */
#include "search-core-hooks.h"
static int NWS_shallow(Search *search, const int alpha, int depth, HashTable *hash_table)
{
	int score, bestscore;
	unsigned long long hash_code;
	// const int beta = alpha + 1;
	HashStoreData hash_data;
	MoveList movelist;
	Move *move;
	Eval eval0;
	VBoard board0;
	long long nodes_org;

	if (depth == 2) return search_eval_2(search, alpha, alpha + 1, board_get_moves(&search->board));

	SEARCH_STATS(++statistics.n_NWS_midgame);
	SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);

	assert(search->eval.n_empties == EDAX_EMPTY_COUNT(search->board));
	assert(EDAX_SCORE_MIN <= alpha && alpha <= EDAX_SCORE_MAX);
	assert(depth > 2);
	assert(hash_table != NULL);

	hash_code = board_get_hash_code(&search->board);
	hash_prefetch(hash_table, hash_code);

	// stability cutoff
	if (search_SC_NWS(search, alpha, &score)) return score;

	search_get_movelist(search, &movelist);
	board0.board = search->board;
	eval0 = search->eval;

	if (movelist.n_moves > 1) {
		// transposition cutoff
		if (hash_get(hash_table, &search->board, hash_code, &hash_data.data) && search_TC_NWS(&hash_data.data, depth, NO_SELECTIVITY, alpha, &score))
			return score;

		// sort the list of moves
		nodes_org = search->n_nodes;
		movelist_evaluate(&movelist, search, &hash_data.data, alpha, depth);

		// loop over all moves
		bestscore = -EDAX_SCORE_INF;
		foreach_best_move(move, movelist) {
			search_update_midgame(search, move);
			score = -NWS_shallow(search, ~alpha, depth - 1, hash_table);
			search_restore_midgame(search, move->x, &eval0);
			search->board = board0.board;
			if (score > bestscore) {
				bestscore = score;
				hash_data.data.move[0] = move->x;
				if (score > alpha) break;
			}
		}

		// save the best result in hash tables
		hash_data.data.wl.c.depth = depth;
		hash_data.data.wl.c.selectivity = NO_SELECTIVITY;	// (4.5.1)
		hash_data.data.wl.c.cost = last_bit(search->n_nodes - nodes_org);
		// hash_data.data.move[0] = bestmove;
		hash_data.alpha = alpha;
		hash_data.beta = alpha + 1;
		hash_data.score = bestscore;
		hash_store(hash_table, &search->board, hash_code, &hash_data);

	} else if (movelist.n_moves == 1) {
		move = movelist_first(&movelist);
		search_update_midgame(search, move);
		bestscore = -NWS_shallow(search, ~alpha, depth - 1, hash_table);
		search_restore_midgame(search, move->x, &eval0);
		search->board = board0.board;

	} else { // no moves
		if (can_move(EDAX_OPPONENT(search->board), EDAX_PLAYER(search->board))) { // pass ?
			search_update_pass_midgame(search, &eval0);
			bestscore = -NWS_shallow(search, ~alpha, depth, hash_table);
			search_restore_pass_midgame(search, &eval0);
		} else { // game-over !
			bestscore = search_solve(search);
		}
	}

	assert(EDAX_SCORE_MIN <= bestscore && bestscore <= EDAX_SCORE_MAX);
	return bestscore;
}

/**
 * @brief Evaluate a midgame position at shallow depth. (No probcut)
 *
 * This function is used when there are still many empty squares on the board. Move
 * ordering, hash table cutoff, enhanced transposition cutoff, etc. are used in
 * order to diminish the size of the tree to analyse, but at the expense of a
 * slower speed.
 * (called from guess_move @ depth <= 6 and movelist_evaluate @ depth > 2)
 *
 * @param search Search.
 * @param alpha Alpha bound.
 * @param beta Beta bound.
 * @param depth Search depth.
 * @return An evaluated score, as a disc difference.
 */
EDAX_CORE_LINKAGE int PVS_shallow(Search *search, int alpha, int beta, int depth)
{
	int score, bestscore, lower;
	// unsigned long long hash_code;
	HashStoreData hash_data;
	MoveList movelist;
	Move *move;
	Eval eval0;
	Board board0;
	long long nodes_org;

	if (depth == 2) return search_eval_2(search, alpha, beta, board_get_moves(&search->board));

	SEARCH_STATS(++statistics.n_PVS_shallow);
	SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);

	assert(search->eval.n_empties == EDAX_EMPTY_COUNT(search->board));
	assert(EDAX_SCORE_MIN <= alpha && alpha <= EDAX_SCORE_MAX);

	// stability cutoff
	if (USE_SC && beta >= EDAX_PVS_STABILITY(search->eval.n_empties)) {
		CUTOFF_STATS(++statistics.n_stability_try;)
		score = EDAX_SCORE_MAX - 2 * get_stability(EDAX_OPPONENT(search->board), EDAX_PLAYER(search->board));
		if (score <= alpha) {
			CUTOFF_STATS(++statistics.n_stability_low_cutoff;)
			return score;
		}
		else if (score < beta) beta = score;
	}

	search_get_movelist(search, &movelist);
	board0 = search->board;
	eval0 = search->eval;

	if (movelist.n_moves > 1) {
		// transposition cutoff (unused, normally first searched position)
		// hash_code = board_get_hash_code(&search->board);
		// if (hash_get(&search->shallow_table, &search->board, hash_code, &hash_data.data) && search_TC_PVS(&hash_data.data, depth, NO_SELECTIVITY, &alpha, &beta, &score)) return score;

		// sort the list of moves
		nodes_org = search->n_nodes;
		movelist_evaluate(&movelist, search, &HASH_DATA_INIT, alpha, depth);

		// loop over all moves
		move = movelist_best(&movelist);
		search_update_midgame(search, move);
		bestscore = -PVS_shallow(search, -beta, -alpha, depth - 1);
		hash_data.data.move[0] = move->x;
		search_restore_midgame(search, move->x, &eval0);
		search->board = board0;
		lower = (bestscore > alpha) ? bestscore : alpha;

		while ((bestscore < beta) && (move = move_next_best(move))) {
			search_update_midgame(search, move);
			score = -NWS_shallow(search, ~lower, depth - 1, &search->shallow_table);
			if (lower < score && score < beta)
				lower = score = -PVS_shallow(search, -beta, -lower, depth - 1);
			search_restore_midgame(search, move->x, &eval0);
			search->board = board0;
			if (score > bestscore) {
				bestscore = score;
				hash_data.data.move[0] = move->x;
			}
		}

		// save the best result in shallow hash
		hash_data.data.wl.c.depth = depth;
		hash_data.data.wl.c.selectivity = NO_SELECTIVITY;	// (4.5.1)
		hash_data.data.wl.c.cost = last_bit(search->n_nodes - nodes_org);
		// hash_data.data.move[0] = bestmove;
		hash_data.alpha = alpha;
		hash_data.beta = beta;
		hash_data.score = bestscore;
		hash_store(&search->shallow_table, &search->board, board_get_hash_code(&search->board), &hash_data);

	} else if (movelist.n_moves == 1) {
		move = movelist.move[0].next;
		search_update_midgame(search, move);
		bestscore = -PVS_shallow(search, -beta, -alpha, depth - 1);
		search_restore_midgame(search, move->x, &eval0);
		search->board = board0;

	} else { // no moves
		if (can_move(EDAX_OPPONENT(search->board), EDAX_PLAYER(search->board))) { // pass ?
			search_update_pass_midgame(search, &eval0);
			bestscore = -PVS_shallow(search, -beta, -alpha, depth);
			search_restore_pass_midgame(search, &eval0);
		} else { // game-over !
			bestscore = search_solve(search);
		}
	}

	assert(EDAX_SCORE_MIN <= bestscore && bestscore <= EDAX_SCORE_MAX);
	return bestscore;
}


/**
 * @brief Evaluate a midgame position with a Null Window Search algorithm.
 *
 * This function is used when there are still many empty squares on the board. Move
 * ordering, hash table cutoff, enhanced transposition cutoff, etc. are used in
 * order to diminish the size of the tree to analyse, but at the expense of a
 * slower speed.
 *
 * @param search Search.
 * @param alpha Alpha bound.
 * @param depth Depth.
 * @param parent Parent node.
 * @return A score, as a disc difference.
 */
EDAX_CORE_LINKAGE int NWS_midgame(Search *search, const int alpha, int depth, Node *parent)
{
	int score;
	unsigned long long hash_code;
	// const int beta = alpha + 1;
	HashStoreData hash_data;
	MoveList movelist;
	Move *move;
	Node node;
	Eval eval0;
	VBoard board0;
	long long nodes_org;

	assert(search->eval.n_empties == EDAX_EMPTY_COUNT(search->board));
	assert((2 <= depth && depth < search->eval.n_empties) || depth == search->eval.n_empties);
	assert(EDAX_SCORE_MIN <= alpha && alpha <= EDAX_SCORE_MAX);
	assert(parent != NULL);

	search_check_timeout(search);
	EDAX_ASYNC_YIELD(search);
	if (search->stop)
		return alpha;

	if (search->eval.n_empties == 0)
		return search_solve_0(search);
	else if (depth < search->eval.n_empties) {
		if (depth <= 3)
			return NWS_shallow(search, alpha, depth, &search->hash_table);
	} else {
		if (depth < DEPTH_MIDGAME_TO_ENDGAME)
			return NWS_endgame(search, alpha);
	}

	SEARCH_STATS(++statistics.n_NWS_midgame);
	SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);

	// stability cutoff
	if (search_SC_NWS(search, alpha, &score)) return score;

	hash_code = board_get_hash_code(&search->board);
	hash_prefetch(&search->hash_table, hash_code);
	hash_prefetch(&search->pv_table, hash_code);

	nodes_org = search->n_nodes + search->child_nodes;

	// A transposition proof needs no move list or flipped-disc calculation.
	if (hash_get(&search->hash_table, &search->board, hash_code, &hash_data.data) || hash_get(&search->pv_table, &search->board, hash_code, &hash_data.data))
		if (search_TC_NWS(&hash_data.data, depth, search->selectivity, alpha, &score)) return score;

	search_get_movelist(search, &movelist);

	if (movelist_is_empty(&movelist)) { // no moves ?
		node_init(&node, search, alpha, alpha + 1, depth, movelist.n_moves, parent);
		if (can_move(EDAX_OPPONENT(search->board), EDAX_PLAYER(search->board))) { // pass ?
			search_update_pass_midgame(search, &eval0);
			node.bestscore = -NWS_midgame(search, -node.beta, depth, &node);
			search_restore_pass_midgame(search, &eval0);
		} else { // game-over !
			node.bestscore = search_solve(search);
		}

	} else {
		// probcut
		if (search_probcut(search, alpha, depth, parent, &score)) return score;

		// sort the list of moves
		if (movelist.n_moves > 1) {
			if (hash_data.data.move[0] == EDAX_NOMOVE) hash_get(&search->hash_table, &search->board, hash_code, &hash_data.data);
			movelist_evaluate(&movelist, search, &hash_data.data, alpha, depth + options.inc_sort_depth[search->node_type[search->height]]);
			movelist_sort(&movelist);
		}

		// ETC
		if (search_ETC_NWS(search, &movelist, hash_code, depth, search->selectivity, alpha, &score)) return score;

		node_init(&node, search, alpha, alpha + 1, depth, movelist.n_moves, parent);

		// loop over all moves
		board0.board = search->board;
		eval0 = search->eval;
		for (move = node_first_move(&node, &movelist); move; move = node_next_move(&node)) {
			if (!node_split(&node, move)) {
				search_update_midgame(search, move);
				move->score = -NWS_midgame(search, ~alpha, depth - 1, &node);
				search_restore_midgame(search, move->x, &eval0);
				search->board = board0.board;
				node_update(&node, move);
			}
		}
		node_wait_slaves(&node);
	}

	// save the best result in hash tables
	if (!search->stop) {
		if (depth <= ((search->eval.n_empties <= depth) ? DEPTH_MIDGAME_TO_ENDGAME : 4))
			hash_data.data.wl.c.selectivity = NO_SELECTIVITY; // hack
		else hash_data.data.wl.c.selectivity = search->selectivity;
		hash_data.data.wl.c.depth = depth;
		hash_data.data.wl.c.cost = last_bit(search->n_nodes + search->child_nodes - nodes_org);
		hash_data.data.move[0] = node.bestmove;
		hash_data.alpha = alpha;
		hash_data.beta = alpha + 1;
		hash_data.score = node.bestscore;

		if (search->height <= PV_HASH_HEIGHT) hash_store(&search->pv_table, &search->board, hash_code, &hash_data);
		hash_store(&search->hash_table, &search->board, hash_code, &hash_data);

		SQUARE_STATS(foreach_move(move, &movelist))
		SQUARE_STATS(++statistics.n_played_square[search->eval.n_empties][SQUARE_TYPE[move->x]];)
		SQUARE_STATS(if (node.bestscore > alpha) ++statistics.n_good_square[search->eval.n_empties][SQUARE_TYPE[node->bestmove]];)

	 	assert(EDAX_SCORE_MIN <= node.bestscore && node.bestscore <= EDAX_SCORE_MAX);

	} else {
		node.bestscore = alpha;
	}

	node_free(&node);

	return node.bestscore;
}


/**
 * @brief Evaluate a position with a deep Principal Variation Search algorithm.
 *
 * This function is used when there are still many empty squares on the board. Move
 * ordering, hash table cutoff, enhanced transposition cutoff, etc. are used in
 * order to diminish the size of the tree to analyse, but at the expense of a
 * slower speed.
 *
 * @param search Search.
 * @param alpha Lower bound.
 * @param beta Upper bound.
 * @param depth Depth.
 * @param parent Parent node.
 * @return A score, as a disc difference.
 */
EDAX_CORE_LINKAGE int PVS_midgame(Search *search, const int alpha, const int beta, int depth, Node *parent)
{
	// declaration
	unsigned long long hash_code;
	HashStoreData hash_data;
	MoveList movelist;
	Move *move;
	Node node;
	Eval eval0;
	Board board0;
	long long nodes_org;
	int reduced_depth, depth_pv_extension, saved_selectivity;

	SEARCH_STATS(++statistics.n_PVS_midgame);

	assert(search->eval.n_empties == EDAX_EMPTY_COUNT(search->board));
	assert(depth <= search->eval.n_empties);
	assert((-EDAX_SCORE_MAX <= alpha && alpha <= EDAX_SCORE_MAX) || printf("alpha = %d\n", alpha));
	assert((-EDAX_SCORE_MAX <= beta && beta <= EDAX_SCORE_MAX) || printf("beta = %d\n", beta));
	assert(alpha <= beta);

	// end of search ?
	search_check_timeout(search);
	EDAX_ASYNC_YIELD(search);
	if (search->stop) return alpha;
	else if (search->eval.n_empties == 0)
		return search_solve_0(search);
	else if (USE_PV_EXTENSION && search->eval.n_empties <= search->depth_pv_extension)
		depth = search->eval.n_empties;
	else if (depth == 2 && search->eval.n_empties > 2)
		return search_eval_2(search, alpha, beta, board_get_moves(&search->board));

	nodes_org = search_count_nodes(search);
	SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);

	search_get_movelist(search, &movelist);
	node_init(&node, search, alpha, beta, depth, movelist.n_moves, parent);
	node.pv_node = true;
	hash_code = board_get_hash_code(&search->board);

	// special cases
	if (movelist_is_empty(&movelist)) {
		if (can_move(EDAX_OPPONENT(search->board), EDAX_PLAYER(search->board))) {
			search_update_pass_midgame(search, &eval0);
			search->node_type[search->height] = PV_NODE;
			node.bestscore = -PVS_midgame(search, -beta, -alpha, depth, &node);
			search_restore_pass_midgame(search, &eval0);
			node.bestmove = EDAX_PASS;
		} else {
			node.alpha = -(node.beta = +EDAX_SCORE_INF);
			node.bestscore = search_solve(search);
			node.bestmove = EDAX_NOMOVE;
		}

	} else { // normal PVS
		if (movelist.n_moves > 1) {
			//IID
			if (!hash_get(&search->pv_table, &search->board, hash_code, &hash_data.data))
				hash_get(&search->hash_table, &search->board, hash_code, &hash_data.data);

			if (USE_IID && hash_data.data.move[0] == EDAX_NOMOVE) {	// (unused)
				if (depth == search->eval.n_empties) reduced_depth = depth - ITERATIVE_MIN_EMPTIES;
				else reduced_depth = depth - 2;
				if (reduced_depth >= 3) {
					saved_selectivity = search->selectivity; search->selectivity = 0;
					depth_pv_extension = search->depth_pv_extension;
					search->depth_pv_extension = 0;
					PVS_midgame(search, EDAX_SCORE_MIN, EDAX_SCORE_MAX, reduced_depth, parent);
					hash_get(&search->pv_table, &search->board, hash_code, &hash_data.data);
					search->depth_pv_extension = depth_pv_extension;
					search->selectivity = saved_selectivity;
				}
			}

			// Evaluate moves for sorting. For a better ordering, the depth is artificially increased
			movelist_evaluate(&movelist, search, &hash_data.data, node.alpha, depth + options.inc_sort_depth[PV_NODE]);
			movelist_sort(&movelist);
		}

		// first move
		board0 = search->board;
		eval0 = search->eval;
		if ((move = node_first_move(&node, &movelist))) { // why if there ?
			search_update_midgame(search, move); search->node_type[search->height] = PV_NODE;
			move->score = -PVS_midgame(search, -beta, -alpha, depth - 1, &node);
			search_restore_midgame(search, move->x, &eval0);
			search->board = board0;
			node_update(&node, move);

			// other moves : try to refute the first/best one
			while ((move = node_next_move(&node))) {
				if (!node_split(&node, move)) {
					const int alpha = node.alpha;
					search_update_midgame(search, move);
					move->score = -NWS_midgame(search, -alpha - 1, depth - 1, &node);
					if (!search->stop && alpha < move->score && move->score < beta) {
						search->node_type[search->height] = PV_NODE;
						move->score = -PVS_midgame(search, -beta, -alpha, depth - 1, &node);
					}
					search_restore_midgame(search, move->x, &eval0);
					search->board = board0;
					node_update(&node, move);
				}
			}
			node_wait_slaves(&node);
		}
	}

	// save the best result in hash tables
	if (!search->stop) {
		if (depth <= ((search->eval.n_empties <= depth) ? DEPTH_MIDGAME_TO_ENDGAME : 4))
			hash_data.data.wl.c.selectivity = NO_SELECTIVITY;
		else	hash_data.data.wl.c.selectivity = search->selectivity;
		hash_data.data.wl.c.depth = depth;
		hash_data.data.wl.c.cost = last_bit(search_count_nodes(search) - nodes_org);
		hash_data.data.move[0] = node.bestmove;
		hash_data.alpha = alpha;
		hash_data.beta = beta;
		hash_data.score = node.bestscore;

		hash_store(&search->hash_table, &search->board, hash_code, &hash_data);
		hash_store(&search->pv_table, &search->board, hash_code, &hash_data);

		// lacal hash (main thread only)
		if (search->eval.n_empties <= depth && depth <= DEPTH_TO_USE_LOCAL_HASH && depth > DEPTH_TO_SHALLOW_SEARCH) {
			VBoard hashboard;
			hashboard.board = search->board;
  #ifdef USE_SOLID
			EDAX_BITBOARD solid = EDAX_FULL_LINES(EDAX_PLAYER(search->board) | EDAX_OPPONENT(search->board)) & EDAX_PLAYER(search->board);
			if (solid) {
				int ofssolid = EDAX_SOLID_OFFSET(solid);	// hash score is ofssolid smaller than real
				EDAX_PLAYER(hashboard.board) = EDAX_PLAYER(search->board) ^ solid;	// normalize solid to opponent
				EDAX_OPPONENT(hashboard.board) = EDAX_OPPONENT(search->board) ^ solid;
				hash_store_local(search->thread_hash.hash + (board_get_hash_code(&hashboard.board) & search->thread_hash.hash_mask),
					hashboard, alpha - ofssolid, beta - ofssolid, node.bestscore - ofssolid, node.bestmove);
			}
  #endif
			hashboard.board = search->board;
			hash_store_local(search->thread_hash.hash + (hash_code & search->thread_hash.hash_mask),
				hashboard, alpha, beta, node.bestscore, node.bestmove);
		}

		SQUARE_STATS(foreach_move(move, movelist))
		SQUARE_STATS(++statistics.n_played_square[search->eval.n_empties][SQUARE_TYPE[move->x]];)
		SQUARE_STATS(if (node.bestscore > alpha) ++statistics.n_good_square[search->eval.n_empties][SQUARE_TYPE[node.bestmove]];)

	 	assert(EDAX_SCORE_MIN <= node.bestscore && node.bestscore <= EDAX_SCORE_MAX);

	} else {
		node.bestscore = alpha;
	}

	node_free(&node);

	return node.bestscore;
}
