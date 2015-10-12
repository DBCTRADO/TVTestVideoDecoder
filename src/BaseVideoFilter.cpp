/*
 *  TVTest DTV Video Decoder
 *  Copyright (C) 2015 DBCTRADO
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
#include "Util.h"
#include "MediaTypes.h"

#pragma comment(lib, "mfuuid.lib")


CBaseVideoFilter::CBaseVideoFilter(PCWSTR pName, LPUNKNOWN lpunk, HRESULT *phr, REFCLSID clsid, long cBuffers)
	: CTransformFilter(pName, lpunk, clsid)
	, m_cBuffers(cBuffers)

	, m_pD3DDeviceManager(nullptr)
	, m_pDecoderService(nullptr)
	, m_hDXVADevice(nullptr)
	, m_fDXVAConnect(false)
	, m_fDXVAOutput(false)
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
	SafeRelease(m_pDecoderService);
	if (m_hDXVADevice != nullptr) {
		m_pD3DDeviceManager->CloseDeviceHandle(m_hDXVADevice);
		m_hDXVADevice = nullptr;
	}
	SafeRelease(m_pD3DDeviceManager);
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
		SetupMediaType(&mt);
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
	REFERENCE_TIME AvgTimePerFrame, bool fInterlaced, int RealWidth, int RealHeight)
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

	if (RealWidth <= 0)
		RealWidth = m_Dimensions.Width;
	if (RealHeight <= 0)
		RealHeight = m_Dimensions.Height;

	TRACE(TEXT("CBaseVideoFilter::ReconnectOutput()\n"));
	TRACE(TEXT("  Size : %dx%d%c => %dx%d%c(%dx%d)\n"),
		  m_OutDimensions.Width, m_OutDimensions.Height,
		  IsMediaTypeInterlaced(&mt) ? TEXT('i') : TEXT('p'),
		  m_Dimensions.Width, m_Dimensions.Height,
		  fInterlaced ? TEXT('i') : TEXT('p'),
		  RealWidth, RealHeight);
	TRACE(TEXT("    AR : %d:%d => %d:%d\n"),
		  m_OutDimensions.AspectX, m_OutDimensions.AspectY,
		  m_Dimensions.AspectX, m_Dimensions.AspectY);
	TRACE(TEXT("  RT/F : %lld => %lld\n"),
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

	RECT rcVideo = {0, 0, RealWidth, RealHeight};
	pvih->rcSource = pvih->rcTarget = rcVideo;
	pvih->AvgTimePerFrame = AvgTimePerFrame;

	pbmih->biWidth = m_Dimensions.Width;
	pbmih->biHeight = m_Dimensions.Height;
	pbmih->biSizeImage = DIBSIZE(*pbmih);

	HRESULT hr;

	hr = m_pOutput->GetConnected()->QueryAccept(&mt);
#ifdef _DEBUG
	if (FAILED(hr)) {
		TRACE(TEXT("	QueryAccept() failed (%x)\n"), hr);
	}
#endif

	if (m_fDXVAConnect) {
		hr = m_pOutput->SetMediaType(&mt);
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
				TRACE(TEXT("	VFW_E_BUFFERS_OUTSTANDING\n"));
				if (RetryTimeout > 0) {
					::Sleep(10);
				} else {
					m_pOutput->BeginFlush();
					m_pOutput->EndFlush();
				}
				RetryTimeout -= 10;
				continue;
			} else {
				TRACE(TEXT("	ReceiveConnection() failed (%x)\n"), hr);
			}

			break;
		}
	}

	m_OutDimensions = m_Dimensions;

	if (!m_fDXVAConnect) {
		NotifyEvent(EC_VIDEO_SIZE_CHANGED, MAKELPARAM(m_Dimensions.Width, m_Dimensions.Height), 0);
	}

	return S_OK;
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
		if (mt.formattype == FORMAT_MPEGVideo
				&& mt.pbFormat && mt.cbFormat >= sizeof(MPEG1VIDEOINFO)) {
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
		} else if (mt.formattype == FORMAT_MPEG2_VIDEO
				&& mt.pbFormat && mt.cbFormat >= sizeof(MPEG2VIDEOINFO)) {
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
	int RealWidth = 0, RealHeight = 0;
	GetOutputSize(&Dim, &RealWidth, &RealHeight);

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

	if (RealWidth > 0 && pvih->rcSource.right > RealWidth) {
		pvih->rcSource.right = RealWidth;
	}
	if (RealHeight > 0 && pvih->rcSource.bottom > RealHeight) {
		pvih->rcSource.bottom = RealHeight;
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
		int RealWidth = 0, RealHeight = 0;
		GetOutputSize(&m_Dimensions, &RealWidth, &RealHeight);
		ReduceFraction(&m_Dimensions.AspectX, &m_Dimensions.AspectY);
		TRACE(TEXT("SetMediaType() Input %d x %d (%d:%d)\n"),
			  m_Dimensions.Width, m_Dimensions.Height, m_Dimensions.AspectX, m_Dimensions.AspectY);
	} else if (dir == PINDIR_OUTPUT) {
		VideoDimensions Dim;
		GetDimensions(*pmt, &Dim);
		TRACE(TEXT("SetMediaType() Output %d x %d (%d:%d)\n"),
			  Dim.Width, Dim.Height, Dim.AspectX, Dim.AspectY);
		if (m_Dimensions == Dim) {
			m_OutDimensions = Dim;
		}
	}

	return __super::SetMediaType(dir, pmt);
}

HRESULT CBaseVideoFilter::CompleteConnect(PIN_DIRECTION direction, IPin *pReceivePin)
{
	if (direction != PINDIR_OUTPUT) {
		return S_OK;
	}

	if (m_hDXVADevice != nullptr) {
		m_pD3DDeviceManager->CloseDeviceHandle(m_hDXVADevice);
		m_hDXVADevice = nullptr;
	}
	SafeRelease(m_pD3DDeviceManager);
	m_fDXVAOutput = false;

	if (m_fDXVAConnect) {
		HRESULT hr;
		IMFGetService *pGetService;

		hr = pReceivePin->QueryInterface(IID_PPV_ARGS(&pGetService));
		if (SUCCEEDED(hr)) {
			IDirect3DDeviceManager9 *pDeviceManager;
			hr = pGetService->GetService(MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pDeviceManager));
			if (SUCCEEDED(hr)) {
				HANDLE hDevice;
				m_pD3DDeviceManager = pDeviceManager;

				hr = m_pD3DDeviceManager->OpenDeviceHandle(&hDevice);
				if (SUCCEEDED(hr)) {
					m_hDXVADevice = hDevice;

					hr = OnDXVAConnect(pReceivePin);
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
									TRACE(TEXT("IDirectXVideoMemoryConfiguration::SetSurfaceType() %s (%x)\n"),
										  SUCCEEDED(hr) ? TEXT("succeeded") : TEXT("failed"), hr);
									break;
								}
							}

							pVMemConfig->Release();
						}
					}
				}

				pGetService->Release();
			}
		}

		if (SUCCEEDED(hr)) {
			m_fDXVAOutput = true;
		} else {
			if (m_hDXVADevice != nullptr) {
				m_pD3DDeviceManager->CloseDeviceHandle(m_hDXVADevice);
				m_hDXVADevice = nullptr;
			}
			SafeRelease(m_pD3DDeviceManager);
		}
	}

	return S_OK;
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

	if (m_pFilter->m_fDXVAOutput) {
		HRESULT hr = S_OK;
		*ppAllocator = DNew_nothrow CDXVA2Allocator(m_pFilter, &hr);
		if (!*ppAllocator)
			return E_OUTOFMEMORY;
		if (FAILED(hr)) {
			SafeDelete(*ppAllocator);
			return hr;
		}
		(*ppAllocator)->AddRef();
		return S_OK;
	}

	return __super::InitAllocator(ppAllocator);
}


// CDXVA2Allocator

CDXVA2Allocator::CDXVA2Allocator(CBaseVideoFilter *pFilter, HRESULT *phr)
	: CBaseAllocator(L"DXVA2Allocator", nullptr, phr)
	, m_pFilter(pFilter)
{
	m_pFilter->AddRef();
}

CDXVA2Allocator::~CDXVA2Allocator()
{
	SafeRelease(m_pFilter);
}

HRESULT CDXVA2Allocator::Alloc()
{
	CAutoLock Lock(this);

	TRACE(TEXT("CDXVA2Allocator::Alloc()\n"));

	if (!m_pFilter->m_pD3DDeviceManager)
		return E_UNEXPECTED;

	HRESULT hr;

	IDirectXVideoDecoderService *pDecoderService;
	hr = m_pFilter->m_pD3DDeviceManager->GetVideoService(
		m_pFilter->m_hDXVADevice, IID_PPV_ARGS(&pDecoderService));
	if (FAILED(hr)) {
		TRACE(TEXT("IDirect3DDeviceManager9::GetVideoService() failed (%x)\n"), hr);
		return hr;
	}

	hr = CBaseAllocator::Alloc();

	/*
	if (hr == S_FALSE) {
		pDecoderDevice->Release();
		return S_OK;
	}
	*/

	if (SUCCEEDED(hr)) {
		Free();

		m_SurfaceList.resize(m_lCount);

		hr = pDecoderService->CreateSurface(
			(m_pFilter->m_Dimensions.Width + 15) & ~15,
			(m_pFilter->m_Dimensions.Height + 15) & ~15,
			m_lCount - 1,
			D3DFMT_NV12,
			D3DPOOL_DEFAULT,
			0,
			DXVA2_VideoDecoderRenderTarget,
			&m_SurfaceList[0],
			nullptr);
		if (FAILED(hr)) {
			TRACE(TEXT("IDirectXVideoDecoderService::CreateSurface() failed (%x)\n"), hr);
			m_SurfaceList.clear();
		} else {
			IDirect3DDevice9 *pD3DDevice;
			hr = m_SurfaceList.front()->GetDevice(&pD3DDevice);
			if (FAILED(hr))
				pD3DDevice = nullptr;

			for (m_lAllocated = 0; m_lAllocated < m_lCount; m_lAllocated++) {
				IDirect3DSurface9 *pSurface = m_SurfaceList[m_lAllocated];

				if (pD3DDevice) {
					pD3DDevice->ColorFill(pSurface, nullptr, D3DCOLOR_XYUV(16, 128, 128));
				}

				CDXVA2MediaSample *pSample = DNew_nothrow CDXVA2MediaSample(this, &hr);

				if (!pSample) {
					hr = E_OUTOFMEMORY;
					break;
				}
				if (FAILED(hr)) {
					break;
				}

				pSample->SetSurface(m_lAllocated, pSurface);
				m_lFree.Add(pSample);
			}

			if (pD3DDevice)
				pD3DDevice->Release();
		}
	}

	pDecoderService->Release();

	if (SUCCEEDED(hr)) {
		m_bChanged = FALSE;
	}

	return hr;
}

