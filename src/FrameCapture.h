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


#include "ITVTestVideoDecoder.h"
#include "Mpeg2Decoder.h"
#include "FrameBuffer.h"
#include "PixelFormatConvert.h"


class CFrameCapture
{
public:
	CFrameCapture();
	~CFrameCapture();

	HRESULT SetFrameCapture(ITVTestVideoDecoderFrameCapture *pFrameCapture, REFGUID Subtype);
	void UnsetFrameCapture();
	bool IsEnabled() const;
	bool IsCaptureSubtypeSupported(REFGUID SrcSubtype, REFGUID DstSubtype) const;
	HRESULT FrameCaptureCallback(const CMpeg2Decoder *pDecoder, const CFrameBuffer *pFrameBuffer);

private:
	ITVTestVideoDecoderFrameCapture *m_pFrameCapture;
	GUID m_FrameCaptureSubtype;
	YUVToRGBConverter m_YUVToRGBConverter;
	uint8_t m_MatrixCoefficients;
	CFrameBuffer m_RGBFrameBuffer;
};
