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
