/*
 *  TVTest DTV Video Decoder
 *  Copyright (C) 2015-2016 DBCTRADO
 *
 *  Port of YADIF from MPlayer
 *  Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include "stdafx.h"
#include <memory.h>
#include "Deinterlace_Yadif.h"
#include "Acceleration.h"
#include "PixelFormatConvert.h"
#include "Util.h"
#define BLEND_NO_INLINE
#include "Blend.h"


#define MIN(a,b) ((a) > (b) ? (b) : (a))
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#define MIN3(a,b,c) MIN(MIN(a, b), c)
#define MAX3(a,b,c) MAX(MAX(a, b), c)
//#define ABS(a) ((a) > 0 ? (a) : -(a))
#pragma intrinsic(abs)
#define ABS(a) abs(a)


static void Yadif_Row(
	uint8_t * restrict dst, const uint8_t *prev, const uint8_t *cur, const uint8_t *next,
	int width, int stride, int parity)
{
	const uint8_t *prev2, *next2;

	if (parity) {
		prev2 = prev;
		next2 = cur;
	} else {
		prev2 = cur;
		next2 = next;
	}

	for (int x = 0; x < width; x++) {
		int c = cur[-stride];
		int d = (prev2[0] + next2[0]) >> 1;
		int e = cur[+stride];
		int temporal_diff0 = ABS(prev2[0] - next2[0]);
		int temporal_diff1 = (ABS(prev[-stride] - c) + ABS(prev[+stride] - e)) >> 1;
		int temporal_diff2 = (ABS(next[-stride] - c) + ABS(next[+stride] - e)) >> 1;
		int diff = MAX3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2);
		int spatial_pred = (c + e) >> 1;
		int spatial_score = ABS(cur[-stride - 1] - cur[+stride - 1]) + ABS(c - e)
		                  + ABS(cur[-stride + 1] - cur[+stride + 1]) - 1;

#define CHECK(j) { \
	int score = ABS(cur[-stride - 1 + j] - cur[+stride - 1 - j])	\
	          + ABS(cur[-stride     + j] - cur[+stride     - j])	\
	          + ABS(cur[-stride + 1 + j] - cur[+stride + 1 - j]);	\
	if (score < spatial_score) {									\
		spatial_score = score;										\
		spatial_pred = (cur[-stride + j] + cur[+stride - j]) >> 1;

		CHECK(-1) CHECK(-2) }} }}
		CHECK( 1) CHECK( 2) }} }}
#undef CHECK

		int b = (prev2[-2 * stride] + next2[-2 * stride]) >> 1;
		int f = (prev2[+2 * stride] + next2[+2 * stride]) >> 1;
#if 0
		int a = cur[-3 * stride];
		int g = cur[+3 * stride];
		int max = MAX3(d - e, d - c, MIN3(MAX(b - c, f - e), MAX(b - c, b - a), MAX(f - g, f - e)));
		int min = MIN3(d - e, d - c, MAX3(MIN(b - c, f - e), MIN(b - c, b - a), MIN(f - g, f - e)));
#else
		int max = MAX3(d - e, d - c, MIN(b - c, f - e));
		int min = MIN3(d - e, d - c, MAX(b - c, f - e));
#endif

		diff = MAX3(diff, min, -max);

		if (spatial_pred > d + diff)
			spatial_pred = d + diff;
		else if (spatial_pred < d - diff)
			spatial_pred = d - diff;

		*dst++ = spatial_pred;

		cur++;
		prev++;
		next++;
		prev2++;
		next2++;
	}
}

#ifdef TVTVIDEODEC_SSE2_SUPPORT

#define LOAD8(dst, src) \
	dst = _mm_loadl_epi64((const __m128i*)(src)); \
	dst = _mm_unpacklo_epi8(dst, xmm7);

#define CHECK(pj, mj) \
	xmm2 = _mm_loadu_si128((const __m128i*)(cur - stride + (pj))); \
	xmm3 = _mm_loadu_si128((const __m128i*)(cur + stride + (mj))); \
	xmm4 = xmm2; \
	xmm5 = xmm2; \
	xmm4 = _mm_xor_si128(xmm4, xmm3); \
	xmm5 = _mm_avg_epu8(xmm5, xmm3); \
	xmm4 = _mm_and_si128(xmm4, b1); \
	xmm5 = _mm_subs_epu8(xmm5, xmm4); \
	xmm5 = _mm_srli_si128(xmm5, 1); \
	xmm5 = _mm_unpacklo_epi8(xmm5, xmm7); \
	xmm4 = xmm2; \
	xmm2 = _mm_subs_epu8(xmm2, xmm3); \
	xmm3 = _mm_subs_epu8(xmm3, xmm4); \
	xmm2 = _mm_max_epu8(xmm2, xmm3); \
	xmm3 = xmm2; \
	xmm4 = xmm2; \
	xmm3 = _mm_srli_si128(xmm3, 1); \
	xmm4 = _mm_srli_si128(xmm4, 2); \
	xmm2 = _mm_unpacklo_epi8(xmm2, xmm7); \
	xmm3 = _mm_unpacklo_epi8(xmm3, xmm7); \
	xmm4 = _mm_unpacklo_epi8(xmm4, xmm7); \
	xmm2 = _mm_add_epi16(xmm2, xmm3); \
	xmm2 = _mm_add_epi16(xmm2, xmm4);

#define CHECK1 \
	xmm3 = xmm0; \
	xmm3 = _mm_cmpgt_epi16(xmm3, xmm2); \
	xmm0 = _mm_min_epi16(xmm0, xmm2); \
	xmm6 = xmm3; \
	xmm5 = _mm_and_si128(xmm5, xmm3); \
	xmm3 = _mm_andnot_si128(xmm3, xmm1); \
	xmm3 = _mm_or_si128(xmm3, xmm5); \
	xmm1 = xmm3;

#define CHECK2 \
	xmm6 = _mm_add_epi16(xmm6, w1); \
	xmm6 = _mm_slli_epi16(xmm6, 14); \
	xmm2 = _mm_adds_epi16(xmm2, xmm6); \
	xmm3 = xmm0; \
	xmm3 = _mm_cmpgt_epi16(xmm3, xmm2); \
	xmm0 = _mm_min_epi16(xmm0, xmm2); \
	xmm5 = _mm_and_si128(xmm5, xmm3); \
	xmm3 = _mm_andnot_si128(xmm3, xmm1); \
	xmm3 = _mm_or_si128(xmm3, xmm5); \
	xmm1 = xmm3;

#define FILTER_FUNC(FUNC_NAME, PABS) \
	static void FUNC_NAME( \
		uint8_t * restrict dst, const uint8_t *prev, const uint8_t *cur, const uint8_t *next, \
		int width, int stride, int parity) \
	{ \
		const __m128i w1 = _mm_set1_epi16(1); \
		const __m128i b1 = _mm_set1_epi8(1); \
		const uint8_t *prev2, *next2; \
		\
		if (parity) { \
			prev2 = prev; \
			next2 = cur; \
		} else { \
			prev2 = cur; \
			next2 = next; \
		} \
		\
		for (int x = 0; x < width; x += 8) { \
			__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7; \
			__m128i tmp0, tmp1, tmp2, tmp3; \
			\
			xmm7 = _mm_setzero_si128(); \
			LOAD8(xmm0, cur - stride); \
			LOAD8(xmm1, cur + stride); \
			LOAD8(xmm2, prev2); \
			LOAD8(xmm3, next2); \
			xmm4 = xmm3; \
			xmm3 = _mm_add_epi16(xmm3, xmm2); \
			xmm3 = _mm_srai_epi16(xmm3, 1); \
			tmp0 = xmm0; \
			tmp1 = xmm3; \
			tmp2 = xmm1; \
			xmm2 = _mm_sub_epi16(xmm2, xmm4); \
			PABS(xmm2, xmm4); \
			LOAD8(xmm3, prev - stride); \
			LOAD8(xmm4, prev + stride); \
			xmm3 = _mm_sub_epi16(xmm3, xmm0); \
			xmm4 = _mm_sub_epi16(xmm4, xmm1); \
			PABS(xmm3, xmm5); \
			PABS(xmm4, xmm5); \
			xmm3 = _mm_add_epi16(xmm3, xmm4); \
			xmm2 = _mm_srli_epi16(xmm2, 1); \
			xmm3 = _mm_srli_epi16(xmm3, 1); \
			xmm2 = _mm_max_epi16(xmm2, xmm3); \
			LOAD8(xmm3, next - stride); \
			LOAD8(xmm4, next + stride); \
			xmm3 = _mm_sub_epi16(xmm3, xmm0); \
			xmm4 = _mm_sub_epi16(xmm4, xmm1); \
			PABS(xmm3, xmm5); \
			PABS(xmm4, xmm5); \
			xmm3 = _mm_add_epi16(xmm3, xmm4); \
			xmm3 = _mm_srli_epi16(xmm3, 1); \
			xmm2 = _mm_max_epi16(xmm2, xmm3); \
			tmp3 = xmm2; \
			xmm1 = _mm_add_epi16(xmm1, xmm0); \
			xmm0 = _mm_add_epi16(xmm0, xmm0); \
			xmm0 = _mm_sub_epi16(xmm0, xmm1); \
			xmm1 = _mm_srli_epi16(xmm1, 1); \
			PABS(xmm0, xmm2); \
			xmm2 = _mm_loadu_si128((const __m128i*)(cur - stride - 1)); \
			xmm3 = _mm_loadu_si128((const __m128i*)(cur + stride - 1)); \
			xmm4 = xmm2; \
			xmm2 = _mm_subs_epu8(xmm2, xmm3); \
			xmm3 = _mm_subs_epu8(xmm3, xmm4); \
			xmm2 = _mm_max_epu8(xmm2, xmm3); \
			xmm3 = xmm2; \
			xmm3 = _mm_srli_si128(xmm3, 2); \
			xmm2 = _mm_unpacklo_epi8(xmm2, xmm7); \
			xmm3 = _mm_unpacklo_epi8(xmm3, xmm7); \
			xmm0 = _mm_add_epi16(xmm0, xmm2); \
			xmm0 = _mm_add_epi16(xmm0, xmm3); \
			xmm0 = _mm_sub_epi16(xmm0, w1); \
			CHECK(-2, 0) \
			CHECK1 \
			CHECK(-3, 1) \
			CHECK2 \
			CHECK(0, -2) \
			CHECK1 \
			CHECK(1, -3) \
			CHECK2 \
			xmm6 = tmp3; \
			LOAD8(xmm2, prev2 - 2 * stride); \
			LOAD8(xmm4, next2 - 2 * stride); \
			LOAD8(xmm3, prev2 + 2 * stride); \
			LOAD8(xmm5, next2 + 2 * stride); \
			xmm2 = _mm_add_epi16(xmm2, xmm4); \
			xmm3 = _mm_add_epi16(xmm3, xmm5); \
			xmm2 = _mm_srli_epi16(xmm2, 1); \
			xmm3 = _mm_srli_epi16(xmm3, 1); \
			xmm4 = tmp0; \
			xmm5 = tmp1; \
			xmm7 = tmp2; \
			xmm2 = _mm_sub_epi16(xmm2, xmm4); \
			xmm3 = _mm_sub_epi16(xmm3, xmm7); \
			xmm0 = xmm5; \
			xmm5 = _mm_sub_epi16(xmm5, xmm4); \
			xmm0 = _mm_sub_epi16(xmm0, xmm7); \
			xmm4 = xmm2; \
			xmm2 = _mm_min_epi16(xmm2, xmm3); \
			xmm3 = _mm_max_epi16(xmm3, xmm4); \
			xmm2 = _mm_max_epi16(xmm2, xmm5); \
			xmm3 = _mm_min_epi16(xmm3, xmm5); \
			xmm2 = _mm_max_epi16(xmm2, xmm0); \
			xmm3 = _mm_min_epi16(xmm3, xmm0); \
			xmm4 = _mm_setzero_si128(); \
			xmm6 = _mm_max_epi16(xmm6, xmm3); \
			xmm4 = _mm_sub_epi16(xmm4, xmm2); \
			xmm6 = _mm_max_epi16(xmm6, xmm4); \
			xmm2 = tmp1; \
			xmm3 = xmm2; \
			xmm2 = _mm_sub_epi16(xmm2, xmm6); \
			xmm3 = _mm_add_epi16(xmm3, xmm6); \
			xmm1 = _mm_max_epi16(xmm1, xmm2); \
			xmm1 = _mm_min_epi16(xmm1, xmm3); \
			xmm1 = _mm_packus_epi16(xmm1, xmm1); \
			_mm_storel_epi64((__m128i*)dst, xmm1); \
			dst += 8; \
			prev += 8; \
			cur += 8; \
			next += 8; \
			prev2 += 8; \
			next2 += 8; \
		} \
	}

#define PABS_SSE2(dst, tmp) \
	tmp = _mm_setzero_si128(); \
	tmp = _mm_sub_epi16(tmp, dst); \
	dst = _mm_max_epi16(dst, tmp);

#define PABS_SSSE3(dst, tmp) \
	dst = _mm_abs_epi16(dst);

FILTER_FUNC(Yadif_Row_SSE2, PABS_SSE2)
#ifdef TVTVIDEODEC_SSSE3_SUPPORT
FILTER_FUNC(Yadif_Row_SSSE3, PABS_SSSE3)
#endif

#endif

static void Yadif_Plane(
	uint8_t *dst, int DstStride, const uint8_t *prev, const uint8_t *cur, const uint8_t *next,
	int Stride, int Width, int Height, int Parity, int tff)
{
#ifdef TVTVIDEODEC_SSE2_SUPPORT
	bool fSSE2 = IsSSE2Enabled();
	auto YadifRow =
#ifdef TVTVIDEODEC_SSSE3_SUPPORT
		IsSSSE3Enabled() ? Yadif_Row_SSSE3 :
#endif
		(fSSE2 ? Yadif_Row_SSE2 : Yadif_Row);
	auto BlendRow = fSSE2 ? BlendRow2_SSE2 : BlendRow2;
#else
#define YadifRow Yadif_Row
#define BlendRow BlendRow2
#endif

	int y;

	y = 0;
	if ((y ^ Parity) & 1) {
		memcpy(dst, cur + Stride, Width); // duplicate 1
	} else {
		memcpy(dst, cur, Width);
	}
	y = 1;
	if ((y ^ Parity) & 1) {
		BlendRow(dst + DstStride, cur, cur + Stride * 2, Width); // interpolate 0 and 2
	} else {
		memcpy(dst + DstStride, cur + Stride, Width); // copy original
	}
	for (y = 2; y < Height - 2; y++) {
		int SrcOffset = y * Stride;
		if ((y ^ Parity) & 1) {
			YadifRow(dst + y * DstStride, prev + SrcOffset, cur + SrcOffset, next + SrcOffset, Width, Stride, Parity ^ tff);
		} else {
			memcpy(dst + y * DstStride, cur + SrcOffset, Width); // copy original
		}
	}
	y = Height - 2;
	if ((y ^ Parity) & 1) {
		BlendRow(dst + (Height - 2) * DstStride, cur + (Height - 3) * Stride, cur + (Height - 1) * Stride, Width); // interpolate Height-3 and Height-1
	} else {
		memcpy(dst + (Height - 2) * DstStride, cur + (Height - 2) * Stride, Width); // copy original
	}
	y = Height - 1;
	if ((y ^ Parity) & 1) {
		memcpy(dst + (Height - 1) * DstStride, cur + (Height - 2) * Stride, Width); // duplicate Height-2
	} else {
		memcpy(dst + (Height - 1) * DstStride, cur + (Height - 1) * Stride, Width); // copy original
	}
}

static void CopyFrameBuffer(CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer)
{
	PixelCopyI420ToI420(
		pDstBuffer->m_Width, pDstBuffer->m_Height,
		pDstBuffer->m_Buffer[0], pDstBuffer->m_Buffer[1], pDstBuffer->m_Buffer[2],
		pDstBuffer->m_PitchY, pDstBuffer->m_PitchC,
		pSrcBuffer->m_Buffer[0], pSrcBuffer->m_Buffer[1], pSrcBuffer->m_Buffer[2],
		pSrcBuffer->m_PitchY, pSrcBuffer->m_PitchC);
	pDstBuffer->CopyAttributesFrom(pSrcBuffer);
}


CDeinterlacer_Yadif::CDeinterlacer_Yadif(bool fBob)
	: m_fBob(fBob)
{
	for (int i = 0; i < _countof(m_Frames); i++) {
		m_Frames[i] = nullptr;
	}
}

CDeinterlacer_Yadif::~CDeinterlacer_Yadif()
{
	Finalize();
}

bool CDeinterlacer_Yadif::Initialize()
{
	Finalize();

	return true;
}

void CDeinterlacer_Yadif::Finalize()
{
	for (int i = 0; i < _countof(m_Frames); i++) {
		SafeDelete(m_Frames[i]);
	}
}

CDeinterlacer::FrameStatus CDeinterlacer_Yadif::GetFrame(
	CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
	bool fTopFieldFirst, int Field)
{
	if (m_Frames[0] != nullptr
			&& (pSrcBuffer->m_Width != m_Frames[0]->m_Width
			 || pSrcBuffer->m_Height != m_Frames[0]->m_Height)) {
		Finalize();
	}

	if (!m_Frames[0] || !m_Frames[1] || !m_Frames[2]) {
		CFrameBuffer *pFrame = new CFrameBuffer;

		if (!pFrame->Allocate(pSrcBuffer->m_Width, pSrcBuffer->m_Height)) {
			return FRAME_SKIP;
		}

		CopyFrameBuffer(pFrame, pSrcBuffer);

		for (int i = 0; ; i++) {
			if (!m_Frames[i]) {
				m_Frames[i] = pFrame;
				break;
			}
		}

		if (!m_Frames[2]) {
			return m_Frames[1] ? FRAME_PENDING : FRAME_SKIP;
		}
	} else {
		CFrameBuffer *pFrame = m_Frames[0];
		m_Frames[0] = m_Frames[1];
		m_Frames[1] = m_Frames[2];
		m_Frames[2] = pFrame;
		CopyFrameBuffer(pFrame, pSrcBuffer);
	}

	const bool fTFF = !!(m_Frames[1]->m_Flags & FRAME_FLAG_TOP_FIELD_FIRST);
	const int Parity = m_fBob ? (Field ^ !fTFF) : !fTFF;

	Yadif_Plane(
		pDstBuffer->m_Buffer[0], pDstBuffer->m_PitchY,
		m_Frames[0]->m_Buffer[0], m_Frames[1]->m_Buffer[0], m_Frames[2]->m_Buffer[0],
		m_Frames[0]->m_PitchY, pSrcBuffer->m_Width, pSrcBuffer->m_Height,
		Parity, fTFF);
	for (int i = 1; i < 3; i++) {
		Yadif_Plane(
			pDstBuffer->m_Buffer[i], pDstBuffer->m_PitchC,
			m_Frames[0]->m_Buffer[i], m_Frames[1]->m_Buffer[i], m_Frames[2]->m_Buffer[i],
			m_Frames[0]->m_PitchC, pSrcBuffer->m_Width / 2, pSrcBuffer->m_Height / 2,
			Parity, fTFF);
	}

	pDstBuffer->CopyAttributesFrom(m_Frames[1]);

	return FRAME_OK;
}
