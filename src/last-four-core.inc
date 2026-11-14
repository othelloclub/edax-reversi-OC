/* Actual portable Edax last-2/3/4 control flow, shared with rule adapters.
 * Original authors: Richard Delorme, Toshihiko Okuhara. GPL-3.0-or-later.
 * Board, terminal scoring, inventory and geometry are the only hooks. */
#include "search-core-hooks.h"
#ifndef EDAX_LAST_BOARD
#define EDAX_LAST_BOARD Board
static Board edax_last_next(Board b, int x, unsigned long long f) {
 Board next = { b.opponent ^ f, b.player ^ (f | x_to_bit(x)) }; return next;
}
static Board edax_last_pass(Board b) { Board next = {b.opponent, b.player}; return next; }
#define EDAX_LAST_NEXT(b,x,f) edax_last_next((b),(x),(f))
#define EDAX_LAST_PASS(b) edax_last_pass(b)
#define EDAX_LAST_FLIP(b,x) Flip((x),(b).player,(b).opponent)
#define EDAX_LAST_NEIGHBOUR(b,x) (NEIGHBOUR[x] & (b).opponent)
#define EDAX_LAST_SOLVE_ONE(b,a,x) solve_1((b).player,(a),(x))
#define EDAX_LAST_FINAL(b,n) board_solve((b).player,(n))
#define EDAX_LAST_SC(b,a,s) search_SC_NWS_4((b).player,(b).opponent,(a),(s))
#define EDAX_LAST_CHECK(s) ((void)0)
#define EDAX_LAST_PARITY_INDEX(x1,x2,x3,x4) (((x3 ^ x4) & 0x24) + ((((x2 ^ x4) & 0x24) * 2 + ((x1 ^ x4) & 0x24)) >> 2))
#endif
/**
 * @brief Get the final score.
 *
 * Get the final min score, when 2 empty squares remain.
 *
 * @param player Board.player to evaluate.
 * @param opponent Board.opponent to evaluate.
 * @param alpha Alpha bound.
 * @param x1 First empty square coordinate.
 * @param x2 Second empty square coordinate.
 * @param n_nodes Node counter.
 * @return The final min score, as a disc difference.
 */
static int solve_2(EDAX_LAST_BOARD board, int alpha, int x1, int x2, volatile unsigned long long *n_nodes)
{
	EDAX_BITBOARD flipped;
	int score, bestscore, leaf_nodes;
	// const int beta = alpha + 1;

	SEARCH_STATS(++statistics.n_solve_2);

	if (EDAX_LAST_NEIGHBOUR(board, x1) && (flipped = EDAX_LAST_FLIP(board, x1))) {	// (84%/84%)
		bestscore = EDAX_LAST_SOLVE_ONE(EDAX_LAST_NEXT(board, x1, flipped), alpha, x2);

		if ((bestscore > alpha) && EDAX_LAST_NEIGHBOUR(board, x2) && (flipped = EDAX_LAST_FLIP(board, x2))) {	// (50%/93%/92%)
			score = EDAX_LAST_SOLVE_ONE(EDAX_LAST_NEXT(board, x2, flipped), alpha, x1);
			if (score < bestscore)
				bestscore = score;
			leaf_nodes = 3;
		} else	leaf_nodes = 2;

	} else if (EDAX_LAST_NEIGHBOUR(board, x2) && (flipped = EDAX_LAST_FLIP(board, x2))) {	// (96%/75%)
		bestscore = EDAX_LAST_SOLVE_ONE(EDAX_LAST_NEXT(board, x2, flipped), alpha, x1);
		leaf_nodes = 2;

	} else {	// pass (17%) - NEIGHBOUR test is almost 100% true
		alpha = ~alpha;	// = -alpha - 1
		if ((flipped = EDAX_LAST_FLIP(EDAX_LAST_PASS(board), x1))) {	// (95%)
			bestscore = EDAX_LAST_SOLVE_ONE(EDAX_LAST_NEXT(EDAX_LAST_PASS(board), x1, flipped), alpha, x2);

			if ((bestscore > alpha) && (flipped = EDAX_LAST_FLIP(EDAX_LAST_PASS(board), x2))) {	// (20%/100%)
				score = EDAX_LAST_SOLVE_ONE(EDAX_LAST_NEXT(EDAX_LAST_PASS(board), x2, flipped), alpha, x1);
				if (score < bestscore)
					bestscore = score;
				leaf_nodes = 3;
			} else	leaf_nodes = 2;

		} else if ((flipped = EDAX_LAST_FLIP(EDAX_LAST_PASS(board), x2))) {	// (97%)
			bestscore = EDAX_LAST_SOLVE_ONE(EDAX_LAST_NEXT(EDAX_LAST_PASS(board), x2, flipped), alpha, x1);
			leaf_nodes = 2;

		} else {	// gameover
			bestscore = EDAX_LAST_FINAL(board, 2);
			leaf_nodes = 1;
		}
		bestscore = -bestscore;
	}

	SEARCH_UPDATE_2EMPTIES_NODES(*n_nodes += leaf_nodes;)
	assert(EDAX_SCORE_MIN <= bestscore && bestscore <= EDAX_SCORE_MAX);
	assert(EDAX_EVEN_SCORE(bestscore));
	return bestscore;
}

