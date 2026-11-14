/* Actual ordinary aspiration-window loop, shared by all rule adapters. GPL-3.0-or-later. */
#include "search-core-hooks.h"
static int edax_aspiration_width(int depth,int empties){
 int width=10-depth;if(width<1)width=1;
 if((width&1)&&depth==empties)++width;
 return width;
}
static int search_aspiration_window(Search *search,int alpha,int beta,int depth,int score,int width,int i){
 int low,high,left,right;
	left = right = (i <= 0 ? 1 : i) * width;
	for (;;) {
		low = score - left; if (low < alpha) low = alpha;
		high = score + right; if (high > beta) high = beta;
		if (low >= high) break;
		if (low >= EDAX_SCORE_MAX) low = EDAX_SCORE_MAX - 1;
		if (high <= EDAX_SCORE_MIN) high = EDAX_SCORE_MIN + 1;
		log_print(&search_log, "aspiration search [%d, %d]:\n", low, high);

		score = PVS_root(search, low, high, depth);

		if (search->stop) break;

		if (score <= low && score > alpha && left > 0) {
			left *= 2;
			right = 0;
		} else if (score >= high && score < beta && right > 0) {
			left = 0;
			right *= 2;
		} else break;
	}
 return score;
}
