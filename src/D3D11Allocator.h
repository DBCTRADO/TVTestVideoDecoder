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


#include <d3d11.h>
#include <dxgi.h>
#include <vector>
#include "COMUtil.h"


class CMpeg2DecoderD3D11;

class CD3D11Allocator : public CBaseAllocator
{
public:
	CD3D11Allocator(CMpeg2DecoderD3D11 *pDecoder, HRESULT *phr);
	~CD3D11Allocator();

	int GetTextureWidth() const { return m_TextureWidth; }
	int GetTextureHeight() const { return m_TextureHeight; }
	BOOL IsCommitted() const { return m_bCommitted; }
	BOOL IsDecommitInProgress() const { return m_bDecommitInProgress; }

// CBaseAllocator

	HRESULT Alloc() override;
	void Free() override;

private:
	CMpeg2DecoderD3D11 *m_pDecoder;
	COMPointer<ID3D11Texture2D> m_Texture;
	int m_TextureWidth;
	int m_TextureHeight;
};

MIDL_INTERFACE("C5E90A78-5938-4E4A-BB36-0FF5D6E213B5")
ID3D11MediaSample : public IUnknown
{
	STDMETHOD(GetTexture)(ID3D11Texture2D **ppTexture, UINT *pArraySlice) PURE;
	STDMETHOD(SetTexture)(ID3D11Texture2D *pTexture, UINT ArraySlice) PURE;
	STDMETHOD_(UINT, GetTextureArraySlice)() PURE;
	STDMETHOD(GetVideoDecoderOutputView)(ID3D11VideoDecoderOutputView **ppView) PURE;
	STDMETHOD(SetVideoDecoderOutputView)(ID3D11VideoDecoderOutputView *pView) PURE;
};

class CD3D11MediaSample
	: public CMediaSample
	, public ID3D11MediaSample
{
public:
	CD3D11MediaSample(CD3D11Allocator *pAllocator, HRESULT *phr);
	~CD3D11MediaSample();

// IUnknown

	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
	STDMETHODIMP_(ULONG) AddRef() override;
	STDMETHODIMP_(ULONG) Release() override;

// IMediaSample

	STDMETHODIMP GetPointer(BYTE **ppBuffer) override;

// ID3D11MediaSample

	STDMETHODIMP GetTexture(ID3D11Texture2D **ppTexture, UINT *pArraySlice) override;
	STDMETHODIMP SetTexture(ID3D11Texture2D *pTexture, UINT ArraySlice) override;
	STDMETHODIMP_(UINT) GetTextureArraySlice() override { return m_ArraySlice; }
	STDMETHODIMP GetVideoDecoderOutputView(ID3D11VideoDecoderOutputView **ppView) override;
	STDMETHODIMP SetVideoDecoderOutputView(ID3D11VideoDecoderOutputView *pView) override;

private:
	COMPointer<ID3D11Texture2D> m_Texture;
	UINT m_ArraySlice;
	COMPointer<ID3D11VideoDecoderOutputView> m_VideoDecoderOutputView;
};
