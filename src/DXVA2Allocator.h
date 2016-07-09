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


#include <d3d9.h>
#include <dxva2api.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <evr.h>
#include <vector>


class CBaseVideoFilter;

class CDXVA2Allocator : public CBaseAllocator
{
public:
	CDXVA2Allocator(CBaseVideoFilter *pFilter, HRESULT *phr);
	~CDXVA2Allocator();

	int GetSurfaceWidth() const { return m_SurfaceWidth; }
	int GetSurfaceHeight() const { return m_SurfaceHeight; }
	BOOL IsCommitted() const { return m_bCommitted; }
	BOOL IsDecommitInProgress() const { return m_bDecommitInProgress; }

// CBaseAllocator

	HRESULT Alloc() override;
	void Free() override;

private:
	CBaseVideoFilter *m_pFilter;
	std::vector<IDirect3DSurface9*> m_SurfaceList;
	int m_SurfaceWidth;
	int m_SurfaceHeight;
};

MIDL_INTERFACE("65AB01B7-B3B4-4667-9C21-01B4061D88DD")
IDXVA2MediaSample : public IUnknown
{
	STDMETHOD(SetSurface)(DWORD SurfaceID, IDirect3DSurface9 *pSurface) PURE;
	STDMETHOD_(DWORD, GetSurfaceID)() PURE;
};

class CDXVA2MediaSample
	: public CMediaSample
	, public IMFGetService
	, public IDXVA2MediaSample
{
public:
	CDXVA2MediaSample(CDXVA2Allocator *pAllocator, HRESULT *phr);
	~CDXVA2MediaSample();

// IUnknown

	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
	STDMETHODIMP_(ULONG) AddRef() override;
	STDMETHODIMP_(ULONG) Release() override;

// IMediaSample

	STDMETHODIMP GetPointer(BYTE **ppBuffer) override;

// IMFGetService

	STDMETHODIMP GetService(REFGUID guidSerivce, REFIID riid, LPVOID *ppv) override;

// IDXVA2MediaSample

	STDMETHODIMP SetSurface(DWORD SurfaceID, IDirect3DSurface9 *pSurface) override;
	STDMETHODIMP_(DWORD) GetSurfaceID() override { return m_SurfaceID; }

private:
	IDirect3DSurface9 *m_pSurface;
	DWORD m_SurfaceID;
};
