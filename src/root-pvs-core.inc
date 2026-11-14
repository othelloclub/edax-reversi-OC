/* Ordinary Edax root search and PV cost. GPL-3.0-or-later. */
#include "search-core-hooks.h"
EDAX_CORE_LINKAGE int search_get_pv_cost(Search *search)
{
	HashTable *hash_table = &search->hash_table;
	HashTable *pv_table = &search->pv_table;
	HashTable *shallow_table = &search->shallow_table;
	HashData hash_data;
	const unsigned long long hash_code = board_get_hash_code(&search->board);

	if ((hash_get(pv_table, &search->board, hash_code, &hash_data) || hash_get(hash_table, &search->board, hash_code, &hash_data) || hash_get(shallow_table, &search->board, hash_code, &hash_data))) {
		return writeable_level(&hash_data);
	}
	return 0;
}

/**
 * @brief Principal Variation Search algorithm at the root of the tree.
 *
 * this function solves the position provided within the limits set by the alpha
 * and beta bounds. The movelist parameter is updated so that the bestmove is the
 * first of the list when the search ended.
 *
 * @param search Search.
 * @param alpha Alpha bound.
 * @param beta Beta bound.
 * @param depth Remaining depth.
 * @return A score, as a disc difference.
 */
EDAX_CORE_LINKAGE int PVS_root(Search *search, const int alpha, const int beta, const int depth)
{
	unsigned long long hash_code;
	HashStoreData hash_data;
	MoveList *const movelist = &search->movelist;
	Move *move;
	Node node;
	Eval eval0;
	Board board0;
	long long nodes_org = search_count_nodes(search);
	assert(alpha < beta);
	assert(EDAX_SCORE_MIN <= alpha && alpha <= EDAX_SCORE_MAX);
	assert(EDAX_SCORE_MIN <= beta && beta <= EDAX_SCORE_MAX);
	assert(depth > 0 && depth <= search->eval.n_empties);

	search->probcut_level = 0;
	search->result->n_moves_left = search->result->n_moves;

	cassio_debug("PVS_root [%d, %d], %d@%d%%\n", alpha, beta, depth, selectivity_table[search->selectivity].percent);
	if (search->options.verbosity == 4) printf("PVS_root [%d, %d], %d@%d%%\n", alpha, beta, depth, selectivity_table[search->selectivity].percent);
	SEARCH_STATS(++statistics.n_PVS_root);
	SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);

	node_init(&node, search, alpha, beta, depth, movelist->n_moves, NULL);
	node.pv_node = true;
	search->node_type[0] = PV_NODE;
	search->time.can_update = false;

	// special cases: pass or game over
	if (movelist_is_empty(movelist)) {
		move = movelist->move->next = movelist->move + 1;
		move->next = NULL;	// close the list (Nikque nikque.13 895b5c5): the next aspiration_search() walks it
		move->flipped = 0;
		if (can_move(EDAX_OPPONENT(search->board), EDAX_PLAYER(search->board))) {
			search_update_pass_midgame(search, &eval0);
			node.bestscore = move->score = -search_route_PVS(search, -node.beta, -node.alpha, depth, &node);
			search_restore_pass_midgame(search, &eval0);
			node.bestmove =  move->x = EDAX_PASS;
		} else  { // game over
			node.bestscore =  move->score = search_solve(search);
			move->x = EDAX_NOMOVE;
		}

	} else {
		// first move
		board0 = search->board;
		eval0 = search->eval;
		if ((move = node_first_move(&node, movelist))) {
			assert(board_check_move(&search->board, move));
			if (search->current_move_hook) search->current_move_hook(search, move);
			search_update_midgame(search, move); search->node_type[search->height] = PV_NODE;
				move->score = -search_route_PVS(search, -beta, -alpha, depth - 1, &node);
				move->cost = search_get_pv_cost(search);
				assert(EDAX_SCORE_MIN <= move->score && move->score <= EDAX_SCORE_MAX);
				assert(search->stability_bound.lower <= move->score && move->score <= search->stability_bound.upper);
			search_restore_midgame(search, move->x, &eval0);
			search->board = board0;
			if (log_is_open(&search_log)) show_current_move(search_log.f, search, move, alpha, beta, false);
			node_update(&node, move);
			/* A stopped child returns a window bound, not a completed move score. */
			if (!search->stop && search->current_move_result_hook) search->current_move_result_hook(search, move, depth, search->selectivity);
			if (search->options.verbosity == 4) pv_debug(search, move, stdout);

			search->time.can_update = true;

			// other moves : try to refute the first/best one
			while ((move = node_next_move(&node))) {
				const int alpha = depth > search->options.multipv_depth ? node.alpha : EDAX_SCORE_MIN;

				assert(board_check_move(&search->board, move));
				if (depth > search->options.multipv_depth && node_split(&node, move)) {
				} else {
					if (search->current_move_hook) search->current_move_hook(search, move);
					search_update_midgame(search, move);
						move->score = -search_route_PVS(search, -alpha - 1, -alpha, depth - 1, &node);
						if (alpha < move->score && move->score < beta) {
							search->node_type[search->height] = PV_NODE;
							move->score = -search_route_PVS(search, -beta, -alpha, depth - 1, &node);
						}
						move->cost = search_get_pv_cost(search);
					assert(EDAX_SCORE_MIN <= move->score && move->score <= EDAX_SCORE_MAX);
					search_restore_midgame(search, move->x, &eval0);
					search->board = board0;
					if (log_is_open(&search_log)) show_current_move(search_log.f, search, move, alpha, beta, false);
					node_update(&node, move);
					if (!search->stop && search->current_move_result_hook) search->current_move_result_hook(search, move, depth, search->selectivity);
					assert(EDAX_SCORE_MIN <= node.bestscore && node.bestscore <= EDAX_SCORE_MAX);
				}
				if (search->options.verbosity == 4) pv_debug(search, move, stdout);
				if (search_time(search) > search->time.maxi && node.bestscore > alpha) search->stop = STOP_TIMEOUT;
			}
			node_wait_slaves(&node);
		}
	}

	if (!search->stop) {
		hash_code = board_get_hash_code(&search->board);
		hash_get(&search->pv_table, &search->board, hash_code, &hash_data.data);
		if (movelist->n_moves) {	// 4.5.1
			if (depth < search->options.multipv_depth) movelist_sort(movelist);
			else movelist_sort_cost(movelist, &hash_data.data);
			movelist_sort_bestmove(movelist, node.bestmove);
		}
		record_best_move(search, movelist_first(movelist), alpha, beta, depth);

		if (movelist->n_moves == get_mobility(EDAX_PLAYER(search->board), EDAX_OPPONENT(search->board))) {
			hash_data.data.wl.c.depth = depth;
			hash_data.data.wl.c.selectivity = search->selectivity;
			hash_data.data.wl.c.cost = last_bit(search_count_nodes(search) - nodes_org);
			hash_data.data.move[0] = node.bestmove;
			hash_data.alpha = alpha;
			hash_data.beta = beta;
			hash_data.score = node.bestscore;

			hash_store(&search->hash_table, &search->board, hash_code, &hash_data);
			if (search->options.guess_pv) hash_force(&search->pv_table, &search->board, hash_code, &hash_data);
			else hash_store(&search->pv_table, &search->board, hash_code, &hash_data);
		}

		assert(EDAX_SCORE_MIN <= node.bestscore && node.bestscore <= EDAX_SCORE_MAX);

	}

	node_free(&node);

	return node.bestscore;
}
