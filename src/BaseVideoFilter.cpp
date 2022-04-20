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

#include "stdafx.h"
#include <memory.h>
#include <utility>
#include <initguid.h>
#include "BaseVideoFilter.h"
#include "PixelFormatConvert.h"
#include "DXVA2Allocator.h"
#include "D3D11Allocator.h"
#include "Util.h"
#include "MediaTypes.h"

#pragma comment(lib, "mfuuid.lib")


CBaseVideoFilter::CBaseVideoFilter(PCWSTR pName, LPUNKNOWN lpunk, HRESULT *phr, REFCLSID clsid, long cBuffers)
	: CTransformFilter(pName, lpunk, clsid)
	, m_cBuffers(cBuffers)

	, m_hDXVADevice(nullptr)
	, m_fDXVAConnect(false)
	, m_fDXVAOutput(false)
	, m_fAttachMediaType(false)
{
	*phr = S_OK;

	/*
	m_pInput = DNew_nothrow CBaseVideoInputPin(L"CBaseVideoInputPin", this, phr, L"VideoInput");
	if (!m_pInput) {
		*phr = E_OUTOFMEMORY;
	}
	if (FAILED(*phr)) {
		return;
	}
	*/

	m_pOutput = DNew_nothrow CBaseVideoOutputPin(L"CBaseVideoOutputPin", this, phr, L"VideoOutput");
	if (!m_pOutput) {
		*phr = E_OUTOFMEMORY;
	}
	if (FAILED(*phr))  {
		// SafeDelete(m_pInput);
		return;
	}
}

CBaseVideoFilter::~CBaseVideoFilter()
{
	FreeAllocator();
	CloseDXVA2DeviceManager();
}

int CBaseVideoFilter::GetPinCount()
{
	return 2;
}

CBasePin *CBaseVideoFilter::GetPin(int n)
{
	switch (n) {
	case 0:
		return m_pInput;
	case 1:
		return m_pOutput;
	}
	return nullptr;
}

HRESULT CBaseVideoFilter::Receive(IMediaSample *pIn)
{
	CAutoLock cAutoLock(&m_csReceive);

	const AM_SAMPLE2_PROPERTIES *pProps = m_pInput->SampleProps();
	if (pProps->dwStreamId != AM_STREAM_MEDIA) {
		return m_pOutput->Deliver(pIn);
	}

	HRESULT hr;

	AM_MEDIA_TYPE *pmt;
	hr = pIn->GetMediaType(&pmt);
	if (SUCCEEDED(hr) && pmt) {
		CMediaType mt(*pmt);
		DeleteMediaType(pmt);
		if (mt != m_pInput->CurrentMediaType()) {
			m_pInput->SetMediaType(&mt);
		}
	}

	hr = Transform(pIn);
	if (FAILED(hr)) {
		return hr;
	}

	return S_OK;
}

HRESULT CBaseVideoFilter::GetDeliveryBuffer(
	IMediaSample **ppOut, int Width, int Height, int AspectX, int AspectY,
	REFERENCE_TIME AvgTimePerFrame, bool fInterlaced)
{
	CheckPointer(ppOut, E_POINTER);

	*ppOut = nullptr;

	HRESULT hr;

	hr = ReconnectOutput(Width, Height, AspectX, AspectY, true, false, AvgTimePerFrame, fInterlaced);
	if (FAILED(hr)) {
		return hr;
	}

	return GetDeliveryBuffer(ppOut);
}

HRESULT CBaseVideoFilter::GetDeliveryBuffer(IMediaSample **ppOut)
{
	CheckPointer(ppOut, E_POINTER);

	*ppOut = nullptr;

	HRESULT hr;
	IMediaSample *pOut;

	hr = m_pOutput->GetDeliveryBuffer(&pOut, nullptr, nullptr, 0);
	if (FAILED(hr)) {
		return hr;
	}

	AM_MEDIA_TYPE *pmt;
	hr = pOut->GetMediaType(&pmt);
	if (SUCCEEDED(hr) && pmt) {
		CMediaType mt = *pmt;
		SetupMediaType(&mt);
		m_pOutput->SetMediaType(&mt);
		DeleteMediaType(pmt);
	}

	pOut->SetDiscontinuity(FALSE);
	pOut->SetSyncPoint(TRUE);

	if (GetConnectedFilterCLSID(m_pOutput) == CLSID_OverlayMixer) {
		pOut->SetDiscontinuity(TRUE);
	}

	*ppOut = pOut;

	return S_OK;
}

