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
#include "Mpeg2DecoderDXVA2.h"
#include "TVTestVideoDecoder.h"
#include "DXVA2Allocator.h"
#include "Util.h"
#include "MediaTypes.h"
#include "libmpeg2/libmpeg2/mpeg2_internal.h"
#include <initguid.h>


// {86695F12-340E-4f04-9FD3-9253DD327460}
DEFINE_GUID(DXVA2_ModeMPEG2and1_VLD, 0x86695f12, 0x340e, 0x4f04, 0x9f, 0xd3, 0x92, 0x53, 0xdd, 0x32, 0x74, 0x60);

static const GUID * const DecoderGuidList[] = {
	&DXVA2_ModeMPEG2_VLD,
	&DXVA2_ModeMPEG2and1_VLD
};


CMpeg2DecoderDXVA2::CMpeg2DecoderDXVA2()
	: m_pFilter(nullptr)
	, m_pDeviceManager(nullptr)
	, m_pDecoderService(nullptr)
	, m_pVideoDecoder(nullptr)
	, m_fDeviceLost(false)
	, m_pSliceBuffer(nullptr)
	, m_SliceBufferSize(0)
	, m_SliceDataSize(0)
	, m_SliceCount(0)
	, m_fWaitForDecodeKeyFrame(true)
	, m_fWaitForDisplayKeyFrame(true)
	, m_CurSurfaceIndex(-1)
	, m_PrevRefSurfaceIndex(-1)
	, m_ForwardRefSurfaceIndex(-1)
	, m_DecodeSampleIndex(-1)
	, m_Samples()
	, m_RefSamples()
{
	::ZeroMemory(&m_AdapterIdentifier, sizeof(m_AdapterIdentifier));
}

CMpeg2DecoderDXVA2::~CMpeg2DecoderDXVA2()
{
	Close();
	CloseDecoderService();
}

bool CMpeg2DecoderDXVA2::Open()
{
	Close();

	m_NumThreads = 1;

	if (!CMpeg2Decoder::Open())
		return false;

	mpeg2_set_client_data(m_pDec, this);
	mpeg2_slice_hook(m_pDec, SliceHook);

	return true;
}

void CMpeg2DecoderDXVA2::Close()
{
	CMpeg2Decoder::Close();

	if (m_pSliceBuffer) {
		free(m_pSliceBuffer);
		m_pSliceBuffer = nullptr;
	}
	m_SliceBufferSize = 0;
}

mpeg2_state_t CMpeg2DecoderDXVA2::Parse()
{
	mpeg2_state_t State = CMpeg2Decoder::Parse();

	switch (State) {
	case STATE_PICTURE:
		m_SliceDataSize = 0;
		m_SliceCount = 0;
		break;

	case STATE_SLICE_1ST:
		DecodeFrame(nullptr);
		m_SliceDataSize = 0;
		m_SliceCount = 0;
		break;
	}

	return State;
}

