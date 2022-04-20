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
#include <d3d9.h>
#include <dxva.h>
#include <dxva2api.h>
#include "Mpeg2Decoder.h"
#include "COMUtil.h"


class CTVTestVideoDecoder;
class CD3D11Allocator;
class CD3D11MediaSample;

class CMpeg2DecoderD3D11 : public CMpeg2Decoder
{
public:
	static constexpr UINT MAX_QUEUE_FRAMES = 4;
	static constexpr UINT DEFAULT_QUEUE_FRAMES = 2;

	CMpeg2DecoderD3D11();
	~CMpeg2DecoderD3D11();
	bool Open() override;
	void Close() override;
	mpeg2_state_t Parse() override;
	HRESULT CreateDevice(CTVTestVideoDecoder *pFilter);
	void CloseDevice();
	bool IsDeviceCreated() const { return !!m_Device; }
	HRESULT CreateDecoder();
	void CloseDecoder();
	bool IsDecoderCreated() const { return !!m_VideoDecoder; }
	HRESULT RecreateDecoder();
	void ResetDecoding();
	HRESULT DecodeFrame(CFrameBuffer *pFrameBuffer);
	void UnlockFrame(CFrameBuffer *pFrameBuffer);
	HRESULT GetQueuedFrame(CFrameBuffer *pFrameBuffer);
	void ClearFrameQueue();
	UINT GetFrameQueueSize() const { return m_FrameQueueSize; }
	bool SetFrameQueueSize(UINT Size);
	bool GetOutputTextureSize(SIZE *pSize) const;
	DXGI_FORMAT GetTextureFormat() const { return m_TextureFormat; }
	bool IsDeviceLost() const { return m_fDeviceLost; }
	const DXGI_ADAPTER_DESC &GetAdapterDesc() const { return m_AdapterDesc; }

private:
	struct SampleInfo
	{
		COMPointer<CD3D11MediaSample> Sample;
		UINT ArraySlice = ~0U;
	};

	static constexpr int MAX_SLICE = 175;

	CTVTestVideoDecoder *m_pFilter;
	COMPointer<ID3D11Device> m_Device;
	COMPointer<ID3D11DeviceContext> m_DeviceContext;
	COMPointer<ID3D11VideoDecoder> m_VideoDecoder;
	DXGI_FORMAT m_TextureFormat;
	GUID m_ProfileID;
	HMODULE m_hD3D11Lib;
	HMODULE m_hDXGILib;
	DXGI_ADAPTER_DESC m_AdapterDesc;
	COMPointer<CD3D11Allocator> m_Allocator;
	COMPointer<ID3D11Texture2D> m_StagingTexture;

	bool m_fDeviceLost;
	DXVA_PictureParameters m_PictureParams;
	DXVA_SliceInfo m_SliceInfo[MAX_SLICE];
	DXVA_QmatrixData m_Qmatrix;
	uint8_t *m_pSliceBuffer;
	size_t m_SliceBufferSize;
	size_t m_SliceDataSize;
	int m_SliceCount;
	bool m_fWaitForDecodeKeyFrame;
	bool m_fWaitForDisplayKeyFrame;
	int m_CurSurfaceIndex;
	int m_PrevRefSurfaceIndex;
	int m_ForwardRefSurfaceIndex;
	int m_DecodeSampleIndex;
	SampleInfo m_Samples[3];
	SampleInfo m_RefSamples[2];
	CFrameBuffer m_FrameQueue[MAX_QUEUE_FRAMES + 1];
	UINT m_FrameQueueSize;
	UINT m_FrameQueuePos;

	HRESULT CommitBuffers(ID3D11VideoContext *pVideoContext);
	void GetPictureParams(DXVA_PictureParameters *pParams);
	void GetQmatrixData(DXVA_QmatrixData *pQmatrix);
	void Slice(mpeg2dec_t *mpeg2dec, int code, const uint8_t *buffer, int bytes);
	static void SliceHook(mpeg2dec_t *mpeg2dec, int code, const uint8_t *buffer, int bytes);
	int GetFBufIndex(const mpeg2_fbuf_t *fbuf) const;
	int GetFBufSampleID(const mpeg2_fbuf_t *fbuf) const;
	HRESULT GetDisplayFrame(CFrameBuffer *pFrameBuffer);
	HRESULT CopySampleTextureToStagingTexture(
		ID3D11Texture2D *pTexture, UINT ArraySlice, ID3D11Texture2D **ppStagingTexture);
	HRESULT GetFrameFromTexture(CFrameBuffer *pFrameBuffer, ID3D11Texture2D *pTexture);

	friend class CD3D11Allocator;
};