void CBaseVideoFilter::SetupMediaType(CMediaType *pmt)
{
	if (IsVideoInfo2(pmt)) {
		DWORD ControlFlags = GetVideoInfoControlFlags();

		if (ControlFlags != 0) {
			VIDEOINFOHEADER2 *pvih2 = (VIDEOINFOHEADER2*)pmt->Format();

			pvih2->dwControlFlags = ControlFlags;
		}
	}
}

HRESULT CBaseVideoFilter::ReconnectOutput(
	int Width, int Height, int AspectX, int AspectY,
	bool fSendSample, bool fForce,
	REFERENCE_TIME AvgTimePerFrame, bool fInterlaced)
{
	CMediaType &mt = m_pOutput->CurrentMediaType();
	bool fReconnect = fForce;

	const VideoDimensions OrigDim = m_Dimensions;

	if (Width != m_Dimensions.Width || Height != m_Dimensions.Height) {
		m_Dimensions.Width = Width;
		m_Dimensions.Height = Height;
		fReconnect = true;
	}
	if (AspectX != m_Dimensions.AspectX || AspectY != m_Dimensions.AspectY) {
		m_Dimensions.AspectX = AspectX;
		m_Dimensions.AspectY = AspectY;
		fReconnect = true;
	}
	if (m_Dimensions != m_OutDimensions) {
		fReconnect = true;
	}

	m_MediaDimensions.Width = Width;
	m_MediaDimensions.Height = Height;
	m_MediaDimensions.AspectX = AspectX;
	m_MediaDimensions.AspectY = AspectY;

	REFERENCE_TIME OrigAvgTimePerFrame = 0;
	GetAvgTimePerFrame(&mt, &OrigAvgTimePerFrame);
	if (AvgTimePerFrame == 0) {
		AvgTimePerFrame = OrigAvgTimePerFrame;
	} else if (std::abs(OrigAvgTimePerFrame - AvgTimePerFrame) > 10) {
		fReconnect = true;
	}

	if (IsVideoInfo2(&mt)) {
		if (IsMediaTypeInterlaced(&mt) != fInterlaced) {
			fReconnect = true;
		}
	}

	if (!fReconnect && m_fDXVAOutput) {
		const BITMAPINFOHEADER *pbmih = GetBitmapInfoHeader(&mt);
		if (pbmih && pbmih->biCompression != FOURCC_dxva) {
			fReconnect = true;
		}
	}

	if (!fReconnect) {
		return S_FALSE;
	}

	CLSID clsid = GetConnectedFilterCLSID(m_pOutput);
	/*
	if (clsid == CLSID_VideoRenderer) {
		NotifyEvent(EC_ERRORABORT, 0, 0);
		return E_FAIL;
	}
	*/
	BOOL fOverlayMixer = (clsid == CLSID_OverlayMixer);

	DBG_TRACE(TEXT("CBaseVideoFilter::ReconnectOutput()"));
	DBG_TRACE(TEXT("  Size : %dx%d%c => %dx%d%c"),
			  m_OutDimensions.Width, m_OutDimensions.Height,
			  IsMediaTypeInterlaced(&mt) ? TEXT('i') : TEXT('p'),
			  m_Dimensions.Width, m_Dimensions.Height,
			  fInterlaced ? TEXT('i') : TEXT('p'));
	DBG_TRACE(TEXT("    AR : %d:%d => %d:%d"),
			  m_OutDimensions.AspectX, m_OutDimensions.AspectY,
			  m_Dimensions.AspectX, m_Dimensions.AspectY);
	DBG_TRACE(TEXT("  RT/F : %lld => %lld"),
			  OrigAvgTimePerFrame, AvgTimePerFrame);

	VIDEOINFOHEADER *pvih = (VIDEOINFOHEADER*)mt.Format();
	BITMAPINFOHEADER *pbmih;
	if (IsVideoInfo(&mt)) {
		pbmih = &pvih->bmiHeader;
		pbmih->biXPelsPerMeter = m_Dimensions.Width * m_Dimensions.AspectY;
		pbmih->biYPelsPerMeter = m_Dimensions.Height * m_Dimensions.AspectX;
	} else if (IsVideoInfo2(&mt)) {
		VIDEOINFOHEADER2 *pvih2 = (VIDEOINFOHEADER2*)pvih;
		pvih2->dwInterlaceFlags =
			fInterlaced ? (AMINTERLACE_IsInterlaced | AMINTERLACE_DisplayModeBobOrWeave) : 0;
		if (m_Dimensions.AspectX && m_Dimensions.AspectY) {
			pvih2->dwPictAspectRatioX = m_Dimensions.AspectX;
			pvih2->dwPictAspectRatioY = m_Dimensions.AspectY;
		} else {
			int ARX = m_Dimensions.Width, ARY = m_Dimensions.Height;
			ReduceFraction(&ARX, &ARY);
			pvih2->dwPictAspectRatioX = ARX;
			pvih2->dwPictAspectRatioY = ARY;
		}
		pbmih = &pvih2->bmiHeader;
	} else {
		return E_FAIL;
	}

	RECT rcVideo = {0, 0, m_Dimensions.Width, m_Dimensions.Height};
	pvih->rcSource = pvih->rcTarget = rcVideo;
	pvih->AvgTimePerFrame = AvgTimePerFrame;

	pbmih->biWidth = m_Dimensions.Width;
	pbmih->biHeight = m_Dimensions.Height;
	pbmih->biSizeImage = DIBSIZE(*pbmih);

	if (m_fDXVAOutput) {
		pbmih->biCompression = FOURCC_dxva;
	}

	HRESULT hr;

	hr = m_pOutput->GetConnected()->QueryAccept(&mt);
#ifdef _DEBUG
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("	QueryAccept() failed (%x)"), hr);
	}
