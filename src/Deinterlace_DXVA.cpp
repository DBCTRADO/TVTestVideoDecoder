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
#include "Deinterlace_DXVA.h"
#include "PixelFormatConvert.h"
#include "Util.h"
#include <initguid.h>


DEFINE_GUID(DXVA2_VideoProcAMDVectorAdaptiveDevice, 0x3C5323C1, 0x6fb7, 0x44f5, 0x90, 0x81, 0x05, 0x6b, 0xf2, 0xee, 0x44, 0x9d);
DEFINE_GUID(DXVA2_VideoProcAMDMotionAdaptiveDevice, 0x552C0DAD, 0xccbc, 0x420b, 0x83, 0xc8, 0x74, 0x94, 0x3c, 0xf9, 0xf1, 0xa6);
DEFINE_GUID(DXVA2_VideoProcAMDAdaptiveDevice,       0x6E8329FF, 0xb642, 0x418b, 0xbc, 0xf0, 0xbc, 0xb6, 0x59, 0x1e, 0x25, 0x5f);
DEFINE_GUID(DXVA2_VideoProcNVidiaAdaptiveDevice,    0x6CB69578, 0x7617, 0x4637, 0x91, 0xe5, 0x1c, 0x02, 0xdb, 0x81, 0x02, 0x85);
DEFINE_GUID(DXVA2_VideoProcIntelEdgeDevice,         0xBF752EF6, 0x8cc4, 0x457a, 0xbe, 0x1b, 0x08, 0xbd, 0x1c, 0xae, 0xee, 0x9f);
DEFINE_GUID(DXVA2_VideoProcNVidiaUnknownDevice,     0xF9F19DA5, 0x3b09, 0x4b2f, 0x9d, 0x89, 0xc6, 0x47, 0x53, 0xe3, 0xea, 0xab);

struct DeviceInfo {
	LPCTSTR pszName;
	const GUID *guid;
};

struct DeinterlaceTechInfo {
	LPCTSTR pszName;
	unsigned int Flag;
};

static const DeviceInfo DeviceList[] = {
	{TEXT("Bob Device"),                     &DXVA2_VideoProcBobDevice},
	{TEXT("AMD Vector adaptive device"),     &DXVA2_VideoProcAMDVectorAdaptiveDevice},
	{TEXT("AMD Motion adaptive device"),     &DXVA2_VideoProcAMDMotionAdaptiveDevice},
	{TEXT("AMD Adaptive device"),            &DXVA2_VideoProcAMDAdaptiveDevice},
	{TEXT("nVidia Spatial-temporal device"), &DXVA2_VideoProcNVidiaAdaptiveDevice},
	{TEXT("Intel Edge directed device"),     &DXVA2_VideoProcIntelEdgeDevice},
	{TEXT("nVidia Unknown device"),          &DXVA2_VideoProcNVidiaUnknownDevice},
};

static const DeinterlaceTechInfo DeinterlaceTechList[] = {
	{TEXT("Inverse telecine"),           DXVA2_DeinterlaceTech_InverseTelecine},
	{TEXT("Motion vector steered"),      DXVA2_DeinterlaceTech_MotionVectorSteered},
	{TEXT("Pixel adaptive"),             DXVA2_DeinterlaceTech_PixelAdaptive},
	{TEXT("Field adaptive"),             DXVA2_DeinterlaceTech_FieldAdaptive},
	{TEXT("Edge filtering"),             DXVA2_DeinterlaceTech_EdgeFiltering},
	{TEXT("Median filtering"),           DXVA2_DeinterlaceTech_MedianFiltering},
	{TEXT("Bob vertical stretch 4-tap"), DXVA2_DeinterlaceTech_BOBVerticalStretch4Tap},
	{TEXT("Bob vertical stretch"),       DXVA2_DeinterlaceTech_BOBVerticalStretch},
	{TEXT("Bob line replicate"),         DXVA2_DeinterlaceTech_BOBLineReplicate},
	{TEXT("Unknown"),                    DXVA2_DeinterlaceTech_Unknown},
};


const DeviceInfo *FindDevice(const GUID &guid)
{
	for (int i = 0; i < _countof(DeviceList); i++) {
		if (*DeviceList[i].guid == guid)
			return &DeviceList[i];
	}
	return nullptr;
}

const DeinterlaceTechInfo *FindDeinterlaceTech(unsigned int Flag)
{
	for (int i = 0; i < _countof(DeinterlaceTechList); i++) {
		if (DeinterlaceTechList[i].Flag == Flag)
			return &DeinterlaceTechList[i];
	}
	return nullptr;
}