HRESULT CMpeg2DecoderDXVA2::CreateDecoderService(CTVTestVideoDecoder *pFilter)
{
	DBG_TRACE(TEXT("CMpeg2DecoderDXVA2::CreateDecoderService()"));

	if (!pFilter)
		return E_POINTER;
	if (!pFilter->m_pD3DDeviceManager || !pFilter->m_hDXVADevice)
		return E_UNEXPECTED;

	CloseDecoderService();

	m_pFilter = pFilter;
	m_pDeviceManager = pFilter->m_pD3DDeviceManager;
	m_pDeviceManager->AddRef();

	HRESULT hr;

	IDirect3DDevice9 *pDevice;
	hr = m_pDeviceManager->LockDevice(m_pFilter->m_hDXVADevice, &pDevice, TRUE);
	if (SUCCEEDED(hr)) {
		D3DDEVICE_CREATION_PARAMETERS CreationParams;
		hr = pDevice->GetCreationParameters(&CreationParams);
		if (SUCCEEDED(hr)) {
			IDirect3D9 *pD3D;
			hr = pDevice->GetDirect3D(&pD3D);
			if (SUCCEEDED(hr)) {
				D3DADAPTER_IDENTIFIER9 AdapterID;
				hr = pD3D->GetAdapterIdentifier(CreationParams.AdapterOrdinal, 0, &AdapterID);
				if (SUCCEEDED(hr)) {
					WCHAR szDriver[MAX_DEVICE_IDENTIFIER_STRING];
					WCHAR szDescription[MAX_DEVICE_IDENTIFIER_STRING];
					WCHAR szDeviceName[32];
					::MultiByteToWideChar(CP_ACP, 0, AdapterID.Driver, -1, szDriver, _countof(szDriver));
					::MultiByteToWideChar(CP_ACP, 0, AdapterID.Description, -1, szDescription, _countof(szDescription));
					::MultiByteToWideChar(CP_ACP, 0, AdapterID.DeviceName, -1, szDeviceName, _countof(szDeviceName));
					DBG_TRACE(TEXT("--- Adapter information ---"));
					DBG_TRACE(TEXT("     Driver : %s"), szDriver);
					DBG_TRACE(TEXT("Description : %s"), szDescription);
					DBG_TRACE(TEXT("Device name : %s"), szDeviceName);
					DBG_TRACE(TEXT("    Product : %08x"), HIWORD(AdapterID.DriverVersion.HighPart));
					DBG_TRACE(TEXT("    Version : %d.%d.%d"),
							  LOWORD(AdapterID.DriverVersion.HighPart),
							  HIWORD(AdapterID.DriverVersion.LowPart),
							  LOWORD(AdapterID.DriverVersion.LowPart));
					DBG_TRACE(TEXT("     Vendor : %08x"), AdapterID.VendorId);
					DBG_TRACE(TEXT("  Device ID : %08x"), AdapterID.DeviceId);
					DBG_TRACE(TEXT("  Subsystem : %08x"), AdapterID.SubSysId);
					DBG_TRACE(TEXT("   Revision : %08x"), AdapterID.Revision);
					m_AdapterIdentifier = AdapterID;
				}
				pD3D->Release();
			}
		}
		pDevice->Release();
		m_pDeviceManager->UnlockDevice(m_pFilter->m_hDXVADevice, FALSE);
	}
	if (FAILED(hr)) {
		::ZeroMemory(&m_AdapterIdentifier, sizeof(m_AdapterIdentifier));
	}

	IDirectXVideoDecoderService *pDecoderService;
	hr = m_pDeviceManager->GetVideoService(m_pFilter->m_hDXVADevice, IID_PPV_ARGS(&pDecoderService));
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("GetVideoService() failed (%x)"), hr);
		CloseDecoderService();
		return hr;
	}
	m_pDecoderService = pDecoderService;

	return S_OK;
}

void CMpeg2DecoderDXVA2::CloseDecoderService()
{
	DBG_TRACE(TEXT("CMpeg2DecoderDXVA2::CloseDecoderService()"));

	CloseDecoder();

	SafeRelease(m_pDecoderService);
	SafeRelease(m_pDeviceManager);
	m_pFilter = nullptr;
}

