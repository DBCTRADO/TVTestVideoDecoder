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

#include "stdafx.h"
#include "DXVA2Allocator.h"
#include "BaseVideoFilter.h"
#include "Util.h"
#include "MediaTypes.h"


// CDXVA2Allocator

CDXVA2Allocator::CDXVA2Allocator(CBaseVideoFilter *pFilter, HRESULT *phr)
	: CBaseAllocator(L"DXVA2Allocator", nullptr, phr)
	, m_pFilter(pFilter)
	, m_SurfaceWidth(0)
	, m_SurfaceHeight(0)
{
}

CDXVA2Allocator::~CDXVA2Allocator()
{
	Decommit();
}

HRESULT CDXVA2Allocator::Alloc()
{
	CAutoLock Lock(this);

	DBG_TRACE(TEXT("CDXVA2Allocator::Alloc()"));

	if (!m_pFilter->m_pD3DDeviceManager || !m_pFilter->m_hDXVADevice)
		return E_UNEXPECTED;

	HRESULT hr;

	IDirectXVideoDecoderService *pDecoderService;
	hr = m_pFilter->m_pD3DDeviceManager->GetVideoService(
		m_pFilter->m_hDXVADevice, IID_PPV_ARGS(&pDecoderService));
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("IDirect3DDeviceManager9::GetVideoService() failed (%x)"), hr);
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

		const int Width = m_pFilter->GetAlignedWidth(), Height = m_pFilter->GetAlignedHeight();
		const D3DFORMAT SurfaceFormat = m_pFilter->GetDXVA2SurfaceFormat();
		DBG_TRACE(TEXT("Create DXVA2 surfaces : '%c%c%c%c' %d x [%d x %d]"),
				  SurfaceFormat & 0xff, (SurfaceFormat >> 8) & 0xff, (SurfaceFormat >> 16) & 0xff, SurfaceFormat >> 24,
				  m_lCount, Width, Height);

		hr = pDecoderService->CreateSurface(
			Width,
			Height,
			m_lCount - 1,
			SurfaceFormat,
			D3DPOOL_DEFAULT,
			0,
			DXVA2_VideoDecoderRenderTarget,
			&m_SurfaceList[0],
			nullptr);
		if (FAILED(hr)) {
			DBG_ERROR(TEXT("IDirectXVideoDecoderService::CreateSurface() failed (%x)"), hr);
			m_SurfaceList.clear();
		} else {
			IDirect3DDevice9 *pD3DDevice;
			hr = m_SurfaceList.front()->GetDevice(&pD3DDevice);
			if (FAILED(hr))
				pD3DDevice = nullptr;

			for (int i = m_lCount - 1; i >= 0; i--) {
				IDirect3DSurface9 *pSurface = m_SurfaceList[i];

				if (pD3DDevice) {
					pD3DDevice->ColorFill(pSurface, nullptr, D3DCOLOR_XYUV(16, 128, 128));
				}

				CDXVA2MediaSample *pSample = DNew_nothrow CDXVA2MediaSample(this, &hr);

				if (!pSample) {
					hr = E_OUTOFMEMORY;
					break;
				}
				if (FAILED(hr)) {
					delete pSample;
					break;
				}

				pSample->SetSurface(i, pSurface);
				m_lFree.Add(pSample);
			}

			if (pD3DDevice)
				pD3DDevice->Release();

			if (SUCCEEDED(hr)) {
				m_lAllocated = m_lCount;

				m_SurfaceWidth = Width;
				m_SurfaceHeight = Height;

				hr = m_pFilter->OnDXVA2SurfaceCreated(&m_SurfaceList[0], m_lAllocated);
			} else {
				Free();
			}
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
	CAutoLock Lock(this);

	DBG_TRACE(TEXT("CDXVA2Allocator::Free()"));

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

	m_SurfaceWidth = 0;
	m_SurfaceHeight = 0;
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

	if (riid == __uuidof(IDXVA2MediaSample)) {
		return GetInterface(static_cast<IDXVA2MediaSample*>(this), ppv);
	}
	if (riid == IID_IMFGetService) {
		return GetInterface(static_cast<IMFGetService*>(this), ppv);
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

STDMETHODIMP CDXVA2MediaSample::SetSurface(DWORD SurfaceID, IDirect3DSurface9 *pSurface)
{
	SafeRelease(m_pSurface);

	m_pSurface = pSurface;
	if (m_pSurface)
		m_pSurface->AddRef();
	m_SurfaceID = SurfaceID;

	return S_OK;
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