#endif

	if (m_fDXVAConnect) {
		hr = m_pOutput->SetMediaType(&mt);
		m_fAttachMediaType = true;

		if (m_DXVA2Allocator
				&& (GetAlignedWidth() > m_DXVA2Allocator->GetSurfaceWidth()
				    || GetAlignedHeight() > m_DXVA2Allocator->GetSurfaceHeight()))
			RecommitAllocator();
	} else {
		int RetryTimeout = 100;

		for (;;) {
			hr = m_pOutput->GetConnected()->ReceiveConnection(m_pOutput, &mt);
			if (SUCCEEDED(hr)) {
				if (fSendSample) {
					IMediaSample *pOut;
					hr = m_pOutput->GetDeliveryBuffer(&pOut, nullptr, nullptr, 0);
					if (SUCCEEDED(hr)) {
						AM_MEDIA_TYPE *pmt;
						hr = pOut->GetMediaType(&pmt);
						if (SUCCEEDED(hr) && pmt) {
							CMediaType mt = *pmt;
							SetupMediaType(&mt);
							m_pOutput->SetMediaType(&mt);
							DeleteMediaType(pmt);
						} else {
							if (fOverlayMixer) {
								long Size = pOut->GetSize();
								if (Size > 0)
									pbmih->biWidth = Size / abs(pbmih->biHeight) * 8 / pbmih->biBitCount;
							}
							m_pOutput->SetMediaType(&mt);
						}
						pOut->Release();
					} else {
						m_Dimensions = OrigDim;
						return hr;
					}
				}
			} else if (hr == VFW_E_BUFFERS_OUTSTANDING && RetryTimeout >= 0) {
				DBG_TRACE(TEXT("	VFW_E_BUFFERS_OUTSTANDING"));
				if (RetryTimeout > 0) {
					::Sleep(10);
				} else {
					m_pOutput->BeginFlush();
					m_pOutput->EndFlush();
				}
				RetryTimeout -= 10;
				continue;
			} else {
				DBG_ERROR(TEXT("	ReceiveConnection() failed (%x)"), hr);
			}

			break;
		}
	}

	m_OutDimensions = m_Dimensions;

	if (!m_fDXVAOutput) {
		NotifyEvent(EC_VIDEO_SIZE_CHANGED, MAKELPARAM(m_Dimensions.Width, m_Dimensions.Height), 0);
	}

	return S_OK;
}

HRESULT CBaseVideoFilter::InitAllocator(IMemAllocator **ppAllocator)
{
	DBG_TRACE(TEXT("CBaseVideoFilter::InitAllocator()"));

	if (m_fDXVAOutput) {
		DBG_TRACE(TEXT("Create DXVA2 Allocator"));
		FreeAllocator();
		HRESULT hr = S_OK;
		CDXVA2Allocator *pDXVA2Allocator = DNew_nothrow CDXVA2Allocator(this, &hr);
		if (!pDXVA2Allocator) {
			return E_OUTOFMEMORY;
		}
		if (FAILED(hr)) {
			delete pDXVA2Allocator;
			return hr;
		}
		m_DXVA2Allocator = pDXVA2Allocator;

		return m_DXVA2Allocator->QueryInterface(IID_PPV_ARGS(ppAllocator));
	}

	return E_NOTIMPL;
}

void CBaseVideoFilter::FreeAllocator()
{
	if (m_DXVA2Allocator) {
		m_DXVA2Allocator->Decommit();
		m_DXVA2Allocator.Release();
	}
}

