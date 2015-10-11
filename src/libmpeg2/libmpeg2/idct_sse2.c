/*
 * idct_sse2.c
 * Copyright (C) 2015      DBCTRADO
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 * See http://libmpeg2.sourceforge.net/ for updates.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#if defined(ARCH_X86) || defined(ARCH_X86_64)

#include <inttypes.h>
#include <xmmintrin.h>
#include <emmintrin.h>
#include <immintrin.h>

#include "mpeg2.h"
#include "mpeg2_internal.h"
#include "attributes.h"


/* Based on Intel AP-945 */

#define BITS_INV_ACC  4
#define SHIFT_INV_ROW (16 - BITS_INV_ACC)
#define SHIFT_INV_COL (1 + BITS_INV_ACC)
#define RND_INV_ROW   (1024 * (6 - BITS_INV_ACC))
#define RND_INV_COL   (16 * (BITS_INV_ACC - 3))
#define RND_INV_CORR  (RND_INV_COL - 1)

static ATTR_ALIGN(16) const int16_t M128_round_inv_row[8] = {RND_INV_ROW, 0, RND_INV_ROW, 0, RND_INV_ROW, 0, RND_INV_ROW, 0};
static ATTR_ALIGN(16) const int16_t M128_one_corr[8] = {1, 1, 1, 1, 1, 1, 1, 1};
static ATTR_ALIGN(16) const int16_t M128_round_inv_col[8] = {RND_INV_COL, RND_INV_COL, RND_INV_COL, RND_INV_COL, RND_INV_COL, RND_INV_COL, RND_INV_COL, RND_INV_COL};
static ATTR_ALIGN(16) const int16_t M128_round_inv_corr[8]= {RND_INV_CORR, RND_INV_CORR, RND_INV_CORR, RND_INV_CORR, RND_INV_CORR, RND_INV_CORR, RND_INV_CORR, RND_INV_CORR};
static ATTR_ALIGN(16) const int16_t M128_tg_1_16[8] = {13036, 13036, 13036, 13036, 13036, 13036, 13036, 13036}; /* tg * (2<<16) + 0.5 */
static ATTR_ALIGN(16) const int16_t M128_tg_2_16[8] = {27146, 27146, 27146, 27146, 27146, 27146, 27146, 27146}; /* tg * (2<<16) + 0.5 */
static ATTR_ALIGN(16) const int16_t M128_tg_3_16[8] = {-21746, -21746, -21746, -21746, -21746, -21746, -21746, -21746}; /* tg * (2<<16) + 0.5 */
static ATTR_ALIGN(16) const int16_t M128_cos_4_16[8] = {-19195, -19195, -19195, -19195, -19195, -19195, -19195, -19195}; /* cos * (2<<16) + 0.5 */

static ATTR_ALIGN(16) const int16_t M128_tab_i_04[] = {
    16384, 21407,  16384,   8867,  16384,  -8867, 16384, -21407,
    16384,  8867, -16384, -21407, -16384,  21407, 16384,  -8867,
    22725, 19266,  19266,  -4520,  12873, -22725,  4520, -12873,
    12873,  4520, -22725, -12873,   4520,  19266, 19266, -22725};
static ATTR_ALIGN(16) const int16_t M128_tab_i_17[] = {
    22725, 29692,  22725,  12299,  22725, -12299, 22725, -29692,
    22725, 12299, -22725, -29692, -22725,  29692, 22725, -12299,
    31521, 26722,  26722,  -6270,  17855, -31521,  6270, -17855,
    17855,  6270, -31521, -17855,   6270,  26722, 26722, -31521};
static ATTR_ALIGN(16) const int16_t M128_tab_i_26[] = {
    21407, 27969,  21407,  11585,  21407, -11585, 21407, -27969,
    21407, 11585, -21407, -27969, -21407,  27969, 21407, -11585,
    29692, 25172,  25172,  -5906,  16819, -29692,  5906, -16819,
    16819,  5906, -29692, -16819,   5906,  25172, 25172, -29692};
static ATTR_ALIGN(16) const int16_t M128_tab_i_35[] = {
    19266, 25172,  19266,  10426,  19266, -10426, 19266, -25172,
    19266, 10426, -19266, -25172, -19266,  25172, 19266, -10426,
    26722, 22654,  22654,  -5315,  15137, -26722,  5315, -15137,
    15137,  5315, -26722, -15137,   5315,  22654, 22654, -26722};


