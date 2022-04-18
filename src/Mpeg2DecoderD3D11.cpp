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
#include "Mpeg2DecoderD3D11.h"
#include "TVTestVideoDecoder.h"
#include "D3D11Allocator.h"
#include "Util.h"
#include "libmpeg2/libmpeg2/mpeg2_internal.h"
#include <initguid.h>


CMpeg2DecoderD3D11::CMpeg2DecoderD3D11()
	: m_pFilter(nullptr)
	, m_pDevice(nullptr)
	, m_pDeviceContext(nullptr)
	, m_pVideoDecoder(nullptr)
	, m_TextureFormat(DXGI_FORMAT_UNKNOWN)
	, m_ProfileID(GUID_NULL)
	, m_hD3D11Lib(nullptr)
	, m_hDXGILib(nullptr)
	, m_AdapterDesc()
	, m_pAllocator(nullptr)
	, m_pStagingTexture(nullptr)

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
	, m_FrameQueueSize(DEFAULT_QUEUE_FRAMES)
	, m_FrameQueuePos(0)
{
}

CMpeg2DecoderD3D11::~CMpeg2DecoderD3D11()
{
	Close();
	CloseDevice();
}

bool CMpeg2DecoderD3D11::Open()
{
	Close();

	m_NumThreads = 1;

	if (!CMpeg2Decoder::Open())
		return false;

	mpeg2_set_client_data(m_pDec, this);
	mpeg2_slice_hook(m_pDec, SliceHook);

	return true;
}

void CMpeg2DecoderD3D11::Close()
{
	CMpeg2Decoder::Close();

	if (m_pSliceBuffer) {
		free(m_pSliceBuffer);
		m_pSliceBuffer = nullptr;
	}
	m_SliceBufferSize = 0;
}

mpeg2_state_t CMpeg2DecoderD3D11::Parse()
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

HRESULT CMpeg2DecoderD3D11::CreateDevice(CTVTestVideoDecoder *pFilter)
{
	DBG_TRACE(TEXT("CMpeg2DecoderD3D11::CreateDevice()"));

	if (!pFilter)
		return E_POINTER;

	CloseDevice();

	m_pFilter = pFilter;

	HRESULT hr;

	m_hD3D11Lib = ::LoadLibrary(TEXT("d3d11.dll"));
	if (!m_hD3D11Lib) {
		hr = HRESULT_FROM_WIN32(::GetLastError());
		goto OnFailed;
	}

	m_hDXGILib = ::LoadLibrary(TEXT("dxgi.dll"));
	if (!m_hDXGILib) {
		hr = HRESULT_FROM_WIN32(::GetLastError());
		goto OnFailed;
	}

	{
		auto pD3D11CreateDevice = reinterpret_cast<decltype(D3D11CreateDevice) *>(::GetProcAddress(m_hD3D11Lib, "D3D11CreateDevice"));
		if (!pD3D11CreateDevice) {
			hr = HRESULT_FROM_WIN32(::GetLastError());
			goto OnFailed;
		}
		auto pCreateDXGIFactory1 = reinterpret_cast<decltype(CreateDXGIFactory1) *>(::GetProcAddress(m_hDXGILib, "CreateDXGIFactory1"));
		if (!pCreateDXGIFactory1) {
			hr = HRESULT_FROM_WIN32(::GetLastError());
			goto OnFailed;
		}

		IDXGIFactory1 *pFactory = nullptr;
		hr = pCreateDXGIFactory1(IID_PPV_ARGS(&pFactory));
		if (SUCCEEDED(hr)) {
			IDXGIAdapter *pAdapter = nullptr;
			hr = pFactory->EnumAdapters(0, &pAdapter);
			if (SUCCEEDED(hr)) {
				static const D3D_FEATURE_LEVEL FeatureLevels[] = {
					//D3D_FEATURE_LEVEL_12_1,
					//D3D_FEATURE_LEVEL_12_0,
					D3D_FEATURE_LEVEL_11_1,
					D3D_FEATURE_LEVEL_11_0,
					D3D_FEATURE_LEVEL_10_1,
					D3D_FEATURE_LEVEL_10_0,
				};
				D3D_FEATURE_LEVEL FeatureLevel;
				ID3D11Device *pDevice = nullptr;

				hr = pD3D11CreateDevice(
					pAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
					D3D11_CREATE_DEVICE_VIDEO_SUPPORT
#ifdef _DEBUG
					| D3D11_CREATE_DEVICE_DEBUG
#endif
					,
					FeatureLevels, _countof(FeatureLevels),
					D3D11_SDK_VERSION,
					&pDevice, &FeatureLevel, nullptr);
				if (SUCCEEDED(hr)) {
					DBG_TRACE(TEXT("D3D11 device feature level : %d.%d"), FeatureLevel >> 12, (FeatureLevel & 0xF00) >> 8);

					m_pDevice = pDevice;

					ID3D11DeviceContext *pDeviceContext = nullptr;
					m_pDevice->GetImmediateContext(&pDeviceContext);
					m_pDeviceContext = pDeviceContext;

					ID3D10Multithread *pMultithread = nullptr;
					if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&pMultithread)))) {
						pMultithread->SetMultithreadProtected(TRUE);
						pMultithread->Release();
					}

					pAdapter->GetDesc(&m_AdapterDesc);
				}