HRESULT CBaseVideoFilter::RecommitAllocator()
{
	HRESULT hr = S_OK;

	if (m_DXVA2Allocator) {
		DBG_TRACE(TEXT("Recommit DXVA2 Allocator"));
		OnDXVA2AllocatorDecommit();
		m_DXVA2Allocator->Decommit();
		if (m_DXVA2Allocator->IsDecommitInProgress()) {
			m_pOutput->GetConnected()->BeginFlush();
			m_pOutput->GetConnected()->EndFlush();
		}
		if (m_D3D9DeviceManager && m_hDXVADevice) {
			hr = m_DXVA2Allocator->Commit();
		}
	}

	return hr;
}

void CBaseVideoFilter::CloseDXVA2DeviceManager()
{
	CloseDXVA2DeviceHandle();
	m_D3D9DeviceManager.Release();
}

void CBaseVideoFilter::CloseDXVA2DeviceHandle()
{
	if (m_hDXVADevice != nullptr) {
		DBG_TRACE(TEXT("Close DXVA2 device handle"));
		_ASSERT(m_D3D9DeviceManager);
		m_D3D9DeviceManager->CloseDeviceHandle(m_hDXVADevice);
		m_hDXVADevice = nullptr;
	}
}

HRESULT CBaseVideoFilter::ConfigureDXVA2(IPin *pPin)
{
	HRESULT hr;
	IMFGetService *pGetService;

	hr = pPin->QueryInterface(IID_PPV_ARGS(&pGetService));
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("Acquiring IMFGetService failed (%x)"), hr);
		return hr;
	}

	IDirect3DDeviceManager9 *pDeviceManager;
	hr = pGetService->GetService(MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pDeviceManager));
	if (SUCCEEDED(hr)) {
		HANDLE hDevice;
		m_D3D9DeviceManager.Attach(pDeviceManager);

		hr = m_D3D9DeviceManager->OpenDeviceHandle(&hDevice);
		if (SUCCEEDED(hr)) {
			m_hDXVADevice = hDevice;

			hr = OnDXVA2DeviceHandleOpened();
			if (SUCCEEDED(hr)) {
				IDirectXVideoMemoryConfiguration *pVMemConfig;
				hr = pGetService->GetService(
					MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pVMemConfig));
				if (SUCCEEDED(hr)) {
					for (DWORD i = 0; ; i++) {
						DXVA2_SurfaceType SurfaceType;

						hr = pVMemConfig->GetAvailableSurfaceTypeByIndex(i, &SurfaceType);
						if (FAILED(hr))
							break;
						if (SurfaceType == DXVA2_SurfaceType_DecoderRenderTarget) {
							hr = pVMemConfig->SetSurfaceType(DXVA2_SurfaceType_DecoderRenderTarget);
#ifdef _DEBUG
							if (FAILED(hr)) {
								DBG_ERROR(TEXT("IDirectXVideoMemoryConfiguration::SetSurfaceType() failed (%x)"), hr);
							}
#endif
							break;
						}
					}

					pVMemConfig->Release();
				}
#ifdef _DEBUG
				else {
					DBG_ERROR(TEXT("Acquiring IDirectXVideoMemoryConfiguration failed (%x)"), hr);
				}
#endif
			}
		}
#ifdef _DEBUG
		else {
			DBG_ERROR(TEXT("OpenDeviceHandle() failed (%x)"), hr);
		}
#endif

		pGetService->Release();
	}
#ifdef _DEBUG
	else {
		DBG_ERROR(TEXT("Acquiring IDirect3DDeviceManager9 failed (%x)"), hr);
	}
#endif

	if (SUCCEEDED(hr)) {
		hr = OnDXVA2Connect(pPin);
	}

	if (SUCCEEDED(hr)) {
		m_fDXVAOutput = true;
	} else {
		CloseDXVA2DeviceManager();
	}

	return hr;
}

void CBaseVideoFilter::GetOutputSize(VideoDimensions *pDimensions) const
{
	if (m_MediaDimensions.Width && m_MediaDimensions.Height) {
		pDimensions->Width = m_MediaDimensions.Width;
		pDimensions->Height = m_MediaDimensions.Height;
	}
	if (m_MediaDimensions.AspectX && m_MediaDimensions.AspectY) {
		pDimensions->AspectX = m_MediaDimensions.AspectX;
		pDimensions->AspectY = m_MediaDimensions.AspectY;
	}
}

