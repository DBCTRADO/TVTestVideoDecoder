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


#include "FrameBuffer.h"


void DeinterlaceBlend(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch, const uint8_t * restrict pSrc, ptrdiff_t SrcPitch);
void DeinterlaceBlendI420(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstU, uint8_t * restrict pDstV,
	ptrdiff_t DstPitchY, ptrdiff_t DstPitchC,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC);
void DeinterlaceBlendNV12(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstUV, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcUV, ptrdiff_t SrcPitch);

void DeinterlaceBob(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch, const uint8_t * restrict pSrc, ptrdiff_t SrcPitch,
	bool fTopFiled);
void DeinterlaceBobI420(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstU, uint8_t * restrict pDstV,
	ptrdiff_t DstPitchY, ptrdiff_t DstPitchC,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC,
	bool fTopFiled);
void DeinterlaceBobNV12(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstUV, ptrdiff_t DstPitch,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcUV, ptrdiff_t SrcPitch,
	bool fTopFiled);

void DeinterlaceELA(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDst, ptrdiff_t DstPitch, const uint8_t * restrict pSrc, ptrdiff_t SrcPitch,
	bool fTopFiled);
void DeinterlaceELAI420(
	uint32_t Width, uint32_t Height,
	uint8_t * restrict pDstY, uint8_t * restrict pDstU, uint8_t * restrict pDstV,
	ptrdiff_t DstPitchY, ptrdiff_t DstPitchC,
	const uint8_t * restrict pSrcY, const uint8_t * restrict pSrcU, const uint8_t * restrict pSrcV,
	ptrdiff_t SrcPitchY, ptrdiff_t SrcPitchC,
	bool fTopFiled);


class CDeinterlacer
{
public:
	enum FrameStatus {
		FRAME_OK,
		FRAME_PENDING,
		FRAME_SKIP
	};

	virtual ~CDeinterlacer() {}
	virtual bool Initialize() { return true; }
	virtual void Finalize() {}
	virtual FrameStatus GetFrame(
		CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
		bool fTopFiledFirst, int Field = 0) = 0;
	virtual bool FramePostProcess(CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer, bool fTopFiledFirst) { return false; }
	virtual bool IsDoubleFrame() const { return false; }
	virtual bool IsRetainFrame() const { return false; }
	virtual bool IsFormatSupported(const GUID &SrcSubtype, const GUID &DstSubtype) const;
};

class CDeinterlacer_Weave : public CDeinterlacer
{
public:
	FrameStatus GetFrame(
		CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
		bool fTopFiledFirst, int Field) override;
	bool IsFormatSupported(const GUID &SrcSubtype, const GUID &DstSubtype) const override;
};

class CDeinterlacer_Blend : public CDeinterlacer
{
public:
	FrameStatus GetFrame(
		CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
		bool fTopFiledFirst, int Field) override;
	bool IsFormatSupported(const GUID &SrcSubtype, const GUID &DstSubtype) const override;
};

class CDeinterlacer_Bob : public CDeinterlacer
{
public:
	FrameStatus GetFrame(
		CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
		bool fTopFiledFirst, int Field) override;
	bool IsDoubleFrame() const override { return true; }
	bool IsFormatSupported(const GUID &SrcSubtype, const GUID &DstSubtype) const override;
};

class CDeinterlacer_ELA : public CDeinterlacer
{
public:
	FrameStatus GetFrame(
		CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
		bool fTopFiledFirst, int Field) override;
	bool IsDoubleFrame() const override { return true; }
};

class CDeinterlacer_FieldShift : public CDeinterlacer
{
public:
	FrameStatus GetFrame(
		CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
		bool fTopFiledFirst, int Field) override;
	bool FramePostProcess(CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer, bool fTopFiledFirst) override;
	bool IsRetainFrame() const override { return true; }
};
