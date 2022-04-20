/*
 *  TVTest DTV Video Decoder
 *  Copyright (C) 2015-2022 DBCTRADO
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


#include "BaseVideoFilter.h"
#include "ITVTestVideoDecoder.h"
#include "TVTestVideoDecoderProp.h"
#include "ISpecifyPropertyPages2.h"
#include "Mpeg2Decoder.h"
#include "FrameBuffer.h"
#include "DeinterlacerSet.h"
#include "Deinterlace_DXVA.h"
#include "ColorAdjustment.h"
#include "FrameCapture.h"


#define REGISTRY_PARENT_KEY_NAME L"Software\\DBCTRADO"
#define REGISTRY_KEY_NAME        REGISTRY_PARENT_KEY_NAME L"\\" TVTVIDEODEC_FILTER_NAME

class CTVTestVideoDecoder
	: public CBaseVideoFilter
	, public ITVTestVideoDecoder2
	, public ISpecifyPropertyPages2
	, protected CDeinterlacerSet
{
	friend class CMpeg2DecoderDXVA2;
	friend class CMpeg2DecoderD3D11;

public:
	CTVTestVideoDecoder(LPUNKNOWN lpunk, HRESULT *phr, bool fLocal = false);
	~CTVTestVideoDecoder();

	static CUnknown * CALLBACK CreateInstance(LPUNKNOWN punk, HRESULT *phr);

// CUnknown

	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void **ppv) override;

// CTransformFilter

	HRESULT EndOfStream() override;
	HRESULT BeginFlush() override;
	HRESULT EndFlush() override;
	HRESULT NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) override;

	HRESULT CheckInputType(const CMediaType *mtIn) override;
	HRESULT BreakConnect(PIN_DIRECTION dir) override;
	HRESULT CompleteConnect(PIN_DIRECTION direction, IPin *pReceivePin) override;

	HRESULT StartStreaming() override;
	HRESULT StopStreaming() override;

	HRESULT AlterQuality(Quality q) override;

// ISpecifyPropertyPages

	STDMETHODIMP GetPages(CAUUID *pPages) override;

// ISpecifyPropertyPages2

	STDMETHODIMP CreatePage(const GUID &guid, IPropertyPage **ppPage) override;

// ITVTestVideoDecoder

	STDMETHODIMP SetEnableDeinterlace(BOOL fEnable) override;
	STDMETHODIMP_(BOOL) GetEnableDeinterlace() override;
	STDMETHODIMP SetDeinterlaceMethod(TVTVIDEODEC_DeinterlaceMethod Method) override;
	STDMETHODIMP_(TVTVIDEODEC_DeinterlaceMethod) GetDeinterlaceMethod() override;
	STDMETHODIMP SetAdaptProgressive(BOOL fEnable) override;
	STDMETHODIMP_(BOOL) GetAdaptProgressive() override;
	STDMETHODIMP SetAdaptTelecine(BOOL fEnable) override;
	STDMETHODIMP_(BOOL) GetAdaptTelecine() override;
	STDMETHODIMP SetInterlacedFlag(BOOL fEnable) override;
	STDMETHODIMP_(BOOL) GetInterlacedFlag() override;

	STDMETHODIMP SetBrightness(int Brightness) override;
	STDMETHODIMP_(int) GetBrightness() override;
	STDMETHODIMP SetContrast(int Contrast) override;
	STDMETHODIMP_(int) GetContrast() override;
	STDMETHODIMP SetHue(int Hue) override;
	STDMETHODIMP_(int) GetHue() override;
	STDMETHODIMP SetSaturation(int Saturation) override;
	STDMETHODIMP_(int) GetSaturation() override;

	STDMETHODIMP SetNumThreads(int NumThreads) override;
	STDMETHODIMP_(int) GetNumThreads() override;
	STDMETHODIMP SetEnableDXVA2(BOOL fEnable) override;
	STDMETHODIMP_(BOOL) GetEnableDXVA2() override;

	STDMETHODIMP LoadOptions() override;
	STDMETHODIMP SaveOptions() override;

	STDMETHODIMP GetStatistics(TVTVIDEODEC_Statistics *pStatistics) override;

	STDMETHODIMP SetFrameCapture(ITVTestVideoDecoderFrameCapture *pFrameCapture) override;

// ITVTestVideoDecoder2

	STDMETHODIMP SetEnableD3D11(BOOL fEnable) override;
	STDMETHODIMP_(BOOL) GetEnableD3D11() override;
	STDMETHODIMP SetNumQueueFrames(UINT NumFrames) override;
	STDMETHODIMP_(UINT) GetNumQueueFrames() override;

private:
	CMpeg2Decoder *m_pDecoder;
	CFrameBuffer m_FrameBuffer;
	REFERENCE_TIME m_AvgTimePerFrame;
	bool m_fWaitForKeyFrame;
	bool m_fDropFrames;
	bool m_fTelecineMode;
	bool m_fFilm;
	uint32_t m_RecentFrameTypes;
	int m_LastFilmFrame;
	int m_FrameCount;
	AM_SimpleRateChange m_RateChange;
	bool m_fLocalInstance;
	bool m_fForceSoftwareDecoder;
	bool m_fReInitDecoder;
	CCritSec m_csStats;
	TVTVIDEODEC_Statistics m_Statistics;

	CFrameBuffer m_DeinterlaceTempBuffer[2];
	CDeinterlacer_DXVA m_Deinterlacer_DXVA;

	CColorAdjustment m_ColorAdjustment;

	CCritSec m_csProps;
	bool m_fDXVA2Decode;
	bool m_fD3D11Decode;
	bool m_fEnableDeinterlace;
	TVTVIDEODEC_DeinterlaceMethod m_DeinterlaceMethod;
	bool m_fAdaptProgressive;
	bool m_fAdaptTelecine;
	bool m_fSetInterlacedFlag;
	bool m_fDXVADeinterlace;
	int m_Brightness;
	int m_Contrast;
	int m_Hue;
	int m_Saturation;
	int m_NumThreads;
	UINT m_NumQueueFrames;
	bool m_fCrop1088To1080;
	CFrameCapture m_FrameCapture;

	HRESULT DeliverFrame(const CFrameBuffer *pFrameBuffer);
	HRESULT Deliver(IMediaSample *pOutSample, CFrameBuffer *pFrameBuffer);
	HRESULT SetupOutputFrameBuffer(CFrameBuffer *pFrameBuffer, IMediaSample *pSample, CDeinterlacer *pDeinterlacer);
	void SetFrameStatus();
	void SetTypeSpecificFlags(IMediaSample *pSample);
	HRESULT InitDecode(bool fPutSequenceHeader);
	HRESULT FlushFrameQueue();
	void ClearFrameQueue();

	HRESULT Transform(IMediaSample *pIn) override;
	bool IsVideoInterlaced() override;
	void GetOutputFormatList(OutputFormatList *pFormatList) const override;
	DWORD GetVideoInfoControlFlags() const override;
	D3DFORMAT GetDXVA2SurfaceFormat() const override;
	HRESULT OnDXVA2DeviceHandleOpened() override;
	HRESULT OnDXVA2Connect(IPin *pPin) override;
	HRESULT OnDXVA2SurfaceCreated(IDirect3DSurface9 **ppSurface, int SurfaceCount) override;
	HRESULT OnDXVA2AllocatorDecommit() override;
};

class CMpeg2DecoderInputPin
	: public CTransformInputPin
	, public IKsPropertySet
{
	friend CTVTestVideoDecoder;

public:
	CMpeg2DecoderInputPin(CTransformFilter *pFilter, HRESULT *phr, PCWSTR pName);

// CUnknown

	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void **ppv) override;

// IMemInputPin

	STDMETHODIMP Receive(IMediaSample* pSample) override;

// IKsPropertySet

	STDMETHODIMP Set(
		REFGUID PropSet, ULONG Id, LPVOID InstanceData, ULONG InstanceLength,
		LPVOID PropertyData, ULONG DataLength) override;
	STDMETHODIMP Get(
		REFGUID PropSet, ULONG Id, LPVOID InstanceData, ULONG InstanceLength,
		LPVOID PropertyData, ULONG DataLength, ULONG *pBytesReturned) override;
	STDMETHODIMP QuerySupported(REFGUID PropSet, ULONG Id, ULONG *pTypeSupport) override;

private:
	LONG m_CorrectTS;
	CCritSec m_csRateLock;
	AM_SimpleRateChange m_RateChange;
};
