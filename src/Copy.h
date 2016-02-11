/*
 *  TVTest DTV Video Decoder
 *  Copyright (C) 2015-2016 DBCTRADO
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once


static inline void Copy_C(void * restrict pDst, const void * restrict pSrc, size_t Size)
{
	memcpy(pDst, pSrc, Size);
}

#ifdef TVTVIDEODEC_SSE2_SUPPORT

#define MM_STORE _mm_store_si128
//#define MM_STORE _mm_stream_si128

static inline void Copy_SSE2(void * restrict pDst, const void * restrict pSrc, size_t Size)
{
	__m128i * restrict q = (__m128i*)pDst;
	const __m128i * restrict p = (const __m128i*)pSrc;
	size_t s16 = (Size + 15) >> 4;
	size_t s64 = s16 >> 2;
	__m128i xmm0, xmm1, xmm2, xmm3;

	if (s64) {
		do {
			xmm0 = _mm_load_si128(p + 0);
			xmm1 = _mm_load_si128(p + 1);
			xmm2 = _mm_load_si128(p + 2);
			xmm3 = _mm_load_si128(p + 3);
			MM_STORE(q + 0, xmm0);
			MM_STORE(q + 1, xmm1);
			MM_STORE(q + 2, xmm2);
			MM_STORE(q + 3, xmm3);
			p += 4;
			q += 4;
		} while (--s64);
	}

	switch (s16 & 3) {
	case 0:
		break;
	case 1:
		xmm0 = _mm_load_si128(p);
		MM_STORE(q, xmm0);
		break;
	case 2:
		xmm0 = _mm_load_si128(p + 0);
		xmm1 = _mm_load_si128(p + 1);
		MM_STORE(q + 0, xmm0);
		MM_STORE(q + 1, xmm1);
		break;
	case 3:
		xmm0 = _mm_load_si128(p + 0);
		xmm1 = _mm_load_si128(p + 1);
		xmm2 = _mm_load_si128(p + 2);
		MM_STORE(q + 0, xmm0);
		MM_STORE(q + 1, xmm1);
		MM_STORE(q + 2, xmm2);
		break;
	default:
		__assume(0);
	}
}

#undef MM_STORE

#endif // TVTVIDEODEC_SSE2_SUPPORT

#ifdef TVTVIDEODEC_AVX2_SUPPORT

#define MM_STORE _mm256_store_si256
//#define MM_STORE _mm256_stream_si256

static inline void Copy_AVX2(void * restrict pDst, const void * restrict pSrc, size_t Size)
{
	__m256i * restrict q = (__m256i*)pDst;
	const __m256i * restrict p = (const __m256i*)pSrc;
	size_t s32 = (Size + 31) >> 5;
	size_t s128 = s32 >> 2;
	__m256i xmm0, xmm1, xmm2, xmm3;

	if (s128) {
		do {
			xmm0 = _mm256_load_si256(p + 0);
			xmm1 = _mm256_load_si256(p + 1);
			xmm2 = _mm256_load_si256(p + 2);
			xmm3 = _mm256_load_si256(p + 3);
			MM_STORE(q + 0, xmm0);
			MM_STORE(q + 1, xmm1);
			MM_STORE(q + 2, xmm2);
			MM_STORE(q + 3, xmm3);
			p += 4;
			q += 4;
		} while (--s128);
	}

	switch (s32 & 3) {
	case 0:
		break;
	case 1:
		xmm0 = _mm256_load_si256(p);
		MM_STORE(q, xmm0);
		break;
	case 2:
		xmm0 = _mm256_load_si256(p + 0);
		xmm1 = _mm256_load_si256(p + 1);
		MM_STORE(q + 0, xmm0);
		MM_STORE(q + 1, xmm1);
		break;
	case 3:
		xmm0 = _mm256_load_si256(p + 0);
		xmm1 = _mm256_load_si256(p + 1);
		xmm2 = _mm256_load_si256(p + 2);
		MM_STORE(q + 0, xmm0);
		MM_STORE(q + 1, xmm1);
		MM_STORE(q + 2, xmm2);
		break;
	default:
		__assume(0);
	}
}

#undef MM_STORE

#endif // TVTVIDEODEC_AVX2_SUPPORT
