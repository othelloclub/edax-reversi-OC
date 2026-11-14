/* Ordinary Edax move-list selection. GPL-3.0-or-later. */
#include "search-core-hooks.h"
EDAX_CORE_LINKAGE Move* move_next_best(Move *previous_best)
{
	Move *move = previous_best->next;
	if (move && move->next) {	// at least 2 elements
		Move *best = previous_best;
		do {
			if (move->next->score > best->next->score)
				best = move;
			move = move->next;
		} while (move->next);
		// if (previous_best != best) {
		move = best->next;
		best->next = move->next;
		move->next = previous_best->next;
		previous_best->next = move;
		// }
	}
	return previous_best->next;
}
EDAX_CORE_LINKAGE void movelist_sort(MoveList *movelist)
{
	// foreach_best_move(move, *movelist) ;

	Move *previous_best = &movelist->move[0];
	while (previous_best->next->next) {	// until last 2
		Move *best = previous_best;
		Move *move = previous_best->next;
		do {
			if (move->next->score > best->next->score)
				best = move;
			move = move->next;
		} while (move->next);
		// if (previous_best != best) {
		move = best->next;
		best->next = move->next;
		move->next = previous_best->next;
		previous_best->next = move;
		// }
		previous_best = previous_best->next;
	}
}

static Move* move_next_most_expensive(Move *previous_best)
{
	Move *move = previous_best->next;
	if (move && move->next) {	// at least 2 elements
		Move *best = previous_best;
		do {
			if (move->next->cost > best->next->cost)
				best = move;
			move = move->next;
		} while (move->next);
		// if (previous_best != best) {
		move = best->next;
		best->next = move->next;
		move->next = previous_best->next;
		previous_best->next = move;
		// }
	}
	return previous_best->next;
}
EDAX_CORE_LINKAGE Move* movelist_sort_bestmove(MoveList *movelist, const int move)
{
	Move *iter, *m;

	for (iter = &movelist->move[0]; (m = iter->next); iter = m) {
		if (m->x == move) {
			iter->next = m->next;
			m->next = movelist->move[0].next;
			movelist->move[0].next = m;
			break;
		}
	}
	return iter;
}
EDAX_CORE_LINKAGE void movelist_sort_cost(MoveList *movelist, const HashData *hash_data)
{
	Move *iter, *prev, *m, *hashmove0, *hashmove1;

	hashmove0 = hashmove1 = NULL;
	prev = &movelist->move[0];
	while ((m = prev->next)) {
		if (m->x == hash_data->move[0]) {
			hashmove0 = m;
			prev->next = m->next;
			if (hashmove1)
				break;

		} else if (m->x == hash_data->move[1]) {
			hashmove1 = m;
			prev->next = m->next;
			if (hashmove0)
				break;
		} else
			prev = m;
	}

	iter = &movelist->move[0];
	if (hashmove0) {
		hashmove0->next = iter->next;
		iter = iter->next = hashmove0;
	}
	if (hashmove1) {
		hashmove1->next = iter->next;
		iter = iter->next = hashmove1;
	}

	while ((iter = move_next_most_expensive(iter)))
		;
}
EDAX_CORE_LINKAGE Move* movelist_exclude(MoveList *movelist, const int move)
{
	Move *iter, *m;

	for (iter = &movelist->move[0]; (m = iter->next); iter = m) {
		if (m->x == move) {
			iter->next = m->next;
			--movelist->n_moves;
			break;
		}
	}
	return m;
}
