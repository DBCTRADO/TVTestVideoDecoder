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

#include "stdafx.h"
#include <malloc.h>
#include "PixelFormatConvert.h"
#include "Acceleration.h"
#include "Copy.h"


static inline void CopyPlane(
	int Width, int Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrc, ptrdiff_t SrcPitch)
{
#ifdef TVTVIDEODEC_SSE2_SUPPORT
	if (IsSSE2Enabled()
			&& !((uintptr_t)pSrc & 15) && !(SrcPitch % 16)
			&& !((uintptr_t)pDst & 15) && !(DstPitch % 16)) {
		if (DstPitch == SrcPitch && DstPitch == (ptrdiff_t)((Width + 15) & ~15)) {
			Copy_SSE2(pDst, pSrc, DstPitch * Height);
		} else {
			for (int y = 0; y < Height; y++) {
				Copy_SSE2(pDst, pSrc, Width);
				pDst += DstPitch;
				pSrc += SrcPitch;
			}
		}
	} else
#endif
	{
		for (int y = 0; y < Height; y++) {
			Copy_C(pDst, pSrc, Width);
			pDst += DstPitch;
			pSrc += SrcPitch;
		}
	}
}

bool PixelCopyI420ToI420(
	int Width, int Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstU, uint8_t * restrict pDstV,
	ptrdiff_t DstPitchY, ptrdiff_t DstPitchC,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC)
{
	if (Width <= 0 || Height <= 0 || (Width & 1) || (Height & 1)
			|| !pDstY || !pDstU || !pDstV || !pSrcY || !pSrcU || !pSrcV)
		return false;

	CopyPlane(Width, Height, pDstY, DstPitchY, pSrcY, SrcPitchY);

	const int WidthC = Width >> 1;
	const int HeightC = Height >> 1;

	CopyPlane(WidthC, HeightC, pDstU, DstPitchC, pSrcU, SrcPitchC);
	CopyPlane(WidthC, HeightC, pDstV, DstPitchC, pSrcV, SrcPitchC);

	return true;
}

bool PixelCopyI420ToNV12(
	int Width, int Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstUV, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC)
{
	if (Width <= 0 || Height <= 0 || (Width & 1) || (Height & 1)
			|| !pDstY || !pDstUV || !pSrcY || !pSrcU || !pSrcV)
		return false;

	CopyPlane(Width, Height, pDstY, DstPitch, pSrcY, SrcPitchY);

	const int WidthC = Width >> 1;
	const int HeightC = Height >> 1;

#ifdef TVTVIDEODEC_SSE2_SUPPORT
	if (IsSSE2Enabled()
			&& !((uintptr_t)pSrcU & 15) && !((uintptr_t)pSrcV & 15) && !(SrcPitchC % 16)
			&& !((uintptr_t)pDstUV & 15) && !(DstPitch % 16)) {
		for (int y = 0; y < HeightC; y++) {
			uint8_t * restrict uv = pDstUV;
			const uint8_t * restrict u = pSrcU, * restrict v = pSrcV;
			int x = WidthC;

#if defined(_M_AMD64)
			for (; x >= 64; x -= 64) {
				__m128i u0, u1, u2, u3, v0, v1, v2, v3;
				__m128i uv0, uv1, uv2, uv3, uv4, uv5, uv6, uv7;

				u0 = _mm_load_si128((const __m128i*)u + 0);
				u1 = _mm_load_si128((const __m128i*)u + 1);
				u2 = _mm_load_si128((const __m128i*)u + 2);
				u3 = _mm_load_si128((const __m128i*)u + 3);
				v0 = _mm_load_si128((const __m128i*)v + 0);
				v1 = _mm_load_si128((const __m128i*)v + 1);
				v2 = _mm_load_si128((const __m128i*)v + 2);
				v3 = _mm_load_si128((const __m128i*)v + 3);
				uv0 = _mm_unpacklo_epi8(u0, v0);
				uv1 = _mm_unpackhi_epi8(u0, v0);
				uv2 = _mm_unpacklo_epi8(u1, v1);
				uv3 = _mm_unpackhi_epi8(u1, v1);
				uv4 = _mm_unpacklo_epi8(u2, v2);
				uv5 = _mm_unpackhi_epi8(u2, v2);
				uv6 = _mm_unpacklo_epi8(u3, v3);
				uv7 = _mm_unpackhi_epi8(u3, v3);
				_mm_store_si128((__m128i*)uv + 0, uv0);
				_mm_store_si128((__m128i*)uv + 1, uv1);
				_mm_store_si128((__m128i*)uv + 2, uv2);
				_mm_store_si128((__m128i*)uv + 3, uv3);
				_mm_store_si128((__m128i*)uv + 4, uv4);
				_mm_store_si128((__m128i*)uv + 5, uv5);
				_mm_store_si128((__m128i*)uv + 6, uv6);
				_mm_store_si128((__m128i*)uv + 7, uv7);
				uv += 128;
				u += 64;
				v += 64;
			}
#endif

			for (; x >= 32; x -= 32) {
				__m128i u0, u1, v0, v1, uv0, uv1, uv2, uv3;

				u0 = _mm_load_si128((const __m128i*)u + 0);
				u1 = _mm_load_si128((const __m128i*)u + 1);
				v0 = _mm_load_si128((const __m128i*)v + 0);
				v1 = _mm_load_si128((const __m128i*)v + 1);
				uv0 = _mm_unpacklo_epi8(u0, v0);
				uv1 = _mm_unpackhi_epi8(u0, v0);
				uv2 = _mm_unpacklo_epi8(u1, v1);
				uv3 = _mm_unpackhi_epi8(u1, v1);
				_mm_store_si128((__m128i*)uv + 0, uv0);
				_mm_store_si128((__m128i*)uv + 1, uv1);
				_mm_store_si128((__m128i*)uv + 2, uv2);
				_mm_store_si128((__m128i*)uv + 3, uv3);
				uv += 64;
				u += 32;
				v += 32;
			}

			for (; x; x--) {
				*uv++ = *u++;
				*uv++ = *v++;
			}

			pDstUV += DstPitch;
			pSrcU += SrcPitchC;
			pSrcV += SrcPitchC;
		}
	} else
#endif // TVTVIDEODEC_SSE2_SUPPORT
	{
		for (int y = 0; y < HeightC; y++) {
			uint8_t * restrict uv = pDstUV;
			const uint8_t * restrict u = pSrcU, * restrict v = pSrcV;

			for (int x = 0; x < WidthC; x++) {
				*uv++ = *u++;
				*uv++ = *v++;
			}

			pDstUV += DstPitch;
			pSrcU += SrcPitchC;
			pSrcV += SrcPitchC;
		}
	}

	return true;
}