#define mm_storel_epi64(a, b) \
    _mm_storel_epi64((__m128i*)(a), b)
    //_mm_storel_pi((__m64*)(a), _mm_castsi128_ps(b))
#define mm_storeh_epi64(a, b) \
    _mm_storel_epi64((__m128i*)(a), _mm_srli_si128(b, 8))
    //_mm_storeh_pi((__m64*)(a), _mm_castsi128_ps(b))

#ifdef ARCH_X86_64
#define IDCT_SSE2_SRC(src)						\
    __m128i xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15;	\
    xmm8  = src[0];							\
    xmm9  = src[1];							\
    xmm10 = src[2];							\
    xmm11 = src[3];							\
    xmm12 = src[4];							\
    xmm13 = src[5];							\
    xmm14 = src[6];							\
    xmm15 = src[7];
#define src0 xmm8
#define src1 xmm9
#define src2 xmm10
#define src3 xmm11
#define src4 xmm12
#define src5 xmm13
#define src6 xmm14
#define src7 xmm15
#else
#define IDCT_SSE2_SRC(src)
#define src0 src[0]
#define src1 src[1]
#define src2 src[2]
#define src3 src[3]
#define src4 src[4]
#define src5 src[5]
#define src6 src[6]
#define src7 src[7]
#endif

#define DCT_8_INV_ROW							\
    xmm0 = _mm_shufflelo_epi16 (xmm0, 0xd8);				\
    xmm1 = _mm_shuffle_epi32 (xmm0, 0);					\
    xmm1 = _mm_madd_epi16 (xmm1, *(__m128i*)esi);			\
    xmm3 = _mm_shuffle_epi32 (xmm0, 0x55);				\
    xmm0 = _mm_shufflehi_epi16 (xmm0, 0xd8);				\
    xmm3 = _mm_madd_epi16 (xmm3, *(__m128i*)(esi + 32));		\
    xmm2 = _mm_shuffle_epi32 (xmm0, 0xaa);				\
    xmm0 = _mm_shuffle_epi32 (xmm0, 0xff);				\
    xmm2 = _mm_madd_epi16 (xmm2, *(__m128i*)(esi + 16));		\
    xmm4 = _mm_shufflehi_epi16 (xmm4, 0xd8);				\
    xmm1 = _mm_add_epi32 (xmm1, *(__m128i*)M128_round_inv_row);		\
    xmm4 = _mm_shufflelo_epi16 (xmm4, 0xd8);				\
    xmm0 = _mm_madd_epi16 (xmm0, *(__m128i*)(esi + 48));		\
    xmm5 = _mm_shuffle_epi32 (xmm4, 0);					\
    xmm6 = _mm_shuffle_epi32 (xmm4, 0xaa);				\
    xmm5 = _mm_madd_epi16 (xmm5, *(__m128i*)ecx);			\
    xmm1 = _mm_add_epi32 (xmm1, xmm2);					\
    xmm2 = xmm1;							\
    xmm7 = _mm_shuffle_epi32 (xmm4, 0x55);				\
    xmm6 = _mm_madd_epi16 (xmm6, *(__m128i*)(ecx + 16));		\
    xmm0 = _mm_add_epi32 (xmm0, xmm3);					\
    xmm4 = _mm_shuffle_epi32 (xmm4, 0xff);				\
    xmm2 = _mm_sub_epi32 (xmm2, xmm0);					\
    xmm7 = _mm_madd_epi16 (xmm7, *(__m128i*)(ecx + 32));		\
    xmm0 = _mm_add_epi32 (xmm0, xmm1);					\
    xmm2 = _mm_srai_epi32 (xmm2, 12);					\
    xmm5 = _mm_add_epi32 (xmm5, *(__m128i*)M128_round_inv_row);		\
    xmm4 = _mm_madd_epi16 (xmm4, *(__m128i*)(ecx + 48));		\
    xmm5 = _mm_add_epi32 (xmm5, xmm6);					\
    xmm6 = xmm5;							\
    xmm0 = _mm_srai_epi32 (xmm0, 12);					\
    xmm2 = _mm_shuffle_epi32 (xmm2, 0x1b);				\
    xmm0 = _mm_packs_epi32 (xmm0, xmm2);				\
    xmm4 = _mm_add_epi32 (xmm4, xmm7);					\
    xmm6 = _mm_sub_epi32 (xmm6, xmm4);					\
    xmm4 = _mm_add_epi32 (xmm4, xmm5);					\
    xmm6 = _mm_srai_epi32 (xmm6, 12);					\
    xmm4 = _mm_srai_epi32 (xmm4, 12);					\
    xmm6 = _mm_shuffle_epi32 (xmm6, 0x1b);				\
    xmm4 = _mm_packs_epi32 (xmm4, xmm6);

