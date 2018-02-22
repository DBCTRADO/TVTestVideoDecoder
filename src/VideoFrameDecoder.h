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


#include "ITVTestVideoDecoder.h"
#include "Mpeg2Decoder.h"
#include "FrameBuffer.h"
#include "FrameCapture.h"
#include "DeinterlacerSet.h"


class CTVTestVideoFrameDecoder
	: public CUnknown
	, public ITVTestVideoFrameDecoder
	, protected CDeinterlacerSet
{
public:
	CTVTestVideoFrameDecoder(LPUNKNOWN punk, HRESULT *phr);
	~CTVTestVideoFrameDecoder();

	static CUnknown * CALLBACK CreateInstance(LPUNKNOWN punk, HRESULT *phr);

// CUnknown

	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void **ppv) override;

// ITVTestVideoFrameDecoder

	STDMETHODIMP Open(REFGUID VideoSubtype) override;
	STDMETHODIMP Close() override;

	STDMETHODIMP InputStream(const void *pData, SIZE_T Size) override;

	STDMETHODIMP SetFrameCapture(ITVTestVideoDecoderFrameCapture *pFrameCapture, REFGUID Subtype) override;

	STDMETHODIMP SetDeinterlaceMethod(TVTVIDEODEC_DeinterlaceMethod Method) override;
	STDMETHODIMP_(TVTVIDEODEC_DeinterlaceMethod) GetDeinterlaceMethod() override;
	STDMETHODIMP SetWaitForKeyFrame(BOOL fWait) override;
	STDMETHODIMP_(BOOL) GetWaitForKeyFrame() override;
	STDMETHODIMP SetSkipBFrames(BOOL fSkip) override;
	STDMETHODIMP_(BOOL) GetSkipBFrames() override;
	STDMETHODIMP SetNumThreads(int NumThreads) override;
	STDMETHODIMP_(int) GetNumThreads() override;

private:
	HRESULT OutFrame(CFrameBuffer *pFrameBuffer);

	CMpeg2Decoder *m_pDecoder;
	CFrameBuffer m_FrameBuffer;
	CFrameCapture m_FrameCapture;
	TVTVIDEODEC_DeinterlaceMethod m_DeinterlaceMethod;
	bool m_fEnableWaitForKeyFrame;
	bool m_fWaitForKeyFrame;
	bool m_fSkipBFrames;
	bool m_fCrop1088To1080;
	int m_NumThreads;
	CCritSec m_csProps;
};
