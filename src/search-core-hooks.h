/* Explicit ordinary defaults and mandatory rule-adapter contract. GPL-3.0-or-later. */
#ifndef EDAX_SEARCH_CORE_HOOKS_H
#define EDAX_SEARCH_CORE_HOOKS_H
#ifndef EDAX_VARIANT_ADAPTER
#define EDAX_CORE_LINKAGE
#define EDAX_FULL_LINES(v) get_all_full_lines(v)
#define EDAX_STABILITY_FULLS(b,f) vget_opp_statility_fulls((b),(f))
#define EDAX_SOLID_OFFSET(v) (bit_count(v)*2)
#define EDAX_PLAYER(b) ((b).player)
#define EDAX_OPPONENT(b) ((b).opponent)
static inline int edax_round_eval_default(int score){score=(score+(score>0?64:-64))/128;return score<SCORE_MIN+1?SCORE_MIN+1:score>SCORE_MAX-1?SCORE_MAX-1:score;}
#define EDAX_ROUND_EVAL(v) edax_round_eval_default(v)
#define EDAX_LEAF_WIPEOUT(s,x,f) ((f)==(s)->board.opponent)
#define EDAX_ORDERING_INDEX(n) (n)
#define EDAX_BITBOARD unsigned long long
#define EDAX_SCORE_MIN SCORE_MIN
#define EDAX_SCORE_MAX SCORE_MAX
#define EDAX_SCORE_INF SCORE_INF
#define EDAX_NOMOVE NOMOVE
#define EDAX_PASS PASS
#define EDAX_QUADRANT_IDS QUADRANT_ID
#define EDAX_QUADRANT_MASKS quadrant_mask
#define EDAX_SQUARE_VALUES SQUARE_VALUE
#define EDAX_EMPTY_COUNT(b) bit_count(~((b).player | (b).opponent))
#define EDAX_NWS_STABILITY(n) NWS_STABILITY_THRESHOLD[n]
#define EDAX_PVS_STABILITY(n) PVS_STABILITY_THRESHOLD[n]
#define EDAX_LOCAL_HASH_MATCH(e,b) vboard_equal((b), &(e)->board)
#define EDAX_EVEN_SCORE(v) (((v) & 1) == 0)
#define EDAX_PROBCUT_SIGMA(s,d,p) eval_sigma((s)->eval.n_empties,(d),(p))
#define EDAX_ETC_NEXT(next,b,m) do { (next).opponent=(b).player^((m)->flipped|x_to_bit((m)->x)); (next).player=(b).opponent^(m)->flipped; } while(0)
#else
#ifndef EDAX_FULL_LINES
#error "Rule adapter must define EDAX_FULL_LINES"
#endif
#ifndef EDAX_STABILITY_FULLS
#error "Rule adapter must define EDAX_STABILITY_FULLS"
#endif
#ifndef EDAX_SOLID_OFFSET
#error "Rule adapter must define EDAX_SOLID_OFFSET"
#endif
#ifndef EDAX_CORE_LINKAGE
#error "Rule adapter must define EDAX_CORE_LINKAGE"
#endif
#ifndef EDAX_PLAYER
#error "Rule adapter must define EDAX_PLAYER"
#endif
#ifndef EDAX_OPPONENT
#error "Rule adapter must define EDAX_OPPONENT"
#endif
#ifndef EDAX_ROUND_EVAL
#error "Rule adapter must define EDAX_ROUND_EVAL"
#endif
#ifndef EDAX_LEAF_WIPEOUT
#error "Rule adapter must define EDAX_LEAF_WIPEOUT"
#endif
#ifndef EDAX_ORDERING_INDEX
#error "Rule adapter must define EDAX_ORDERING_INDEX"
#endif
#ifndef EDAX_BITBOARD
#error "Rule adapter must define EDAX_BITBOARD"
#endif
#ifndef EDAX_SCORE_MIN
#error "Rule adapter must define EDAX_SCORE_MIN"
#endif
#ifndef EDAX_SCORE_MAX
#error "Rule adapter must define EDAX_SCORE_MAX"
#endif
#ifndef EDAX_SCORE_INF
#error "Rule adapter must define EDAX_SCORE_INF"
#endif
#ifndef EDAX_NOMOVE
#error "Rule adapter must define EDAX_NOMOVE"
#endif
#ifndef EDAX_PASS
#error "Rule adapter must define EDAX_PASS"
#endif
#ifndef EDAX_QUADRANT_IDS
#error "Rule adapter must define EDAX_QUADRANT_IDS"
#endif
#ifndef EDAX_QUADRANT_MASKS
#error "Rule adapter must define EDAX_QUADRANT_MASKS"
#endif
#ifndef EDAX_SQUARE_VALUES
#error "Rule adapter must define EDAX_SQUARE_VALUES"
#endif
#ifndef EDAX_EMPTY_COUNT
#error "Rule adapter must define EDAX_EMPTY_COUNT"
#endif
#ifndef EDAX_NWS_STABILITY
#error "Rule adapter must define EDAX_NWS_STABILITY"
#endif
#ifndef EDAX_PVS_STABILITY
#error "Rule adapter must define EDAX_PVS_STABILITY"
#endif
#ifndef EDAX_LOCAL_HASH_MATCH
#error "Rule adapter must define EDAX_LOCAL_HASH_MATCH"
#endif
#ifndef EDAX_EVEN_SCORE
#error "Rule adapter must define EDAX_EVEN_SCORE"
#endif
#ifndef EDAX_ETC_NEXT
#error "Rule adapter must define EDAX_ETC_NEXT"
#endif
#ifndef EDAX_PROBCUT_SIGMA
#error "Rule adapter must define EDAX_PROBCUT_SIGMA"
#endif
#endif
#endif