CDeinterlacer_DXVA::CDeinterlacer_DXVA()
	: m_hD3D9Lib(nullptr)
	, m_hDXVA2Lib(nullptr)
	, m_pDirect3D9(nullptr)
	, m_pDirect3DDevice9(nullptr)
	, m_pDeviceManager(nullptr)
	, m_hDevice(nullptr)
	, m_pVideoProcessorService(nullptr)
	, m_pVideoProcessor(nullptr)
	, m_Width(0)
	, m_Height(0)
	, m_FrameNumber(0)
	, m_fOpenFailed(false)
{
}

CDeinterlacer_DXVA::~CDeinterlacer_DXVA()
{
	Finalize();
}

bool CDeinterlacer_DXVA::Initialize()
{
	DBG_TRACE(TEXT("CDeinterlacer_DXVA::Initialize()"));

	HRESULT hr;

	if (!m_pDeviceManager) {
		if (!m_hD3D9Lib) {
			m_hD3D9Lib = ::LoadLibrary(TEXT("d3d9.dll"));
			if (!m_hD3D9Lib) {
				DBG_ERROR(TEXT("Failed to load d3d9.dll"));
				return false;
			}
		}

		if (!m_hDXVA2Lib) {
			m_hDXVA2Lib = ::LoadLibrary(TEXT("dxva2.dll"));
			if (!m_hDXVA2Lib) {
				DBG_ERROR(TEXT("Failed to load dxva2.dll"));
				return false;
			}
		}

		if (!m_pDirect3D9) {
			auto pDirect3DCreate9 =
				reinterpret_cast<decltype(Direct3DCreate9)*>(::GetProcAddress(m_hD3D9Lib, "Direct3DCreate9"));
			if (!pDirect3DCreate9) {
				DBG_ERROR(TEXT("Failed to get Direct3DCreate9() address"));
				return false;
			}

			m_pDirect3D9 = pDirect3DCreate9(D3D_SDK_VERSION);
			if (!m_pDirect3D9) {
				DBG_ERROR(TEXT("Failed to create IDirect3D9"));
				return false;
			}
		}

		if (!m_pDirect3DDevice9) {
			D3DPRESENT_PARAMETERS d3dpp = {};

			d3dpp.BackBufferWidth = 640;
			d3dpp.BackBufferHeight = 480;
			d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
			d3dpp.BackBufferCount = 1;
			d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
			d3dpp.MultiSampleQuality = 0;
			d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
			d3dpp.hDeviceWindow = nullptr;
			d3dpp.Windowed = TRUE;
			d3dpp.EnableAutoDepthStencil = FALSE;
			d3dpp.Flags = D3DPRESENTFLAG_VIDEO | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
			d3dpp.FullScreen_RefreshRateInHz = 0;
			d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

			IDirect3DDevice9 *pDirect3DDevice9;
			hr = m_pDirect3D9->CreateDevice(
				D3DADAPTER_DEFAULT,
				D3DDEVTYPE_HAL,
				nullptr,
				D3DCREATE_SOFTWARE_VERTEXPROCESSING,
				&d3dpp,
				&pDirect3DDevice9);
			if (hr != D3D_OK) {
				DBG_ERROR(TEXT("IDirect3D9::CreateDevice() failed (%x)"), hr);
				return false;
			}
			m_pDirect3DDevice9 = pDirect3DDevice9;
		}

		auto pDXVA2CreateDirect3DDeviceManager9 =
			reinterpret_cast<decltype(DXVA2CreateDirect3DDeviceManager9)*>(
				::GetProcAddress(m_hDXVA2Lib, "DXVA2CreateDirect3DDeviceManager9"));
		if (!pDXVA2CreateDirect3DDeviceManager9) {
			DBG_ERROR(TEXT("Failed to get DXVA2CreateDirect3DDeviceManager9() address"));
			return false;
		}

		IDirect3DDeviceManager9 *pDeviceManager;
		hr = pDXVA2CreateDirect3DDeviceManager9(&m_ResetToken, &pDeviceManager);
		if (FAILED(hr)) {
			DBG_ERROR(TEXT("DXVA2CreateDirect3DDeviceManager9() failed (%x)"), hr);
			return false;
		}

		hr = pDeviceManager->ResetDevice(m_pDirect3DDevice9, m_ResetToken);
		if (FAILED(hr)) {
			DBG_ERROR(TEXT("IDirect3DDeviceManager9::ResetDevice() failed (%x)"), hr);
			pDeviceManager->Release();
			return false;
		}

		m_pDeviceManager = pDeviceManager;
	}

	if (!m_pVideoProcessorService) {
		IDirectXVideoProcessorService *pVideoProcessorService;

		if (m_hDevice) {
			hr = m_pDeviceManager->GetVideoService(m_hDevice, IID_PPV_ARGS(&pVideoProcessorService));
			if (FAILED(hr)) {
				DBG_ERROR(TEXT("IDirect3DDeviceManager9::GetVideoService() failed (%x)"), hr);
				return false;
			}
		} else {
			HANDLE hDevice;
			hr = m_pDeviceManager->OpenDeviceHandle(&hDevice);
			if (FAILED(hr)) {
				DBG_ERROR(TEXT("IDirect3DDeviceManager9::OpenDeviceHandle() failed (%x)"), hr);
				return false;
			}
			hr = m_pDeviceManager->GetVideoService(hDevice, IID_PPV_ARGS(&pVideoProcessorService));
			m_pDeviceManager->CloseDeviceHandle(hDevice);
			if (FAILED(hr)) {
				DBG_ERROR(TEXT("IDirect3DDeviceManager9::GetVideoService() failed (%x)"), hr);
				return false;
			}
		}

		m_pVideoProcessorService = pVideoProcessorService;
	}

	m_fOpenFailed = false;

	return true;
}