#define DCT_8_INV_COL_8							\
    xmm1 = *(__m128i*)M128_tg_3_16;					\
    xmm2 = xmm0;							\
    xmm3 = src3;							\
    xmm0 = _mm_mulhi_epi16 (xmm0, xmm1);				\
    xmm1 = _mm_mulhi_epi16 (xmm1, xmm3);				\
    xmm5 = *(__m128i*)M128_tg_1_16;					\
    xmm6 = xmm4;							\
    xmm4 = _mm_mulhi_epi16 (xmm4, xmm5);				\
    xmm0 = _mm_adds_epi16 (xmm0, xmm2);					\
    xmm5 = _mm_mulhi_epi16 (xmm5, src1);				\
    xmm1 = _mm_adds_epi16 (xmm1, xmm3);					\
    xmm7 = src6;							\
    xmm0 = _mm_adds_epi16 (xmm0, xmm3);					\
    xmm3 = *(__m128i*)M128_tg_2_16;					\
    xmm2 = _mm_subs_epi16 (xmm2, xmm1);					\
    xmm7 = _mm_mulhi_epi16 (xmm7, xmm3);				\
    xmm1 = xmm0;							\
    xmm3 = _mm_mulhi_epi16 (xmm3, src2);				\
    xmm5 = _mm_subs_epi16 (xmm5, xmm6);					\
    xmm4 = _mm_adds_epi16 (xmm4, src1);					\
    xmm0 = _mm_adds_epi16 (xmm0, xmm4);					\
    xmm0 = _mm_adds_epi16 (xmm0, *(__m128i*)M128_one_corr);		\
    xmm4 = _mm_subs_epi16 (xmm4, xmm1);					\
    xmm6 = xmm5;							\
    xmm5 = _mm_subs_epi16 (xmm5, xmm2);					\
    xmm5 = _mm_adds_epi16 (xmm5, *(__m128i*)M128_one_corr);		\
    xmm6 = _mm_adds_epi16 (xmm6, xmm2);					\
    src7 = xmm0;							\
    xmm1 = xmm4;							\
    xmm0 = *(__m128i*)M128_cos_4_16;					\
    xmm4 = _mm_adds_epi16 (xmm4, xmm5);					\
    xmm2 = *(__m128i*)M128_cos_4_16;					\
    xmm2 = _mm_mulhi_epi16 (xmm2, xmm4);				\
    src3 = xmm6;							\
    xmm1 = _mm_subs_epi16 (xmm1, xmm5);					\
    xmm7 = _mm_adds_epi16 (xmm7, src2);					\
    xmm3 = _mm_subs_epi16 (xmm3, src6);					\
    xmm6 = src0;							\
    xmm0 = _mm_mulhi_epi16 (xmm0, xmm1);				\
    xmm5 = src4;							\
    xmm5 = _mm_adds_epi16 (xmm5, xmm6);					\
    xmm6 = _mm_subs_epi16 (xmm6, src4);					\
    xmm4 = _mm_adds_epi16 (xmm4, xmm2);					\
    xmm4 = _mm_or_si128 (xmm4, *(__m128i*)M128_one_corr);		\
    xmm0 = _mm_adds_epi16 (xmm0, xmm1);					\
    xmm0 = _mm_or_si128 (xmm0, *(__m128i*)M128_one_corr);		\
    xmm2 = xmm5;							\
    xmm5 = _mm_adds_epi16 (xmm5, xmm7);					\
    xmm1 = xmm6;							\
    xmm5 = _mm_adds_epi16 (xmm5, *(__m128i*)M128_round_inv_col);	\
    xmm2 = _mm_subs_epi16 (xmm2, xmm7);					\
    xmm7 = src7;							\
    xmm6 = _mm_adds_epi16 (xmm6, xmm3);					\
    xmm6 = _mm_adds_epi16 (xmm6, *(__m128i*)M128_round_inv_col );	\
    xmm7 = _mm_adds_epi16 (xmm7, xmm5);					\
    xmm7 = _mm_srai_epi16 (xmm7, SHIFT_INV_COL);			\
    xmm1 = _mm_subs_epi16 (xmm1, xmm3);					\
    xmm1 = _mm_adds_epi16 (xmm1, *(__m128i*)M128_round_inv_corr);	\
    xmm3 = xmm6;							\
    xmm2 = _mm_adds_epi16 (xmm2, *(__m128i*)M128_round_inv_corr);	\
    xmm6 = _mm_adds_epi16 (xmm6, xmm4);					\
    src0 = xmm7;							\
    xmm6 = _mm_srai_epi16 (xmm6, SHIFT_INV_COL);			\
    xmm7 = xmm1;							\
    xmm1 = _mm_adds_epi16 (xmm1, xmm0);					\
    src1 = xmm6;							\
    xmm1 = _mm_srai_epi16 (xmm1, SHIFT_INV_COL);			\
    xmm6 = src3;							\
    xmm7 = _mm_subs_epi16 (xmm7, xmm0);					\
    xmm7 = _mm_srai_epi16 (xmm7, SHIFT_INV_COL);			\
    src2 = xmm1;							\
    xmm5 = _mm_subs_epi16 (xmm5, src7);					\
    xmm5 = _mm_srai_epi16 (xmm5, SHIFT_INV_COL);			\
    src7 = xmm5;							\
    xmm3 = _mm_subs_epi16 (xmm3, xmm4);					\
    xmm6 = _mm_adds_epi16 (xmm6, xmm2);					\
    xmm2 = _mm_subs_epi16 (xmm2, src3);					\
    xmm6 = _mm_srai_epi16 (xmm6, SHIFT_INV_COL);			\
    xmm2 = _mm_srai_epi16 (xmm2, SHIFT_INV_COL);			\
    src3 = xmm6;							\
    xmm3 = _mm_srai_epi16 (xmm3, SHIFT_INV_COL);			\
    src4 = xmm2;							\
    src5 = xmm7;							\
    src6 = xmm3;