/**
 * @brief Get the final score.
 *
 * Get the final max score, when 3 empty squares remain.
 *
 * @param player Board.player to evaluate.
 * @param opponent Board.opponent to evaluate.
 * @param alpha Alpha bound.
 * @param shuf3 Parity flags.
 * @param empties Packed empty square coordinates.
 * @param n_nodes Node counter.
 * @return The final max score, as a disc difference.
 */
static int solve_3(EDAX_LAST_BOARD board, int alpha, unsigned int shuf3, unsigned int empties3, volatile unsigned long long *n_nodes)
{
	EDAX_BITBOARD flipped;
	EDAX_LAST_BOARD next;
	int score, bestscore, pol;
	// const int beta = alpha + 1;
	int x1 = (empties3 >> ((shuf3 & 0x30) >> 1)) & 0xFF;
	int x2 = (empties3 >> ((shuf3 & 0x0C) << 1)) & 0xFF;
	int x3 = (empties3 >> ((shuf3 & 0x03) * 8)) & 0xFF;

	SEARCH_STATS(++statistics.n_solve_3);
	SEARCH_UPDATE_INTERNAL_NODES(*n_nodes);

	bestscore = -EDAX_SCORE_INF;
	pol = 1;
	do {
		// best move alphabeta search
		if (EDAX_LAST_NEIGHBOUR(board, x1) && (flipped = EDAX_LAST_FLIP(board, x1))) {	// (89%/91%)
			next = EDAX_LAST_NEXT(board, x1, flipped);
			bestscore = solve_2(next, alpha, x2, x3, n_nodes);
			if (bestscore > alpha) return bestscore * pol;	// (78%/63%)
		}

		if (/* EDAX_LAST_NEIGHBOUR(board, x2) && */ (flipped = EDAX_LAST_FLIP(board, x2))) {	// (97%/78%)
			next = EDAX_LAST_NEXT(board, x2, flipped);
			score = solve_2(next, alpha, x1, x3, n_nodes);
			if (score > alpha) return score * pol;	// (32%/9%)
			else if (score > bestscore) bestscore = score;
		}

		if (/* EDAX_LAST_NEIGHBOUR(board, x3) && */ (flipped = EDAX_LAST_FLIP(board, x3))) {	// (100%/89%)
			next = EDAX_LAST_NEXT(board, x3, flipped);
			score = solve_2(next, alpha, x1, x2, n_nodes);
			if (score > bestscore) bestscore = score;
			return bestscore * pol;	// (26%)
		}

		if (bestscore > -EDAX_SCORE_INF)	// (76%)
			return bestscore * pol;	// (9%)

		board = EDAX_LAST_PASS(board);	// pass
		alpha = ~alpha;	// = -(alpha + 1)
	} while ((pol = -pol) < 0);

	return EDAX_LAST_FINAL(board, 3);	// gameover
}

