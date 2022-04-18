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
#include "D3D11Allocator.h"
#include "Mpeg2DecoderD3D11.h"
#include "Util.h"
#include "MediaTypes.h"


// CD3D11Allocator

CD3D11Allocator::CD3D11Allocator(CMpeg2DecoderD3D11 *pDecoder, HRESULT *phr)
	: CBaseAllocator(L"D3D11Allocator", nullptr, phr)
	, m_pDecoder(pDecoder)
	, m_pTexture(nullptr)
	, m_TextureWidth(0)
	, m_TextureHeight(0)
{
}

CD3D11Allocator::~CD3D11Allocator()
{
	Decommit();
}

HRESULT CD3D11Allocator::Alloc()
{
	CAutoLock Lock(this);

	DBG_TRACE(TEXT("CD3D11Allocator::Alloc()"));

	ID3D11Device *pDevice = m_pDecoder->m_pDevice;

	if (!pDevice)
		return E_UNEXPECTED;

	HRESULT hr = CBaseAllocator::Alloc();

	/*
	if (hr == S_FALSE) {
		return S_OK;
	}
	*/

	if (SUCCEEDED(hr)) {
		Free();

		SIZE TextureSize;
		m_pDecoder->GetOutputTextureSize(&TextureSize);

		D3D11_TEXTURE2D_DESC TextureDesc = {};

		TextureDesc.Width = TextureSize.cx;
		TextureDesc.Height = TextureSize.cy;
		TextureDesc.MipLevels = 1;
		TextureDesc.ArraySize = m_lCount;
		TextureDesc.Format = m_pDecoder->GetTextureFormat();
		TextureDesc.SampleDesc.Count = 1;
		TextureDesc.BindFlags = D3D11_BIND_DECODER;

		DBG_TRACE(
			TEXT("Create D3D11 textures : %d %d x [%d x %d]"),
			TextureDesc.Format, m_lCount, TextureDesc.Width, TextureDesc.Height);

		uint8_t *pInitialData = nullptr;
		std::vector<D3D11_SUBRESOURCE_DATA> InitialData;
		if (TextureDesc.Format == DXGI_FORMAT_NV12) {
			const UINT PlaneSize = TextureSize.cx * TextureSize.cy;
			pInitialData = static_cast<uint8_t *>(::HeapAlloc(::GetProcessHeap(), 0, PlaneSize + PlaneSize / 2));
			if (pInitialData) {
				::FillMemory(pInitialData, PlaneSize, 16);
				::FillMemory(pInitialData + PlaneSize, PlaneSize / 2, 128);

				InitialData.resize(TextureDesc.ArraySize);
				for (D3D11_SUBRESOURCE_DATA &Data : InitialData) {
					Data.pSysMem = pInitialData;
					Data.SysMemPitch = TextureSize.cx;
					Data.SysMemSlicePitch = PlaneSize + PlaneSize / 2;
				}
			}
		}

		ID3D11Texture2D *pTexture = nullptr;
		hr = pDevice->CreateTexture2D(&TextureDesc, pInitialData ? &InitialData[0] : nullptr, &pTexture);

		if (pInitialData)
			::HeapFree(::GetProcessHeap(), 0, pInitialData);

		if (FAILED(hr)) {
			DBG_ERROR(TEXT("ID3D11Device::CreateTexture2D() failed (%x)"), hr);
		} else {
			m_pTexture = pTexture;

			ID3D11VideoDevice *pVideoDevice = nullptr;
			hr = pDevice->QueryInterface(IID_PPV_ARGS(&pVideoDevice));
			if (SUCCEEDED(hr)) {
				D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC ViewDesc;
				ViewDesc.DecodeProfile = m_pDecoder->m_ProfileID;
				ViewDesc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;

				for (int i = 0; i < m_lCount; i++) {
					CD3D11MediaSample *pSample = DNew_nothrow CD3D11MediaSample(this, &hr);

					if (!pSample) {
						hr = E_OUTOFMEMORY;
						break;
					}
					if (FAILED(hr)) {
						delete pSample;
						break;
					}

					pSample->SetTexture(pTexture, i);
					m_lFree.Add(pSample);

					ViewDesc.Texture2D.ArraySlice = i;
					ID3D11VideoDecoderOutputView *pView = nullptr;
					hr = pVideoDevice->CreateVideoDecoderOutputView(pTexture, &ViewDesc, &pView);
					if (FAILED(hr)) {
						DBG_ERROR(TEXT("ID3D11VideoDevice::CreateVideoDecoderOutputView() failed (%x)"), hr);
						break;
					}
					pSample->SetVideoDecoderOutputView(pView);
					pView->Release();
				}

				pVideoDevice->Release();
			}

			if (SUCCEEDED(hr)) {
				m_lAllocated = m_lCount;

				m_TextureWidth = TextureDesc.Width;
				m_TextureHeight = TextureDesc.Height;
			} else {
				Free();
			}
		}
	}

	if (SUCCEEDED(hr)) {
		m_bChanged = FALSE;
	}

	return hr;
}