bool PixelCopyNV12ToI420(
	int Width, int Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstU, uint8_t * restrict pDstV,
	ptrdiff_t DstPitchY, ptrdiff_t DstPitchC,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcUV, ptrdiff_t SrcPitch)
{
	if (Width <= 0 || Height <= 0 || (Width & 1) || (Height & 1)
			|| !pDstY || !pDstU || !pDstV || !pSrcY || !pSrcUV)
		return false;

	CopyPlane(Width, Height, pDstY, DstPitchY, pSrcY, SrcPitch);

	const int WidthC = Width >> 1;
	const int HeightC = Height >> 1;

#ifdef TVTVIDEODEC_SSE2_SUPPORT
	if (IsSSE2Enabled()
			&& !((uintptr_t)pSrcUV & 15) && !(SrcPitch % 16)
			&& !((uintptr_t)pDstU & 15) && !((uintptr_t)pDstV & 15) && !(DstPitchC % 16)) {
		const __m128i mask = _mm_set1_epi16(0x00ff);

		for (int y = 0; y < HeightC; y++) {
			uint8_t * restrict u = pDstU, * restrict v = pDstV;
			const uint8_t * restrict uv = pSrcUV;
			int x = WidthC;

			for (; x >= 16; x -= 16) {
				__m128i uv0, uv1, u0, u1, v0, v1;

				uv0 = _mm_load_si128((const __m128i*)uv + 0);
				uv1 = _mm_load_si128((const __m128i*)uv + 1);
				u0 = _mm_and_si128(uv0, mask);
				u1 = _mm_and_si128(uv1, mask);
				u0 = _mm_packus_epi16(u0, u1);
				uv0 = _mm_srli_si128(uv0, 1);
				uv1 = _mm_srli_si128(uv1, 1);
				v0 = _mm_and_si128(uv0, mask);
				v1 = _mm_and_si128(uv1, mask);
				v0 = _mm_packus_epi16(v0, v1);
				_mm_store_si128((__m128i*)u, u0);
				_mm_store_si128((__m128i*)v, v0);
				uv += 32;
				u += 16;
				v += 16;
			}

			for (; x; x--) {
				*u++ = *uv++;
				*v++ = *uv++;
			}

			pDstU += DstPitchC;
			pDstV += DstPitchC;
			pSrcUV += SrcPitch;
		}
	} else
#endif
	{
		for (int y = 0; y < HeightC; y++) {
			uint8_t * restrict u = pDstU, * restrict v = pDstV;
			const uint8_t * restrict uv = pSrcUV;

			for (int x = 0; x < WidthC; x++) {
				*u++ = *uv++;
				*v++ = *uv++;
			}

			pDstU += DstPitchC;
			pDstV += DstPitchC;
			pSrcUV += SrcPitch;
		}
	}

	return true;
}

