/*
 *  TVTest DTV Video Decoder
 *  Copyright (C) 2015-2018 DBCTRADO
 *
 *  ELA deinterlacing is based on VirtualDub
 *  Copyright (C) 1998-2007 Avery Lee
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
#include "Deinterlace.h"
#include "PixelFormatConvert.h"
#include "Acceleration.h"
#include "MediaTypes.h"
#include "Blend.h"
#include "Copy.h"

#pragma intrinsic(abs)


static void BlendPlane(
	uint8_t * restrict pDst, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrc, ptrdiff_t SrcPitch,
	uint32_t Width, uint32_t Height)
{
	uint8_t * restrict q = pDst;
	const uint8_t * restrict p = pSrc;

#ifdef TVTVIDEODEC_AVX2_SUPPORT
	if (IsAVX2Enabled()
			&& !((uintptr_t)pDst & 31) && !(DstPitch % 32)
			&& !((uintptr_t)pSrc & 31) && !(SrcPitch % 32)) {
		BlendRow2_AVX2(q, p, p + SrcPitch, Width);

		for (uint32_t h = Height - 2; h > 0; h--) {
			q += DstPitch;
			p += SrcPitch;
			BlendRow3_AVX2(q, p - SrcPitch, p, p + SrcPitch, Width);
		}

		BlendRow2_AVX2(q + DstPitch, p, p + SrcPitch, Width);
	} else
#endif
#ifdef TVTVIDEODEC_SSE2_SUPPORT
	if (IsSSE2Enabled()
			&& !((uintptr_t)pDst & 15) && !(DstPitch % 16)
			&& !((uintptr_t)pSrc & 15) && !(SrcPitch % 16)) {
		BlendRow2_SSE2(q, p, p + SrcPitch, Width);

		for (uint32_t h = Height - 2; h > 0; h--) {
			q += DstPitch;
			p += SrcPitch;
			BlendRow3_SSE2(q, p - SrcPitch, p, p + SrcPitch, Width);
		}

		BlendRow2_SSE2(q + DstPitch, p, p + SrcPitch, Width);
	} else
#endif
	{
		BlendRow2(q, p, p + SrcPitch, Width);

		for (uint32_t h = Height - 2; h > 0; h--) {
			q += DstPitch;
			p += SrcPitch;
			BlendRow3(q, p - SrcPitch, p, p + SrcPitch, Width);
		}

		BlendRow2(q + DstPitch, p, p + SrcPitch, Width);
	}
}

static void BobPlane(
	uint8_t * restrict pDst, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrc, ptrdiff_t SrcPitch,
	uint32_t Width, uint32_t Height, bool fTopFiled)
{
	const uint32_t Field = fTopFiled ? 0 : 1;
	uint8_t * restrict q = pDst;
	const uint8_t * restrict p = pSrc;
	uint32_t y = 0;

#ifdef TVTVIDEODEC_AVX2_SUPPORT
	if (IsAVX2Enabled()
			&& !((uintptr_t)pDst & 31) && !(DstPitch % 32)
			&& !((uintptr_t)pSrc & 31) && !(SrcPitch % 32)) {
		if (!fTopFiled) {
			p += SrcPitch;
			Copy_AVX2(q, p, Width);
			q += DstPitch;
			y++;
		}

		for (; y < Height - 1; y++) {
			if ((y & 1) == Field) {
				Copy_AVX2(q, p, Width);
			} else {
				BlendRow2_AVX2(q, p - SrcPitch, p + SrcPitch, Width);
			}
			q += DstPitch;
			p += SrcPitch;
		}

		if ((y & 1) != Field)
			p -= SrcPitch;
		Copy_AVX2(q, p, Width);
	} else
#endif
#ifdef TVTVIDEODEC_SSE2_SUPPORT
	if (IsSSE2Enabled()
			&& !((uintptr_t)pDst & 15) && !(DstPitch % 16)
			&& !((uintptr_t)pSrc & 15) && !(SrcPitch % 16)) {
		if (!fTopFiled) {
			p += SrcPitch;
			Copy_SSE2(q, p, Width);
			q += DstPitch;
			y++;
		}

		for (; y < Height - 1; y++) {
			if ((y & 1) == Field) {
				Copy_SSE2(q, p, Width);
			} else {
				BlendRow2_SSE2(q, p - SrcPitch, p + SrcPitch, Width);
			}
			q += DstPitch;
			p += SrcPitch;
		}

		if ((y & 1) != Field)
			p -= SrcPitch;
		Copy_SSE2(q, p, Width);
	} else
#endif
	{
		if (!fTopFiled) {
			p += SrcPitch;
			Copy_C(q, p, Width);
			q += DstPitch;
			y++;
		}

		for (; y < Height - 1; y++) {
			if ((y & 1) == Field) {
				Copy_C(q, p, Width);
			} else {
				BlendRow2(q, p - SrcPitch, p + SrcPitch, Width);
			}
			q += DstPitch;
			p += SrcPitch;
		}

		if ((y & 1) != Field)
			p -= SrcPitch;
		Copy_C(q, p, Width);
	}
}


static void ELA_Score(
	uint8_t * restrict pDst, const uint8_t * restrict pSrcT, const uint8_t * restrict pSrcB, int Width)
{
	pSrcT += 16;
	pSrcB += 16;
	do {
		int tl2 = pSrcT[-3];
		int tl1 = pSrcT[-2];
		int tc0 = pSrcT[-1];
		int tr1 = pSrcT[ 0];
		int tr2 = pSrcT[ 1];
		int tr3 = pSrcT[ 2];

		int bl2 = pSrcB[-3];
		int bl1 = pSrcB[-2];
		int bc0 = pSrcB[-1];
		int br1 = pSrcB[ 0];
		int br2 = pSrcB[ 1];
		int br3 = pSrcB[ 2];

		pDst[0] = (uint8_t)abs(tc0 - bc0);
		pDst[1] = (uint8_t)abs(tl1 - br1);
		pDst[2] = (uint8_t)abs(tl2 - br2);
		pDst[3] = (uint8_t)abs(tr1 - bl1);
		pDst[4] = (uint8_t)abs(tr2 - bl2);
		pDst[5] = (uint8_t)((tr1 + br1 + 1) >> 1);
		pDst[6] = (uint8_t)((tc0 + br2 + 1) >> 1);
		pDst[7] = (uint8_t)((tl1 + br3 + 1) >> 1);
		pDst[8] = (uint8_t)((tr2 + bc0 + 1) >> 1);
		pDst[9] = (uint8_t)((tr3 + bl1 + 1) >> 1);

		pDst += 10;
		pSrcT++;
		pSrcB++;
	} while (--Width);
}

static void ELA_Result(uint8_t * restrict pDst, const uint8_t * restrict pElaBuf, int Width)
{
	do {
		int scorec0 = pElaBuf[10] * 2 + (pElaBuf[0] + pElaBuf[20]);
		int result = pElaBuf[5];

		int scorel1 = pElaBuf[11] * 2 + (pElaBuf[1] + pElaBuf[21]);
		if (scorel1 < scorec0) {
			result = pElaBuf[6];
			scorec0 = scorel1;

			int scorel2 = pElaBuf[12] * 2 + (pElaBuf[2] + pElaBuf[22]);
			if (scorel2 < scorec0) {
				result = pElaBuf[7];
				scorec0 = scorel2;
			}
		}

		int scorer1 = pElaBuf[13] * 2 + (pElaBuf[3] + pElaBuf[23]);
		if (scorer1 < scorec0) {
			result = pElaBuf[8];
			scorec0 = scorer1;

			int scorer2 = pElaBuf[14] * 2 + (pElaBuf[4] + pElaBuf[24]);
			if (scorer2 < scorec0)
				result = pElaBuf[9];
		}

		*pDst++ = (uint8_t)result;
		pElaBuf += 10;
	} while (--Width);
}

static void ELARow(
	uint8_t * restrict pDst, const uint8_t * restrict pSrcT, const uint8_t * restrict pSrcB,
	uint32_t Width, uint8_t * restrict pTempBuf)
{
	uint32_t w16 = (Width + 15) >> 4;
	uint32_t wr = w16 << 4;

	uint8_t * restrict pElaBuf = pTempBuf;
	uint8_t * restrict pTopBuf = pElaBuf + 10 * wr;
	uint8_t * restrict pBottomBuf = pTopBuf + wr + 32;

	pTopBuf[13] = pTopBuf[14] = pTopBuf[15] = pSrcT[0];
	pBottomBuf[13] = pBottomBuf[14] = pBottomBuf[15] = pSrcB[0];

	for (uint32_t x = 0; x < wr; ++x) {
		pTopBuf[x+16] = pSrcT[x];
		pBottomBuf[x+16] = pSrcB[x];
	}

	uint32_t woffset = Width & 15;
	if (woffset) {
		uint8_t * restrict topfinal = &pTopBuf[Width + 16];
		uint8_t * restrict botfinal = &pBottomBuf[Width + 16];
		const uint8_t tv = topfinal[-1];
		const uint8_t bv = botfinal[-1];

		for (uint32_t i = woffset; i < 16; ++i) {
			*topfinal++ = tv;
			*botfinal++ = bv;
		}
	}

	pTopBuf[wr + 16] = pTopBuf[wr + 17] = pTopBuf[wr + 18] = pTopBuf[wr + 15];
	pTopBuf[wr + 16] = pTopBuf[wr + 17] = pBottomBuf[wr + 18] = pBottomBuf[wr + 15];

	ELA_Score(pElaBuf, pTopBuf, pBottomBuf, Width);
	ELA_Result(pDst, pElaBuf, Width);
}

#ifdef TVTVIDEODEC_SSE2_SUPPORT

static void ELA_Score_SSE2(
	__m128i * restrict pDst, const __m128i * restrict pSrcT, const __m128i * restrict pSrcB, int w16)
{
	do {
		__m128i src0, src1, src2;

		src0 = _mm_load_si128(pSrcT + 0);
		src1 = _mm_load_si128(pSrcT + 1);
		src2 = _mm_load_si128(pSrcT + 2);

		__m128i topl2 = _mm_or_si128(_mm_srli_si128(src0, 16 - 3), _mm_slli_si128(src1, 3));
		__m128i topl1 = _mm_or_si128(_mm_srli_si128(src0, 16 - 2), _mm_slli_si128(src1, 2));
		__m128i topc0 = _mm_or_si128(_mm_srli_si128(src0, 16 - 1), _mm_slli_si128(src1, 1));
		__m128i topr1 = src1;
		__m128i topr2 = _mm_or_si128(_mm_srli_si128(src1, 1), _mm_slli_si128(src2, 16 - 1));
		__m128i topr3 = _mm_or_si128(_mm_srli_si128(src1, 2), _mm_slli_si128(src2, 16 - 2));

		src0 = _mm_load_si128(pSrcB + 0);
		src1 = _mm_load_si128(pSrcB + 1);
		src2 = _mm_load_si128(pSrcB + 2);

		__m128i botl2 = _mm_or_si128(_mm_srli_si128(src0, 16 - 3), _mm_slli_si128(src1, 3));
		__m128i botl1 = _mm_or_si128(_mm_srli_si128(src0, 16 - 2), _mm_slli_si128(src1, 2));
		__m128i botc0 = _mm_or_si128(_mm_srli_si128(src0, 16 - 1), _mm_slli_si128(src1, 1));
		__m128i botr1 = src1;
		__m128i botr2 = _mm_or_si128(_mm_srli_si128(src1, 1), _mm_slli_si128(src2, 16 - 1));
		__m128i botr3 = _mm_or_si128(_mm_srli_si128(src1, 2), _mm_slli_si128(src2, 16 - 2));

		__m128i scorec0 = _mm_or_si128(_mm_subs_epu8(topc0, botc0), _mm_subs_epu8(botc0, topc0));
		__m128i scorel1 = _mm_or_si128(_mm_subs_epu8(topl1, botr1), _mm_subs_epu8(botr1, topl1));
		__m128i scorel2 = _mm_or_si128(_mm_subs_epu8(topl2, botr2), _mm_subs_epu8(botr2, topl2));
		__m128i scorer1 = _mm_or_si128(_mm_subs_epu8(topr1, botl1), _mm_subs_epu8(botl1, topr1));
		__m128i scorer2 = _mm_or_si128(_mm_subs_epu8(topr2, botl2), _mm_subs_epu8(botl2, topr2));

		_mm_store_si128(pDst + 0, scorec0);
		_mm_store_si128(pDst + 1, scorel1);
		_mm_store_si128(pDst + 2, scorel2);
		_mm_store_si128(pDst + 3, scorer1);
		_mm_store_si128(pDst + 4, scorer2);
		_mm_store_si128(pDst + 5, _mm_avg_epu8(topr1, botr1));
		_mm_store_si128(pDst + 6, _mm_avg_epu8(topc0, botr2));
		_mm_store_si128(pDst + 7, _mm_avg_epu8(topl1, botr3));
		_mm_store_si128(pDst + 8, _mm_avg_epu8(topr2, botc0));
		_mm_store_si128(pDst + 9, _mm_avg_epu8(topr3, botl1));

		pDst += 10;
		pSrcT++;
		pSrcB++;
	} while (--w16);
}

static void ELA_Result_SSE2(__m128i * restrict pDst, const __m128i * restrict pElaBuf, int w16)
{
	const __m128i ff = _mm_set1_epi8((char)0xff);
	const __m128i x80b = _mm_set1_epi8((char)0x80);

	do {
		__m128i x0, x1, x2, y;

		x0 = _mm_load_si128(pElaBuf + 0);
		y = _mm_load_si128(pElaBuf + 10);
		x1 = _mm_or_si128(_mm_srli_si128(x0, 1), _mm_slli_si128(y, 15));
		x2 = _mm_or_si128(_mm_srli_si128(x0, 2), _mm_slli_si128(y, 14));
		__m128i scorec0 = _mm_avg_epu8(_mm_avg_epu8(x0, x2), x1);

		x0 = _mm_load_si128(pElaBuf + 1);
		y = _mm_load_si128(pElaBuf + 11);
		x1 = _mm_or_si128(_mm_srli_si128(x0, 1), _mm_slli_si128(y, 15));
		x2 = _mm_or_si128(_mm_srli_si128(x0, 2), _mm_slli_si128(y, 14));
		__m128i scorel1 = _mm_avg_epu8(_mm_avg_epu8(x0, x2), x1);

		x0 = _mm_load_si128(pElaBuf + 2);
		y = _mm_load_si128(pElaBuf + 12);
		x1 = _mm_or_si128(_mm_srli_si128(x0, 1), _mm_slli_si128(y, 15));
		x2 = _mm_or_si128(_mm_srli_si128(x0, 2), _mm_slli_si128(y, 14));
		__m128i scorel2 = _mm_avg_epu8(_mm_avg_epu8(x0, x2), x1);

		x0 = _mm_load_si128(pElaBuf + 3);
		y = _mm_load_si128(pElaBuf + 13);
		x1 = _mm_or_si128(_mm_srli_si128(x0, 1), _mm_slli_si128(y, 15));
		x2 = _mm_or_si128(_mm_srli_si128(x0, 2), _mm_slli_si128(y, 14));
		__m128i scorer1 = _mm_avg_epu8(_mm_avg_epu8(x0, x2), x1);

		x0 = _mm_load_si128(pElaBuf + 4);
		y = _mm_load_si128(pElaBuf + 14);
		x1 = _mm_or_si128(_mm_srli_si128(x0, 1), _mm_slli_si128(y, 15));
		x2 = _mm_or_si128(_mm_srli_si128(x0, 2), _mm_slli_si128(y, 14));
		__m128i scorer2 = _mm_avg_epu8(_mm_avg_epu8(x0, x2), x1);

		scorec0 = _mm_xor_si128(scorec0, x80b);
		scorel1 = _mm_xor_si128(scorel1, x80b);
		scorel2 = _mm_xor_si128(scorel2, x80b);
		scorer1 = _mm_xor_si128(scorer1, x80b);
		scorer2 = _mm_xor_si128(scorer2, x80b);

		// result = (scorel1 < scorec0) ? (scorel2 < scorel1 ? l2 : l1) : (scorer1 < scorec0) ? (scorer2 < scorer1 ? r2 : r1) : c0

		__m128i cmplt_l1_c0 = _mm_cmplt_epi8(scorel1, scorec0);
		__m128i cmplt_r1_c0 = _mm_cmplt_epi8(scorer1, scorec0);
		__m128i cmplt_l1_r1 = _mm_cmplt_epi8(scorel1, scorer1);

		__m128i is_l1 = _mm_and_si128(cmplt_l1_r1, cmplt_l1_c0);
		__m128i is_r1 = _mm_andnot_si128(cmplt_l1_r1, cmplt_r1_c0);
		__m128i is_c0 = _mm_or_si128(cmplt_l1_c0, cmplt_r1_c0);
		is_c0 = _mm_xor_si128(is_c0, ff);

		__m128i is_l2 = _mm_and_si128(is_l1, _mm_cmplt_epi8(scorel2, scorel1));
		__m128i is_r2 = _mm_and_si128(is_r1, _mm_cmplt_epi8(scorer2, scorer1));

		is_l1 = _mm_andnot_si128(is_l2, is_l1);
		is_r1 = _mm_andnot_si128(is_r2, is_r1);

		__m128i result_c0 = _mm_and_si128(_mm_load_si128(pElaBuf + 5), is_c0);
		__m128i result_l1 = _mm_and_si128(_mm_load_si128(pElaBuf + 6), is_l1);
		__m128i result_l2 = _mm_and_si128(_mm_load_si128(pElaBuf + 7), is_l2);
		__m128i result_r1 = _mm_and_si128(_mm_load_si128(pElaBuf + 8), is_r1);
		__m128i result_r2 = _mm_and_si128(_mm_load_si128(pElaBuf + 9), is_r2);

		result_l1 = _mm_or_si128(result_l1, result_l2);
		result_r1 = _mm_or_si128(result_r1, result_r2);
		__m128i result = _mm_or_si128(result_l1, result_r1);
		result = _mm_or_si128(result, result_c0);

		_mm_store_si128(pDst, result);

		pDst++;
		pElaBuf += 10;
	} while (--w16);
}

#endif // TVTVIDEODEC_SSE2_SUPPORT

#ifdef TVTVIDEODEC_SSSE3_SUPPORT

static void ELA_Score_SSSE3(
	__m128i * restrict pDst, const __m128i * restrict pSrcT, const __m128i * restrict pSrcB, int w16)
{
	do {
		__m128i src0, src1, src2;

		src0 = _mm_load_si128(pSrcT + 0);
		src1 = _mm_load_si128(pSrcT + 1);
		src2 = _mm_load_si128(pSrcT + 2);

		__m128i topl2 = _mm_alignr_epi8(src1, src0, 16 - 3);
		__m128i topl1 = _mm_alignr_epi8(src1, src0, 16 - 2);
		__m128i topc0 = _mm_alignr_epi8(src1, src0, 16 - 1);
		__m128i topr1 = src1;
		__m128i topr2 = _mm_alignr_epi8(src2, src1, 1);
		__m128i topr3 = _mm_alignr_epi8(src2, src1, 2);

		src0 = _mm_load_si128(pSrcB + 0);
		src1 = _mm_load_si128(pSrcB + 1);
		src2 = _mm_load_si128(pSrcB + 2);

		__m128i botl2 = _mm_alignr_epi8(src1, src0, 16 - 3);
		__m128i botl1 = _mm_alignr_epi8(src1, src0, 16 - 2);
		__m128i botc0 = _mm_alignr_epi8(src1, src0, 16 - 1);
		__m128i botr1 = src1;
		__m128i botr2 = _mm_alignr_epi8(src2, src1, 1);
		__m128i botr3 = _mm_alignr_epi8(src2, src1, 2);

		__m128i scorec0 = _mm_or_si128(_mm_subs_epu8(topc0, botc0), _mm_subs_epu8(botc0, topc0));
		__m128i scorel1 = _mm_or_si128(_mm_subs_epu8(topl1, botr1), _mm_subs_epu8(botr1, topl1));
		__m128i scorel2 = _mm_or_si128(_mm_subs_epu8(topl2, botr2), _mm_subs_epu8(botr2, topl2));
		__m128i scorer1 = _mm_or_si128(_mm_subs_epu8(topr1, botl1), _mm_subs_epu8(botl1, topr1));
		__m128i scorer2 = _mm_or_si128(_mm_subs_epu8(topr2, botl2), _mm_subs_epu8(botl2, topr2));

		_mm_store_si128(pDst + 0, scorec0);
		_mm_store_si128(pDst + 1, scorel1);
		_mm_store_si128(pDst + 2, scorel2);
		_mm_store_si128(pDst + 3, scorer1);
		_mm_store_si128(pDst + 4, scorer2);
		_mm_store_si128(pDst + 5, _mm_avg_epu8(topr1, botr1));
		_mm_store_si128(pDst + 6, _mm_avg_epu8(topc0, botr2));
		_mm_store_si128(pDst + 7, _mm_avg_epu8(topl1, botr3));
		_mm_store_si128(pDst + 8, _mm_avg_epu8(topr2, botc0));
		_mm_store_si128(pDst + 9, _mm_avg_epu8(topr3, botl1));

		pDst += 10;
		pSrcT++;
		pSrcB++;
	} while (--w16);
}

static void ELA_Result_SSSE3(__m128i * restrict pDst, const __m128i * restrict pElaBuf, int w16)
{
	const __m128i ff = _mm_set1_epi8((char)0xff);
	const __m128i x80b = _mm_set1_epi8((char)0x80);

	do {
		__m128i x0, x1, x2, y;

		x0 = _mm_load_si128(pElaBuf + 0);
		y = _mm_load_si128(pElaBuf + 10);
		x1 = _mm_alignr_epi8(y, x0, 1);
		x2 = _mm_alignr_epi8(y, x0, 2);
		x0 = _mm_avg_epu8(x0, x2);
		__m128i scorec0 = _mm_avg_epu8(x0, x1);

		x0 = _mm_load_si128(pElaBuf + 1);
		y = _mm_load_si128(pElaBuf + 11);
		x1 = _mm_alignr_epi8(y, x0, 1);
		x2 = _mm_alignr_epi8(y, x0, 2);
		x0 = _mm_avg_epu8(x0, x2);
		__m128i scorel1 = _mm_avg_epu8(x0, x1);

		x0 = _mm_load_si128(pElaBuf + 2);
		y = _mm_load_si128(pElaBuf + 12);
		x1 = _mm_alignr_epi8(y, x0, 1);
		x2 = _mm_alignr_epi8(y, x0, 2);
		x0 = _mm_avg_epu8(x0, x2);
		__m128i scorel2 = _mm_avg_epu8(x0, x1);

		x0 = _mm_load_si128(pElaBuf + 3);
		y = _mm_load_si128(pElaBuf + 13);
		x1 = _mm_alignr_epi8(y, x0, 1);
		x2 = _mm_alignr_epi8(y, x0, 2);
		x0 = _mm_avg_epu8(x0, x2);
		__m128i scorer1 = _mm_avg_epu8(x0, x1);

		x0 = _mm_load_si128(pElaBuf + 4);
		y = _mm_load_si128(pElaBuf + 14);
		x1 = _mm_alignr_epi8(y, x0, 1);
		x2 = _mm_alignr_epi8(y, x0, 2);
		x0 = _mm_avg_epu8(x0, x2);
		__m128i scorer2 = _mm_avg_epu8(x0, x1);

		scorec0 = _mm_xor_si128(scorec0, x80b);
		scorel1 = _mm_xor_si128(scorel1, x80b);
		scorel2 = _mm_xor_si128(scorel2, x80b);
		scorer1 = _mm_xor_si128(scorer1, x80b);
		scorer2 = _mm_xor_si128(scorer2, x80b);

		// result = (scorel1 < scorec0) ? (scorel2 < scorel1 ? l2 : l1) : (scorer1 < scorec0) ? (scorer2 < scorer1 ? r2 : r1) : c0

		__m128i cmplt_l1_c0 = _mm_cmplt_epi8(scorel1, scorec0);
		__m128i cmplt_r1_c0 = _mm_cmplt_epi8(scorer1, scorec0);
		__m128i cmplt_l1_r1 = _mm_cmplt_epi8(scorel1, scorer1);

		__m128i is_l1 = _mm_and_si128(cmplt_l1_r1, cmplt_l1_c0);
		__m128i is_r1 = _mm_andnot_si128(cmplt_l1_r1, cmplt_r1_c0);
		__m128i is_c0 = _mm_or_si128(cmplt_l1_c0, cmplt_r1_c0);
		is_c0 = _mm_xor_si128(is_c0, ff);

		__m128i is_l2 = _mm_and_si128(is_l1, _mm_cmplt_epi8(scorel2, scorel1));
		__m128i is_r2 = _mm_and_si128(is_r1, _mm_cmplt_epi8(scorer2, scorer1));

		is_l1 = _mm_andnot_si128(is_l2, is_l1);
		is_r1 = _mm_andnot_si128(is_r2, is_r1);

		__m128i result_c0 = _mm_and_si128(_mm_load_si128(pElaBuf + 5), is_c0);
		__m128i result_l1 = _mm_and_si128(_mm_load_si128(pElaBuf + 6), is_l1);
		__m128i result_l2 = _mm_and_si128(_mm_load_si128(pElaBuf + 7), is_l2);
		__m128i result_r1 = _mm_and_si128(_mm_load_si128(pElaBuf + 8), is_r1);
		__m128i result_r2 = _mm_and_si128(_mm_load_si128(pElaBuf + 9), is_r2);

		result_l1 = _mm_or_si128(result_l1, result_l2);
		result_r1 = _mm_or_si128(result_r1, result_r2);
		__m128i result = _mm_or_si128(result_l1, result_r1);
		result = _mm_or_si128(result, result_c0);

		_mm_store_si128(pDst, result);

		pDst++;
		pElaBuf += 10;
	} while (--w16);
}

#endif // TVTVIDEODEC_SSSE3_SUPPORT

#if defined(TVTVIDEODEC_SSE2_SUPPORT) || defined(TVTVIDEODEC_SSSE3_SUPPORT)

static void ELARow_SSE2_SSSE3(
	uint8_t * restrict pDst, const uint8_t * restrict pSrcT, const uint8_t * restrict pSrcB,
	uint32_t Width, __m128i * restrict pTempBuf
#ifdef TVTVIDEODEC_SSSE3_SUPPORT
	, bool fSSSE3
#endif
	)
{
	const __m128i * restrict srct = (const __m128i*)pSrcT;
	const __m128i * restrict srcb = (const __m128i*)pSrcB;
	uint32_t w16 = (Width + 15) >> 4;
	__m128i * restrict pElaBuf = pTempBuf;
	__m128i * restrict pTopBuf = pElaBuf + 10 * w16;
	__m128i * restrict pBottomBuf = pTopBuf + w16 + 2;

	pTopBuf[0] = srct[0];
	pBottomBuf[0] = srcb[0];

	for (uint32_t x=0; x<w16; ++x) {
		pTopBuf[x+1] = srct[x];
		pBottomBuf[x+1] = srcb[x];
	}

	uint32_t woffset = Width & 15;
	if (woffset) {
		uint8_t * restrict topfinal = (uint8_t*)&pTopBuf[w16] + woffset;
		uint8_t * restrict botfinal = (uint8_t*)&pBottomBuf[w16] + woffset;
		const uint8_t tv = topfinal[-1];
		const uint8_t bv = botfinal[-1];

		for (uint32_t i = woffset; i < 16; ++i) {
			*topfinal++ = tv;
			*botfinal++ = bv;
		}
	}

	pTopBuf[w16 + 1] = pTopBuf[w16];
	pBottomBuf[w16 + 1] = pBottomBuf[w16];

#ifdef TVTVIDEODEC_SSSE3_SUPPORT
	if (fSSSE3) {
		ELA_Score_SSSE3(pElaBuf, pTopBuf, pBottomBuf, w16);
		ELA_Result_SSSE3((__m128i*)pDst, pElaBuf, w16);
	} else
#endif
	{
		ELA_Score_SSE2(pElaBuf, pTopBuf, pBottomBuf, w16);
		ELA_Result_SSE2((__m128i*)pDst, pElaBuf, w16);
	}
}

#endif

static void ELAPlane(
	uint8_t * restrict pDst, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrc, ptrdiff_t SrcPitch,
	uint32_t Width, uint32_t Height, bool fTopFiled)
{
	static const size_t BUFFER_SIZE = (12 * (1920 / 16) + 4) * 16;
	uint32_t w16 = (Width + 15) >> 4;
	size_t BufferSize = (12 * w16 + 4) * 16;
	__declspec(align(16)) uint8_t Buffer[BUFFER_SIZE];
	uint8_t *pELABuffer = (BufferSize <= BUFFER_SIZE) ? Buffer : (uint8_t*)_aligned_malloc(BufferSize, 16);

	const uint32_t Field = fTopFiled ? 0 : 1;
	uint8_t * restrict q = pDst;
	const uint8_t * restrict p = pSrc;
	uint32_t y = 0;

#ifdef TVTVIDEODEC_SSE2_SUPPORT
	if (IsSSE2Enabled()
			&& !((uintptr_t)pDst & 15) && !(DstPitch % 16)
			&& !((uintptr_t)pSrc & 15) && !(SrcPitch % 16)) {
#ifdef TVTVIDEODEC_SSSE3_SUPPORT
		const bool fSSSE3 = IsSSSE3Enabled();
#endif

		if (!fTopFiled) {
			p += SrcPitch;
			Copy_SSE2(q, p, Width);
			q += DstPitch;
			y++;
		}

		for (; y < Height - 1; y ++) {
			if ((y & 1) == Field) {
				Copy_SSE2(q, p, Width);
			} else {
				ELARow_SSE2_SSSE3(
					q, p - SrcPitch, p + SrcPitch, Width, (__m128i*)pELABuffer
#ifdef TVTVIDEODEC_SSSE3_SUPPORT
					, fSSSE3
#endif
					);
			}
			q += DstPitch;
			p += SrcPitch;
		}

		if ((y & 1) != Field)
			p -= SrcPitch;
		Copy_SSE2(q, p, Width);
	} else
#endif
	{
		if (!fTopFiled) {
			p += SrcPitch;
			Copy_C(q, p, Width);
			q += DstPitch;
			y++;
		}

		for (; y < Height - 1; y ++) {
			if ((y & 1) == Field) {
				Copy_C(q, p, Width);
			} else {
				ELARow(q, p - SrcPitch, p + SrcPitch, Width, pELABuffer);
			}
			q += DstPitch;
			p += SrcPitch;
		}

		if ((y & 1) != Field)
			p -= SrcPitch;
		Copy_C(q, p, Width);
	}

	if (pELABuffer != Buffer)
		_aligned_free(pELABuffer);
}


void DeinterlaceBlend(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch, const uint8_t * restrict pSrc, ptrdiff_t SrcPitch)
{
	BlendPlane(pDst, DstPitch, pSrc, SrcPitch, Width, Height);
}

void DeinterlaceBlendI420(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstU, uint8_t * restrict pDstV,
	ptrdiff_t DstPitchY, ptrdiff_t DstPitchC,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC)
{
	BlendPlane(pDstY, DstPitchY, pSrcY, SrcPitchY, Width, Height);
	BlendPlane(pDstU, DstPitchC, pSrcU, SrcPitchC, Width / 2, Height / 2);
	BlendPlane(pDstV, DstPitchC, pSrcV, SrcPitchC, Width / 2, Height / 2);
}

void DeinterlaceBob(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch, const uint8_t * restrict pSrc, ptrdiff_t SrcPitch,
	bool fTopFiled)
{
	BobPlane(pDst, DstPitch, pSrc, SrcPitch, Width, Height, fTopFiled);
}

void DeinterlaceBobI420(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstU, uint8_t * restrict pDstV,
	ptrdiff_t DstPitchY, ptrdiff_t DstPitchC,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC,
	bool fTopFiled)
{
	BobPlane(pDstY, DstPitchY, pSrcY, SrcPitchY, Width, Height, fTopFiled);
	BobPlane(pDstU, DstPitchC, pSrcU, SrcPitchC, Width / 2, Height / 2, fTopFiled);
	BobPlane(pDstV, DstPitchC, pSrcV, SrcPitchC, Width / 2, Height / 2, fTopFiled);
}

void DeinterlaceELA(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch, const uint8_t * restrict pSrc, ptrdiff_t SrcPitch,
	bool fTopFiled)
{
	ELAPlane(pDst, DstPitch, pSrc, SrcPitch, Width, Height, fTopFiled);
}

void DeinterlaceELAI420(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstU, uint8_t * restrict pDstV,
	ptrdiff_t DstPitchY, ptrdiff_t DstPitchC,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC,
	bool fTopFiled)
{
	ELAPlane(pDstY, DstPitchY, pSrcY, SrcPitchY, Width, Height, fTopFiled);
	ELAPlane(pDstU, DstPitchC, pSrcU, SrcPitchC, Width / 2, Height / 2, fTopFiled);
	ELAPlane(pDstV, DstPitchC, pSrcV, SrcPitchC, Width / 2, Height / 2, fTopFiled);
}


// CDeinterlacer

bool CDeinterlacer::IsFormatSupported(const GUID &SrcSubtype, const GUID &DstSubtype) const
{
	return SrcSubtype == MEDIASUBTYPE_I420 && DstSubtype == MEDIASUBTYPE_I420;
}


// CDeinterlacer_Weave

CDeinterlacer::FrameStatus CDeinterlacer_Weave::GetFrame(
	CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
	bool fTopFiledFirst, int Field)
{
	if (pSrcBuffer->m_Subtype == MEDIASUBTYPE_I420) {
		if (pDstBuffer->m_Subtype == MEDIASUBTYPE_I420) {
			PixelCopyI420ToI420(
				pDstBuffer->m_Width, pDstBuffer->m_Height,
				pDstBuffer->m_Buffer[0], pDstBuffer->m_Buffer[1], pDstBuffer->m_Buffer[2],
				pDstBuffer->m_PitchY, pDstBuffer->m_PitchC,
				pSrcBuffer->m_Buffer[0], pSrcBuffer->m_Buffer[1], pSrcBuffer->m_Buffer[2],
				pSrcBuffer->m_PitchY, pSrcBuffer->m_PitchC);
		} else if (pDstBuffer->m_Subtype == MEDIASUBTYPE_NV12) {
			PixelCopyI420ToNV12(
				pDstBuffer->m_Width, pDstBuffer->m_Height,
				pDstBuffer->m_Buffer[0], pDstBuffer->m_Buffer[1],
				pDstBuffer->m_PitchY,
				pSrcBuffer->m_Buffer[0], pSrcBuffer->m_Buffer[1], pSrcBuffer->m_Buffer[2],
				pSrcBuffer->m_PitchY, pSrcBuffer->m_PitchC);
		}
	} else if (pSrcBuffer->m_Subtype == MEDIASUBTYPE_NV12) {
		if (pDstBuffer->m_Subtype == MEDIASUBTYPE_I420) {
			PixelCopyNV12ToI420(
				pDstBuffer->m_Width, pDstBuffer->m_Height,
				pDstBuffer->m_Buffer[0], pDstBuffer->m_Buffer[1], pDstBuffer->m_Buffer[2],
				pDstBuffer->m_PitchY, pDstBuffer->m_PitchC,
				pSrcBuffer->m_Buffer[0], pSrcBuffer->m_Buffer[1],
				pSrcBuffer->m_PitchY);
		} else if (pDstBuffer->m_Subtype == MEDIASUBTYPE_NV12) {
			PixelCopyNV12ToNV12(
				pDstBuffer->m_Width, pDstBuffer->m_Height,
				pDstBuffer->m_Buffer[0], pDstBuffer->m_Buffer[1],
				pDstBuffer->m_PitchY,
				pSrcBuffer->m_Buffer[0], pSrcBuffer->m_Buffer[1],
				pSrcBuffer->m_PitchY);
		}
	}

	return FRAME_OK;
}

bool CDeinterlacer_Weave::IsFormatSupported(const GUID &SrcSubtype, const GUID &DstSubtype) const
{
	return (SrcSubtype == MEDIASUBTYPE_I420 || SrcSubtype == MEDIASUBTYPE_NV12)
		&& (DstSubtype == MEDIASUBTYPE_I420 || DstSubtype == MEDIASUBTYPE_NV12);
}


// CDeinterlacer_Blend

CDeinterlacer::FrameStatus CDeinterlacer_Blend::GetFrame(
	CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
	bool fTopFiledFirst, int Field)
{
	DeinterlaceBlendI420(
		pDstBuffer->m_Width, pDstBuffer->m_Height,
		pDstBuffer->m_Buffer[0], pDstBuffer->m_Buffer[1], pDstBuffer->m_Buffer[2],
		pDstBuffer->m_PitchY, pDstBuffer->m_PitchC,
		pSrcBuffer->m_Buffer[0], pSrcBuffer->m_Buffer[1], pSrcBuffer->m_Buffer[2],
		pSrcBuffer->m_PitchY, pSrcBuffer->m_PitchC);

	return FRAME_OK;
}


// CDeinterlacer_Bob

CDeinterlacer::FrameStatus CDeinterlacer_Bob::GetFrame(
	CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
	bool fTopFiledFirst, int Field)
{
	DeinterlaceBobI420(
		pDstBuffer->m_Width, pDstBuffer->m_Height,
		pDstBuffer->m_Buffer[0], pDstBuffer->m_Buffer[1], pDstBuffer->m_Buffer[2],
		pDstBuffer->m_PitchY, pDstBuffer->m_PitchC,
		pSrcBuffer->m_Buffer[0], pSrcBuffer->m_Buffer[1], pSrcBuffer->m_Buffer[2],
		pSrcBuffer->m_PitchY, pSrcBuffer->m_PitchC,
		!fTopFiledFirst);

	return FRAME_OK;
}


// CDeinterlacer_ELA

CDeinterlacer::FrameStatus CDeinterlacer_ELA::GetFrame(
	CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
	bool fTopFiledFirst, int Field)
{
	DeinterlaceELAI420(
		pDstBuffer->m_Width, pDstBuffer->m_Height,
		pDstBuffer->m_Buffer[0], pDstBuffer->m_Buffer[1], pDstBuffer->m_Buffer[2],
		pDstBuffer->m_PitchY, pDstBuffer->m_PitchC,
		pSrcBuffer->m_Buffer[0], pSrcBuffer->m_Buffer[1], pSrcBuffer->m_Buffer[2],
		pSrcBuffer->m_PitchY, pSrcBuffer->m_PitchC,
		fTopFiledFirst);

	return FRAME_OK;
}


// CDeinterlacer_FieldShift

CDeinterlacer::FrameStatus CDeinterlacer_FieldShift::GetFrame(
	CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
	bool fTopFiledFirst, int Field)
{
	const uint32_t Width = pDstBuffer->m_Width, Height = pDstBuffer->m_Height;
	const ptrdiff_t DstPitchY = pDstBuffer->m_PitchY, DstPitchC = pDstBuffer->m_PitchC;
	const ptrdiff_t SrcPitchY = pSrcBuffer->m_PitchY, SrcPitchC = pSrcBuffer->m_PitchC;
	uint8_t *pDstY = pDstBuffer->m_Buffer[0], *pDstU = pDstBuffer->m_Buffer[1], *pDstV = pDstBuffer->m_Buffer[2];
	const uint8_t *pSrcY = pSrcBuffer->m_Buffer[0], *pSrcU = pSrcBuffer->m_Buffer[1], *pSrcV = pSrcBuffer->m_Buffer[2];
	ptrdiff_t DstOffsetY, DstOffsetC, SrcOffsetY, SrcOffsetC;

	if (fTopFiledFirst) {
		DstOffsetY = DstOffsetC = 0;
		SrcOffsetY = SrcOffsetC = 0;
	} else {
		DstOffsetY = DstPitchY;
		DstOffsetC = DstPitchC;
		SrcOffsetY = SrcPitchY;
		SrcOffsetC = SrcPitchC;
	}

	PixelCopyRGBToRGB(
		Width, Height / 2,
		pDstY + DstOffsetY, DstPitchY * 2, 8,
		pSrcY + SrcOffsetY, SrcPitchY * 2, 8);
	PixelCopyRGBToRGB(
		Width / 2, Height / 4,
		pDstU + DstOffsetC, DstPitchC * 2, 8,
		pSrcU + SrcOffsetC, SrcPitchC * 2, 8);
	PixelCopyRGBToRGB(
		Width / 2, Height / 4,
		pDstV + DstOffsetC, DstPitchC * 2, 8,
		pSrcV + SrcOffsetC, SrcPitchC * 2, 8);

	return FRAME_OK;
}

bool CDeinterlacer_FieldShift::FramePostProcess(CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer, bool fTopFiledFirst)
{
	const uint32_t Width = pDstBuffer->m_Width, Height = pDstBuffer->m_Height;
	const ptrdiff_t DstPitchY = pDstBuffer->m_PitchY, DstPitchC = pDstBuffer->m_PitchC;
	const ptrdiff_t SrcPitchY = pSrcBuffer->m_PitchY, SrcPitchC = pSrcBuffer->m_PitchC;
	uint8_t *pDstY = pDstBuffer->m_Buffer[0], *pDstU = pDstBuffer->m_Buffer[1], *pDstV = pDstBuffer->m_Buffer[2];
	const uint8_t *pSrcY = pSrcBuffer->m_Buffer[0], *pSrcU = pSrcBuffer->m_Buffer[1], *pSrcV = pSrcBuffer->m_Buffer[2];
	ptrdiff_t DstOffsetY, DstOffsetC, SrcOffsetY, SrcOffsetC;

	if (fTopFiledFirst) {
		DstOffsetY = DstPitchY;
		DstOffsetC = DstPitchC;
		SrcOffsetY = SrcPitchY;
		SrcOffsetC = SrcPitchC;
	} else {
		DstOffsetY = DstOffsetC = 0;
		SrcOffsetY = SrcOffsetC = 0;
	}

	PixelCopyRGBToRGB(
		Width, Height / 2,
		pDstY + DstOffsetY, DstPitchY * 2, 8,
		pSrcY + SrcOffsetY, SrcPitchY * 2, 8);
	PixelCopyRGBToRGB(
		Width / 2, Height / 4,
		pDstU + DstOffsetC, DstPitchC * 2, 8,
		pSrcU + SrcOffsetC, SrcPitchC * 2, 8);
	PixelCopyRGBToRGB(
		Width / 2, Height / 4,
		pDstV + DstOffsetC, DstPitchC * 2, 8,
		pSrcV + SrcOffsetC, SrcPitchC * 2, 8);

	return true;
}
