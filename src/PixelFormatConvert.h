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


bool PixelCopyI420ToI420(
	int Width, int Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstU, uint8_t * restrict pDstV,
	ptrdiff_t DstPitchY, ptrdiff_t DstPitchC,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC);
bool PixelCopyI420ToNV12(
	int Width, int Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstUV, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC);
bool PixelCopyNV12ToI420(
	int Width, int Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstU, uint8_t * restrict pDstV,
	ptrdiff_t DstPitchY, ptrdiff_t DstPitchC,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcUV, ptrdiff_t SrcPitch);
bool PixelCopyNV12ToNV12(
	int Width, int Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstUV, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcUV, ptrdiff_t SrcPitch);
bool PixelCopyRGBToRGB(
	int Width, int Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch, int DstBPP,
	const uint8_t * restrict pSrc, ptrdiff_t SrcPitch, int SrcBPP);