#define IDCT_SSE2 {							\
    __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;		\
    const uint8_t * esi, *ecx;						\
    xmm0 = src0;							\
    esi = (const uint8_t *) M128_tab_i_04;				\
    xmm4 = src2;							\
    ecx = (const uint8_t *) M128_tab_i_26;				\
    DCT_8_INV_ROW							\
    src0 = xmm0;							\
    src2 = xmm4;							\
    xmm0 = src4;							\
    xmm4 = src6;							\
    DCT_8_INV_ROW							\
    src4 = xmm0;							\
    src6 = xmm4;							\
    xmm0 = src3;							\
    esi = (const uint8_t *) M128_tab_i_35;				\
    xmm4 = src1;							\
    ecx = (const uint8_t *) M128_tab_i_17;				\
    DCT_8_INV_ROW							\
    src3 = xmm0;							\
    src1 = xmm4;							\
    xmm0 = src5;							\
    xmm4 = src7;							\
    DCT_8_INV_ROW							\
    DCT_8_INV_COL_8							\
}

#if defined(_M_IX86) && (defined(_MSC_VER) || defined(__INTEL_COMPILER))

#define DCT_8_INV_ROW_ASM			\
    __asm pshuflw    xmm0, xmm0, 0xD8		\
    __asm pshufd     xmm1, xmm0, 0		\
    __asm pmaddwd    xmm1, [esi]		\
    __asm pshufd     xmm3, xmm0, 0x55		\
    __asm pshufhw    xmm0, xmm0, 0xD8		\
    __asm pmaddwd    xmm3, [esi+32]		\
    __asm pshufd     xmm2, xmm0, 0xAA		\
    __asm pshufd     xmm0, xmm0, 0xFF		\
    __asm pmaddwd    xmm2, [esi+16]		\
    __asm pshufhw    xmm4, xmm4, 0xD8		\
    __asm paddd      xmm1, M128_round_inv_row	\
    __asm pshuflw    xmm4, xmm4, 0xD8		\
    __asm pmaddwd    xmm0, [esi+48]		\
    __asm pshufd     xmm5, xmm4, 0		\
    __asm pshufd     xmm6, xmm4, 0xAA		\
    __asm pmaddwd    xmm5, [ecx]		\
    __asm paddd      xmm1, xmm2			\
    __asm movdqa     xmm2, xmm1			\
    __asm pshufd     xmm7, xmm4, 0x55		\
    __asm pmaddwd    xmm6, [ecx+16]		\
    __asm paddd      xmm0, xmm3			\
    __asm pshufd     xmm4, xmm4, 0xFF		\
    __asm psubd      xmm2, xmm0			\
    __asm pmaddwd    xmm7, [ecx+32]		\
    __asm paddd      xmm0, xmm1			\
    __asm psrad      xmm2, 12			\
    __asm paddd      xmm5, M128_round_inv_row	\
    __asm pmaddwd    xmm4, [ecx+48]		\
    __asm paddd      xmm5, xmm6			\
    __asm movdqa     xmm6, xmm5			\
    __asm psrad      xmm0, 12			\
    __asm pshufd     xmm2, xmm2, 0x1B		\
    __asm packssdw   xmm0, xmm2			\
    __asm paddd      xmm4, xmm7			\
    __asm psubd      xmm6, xmm4			\
    __asm paddd      xmm4, xmm5			\
    __asm psrad      xmm6, 12			\
    __asm psrad      xmm4, 12			\
    __asm pshufd     xmm6, xmm6, 0x1B		\
    __asm packssdw   xmm4, xmm6

