/* Weighted population counts in two lanes; same platform policy as bit.c. GPL-3.0-or-later. */
#ifndef EDAX_WEIGHTED_COUNT_H
#define EDAX_WEIGHTED_COUNT_H
#if defined(__ARM_NEON)
typedef uint64x2_t EdaxCountPair;
static inline EdaxCountPair edax_weighted_count2(unsigned long long a,unsigned long long b,unsigned long long corners){
 uint64x2_t x=vcombine_u64(vcreate_u64(a),vcreate_u64(b));
 return vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(vaddq_u8(vcntq_u8(vreinterpretq_u8_u64(x)),vcntq_u8(vreinterpretq_u8_u64(vandq_u64(x,vdupq_n_u64(corners))))))));
}
static inline int edax_count_lane(EdaxCountPair x,int i){return i?(int)vgetq_lane_u64(x,1):(int)vgetq_lane_u64(x,0);}
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
typedef v128_t EdaxCountPair;
static inline EdaxCountPair edax_weighted_count2(unsigned long long a,unsigned long long b,unsigned long long corners){
 v128_t x=wasm_i64x2_make(a,b);x=wasm_i8x16_add(wasm_i8x16_popcnt(x),wasm_i8x16_popcnt(wasm_v128_and(x,wasm_i64x2_splat(corners))));
 x=wasm_i64x2_add(x,wasm_u64x2_shr(x,8));x=wasm_i64x2_add(x,wasm_u64x2_shr(x,16));x=wasm_i64x2_add(x,wasm_u64x2_shr(x,32));return wasm_v128_and(x,wasm_i64x2_splat(255));
}
static inline int edax_count_lane(EdaxCountPair x,int i){return i?(int)wasm_i64x2_extract_lane(x,1):(int)wasm_i64x2_extract_lane(x,0);}
#endif
#endif