bool CBaseVideoFilter::GetDimensions(const AM_MEDIA_TYPE &mt, VideoDimensions *pDimensions)
{
	int Width, Height, AspectX, AspectY;

	*pDimensions = VideoDimensions();

	if (IsVideoInfo(&mt)) {
		const VIDEOINFOHEADER *pvih = (const VIDEOINFOHEADER*)mt.pbFormat;
		Width = pvih->bmiHeader.biWidth;
		Height = abs(pvih->bmiHeader.biHeight);
		AspectX = Width * pvih->bmiHeader.biYPelsPerMeter;
		AspectY = Height * pvih->bmiHeader.biXPelsPerMeter;
	} else if (IsVideoInfo2(&mt)) {
		const VIDEOINFOHEADER2 *pvih2 = (const VIDEOINFOHEADER2*)mt.pbFormat;
		Width = pvih2->bmiHeader.biWidth;
		Height = abs(pvih2->bmiHeader.biHeight);
		AspectX = pvih2->dwPictAspectRatioX;
		AspectY = pvih2->dwPictAspectRatioY;
	} else {
		return false;
	}

	if (!AspectX || !AspectY) {
		if (IsMpeg1VideoInfo(&mt)) {
			const MPEG1VIDEOINFO *pmpg1vi = (const MPEG1VIDEOINFO*)mt.pbFormat;
			const BYTE *p = pmpg1vi->bSequenceHeader;

			if (pmpg1vi->cbSequenceHeader >= 8 && *(const DWORD*)p == 0xb3010000) {
				Width = (p[4] << 4) | (p[5] >> 4);
				Height = ((p[5] & 0xf) << 8) | p[6];
				static const int AspectRatio[] = {
					10000,  6735,  7031,  7615,  8055,  8437,  8935,
					 9157,  9815, 10255, 10695, 10950, 11575, 12015
				};
				int i = p[7] >> 4;
				if (i >= 1 && i <= 14) {
					AspectX = ::MulDiv(Width, 10000, AspectRatio[i - 1]);
					AspectY = Height;
				}
			}
		} else if (IsMpeg2VideoInfo(&mt)) {
			const MPEG2VIDEOINFO *pmpg2vi = (const MPEG2VIDEOINFO*)mt.pbFormat;
			const BYTE *p = (const BYTE*)pmpg2vi->dwSequenceHeader;

			if (pmpg2vi->cbSequenceHeader >= 8 && *(const DWORD*)p == 0xb3010000) {
				Width = (p[4] << 4) | (p[5] >> 4);
				Height = ((p[5] & 0xf) << 8) | p[6];
				static const struct {
					int x, y;
				} AspectRatio[] = {{1, 1}, {4, 3}, {16, 9}, {221, 100}};
				int i = p[7] >> 4;
				if (i >= 1 && i <= 4) {
					i--;
					AspectX = AspectRatio[i].x;
					AspectY = AspectRatio[i].y;
				}
			}
		}
	}

	if (!AspectX || !AspectY) {
		AspectX = Width;
		AspectY = Height;
	}

	ReduceFraction(&AspectX, &AspectY);

	pDimensions->Width = Width;
	pDimensions->Height = Height;
	pDimensions->AspectX = AspectX;
	pDimensions->AspectY = AspectY;

	return true;
}

HRESULT CBaseVideoFilter::CopySampleBuffer(BYTE *pOut, const CFrameBuffer *pSrc, bool fInterlaced)
{
	BITMAPINFOHEADER bmihOut;
	if (!GetBitmapInfoHeader(&m_pOutput->CurrentMediaType(), &bmihOut)) {
		return E_UNEXPECTED;
	}

	const GUID &subtype = pSrc->m_Subtype;
	const int Width = pSrc->m_Width;
	const int Height = pSrc->m_Height;

	if (subtype == MEDIASUBTYPE_I420 || subtype == MEDIASUBTYPE_IYUV || subtype == MEDIASUBTYPE_YV12) {
		const uint8_t *pInY = pSrc->m_Buffer[0];
		const uint8_t *pInU = pSrc->m_Buffer[1];
		const uint8_t *pInV = pSrc->m_Buffer[2];

		if (subtype == MEDIASUBTYPE_YV12)
			std::swap(pInU, pInV);

		uint8_t *pOutU = pOut + bmihOut.biWidth * Height;
		uint8_t *pOutV = pOutU + ((bmihOut.biWidth * Height) >> 2);

		if (bmihOut.biCompression == FOURCC_I420
				|| bmihOut.biCompression == FOURCC_IYUV
				|| bmihOut.biCompression == FOURCC_YV12) {
			if (bmihOut.biCompression == FOURCC_YV12)
				std::swap(pOutU, pOutV);
			PixelCopyI420ToI420(
				Width, Height,
				pOut, pOutU, pOutV, bmihOut.biWidth, bmihOut.biWidth >> 1,
				pInY, pInU, pInV, pSrc->m_PitchY, pSrc->m_PitchC);
		} else if (bmihOut.biCompression == FOURCC_NV12) {
			PixelCopyI420ToNV12(
				Width, Height,
				pOut, pOutU, bmihOut.biWidth,
				pInY, pInU, pInV, pSrc->m_PitchY, pSrc->m_PitchC);
		} else {
			return VFW_E_TYPE_NOT_ACCEPTED;
		}
	} else if (subtype == MEDIASUBTYPE_NV12) {
		const uint8_t *pInY = pSrc->m_Buffer[0];
		const uint8_t *pInUV = pSrc->m_Buffer[1];
		uint8_t *pOutU = pOut + bmihOut.biWidth * Height;
		uint8_t *pOutV = pOutU + ((bmihOut.biWidth * Height) >> 2);

		if (bmihOut.biCompression == FOURCC_NV12) {
			PixelCopyNV12ToNV12(
				Width, Height,
				pOut, pOutU, bmihOut.biWidth,
				pInY, pInUV, pSrc->m_PitchY);
		} else if (bmihOut.biCompression == FOURCC_I420
				|| bmihOut.biCompression == FOURCC_IYUV
				|| bmihOut.biCompression == FOURCC_YV12) {
			if (bmihOut.biCompression == FOURCC_YV12)
				std::swap(pOutU, pOutV);
			PixelCopyNV12ToI420(
				Width, Height,
				pOut, pOutU, pOutV, bmihOut.biWidth, bmihOut.biWidth >> 1,
				pInY, pInUV, pSrc->m_PitchY);
		} else {
			return VFW_E_TYPE_NOT_ACCEPTED;
		}
	} else {
		return VFW_E_TYPE_NOT_ACCEPTED;
	}

	return S_OK;
}