#define DCT_8_INV_COL_8_ASM					\
    __asm movdqa     xmm1, XMMWORD PTR M128_tg_3_16		\
    __asm movdqa     xmm2, xmm0					\
    __asm movdqa     xmm3, XMMWORD PTR [edx+3*16]		\
    __asm pmulhw     xmm0, xmm1					\
    __asm pmulhw     xmm1, xmm3					\
    __asm movdqa     xmm5, XMMWORD PTR M128_tg_1_16		\
    __asm movdqa     xmm6, xmm4					\
    __asm pmulhw     xmm4, xmm5					\
    __asm paddsw     xmm0, xmm2					\
    __asm pmulhw     xmm5, [edx+1*16]				\
    __asm paddsw     xmm1, xmm3					\
    __asm movdqa     xmm7, XMMWORD PTR [edx+6*16]		\
    __asm paddsw     xmm0, xmm3					\
    __asm movdqa     xmm3, XMMWORD PTR M128_tg_2_16		\
    __asm psubsw     xmm2, xmm1					\
    __asm pmulhw     xmm7, xmm3					\
    __asm movdqa     xmm1, xmm0					\
    __asm pmulhw     xmm3, [edx+2*16]				\
    __asm psubsw     xmm5, xmm6					\
    __asm paddsw     xmm4, [edx+1*16]				\
    __asm paddsw     xmm0, xmm4					\
    __asm paddsw     xmm0, XMMWORD PTR M128_one_corr		\
    __asm psubsw     xmm4, xmm1					\
    __asm movdqa     xmm6, xmm5					\
    __asm psubsw     xmm5, xmm2					\
    __asm paddsw     xmm5, XMMWORD PTR M128_one_corr		\
    __asm paddsw     xmm6, xmm2					\
    __asm movdqa     [edx+7*16], xmm0				\
    __asm movdqa     xmm1, xmm4					\
    __asm movdqa     xmm0, XMMWORD PTR M128_cos_4_16		\
    __asm paddsw     xmm4, xmm5					\
    __asm movdqa     xmm2, XMMWORD PTR M128_cos_4_16		\
    __asm pmulhw     xmm2, xmm4					\
    __asm movdqa     [edx+3*16], xmm6				\
    __asm psubsw     xmm1, xmm5					\
    __asm paddsw     xmm7, [edx+2*16]				\
    __asm psubsw     xmm3, [edx+6*16]				\
    __asm movdqa     xmm6, [edx]				\
    __asm pmulhw     xmm0, xmm1					\
    __asm movdqa     xmm5, [edx+4*16]				\
    __asm paddsw     xmm5, xmm6					\
    __asm psubsw     xmm6, [edx+4*16]				\
    __asm paddsw     xmm4, xmm2					\
    __asm por        xmm4, XMMWORD PTR M128_one_corr		\
    __asm paddsw     xmm0, xmm1					\
    __asm por        xmm0, XMMWORD PTR M128_one_corr		\
    __asm movdqa     xmm2, xmm5					\
    __asm paddsw     xmm5, xmm7					\
    __asm movdqa     xmm1, xmm6					\
    __asm paddsw     xmm5, XMMWORD PTR M128_round_inv_col	\
    __asm psubsw     xmm2, xmm7					\
    __asm movdqa     xmm7, [edx+7*16]				\
    __asm paddsw     xmm6, xmm3					\
    __asm paddsw     xmm6, XMMWORD PTR M128_round_inv_col	\
    __asm paddsw     xmm7, xmm5					\
    __asm psraw      xmm7, SHIFT_INV_COL			\
    __asm psubsw     xmm1, xmm3					\
    __asm paddsw     xmm1, XMMWORD PTR M128_round_inv_corr	\
    __asm movdqa     xmm3, xmm6					\
    __asm paddsw     xmm2, XMMWORD PTR M128_round_inv_corr	\
    __asm paddsw     xmm6, xmm4					\
    __asm movdqa     [edx], xmm7				\
    __asm psraw      xmm6, SHIFT_INV_COL			\
    __asm movdqa     xmm7, xmm1					\
    __asm paddsw     xmm1, xmm0					\
    __asm movdqa     [edx+1*16], xmm6				\
    __asm psraw      xmm1, SHIFT_INV_COL			\
    __asm movdqa     xmm6, [edx+3*16]				\
    __asm psubsw     xmm7, xmm0					\
    __asm psraw      xmm7, SHIFT_INV_COL			\
    __asm movdqa     [edx+2*16], xmm1				\
    __asm psubsw     xmm5, [edx+7*16]				\
    __asm psraw      xmm5, SHIFT_INV_COL			\
    __asm movdqa     [edx+7*16], xmm5				\
    __asm psubsw     xmm3, xmm4					\
    __asm paddsw     xmm6, xmm2					\
    __asm psubsw     xmm2, [edx+3*16]				\
    __asm psraw      xmm6, SHIFT_INV_COL			\
    __asm psraw      xmm2, SHIFT_INV_COL			\
    __asm movdqa     [edx+3*16], xmm6				\
    __asm psraw      xmm3, SHIFT_INV_COL			\
    __asm movdqa     [edx+4*16], xmm2				\
    __asm movdqa     [edx+5*16], xmm7				\
    __asm movdqa     [edx+6*16], xmm3