HRESULT CMpeg2DecoderDXVA2::CreateDecoder(IDirect3DSurface9 **ppSurface, int SurfaceCount)
{
	DBG_TRACE(TEXT("CMpeg2DecoderDXVA2::CreateDecoder()"));

	if (!ppSurface)
		return E_POINTER;
	if (!m_pFilter || !m_pDecoderService)
		return E_UNEXPECTED;

	CloseDecoder();

	HRESULT hr;

	UINT Count;
	GUID *pGuids;
	hr = m_pDecoderService->GetDecoderDeviceGuids(&Count, &pGuids);
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("GetDecoderDeviceGuids() failed (%x)"), hr);
		return hr;
	}

	bool fFound = false;
	GUID guidDecoder;
	for (UINT i = 0; i < Count && !fFound; i++) {
		for (int j = 0; j < _countof(DecoderGuidList); j++) {
			if (pGuids[i] == *DecoderGuidList[j]) {
				guidDecoder = pGuids[i];
				fFound = true;
				break;
			}
		}
	}
	::CoTaskMemFree(pGuids);
	if (!fFound) {
		DBG_ERROR(TEXT("Decoder not found"));
		return hr;
	}

	D3DFORMAT *pFormats;
	hr = m_pDecoderService->GetDecoderRenderTargets(guidDecoder, &Count, &pFormats);
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("GetDecoderRenderTargets() failed (%x)"), hr);
		return hr;
	}

	fFound = false;
	for (UINT i = 0; i < Count; i++) {
		if (pFormats[i] == D3DFMT_NV12) {
			fFound = true;
			break;
		}
	}
	::CoTaskMemFree(pFormats);
	if (!fFound) {
		DBG_ERROR(TEXT("Format not available"));
		return hr;
	}

	DXVA2_VideoDesc VideoDesc = {};

	VideoDesc.SampleWidth = m_pFilter->GetAlignedWidth();
	VideoDesc.SampleHeight = m_pFilter->GetAlignedHeight();
	VideoDesc.SampleFormat.SampleFormat = DXVA2_SampleFieldInterleavedEvenFirst;
	VideoDesc.SampleFormat.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_MPEG2;
	VideoDesc.SampleFormat.NominalRange = DXVA2_NominalRange_16_235;
	VideoDesc.SampleFormat.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT709;
	VideoDesc.SampleFormat.VideoLighting = DXVA2_VideoLighting_dim;
	VideoDesc.SampleFormat.VideoPrimaries = DXVA2_VideoPrimaries_BT709;
	VideoDesc.SampleFormat.VideoTransferFunction = DXVA2_VideoTransFunc_709;
	VideoDesc.Format = D3DFMT_NV12;

	DXVA2_ConfigPictureDecode *pConfigs;
	hr = m_pDecoderService->GetDecoderConfigurations(
		guidDecoder, &VideoDesc, nullptr, &Count, &pConfigs);
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("GetDecoderConfigurations() failed (%x)"), hr);
		return hr;
	}

	int SelConfig = -1;
	for (UINT i = 0; i < Count; i++) {
		if (pConfigs[i].ConfigBitstreamRaw == 1) {
			SelConfig = i;
			break;
		}
	}
	if (SelConfig < 0) {
		::CoTaskMemFree(pConfigs);
		return E_FAIL;
	}

	IDirectXVideoDecoder *pVideoDecoder;
	hr = m_pDecoderService->CreateVideoDecoder(
		guidDecoder,
		&VideoDesc,
		&pConfigs[SelConfig],
		ppSurface,
		SurfaceCount,
		&pVideoDecoder);
	::CoTaskMemFree(pConfigs);
	if (FAILED(hr)) {
		DBG_ERROR(TEXT("CreateVideoDecoder() failed (%x)"), hr);
		return hr;
	}
	m_pVideoDecoder = pVideoDecoder;

	return hr;
}

void CMpeg2DecoderDXVA2::CloseDecoder()
{
	DBG_TRACE(TEXT("CMpeg2DecoderDXVA2::CloseDecoder()"));

	ResetDecoding();

	SafeRelease(m_pVideoDecoder);
	m_fDeviceLost = false;
}