HRESULT CBaseVideoFilter::CheckInputType(const CMediaType *mtIn)
{
	return S_OK;
}

HRESULT CBaseVideoFilter::CheckOutputType(const CMediaType *mtOut)
{
	return S_OK;
}

HRESULT CBaseVideoFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut)
{
	return S_OK;
}

HRESULT CBaseVideoFilter::DecideBufferSize(IMemAllocator *pAllocator, ALLOCATOR_PROPERTIES *pProperties)
{
	DBG_TRACE(TEXT("DecideBufferSize()"));

	if (!m_pInput->IsConnected()) {
		return E_UNEXPECTED;
	}

	BITMAPINFOHEADER bmih;
	GetBitmapInfoHeader(&m_pOutput->CurrentMediaType(), &bmih);

	pProperties->cBuffers = m_cBuffers;
	pProperties->cbBuffer = bmih.biSizeImage;
	pProperties->cbAlign  = 1;
	pProperties->cbPrefix = 0;

	HRESULT hr;
	ALLOCATOR_PROPERTIES Actual;
	hr = pAllocator->SetProperties(pProperties, &Actual);
	if (FAILED(hr)) {
		return hr;
	}
	if (pProperties->cBuffers > Actual.cBuffers || pProperties->cbBuffer > Actual.cbBuffer) {
		return E_FAIL;
	}

	return S_OK;
}