#define IDCT_SSE2_ASM(src) __asm {			\
    __asm mov        edx, src				\
    __asm movdqa     xmm0, XMMWORD PTR[edx]		\
    __asm lea        esi, M128_tab_i_04			\
    __asm movdqa     xmm4, XMMWORD PTR[edx+16*2]	\
    __asm lea        ecx, M128_tab_i_26			\
    DCT_8_INV_ROW_ASM					\
    __asm movdqa     XMMWORD PTR[edx], xmm0		\
    __asm movdqa     XMMWORD PTR[edx+16*2], xmm4	\
    __asm movdqa     xmm0, XMMWORD PTR[edx+16*4]	\
    __asm movdqa     xmm4, XMMWORD PTR[edx+16*6]	\
    DCT_8_INV_ROW_ASM					\
    __asm movdqa     XMMWORD PTR[edx+16*4], xmm0	\
    __asm movdqa     XMMWORD PTR[edx+16*6], xmm4	\
    __asm movdqa     xmm0, XMMWORD PTR[edx+16*3]	\
    __asm lea        esi, M128_tab_i_35			\
    __asm movdqa     xmm4, XMMWORD PTR[edx+16*1]	\
    __asm lea        ecx, M128_tab_i_17			\
    DCT_8_INV_ROW_ASM					\
    __asm movdqa     XMMWORD PTR[edx+16*3], xmm0	\
    __asm movdqa     XMMWORD PTR[edx+16*1], xmm4	\
    __asm movdqa     xmm0, XMMWORD PTR[edx+16*5]	\
    __asm movdqa     xmm4, XMMWORD PTR[edx+16*7]	\
    DCT_8_INV_ROW_ASM					\
    DCT_8_INV_COL_8_ASM					\
}

#endif