void CD3D11Allocator::Free()
{
	CAutoLock Lock(this);

	DBG_TRACE(TEXT("CD3D11Allocator::Free()"));

	IMediaSample *pSample;
	while ((pSample = m_lFree.RemoveHead()) != nullptr) {
		delete pSample;
	}

	SafeRelease(m_pTexture);

	m_lAllocated = 0;

	m_TextureWidth = 0;
	m_TextureHeight = 0;
}


// CD3D11MediaSample

CD3D11MediaSample::CD3D11MediaSample(CD3D11Allocator *pAllocator, HRESULT *phr)
	: CMediaSample(L"D3D11MediaSample", pAllocator, phr, nullptr, 0)
	, m_pTexture(nullptr)
	, m_ArraySlice(0)
	, m_pVideoDecoderOutputView(nullptr)
{
}

CD3D11MediaSample::~CD3D11MediaSample()
{
	SafeRelease(m_pTexture);
	SafeRelease(m_pVideoDecoderOutputView);
}

STDMETHODIMP CD3D11MediaSample::QueryInterface(REFIID riid, void **ppv)
{
	CheckPointer(ppv, E_POINTER);

	if (riid == __uuidof(ID3D11MediaSample)) {
		return GetInterface(static_cast<ID3D11MediaSample*>(this), ppv);
	}

	return CMediaSample::QueryInterface(riid, ppv);
}

STDMETHODIMP_(ULONG) CD3D11MediaSample::AddRef()
{
    return CMediaSample::AddRef();
}

STDMETHODIMP_(ULONG) CD3D11MediaSample::Release()
{
	return CMediaSample::Release();
}

STDMETHODIMP CD3D11MediaSample::GetPointer(BYTE **ppBuffer)
{
	if (ppBuffer)
		*ppBuffer = nullptr;
	return E_NOTIMPL;
}

STDMETHODIMP CD3D11MediaSample::GetTexture(ID3D11Texture2D **ppTexture, UINT *pArraySlice)
{
	CheckPointer(ppTexture, E_POINTER);
	CheckPointer(pArraySlice, E_POINTER);

	*ppTexture = m_pTexture;
	*pArraySlice = m_ArraySlice;

	if (!m_pTexture)
		return E_FAIL;

	m_pTexture->AddRef();

	return S_OK;
}

STDMETHODIMP CD3D11MediaSample::SetTexture(ID3D11Texture2D *pTexture, UINT ArraySlice)
{
	SafeRelease(m_pTexture);

	m_pTexture = pTexture;
	if (m_pTexture)
		m_pTexture->AddRef();
	m_ArraySlice = ArraySlice;

	return S_OK;
}

STDMETHODIMP CD3D11MediaSample::GetVideoDecoderOutputView(ID3D11VideoDecoderOutputView **ppView)
{
	CheckPointer(ppView, E_POINTER);

	*ppView = m_pVideoDecoderOutputView;

	if (!m_pVideoDecoderOutputView)
		return E_FAIL;

	m_pVideoDecoderOutputView->AddRef();

	return S_OK;
}

STDMETHODIMP CD3D11MediaSample::SetVideoDecoderOutputView(ID3D11VideoDecoderOutputView *pView)
{
	SafeRelease(m_pVideoDecoderOutputView);

	m_pVideoDecoderOutputView = pView;
	if (m_pVideoDecoderOutputView)
		m_pVideoDecoderOutputView->AddRef();

	return S_OK;
}