/**
 * @brief Get the final score.
 *
 * Get the final min score, when 4 empty squares remain.
 *
 * @param search Search position.
 * @param alpha Upper score value.
 * @return The final min score, as a disc difference.
 */
static int search_solve_4(Search *search, int alpha)
{
	EDAX_BITBOARD flipped;
	EDAX_LAST_BOARD board, next;
	int x1, x2, x3, x4, paritysort, score, bestscore, pol;
	// const int beta = alpha + 1;
	static const unsigned char parity_case[64] = {	/* x4x3x2x1 = */
		/*0000*/  0, /*0001*/  0, /*0010*/  1, /*0011*/  9, /*0100*/  2, /*0101*/ 10, /*0110*/ 11, /*0111*/  3,
		/*0002*/  0, /*0003*/  0, /*0012*/  0, /*0013*/  0, /*0102*/  4, /*0103*/  4, /*0112*/  5, /*0113*/  5,
		/*0020*/  1, /*0021*/  0, /*0030*/  1, /*0031*/  0, /*0120*/  6, /*0121*/  7, /*0130*/  6, /*0131*/  7,
		/*0022*/  9, /*0023*/  0, /*0032*/  0, /*0033*/  9, /*0122*/  8, /*0123*/  0, /*0132*/  0, /*0133*/  8,
		/*0200*/  2, /*0201*/  4, /*0210*/  6, /*0211*/  8, /*0300*/  2, /*0301*/  4, /*0310*/  6, /*0311*/  8,
		/*0202*/ 10, /*0203*/  4, /*0212*/  7, /*0213*/  0, /*0302*/  4, /*0303*/ 10, /*0312*/  0, /*0313*/  7,
		/*0220*/ 11, /*0221*/  5, /*0230*/  6, /*0231*/  0, /*0320*/  6, /*0321*/  0, /*0330*/ 11, /*0331*/  5,
		/*0222*/  3, /*0223*/  5, /*0232*/  7, /*0233*/  8, /*0322*/  8, /*0323*/  7, /*0332*/  5, /*0333*/  3
	};
	unsigned int empties_series, shuf4;
	static const unsigned int sort4_shuf[] = {	// 4.5.5: use shuffle mask in non-simd version too.
		0x3978b4e4,	//  0: 1(x1) 3(x2 x3 x4), 1(x1) 1(x2) 2(x3 x4), 1 1 1 1, 4		x4x1x2x3-x3x1x2x4-x2x1x3x4-x1x2x3x4
		0x3978e4b4,	//  1: 1(x2) 3(x1 x3 x4)	x4x1x2x3-x3x1x2x4-x1x2x3x4-x2x1x3x4
		0x39b4e478,	//  2: 1(x3) 3(x1 x2 x4)	x4x1x2x3-x2x1x3x4-x1x2x3x4-x3x1x2x4
		0x78b4e439,	//  3: 1(x4) 3(x1 x2 x3)	x3x1x2x4-x2x1x3x4-x1x2x3x4-x4x1x2x3
		0x39b478d8,	//  4: 1(x1) 1(x3) 2(x2 x4)	x4x1x2x3-x2x1x3x4-x3x1x2x4-x1x3x2x4
		0x78b439c9,	//  5: 1(x1) 1(x4) 2(x2 x3)	x3x1x2x4-x2x1x3x4-x4x1x2x3-x1x4x2x3
		0x39e46c9c,	//  6: 1(x2) 1(x3) 2(x1 x4)	x4x1x2x3-x1x2x3x4-x3x2x1x4-x2x3x1x4
		0x78e42d8d,	//  7: 1(x2) 1(x4) 2(x1 x3)	x3x1x2x4-x1x2x3x4-x4x2x1x3-x2x4x1x3
		0xb4e41e4e,	//  8: 1(x3) 1(x4) 2(x1 x2)	x2x1x3x4-x1x2x3x4-x4x3x1x2-x3x4x1x2
		0x1e4eb4e4,	//  9: 2(x1 x2) 2(x3 x4)	x4x3x1x2-x3x4x1x2-x2x1x3x4-x1x2x3x4
		0x2d788dd8,	// 10: 2(x1 x3) 2(x2 x4)	x4x2x1x3-x3x1x2x4-x2x4x1x3-x1x3x2x4
		0x396c9cc9	// 11: 2(x1 x4) 2(x2 x3)	x4x1x2x3-x3x2x1x4-x2x3x1x4-x1x4x2x3
	};

	SEARCH_STATS(++statistics.n_search_solve_4);
	SEARCH_UPDATE_INTERNAL_NODES(search->n_nodes);

	// stability cutoff (try 12%, cut 7%)
	board = search->board;
	EDAX_LAST_CHECK(search);
	if (EDAX_LAST_SC(board, alpha, &score)) return score;

	x1 = search->empties[EDAX_NOMOVE].next;
	x2 = search->empties[x1].next;
	x3 = search->empties[x2].next;
	x4 = search->empties[x3].next;

	// parity based move sorting.
	// The following hole sizes are possible:
	//    4 - 1 3 - 2 2 - 1 1 2 - 1 1 1 1
	// Only the 1 1 2 case needs move sorting on this ply.
	// 4.5.5: prefer 1 empty over 3 empties, 1 3 case also needs sorting.
	paritysort = parity_case[EDAX_LAST_PARITY_INDEX(x1, x2, x3, x4)];
	shuf4 = sort4_shuf[paritysort];
	empties_series = (x1 << 24) | (x2 << 16) | (x3 << 8) | x4;

	bestscore = EDAX_SCORE_INF;	// min stage
	pol = 1;
	do {
		// best move alphabeta search
		x1 = (empties_series >> ((shuf4 >> (6 - 3)) & 0x18)) & 0xFF;
		if (EDAX_LAST_NEIGHBOUR(board, x1) && (flipped = EDAX_LAST_FLIP(board, x1))) {	// (76%/77%)
			next = EDAX_LAST_NEXT(board, x1, flipped);
			bestscore = solve_3(next, alpha, shuf4, empties_series, &search->n_nodes);
			if (bestscore <= alpha) return bestscore * pol;	// (68%)
		}

		x2 = (empties_series >> ((shuf4 >> (14 - 3)) & 0x18)) & 0xFF;
		if (EDAX_LAST_NEIGHBOUR(board, x2) && (flipped = EDAX_LAST_FLIP(board, x2))) {	// (87%/84%)
			next = EDAX_LAST_NEXT(board, x2, flipped);
			score = solve_3(next, alpha, shuf4 >> 8, empties_series, &search->n_nodes);
			if (score <= alpha) return score * pol;	// (37%)
			else if (score < bestscore) bestscore = score;
		}

		x3 = (empties_series >> ((shuf4 >> (22 - 3)) & 0x18)) & 0xFF;
		if (EDAX_LAST_NEIGHBOUR(board, x3) && (flipped = EDAX_LAST_FLIP(board, x3))) {	// (77%/80%)
			next = EDAX_LAST_NEXT(board, x3, flipped);
			score = solve_3(next, alpha, shuf4 >> 16, empties_series, &search->n_nodes);
			if (score <= alpha) return score * pol;	// (14%)
			else if (score < bestscore) bestscore = score;
		}

		x4 = (empties_series >> ((shuf4 >> 30) * 8)) & 0xFF;
		if (EDAX_LAST_NEIGHBOUR(board, x4) && (flipped = EDAX_LAST_FLIP(board, x4))) {	// (79%/88%)
			next = EDAX_LAST_NEXT(board, x4, flipped);
			score = solve_3(next, alpha, shuf4 >> 24, empties_series, &search->n_nodes);
			if (score < bestscore) bestscore = score;
			return bestscore * pol;	// (37%)
		}

		if (bestscore < EDAX_SCORE_INF)	// (72%)
			return bestscore * pol;	// (13%)

		board = EDAX_LAST_PASS(board);	// pass
		alpha = ~alpha;	// = -(alpha + 1)
	} while ((pol = -pol) < 0);

	return -EDAX_LAST_FINAL(board, 4);	// gameover
}