bool PixelCopyNV12ToNV12(
	int Width, int Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstUV, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcUV, ptrdiff_t SrcPitch)
{
	if (Width <= 0 || Height <= 0 || (Width & 1) || (Height & 1)
			|| !pDstY || !pDstUV || !pSrcY || !pSrcUV)
		return false;

	CopyPlane(Width, Height, pDstY, DstPitch, pSrcY, SrcPitch);
	CopyPlane(Width, Height >> 1, pDstUV, DstPitch, pSrcUV, SrcPitch);

	return true;
}

bool PixelCopyRGBToRGB(
	int Width, int Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch, int DstBPP,
	const uint8_t * restrict pSrc, ptrdiff_t SrcPitch, int SrcBPP)
{
	if (Width <= 0 || Height <= 0 || !pDst || !pSrc)
		return false;

	if (DstBPP == SrcBPP) {
		CopyPlane(((Width * DstBPP) + 7) >> 3, Height, pDst, DstPitch, pSrc, SrcPitch);
	} else {
		return false;
	}

	return true;
}

bool UpsampleI420pToI422(
	int Width, int Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrc, ptrdiff_t SrcPitch)
{
	if (Width <= 0 || Height <= 0 || !pDst || !pSrc)
		return false;

	const int HalfWidth = Width >> 1;
	uint8_t * restrict q = pDst;
	const uint8_t * restrict p = pSrc;

#ifdef TVTVIDEODEC_SSE2_SUPPORT
	if (IsSSE2Enabled()
			&& !((uintptr_t)pSrc & 15) && !(SrcPitch % 16)
			&& !((uintptr_t)pDst & 15) && !(DstPitch % 16)) {
		const size_t WidthBytes = (HalfWidth + 15) & ~15;

		Copy_SSE2(q, p, WidthBytes);
		q += DstPitch;

		const __m128i Zero = _mm_setzero_si128();
		const __m128i Two = _mm_set1_epi16(2);

		for (int y = 1; y < Height - 1; y += 2) {
			const uint8_t * restrict p0 = p;
			uint8_t * restrict q0 = q;

#if defined(_M_AMD64)
			for (int x = 0; x < HalfWidth; x += 16) {
				__m128i t0, t1, t2, t3, b0, b1, b2, b3;

				t1 = _mm_load_si128((const __m128i*)p0);
				b1 = _mm_load_si128((const __m128i*)(p0 + SrcPitch));
				t0 = _mm_unpacklo_epi8(t1, Zero);
				t1 = _mm_unpackhi_epi8(t1, Zero);
				b0 = _mm_unpacklo_epi8(b1, Zero);
				b1 = _mm_unpackhi_epi8(b1, Zero);
				t2 = _mm_slli_epi16(t0, 1);
				t3 = _mm_slli_epi16(t1, 1);
				b2 = _mm_slli_epi16(b0, 1);
				b3 = _mm_slli_epi16(b1, 1);
				t2 = _mm_add_epi16(t2, t0);
				t3 = _mm_add_epi16(t3, t1);
				b2 = _mm_add_epi16(b2, b0);
				b3 = _mm_add_epi16(b3, b1);
				t2 = _mm_add_epi16(t2, b0);
				t3 = _mm_add_epi16(t3, b1);
				b2 = _mm_add_epi16(b2, t0);
				b3 = _mm_add_epi16(b3, t1);
				t2 = _mm_add_epi16(t2, Two);
				t3 = _mm_add_epi16(t3, Two);
				b2 = _mm_add_epi16(b2, Two);
				b3 = _mm_add_epi16(b3, Two);
				t2 = _mm_srli_epi16(t2, 2);
				t3 = _mm_srli_epi16(t3, 2);
				b2 = _mm_srli_epi16(b2, 2);
				b3 = _mm_srli_epi16(b3, 2);
				t2 = _mm_packus_epi16(t2, t3);
				b2 = _mm_packus_epi16(b2, b3);
				_mm_store_si128((__m128i*)q0, t2);
				_mm_store_si128((__m128i*)(q0 + DstPitch), b2);

				p0 += 16;
				q0 += 16;
			}
#else
			for (int x = 0; x < HalfWidth; x += 8) {
				__m128i t0, t1, b0, b1;

				t0 = _mm_loadl_epi64((const __m128i*)p0);
				b0 = _mm_loadl_epi64((const __m128i*)(p0 + SrcPitch));
				t0 = _mm_unpacklo_epi8(t0, Zero);
				b0 = _mm_unpacklo_epi8(b0, Zero);
				t1 = _mm_slli_epi16(t0, 1);
				b1 = _mm_slli_epi16(b0, 1);
				t1 = _mm_add_epi16(t1, t0);
				b1 = _mm_add_epi16(b1, b0);
				t1 = _mm_add_epi16(t1, b0);
				b1 = _mm_add_epi16(b1, t0);
				t1 = _mm_add_epi16(t1, Two);
				b1 = _mm_add_epi16(b1, Two);
				t1 = _mm_srli_epi16(t1, 2);
				b1 = _mm_srli_epi16(b1, 2);
				t1 = _mm_packus_epi16(t1, _mm_undefined_si128());
				b1 = _mm_packus_epi16(b1, _mm_undefined_si128());
				_mm_storel_epi64((__m128i*)q0, t1);
				_mm_storel_epi64((__m128i*)(q0 + DstPitch), b1);

				p0 += 8;
				q0 += 8;
			}
#endif

			q += DstPitch * 2;
			p += SrcPitch;
		}

		Copy_SSE2(q, p, WidthBytes);
	} else
#endif // TVTVIDEODEC_SSE2_SUPPORT
	{
		Copy_C(q, p, HalfWidth);
		q += DstPitch;

		for (int y = 1; y < Height - 1; y += 2) {
			for (int x = 0; x < HalfWidth; x++) {
				const uint8_t s0 = p[SrcPitch * 0 + x];
				const uint8_t s1 = p[SrcPitch * 1 + x];
				q[DstPitch * 0 + x] = ((s0 * 3) + (s1 * 1) + 2) >> 2;
				q[DstPitch * 1 + x] = ((s0 * 1) + (s1 * 3) + 2) >> 2;
			}

			q += DstPitch * 2;
			p += SrcPitch;
		}

		Copy_C(q, p, HalfWidth);
	}

	return true;
}