HRESULT CMpeg2DecoderDXVA2::RecreateDecoder()
{
	DBG_TRACE(TEXT("CMpeg2DecoderDXVA2::RecreateDecoder()"));

	if (!m_pVideoDecoder || !m_pDecoderService)
		return E_UNEXPECTED;

	ResetDecoding();

	HRESULT hr;
	GUID guidDevice;
	DXVA2_VideoDesc VideoDesc;
	DXVA2_ConfigPictureDecode Config;
	IDirect3DSurface9 **ppSurface;
	UINT NumSurfaces;

	hr = m_pVideoDecoder->GetCreationParameters(
		&guidDevice, &VideoDesc, &Config, &ppSurface, &NumSurfaces);
	if (FAILED(hr)) {
		return hr;
	}
	SafeRelease(m_pVideoDecoder);

	IDirectXVideoDecoder *pVideoDecoder;
	hr = m_pDecoderService->CreateVideoDecoder(
		guidDevice,
		&VideoDesc,
		&Config,
		ppSurface,
		NumSurfaces,
		&pVideoDecoder);
	for (UINT i = 0; i < NumSurfaces; i++) {
		SafeRelease(ppSurface[i]);
	}
	::CoTaskMemFree(ppSurface);
	if (FAILED(hr)) {
		return hr;
	}
	m_pVideoDecoder = pVideoDecoder;

	return S_OK;
}

void CMpeg2DecoderDXVA2::ResetDecoding()
{
	DBG_TRACE(TEXT("CMpeg2DecoderDXVA2::ResetDecoding()"));

	for (auto &e: m_Samples) {
		SafeRelease(e.pSample);
		e.SurfaceID = -1;
	}
	for (auto &e: m_RefSamples) {
		SafeRelease(e.pSample);
		e.SurfaceID = -1;
	}

	m_SliceDataSize = 0;
	m_SliceCount = 0;
	m_fWaitForDecodeKeyFrame = true;
	m_fWaitForDisplayKeyFrame = true;
	m_CurSurfaceIndex = -1;
	m_PrevRefSurfaceIndex = -1;
	m_ForwardRefSurfaceIndex = -1;
	m_DecodeSampleIndex = -1;
}

