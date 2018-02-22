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

#include "stdafx.h"
#include "FrameCapture.h"
#include "Util.h"
#include "MediaTypes.h"


CFrameCapture::CFrameCapture()
	: m_pFrameCapture(nullptr)
	, m_MatrixCoefficients(0xFF)
{
}

CFrameCapture::~CFrameCapture()
{
	SafeRelease(m_pFrameCapture);
}

HRESULT CFrameCapture::SetFrameCapture(ITVTestVideoDecoderFrameCapture *pFrameCapture, REFGUID Subtype)
{
	SafeRelease(m_pFrameCapture);

	if (!IsCaptureSubtypeSupported(MEDIASUBTYPE_I420, Subtype)) {
		return E_NOTIMPL;
	}

	if (pFrameCapture) {
		m_pFrameCapture = pFrameCapture;
		m_pFrameCapture->AddRef();
		m_FrameCaptureSubtype = Subtype;
	}

	return S_OK;
}

void CFrameCapture::UnsetFrameCapture()
{
	SafeRelease(m_pFrameCapture);
}

bool CFrameCapture::IsEnabled() const
{
	return m_pFrameCapture != nullptr;
}

bool CFrameCapture::IsCaptureSubtypeSupported(REFGUID SrcSubtype, REFGUID DstSubtype) const
{
	return (SrcSubtype == MEDIASUBTYPE_I420 || SrcSubtype == MEDIASUBTYPE_IYUV)
		&& (DstSubtype == MEDIASUBTYPE_I420 || DstSubtype == MEDIASUBTYPE_IYUV
			|| DstSubtype == MEDIASUBTYPE_RGB24 || DstSubtype == MEDIASUBTYPE_RGB32);
}

HRESULT CFrameCapture::FrameCaptureCallback(const CMpeg2Decoder *pDecoder, const CFrameBuffer *pFrameBuffer)
{
	if (!m_pFrameCapture) {
		return E_UNEXPECTED;
	}

	if (!pDecoder || !pFrameBuffer) {
		return E_POINTER;
	}

	if (pFrameBuffer->m_Subtype != MEDIASUBTYPE_I420 && pFrameBuffer->m_Subtype != MEDIASUBTYPE_IYUV) {
		return E_UNEXPECTED;
	}

	TVTVIDEODEC_FrameInfo FrameInfo = {};

	FrameInfo.Width = pFrameBuffer->m_Width;
	FrameInfo.Height = pFrameBuffer->m_Height;
	FrameInfo.AspectX = pFrameBuffer->m_AspectX;
	FrameInfo.AspectY = pFrameBuffer->m_AspectY;
	FrameInfo.Subtype = m_FrameCaptureSubtype;

	if (pFrameBuffer->m_Flags & FRAME_FLAG_TOP_FIELD_FIRST)
		FrameInfo.Flags |= TVTVIDEODEC_FRAME_TOP_FIELD_FIRST;
	if (pFrameBuffer->m_Flags & FRAME_FLAG_REPEAT_FIRST_FIELD)
		FrameInfo.Flags |= TVTVIDEODEC_FRAME_REPEAT_FIRST_FIELD;
	if (pFrameBuffer->m_Flags & FRAME_FLAG_PROGRESSIVE_FRAME)
		FrameInfo.Flags |= TVTVIDEODEC_FRAME_PROGRESSIVE;
	if (pFrameBuffer->m_Deinterlace == TVTVIDEODEC_DEINTERLACE_WEAVE)
		FrameInfo.Flags |= TVTVIDEODEC_FRAME_WEAVE;
	if (pFrameBuffer->m_Flags & FRAME_FLAG_I_FRAME)
		FrameInfo.Flags |= TVTVIDEODEC_FRAME_TYPE_I;
	if (pFrameBuffer->m_Flags & FRAME_FLAG_P_FRAME)
		FrameInfo.Flags |= TVTVIDEODEC_FRAME_TYPE_P;
	if (pFrameBuffer->m_Flags & FRAME_FLAG_B_FRAME)
		FrameInfo.Flags |= TVTVIDEODEC_FRAME_TYPE_B;

	const mpeg2_info_t *info = pDecoder->GetMpeg2Info();
	if (info && (info->sequence->flags & SEQ_FLAG_COLOUR_DESCRIPTION)) {
		FrameInfo.ColorDescription.Flags = TVTVIDEODEC_COLOR_DESCRIPTION_PRESENT;
		FrameInfo.ColorDescription.ColorPrimaries = info->sequence->colour_primaries;
		FrameInfo.ColorDescription.MatrixCoefficients = info->sequence->matrix_coefficients;
		FrameInfo.ColorDescription.TransferCharacteristics = info->sequence->transfer_characteristics;
	} else {
		FrameInfo.ColorDescription.ColorPrimaries = 1;
		FrameInfo.ColorDescription.MatrixCoefficients = 1;
		FrameInfo.ColorDescription.TransferCharacteristics = 1;
	}

	const CFrameBuffer *pOutFrameBuffer;

	if (m_FrameCaptureSubtype == MEDIASUBTYPE_I420 || m_FrameCaptureSubtype == MEDIASUBTYPE_IYUV) {
		pOutFrameBuffer = pFrameBuffer;
	} else if (m_FrameCaptureSubtype == MEDIASUBTYPE_RGB24 || m_FrameCaptureSubtype == MEDIASUBTYPE_RGB32) {
		if (m_MatrixCoefficients != FrameInfo.ColorDescription.MatrixCoefficients) {
			m_YUVToRGBConverter.InitializeMPEG2(m_MatrixCoefficients);
			m_MatrixCoefficients = FrameInfo.ColorDescription.MatrixCoefficients;
		}
		if (!m_RGBFrameBuffer.Allocate(pFrameBuffer->m_Width, pFrameBuffer->m_Height, m_FrameCaptureSubtype)) {
			return E_OUTOFMEMORY;
		}
		if (!m_YUVToRGBConverter.Convert(
				&m_RGBFrameBuffer, pFrameBuffer,
				(FrameInfo.Flags & TVTVIDEODEC_FRAME_PROGRESSIVE) || !(FrameInfo.Flags & TVTVIDEODEC_FRAME_WEAVE))) {
			return E_FAIL;
		}
		pOutFrameBuffer = &m_RGBFrameBuffer;
	} else {
		return E_UNEXPECTED;
	}

	const BYTE *Buffer[3];
	int Pitch[3];

	Buffer[0] = pOutFrameBuffer->m_Buffer[0];
	Buffer[1] = pOutFrameBuffer->m_Buffer[1];
	Buffer[2] = pOutFrameBuffer->m_Buffer[2];
	Pitch[0] = pOutFrameBuffer->m_PitchY;
	Pitch[1] = pOutFrameBuffer->m_PitchC;
	Pitch[2] = pOutFrameBuffer->m_PitchC;

	FrameInfo.Buffer = Buffer;
	FrameInfo.Pitch = Pitch;

	return m_pFrameCapture->OnFrame(&FrameInfo);
}