bool UpsampleI420iToI422(
	int Width, int Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrc, ptrdiff_t SrcPitch)
{
	if (Width <= 0 || Height <= 0 || !pDst || !pSrc)
		return false;

	const int HalfWidth = Width >> 1;
	uint8_t * restrict q = pDst;
	const uint8_t * restrict p = pSrc;

#ifdef TVTVIDEODEC_SSE2_SUPPORT
	if (IsSSE2Enabled()
			&& !((uintptr_t)pSrc & 15) && !(SrcPitch % 16)
			&& !((uintptr_t)pDst & 15) && !(DstPitch % 16)) {
		const size_t WidthBytes = (HalfWidth + 15) & ~15;

		Copy_SSE2(q, p, WidthBytes);
		q += DstPitch;
		Copy_SSE2(q, p + SrcPitch, WidthBytes);
		q += DstPitch;

		const __m128i Zero  = _mm_setzero_si128();
		const __m128i Four  = _mm_set1_epi16(4);
		const __m128i Five  = _mm_set1_epi16(5);
		const __m128i Seven = _mm_set1_epi16(7);

		for (int y = 2; y < Height - 2; y += 4) {
			const uint8_t * restrict p0 = p;
			uint8_t * restrict q0 = q;

			for (int x = 0; x < HalfWidth; x += 8) {
				__m128i s0, s1, s2, s3, s4, s5, s6, s7;

				s0 = _mm_loadl_epi64((const __m128i*)(p0 + (SrcPitch * 0)));
				s1 = _mm_loadl_epi64((const __m128i*)(p0 + (SrcPitch * 1)));
				s2 = _mm_loadl_epi64((const __m128i*)(p0 + (SrcPitch * 2)));
				s3 = _mm_loadl_epi64((const __m128i*)(p0 + (SrcPitch * 3)));
				s0 = _mm_unpacklo_epi8(s0, Zero);
				s1 = _mm_unpacklo_epi8(s1, Zero);
				s2 = _mm_unpacklo_epi8(s2, Zero);
				s3 = _mm_unpacklo_epi8(s3, Zero);
				s5 = _mm_mullo_epi16(s0, Five);
				s6 = _mm_mullo_epi16(s3, Five);
				s4 = _mm_slli_epi16(s2, 1);
				s7 = _mm_slli_epi16(s1, 1);
				s5 = _mm_add_epi16(s5, s2);
				s6 = _mm_add_epi16(s6, s1);
				s4 = _mm_add_epi16(s4, s5);
				s7 = _mm_add_epi16(s7, s6);
				s5 = _mm_mullo_epi16(s1, Seven);
				s6 = _mm_mullo_epi16(s2, Seven);
				s5 = _mm_add_epi16(s5, s3);
				s6 = _mm_add_epi16(s6, s0);
				s4 = _mm_add_epi16(s4, Four);
				s5 = _mm_add_epi16(s5, Four);
				s6 = _mm_add_epi16(s6, Four);
				s7 = _mm_add_epi16(s7, Four);
				s4 = _mm_srli_epi16(s4, 3);
				s5 = _mm_srli_epi16(s5, 3);
				s6 = _mm_srli_epi16(s6, 3);
				s7 = _mm_srli_epi16(s7, 3);
				s4 = _mm_packus_epi16(s4, _mm_undefined_si128());
				s5 = _mm_packus_epi16(s5, _mm_undefined_si128());
				s6 = _mm_packus_epi16(s6, _mm_undefined_si128());
				s7 = _mm_packus_epi16(s7, _mm_undefined_si128());
				_mm_storel_epi64((__m128i*)(q0 + (DstPitch * 0)), s4);
				_mm_storel_epi64((__m128i*)(q0 + (DstPitch * 1)), s5);
				_mm_storel_epi64((__m128i*)(q0 + (DstPitch * 2)), s6);
				_mm_storel_epi64((__m128i*)(q0 + (DstPitch * 3)), s7);

				p0 += 8;
				q0 += 8;
			}

			q += DstPitch * 4;
			p += SrcPitch * 2;
		}

		Copy_SSE2(q, p, WidthBytes);
		q += DstPitch;
		p += SrcPitch;
		Copy_SSE2(q, p, WidthBytes);
	} else
#endif // TVTVIDEODEC_SSE2_SUPPORT
	{
		Copy_C(q, p, HalfWidth);
		q += DstPitch;
		Copy_C(q, p + SrcPitch, HalfWidth);
		q += DstPitch;

		for (int y = 2; y < Height - 2; y += 4) {
			for (int x = 0; x < HalfWidth; x++) {
				const uint8_t s0 = p[SrcPitch * 0 + x];
				const uint8_t s1 = p[SrcPitch * 1 + x];
				const uint8_t s2 = p[SrcPitch * 2 + x];
				const uint8_t s3 = p[SrcPitch * 3 + x];
				q[DstPitch * 0 + x] = ((s0 * 5) + (s2 * 3) + 4) >> 3;
				q[DstPitch * 1 + x] = ((s1 * 7) + (s3 * 1) + 4) >> 3;
				q[DstPitch * 2 + x] = ((s0 * 1) + (s2 * 7) + 4) >> 3;
				q[DstPitch * 3 + x] = ((s1 * 3) + (s3 * 5) + 4) >> 3;
			}

			q += DstPitch * 4;
			p += SrcPitch * 2;
		}

		Copy_C(q, p, HalfWidth);
		q += DstPitch;
		p += SrcPitch;
		Copy_C(q, p, HalfWidth);
	}

	return true;
}