void CDeinterlacer_DXVA::Finalize()
{
	Close();

	SafeRelease(m_pVideoProcessorService);
	SafeRelease(m_pDeviceManager);
	SafeRelease(m_pDirect3DDevice9);
	SafeRelease(m_pDirect3D9);

	if (m_hDXVA2Lib) {
		::FreeLibrary(m_hDXVA2Lib);
		m_hDXVA2Lib = nullptr;
	}

	if (m_hD3D9Lib) {
		::FreeLibrary(m_hD3D9Lib);
		m_hD3D9Lib = nullptr;
	}
}

bool CDeinterlacer_DXVA::Open(int Width, int Height)
{
	Close();

	DXVA2_VideoDesc desc = {};

	desc.SampleWidth = Width;
	desc.SampleHeight = Height;
	desc.SampleFormat.SampleFormat = DXVA2_SampleFieldInterleavedEvenFirst;
	desc.SampleFormat.VideoLighting = DXVA2_VideoLighting_dim;
	desc.SampleFormat.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_MPEG2;
	desc.SampleFormat.NominalRange = DXVA2_NominalRange_16_235;
	desc.SampleFormat.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT709;
	desc.SampleFormat.VideoPrimaries = DXVA2_VideoPrimaries_BT709;
	desc.SampleFormat.VideoTransferFunction = DXVA2_VideoTransFunc_709;
	desc.Format = (D3DFORMAT)MAKEFOURCC('N','V','1','2');

	if (!OpenProcessor(Width, Height, desc)) {
		Close();
		m_fOpenFailed = true;
		return false;
	}

	return true;
}

void CDeinterlacer_DXVA::Close()
{
	SafeRelease(m_pVideoProcessor);

	for (auto e: m_Surfaces) {
		e->Release();
	}
	m_Surfaces.clear();
	m_RefSamples.clear();
}

