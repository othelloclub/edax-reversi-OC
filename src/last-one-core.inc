/* Actual Edax bitscan last-one lazy cutoff, with rule/board hooks.
 * Original authors: Richard Delorme, Toshihiko Okuhara (adapted from Zebra).
 * GPL-3.0-or-later. */
#ifndef EDAX_LAST_ONE_TYPE
#define EDAX_LAST_ONE_TYPE unsigned long long
#define EDAX_LAST_ONE_LINKAGE
#define EDAX_LAST_ONE_BASE(p) (2 * bit_count(p) - 64 + 2)
#define EDAX_LAST_ONE_FLIPS(p,x) (*count_last_flip[x])(p)
#define EDAX_LAST_ONE_OPP_FLIPS(p,x) (*count_last_flip[x])(~(p))
#define EDAX_LAST_ONE_SIGN(v) (v)
#define EDAX_LAST_ONE_FINAL(p,s,s2) ((s)<=0?(s2):(s))
#define EDAX_LAST_ONE_CHECK_OPP(p,s,a) ((s)>(a))
#endif
EDAX_LAST_ONE_LINKAGE int solve_1(EDAX_LAST_ONE_TYPE player, int alpha, int x)
{
	int score, score2, n_flips;

	score = EDAX_LAST_ONE_BASE(player);	// = (bit_count(P) + 1) - (SCORE_MAX - 1 - bit_count(P))

	n_flips = EDAX_LAST_ONE_FLIPS(player, x);
	score = EDAX_LAST_ONE_SIGN(score + n_flips);

	if (n_flips == 0) {	// (23%)
		score2 = EDAX_LAST_ONE_SIGN(EDAX_LAST_ONE_BASE(player) - 2);	// empty for opponent
		score = EDAX_LAST_ONE_FINAL(player, score, score2);
		if (EDAX_LAST_ONE_CHECK_OPP(player, score, alpha)) {	// lazy cut-off (40%)
			if ((n_flips = EDAX_LAST_ONE_OPP_FLIPS(player, x)) != 0)	// (98%)
				score = score2 - EDAX_LAST_ONE_SIGN(n_flips);
		}
	}

	return score;
}