HRESULT CBaseVideoFilter::GetMediaType(int iPosition, CMediaType *pmt)
{
	DBG_TRACE(TEXT("GetMediaType() : %d"), iPosition);

	if (!m_pInput->IsConnected()) {
		return E_UNEXPECTED;
	}

	if (iPosition < 0) {
		return E_INVALIDARG;
	}

	OutputFormatList FormatList;

	if (m_pInput->CurrentMediaType().formattype == FORMAT_VideoInfo2) {
		iPosition *= 2;
	}
	GetOutputFormatList(&FormatList);
	if (iPosition >= FormatList.FormatCount * 2) {
		return VFW_S_NO_MORE_ITEMS;
	}

	const OutputFormatInfo *pFormatInfo = &FormatList.pFormats[iPosition / 2];

	pmt->majortype = MEDIATYPE_Video;
	pmt->subtype = *pFormatInfo->subtype;

	VideoDimensions Dim = m_InDimensions;
	//GetOutputSize(&Dim);

	BITMAPINFOHEADER *pbmih;

	if (iPosition & 1) {
		VIDEOINFOHEADER *pvih = (VIDEOINFOHEADER*)pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER));
		if (!pvih) {
			return E_OUTOFMEMORY;
		}
		pmt->formattype = FORMAT_VideoInfo;
		ZeroMemory(pvih, sizeof(VIDEOINFOHEADER));
		pbmih = &pvih->bmiHeader;
		pbmih->biXPelsPerMeter = Dim.Width * Dim.AspectY;
		pbmih->biYPelsPerMeter = Dim.Height * Dim.AspectX;
	} else {
		VIDEOINFOHEADER2 *pvih2 = (VIDEOINFOHEADER2*)pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER2));
		if (!pvih2) {
			return E_OUTOFMEMORY;
		}
		pmt->formattype = FORMAT_VideoInfo2;
		ZeroMemory(pvih2, sizeof(VIDEOINFOHEADER2));
		if (IsVideoInterlaced()) {
			pvih2->dwInterlaceFlags = AMINTERLACE_IsInterlaced | AMINTERLACE_DisplayModeBobOrWeave;
		}
		if (!Dim.AspectX || !Dim.AspectY) {
			Dim.AspectX = Dim.Width;
			Dim.AspectY = Dim.Height;
			ReduceFraction(&Dim.AspectX, &Dim.AspectY);
		}
		pvih2->dwPictAspectRatioX = Dim.AspectX;
		pvih2->dwPictAspectRatioY = Dim.AspectY;
		pbmih = &pvih2->bmiHeader;
	}

	pbmih->biSize        = sizeof(BITMAPINFOHEADER);
	pbmih->biWidth       = Dim.Width;
	pbmih->biHeight      = Dim.Height;
	pbmih->biPlanes      = pFormatInfo->Planes;
	pbmih->biBitCount    = pFormatInfo->BitCount;
	pbmih->biCompression = pFormatInfo->Compression;
	pbmih->biSizeImage   = DIBSIZE(*pbmih);

	pmt->SetSampleSize(pbmih->biSizeImage);

	const CMediaType &mtIn = m_pInput->CurrentMediaType();
	const VIDEOINFOHEADER *pvihIn = (const VIDEOINFOHEADER*)mtIn.Format();
	VIDEOINFOHEADER *pvih = (VIDEOINFOHEADER*)pmt->Format();

	if (pvihIn && pvihIn->rcSource.right != 0 && pvihIn->rcSource.bottom != 0) {
		pvih->rcSource = pvihIn->rcSource;
		pvih->rcTarget = pvihIn->rcTarget;
	} else {
		pvih->rcSource.right = pvih->rcTarget.right = m_InDimensions.Width;
		pvih->rcSource.bottom = pvih->rcTarget.bottom = m_InDimensions.Height;
	}

	if (pvihIn) {
		pvih->dwBitRate = pvihIn->dwBitRate;
		pvih->dwBitErrorRate = pvihIn->dwBitErrorRate;
		pvih->AvgTimePerFrame = pvihIn->AvgTimePerFrame;
	}

	return S_OK;
}

HRESULT CBaseVideoFilter::SetMediaType(PIN_DIRECTION dir, const CMediaType *pmt)
{
	if (dir == PINDIR_INPUT) {
		GetDimensions(*pmt, &m_Dimensions);
		m_InDimensions = m_Dimensions;
		//GetOutputSize(&m_Dimensions);
		DBG_TRACE(TEXT("SetMediaType() : Input %d x %d (%d:%d)"),
				  m_Dimensions.Width, m_Dimensions.Height, m_Dimensions.AspectX, m_Dimensions.AspectY);
	} else if (dir == PINDIR_OUTPUT) {
		VideoDimensions Dim;
		GetDimensions(*pmt, &Dim);
		DBG_TRACE(TEXT("SetMediaType() : Output %d x %d (%d:%d)"),
				  Dim.Width, Dim.Height, Dim.AspectX, Dim.AspectY);
		if (m_Dimensions == Dim) {
			m_OutDimensions = Dim;
		}
	}

	return __super::SetMediaType(dir, pmt);
}

HRESULT CBaseVideoFilter::BreakConnect(PIN_DIRECTION dir)
{
	if (dir == PINDIR_OUTPUT) {
		m_OutDimensions = m_InDimensions;
	}

	return __super::BreakConnect(dir);
}

HRESULT CBaseVideoFilter::CompleteConnect(PIN_DIRECTION direction, IPin *pReceivePin)
{
	if (direction != PINDIR_OUTPUT) {
		return S_OK;
	}

	CloseDXVA2DeviceManager();
	m_fDXVAOutput = false;

	if (m_fDXVAConnect) {
		ConfigureDXVA2(pReceivePin);
	}

	return S_OK;
}

int CBaseVideoFilter::GetAlignedWidth() const
{
	return GetDXVASurfaceSize(SIZE{m_Dimensions.Width, m_Dimensions.Height}, true).cx;
}

int CBaseVideoFilter::GetAlignedHeight() const
{
	return GetDXVASurfaceSize(SIZE{m_Dimensions.Width, m_Dimensions.Height}, true).cy;
}


// CBaseInputAllocator

CBaseVideoInputAllocator::CBaseVideoInputAllocator(HRESULT* phr)
	: CMemAllocator(L"BaseVideoInputAllocator", nullptr, phr)
{
	if (phr) {
		*phr = S_OK;
	}
}