HRESULT CMpeg2DecoderDXVA2::DecodeFrame(IMediaSample **ppSample)
{
	if (ppSample) {
		*ppSample = nullptr;
	}

	if (!m_pDec || !m_pVideoDecoder) {
		return E_UNEXPECTED;
	}

	if (m_pDec->picture->flags & PIC_FLAG_SKIP) {
		return GetDisplaySample(ppSample);
	}

	m_DecodeSampleIndex = GetFBufIndex(m_pDec->fbuf[0]);

	if (!m_SliceCount || m_DecodeSampleIndex < 0) {
		return S_FALSE;
	}

	if (m_fWaitForDecodeKeyFrame) {
		if ((m_pDec->picture->flags & PIC_MASK_CODING_TYPE) != PIC_FLAG_CODING_TYPE_I) {
			return S_FALSE;
		}
		m_fWaitForDecodeKeyFrame = false;
	}

	HRESULT hr;

	hr = m_pDeviceManager->TestDevice(m_pFilter->m_hDXVADevice);
	if (FAILED(hr)) {
		if (hr == DXVA2_E_NEW_VIDEO_DEVICE) {
			DBG_TRACE(TEXT("Device lost"));
			m_fDeviceLost = true;
		}
		return hr;
	}

	switch (m_pDec->picture->flags & PIC_MASK_CODING_TYPE) {
	case PIC_FLAG_CODING_TYPE_I:
		m_PrevRefSurfaceIndex = -1;
		m_ForwardRefSurfaceIndex = -1;
		//DBG_TRACE(TEXT("I [%d]"), m_CurSurfaceIndex);
		break;
	case PIC_FLAG_CODING_TYPE_P:
		m_PrevRefSurfaceIndex = GetFBufSampleID(m_pDec->fbuf[1]);
		m_ForwardRefSurfaceIndex = -1;
		//DBG_TRACE(TEXT("P [%d]->%d"), m_CurSurfaceIndex, m_PrevRefSurfaceIndex);
		break;
	case PIC_FLAG_CODING_TYPE_B:
		m_PrevRefSurfaceIndex = GetFBufSampleID(m_pDec->fbuf[1]);
		m_ForwardRefSurfaceIndex = GetFBufSampleID(m_pDec->fbuf[2]);
		//DBG_TRACE(TEXT("B %d->[%d]->%d"), m_PrevRefSurfaceIndex, m_CurSurfaceIndex, m_ForwardRefSurfaceIndex);
		if (m_ForwardRefSurfaceIndex < 0)
			return S_FALSE;
		break;
	}

	CDXVA2MediaSample *pSample = m_Samples[m_DecodeSampleIndex].pSample;

	if (!pSample) {
		IMediaSample *pMediaSample;
		IDXVA2MediaSample *pDXVA2Sample;

		for (;;) {
			hr = m_pFilter->GetDeliveryBuffer(&pMediaSample);
			if (FAILED(hr)) {
				return hr;
			}
			hr = pMediaSample->QueryInterface(IID_PPV_ARGS(&pDXVA2Sample));
			pMediaSample->Release();
			if (FAILED(hr)) {
				return hr;
			}
			pSample = static_cast<CDXVA2MediaSample*>(pDXVA2Sample);
			if (pSample->GetSurfaceID() == m_RefSamples[0].SurfaceID) {
				m_RefSamples[0].pSample = pSample;
			} else if (pSample->GetSurfaceID() == m_RefSamples[1].SurfaceID) {
				m_RefSamples[1].pSample = pSample;
			} else {
				break;
			}
		}
		m_Samples[m_DecodeSampleIndex].pSample = pSample;
		m_Samples[m_DecodeSampleIndex].SurfaceID = pSample->GetSurfaceID();
	}

	m_CurSurfaceIndex = pSample->GetSurfaceID();

#ifdef _DEBUG
	if ((m_pDec->picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_P) {
		_ASSERT(m_PrevRefSurfaceIndex>=0 && m_CurSurfaceIndex != m_PrevRefSurfaceIndex);
	} else if ((m_pDec->picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_B) {
		_ASSERT(m_PrevRefSurfaceIndex>=0
			&& m_CurSurfaceIndex != m_PrevRefSurfaceIndex
			&& m_ForwardRefSurfaceIndex>=0
			&& m_CurSurfaceIndex != m_ForwardRefSurfaceIndex);
	}
#endif

	IDirect3DSurface9 *pSurface;
	IMFGetService *pMFGetService;
	hr = pSample->QueryInterface(IID_PPV_ARGS(&pMFGetService));
	if (SUCCEEDED(hr)) {
		hr = pMFGetService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface));
		pMFGetService->Release();
	}
	if (FAILED(hr)) {
		return hr;
	}

	int Retry = 0;
	for (;;) {
		hr = m_pVideoDecoder->BeginFrame(pSurface, nullptr);
		if (hr != E_PENDING || Retry >= 50)
			break;
		::Sleep(2);
		Retry++;
	}
	if (SUCCEEDED(hr)) {
		hr = CommitBuffers();
		if (SUCCEEDED(hr)) {
			DXVA2_DecodeExecuteParams ExecParams;
			DXVA2_DecodeBufferDesc BufferDesc[4];
			const UINT NumMBsInBuffer =
				(m_PictureParams.wPicWidthInMBminus1 + 1) * (m_PictureParams.wPicHeightInMBminus1 + 1);

			::ZeroMemory(BufferDesc, sizeof(BufferDesc));
			BufferDesc[0].CompressedBufferType = DXVA2_PictureParametersBufferType;
			BufferDesc[0].DataSize = sizeof(DXVA_PictureParameters);
			BufferDesc[1].CompressedBufferType = DXVA2_InverseQuantizationMatrixBufferType;
			BufferDesc[1].DataSize = sizeof(DXVA_QmatrixData);
			BufferDesc[2].CompressedBufferType = DXVA2_BitStreamDateBufferType;
			BufferDesc[2].DataSize = (UINT)m_SliceDataSize;
			BufferDesc[2].NumMBsInBuffer = NumMBsInBuffer;
			BufferDesc[3].CompressedBufferType = DXVA2_SliceControlBufferType;
			BufferDesc[3].DataSize = m_SliceCount * sizeof(DXVA_SliceInfo);
			BufferDesc[3].NumMBsInBuffer = NumMBsInBuffer;

			ExecParams.NumCompBuffers = 4;
			ExecParams.pCompressedBuffers = BufferDesc;
			ExecParams.pExtensionData = nullptr;

			hr = m_pVideoDecoder->Execute(&ExecParams);
			if (SUCCEEDED(hr)) {
				hr = GetDisplaySample(ppSample);
			}
		}

		m_pVideoDecoder->EndFrame(nullptr);
	}

	if ((m_pDec->picture->flags & PIC_MASK_CODING_TYPE) != PIC_FLAG_CODING_TYPE_B
			&& ppSample) {
		SafeRelease(m_RefSamples[1].pSample);
		m_RefSamples[1] = m_RefSamples[0];
		m_RefSamples[0].pSample = nullptr;
		m_RefSamples[0].SurfaceID = m_CurSurfaceIndex;
	}

	pSurface->Release();

	return hr;
}