bool CDeinterlacer_DXVA::OpenProcessor(int Width, int Height, const DXVA2_VideoDesc &desc)
{
	if (!m_pVideoProcessorService)
		return false;

	HRESULT hr;
	UINT Count;
	GUID *guids;

	hr = m_pVideoProcessorService->GetVideoProcessorDeviceGuids(&desc, &Count, &guids);
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("IDirectXVideoProcessorService::GetVideoProcessorDeviceGuids() failed (%x)"), hr);
		return false;
	}

	if (Count == 0) {
		DBG_ERROR(TEXT("Unable to find any video processor devices"));
		return false;
	}

	GUID guidDevice = GUID_NULL;
	DXVA2_VideoProcessorCaps caps;

	for (UINT i = 0; i < Count; i++) {
		const GUID &guid = guids[i];

		if (guid == DXVA2_VideoProcBobDevice)
			continue;

		hr = m_pVideoProcessorService->GetVideoProcessorCaps(
			guid, &desc, desc.Format/*D3DFMT_X8R8G8B8*/, &caps);
		if (SUCCEEDED(hr)) {
			const DeviceInfo *pDevice = FindDevice(guid);

			if (pDevice) {
				DBG_TRACE(TEXT("Deinterlace processor found (%s)"), pDevice->pszName);
				guidDevice = guid;
				break;
			} else if (caps.DeinterlaceTechnology) {
				guidDevice = guid;
				break;
			}
		}
	}

	::CoTaskMemFree(guids);

	if (guidDevice == GUID_NULL) {
		DBG_ERROR(TEXT("Unable to find any deinterlace processors"));
		return false;
	}

	IDirectXVideoProcessor *pVideoProcessor;
	hr = m_pVideoProcessorService->CreateVideoProcessor(
		guidDevice, &desc, desc.Format/*D3DFMT_X8R8G8B8*/, 0, &pVideoProcessor);
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("Failed to create IDirectXVideoProcessor (%x)"), hr);
		return false;
	}

	m_pVideoProcessor = pVideoProcessor;

	DBG_TRACE(TEXT("Backward ref %u / Forward ref %u"),
			  caps.NumBackwardRefSamples, caps.NumForwardRefSamples);

	m_BackwardRefSamples = caps.NumBackwardRefSamples;
	m_ForwardRefSamples = caps.NumForwardRefSamples;

	// Workaround for driver bug
	if (guidDevice == DXVA2_VideoProcIntelEdgeDevice)
		m_BackwardRefSamples = 0;

	m_pVideoProcessor->GetProcAmpRange(
		DXVA2_ProcAmp_Brightness, &m_ProcAmpBrightness);
	m_pVideoProcessor->GetProcAmpRange(
		DXVA2_ProcAmp_Contrast, &m_ProcAmpContrast);
	m_pVideoProcessor->GetProcAmpRange(
		DXVA2_ProcAmp_Hue, &m_ProcAmpHue);
	m_pVideoProcessor->GetProcAmpRange(
		DXVA2_ProcAmp_Saturation, &m_ProcAmpSaturation);

	m_NoiseFilterLuma.Level        = GetFilterDefaultValue(DXVA2_NoiseFilterLumaLevel);
	m_NoiseFilterLuma.Radius       = GetFilterDefaultValue(DXVA2_NoiseFilterLumaRadius);
	m_NoiseFilterLuma.Threshold    = GetFilterDefaultValue(DXVA2_NoiseFilterLumaThreshold);
	m_NoiseFilterChroma.Level      = GetFilterDefaultValue(DXVA2_NoiseFilterChromaLevel);
	m_NoiseFilterChroma.Radius     = GetFilterDefaultValue(DXVA2_NoiseFilterChromaRadius);
	m_NoiseFilterChroma.Threshold  = GetFilterDefaultValue(DXVA2_NoiseFilterChromaThreshold);
	m_DetailFilterLuma.Level       = GetFilterDefaultValue(DXVA2_DetailFilterLumaLevel);
	m_DetailFilterLuma.Radius      = GetFilterDefaultValue(DXVA2_DetailFilterLumaRadius);
	m_DetailFilterLuma.Threshold   = GetFilterDefaultValue(DXVA2_DetailFilterLumaThreshold);
	m_DetailFilterChroma.Level     = GetFilterDefaultValue(DXVA2_DetailFilterChromaLevel);
	m_DetailFilterChroma.Radius    = GetFilterDefaultValue(DXVA2_DetailFilterChromaRadius);
	m_DetailFilterChroma.Threshold = GetFilterDefaultValue(DXVA2_DetailFilterChromaThreshold);

	const UINT NumSurfaces = m_BackwardRefSamples + m_ForwardRefSamples + 2;

	m_Surfaces.resize(NumSurfaces);
	hr = m_pVideoProcessorService->CreateSurface(
		(desc.SampleWidth + 15) & ~15,
		(desc.SampleHeight + 15) & ~15,
		NumSurfaces - 1,
		desc.Format,
		//D3DPOOL_DEFAULT,
		caps.InputPool,
		0,
		DXVA2_VideoProcessorRenderTarget,
		&m_Surfaces[0],
		nullptr);
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("Failed to create surface (%x)"), hr);
		m_Surfaces.clear();
		return false;
	}

	m_Width = Width;
	m_Height = Height;
	m_VideoDesc = desc;
	m_FrameNumber = 0;

	return true;
}

DXVA2_Fixed32 CDeinterlacer_DXVA::GetFilterDefaultValue(UINT FilterSetting)
{
	DXVA2_ValueRange Range;
	HRESULT hr = m_pVideoProcessor->GetFilterPropertyRange(FilterSetting, &Range);
	if (FAILED(hr)) {
		static const DXVA2_Fixed32 Zero = {};
		return Zero;
	}
	return Range.DefaultValue;
}