static const int YUV_RGB_TABLE_OFFSET = 384;

void SetupYUVToRGBConvertParametersMPEG2(
	YUVToRGBConvertParameters *pParams, uint8_t MatrixCoefficients, bool fStraight)
{
	struct RGBParameters {
		int RV, GU, GV, BU;
	};
	static const RGBParameters RGBTable[8][2] = {
		{{117504, -13954, -34903, 138453}, {103219, -12257, -30659, 121621}},	// no sequence_display_extension
		{{117504, -13954, -34903, 138453}, {103219, -12257, -30659, 121621}},	// ITU-R Rec. 709
		{{104597, -25675, -53279, 132201}, { 91881, -22553, -46801, 116129}},	// unspecified
		{{104597, -25675, -53279, 132201}, { 91881, -22553, -46801, 116129}},	// reserved
		{{104448, -24759, -53109, 132798}, { 91750, -21749, -46652, 116654}},	// FCC
		{{104597, -25675, -53279, 132201}, { 91881, -22553, -46801, 116129}},	// ITU-R Rec. 624-4 System B, G
		{{104597, -25675, -53279, 132201}, { 91881, -22553, -46801, 116129}},	// SMPTE 170M
		{{117579, -16907, -35559, 136230}, {103284, -14851, -31235, 119668}},	// SMPTE 240M
	};

	const RGBParameters &RGBParams = RGBTable[MatrixCoefficients < 8 ? MatrixCoefficients : 0][fStraight ? 1 : 0];

	pParams->YOffset = (fStraight ? 0 : 16);
	pParams->YGain = (fStraight ? 65536 : 76309);
	pParams->RV = RGBParams.RV;
	pParams->GU = RGBParams.GU;
	pParams->GV = RGBParams.GV;
	pParams->BU = RGBParams.BU;
}