HRESULT CMpeg2DecoderDXVA2::CommitBuffers()
{
	HRESULT hr;
	void *pBuffer;
	UINT BufferSize;

	hr = m_pVideoDecoder->GetBuffer(DXVA2_PictureParametersBufferType, &pBuffer, &BufferSize);
	if (FAILED(hr)) {
		return hr;
	}
	if (BufferSize < sizeof(DXVA_PictureParameters)) {
		return E_FAIL;
	}
	GetPictureParams(&m_PictureParams);
	memcpy(pBuffer, &m_PictureParams, sizeof(DXVA_PictureParameters));
	m_pVideoDecoder->ReleaseBuffer(DXVA2_PictureParametersBufferType);

	hr = m_pVideoDecoder->GetBuffer(DXVA2_InverseQuantizationMatrixBufferType, &pBuffer, &BufferSize);
	if (FAILED(hr)) {
		return hr;
	}
	if (BufferSize < sizeof(DXVA_QmatrixData)) {
		return E_FAIL;
	}
	GetQmatrixData(&m_Qmatrix);
	memcpy(pBuffer, &m_Qmatrix, sizeof(DXVA_QmatrixData));
	m_pVideoDecoder->ReleaseBuffer(DXVA2_InverseQuantizationMatrixBufferType);

	hr = m_pVideoDecoder->GetBuffer(DXVA2_BitStreamDateBufferType, &pBuffer, &BufferSize);
	if (FAILED(hr)) {
		return hr;
	}
	if (BufferSize < m_SliceDataSize) {
		return E_FAIL;
	}
	memcpy(pBuffer, m_pSliceBuffer, m_SliceDataSize);
	m_pVideoDecoder->ReleaseBuffer(DXVA2_BitStreamDateBufferType);

	hr = m_pVideoDecoder->GetBuffer(DXVA2_SliceControlBufferType, &pBuffer, &BufferSize);
	if (FAILED(hr)) {
		return hr;
	}
	if (BufferSize < m_SliceCount * sizeof(DXVA_SliceInfo)) {
		return E_FAIL;
	}
	memcpy(pBuffer, m_SliceInfo, m_SliceCount * sizeof(DXVA_SliceInfo));
	m_pVideoDecoder->ReleaseBuffer(DXVA2_SliceControlBufferType);

	return S_OK;
}