#ifdef _DEBUG
				else {
					DBG_TRACE(TEXT("D3D11CreateDevice() failed (%x)"), hr);
				}
#endif

				pAdapter->Release();
			}

			pFactory->Release();
		}
	}

OnFailed:
	if (FAILED(hr)) {
		CloseDevice();
		return hr;
	}

	return S_OK;
}

void CMpeg2DecoderD3D11::CloseDevice()
{
	DBG_TRACE(TEXT("CMpeg2DecoderD3D11::CloseDevice()"));

	CloseDecoder();

	SafeRelease(m_pDeviceContext);
	SafeRelease(m_pDevice);

	if (m_hD3D11Lib) {
		::FreeLibrary(m_hD3D11Lib);
		m_hD3D11Lib = nullptr;
	}

	if (m_hDXGILib) {
		::FreeLibrary(m_hDXGILib);
		m_hDXGILib = nullptr;
	}

	m_pFilter = nullptr;
}

HRESULT CMpeg2DecoderD3D11::CreateDecoder()
{
	DBG_TRACE(TEXT("CMpeg2DecoderD3D11::CreateDecoder()"));

	if (!m_pFilter || !m_pDevice)
		return E_UNEXPECTED;

	CloseDecoder();

	ID3D11VideoDevice *pVideoDevice = nullptr;
	HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pVideoDevice));
	if (FAILED(hr)) {
		return hr;
	}

	const UINT ProfileCount = pVideoDevice->GetVideoDecoderProfileCount();
	GUID ProfileID;
	bool fProfileFound = false;

	for (UINT i = 0; i < ProfileCount; i++) {
		hr = pVideoDevice->GetVideoDecoderProfile(i, &ProfileID);
		if (SUCCEEDED(hr) && ProfileID == D3D11_DECODER_PROFILE_MPEG2_VLD) {
			BOOL fSupported = FALSE;
			hr = pVideoDevice->CheckVideoDecoderFormat(&ProfileID, DXGI_FORMAT_NV12, &fSupported);
			if (SUCCEEDED(hr) && fSupported) {
				fProfileFound = true;
				break;
			}
		}
	}

	if (!fProfileFound) {
		DBG_TRACE(TEXT("Video decoder profile not found"));
		hr = E_FAIL;
	} else {
		D3D11_VIDEO_DECODER_DESC DecoderDesc = {};
		DecoderDesc.Guid = ProfileID;
		DecoderDesc.SampleWidth = m_pFilter->m_Dimensions.Width;
		DecoderDesc.SampleHeight = m_pFilter->m_Dimensions.Height;
		DecoderDesc.OutputFormat = DXGI_FORMAT_NV12;

		UINT ConfigCount = 0;
		hr = pVideoDevice->GetVideoDecoderConfigCount(&DecoderDesc, &ConfigCount);
		if (SUCCEEDED(hr)) {
			D3D11_VIDEO_DECODER_CONFIG DecoderConfig;
			int SelConfig = -1;

			for (UINT i = 0; i < ConfigCount; i++) {
				hr = pVideoDevice->GetVideoDecoderConfig(&DecoderDesc, i, &DecoderConfig);
				if (DecoderConfig.ConfigBitstreamRaw == 1) {
					SelConfig = i;
					break;
				}
			}

			if (SelConfig < 0) {
				DBG_TRACE(TEXT("Video decoder config not found"));
				hr = E_FAIL;
			} else {
				ID3D11VideoDecoder *pVideoDecoder = nullptr;
				hr = pVideoDevice->CreateVideoDecoder(&DecoderDesc, &DecoderConfig, &pVideoDecoder);
				if (SUCCEEDED(hr)) {
					m_pVideoDecoder = pVideoDecoder;
					m_TextureFormat = DecoderDesc.OutputFormat;
					m_ProfileID = ProfileID;
				}
#ifdef _DEBUG
				else {
					DBG_TRACE(TEXT("ID3D11VideoDevice::CreateVideoDecoder() failed (%x)"), hr);
				}
#endif
			}
		}
	}

	pVideoDevice->Release();

	return hr;
}