void SetupYUVToRGBConvertTable(
	YUVToRGBConvertTable *pTable, const YUVToRGBConvertParameters *pParams)
{
	int i;

	for (i = 0; i < YUV_RGB_TABLE_OFFSET; i++) {
		pTable->Table[i] = 0;
	}
	for (; i < YUV_RGB_TABLE_OFFSET + 256; i++) {
		pTable->Table[i] = i - YUV_RGB_TABLE_OFFSET;
	}
	for (; i < 1024; i++) {
		pTable->Table[i] = 255;
	}
}

bool PixelCopyI422ToRGB(
	int Width, int Height,
	uint8_t * restrict pDst, int DstPlanes, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC,
	const YUVToRGBConvertParameters &Params, const YUVToRGBConvertTable &Table)
{
	if (Width <= 0 || Height <= 0 || DstPlanes < 3 || !pDst || !pSrcY || !pSrcU || !pSrcV)
		return false;

	const int YOffset = Params.YOffset, YGain = Params.YGain;
	const int RV = Params.RV, GU = Params.GU, GV = Params.GV, BU = Params.BU;
	const uint8_t * restrict py = pSrcY;
	const uint8_t * restrict pu = pSrcU;
	const uint8_t * restrict pv = pSrcV;
	uint8_t * restrict pDstRow = pDst;

#ifdef TVTVIDEODEC_SSE2_SUPPORT
	if (((DstPlanes == 4 && IsSSE2Enabled())
#ifdef TVTVIDEODEC_SSSE3_SUPPORT
			|| (DstPlanes == 3 && IsSSSE3Enabled())
#endif
		 	)
			&& !((uintptr_t)pSrcY & 15) && !(SrcPitchY % 16)
			&& !((uintptr_t)pSrcU & 15) && !((uintptr_t)pSrcV & 15) && !(SrcPitchC % 16)
			&& !((uintptr_t)pDst & 15) && !(DstPitch % 16)) {
		const __m128i yg  = _mm_set1_epi16((short)((YGain + 4) / 8));
		const __m128i yo  = _mm_set1_epi16((short)YOffset);
		const __m128i bu  = _mm_unpacklo_epi16(_mm_set1_epi16((short)((BU + 4) / 8)), yg);
		const __m128i gu  = _mm_set1_epi16((short)((GU + 4) / 8));
		const __m128i gv  = _mm_set1_epi16((short)((GV + 4) / 8));
		const __m128i guv = _mm_unpacklo_epi16(gu, gv);
		const __m128i rv  = _mm_unpacklo_epi16(_mm_set1_epi16((short)((RV + 4) / 8)), yg);
		const __m128i Zero = _mm_setzero_si128();
		const __m128i Center = _mm_set1_epi16(128);
#ifdef TVTVIDEODEC_SSSE3_SUPPORT
		const __m128i Shuffle = _mm_set_epi8(-1, -1, -1, -1, 14, 13, 12, 10, 9, 8, 6, 5, 4, 2, 1, 0);
#endif

		for (int y = 0; y < Height; y++) {
			uint8_t * restrict q = pDstRow;

			for (int x = 0; x < Width; x += 8) {
				__m128i y0, u0, v0;

				y0 = _mm_loadl_epi64((const __m128i*)(py + x));
				u0 = _mm_loadl_epi64((const __m128i*)(pu + (x >> 1)));
				v0 = _mm_loadl_epi64((const __m128i*)(pv + (x >> 1)));
				y0 = _mm_unpacklo_epi8(y0, Zero);
				u0 = _mm_unpacklo_epi8(u0, Zero);
				v0 = _mm_unpacklo_epi8(v0, Zero);
				y0 = _mm_sub_epi16(y0, yo);
				u0 = _mm_unpacklo_epi16(u0, u0);
				v0 = _mm_unpacklo_epi16(v0, v0);
				u0 = _mm_sub_epi16(u0, Center);
				v0 = _mm_sub_epi16(v0, Center);

				__m128i r0, r1, g0, g1, b0, b1, y1, y2;

				b0 = _mm_unpacklo_epi16(u0, y0);
				b1 = _mm_unpackhi_epi16(u0, y0);
				b0 = _mm_madd_epi16(b0, bu);
				b1 = _mm_madd_epi16(b1, bu);
				b0 = _mm_srai_epi32(b0, 13);
				b1 = _mm_srai_epi32(b1, 13);
				b0 = _mm_packs_epi32(b0, b1);
				b0 = _mm_packus_epi16(b0, _mm_undefined_si128());

				r0 = _mm_unpacklo_epi16(v0, y0);
				r1 = _mm_unpackhi_epi16(v0, y0);
				r0 = _mm_madd_epi16(r0, rv);
				r1 = _mm_madd_epi16(r1, rv);
				r0 = _mm_srai_epi32(r0, 13);
				r1 = _mm_srai_epi32(r1, 13);
				r0 = _mm_packs_epi32(r0, r1);
				r0 = _mm_packus_epi16(r0, _mm_undefined_si128());

				y1 = _mm_mullo_epi16(y0, yg);
				y2 = _mm_mulhi_epi16(y0, yg);
				g0 = _mm_unpacklo_epi16(u0, v0);
				g1 = _mm_unpackhi_epi16(u0, v0);
				y0 = _mm_unpacklo_epi16(y1, y2);
				y1 = _mm_unpackhi_epi16(y1, y2);
				g0 = _mm_madd_epi16(g0, guv);
				g1 = _mm_madd_epi16(g1, guv);
				g0 = _mm_add_epi32(g0, y0);
				g1 = _mm_add_epi32(g1, y1);
				g0 = _mm_srai_epi32(g0, 13);
				g1 = _mm_srai_epi32(g1, 13);
				g0 = _mm_packs_epi32(g0, g1);
				g0 = _mm_packus_epi16(g0, _mm_undefined_si128());

				__m128i c0, c1, c2, c3;

				c0 = _mm_unpacklo_epi8(b0, g0);
				c1 = _mm_unpacklo_epi8(r0, Zero);
				c2 = _mm_unpacklo_epi16(c0, c1);
				c3 = _mm_unpackhi_epi16(c0, c1);

#ifdef TVTVIDEODEC_SSSE3_SUPPORT
				if (DstPlanes == 3) {
					c2 = _mm_shuffle_epi8(c2, Shuffle);
					c3 = _mm_shuffle_epi8(c3, Shuffle);
					c0 = _mm_slli_si128(c3, 12);
					c2 = _mm_or_si128(c2, c0);
					c3 = _mm_srli_si128(c3, 4);
					_mm_storeu_si128((__m128i*)q + 0, c2);
					_mm_storel_epi64((__m128i*)q + 1, c3);
				} else
#endif
				{
					_mm_store_si128((__m128i*)q + 0, c2);
					_mm_store_si128((__m128i*)q + 1, c3);
				}

				q += DstPlanes * 8;
			}

			py += SrcPitchY;
			pu += SrcPitchC;
			pv += SrcPitchC;
			pDstRow += DstPitch;
		}
	} else
#endif // TVTVIDEODEC_SSE2_SUPPORT
	{
		const uint8_t * restrict pTable = Table.Table;

		for (int y = 0; y < Height; y++) {
			uint8_t * restrict q = pDstRow;

			for (int x = 0; x < Width; x++) {
				const int Y = YGain * (py[x] - YOffset);
				const int U = pu[x >> 1] - 128;
				const int V = pv[x >> 1] - 128;
				q[0] = pTable[YUV_RGB_TABLE_OFFSET + ((Y + (BU * U)) >> 16)];
				q[1] = pTable[YUV_RGB_TABLE_OFFSET + ((Y + (GU * U) + (GV * V)) >> 16)];
				q[2] = pTable[YUV_RGB_TABLE_OFFSET + ((Y + (RV * V)) >> 16)];
				q += DstPlanes;
			}

			py += SrcPitchY;
			pu += SrcPitchC;
			pv += SrcPitchC;
			pDstRow += DstPitch;
		}
	}

	return true;
}


