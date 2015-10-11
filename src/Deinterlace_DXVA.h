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

#pragma once


#include "Deinterlace.h"
#include <d3d9.h>
#include <dxva2api.h>
#include <vector>
#include <deque>


class CDeinterlacer_DXVA : public CDeinterlacer
{
public:
	CDeinterlacer_DXVA();
	~CDeinterlacer_DXVA();
	bool Open(int Width, int Height);
	void Close();
	bool IsOpenFailed() const { return m_fOpenFailed; }

// CDeinterlacer

	bool Initialize() override;
	void Finalize() override;
	FrameStatus GetFrame(CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer, bool fTopFiledFirst) override;

private:
	struct SampleInfo
	{
		IDirect3DSurface9 *pSurface;
		REFERENCE_TIME StartTime;
		REFERENCE_TIME EndTime;
		bool fTopFieldFirst;
	};

	HMODULE m_hD3D9Lib;
	HMODULE m_hDXVA2Lib;
	IDirect3D9 *m_pDirect3D9;
	IDirect3DDevice9 *m_pDirect3DDevice9;
	IDirect3DDeviceManager9 *m_pDeviceManager;
	UINT m_ResetToken;
	HANDLE m_hDevice;
	IDirectXVideoProcessorService *m_pVideoProcessorService;
	IDirectXVideoProcessor *m_pVideoProcessor;
	DXVA2_VideoDesc m_VideoDesc;
	UINT m_ForwardRefSamples;
	UINT m_BackwardRefSamples;
	std::vector<IDirect3DSurface9*> m_Surfaces;
	std::deque<IDirect3DSurface9*> m_RefSurfaces;
	std::deque<SampleInfo> m_RefSamples;
	DXVA2_ValueRange m_ProcAmpBrightness;
	DXVA2_ValueRange m_ProcAmpContrast;
	DXVA2_ValueRange m_ProcAmpHue;
	DXVA2_ValueRange m_ProcAmpSaturation;
	DXVA2_FilterValues m_NoiseFilterLuma;
	DXVA2_FilterValues m_NoiseFilterChroma;
	DXVA2_FilterValues m_DetailFilterLuma;
	DXVA2_FilterValues m_DetailFilterChroma;
	int m_Width;
	int m_Height;
	LONGLONG m_FrameNumber;
	bool m_fOpenFailed;

	bool OpenProcessor(int Width, int Height, const DXVA2_VideoDesc &desc);
	DXVA2_Fixed32 GetFilterDefaultValue(UINT FilterSetting);
	IDirect3DSurface9 *GetFreeSurface();

	friend class CTVTestVideoDecoder;
};