void CMpeg2DecoderD3D11::CloseDecoder()
{
	DBG_TRACE(TEXT("CMpeg2DecoderD3D11::CloseDecoder()"));

	ResetDecoding();

	SafeRelease(m_pVideoDecoder);
	m_fDeviceLost = false;

	m_TextureFormat = DXGI_FORMAT_UNKNOWN;
	m_ProfileID = GUID_NULL;
}

HRESULT CMpeg2DecoderD3D11::RecreateDecoder()
{
	DBG_TRACE(TEXT("CMpeg2DecoderD3D11::RecreateDecoder()"));

	if (!m_pVideoDecoder || !m_pDevice)
		return E_UNEXPECTED;

	ResetDecoding();

	D3D11_VIDEO_DECODER_DESC DecoderDesc;
	D3D11_VIDEO_DECODER_CONFIG DecoderConfig;
	HRESULT hr = m_pVideoDecoder->GetCreationParameters(&DecoderDesc, &DecoderConfig);
	if (FAILED(hr)) {
		return hr;
	}
	SafeRelease(m_pVideoDecoder);

	ID3D11VideoDevice *pVideoDevice = nullptr;
	hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pVideoDevice));
	if (FAILED(hr)) {
		return hr;
	}

	ID3D11VideoDecoder *pVideoDecoder = nullptr;
	hr = pVideoDevice->CreateVideoDecoder(&DecoderDesc, &DecoderConfig, &pVideoDecoder);
	if (FAILED(hr)) {
		pVideoDevice->Release();
		return hr;
	}

	m_pVideoDecoder = pVideoDecoder;

	pVideoDevice->Release();

	return S_OK;
}

void CMpeg2DecoderD3D11::ResetDecoding()
{
	DBG_TRACE(TEXT("CMpeg2DecoderD3D11::ResetDecoding()"));

	for (auto &e: m_Samples) {
		SafeRelease(e.pSample);
		e.ArraySlice = ~0U;
	}
	for (auto &e: m_RefSamples) {
		SafeRelease(e.pSample);
		e.ArraySlice = ~0U;
	}

	ClearFrameQueue();

	if (m_pAllocator) {
		m_pAllocator->Decommit();
		SafeRelease(m_pAllocator);
	}

	SafeRelease(m_pStagingTexture);

	m_SliceDataSize = 0;
	m_SliceCount = 0;
	m_fWaitForDecodeKeyFrame = true;
	m_fWaitForDisplayKeyFrame = true;
	m_CurSurfaceIndex = -1;
	m_PrevRefSurfaceIndex = -1;
	m_ForwardRefSurfaceIndex = -1;
	m_DecodeSampleIndex = -1;
}