void mpeg2_idct_copy_sse2 (int16_t * block, uint8_t * dest, int stride)
{
    __m128i * src = (__m128i *) block;
    __m128i r0, r1, r2, r3;
    __m128i zero;

#ifdef IDCT_SSE2_ASM
    IDCT_SSE2_ASM (src)
#else
    IDCT_SSE2_SRC (src)
    IDCT_SSE2
#endif

    r0 = _mm_packus_epi16 (src0, src1);
    r1 = _mm_packus_epi16 (src2, src3);
    r2 = _mm_packus_epi16 (src4, src5);
    r3 = _mm_packus_epi16 (src6, src7);

    mm_storel_epi64 (dest + 0 * stride, r0);
    mm_storeh_epi64 (dest + 1 * stride, r0);
    mm_storel_epi64 (dest + 2 * stride, r1);
    mm_storeh_epi64 (dest + 3 * stride, r1);
    mm_storel_epi64 (dest + 4 * stride, r2);
    mm_storeh_epi64 (dest + 5 * stride, r2);
    mm_storel_epi64 (dest + 6 * stride, r3);
    mm_storeh_epi64 (dest + 7 * stride, r3);

    zero = _mm_setzero_si128 ();

    _mm_store_si128 (src + 0, zero);
    _mm_store_si128 (src + 1, zero);
    _mm_store_si128 (src + 2, zero);
    _mm_store_si128 (src + 3, zero);
    _mm_store_si128 (src + 4, zero);
    _mm_store_si128 (src + 5, zero);
    _mm_store_si128 (src + 6, zero);
    _mm_store_si128 (src + 7, zero);
}

void mpeg2_idct_add_sse2 (int last, int16_t * block, uint8_t * dest, int stride)
{
    __m128i * src = (__m128i *) block;
    __m128i r0, r1, r2, r3, r4, r5, r6, r7;
    __m128i zero;

#ifdef IDCT_SSE2_ASM
    IDCT_SSE2_ASM (src)
#else
    IDCT_SSE2_SRC (src)
    IDCT_SSE2
#endif

    r0 = _mm_loadl_epi64 ((__m128i*)(dest + 0 * stride));
    r1 = _mm_loadl_epi64 ((__m128i*)(dest + 1 * stride));
    r2 = _mm_loadl_epi64 ((__m128i*)(dest + 2 * stride));
    r3 = _mm_loadl_epi64 ((__m128i*)(dest + 3 * stride));
    r4 = _mm_loadl_epi64 ((__m128i*)(dest + 4 * stride));
    r5 = _mm_loadl_epi64 ((__m128i*)(dest + 5 * stride));
    r6 = _mm_loadl_epi64 ((__m128i*)(dest + 6 * stride));
    r7 = _mm_loadl_epi64 ((__m128i*)(dest + 7 * stride));

    zero = _mm_setzero_si128 ();

    r0 = _mm_unpacklo_epi8 (r0, zero);
    r1 = _mm_unpacklo_epi8 (r1, zero);
    r2 = _mm_unpacklo_epi8 (r2, zero);
    r3 = _mm_unpacklo_epi8 (r3, zero);
    r4 = _mm_unpacklo_epi8 (r4, zero);
    r5 = _mm_unpacklo_epi8 (r5, zero);
    r6 = _mm_unpacklo_epi8 (r6, zero);
    r7 = _mm_unpacklo_epi8 (r7, zero);

    r0 = _mm_adds_epi16 (src0, r0);
    r1 = _mm_adds_epi16 (src1, r1);
    r2 = _mm_adds_epi16 (src2, r2);
    r3 = _mm_adds_epi16 (src3, r3);
    r4 = _mm_adds_epi16 (src4, r4);
    r5 = _mm_adds_epi16 (src5, r5);
    r6 = _mm_adds_epi16 (src6, r6);
    r7 = _mm_adds_epi16 (src7, r7);

    r0 = _mm_packus_epi16 (r0, r1);
    r1 = _mm_packus_epi16 (r2, r3);
    r2 = _mm_packus_epi16 (r4, r5);
    r3 = _mm_packus_epi16 (r6, r7);

    mm_storel_epi64 (dest + 0 * stride, r0);
    mm_storeh_epi64 (dest + 1 * stride, r0);
    mm_storel_epi64 (dest + 2 * stride, r1);
    mm_storeh_epi64 (dest + 3 * stride, r1);
    mm_storel_epi64 (dest + 4 * stride, r2);
    mm_storeh_epi64 (dest + 5 * stride, r2);
    mm_storel_epi64 (dest + 6 * stride, r3);
    mm_storeh_epi64 (dest + 7 * stride, r3);

    _mm_store_si128 (src + 0, zero);
    _mm_store_si128 (src + 1, zero);
    _mm_store_si128 (src + 2, zero);
    _mm_store_si128 (src + 3, zero);
    _mm_store_si128 (src + 4, zero);
    _mm_store_si128 (src + 5, zero);
    _mm_store_si128 (src + 6, zero);
    _mm_store_si128 (src + 7, zero);
}

void mpeg2_idct_sse2_init (void)
{
}

#endif
