/* Ordinary Edax ProbCut, shared with rule adapters. GPL-3.0-or-later.
 * Original authors: Richard Delorme, Toshihiko Okuhara.
 * Rule adapters supply only the measured error model (EDAX_PROBCUT_SIGMA)
 * and their score limits; the probe depths, selectivity table, static-eval
 * prefilter and recursion limit are the ordinary ones. */
#include "search-core-hooks.h"

#ifndef RCD
/** macro RCD : set to 0.0 if -rcd is added to the icc compiler */
#define RCD 0.5
#endif

/**
 * @brief Probcut
 *
 * Check if a position is worth to analyze further.
 *
 * @param search Position to test.
 * @param alpha Alpha lower bound.
 * @param depth Search depth.
 * @param parent Parent node.
 * @param value Returned value.
 * @return true if probable cutoff has been found, false otherwise.
 */
static inline void search_update_probcut(Search *search, const NodeType node_type)
{
	search->node_type[search->height] = node_type;
	if (!USE_RECURSIVE_PROBCUT) search->selectivity = NO_SELECTIVITY;
	LIMIT_RECURSIVE_PROBCUT(++search->probcut_level;)
}


static inline void search_restore_probcut(Search *search, const NodeType node_type, const int selectivity)
{
	search->node_type[search->height] = node_type;
	if (!USE_RECURSIVE_PROBCUT) search->selectivity = selectivity;
	LIMIT_RECURSIVE_PROBCUT(--search->probcut_level;)
}

static bool search_probcut(Search *search, const int alpha, const int depth, Node *parent, int *value)
{
	// assertion
	assert(search != NULL);
	assert(parent != NULL);
	assert(search->node_type[search->height] != PV_NODE);
	assert(0 <= search->selectivity && search->selectivity <= NO_SELECTIVITY);

	// do probcut ?
	if (USE_PROBCUT && depth >= options.probcut_d && search->selectivity < NO_SELECTIVITY	LIMIT_RECURSIVE_PROBCUT(&& search->probcut_level < 2)) {
		int probcut_error, eval_error;
		int probcut_depth, probcut_beta, probcut_alpha;
		int eval_score, eval_beta, eval_alpha;
		int score;
		const int beta = alpha + 1;
		double t = selectivity_table[search->selectivity].t;
		const int saved_selectivity = search->selectivity;
		const NodeType node_type = search->node_type[search->height];

		PROBCUT_STATS(++statistics.n_probcut_try);

		// compute reduced depth & associated error
		probcut_depth = 2 * floor(options.probcut_d * depth) + (depth & 1);
		if (probcut_depth == 0) probcut_depth = depth - 2;
		assert(probcut_depth > 1 && probcut_depth <= depth - 2 && (probcut_depth & 1) == (depth & 1));
		probcut_error = t * EDAX_PROBCUT_SIGMA(search, depth, probcut_depth) + RCD;

		// compute evaluation error (i.e. error at depth 0) averaged for both depths
		eval_score = search_eval_0(search);
		eval_error = t * 0.5 * (EDAX_PROBCUT_SIGMA(search, depth, 0) + EDAX_PROBCUT_SIGMA(search, depth, probcut_depth)) + RCD;

		// try a probable upper cut first
		eval_beta = beta - eval_error;
		probcut_beta = beta + probcut_error;
		probcut_alpha = probcut_beta - 1;
		if (eval_score >= eval_beta && probcut_beta < EDAX_SCORE_MAX) { 	// check if trying a beta probcut is worth
			PROBCUT_STATS(++statistics.n_probcut_high_try);
			search_update_probcut(search, CUT_NODE);
			score = NWS_midgame(search, probcut_alpha, probcut_depth, parent);
			search_restore_probcut(search, node_type, saved_selectivity);
			if (score >= probcut_beta) {
				*value = beta;
				PROBCUT_STATS(++statistics.n_probcut_high_cutoff);
				return true;
			}
		}

		// try a probable lower cut if upper cut failed
		eval_alpha = alpha + eval_error;
		probcut_alpha = alpha - probcut_error;
		if (eval_score < eval_alpha && probcut_alpha > EDAX_SCORE_MIN) { // check if trying an alpha probcut is worth
			PROBCUT_STATS(++statistics.n_probcut_low_try);
			search_update_probcut(search, ALL_NODE);
			score = NWS_midgame(search, probcut_alpha, probcut_depth, parent);
			search_restore_probcut(search, node_type, saved_selectivity);
			if (score <= probcut_alpha) {
				*value = alpha;
				PROBCUT_STATS(++statistics.n_probcut_low_cutoff);
				return true;
			}
		}
	}

	return false;
}