HRESULT CMpeg2DecoderD3D11::DecodeFrame(CFrameBuffer *pFrameBuffer)
{
	if (!m_pDec || !m_pVideoDecoder) {
		return E_UNEXPECTED;
	}

	if (m_pDec->picture->flags & PIC_FLAG_SKIP) {
		return GetDisplayFrame(pFrameBuffer);
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

	HRESULT hr = m_pDevice->GetDeviceRemovedReason();
	if (FAILED(hr)) {
		DBG_TRACE(TEXT("Device lost"));
		m_fDeviceLost = true;
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

	CD3D11MediaSample *pSample = m_Samples[m_DecodeSampleIndex].pSample;

	if (!pSample) {
		if (!m_pAllocator) {
			hr = S_OK;
			m_pAllocator = DNew_nothrow CD3D11Allocator(this, &hr);
			if (!m_pAllocator) {
				return E_OUTOFMEMORY;
			}
			if (FAILED(hr)) {
				SafeDelete(m_pAllocator);
				return hr;
			}
			m_pAllocator->AddRef();

			ALLOCATOR_PROPERTIES RequestProperties, ActualProperties;
			RequestProperties.cBuffers = 8;
			RequestProperties.cbBuffer = 1;
			RequestProperties.cbAlign = 1;
			RequestProperties.cbPrefix = 0;
			hr = m_pAllocator->SetProperties(&RequestProperties, &ActualProperties);
			if (FAILED(hr)) {
				DBG_TRACE(TEXT("CD3D11Allocator::SetProperties() failed (%x)"), hr);
				return hr;
			}

			hr = m_pAllocator->Commit();
			if (FAILED(hr)) {
				DBG_TRACE(TEXT("CD3D11Allocator::Commit() failed (%x)"), hr);
				return hr;
			}
		}

		for (;;) {
			IMediaSample *pMediaSample = nullptr;
			hr = m_pAllocator->GetBuffer(&pMediaSample, nullptr, nullptr, 0);
			if (FAILED(hr)) {
				DBG_TRACE(TEXT("CD3D11Allocator::GetBuffer() failed (%x)"), hr);
				return hr;
			}
			ID3D11MediaSample *pD3D11Sample = nullptr;
			hr = pMediaSample->QueryInterface(IID_PPV_ARGS(&pD3D11Sample));
			pMediaSample->Release();
			if (FAILED(hr)) {
				return hr;
			}
			pSample = static_cast<CD3D11MediaSample*>(pD3D11Sample);
			if (pSample->GetTextureArraySlice() == m_RefSamples[0].ArraySlice) {
				m_RefSamples[0].pSample = pSample;
			} else if (pSample->GetTextureArraySlice() == m_RefSamples[1].ArraySlice) {
				m_RefSamples[1].pSample = pSample;
			} else {
				break;
			}
		}
		m_Samples[m_DecodeSampleIndex].pSample = pSample;
		m_Samples[m_DecodeSampleIndex].ArraySlice = pSample->GetTextureArraySlice();
	}

	m_CurSurfaceIndex = pSample->GetTextureArraySlice();

#ifdef _DEBUG
	if ((m_pDec->picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_P) {
		_ASSERT(m_PrevRefSurfaceIndex >= 0 && m_CurSurfaceIndex != m_PrevRefSurfaceIndex);
	} else if ((m_pDec->picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_B) {
		_ASSERT(m_PrevRefSurfaceIndex >=0
			&& m_CurSurfaceIndex != m_PrevRefSurfaceIndex
			&& m_ForwardRefSurfaceIndex >=0
			&& m_CurSurfaceIndex != m_ForwardRefSurfaceIndex);
	}
#endif

	ID3D11VideoContext *pVideoContext = nullptr;
	hr = m_pDeviceContext->QueryInterface(IID_PPV_ARGS(&pVideoContext));
	if (FAILED(hr)) {
		return hr;
	}

	ID3D11VideoDecoderOutputView *pVideoDecoderOutputView = nullptr;
	hr = pSample->GetVideoDecoderOutputView(&pVideoDecoderOutputView);
	if (FAILED(hr)) {
		pVideoContext->Release();
		return hr;
	}

	int Retry = 0;
	for (;;) {
		hr = pVideoContext->DecoderBeginFrame(m_pVideoDecoder, pVideoDecoderOutputView, 0, nullptr);
		if ((hr != E_PENDING && hr != D3DERR_WASSTILLDRAWING) || Retry >= 50)
			break;
		::Sleep(2);
		Retry++;
	}

	if (SUCCEEDED(hr)) {
		hr = CommitBuffers(pVideoContext);
		if (SUCCEEDED(hr)) {
			D3D11_VIDEO_DECODER_BUFFER_DESC BufferDesc[4];
			const UINT NumMBsInBuffer =
				(m_PictureParams.wPicWidthInMBminus1 + 1) * (m_PictureParams.wPicHeightInMBminus1 + 1);

			::ZeroMemory(BufferDesc, sizeof(BufferDesc));
			BufferDesc[0].BufferType = D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
			BufferDesc[0].DataSize = sizeof(DXVA_PictureParameters);
			BufferDesc[1].BufferType = D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX;
			BufferDesc[1].DataSize = sizeof(DXVA_QmatrixData);
			BufferDesc[2].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
			BufferDesc[2].DataSize = (UINT)m_SliceDataSize;
			BufferDesc[2].NumMBsInBuffer = NumMBsInBuffer;
			BufferDesc[3].BufferType = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
			BufferDesc[3].DataSize = m_SliceCount * sizeof(DXVA_SliceInfo);
			BufferDesc[3].NumMBsInBuffer = NumMBsInBuffer;

			hr = pVideoContext->SubmitDecoderBuffers(m_pVideoDecoder, 4, BufferDesc);
		}

		pVideoContext->DecoderEndFrame(m_pVideoDecoder);

		if (SUCCEEDED(hr)) {
			hr = GetDisplayFrame(pFrameBuffer);
		}
	}

	pVideoContext->Release();

	if ((m_pDec->picture->flags & PIC_MASK_CODING_TYPE) != PIC_FLAG_CODING_TYPE_B
			&& pFrameBuffer) {
		SafeRelease(m_RefSamples[1].pSample);
		m_RefSamples[1] = m_RefSamples[0];
		m_RefSamples[0].pSample = nullptr;
		m_RefSamples[0].ArraySlice = m_CurSurfaceIndex;
	}

	return hr;
}

void CMpeg2DecoderD3D11::UnlockFrame(CFrameBuffer *pFrameBuffer)
{
	if (!m_pDeviceContext)
		return;
	if (!pFrameBuffer->m_pD3D11Texture)
		return;

	m_pDeviceContext->Unmap(pFrameBuffer->m_pD3D11Texture, pFrameBuffer->m_D3D11TextureArraySlice);

	SafeRelease(pFrameBuffer->m_pD3D11Texture);
}

HRESULT CMpeg2DecoderD3D11::GetQueuedFrame(CFrameBuffer *pFrameBuffer)
{
	for (UINT i = 0; i < m_FrameQueueSize; i++) {
		m_FrameQueuePos = (m_FrameQueuePos + 1) % (m_FrameQueueSize + 1);
		CFrameBuffer &Frame = m_FrameQueue[m_FrameQueuePos];

		if (Frame.m_pD3D11Texture) {
			const HRESULT hr = GetFrameFromTexture(pFrameBuffer, Frame.m_pD3D11Texture);
			if (SUCCEEDED(hr)) {
				pFrameBuffer->CopyAttributesFrom(&Frame);
			} else {
				SafeRelease(Frame.m_pD3D11Texture);
			}
			return hr;
		}
	}

	return S_FALSE;
}

void CMpeg2DecoderD3D11::ClearFrameQueue()
{
	for (auto &e : m_FrameQueue) {
		SafeRelease(e.m_pD3D11Texture);
	}

	m_FrameQueuePos = 0;
}

bool CMpeg2DecoderD3D11::SetFrameQueueSize(UINT Size)
{
	if (Size > MAX_QUEUE_FRAMES)
		return false;

	if (m_FrameQueueSize != Size) {
		ClearFrameQueue();
		m_FrameQueueSize = Size;
	}

	return true;
}

bool CMpeg2DecoderD3D11::GetOutputTextureSize(SIZE *pSize) const
{
	int Width, Height;

	if (!GetOutputSize(&Width, &Height)) {
		pSize->cx = 0;
		pSize->cy = 0;
		return false;
	}

	*pSize = GetDXVASurfaceSize(SIZE{Width, Height}, true);

	return true;
}

HRESULT CMpeg2DecoderD3D11::CommitBuffers(ID3D11VideoContext *pVideoContext)
{
	HRESULT hr;
	void *pBuffer;
	UINT BufferSize;

	hr = pVideoContext->GetDecoderBuffer(m_pVideoDecoder, D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, &BufferSize, &pBuffer);
	if (FAILED(hr)) {
		return hr;
	}
	if (BufferSize < sizeof(DXVA_PictureParameters)) {
		return E_FAIL;
	}
	GetPictureParams(&m_PictureParams);
	memcpy(pBuffer, &m_PictureParams, sizeof(DXVA_PictureParameters));
	pVideoContext->ReleaseDecoderBuffer(m_pVideoDecoder, D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS);

	hr = pVideoContext->GetDecoderBuffer(m_pVideoDecoder, D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX, &BufferSize, &pBuffer);
	if (FAILED(hr)) {
		return hr;
	}
	if (BufferSize < sizeof(DXVA_QmatrixData)) {
		return E_FAIL;
	}
	GetQmatrixData(&m_Qmatrix);
	memcpy(pBuffer, &m_Qmatrix, sizeof(DXVA_QmatrixData));
	pVideoContext->ReleaseDecoderBuffer(m_pVideoDecoder, D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX);

	hr = pVideoContext->GetDecoderBuffer(m_pVideoDecoder, D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, &BufferSize, &pBuffer);
	if (FAILED(hr)) {
		return hr;
	}
	if (BufferSize < m_SliceDataSize) {
		return E_FAIL;
	}
	memcpy(pBuffer, m_pSliceBuffer, m_SliceDataSize);
	pVideoContext->ReleaseDecoderBuffer(m_pVideoDecoder, D3D11_VIDEO_DECODER_BUFFER_BITSTREAM);

	hr = pVideoContext->GetDecoderBuffer(m_pVideoDecoder, D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, &BufferSize, &pBuffer);
	if (FAILED(hr)) {
		return hr;
	}
	if (BufferSize < m_SliceCount * sizeof(DXVA_SliceInfo)) {
		return E_FAIL;
	}
	memcpy(pBuffer, m_SliceInfo, m_SliceCount * sizeof(DXVA_SliceInfo));
	pVideoContext->ReleaseDecoderBuffer(m_pVideoDecoder, D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL);

	return S_OK;
}

void CMpeg2DecoderD3D11::GetPictureParams(DXVA_PictureParameters *pParams)
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

void CMpeg2DecoderD3D11::GetQmatrixData(DXVA_QmatrixData *pQmatrix)
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

void CMpeg2DecoderD3D11::Slice(mpeg2dec_t *mpeg2dec, int code, const uint8_t *buffer, int bytes)
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

void CMpeg2DecoderD3D11::SliceHook(mpeg2dec_t *mpeg2dec, int code, const uint8_t *buffer, int bytes)
{
	static_cast<CMpeg2DecoderD3D11 *>(mpeg2dec->client_data)->Slice(mpeg2dec, code, buffer, bytes);
}

int CMpeg2DecoderD3D11::GetFBufIndex(const mpeg2_fbuf_t *fbuf) const
{
	for (int i = 0; i < 3; i++) {
		if (fbuf == &m_pDec->fbuf_alloc[i].fbuf)
			return i;
	}
	return -1;
}

int CMpeg2DecoderD3D11::GetFBufSampleID(const mpeg2_fbuf_t *fbuf) const
{
	int Index = GetFBufIndex(fbuf);

	if (Index >= 0)
		return m_Samples[Index].ArraySlice;
	return -1;
}

HRESULT CMpeg2DecoderD3D11::GetDisplayFrame(CFrameBuffer *pFrameBuffer)
{
	HRESULT hr = S_FALSE;

	if (m_fWaitForDisplayKeyFrame) {
		if (m_pDec->info.display_picture
				&& (m_pDec->info.display_picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_I) {
			m_fWaitForDisplayKeyFrame = false;
		}
	}

	if (pFrameBuffer) {
		if (!m_fWaitForDisplayKeyFrame) {
			const int DisplaySampleIndex = GetFBufIndex(m_pDec->info.display_fbuf);
			if (DisplaySampleIndex >= 0) {
				CD3D11MediaSample *pSample = m_Samples[DisplaySampleIndex].pSample;
				if (pSample) {
					ID3D11Texture2D *pTexture = nullptr;
					UINT ArraySlice;
					hr = pSample->GetTexture(&pTexture, &ArraySlice);
					if (SUCCEEDED(hr)) {
						if (m_FrameQueueSize > 0) {
							CFrameBuffer &QueuedFrame = m_FrameQueue[m_FrameQueuePos];
							QueuedFrame.CopyAttributesFrom(pFrameBuffer);
							CopySampleTextureToStagingTexture(pTexture, ArraySlice, &QueuedFrame.m_pD3D11Texture);
							const UINT NextPos = (m_FrameQueuePos + 1) % (m_FrameQueueSize + 1);
							CFrameBuffer &ReturnFrame = m_FrameQueue[NextPos];
							if (ReturnFrame.m_pD3D11Texture) {
								pFrameBuffer->CopyAttributesFrom(&ReturnFrame);
								hr = GetFrameFromTexture(pFrameBuffer, ReturnFrame.m_pD3D11Texture);
							} else {
								hr = S_FALSE;
							}
							m_FrameQueuePos = NextPos;
						} else {
							hr = CopySampleTextureToStagingTexture(pTexture, ArraySlice, &m_pStagingTexture);
							if (SUCCEEDED(hr))
								hr = GetFrameFromTexture(pFrameBuffer, m_pStagingTexture);
							pTexture->Release();
						}
					}

					pSample->Release();
					m_Samples[DisplaySampleIndex].pSample = nullptr;
				}
			}
		}
	}

	return hr;
}

HRESULT CMpeg2DecoderD3D11::CopySampleTextureToStagingTexture(
	ID3D11Texture2D *pTexture, UINT SrcArraySlice, ID3D11Texture2D **ppStagingTexture)
{
	CheckPointer(pTexture, E_POINTER);

	if (!*ppStagingTexture) {
		D3D11_TEXTURE2D_DESC TextureDesc = {};
		pTexture->GetDesc(&TextureDesc);
		TextureDesc.ArraySize = 1;
		TextureDesc.Usage = D3D11_USAGE_STAGING;
		TextureDesc.BindFlags = 0;
		TextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		TextureDesc.MiscFlags = 0;

		ID3D11Texture2D *pStagingTexture = nullptr;
		const HRESULT hr = m_pDevice->CreateTexture2D(&TextureDesc, nullptr, &pStagingTexture);
		if (FAILED(hr)) {
			DBG_TRACE(TEXT("ID3DDevice::CreateTexture2D() failed (%x)"), hr);
			return hr;
		}
		*ppStagingTexture = pStagingTexture;
	}

	m_pDeviceContext->CopySubresourceRegion(
		*ppStagingTexture, 0, 0, 0, 0, pTexture, SrcArraySlice, nullptr);

	return S_OK;
}

HRESULT CMpeg2DecoderD3D11::GetFrameFromTexture(CFrameBuffer *pFrameBuffer, ID3D11Texture2D *pTexture)
{
	CheckPointer(pFrameBuffer, E_POINTER);
	CheckPointer(pTexture, E_POINTER);

	D3D11_MAPPED_SUBRESOURCE Mapped;
	HRESULT hr = m_pDeviceContext->Map(pTexture, 0, D3D11_MAP_READ, 0, &Mapped);
	if (FAILED(hr)) {
		DBG_TRACE(TEXT("ID3DDeviceContext::Map() failed (%x)"), hr);
		return hr;
	}

	D3D11_TEXTURE2D_DESC TextureDesc;
	pTexture->GetDesc(&TextureDesc);

	pFrameBuffer->m_Width = TextureDesc.Width;
	pFrameBuffer->m_Height = TextureDesc.Height;
	pFrameBuffer->m_Buffer[0] = static_cast<uint8_t *>(Mapped.pData);
	pFrameBuffer->m_Buffer[1] = pFrameBuffer->m_Buffer[0] + Mapped.RowPitch * TextureDesc.Height;
	pFrameBuffer->m_Buffer[2] = pFrameBuffer->m_Buffer[1];
	pFrameBuffer->m_PitchY = Mapped.RowPitch;
	pFrameBuffer->m_PitchC = Mapped.RowPitch;
	pFrameBuffer->m_Subtype = MEDIASUBTYPE_NV12;
	pFrameBuffer->m_pD3D11Texture = pTexture;
	pFrameBuffer->m_pD3D11Texture->AddRef();
	pFrameBuffer->m_D3D11TextureArraySlice = 0;

	return S_OK;
}
