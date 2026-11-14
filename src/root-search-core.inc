/* Ordinary Edax root routing and stability bounds. GPL-3.0-or-later. */
#include "search-core-hooks.h"
EDAX_CORE_LINKAGE int search_bound(const Search *search, int score)
{
	if (score < search->stability_bound.lower) score = search->stability_bound.lower;
	if (score > search->stability_bound.upper) score = search->stability_bound.upper;
	return score;
}

/**
 * @brief Reroute the PVS between midgame,endgame or terminal PVS.
 *
 * @param search Search.
 * @param alpha Alpha bound.
 * @param beta Beta bound.
 * @param depth Depth.
 * @param node Node position.
 * @return A score, as a disc difference.
 */
static int search_route_PVS(Search *search, int alpha, int beta, const int depth, Node *node)
{
	int score;

	assert(alpha < beta);
	assert(EDAX_SCORE_MIN <= alpha);
	assert(beta <= EDAX_SCORE_MAX);
	assert(depth >= 0 && depth <= search->eval.n_empties);

	if (depth == search->eval.n_empties) {
		if (depth == 0) score = search_solve_0(search);
		else score = PVS_midgame(search, alpha, beta, depth, node);
	} else {
		if (depth == 0) score = search_eval_0(search);
		else if (depth == 1) score = -search_eval_1(search, -beta, -alpha, board_get_moves(&search->board));
		else if (depth == 2) score = search_eval_2(search, alpha, beta, board_get_moves(&search->board));
		else score = PVS_midgame(search, alpha, beta, depth, node);
	}

	score = -search_bound(search, -score);
	assert(EDAX_SCORE_MIN <= score && score <= EDAX_SCORE_MAX);

	return score;
}