CDeinterlacer::FrameStatus CDeinterlacer_DXVA::GetFrame(
	CFrameBuffer *pDstBuffer, const CFrameBuffer *pSrcBuffer,
	bool fTopField, int Field)
{
	const int Width = pDstBuffer->m_Width, Height = pDstBuffer->m_Height;

	if (!m_pVideoProcessor || m_Width != Width || m_Height != Height) {
		if (m_fOpenFailed)
			return FRAME_SKIP;
		if (!Open(Width, Height))
			return FRAME_SKIP;
	}

	const size_t TotalSamples = m_BackwardRefSamples + m_ForwardRefSamples + 1;

	if (m_RefSamples.size() == TotalSamples)
		m_RefSamples.pop_front();

	IDirect3DSurface9 *pSurface = GetFreeSurface();
	if (!pSurface) {
		DBG_ERROR(TEXT("Unable to find unused surface"));
		return FRAME_SKIP;
	}

	HRESULT hr;

	D3DSURFACE_DESC desc;

	hr = pSurface->GetDesc(&desc);
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("Failed to get surface desc (%x)"), hr);
		return FRAME_SKIP;
	}

	D3DLOCKED_RECT rect;

	hr = pSurface->LockRect(&rect, nullptr, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK);
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("Failed to lock surface (%x)"), hr);
		return FRAME_SKIP;
	}

	PixelCopyI420ToNV12(
		Width, Height,
		(uint8_t*)rect.pBits, (uint8_t*)rect.pBits + desc.Height * rect.Pitch, rect.Pitch,
		pSrcBuffer->m_Buffer[0], pSrcBuffer->m_Buffer[1], pSrcBuffer->m_Buffer[2],
		pSrcBuffer->m_PitchY, pSrcBuffer->m_PitchC);

	pSurface->UnlockRect();

	SampleInfo Sample;
	Sample.pSurface = pSurface;
	Sample.StartTime = pDstBuffer->m_rtStart;
	Sample.EndTime = pDstBuffer->m_rtStop;
	Sample.fTopFieldFirst = fTopField;
	m_RefSamples.push_back(Sample);

	if (m_RefSamples.size() <= m_ForwardRefSamples)
		return FRAME_SKIP;

	m_FrameNumber++;

	DXVA2_VideoSample Samples[MAX_DEINTERLACE_SURFACES];
	RECT rc = {0, 0, Width, Height};

	ZeroMemory(Samples, sizeof(Samples));

	size_t BackwardSamples = 0;
	if (m_RefSamples.size() > m_ForwardRefSamples + 1)
		BackwardSamples = m_RefSamples.size() - (m_ForwardRefSamples + 1);

	for (size_t i = 0; i < m_RefSamples.size(); i++) {
		DXVA2_VideoSample &vs = Samples[i];

		vs.Start = m_FrameNumber + i - BackwardSamples;
		vs.End = vs.Start + 1;
		vs.SampleFormat = m_VideoDesc.SampleFormat;
		vs.SampleFormat.SampleFormat =
			m_RefSamples[i].fTopFieldFirst ?
				DXVA2_SampleFieldInterleavedEvenFirst :
				DXVA2_SampleFieldInterleavedOddFirst;
		vs.SrcSurface = m_RefSamples[i].pSurface;
		vs.SrcRect = rc;
		vs.DstRect = rc;
		vs.PlanarAlpha = DXVA2_Fixed32OpaqueAlpha();
	}

	DXVA2_VideoProcessBltParams blt = {};

	blt.TargetFrame = m_FrameNumber;
	blt.TargetRect = rc;
	blt.BackgroundColor.Y     = 0x1000;
	blt.BackgroundColor.Cb    = 0x8000;
	blt.BackgroundColor.Cr    = 0x8000;
	blt.BackgroundColor.Alpha = 0xffff;
	blt.DestFormat.SampleFormat = DXVA2_SampleProgressiveFrame;
	blt.DestFormat.NominalRange = m_VideoDesc.SampleFormat.NominalRange;
	blt.DestFormat.VideoTransferFunction = DXVA2_VideoTransFunc_sRGB;
	blt.ProcAmpValues.Brightness = m_ProcAmpBrightness.DefaultValue;
	blt.ProcAmpValues.Contrast = m_ProcAmpContrast.DefaultValue;
	blt.ProcAmpValues.Hue = m_ProcAmpHue.DefaultValue;
	blt.ProcAmpValues.Saturation = m_ProcAmpSaturation.DefaultValue;
	blt.Alpha = DXVA2_Fixed32OpaqueAlpha();
	blt.NoiseFilterLuma = m_NoiseFilterLuma;
	blt.NoiseFilterChroma = m_NoiseFilterChroma;
	blt.DetailFilterLuma = m_DetailFilterLuma;
	blt.DetailFilterChroma = m_DetailFilterChroma;

	pSurface = m_Surfaces.front();

	hr = m_pVideoProcessor->VideoProcessBlt(pSurface, &blt, Samples, (UINT)m_RefSamples.size(), nullptr);
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("VideoProcessBlt() failed (%x)"), hr);
		return FRAME_SKIP;
	}

	if (pDstBuffer->m_pSurface) {
		D3DSURFACE_DESC descSrc, descDst;
		D3DLOCKED_RECT rectSrc, rectDst;

		hr = pSurface->GetDesc(&descSrc);
		if (FAILED(hr)) {
			DBG_ERROR(TEXT("Failed to get surface desc (%x)"), hr);
			return FRAME_SKIP;
		}

		hr = pDstBuffer->m_pSurface->GetDesc(&descDst);
		if (FAILED(hr)) {
			DBG_ERROR(TEXT("Failed to get surface desc (%x)"), hr);
			return FRAME_SKIP;
		}

		hr = pSurface->LockRect(&rectSrc, nullptr, D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK);
		if (FAILED(hr)) {
			DBG_ERROR(TEXT("Failed to lock surface (%x)"), hr);
			return FRAME_SKIP;
		}

		hr = pDstBuffer->m_pSurface->LockRect(&rectDst, nullptr, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK);
		if (FAILED(hr)) {
			DBG_ERROR(TEXT("Failed to lock surface (%x)"), hr);
			pSurface->UnlockRect();
			return FRAME_SKIP;
		}

		PixelCopyNV12ToNV12(
			Width, Height,
			(uint8_t*)rectDst.pBits, (uint8_t*)rectDst.pBits + descDst.Height * rectDst.Pitch, rectDst.Pitch,
			(const uint8_t*)rectSrc.pBits, (const uint8_t*)rectSrc.pBits + descSrc.Height * rectSrc.Pitch, rectSrc.Pitch);

		pDstBuffer->m_pSurface->UnlockRect();
		pSurface->UnlockRect();
	} else {
		hr = pSurface->GetDesc(&desc);
		if (FAILED(hr)) {
			DBG_ERROR(TEXT("Failed to get surface desc (%x)"), hr);
			return FRAME_SKIP;
		}

		hr = pSurface->LockRect(&rect, nullptr, D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK);
		if (FAILED(hr)) {
			DBG_ERROR(TEXT("Failed to lock surface (%x)"), hr);
			return FRAME_SKIP;
		}

#if 0
		PixelCopyNV12ToI420(
			Width, Height,
			pDstBuffer->m_Buffer[0], pDstBuffer->m_Buffer[1], pDstBuffer->m_Buffer[2],
			pDstBuffer->m_PitchY, pDstBuffer->m_PitchC,
			(const uint8_t*)rect.pBits, (const uint8_t*)rect.pBits + desc.Height * rect.Pitch, rect.Pitch);
#else
		PixelCopyNV12ToNV12(
			Width, Height,
			pDstBuffer->m_Buffer[0], pDstBuffer->m_Buffer[1], pDstBuffer->m_PitchY,
			(const uint8_t*)rect.pBits, (const uint8_t*)rect.pBits + desc.Height * rect.Pitch, rect.Pitch);
		pDstBuffer->m_Subtype = MEDIASUBTYPE_NV12;
#endif

		pSurface->UnlockRect();
	}

	const SampleInfo *pSample = &m_RefSamples[m_RefSamples.size() - 1 - m_ForwardRefSamples];
	pDstBuffer->m_rtStart = pSample->StartTime;
	pDstBuffer->m_rtStop = pSample->EndTime;

	return FRAME_OK;
}

IDirect3DSurface9 *CDeinterlacer_DXVA::GetFreeSurface()
{
	for (size_t i = 1; i < m_Surfaces.size(); i++) {
		bool fUsed = false;
		for (size_t j = 0; j < m_RefSamples.size(); j++) {
			if (m_RefSamples[j].pSurface == m_Surfaces[i]) {
				fUsed = true;
				break;
			}
		}
		if (!fUsed) {
			return m_Surfaces[i];
		}
	}

	return nullptr;
}
