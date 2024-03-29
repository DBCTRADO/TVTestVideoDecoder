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


#include <d3d9.h>
#include <dxva.h>
#include <dxva2api.h>
#include <mfapi.h>
#include "Mpeg2Decoder.h"
#include "COMUtil.h"


class CTVTestVideoDecoder;
class CDXVA2MediaSample;

class CMpeg2DecoderDXVA2 : public CMpeg2Decoder
{
public:
	CMpeg2DecoderDXVA2();
	~CMpeg2DecoderDXVA2();
	bool Open() override;
	void Close() override;
	mpeg2_state_t Parse() override;
	HRESULT CreateDecoderService(CTVTestVideoDecoder *pFilter);
	void CloseDecoderService();
	HRESULT CreateDecoder(IDirect3DSurface9 **ppSurface, int SurfaceCount);
	void CloseDecoder();
	bool IsDecoderCreated() const { return !!m_VideoDecoder; }
	HRESULT RecreateDecoder();
	void ResetDecoding();
	HRESULT DecodeFrame(IMediaSample **ppSample);
	D3DFORMAT GetSurfaceFormat() const { return m_SurfaceFormat; }
	bool IsDeviceLost() const { return m_fDeviceLost; }
	const D3DADAPTER_IDENTIFIER9 &GetAdapterIdentifier() const { return m_AdapterIdentifier; }

private:
	struct SampleInfo
	{
		COMPointer<CDXVA2MediaSample> Sample;
		int SurfaceID = -1;
	};

	static const int MAX_SLICE = 175;

	CTVTestVideoDecoder *m_pFilter;
	COMPointer<IDirect3DDeviceManager9> m_DeviceManager;
	COMPointer<IDirectXVideoDecoderService> m_DecoderService;
	COMPointer<IDirectXVideoDecoder> m_VideoDecoder;
	D3DFORMAT m_SurfaceFormat;
	bool m_fDeviceLost;
	DXVA_PictureParameters m_PictureParams;
	DXVA_SliceInfo m_SliceInfo[MAX_SLICE];
	DXVA_QmatrixData m_Qmatrix;
	D3DADAPTER_IDENTIFIER9 m_AdapterIdentifier;
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

	bool FindDecoder(GUID *pGuid, D3DFORMAT *pFormat,
					 const D3DFORMAT *pFormatList, int FormatCount);
	int FindRenderTarget(const GUID &guid, const D3DFORMAT *pFormatList, int FormatCount);
	HRESULT CommitBuffers();
	void GetPictureParams(DXVA_PictureParameters *pParams);
	void GetQmatrixData(DXVA_QmatrixData *pQmatrix);
	void Slice(mpeg2dec_t *mpeg2dec, int code, const uint8_t *buffer, int bytes);
	static void SliceHook(mpeg2dec_t *mpeg2dec, int code, const uint8_t *buffer, int bytes);
	int GetFBufIndex(const mpeg2_fbuf_t *fbuf) const;
	int GetFBufSampleID(const mpeg2_fbuf_t *fbuf) const;
	HRESULT GetDisplaySample(IMediaSample **ppSample);
};