void CBaseVideoInputAllocator::SetMediaType(const CMediaType &mt)
{
	m_mt = mt;
}

STDMETHODIMP CBaseVideoInputAllocator::GetBuffer(IMediaSample **ppBuffer, REFERENCE_TIME *pStartTime, REFERENCE_TIME *pEndTime, DWORD dwFlags)
{
	if (!m_bCommitted) {
		return VFW_E_NOT_COMMITTED;
	}

	HRESULT hr = __super::GetBuffer(ppBuffer, pStartTime, pEndTime, dwFlags);

	if (SUCCEEDED(hr) && m_mt.majortype != GUID_NULL) {
		(*ppBuffer)->SetMediaType(&m_mt);
		m_mt.majortype = GUID_NULL;
	}

	return hr;
}


// CBaseVideoInputPin

CBaseVideoInputPin::CBaseVideoInputPin(PCWSTR pObjectName, CBaseVideoFilter *pFilter, HRESULT *phr, PCWSTR pName)
	: CTransformInputPin(pObjectName, pFilter, phr, pName)
	, m_pAllocator(nullptr)
{
}

CBaseVideoInputPin::~CBaseVideoInputPin()
{
	SafeRelease(m_pAllocator);
}

STDMETHODIMP CBaseVideoInputPin::GetAllocator(IMemAllocator **ppAllocator)
{
	CheckPointer(ppAllocator, E_POINTER);

	*ppAllocator = nullptr;

	if (m_pAllocator == nullptr) {
		HRESULT hr = S_OK;
		m_pAllocator = DNew_nothrow CBaseVideoInputAllocator(&hr);
		if (!m_pAllocator) {
			return E_OUTOFMEMORY;
		}
		if (FAILED(hr)) {
			SafeDelete(m_pAllocator);
			return hr;
		}
		m_pAllocator->AddRef();
	}

	*ppAllocator = m_pAllocator;
	(*ppAllocator)->AddRef();

	return S_OK;
}

STDMETHODIMP CBaseVideoInputPin::ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt)
{
	CAutoLock cObjectLock(m_pLock);

	if (m_Connected) {
		CMediaType mt(*pmt);

		if (FAILED(CheckMediaType(&mt))) {
			return VFW_E_TYPE_NOT_ACCEPTED;
		}

		IMemAllocator *pMemAllocator;
		HRESULT hr = GetAllocator(&pMemAllocator);
		if (SUCCEEDED(hr)) {
			hr = pMemAllocator->Decommit();
			if (SUCCEEDED(hr)) {
				ALLOCATOR_PROPERTIES props, actual;

				hr = pMemAllocator->GetProperties(&props);

				BITMAPINFOHEADER bih;
				if (GetBitmapInfoHeader(pmt, &bih) && bih.biSizeImage) {
					props.cbBuffer = bih.biSizeImage;
				}

				hr = pMemAllocator->SetProperties(&props, &actual);
				if (SUCCEEDED(hr)) {
					if (props.cbBuffer != actual.cbBuffer) {
						hr = E_FAIL;
					} else {
						hr = pMemAllocator->Commit();
					}
				}
			}

			pMemAllocator->Release();
		}

		if (FAILED(hr)) {
			return hr;
		}

		if (m_pAllocator) {
			m_pAllocator->SetMediaType(mt);
		}

		hr = SetMediaType(&mt);
		if (FAILED(hr)) {
			return VFW_E_TYPE_NOT_ACCEPTED;
		}

		return S_OK;
	}

	return __super::ReceiveConnection(pConnector, pmt);
}


// CBaseVideoOutputPin

CBaseVideoOutputPin::CBaseVideoOutputPin(PCWSTR pObjectName, CBaseVideoFilter *pFilter, HRESULT *phr, PCWSTR pName)
	: CTransformOutputPin(pObjectName, pFilter, phr, pName)
	, m_pFilter(pFilter)
{
}

HRESULT CBaseVideoOutputPin::CheckMediaType(const CMediaType *mtOut)
{
	if (IsConnected()) {
		HRESULT hr = m_pFilter->CheckOutputType(mtOut);
		if (FAILED(hr)) {
			return hr;
		}
	}

	return __super::CheckMediaType(mtOut);
}

HRESULT CBaseVideoOutputPin::InitAllocator(IMemAllocator **ppAllocator)
{
	CheckPointer(ppAllocator, E_POINTER);

	HRESULT hr;

	hr = m_pFilter->InitAllocator(ppAllocator);

	if (hr == E_NOTIMPL) {
		hr = __super::InitAllocator(ppAllocator);
	}

	return hr;
}
