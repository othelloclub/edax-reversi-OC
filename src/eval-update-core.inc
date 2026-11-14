/* Ordinary incremental vector update, shared by model-specific feature rows.
 * Original Edax update by Toshihiko Okuhara. GPL-3.0-or-later. */
#ifndef EDAX_EVAL_VECTOR_HELPERS
#define EDAX_EVAL_VECTOR_HELPERS
#include <string.h>
#if defined(__AVX2__)
#define EDAX_EVAL_VECTOR_AVAILABLE 1
#define EDAX_EVAL_LANES 16
typedef __m256i EdaxEvalVector;
#define edax_eval_add _mm256_add_epi16
#define edax_eval_sub _mm256_sub_epi16
#elif defined(__ARM_NEON)
#define EDAX_EVAL_VECTOR_AVAILABLE 1
#define EDAX_EVAL_LANES 8
typedef uint16x8_t EdaxEvalVector;
#define edax_eval_add vaddq_u16
#define edax_eval_sub vsubq_u16
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define EDAX_EVAL_VECTOR_AVAILABLE 1
#define EDAX_EVAL_LANES 8
typedef v128_t EdaxEvalVector;
#define edax_eval_add wasm_i16x8_add
#define edax_eval_sub wasm_i16x8_sub
#elif defined(hasSSE2) || defined(USE_MSVC_X86)
#define EDAX_EVAL_VECTOR_AVAILABLE 1
#define EDAX_EVAL_LANES 8
typedef __m128i EdaxEvalVector;
#define edax_eval_add _mm_add_epi16
#define edax_eval_sub _mm_sub_epi16
#endif
#ifdef EDAX_EVAL_VECTOR_AVAILABLE
static inline EdaxEvalVector edax_eval_load(const unsigned short *p){EdaxEvalVector x;memcpy(&x,p,sizeof x);return x;}
static inline void edax_eval_store(unsigned short *p,EdaxEvalVector x){memcpy(p,&x,sizeof x);}
#endif
#endif
#ifdef EDAX_EVAL_VECTOR_AVAILABLE
/* Explicit vector locals, as in the ordinary SIMD kernel. Avoid a feature
 * array indexed inside the flipped-disc loop; all stores follow all loads,
 * so in-place updates retain the same aliasing contract. */
#if EDAX_EVAL_FEATURE_COUNT % EDAX_EVAL_LANES != 0
#error "Evaluation features must fill complete vectors"
#elif EDAX_EVAL_FEATURE_COUNT / EDAX_EVAL_LANES == 3
#define EDAX_EVAL_BLOCKS(op) op(0) op(1) op(2)
#elif EDAX_EVAL_FEATURE_COUNT / EDAX_EVAL_LANES == 4
#define EDAX_EVAL_BLOCKS(op) op(0) op(1) op(2) op(3)
#elif EDAX_EVAL_FEATURE_COUNT / EDAX_EVAL_LANES == 5
#define EDAX_EVAL_BLOCKS(op) op(0) op(1) op(2) op(3) op(4)
#elif EDAX_EVAL_FEATURE_COUNT / EDAX_EVAL_LANES == 10
#define EDAX_EVAL_BLOCKS(op) op(0) op(1) op(2) op(3) op(4) op(5) op(6) op(7) op(8) op(9)
#elif EDAX_EVAL_FEATURE_COUNT / EDAX_EVAL_LANES == 6
#define EDAX_EVAL_BLOCKS(op) op(0) op(1) op(2) op(3) op(4) op(5)
#elif EDAX_EVAL_FEATURE_COUNT / EDAX_EVAL_LANES == 8
#define EDAX_EVAL_BLOCKS(op) op(0) op(1) op(2) op(3) op(4) op(5) op(6) op(7)
#else
#error "Unsupported evaluation vector count"
#endif
#if EDAX_EVAL_PLACE_SIGN > 0
#define EDAX_EVAL_PLACE_OP edax_eval_add
#else
#define EDAX_EVAL_PLACE_OP edax_eval_sub
#endif
#define EDAX_EVAL_DECLARE(i) EdaxEvalVector f##i=edax_eval_load(in+(i)*EDAX_EVAL_LANES);
#define EDAX_EVAL_PLACE(i) do{EdaxEvalVector w=edax_eval_load(row+(i)*EDAX_EVAL_LANES);if(EDAX_EVAL_PLACE_FACTOR==2)w=edax_eval_add(w,w);f##i=EDAX_EVAL_PLACE_OP(f##i,w);}while(0);
#define EDAX_EVAL_ADD(i) f##i=edax_eval_add(f##i,edax_eval_load(row+(i)*EDAX_EVAL_LANES));
#define EDAX_EVAL_SUB(i) f##i=edax_eval_sub(f##i,edax_eval_load(row+(i)*EDAX_EVAL_LANES));
#define EDAX_EVAL_STORE(i) edax_eval_store(out+(i)*EDAX_EVAL_LANES,f##i);
static inline void EDAX_EVAL_UPDATE_NAME(int x,EDAX_EVAL_BITS flipped,unsigned short *out,const unsigned short *in,int odd){
 EDAX_EVAL_BLOCKS(EDAX_EVAL_DECLARE)
 const unsigned short *row=EDAX_EVAL_FEATURE_ROW(x);
 if(odd){
  #define EDAX_EVAL_PLACE_FACTOR EDAX_EVAL_PLACE_ODD
  EDAX_EVAL_BLOCKS(EDAX_EVAL_PLACE)
  #undef EDAX_EVAL_PLACE_FACTOR
  while(flipped){x=EDAX_EVAL_FIRST_BIT(flipped);flipped&=flipped-1;row=EDAX_EVAL_FEATURE_ROW(x);
   EDAX_EVAL_BLOCKS(EDAX_EVAL_ADD)
  }
 }else{
  #define EDAX_EVAL_PLACE_FACTOR EDAX_EVAL_PLACE_EVEN
  EDAX_EVAL_BLOCKS(EDAX_EVAL_PLACE)
  #undef EDAX_EVAL_PLACE_FACTOR
  while(flipped){x=EDAX_EVAL_FIRST_BIT(flipped);flipped&=flipped-1;row=EDAX_EVAL_FEATURE_ROW(x);
   EDAX_EVAL_BLOCKS(EDAX_EVAL_SUB)
  }
 }
 EDAX_EVAL_BLOCKS(EDAX_EVAL_STORE)
}
#undef EDAX_EVAL_BLOCKS
#undef EDAX_EVAL_PLACE_OP
#undef EDAX_EVAL_DECLARE
#undef EDAX_EVAL_PLACE
#undef EDAX_EVAL_ADD
#undef EDAX_EVAL_SUB
#undef EDAX_EVAL_STORE
#endif
#undef EDAX_EVAL_UPDATE_NAME
#undef EDAX_EVAL_BITS
#undef EDAX_EVAL_FEATURE_COUNT
#undef EDAX_EVAL_FEATURE_ROW
#undef EDAX_EVAL_FIRST_BIT
#undef EDAX_EVAL_PLACE_SIGN
#undef EDAX_EVAL_PLACE_ODD
#undef EDAX_EVAL_PLACE_EVEN
