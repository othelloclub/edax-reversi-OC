/* Ordinary Edax previous-search recovery. GPL-3.0-or-later. */
#include "search-core-hooks.h"
static bool get_last_level(Search *search, int *depth, int *selectivity)
{
	int i, d, s, x;
	Board board;
	Move move;
	unsigned long long hash_code;
	HashData hash_data;

	board = search->board;

	*depth = *selectivity = -1;

	for (i = 0; i < 4; ++i) {
		hash_code = board_get_hash_code(&board);
		if (hash_get(&search->pv_table, &board, hash_code, &hash_data)
		 || hash_get(&search->hash_table, &board, hash_code, &hash_data)) {
			x = hash_data.move[0];
		} else break;

		d = hash_data.wl.c.depth + i;
		s = hash_data.wl.c.selectivity;

		if (d > *depth) *depth = d;
		if (s > *selectivity) *selectivity = s;

		board_get_move_flip(&board, x, &move);
		board_update(&board, &move);

		if (x == EDAX_PASS) --i;
	}

	return *depth > -1 && *selectivity > -1;
}
static bool edax_resume_iteration(Search *search,const HashData *data,int *start,int *score,int *old_depth,int *old_selectivity){
 *old_depth=data->wl.c.depth;*old_selectivity=data->wl.c.selectivity;
 if(USE_PREVIOUS_SEARCH&&data->lower==data->upper){
  if(get_last_level(search,old_depth,old_selectivity)){*start=*old_depth;search->selectivity=*old_selectivity;}
  *score=data->lower;return true;
 }
 return false;
}