void CMpeg2DecoderDXVA2::GetPictureParams(DXVA_PictureParameters *pParams)
{
	const mpeg2_decoder_t *decoder = &m_pDec->decoder;
	const mpeg2_picture_t *picture = m_pDec->picture;
	const uint32_t PicType = picture->flags & PIC_MASK_CODING_TYPE;
	const int IsField = decoder->picture_structure != FRAME_PICTURE;

	::ZeroMemory(pParams, sizeof(DXVA_PictureParameters));

	pParams->wDecodedPictureIndex = m_CurSurfaceIndex;
	if (PicType != PIC_FLAG_CODING_TYPE_B) {
		pParams->wForwardRefPictureIndex =
			(PicType != PIC_FLAG_CODING_TYPE_I) ? (WORD)m_PrevRefSurfaceIndex : 0xffff;
		pParams->wBackwardRefPictureIndex = 0xffff;
	} else {
		pParams->wForwardRefPictureIndex = (WORD)m_ForwardRefSurfaceIndex;
		pParams->wBackwardRefPictureIndex = (WORD)m_PrevRefSurfaceIndex;
	}
	pParams->wPicWidthInMBminus1 = (m_pDec->sequence.width >> 4) - 1;
	pParams->wPicHeightInMBminus1 = ((m_pDec->sequence.height >> 4) >> IsField) - 1;
	pParams->bMacroblockWidthMinus1 = 15;
	pParams->bMacroblockHeightMinus1 = 15;
	pParams->bBlockWidthMinus1 = 7;
	pParams->bBlockHeightMinus1 = 7;
	pParams->bBPPminus1 = 7;
	pParams->bPicStructure = decoder->picture_structure;
	pParams->bSecondField = IsField && decoder->second_field;
	pParams->bPicIntra = PicType == PIC_FLAG_CODING_TYPE_I;
	pParams->bPicBackwardPrediction = PicType == PIC_FLAG_CODING_TYPE_B;
	pParams->bChromaFormat = m_pDec->sequence.chroma_format;
	pParams->bPicScanFixed = 1;
	pParams->bPicScanMethod = decoder->alternate_scan;
	pParams->wBitstreamFcodes =
		((decoder->f_motion.f_code[0] + 1) << 12) |
		((decoder->f_motion.f_code[1] + 1) << 8) |
		((decoder->b_motion.f_code[0] + 1) << 4) |
		((decoder->b_motion.f_code[1] + 1));
	pParams->wBitstreamPCEelements =
		(decoder->intra_dc_precision << 14) |
		(decoder->picture_structure << 12) |
		(decoder->top_field_first << 11) |
		(decoder->frame_pred_frame_dct << 10) |
		(decoder->concealment_motion_vectors << 9) |
		(decoder->q_scale_type << 8) |
		(decoder->intra_vlc_format << 7) |
		(decoder->alternate_scan << 6) |
		(decoder->repeat_first_field << 5) |
		(decoder->chroma_420_type << 4) |
		(decoder->progressive_frame << 3);
}

void CMpeg2DecoderDXVA2::GetQmatrixData(DXVA_QmatrixData *pQmatrix)
{
	for (int i = 0; i < 4; i++) {
		pQmatrix->bNewQmatrix[i] = !!(m_pDec->valid_matrix & (1 << i));
	}

	_ASSERT(pQmatrix->bNewQmatrix[0] && pQmatrix->bNewQmatrix[1]);

	for (int i = 0; i < 4; i++) {
		if (pQmatrix->bNewQmatrix[i]) {
			for (int j = 0; j < 64; j++) {
				pQmatrix->Qmatrix[i][j] = m_pDec->quantizer_matrix[i][mpeg2_scan_norm[j]];
			}
		}
	}
}

