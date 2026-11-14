/* Ordinary Edax depth-zero/one/two control flow. GPL-3.0-or-later. */
#include "search-core-hooks.h"
EDAX_CORE_LINKAGE int search_eval_0(Search *search)
{
	int score;

	SEARCH_STATS(++statistics.n_search_eval_0);
	SEARCH_UPDATE_EVAL_NODES(search->n_nodes);

	score = accumlate_eval(60 - search->eval.n_empties,  &search->eval);

	score = EDAX_ROUND_EVAL(score);

	return score;
}

/**
 * @brief Evaluate a position at depth 1. (min stage)
 *
 * As an optimization, the last move is used to only updates the evaluation
 * features.
 *
 * @param search Position to evaluate.
 * @param alpha Alpha bound.
 * @param beta Beta bound.
 * @param moves Next turn legal moves.
 * @return An evaluated min score.
 */
EDAX_CORE_LINKAGE int search_eval_1(Search *search, int alpha, int beta, EDAX_BITBOARD moves)
{
	int x, score, bestscore, alphathres;
	EDAX_BITBOARD flipped;
	Eval Ev;
	VBoard board0;
#ifdef EDAX_LEAF_EVAL_CONTEXT
 #ifndef EDAX_LEAF_EVAL_SCORE
  #error "leaf evaluation context requires a score hook"
 #endif
 #ifdef EDAX_LEAF_EVAL_INITIALIZE
	EDAX_LEAF_EVAL_CONTEXT leaf_eval;
	EDAX_LEAF_EVAL_INITIALIZE(&leaf_eval);
 #else
	EDAX_LEAF_EVAL_CONTEXT leaf_eval = {0};
 #endif
#endif

	SEARCH_STATS(++statistics.n_search_eval_1);
	SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);

	if (moves) {
		bestscore = EDAX_SCORE_INF * 128;	// min stage
		if (alpha < EDAX_SCORE_MIN + 1) alphathres = ((EDAX_SCORE_MIN + 1) * 128) + 64;
		else alphathres = (alpha * 128) + 63 + (int) (alpha < 0);	// highest score rounded to alpha

		board0.board = search->board;
		x = EDAX_NOMOVE;
		do {
			do {
				x = search->empties[x].next;
			} while (!(moves & x_to_bit(x)));

			moves &= ~x_to_bit(x);
			flipped = vboard_flip(board0, x);
			if (EDAX_LEAF_WIPEOUT(search, x, flipped))
				return EDAX_SCORE_MIN;	// wipeout

			SEARCH_UPDATE_EVAL_NODES(search->n_nodes);
#ifdef EDAX_LEAF_EVAL_CONTEXT
			score = EDAX_LEAF_EVAL_SCORE(&leaf_eval, search, x, flipped);
#else
			eval_update_leaf(x, flipped, &Ev, &search->eval);
			score = accumlate_eval(60 - search->eval.n_empties + 1, &Ev);
#endif

			if (score < bestscore)
				bestscore = score;
		} while (moves && (bestscore > alphathres));

		bestscore = EDAX_ROUND_EVAL(bestscore);

	} else {
		moves = get_moves(EDAX_OPPONENT(search->board), EDAX_PLAYER(search->board));
		if (moves) {
			search_update_pass_midgame(search, &Ev);
			bestscore = -search_eval_1(search, -beta, -alpha, moves);
			search_restore_pass_midgame(search, &Ev);
		} else { // game over
			bestscore = -search_solve(search);
		}
	}

	assert(EDAX_SCORE_MIN <= bestscore && bestscore <= EDAX_SCORE_MAX);
	return bestscore;
}

/**
 * @brief Evaluate a position at depth 2.
 *
 * Simple alpha-beta with no move sorting.
 *
 * @param search Position to evaluate.
 * @param alpha Lower bound
 * @param beta  Upper bound
 * @param moves Next turn legal moves.
 * @return An evaluated best score.
 */
EDAX_CORE_LINKAGE int search_eval_2(Search *search, int alpha, int beta, EDAX_BITBOARD moves)
{
	int x, bestscore, score;
	EDAX_BITBOARD flipped;
	Eval eval0;
	VBoard board0;

	SEARCH_STATS(++statistics.n_search_eval_2);
	SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);

	assert(-EDAX_SCORE_MAX <= alpha);
	assert(beta <= EDAX_SCORE_MAX);
	assert(alpha <= beta);

	if (moves) {
		bestscore = -EDAX_SCORE_INF;
		eval0.feature = search->eval.feature;
		eval0.n_empties = search->eval.n_empties--;
		board0.board = search->board;
		x = EDAX_NOMOVE;
		do {
			do {
				x = search->empties[x].next;
			} while (!(moves & x_to_bit(x)));

			moves &= ~x_to_bit(x);
			// search->empties[prev].next = search->empties[x].next;	// let search_eval_1 skip the last occupied
			flipped = vboard_next(board0, x, &search->board);
			eval_update_leaf(x, flipped, &search->eval, &eval0);
			score = search_eval_1(search, alpha, beta, board_get_moves(&search->board));
			// search->empties[prev].next = x;	// restore

			if (score > bestscore) {
				bestscore = score;
				if (bestscore >= beta) break;
				else if (bestscore > alpha) alpha = bestscore;
			}
		} while (moves);
		search->eval.feature = eval0.feature;
		search->eval.n_empties = eval0.n_empties;
		search->board = board0.board;

	} else {
		moves = get_moves(EDAX_OPPONENT(search->board), EDAX_PLAYER(search->board));
		if (moves) {
			search_update_pass_midgame(search, &eval0);
			bestscore = -search_eval_2(search, -beta, -alpha, moves);
			search_restore_pass_midgame(search, &eval0);
		} else { // game over
			bestscore = search_solve(search);
		}
	}

	assert(EDAX_SCORE_MIN <= bestscore && bestscore <= EDAX_SCORE_MAX);

	return bestscore;
}
