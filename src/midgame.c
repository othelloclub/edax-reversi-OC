/**
 * @file midgame.c
 *
 * Search near the end of the game.
 *
 * @date 1998 - 2025
 * @author Richard Delorme
 * @author Toshihiko Okuhara
 * @version 4.5
 */

#include "search.h"

#include "bit.h"
#include "options.h"
#include "stats.h"
#include "ybwc.h"
#include "settings.h"

#include <assert.h>
#include <stddef.h>
#include <math.h>

#ifndef RCD
/** macro RCD : set to 0.0 if -rcd is added to the icc compiler */
#define RCD 0.5
#endif

/**
 * @brief evaluate a midgame position with the evaluation function.
 *
 * @param ply	60 - n_empties
 * @param eval	Evaluation function.
 * @return An evaluated score.
 */
static int accumlate_eval(int ply, Eval *eval)
{
	unsigned short *f = eval->feature.us;
	const Eval_weight *w;
	int b0, sum;

	b0 = ply & 1;	// even/odd ply interleaved for aligned VPGATHERDD access (4.5.5)
	ply -= 2;
	if (ply < 0)
		ply = 0;
	ply >>= 1;
	if (ply >= EVAL_N_2PLY - 1)
		ply = EVAL_N_2PLY - 2;

#if defined(__AVX2__) && !defined(__bdver4__) && !defined(__znver1__) && !defined(__znver2__)
	static const unsigned long long shift_even[2] = { 16, 0 };
	enum {
		W_C9 = offsetof(Eval_weight, C9) / 4,
		W_C10 = offsetof(Eval_weight, C10) / 4,
		W_S100 = offsetof(Eval_weight, S100) / 4,
		W_S101 = offsetof(Eval_weight, S101) / 4
	};
	w = *EVAL_WEIGHT + ply;
	__m128i SW = _mm_loadl_epi64((__m128i *)(shift_even + b0));	// 16 for even, 0 for odd
	__m256i FF = _mm256_add_epi32(_mm256_cvtepu16_epi32(eval->feature.v8[0]),
		_mm256_set_epi32(W_C10, W_C10, W_C10, W_C10, W_C9, W_C9, W_C9, W_C9));
	__m256i DD = _mm256_i32gather_epi32((int *) w, FF, 4);
	__m256i SS = _mm256_srai_epi32(_mm256_sll_epi32(DD, SW), 16);	// select word and sign extend

	FF = _mm256_add_epi32(_mm256_cvtepu16_epi32(eval->feature.v8[1]),
		_mm256_set_epi32(W_S101, W_S101, W_S101, W_S101, W_S100, W_S100, W_S100, W_S100));
	DD = _mm256_i32gather_epi32((int *) w, FF, 4);
	SS = _mm256_add_epi32(SS, _mm256_srai_epi32(_mm256_sll_epi32(DD, SW), 16));

	DD = _mm256_i32gather_epi32((int *) w->S8x4, _mm256_cvtepu16_epi32(eval->feature.v8[2]), 4);
	SS = _mm256_add_epi32(SS, _mm256_srai_epi32(_mm256_sll_epi32(DD, SW), 16));

	DD = _mm256_i32gather_epi32((int *) w->S7654, _mm256_cvtepu16_epi32(*(__m128i *) &eval->feature.us[30]), 4);
	SS = _mm256_add_epi32(SS, _mm256_srai_epi32(_mm256_sll_epi32(DD, SW), 16));

	DD = _mm256_i32gather_epi32((int *) w->S7654, _mm256_cvtepu16_epi32(*(__m128i *) &eval->feature.us[38]), 4);
	SS = _mm256_add_epi32(SS, _mm256_srai_epi32(_mm256_sll_epi32(DD, SW), 16));
	__m128i S = _mm_add_epi32(_mm256_castsi256_si128(SS), _mm256_extracti128_si256(SS, 1));

	__m128i D = _mm_i32gather_epi32((int *) w->S8x4, _mm_cvtepu16_epi32(eval->feature.v8[3]), 4);
	S = _mm_add_epi32(S, _mm_srai_epi32(_mm_sll_epi32(D, SW), 16));
		// https://stackoverflow.com/questions/6996764/fastest-way-to-do-horizontal-sse-vector-sum-or-other-reduction
	S = _mm_add_epi32(S, _mm_unpackhi_epi64(S, S));
	sum = _mm_cvtsi128_si32(_mm_add_epi32(S, _mm_shufflelo_epi16(S, 0x4e)));
	w = (Eval_weight *)(w->S0 + b0);

#else
	w = (Eval_weight *)((*EVAL_WEIGHT)[ply].S0 + b0);
	sum = *(w->C9[f[0]]) + *(w->C9[f[1]]) + *(w->C9[f[2]]) + *(w->C9[f[3]])
	  + *(w->C10[f[4]]) + *(w->C10[f[5]]) + *(w->C10[f[6]]) + *(w->C10[f[7]])
	  + *(w->S100[f[8]]) + *(w->S100[f[9]]) + *(w->S100[f[10]]) + *(w->S100[f[11]])
	  + *(w->S101[f[12]]) + *(w->S101[f[13]]) + *(w->S101[f[14]]) + *(w->S101[f[15]])
	  + *(w->S8x4[f[16]]) + *(w->S8x4[f[17]]) + *(w->S8x4[f[18]]) + *(w->S8x4[f[19]])
	  + *(w->S8x4[f[20]]) + *(w->S8x4[f[21]]) + *(w->S8x4[f[22]]) + *(w->S8x4[f[23]])
	  + *(w->S8x4[f[24]]) + *(w->S8x4[f[25]]) + *(w->S8x4[f[26]]) + *(w->S8x4[f[27]])
	  + *(w->S7654[f[30]]) + *(w->S7654[f[31]]) + *(w->S7654[f[32]]) + *(w->S7654[f[33]])
	  + *(w->S7654[f[34]]) + *(w->S7654[f[35]]) + *(w->S7654[f[36]]) + *(w->S7654[f[37]])
	  + *(w->S7654[f[38]]) + *(w->S7654[f[39]]) + *(w->S7654[f[40]]) + *(w->S7654[f[41]])
	  + *(w->S7654[f[42]]) + *(w->S7654[f[43]]) + *(w->S7654[f[44]]) + *(w->S7654[f[45]]);
#endif
	return sum + *(w->S8x4[f[28]]) + *(w->S8x4[f[29]]) + *(w->S0);
}

/**
 * @brief evaluate a midgame position with the evaluation function.
 *
 * @param search Position to evaluate.
 * @return An evaluated score.
 */
#include "midgame-leaf-core.inc"

/* ProbCut body is shared with rule adapters; the ordinary build supplies
 * eval_sigma() through search-core-hooks.h. */
#include "probcut-core.inc"

/**
 * @brief Evaluate a midgame position with a Null Window Search algorithm. (No probcut)
 *
 * This function is used when there are still many empty squares on the board. Move
 * ordering, hash table cutoff, enhanced transposition cutoff, etc. are used in
 * order to diminish the size of the tree to analyse, but at the expense of a
 * slower speed.
 * (called from PVS_shallow and NWS_midgame @ depth <= 3)
 *
 * @param search     search.
 * @param alpha      lower bound.
 * @param depth Search remaining depth.
 * @param hash_table Hash Table to use.
 * @return An evaluated score, as a disc difference.
 */
#include "midgame-core.inc"