void CDXVA2Allocator::Free()
{
	TRACE(TEXT("CDXVA2Allocator::Free()\n"));

	IMediaSample *pSample;
	while ((pSample = m_lFree.RemoveHead()) != nullptr) {
		delete pSample;
	}

	if (!m_SurfaceList.empty()) {
		for (auto e: m_SurfaceList) {
			SafeRelease(e);
		}
		m_SurfaceList.clear();
	}

	m_lAllocated = 0;
}


// CDXVA2MediaSample

CDXVA2MediaSample::CDXVA2MediaSample(CDXVA2Allocator *pAllocator, HRESULT *phr)
	: CMediaSample(L"DXVA2MediaSample", pAllocator, phr, nullptr, 0)
	, m_pSurface(nullptr)
	, m_SurfaceID(0)
{
}

CDXVA2MediaSample::~CDXVA2MediaSample()
{
	SafeRelease(m_pSurface);
}

STDMETHODIMP CDXVA2MediaSample::QueryInterface(REFIID riid, void **ppv)
{
	CheckPointer(ppv, E_POINTER);

	if (riid == IID_IMFGetService) {
		*ppv = static_cast<IMFGetService*>(this);
		AddRef();
		return S_OK;
	}

	return CMediaSample::QueryInterface(riid, ppv);
}

STDMETHODIMP_(ULONG) CDXVA2MediaSample::AddRef()
{
    return CMediaSample::AddRef();
}

STDMETHODIMP_(ULONG) CDXVA2MediaSample::Release()
{
	return CMediaSample::Release();
}

void CDXVA2MediaSample::SetSurface(DWORD SurfaceID, IDirect3DSurface9 *pSurface)
{
	SafeRelease(m_pSurface);

	m_pSurface = pSurface;
	if (m_pSurface)
		m_pSurface->AddRef();
	m_SurfaceID = SurfaceID;
}

STDMETHODIMP CDXVA2MediaSample::GetPointer(BYTE **ppBuffer)
{
	if (ppBuffer)
		*ppBuffer = nullptr;
	return E_NOTIMPL;
}

STDMETHODIMP CDXVA2MediaSample::GetService(REFGUID guidService, REFIID riid, LPVOID *ppv)
{
	CheckPointer(ppv, E_POINTER);

	*ppv = nullptr;

	if (guidService != MR_BUFFER_SERVICE)
		return MF_E_UNSUPPORTED_SERVICE;
	if (!m_pSurface)
		return E_NOINTERFACE;

	return m_pSurface->QueryInterface(riid, ppv);
}
