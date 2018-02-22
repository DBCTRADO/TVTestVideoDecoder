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
#include "VideoFrameDecoder.h"
#include "Util.h"
#include "Common.h"
#include "MediaTypes.h"


CTVTestVideoFrameDecoder::CTVTestVideoFrameDecoder(LPUNKNOWN punk, HRESULT *phr)
	: CUnknown(L"TVTestVideoFrameDecoder", punk, phr)
	, m_pDecoder(nullptr)
	, m_DeinterlaceMethod(TVTVIDEODEC_DEINTERLACE_BLEND)
	, m_fEnableWaitForKeyFrame(true)
	, m_fWaitForKeyFrame(true)
	, m_fSkipBFrames(false)
	, m_fCrop1088To1080(true)
	, m_NumThreads(0)
{
	if (phr)
		*phr = S_OK;
}

CTVTestVideoFrameDecoder::~CTVTestVideoFrameDecoder()
{
	SafeDelete(m_pDecoder);
}

CUnknown * CALLBACK CTVTestVideoFrameDecoder::CreateInstance(LPUNKNOWN punk, HRESULT *phr)
{
	CTVTestVideoFrameDecoder *pNewObject = DNew_nothrow CTVTestVideoFrameDecoder(punk, phr);

	if (!pNewObject && phr) {
		*phr = E_OUTOFMEMORY;
	}

	return pNewObject;
}

STDMETHODIMP CTVTestVideoFrameDecoder::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
	if (riid == __uuidof(ITVTestVideoFrameDecoder))
		return GetInterface(static_cast<ITVTestVideoFrameDecoder*>(this), ppv);

	return __super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP CTVTestVideoFrameDecoder::Open(REFGUID VideoSubtype)
{
	if (VideoSubtype != MEDIASUBTYPE_MPEG2_VIDEO) {
		return E_INVALIDARG;
	}

	CAutoLock Lock(&m_csProps);

	if (m_pDecoder) {
		m_pDecoder->Close();
	} else {
		m_pDecoder = DNew_nothrow CMpeg2Decoder;
		if (!m_pDecoder) {
			return E_OUTOFMEMORY;
		}
	}

	m_pDecoder->SetNumThreads(m_NumThreads);
	m_pDecoder->Open();

	m_fWaitForKeyFrame = m_fEnableWaitForKeyFrame;

	m_FrameBuffer.m_Flags = 0;
	m_FrameBuffer.m_Deinterlace = m_DeinterlaceMethod;

	m_Deinterlacers[m_DeinterlaceMethod]->Initialize();

	return S_OK;
}

STDMETHODIMP CTVTestVideoFrameDecoder::Close()
{
	CAutoLock Lock(&m_csProps);

	if (m_pDecoder) {
		m_pDecoder->Close();
	}

	return S_OK;
}