YUVToRGBConverter::YUVToRGBConverter()
	: m_pBuffer(nullptr)
	, m_BufferSize(0)
{
}

YUVToRGBConverter::~YUVToRGBConverter()
{
	if (m_pBuffer) {
		::_aligned_free(m_pBuffer);
	}
}

void YUVToRGBConverter::InitializeMPEG2(uint8_t MatrixCoefficients, bool fStraight)
{
	SetupYUVToRGBConvertParametersMPEG2(&m_Params, MatrixCoefficients, fStraight);
	SetupYUVToRGBConvertTable(&m_Table, &m_Params);
}

bool YUVToRGBConverter::Convert(
	CFrameBuffer *pDstFrameBuffer, const CFrameBuffer *pSrcFrameBuffer, bool fProgressive)
{
	if (!pDstFrameBuffer || !pSrcFrameBuffer
			|| pDstFrameBuffer->m_Width != pSrcFrameBuffer->m_Width
			|| pDstFrameBuffer->m_Height != pSrcFrameBuffer->m_Height
			|| (pDstFrameBuffer->m_Subtype != MEDIASUBTYPE_RGB24
				&& pDstFrameBuffer->m_Subtype != MEDIASUBTYPE_RGB32)) {
		return false;
	}

	if (pSrcFrameBuffer->m_Subtype == MEDIASUBTYPE_I420
			|| pSrcFrameBuffer->m_Subtype == MEDIASUBTYPE_IYUV) {
		const size_t RowBytes = ((pSrcFrameBuffer->m_Width >> 1) + 31) & ~31;
		const size_t PlaneSize = RowBytes * pSrcFrameBuffer->m_Height;
		const size_t BufferSize = PlaneSize * 2;

		if (m_BufferSize < BufferSize) {
			if (m_pBuffer) {
				::_aligned_free(m_pBuffer);
			}
			m_pBuffer = static_cast<uint8_t*>(::_aligned_malloc(BufferSize, 32));
			if (!m_pBuffer) {
				m_BufferSize = 0;
				return false;
			}
			m_BufferSize = BufferSize;
		}

		uint8_t *u = m_pBuffer;
		uint8_t *v = m_pBuffer + PlaneSize;

		if (fProgressive) {
			UpsampleI420pToI422(
				pSrcFrameBuffer->m_Width, pSrcFrameBuffer->m_Height,
				u, RowBytes,
				pSrcFrameBuffer->m_Buffer[1], pSrcFrameBuffer->m_PitchC);
			UpsampleI420pToI422(
				pSrcFrameBuffer->m_Width, pSrcFrameBuffer->m_Height,
				v, RowBytes,
				pSrcFrameBuffer->m_Buffer[2], pSrcFrameBuffer->m_PitchC);
		} else {
			UpsampleI420iToI422(
				pSrcFrameBuffer->m_Width, pSrcFrameBuffer->m_Height,
				u, RowBytes,
				pSrcFrameBuffer->m_Buffer[1], pSrcFrameBuffer->m_PitchC);
			UpsampleI420iToI422(
				pSrcFrameBuffer->m_Width, pSrcFrameBuffer->m_Height,
				v, RowBytes,
				pSrcFrameBuffer->m_Buffer[2], pSrcFrameBuffer->m_PitchC);
		}

		return PixelCopyI422ToRGB(
			pSrcFrameBuffer->m_Width, pSrcFrameBuffer->m_Height,
			pDstFrameBuffer->m_Buffer[0],
			(pDstFrameBuffer->m_Subtype == MEDIASUBTYPE_RGB24) ? 3 : 4,
			pDstFrameBuffer->m_PitchY,
			pSrcFrameBuffer->m_Buffer[0], u, v,
			pSrcFrameBuffer->m_PitchY, RowBytes,
			m_Params, m_Table);
	}

	return false;
}
