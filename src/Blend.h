/*
 *  TVTest DTV Video Decoder
 *  Copyright (C) 2015-2018 DBCTRADO
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


#ifndef BLEND_NO_INLINE
#define BLEND_INLINE inline
#else
#define BLEND_INLINE
#endif


static BLEND_INLINE void BlendRow2(
	uint8_t * restrict pDst, const uint8_t * restrict pSrc1, const uint8_t * restrict pSrc2, uint32_t Width)
{
	const uint32_t * restrict p1 = (const uint32_t*)pSrc1;
	const uint32_t * restrict p2 = (const uint32_t*)pSrc2;
	uint32_t * restrict q = (uint32_t*)pDst;
	uint32_t w4 = (Width + 3) >> 2;

	do {
		const uint32_t a = *p1++;
		const uint32_t b = *p2++;

		*q++ = (a & b) + (((a ^ b) & 0xfefefefe) >> 1);
	} while (--w4);
}

static BLEND_INLINE void BlendRow3(
	uint8_t * restrict pDst, const uint8_t * restrict pSrc1, const uint8_t * restrict pSrc2, const uint8_t * restrict pSrc3, uint32_t Width)
{
	const uint32_t * restrict p1 = (const uint32_t*)pSrc1;
	const uint32_t * restrict p2 = (const uint32_t*)pSrc2;
	const uint32_t * restrict p3 = (const uint32_t*)pSrc3;
	uint32_t * restrict q = (uint32_t*)pDst;
	uint32_t w4 = (Width + 3) >> 2;

	do {
		const uint32_t a = *p1++;
		const uint32_t b = *p2++;
		const uint32_t c = *p3++;
#if 0
		*q++ = ((a >> 2) & 0x3f3f3f3f) + ((b >> 1) & 0x7f7f7f7f) + ((c >> 2) & 0x3f3f3f3f);
#else
		uint32_t r;
		r = (a & c) + (((a ^ c) & 0xfefefefe) >> 1);
		r = (r & b) + (((r ^ b) & 0xfefefefe) >> 1);
		*q++ = r;
#endif
	} while (--w4);
}

#ifdef TVTVIDEODEC_SSE2_SUPPORT

static BLEND_INLINE void BlendRow2_SSE2(
	uint8_t * restrict pDst, const uint8_t * restrict pSrc1, const uint8_t * restrict pSrc2, uint32_t Width)
{
	const __m128i * restrict p1 = (const __m128i*)pSrc1;
	const __m128i * restrict p2 = (const __m128i*)pSrc2;
	__m128i * restrict q = (__m128i*)pDst;
	uint32_t w16 = (Width + 15) >> 4;
	uint32_t w64 = w16 >> 2;

	if (w64) {
		do {
			__m128i a0, a1, a2, a3, b0, b1, b2, b3;

			a0 = _mm_load_si128(p1 + 0);
			a1 = _mm_load_si128(p1 + 1);
			a2 = _mm_load_si128(p1 + 2);
			a3 = _mm_load_si128(p1 + 3);
			b0 = _mm_load_si128(p2 + 0);
			b1 = _mm_load_si128(p2 + 1);
			b2 = _mm_load_si128(p2 + 2);
			b3 = _mm_load_si128(p2 + 3);
			a0 = _mm_avg_epu8(a0, b0);
			a1 = _mm_avg_epu8(a1, b1);
			a2 = _mm_avg_epu8(a2, b2);
			a3 = _mm_avg_epu8(a3, b3);
			_mm_store_si128(q + 0, a0);
			_mm_store_si128(q + 1, a1);
			_mm_store_si128(q + 2, a2);
			_mm_store_si128(q + 3, a3);
			q += 4;
			p1 += 4;
			p2 += 4;
		} while (--w64);
	}

	w16 &= 3;
	if (w16) {
		do {
			__m128i a, b;

			a = _mm_load_si128(p1);
			b = _mm_load_si128(p2);
			a = _mm_avg_epu8(a, b);
			_mm_store_si128(q, a);
			q++;
			p1++;
			p2++;
		} while (--w16);
	}
}

static BLEND_INLINE void BlendRow3_SSE2(
	uint8_t * restrict pDst, const uint8_t * restrict pSrc1, const uint8_t * restrict pSrc2, const uint8_t * restrict pSrc3, uint32_t Width)
{
	const __m128i * restrict p1 = (const __m128i*)pSrc1;
	const __m128i * restrict p2 = (const __m128i*)pSrc2;
	const __m128i * restrict p3 = (const __m128i*)pSrc3;
	__m128i * restrict q = (__m128i*)pDst;
	uint32_t w16 = (Width + 15) >> 4;

#if defined(_M_AMD64)

	uint32_t w64 = w16 >> 2;

	if (w64) {
		do {
			__m128i a0, a1, a2, a3, b0, b1, b2, b3, c0, c1, c2, c3;

			a0 = _mm_load_si128(p1 + 0);
			a1 = _mm_load_si128(p1 + 1);
			a2 = _mm_load_si128(p1 + 2);
			a3 = _mm_load_si128(p1 + 3);
			b0 = _mm_load_si128(p2 + 0);
			b1 = _mm_load_si128(p2 + 1);
			b2 = _mm_load_si128(p2 + 2);
			b3 = _mm_load_si128(p2 + 3);
			c0 = _mm_load_si128(p3 + 0);
			c1 = _mm_load_si128(p3 + 1);
			c2 = _mm_load_si128(p3 + 2);
			c3 = _mm_load_si128(p3 + 3);
			a0 = _mm_avg_epu8(a0, c0);
			a1 = _mm_avg_epu8(a1, c1);
			a2 = _mm_avg_epu8(a2, c2);
			a3 = _mm_avg_epu8(a3, c3);
			a0 = _mm_avg_epu8(a0, b0);
			a1 = _mm_avg_epu8(a1, b1);
			a2 = _mm_avg_epu8(a2, b2);
			a3 = _mm_avg_epu8(a3, b3);
			_mm_store_si128(q + 0, a0);
			_mm_store_si128(q + 1, a1);
			_mm_store_si128(q + 2, a2);
			_mm_store_si128(q + 3, a3);
			q += 4;
			p1 += 4;
			p2 += 4;
			p3 += 4;
		} while (--w64);
	}

	w16 &= 3;
	if (w16) {
		do {
			__m128i a, b, c;

			a = _mm_load_si128(p1);
			b = _mm_load_si128(p2);
			c = _mm_load_si128(p3);
			a = _mm_avg_epu8(a, c);
			a = _mm_avg_epu8(a, b);
			_mm_store_si128(q, a);
			q++;
			p1++;
			p2++;
			p3++;
		} while (--w16);
	}

#else

	uint32_t w32 = w16 >> 1;

	if (w32) {
		do {
			__m128i a0, a1, b0, b1, c0, c1;

			a0 = _mm_load_si128(p1 + 0);
			a1 = _mm_load_si128(p1 + 1);
			b0 = _mm_load_si128(p2 + 0);
			b1 = _mm_load_si128(p2 + 1);
			c0 = _mm_load_si128(p3 + 0);
			c1 = _mm_load_si128(p3 + 1);
			a0 = _mm_avg_epu8(a0, c0);
			a1 = _mm_avg_epu8(a1, c1);
			a0 = _mm_avg_epu8(a0, b0);
			a1 = _mm_avg_epu8(a1, b1);
			_mm_store_si128(q + 0, a0);
			_mm_store_si128(q + 1, a1);
			q += 2;
			p1 += 2;
			p2 += 2;
			p3 += 2;
		} while (--w32);
	}

	if (w16 & 1) {
		__m128i a, b, c;

		a = _mm_load_si128(p1);
		b = _mm_load_si128(p2);
		c = _mm_load_si128(p3);
		a = _mm_avg_epu8(a, c);
		a = _mm_avg_epu8(a, b);
		_mm_store_si128(q, a);
	}

#endif
}

#endif // TVTVIDEODEC_SSE2_SUPPORT

#ifdef TVTVIDEODEC_AVX2_SUPPORT

static BLEND_INLINE void BlendRow2_AVX2(
	uint8_t * restrict pDst, const uint8_t * restrict pSrc1, const uint8_t * restrict pSrc2, uint32_t Width)
{
	const __m256i * restrict p1 = (const __m256i*)pSrc1;
	const __m256i * restrict p2 = (const __m256i*)pSrc2;
	__m256i * restrict q = (__m256i*)pDst;
	uint32_t w32 = (Width + 31) >> 5;
	uint32_t w128 = w32 >> 2;

	if (w128) {
		do {
			__m256i a0, a1, a2, a3, b0, b1, b2, b3;

			a0 = _mm256_load_si256(p1 + 0);
			a1 = _mm256_load_si256(p1 + 1);
			a2 = _mm256_load_si256(p1 + 2);
			a3 = _mm256_load_si256(p1 + 3);
			b0 = _mm256_load_si256(p2 + 0);
			b1 = _mm256_load_si256(p2 + 1);
			b2 = _mm256_load_si256(p2 + 2);
			b3 = _mm256_load_si256(p2 + 3);
			a0 = _mm256_avg_epu8(a0, b0);
			a1 = _mm256_avg_epu8(a1, b1);
			a2 = _mm256_avg_epu8(a2, b2);
			a3 = _mm256_avg_epu8(a3, b3);
			_mm256_store_si256(q + 0, a0);
			_mm256_store_si256(q + 1, a1);
			_mm256_store_si256(q + 2, a2);
			_mm256_store_si256(q + 3, a3);
			q += 4;
			p1 += 4;
			p2 += 4;
		} while (--w128);
	}

	w32 &= 31;
	if (w32) {
		do {
			__m256i a, b;

			a = _mm256_load_si256(p1);
			b = _mm256_load_si256(p2);
			a = _mm256_avg_epu8(a, b);
			_mm256_store_si256(q, a);
			q++;
			p1++;
			p2++;
		} while (--w32);
	}
}

static BLEND_INLINE void BlendRow3_AVX2(
	uint8_t * restrict pDst, const uint8_t * restrict pSrc1, const uint8_t * restrict pSrc2, const uint8_t * restrict pSrc3, uint32_t Width)
{
	const __m256i * restrict p1 = (const __m256i*)pSrc1;
	const __m256i * restrict p2 = (const __m256i*)pSrc2;
	const __m256i * restrict p3 = (const __m256i*)pSrc3;
	__m256i * restrict q = (__m256i*)pDst;
	uint32_t w32 = (Width + 31) >> 5;
	uint32_t w128 = w32 >> 2;

	if (w128) {
		do {
			__m256i a0, a1, a2, a3, b0, b1, b2, b3, c0, c1, c2, c3;

			a0 = _mm256_load_si256(p1 + 0);
			a1 = _mm256_load_si256(p1 + 1);
			a2 = _mm256_load_si256(p1 + 2);
			a3 = _mm256_load_si256(p1 + 3);
			b0 = _mm256_load_si256(p2 + 0);
			b1 = _mm256_load_si256(p2 + 1);
			b2 = _mm256_load_si256(p2 + 2);
			b3 = _mm256_load_si256(p2 + 3);
			c0 = _mm256_load_si256(p3 + 0);
			c1 = _mm256_load_si256(p3 + 1);
			c2 = _mm256_load_si256(p3 + 2);
			c3 = _mm256_load_si256(p3 + 3);
			a0 = _mm256_avg_epu8(a0, c0);
			a1 = _mm256_avg_epu8(a1, c1);
			a2 = _mm256_avg_epu8(a2, c2);
			a3 = _mm256_avg_epu8(a3, c3);
			a0 = _mm256_avg_epu8(a0, b0);
			a1 = _mm256_avg_epu8(a1, b1);
			a2 = _mm256_avg_epu8(a2, b2);
			a3 = _mm256_avg_epu8(a3, b3);
			_mm256_store_si256(q + 0, a0);
			_mm256_store_si256(q + 1, a1);
			_mm256_store_si256(q + 2, a2);
			_mm256_store_si256(q + 3, a3);
			q += 4;
			p1 += 4;
			p2 += 4;
			p3 += 4;
		} while (--w128);
	}

	w32 &= 31;
	if (w32) {
		do {
			__m256i a, b, c;

			a = _mm256_load_si256(p1);
			b = _mm256_load_si256(p2);
			c = _mm256_load_si256(p3);
			a = _mm256_avg_epu8(a, c);
			a = _mm256_avg_epu8(a, b);
			_mm256_store_si256(q, a);
			q++;
			p1++;
			p2++;
			p3++;
		} while (--w32);
	}
}

#endif // TVTVIDEODEC_AVX2_SUPPORT