void CMpeg2DecoderDXVA2::Slice(mpeg2dec_t *mpeg2dec, int code, const uint8_t *buffer, int bytes)
{
	if (m_SliceCount >= MAX_SLICE)
		return;

	if (m_SliceDataSize + 4 + bytes > m_SliceBufferSize) {
		static const size_t GrowSize = 512 * 1024;
		size_t NewSize = ((m_SliceDataSize + 4 + bytes) + GrowSize) / GrowSize * GrowSize;
		uint8_t *pNewBuffer = static_cast<uint8_t *>(realloc(m_pSliceBuffer, NewSize));

		if (!pNewBuffer)
			return;
		m_pSliceBuffer = pNewBuffer;
		m_SliceBufferSize = NewSize;
	}

	uint8_t *p = m_pSliceBuffer + m_SliceDataSize;
	p[0] = 0;
	p[1] = 0;
	p[2] = 1;
	p[3] = (uint8_t)code;
	memcpy(p + 4, buffer, bytes);

	bitstream_t bitstream;
	int Offset = 0;

	bitstream_init(&bitstream, buffer, bytes);

	if (mpeg2dec->decoder.vertical_position_extension) {
		bitstream_dumpbits(&bitstream, 3);
		Offset += 3;
	}
	int quantizer_scale_code = bitstream_ubits(&bitstream, 5);
	bitstream_dumpbits(&bitstream, 5);
	Offset += 5;
	for (;;) {
		Offset++;
		if (!(bitstream.buf & 0x80000000))
			break;
		bitstream_dumpbits(&bitstream, 9);
		bitstream_needbits(&bitstream);
		Offset += 8;
	}

	DXVA_SliceInfo &SliceInfo = m_SliceInfo[m_SliceCount];

	SliceInfo.wHorizontalPosition = 0;
	SliceInfo.wVerticalPosition = static_cast<WORD>(code - 1);
	SliceInfo.dwSliceBitsInBuffer = 8 * (4 + bytes);
	SliceInfo.dwSliceDataLocation = static_cast<DWORD>(m_SliceDataSize);
	SliceInfo.bStartCodeBitOffset = 0;
	SliceInfo.bReservedBits = 0;
	SliceInfo.wMBbitOffset = static_cast<WORD>(4 * 8 + Offset);
	SliceInfo.wNumberMBsInSlice = static_cast<WORD>(mpeg2dec->sequence.width >> 4);
	SliceInfo.wQuantizerScaleCode = quantizer_scale_code;
	SliceInfo.wBadSliceChopping = 0;

	m_SliceCount++;
	m_SliceDataSize += 4 + bytes;
}

void CMpeg2DecoderDXVA2::SliceHook(mpeg2dec_t *mpeg2dec, int code, const uint8_t *buffer, int bytes)
{
	static_cast<CMpeg2DecoderDXVA2 *>(mpeg2dec->client_data)->Slice(mpeg2dec, code, buffer, bytes);
}

int CMpeg2DecoderDXVA2::GetFBufIndex(const mpeg2_fbuf_t *fbuf) const
{
	for (int i = 0; i < 3; i++) {
		if (fbuf == &m_pDec->fbuf_alloc[i].fbuf)
			return i;
	}
	return -1;
}

int CMpeg2DecoderDXVA2::GetFBufSampleID(const mpeg2_fbuf_t *fbuf) const
{
	int Index = GetFBufIndex(fbuf);

	if (Index >= 0)
		return m_Samples[Index].SurfaceID;
	return -1;
}

HRESULT CMpeg2DecoderDXVA2::GetDisplaySample(IMediaSample **ppSample)
{
	HRESULT hr = S_FALSE;

	if (m_fWaitForDisplayKeyFrame) {
		if (m_pDec->info.display_picture
				&& (m_pDec->info.display_picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_I) {
			m_fWaitForDisplayKeyFrame = false;
		}
	}

	if (ppSample) {
		if (!m_fWaitForDisplayKeyFrame) {
			const int DisplaySampleIndex = GetFBufIndex(m_pDec->info.display_fbuf);
			if (DisplaySampleIndex >= 0) {
				*ppSample = m_Samples[DisplaySampleIndex].pSample;
				if (*ppSample) {
					m_Samples[DisplaySampleIndex].pSample = nullptr;
					hr = S_OK;
				}
			}
		}
	}

	return hr;
}