STDMETHODIMP CTVTestVideoFrameDecoder::InputStream(const void *pData, SIZE_T Size)
{
	CheckPointer(pData, E_POINTER);

	if (Size == 0 || Size > LONG_MAX) {
		return E_INVALIDARG;
	}

	CAutoLock Lock(&m_csProps);

	if (!m_pDecoder) {
		return E_UNEXPECTED;
	}

	const mpeg2_info_t *pInfo = m_pDecoder->GetMpeg2Info();
	long DataLength = static_cast<long>(Size);

	while (DataLength >= 0) {
		mpeg2_state_t state = m_pDecoder->Parse();

		switch (state) {
		case STATE_BUFFER:
			if (DataLength > 0) {
				m_pDecoder->PutBuffer(static_cast<const uint8_t *>(pData), DataLength);
				DataLength = 0;
			} else {
				DataLength = -1;
			}
			break;

		case STATE_PICTURE:
			{
				const mpeg2_picture_t *picture = m_pDecoder->GetPicture();
				bool fSkip = false;

				if (m_fSkipBFrames
						&& (picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_B) {
					fSkip = true;
				}

				m_pDecoder->Skip(fSkip);
			}
			break;

		case STATE_SLICE:
		case STATE_END:
			if (pInfo->display_fbuf
					&& pInfo->sequence->width == pInfo->sequence->chroma_width * 2
					&& pInfo->sequence->height == pInfo->sequence->chroma_height * 2) {
				const mpeg2_picture_t *picture = pInfo->display_picture;

				if (picture) {
					int Width = pInfo->sequence->picture_width;
					int Height = pInfo->sequence->picture_height;

					if (m_fCrop1088To1080 && Height == 1088) {
						Height = 1080;
					}
					if (m_FrameBuffer.m_Width != Width || m_FrameBuffer.m_Height != Height) {
						if (!m_FrameBuffer.Allocate(Width, Height)) {
							Close();
							return E_OUTOFMEMORY;
						}
					}

					m_pDecoder->GetAspectRatio(&m_FrameBuffer.m_AspectX, &m_FrameBuffer.m_AspectY);

					m_pDecoder->GetFrameFlags(&m_FrameBuffer.m_Flags);

					if (m_FrameBuffer.m_Flags & FRAME_FLAG_I_FRAME) {
						m_fWaitForKeyFrame = false;
					}

					if (!(picture->flags & PIC_FLAG_SKIP) && !m_fWaitForKeyFrame) {
						HRESULT hr = OutFrame(&m_FrameBuffer);
						if (FAILED(hr)) {
							Close();
							return hr;
						}
					}
				}
			}
			break;

		case STATE_INVALID:
			Open(MEDIASUBTYPE_MPEG2_VIDEO);
			break;
		}
	}

	return S_OK;
}

STDMETHODIMP CTVTestVideoFrameDecoder::SetFrameCapture(ITVTestVideoDecoderFrameCapture *pFrameCapture, REFGUID Subtype)
{
	CAutoLock Lock(&m_csProps);

	return m_FrameCapture.SetFrameCapture(pFrameCapture, Subtype);
}

STDMETHODIMP CTVTestVideoFrameDecoder::SetDeinterlaceMethod(TVTVIDEODEC_DeinterlaceMethod Method)
{
	if (Method < TVTVIDEODEC_DEINTERLACE_FIRST || Method > TVTVIDEODEC_DEINTERLACE_LAST) {
		return E_INVALIDARG;
	}

	CAutoLock Lock(&m_csProps);

	if (m_DeinterlaceMethod != Method) {
		m_DeinterlaceMethod = Method;

		if (m_pDecoder) {
			m_FrameBuffer.m_Deinterlace = Method;
			m_Deinterlacers[Method]->Initialize();
		}
	}

	return S_OK;
}

STDMETHODIMP_(TVTVIDEODEC_DeinterlaceMethod) CTVTestVideoFrameDecoder::GetDeinterlaceMethod()
{
	return m_DeinterlaceMethod;
}

STDMETHODIMP CTVTestVideoFrameDecoder::SetWaitForKeyFrame(BOOL fWait)
{
	CAutoLock Lock(&m_csProps);
	m_fEnableWaitForKeyFrame = fWait != FALSE;
	return S_OK;
}

STDMETHODIMP_(BOOL) CTVTestVideoFrameDecoder::GetWaitForKeyFrame()
{
	return m_fEnableWaitForKeyFrame;
}

STDMETHODIMP CTVTestVideoFrameDecoder::SetSkipBFrames(BOOL fSkip)
{
	CAutoLock Lock(&m_csProps);
	m_fSkipBFrames = fSkip != FALSE;
	return S_OK;
}

STDMETHODIMP_(BOOL) CTVTestVideoFrameDecoder::GetSkipBFrames()
{
	return m_fSkipBFrames;
}

STDMETHODIMP CTVTestVideoFrameDecoder::SetNumThreads(int NumThreads)
{
	if (NumThreads < 0 || NumThreads > TVTVIDEODEC_MAX_THREADS) {
		return E_INVALIDARG;
	}
	CAutoLock Lock(&m_csProps);
	m_NumThreads = NumThreads;
	return S_OK;
}

STDMETHODIMP_(int) CTVTestVideoFrameDecoder::GetNumThreads()
{
	return m_NumThreads;
}

HRESULT CTVTestVideoFrameDecoder::OutFrame(CFrameBuffer *pFrameBuffer)
{
	CFrameBuffer SrcBuffer;

	SrcBuffer.CopyAttributesFrom(pFrameBuffer);

	if (!m_pDecoder->GetFrame(&SrcBuffer)) {
		return E_UNEXPECTED;
	}

	CDeinterlacer *pDeinterlacer = m_Deinterlacers[pFrameBuffer->m_Deinterlace];
	const bool fTopFieldFirst = !!(pFrameBuffer->m_Flags & FRAME_FLAG_TOP_FIELD_FIRST);
	CDeinterlacer::FrameStatus FrameStatus;
	HRESULT hr = S_OK;

	FrameStatus = pDeinterlacer->GetFrame(pFrameBuffer, &SrcBuffer, fTopFieldFirst, 0);
	if (FrameStatus == CDeinterlacer::FRAME_SKIP) {
		FrameStatus = m_Deinterlacer_Blend.GetFrame(pFrameBuffer, &SrcBuffer, fTopFieldFirst, 0);
	}
	if (FrameStatus == CDeinterlacer::FRAME_OK) {
		hr = m_FrameCapture.FrameCaptureCallback(m_pDecoder, pFrameBuffer);
		if (FAILED(hr)) {
			return hr;
		}
	}
	if (pDeinterlacer->IsDoubleFrame()) {
		FrameStatus = pDeinterlacer->GetFrame(pFrameBuffer, &SrcBuffer, !fTopFieldFirst, 1);
		if (FrameStatus == CDeinterlacer::FRAME_OK) {
			hr = m_FrameCapture.FrameCaptureCallback(m_pDecoder, pFrameBuffer);
		}
	}

	return hr;
}
