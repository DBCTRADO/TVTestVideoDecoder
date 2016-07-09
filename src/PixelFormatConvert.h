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


#include "FrameBuffer.h"


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

bool UpsampleI420pToI422(
	int Width, int Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrc, ptrdiff_t SrcPitch);
bool UpsampleI420iToI422(
	int Width, int Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrc, ptrdiff_t SrcPitch);

struct YUVToRGBConvertParameters
{
	int YOffset;
	int YGain;
	int RV;
	int GU;
	int GV;
	int BU;
};

struct YUVToRGBConvertTable
{
	uint8_t Table[1024];
};

void SetupYUVToRGBConvertParametersMPEG2(
	YUVToRGBConvertParameters *pParams, uint8_t MatrixCoefficients, bool fStraight = false);
void SetupYUVToRGBConvertTable(
	YUVToRGBConvertTable *pTable, const YUVToRGBConvertParameters *pParams);

bool PixelCopyI422ToRGB(
	int Width, int Height,
	uint8_t * restrict pDst, int DstPlanes, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC,
	const YUVToRGBConvertParameters &Params, const YUVToRGBConvertTable &Table);

class YUVToRGBConverter
{
public:
	YUVToRGBConverter();
	~YUVToRGBConverter();
	void InitializeMPEG2(uint8_t MatrixCoefficients, bool fStraight = false);
	bool Convert(CFrameBuffer *pDstFrameBuffer, const CFrameBuffer *pSrcFrameBuffer, bool fProgressive);

private:
	YUVToRGBConvertParameters m_Params;
	YUVToRGBConvertTable m_Table;
	uint8_t *m_pBuffer;
	size_t m_BufferSize;
};
