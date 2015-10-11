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
#include <ks.h>
#include <ksmedia.h>
#include <d3d9.h>
#include <dxva.h>
#include <cmath>
#include "TVTestVideoDecoder.h"
#include "TVTestVideoDecoderProp.h"
#include "TVTestVideoDecoderStat.h"
#include "PixelFormatConvert.h"
#include "Util.h"
#include "MediaTypes.h"
#include "resource.h"


#define KEY_EnableDeinterlace L"EnableDeinterlace"
#define KEY_DeinterlaceMethod L"DeinterlaceMethod"
#define KEY_AdaptProgressive  L"AdaptProgressive"
#define KEY_AdaptTelecine     L"AdaptTelecine"
#define KEY_SetInterlacedFlag L"SetInterlacedFlag"
#define KEY_Brightness        L"Brightness"
#define KEY_Contrast          L"Contrast"
#define KEY_Hue               L"Hue"
#define KEY_Saturation        L"Saturation"
#define KEY_NumThreads        L"NumThreads"

#define INVALID_TIME _I64_MIN


static bool RegReadDWORD(HKEY hKey, LPCTSTR pszName, DWORD *pValue)
{
	DWORD Size = sizeof(DWORD);

	return ::RegQueryValueEx(hKey, pszName, nullptr, nullptr, (LPBYTE)pValue, &Size) == ERROR_SUCCESS
		&& Size == sizeof(DWORD);
}

static bool RegWriteDWORD(HKEY hKey, LPCTSTR pszName, DWORD Value)
{
	return ::RegSetValueEx(hKey, pszName, 0, REG_DWORD, (const BYTE*)&Value, sizeof(Value)) == ERROR_SUCCESS;
}


// CTVTestVideoDecoder

CTVTestVideoDecoder::CTVTestVideoDecoder(LPUNKNOWN lpunk, HRESULT* phr, bool fLocal)
	: CBaseVideoFilter(L"TVTestVideoDecoder", lpunk, phr, __uuidof(ITVTestVideoDecoder), 1)
	, m_fWaitForKeyFrame(true)
	, m_fDropFrames(false)
	, m_fLocalInstance(fLocal)
	, m_fEnableDeinterlace(true)
	, m_DeinterlaceMethod(TVTVIDEODEC_DEINTERLACE_BLEND)
	, m_fAdaptProgressive(true)
	, m_fAdaptTelecine(true)
	, m_fSetInterlacedFlag(true)
	, m_fDXVADeinterlace(false)
	, m_Brightness(0)
	, m_Contrast(0)
	, m_Hue(0)
	, m_Saturation(0)
	, m_NumThreads(0)
	, m_fCrop1088To1080(true)
	, m_pFrameCapture(nullptr)
{
	m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_WEAVE] = &m_Deinterlacer_Weave;
	m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_BLEND] = &m_Deinterlacer_Blend;
	m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_BOB  ] = &m_Deinterlacer_Bob;
	m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_ELA  ] = &m_Deinterlacer_ELA;
	m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_YADIF] = &m_Deinterlacer_Yadif;

	m_RateChange.StartTime = 0;
	m_RateChange.Rate = 10000;

	::ZeroMemory(&m_Statistics, sizeof(m_Statistics));

	if (FAILED(*phr)) {
		return;
	}

	m_pInput = DNew_nothrow CMpeg2DecoderInputPin(this, phr, L"VideoInput");
	if (!m_pInput) {
		*phr = E_OUTOFMEMORY;
	}
	if (FAILED(*phr)) {
		return;
	}

	if (!m_fLocalInstance) {
		LoadOptions();
	}

	/*
	if (m_fDXVADeinterlace) {
		m_fDXVAConnect = true;
	}
	*/
}

CTVTestVideoDecoder::~CTVTestVideoDecoder()
{
	SafeRelease(m_pFrameCapture);
}

CUnknown * CALLBACK CTVTestVideoDecoder::CreateInstance(LPUNKNOWN punk, HRESULT *phr)
{
	CTVTestVideoDecoder *pNewObject = DNew_nothrow CTVTestVideoDecoder(punk, phr);

	if (!pNewObject) {
		*phr = E_OUTOFMEMORY;
	}

	return pNewObject;
}

STDMETHODIMP CTVTestVideoDecoder::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
	if (riid == __uuidof(ITVTestVideoDecoder))
		return GetInterface(static_cast<ITVTestVideoDecoder*>(this), ppv);
	if (riid == __uuidof(ISpecifyPropertyPages))
		return GetInterface(static_cast<ISpecifyPropertyPages*>(this), ppv);
	if (riid == __uuidof(ISpecifyPropertyPages2))
		return GetInterface(static_cast<ISpecifyPropertyPages2*>(this), ppv);

	return __super::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CTVTestVideoDecoder::EndOfStream()
{
	CAutoLock Lock(&m_csReceive);
	return __super::EndOfStream();
}

HRESULT CTVTestVideoDecoder::BeginFlush()
{
	return __super::BeginFlush();
}

HRESULT CTVTestVideoDecoder::EndFlush()
{
	return __super::EndFlush();
}

HRESULT CTVTestVideoDecoder::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
	CAutoLock Lock(&m_csReceive);
	m_fDropFrames = false;
	return __super::NewSegment(tStart, tStop, dRate);
}

void CTVTestVideoDecoder::GetOutputFormatList(OutputFormatList *pFormatList) const
{
	static const OutputFormatInfo Formats[] = {
		{&MEDIASUBTYPE_NV12, 3, 12, FOURCC_NV12},
		{&MEDIASUBTYPE_YV12, 3, 12, FOURCC_YV12},
		{&MEDIASUBTYPE_I420, 3, 12, FOURCC_I420},
		{&MEDIASUBTYPE_IYUV, 3, 12, FOURCC_IYUV},
	};

	pFormatList->pFormats = Formats;
	pFormatList->FormatCount = _countof(Formats);
}

void CTVTestVideoDecoder::InitDecode(bool fPutSequenceHeader)
{
	CAutoLock Lock(&m_csReceive);

	m_Decoder.Close();
	m_Decoder.SetNumThreads(m_NumThreads);
	m_Decoder.Open();

	if (fPutSequenceHeader) {
		const CMediaType &mt = m_pInput->CurrentMediaType();
		const BYTE *pSequenceHeader = nullptr;
		DWORD cbSequenceHeader = 0;

		if (mt.formattype == FORMAT_MPEGVideo) {
			const MPEG1VIDEOINFO *pmpg1vi = (const MPEG1VIDEOINFO*)mt.Format();
			pSequenceHeader = pmpg1vi->bSequenceHeader;
			cbSequenceHeader = pmpg1vi->cbSequenceHeader;
		} else if (mt.formattype == FORMAT_MPEG2_VIDEO) {
			const MPEG2VIDEOINFO *pmpg2vi = (const MPEG2VIDEOINFO*)mt.Format();
			pSequenceHeader = (const BYTE*)pmpg2vi->dwSequenceHeader;
			cbSequenceHeader = pmpg2vi->cbSequenceHeader;
		}

		if (pSequenceHeader && cbSequenceHeader) {
			m_Decoder.PutBuffer(pSequenceHeader, cbSequenceHeader);
		}
	}

	for (int i = 0; i < _countof(m_PictureStatus); i++) {
		m_PictureStatus[i].rtStart = INVALID_TIME;
	}

	m_fWaitForKeyFrame = true;

	m_fTelecineMode = false;
	m_fFilm = false;
	m_RecentFrameTypes = 0;
	m_LastFilmFrame = 0;
	m_FrameCount = 0;

	m_FrameBuffer.m_rtStart = INVALID_TIME;
	m_FrameBuffer.m_rtStop = INVALID_TIME;
	m_FrameBuffer.m_Flags = 0;
	m_FrameBuffer.m_Deinterlace = TVTVIDEODEC_DEINTERLACE_WEAVE;

	InitDeinterlacers();
}

void CTVTestVideoDecoder::InitDeinterlacers()
{
	for (int i = 0; i < _countof(m_Deinterlacers); i++) {
		m_Deinterlacers[i]->Initialize();
	}
}

void CTVTestVideoDecoder::SetFrameStatus()
{
	const uint32_t OldFlags = m_FrameBuffer.m_Flags;
	uint32_t NewFlags;

	if (!m_Decoder.GetFrameFlags(&NewFlags))
		return;

	if (!(NewFlags & FRAME_FLAG_PROGRESSIVE_SEQUENCE)
			&& !(OldFlags & FRAME_FLAG_REPEAT_FIRST_FIELD)) {
		if (!m_fFilm
				&& (NewFlags & FRAME_FLAG_REPEAT_FIRST_FIELD)
				&& (NewFlags & FRAME_FLAG_PROGRESSIVE_FRAME)) {
			m_fFilm = true;
		} else if (m_fFilm && !(NewFlags & FRAME_FLAG_REPEAT_FIRST_FIELD)) {
			m_fFilm = false;
		}
	}

	bool fTelecineMode = false;
	m_RecentFrameTypes >>= 1;
	if (m_fFilm) {
		m_RecentFrameTypes |= 1 << 30;
		m_LastFilmFrame = m_FrameCount;
		if (PopCount(m_RecentFrameTypes) >= 10) {
			fTelecineMode = true;
		}
	}
	if (!m_fTelecineMode && fTelecineMode) {
		TRACE(TEXT("Enter telecine mode\n"));
		m_fTelecineMode = true;
	} else if (m_fTelecineMode && !fTelecineMode && m_FrameCount - m_LastFilmFrame >= 30 * 6) {
		TRACE(TEXT("Exit telecine mode\n"));
		m_fTelecineMode = false;
	}
	m_FrameCount++;

	TVTVIDEODEC_DeinterlaceMethod Deinterlace;
	const CMediaType &mt = m_pOutput->CurrentMediaType();

	if (IsMediaTypeInterlaced(&mt)) {
		Deinterlace = TVTVIDEODEC_DEINTERLACE_WEAVE;
	} else {
		Deinterlace = m_DeinterlaceMethod;

		if (Deinterlace != TVTVIDEODEC_DEINTERLACE_WEAVE) {
			if ((NewFlags & FRAME_FLAG_PROGRESSIVE_SEQUENCE) && m_fAdaptProgressive) {
				Deinterlace = TVTVIDEODEC_DEINTERLACE_WEAVE;
			} else if (m_fTelecineMode && m_fAdaptTelecine) {
				if (m_fFilm) {
					Deinterlace = TVTVIDEODEC_DEINTERLACE_WEAVE;
				} else {
					Deinterlace = TVTVIDEODEC_DEINTERLACE_BLEND;
				}
			}
		}
	}

	if (Deinterlace != m_FrameBuffer.m_Deinterlace) {
		m_Deinterlacers[Deinterlace]->Initialize();
		m_FrameBuffer.m_Deinterlace = Deinterlace;
	}

	m_FrameBuffer.m_Flags = NewFlags;
}

void CTVTestVideoDecoder::SetTypeSpecificFlags(IMediaSample *pSample)
{
	IMediaSample2 *pSample2;

	if (SUCCEEDED(pSample->QueryInterface(IID_PPV_ARGS(&pSample2)))) {
		AM_SAMPLE2_PROPERTIES props;

		if (SUCCEEDED(pSample2->GetProperties(sizeof(props), (BYTE*)&props))) {
			props.dwTypeSpecificFlags &= ~0x7f;

			const CMediaType &mt = m_pOutput->CurrentMediaType();

			if (IsMediaTypeInterlaced(&mt)) {
				if (m_FrameBuffer.m_Flags & FRAME_FLAG_PROGRESSIVE_SEQUENCE) {
					props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_WEAVE;
				}

				if (m_FrameBuffer.m_Flags & FRAME_FLAG_TOP_FIELD_FIRST) {
					props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_FIELD1FIRST;
				}
				if (m_FrameBuffer.m_Flags & FRAME_FLAG_REPEAT_FIRST_FIELD) {
					props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_REPEAT_FIELD;
				}

				if (m_FrameBuffer.m_Flags & FRAME_FLAG_I_FRAME) {
					props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_I_SAMPLE;
				}
				if (m_FrameBuffer.m_Flags & FRAME_FLAG_P_FRAME) {
					props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_P_SAMPLE;
				}
				if (m_FrameBuffer.m_Flags & FRAME_FLAG_B_FRAME) {
					props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_B_SAMPLE;
				}
			}

			pSample2->SetProperties(sizeof(props), (BYTE*)&props);
		}

		pSample2->Release();
	}
}

DWORD CTVTestVideoDecoder::GetVideoInfoControlFlags() const
{
	DWORD Flags = AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT;
	DXVA_ExtendedFormat *fmt = (DXVA_ExtendedFormat*)&Flags;

	fmt->VideoChromaSubsampling = DXVA_VideoChromaSubsampling_MPEG2;
//	fmt->NominalRange = m_fFullRange ? DXVA_NominalRange_0_255 : DXVA_NominalRange_16_235;
	fmt->NominalRange = DXVA_NominalRange_16_235;
#if 0
	fmt->VideoTransferMatrix = DXVA_VideoTransferMatrix_Unknown;
	fmt->VideoPrimaries = DXVA_VideoPrimaries_Unknown;
	fmt->VideoTransferFunction = DXVA_VideoTransFunc_Unknown;
#else
	fmt->VideoTransferMatrix = DXVA_VideoTransferMatrix_BT709;
	fmt->VideoPrimaries = DXVA_VideoPrimaries_BT709;
	fmt->VideoTransferFunction = DXVA_VideoTransFunc_22_709;
#endif

	const mpeg2_info_t *pInfo = m_Decoder.GetMpeg2Info();

	if (pInfo) {
		const mpeg2_sequence_t *sequence = pInfo->sequence;

		if (sequence->flags & SEQ_FLAG_COLOUR_DESCRIPTION) {
			switch (sequence->colour_primaries) {
			case 1:
				fmt->VideoPrimaries = DXVA_VideoPrimaries_BT709;
				break;
			case 4:
				fmt->VideoPrimaries = DXVA_VideoPrimaries_BT470_2_SysM;
				break;
			case 5:
				fmt->VideoPrimaries = DXVA_VideoPrimaries_BT470_2_SysBG;
				break;
			case 6:
				fmt->VideoPrimaries = DXVA_VideoPrimaries_SMPTE170M;
				break;
			case 7:
				fmt->VideoPrimaries = DXVA_VideoPrimaries_SMPTE240M;
				break;
			}

			switch (sequence->matrix_coefficients) {
			case 1:
				fmt->VideoTransferMatrix = DXVA_VideoTransferMatrix_BT709;
				break;
			case 5:
			case 6:
				fmt->VideoTransferMatrix = DXVA_VideoTransferMatrix_BT601;
				break;
			case 7:
				fmt->VideoTransferMatrix = DXVA_VideoTransferMatrix_SMPTE240M;
				break;
			}

			switch (sequence->transfer_characteristics) {
			case 1:
				fmt->VideoTransferFunction = DXVA_VideoTransFunc_22_709;
				break;
			case 4:
				fmt->VideoTransferFunction = DXVA_VideoTransFunc_22;
				break;
			case 5:
				fmt->VideoTransferFunction = DXVA_VideoTransFunc_28;
				break;
			case 6:
				fmt->VideoTransferFunction = DXVA_VideoTransFunc_22_709;
				break;
			case 7:
				fmt->VideoTransferFunction = DXVA_VideoTransFunc_22_240M;
				break;
			case 8:
				fmt->VideoTransferFunction = DXVA_VideoTransFunc_10;
				break;
			}
		}
	}

	return Flags;
}

void CTVTestVideoDecoder::GetOutputSize(
	int *pWidth, int *pHeight, int *pAspectX, int *pAspectY, int *pRealWidth, int *pRealHeight)
{
	if (m_Decoder.GetOutputSize(pWidth, pHeight)) {
		if (m_fCrop1088To1080 && *pHeight == 1088) {
			*pHeight = 1080;
		}
	}
}

bool CTVTestVideoDecoder::IsVideoInterlaced()
{
	return !GetEnableDeinterlace() && GetInterlacedFlag();
}

HRESULT CTVTestVideoDecoder::OnDXVAConnect(IPin *pPin)
{
	m_Deinterlacer_DXVA.Finalize();
	m_Deinterlacer_DXVA.m_pDeviceManager = m_pD3DDeviceManager;
	m_Deinterlacer_DXVA.m_pDeviceManager->AddRef();
	m_Deinterlacer_DXVA.m_hDevice = m_hDXVADevice;
	m_Deinterlacer_DXVA.Initialize();
	return S_OK;
}

HRESULT CTVTestVideoDecoder::Transform(IMediaSample *pIn)
{
	HRESULT hr;

	if (pIn->IsDiscontinuity() == S_OK) {
		InitDecode(false);
	}

	long DataLength = pIn->GetActualDataLength();
	BYTE *pDataIn;
	hr = pIn->GetPointer(&pDataIn);
	if (FAILED(hr)) {
		return hr;
	}

	REFERENCE_TIME rtStart = INVALID_TIME, rtStop;
	hr = pIn->GetTime(&rtStart, &rtStop);
	if (FAILED(hr)) {
		rtStart = INVALID_TIME;
	}

	const mpeg2_info_t *pInfo = m_Decoder.GetMpeg2Info();

	while (DataLength >= 0) {
		mpeg2_state_t state = m_Decoder.Parse();

		switch (state) {
		case STATE_BUFFER:
			if (DataLength > 0) {
				m_Decoder.PutBuffer(pDataIn, DataLength);
				DataLength = 0;
			} else {
				DataLength = -1;
			}
			break;

		case STATE_SEQUENCE:
			{
				CAutoLock Lock(&m_csStats);

				if (pInfo->sequence->frame_period) {
					m_AvgTimePerFrame = (10LL * pInfo->sequence->frame_period + 13LL) / 27LL;
				} else {
					REFERENCE_TIME rt;
					if (GetAvgTimePerFrame(&m_pInput->CurrentMediaType(), &rt)) {
						m_AvgTimePerFrame = rt;
					} else {
						m_AvgTimePerFrame = 0;
					}
				}
			}
			break;

		case STATE_PICTURE:
			{
				const mpeg2_picture_t *picture = m_Decoder.GetPicture();

				m_PictureStatus[m_Decoder.GetPictureIndex(picture)].rtStart = rtStart;
				rtStart = INVALID_TIME;

				bool fDrop = false;

				{
					CAutoLock Lock(&m_csStats);

					switch (picture->flags & PIC_MASK_CODING_TYPE) {
					case PIC_FLAG_CODING_TYPE_I:
						m_Statistics.IFrameCount++;
						break;
					case PIC_FLAG_CODING_TYPE_P:
						m_Statistics.PFrameCount++;
						break;
					case PIC_FLAG_CODING_TYPE_B:
						m_Statistics.BFrameCount++;
						if (m_fDropFrames) {
							fDrop = true;
							m_Statistics.SkippedFrameCount++;
						}
						break;
					}
				}

				m_Decoder.Skip(pIn->IsPreroll() == S_OK || fDrop);
			}
			break;

		case STATE_SLICE:
		case STATE_END:
			{
				const mpeg2_picture_t *picture = pInfo->display_picture;

				if (picture && !(picture->flags & PIC_FLAG_SKIP) && pInfo->display_fbuf
						&& pInfo->sequence->width == pInfo->sequence->chroma_width * 2
						&& pInfo->sequence->height == pInfo->sequence->chroma_height * 2) {
					int Width = pInfo->sequence->picture_width;
					int Height = pInfo->sequence->picture_height;

					if (m_fCrop1088To1080 && Height == 1088) {
						Height = 1080;
					}
					if (m_FrameBuffer.m_Width != Width || m_FrameBuffer.m_Height != Height) {
						m_FrameBuffer.Allocate(Width, Height);
					}

					m_FrameBuffer.m_rtStart = m_PictureStatus[m_Decoder.GetPictureIndex(picture)].rtStart;
					if (m_FrameBuffer.m_rtStart < 0) {
						m_FrameBuffer.m_rtStart = m_FrameBuffer.m_rtStop;
					}
					if (m_FrameBuffer.m_rtStart >= 0) {
						REFERENCE_TIME Duration = m_AvgTimePerFrame * picture->nb_fields;
						if (!pInfo->display_picture_2nd) {
							Duration /= 2;
						}
						m_FrameBuffer.m_rtStop = m_FrameBuffer.m_rtStart + Duration;
					}

					SetFrameStatus();
					UpdateAspectRatio();

					if (m_FrameBuffer.m_Flags & FRAME_FLAG_I_FRAME) {
						m_fWaitForKeyFrame = false;
					}

					hr = DeliverFrame(&m_FrameBuffer);
					if (FAILED(hr)) {
						TRACE(TEXT("DeliverFrame() failed (%x)\n"), hr);
						return hr;
					}
				}
			}
			break;

		case STATE_INVALID:
			TRACE(TEXT("STATE_INVALID\n"));
			InitDecode(false);
			break;
		}
	}

	return S_OK;
}

void CTVTestVideoDecoder::UpdateAspectRatio()
{
	int AspectX, AspectY;

	if (m_Decoder.GetAspectRatio(&AspectX, &AspectY)) {
		SetAspectRatio(AspectX, AspectY);
	}
}

HRESULT CTVTestVideoDecoder::DeliverFrame(CFrameBuffer *pFrameBuffer)
{
	HRESULT hr;
	CDeinterlacer *pDeinterlacer;

	if (m_fDXVAOutput
			|| (m_fDXVADeinterlace
				&& pFrameBuffer->m_Deinterlace != TVTVIDEODEC_DEINTERLACE_WEAVE)) {
		pDeinterlacer = &m_Deinterlacer_DXVA;
	} else {
		pDeinterlacer = m_Deinterlacers[pFrameBuffer->m_Deinterlace];
	}

	IMediaSample *pOutSample;
	hr = GetDeliveryBuffer(
		&pOutSample, pFrameBuffer->m_Width, pFrameBuffer->m_Height,
		pDeinterlacer->IsDoubleFrame() ? m_AvgTimePerFrame / 2 : m_AvgTimePerFrame,
		IsVideoInterlaced());
	if (FAILED(hr)) {
		return hr;
	}

	CFrameBuffer SrcBuffer, DstBuffer;

	SrcBuffer.m_rtStart = pFrameBuffer->m_rtStart;
	SrcBuffer.m_rtStop = pFrameBuffer->m_rtStop;
	SrcBuffer.m_Flags = pFrameBuffer->m_Flags;

	if (!m_Decoder.GetFrame(&SrcBuffer)) {
		pOutSample->Release();
		return S_FALSE;
	}

	pFrameBuffer->CopyReferenceTo(&DstBuffer);

	hr = SetupOutputFrameBuffer(&DstBuffer, pOutSample, pDeinterlacer);
	if (FAILED(hr)) {
		pOutSample->Release();
		return hr;
	}

	const bool fTopFieldFirst = !!(pFrameBuffer->m_Flags & FRAME_FLAG_TOP_FIELD_FIRST);
	CDeinterlacer::FrameStatus FrameStatus;
	REFERENCE_TIME rtStop = INVALID_TIME;

	FrameStatus = pDeinterlacer->GetFrame(&DstBuffer, &SrcBuffer, fTopFieldFirst);
	if (FrameStatus == CDeinterlacer::FRAME_SKIP) {
		FrameStatus = m_Deinterlacer_Blend.GetFrame(&DstBuffer, &SrcBuffer, fTopFieldFirst);
	}
	if (FrameStatus == CDeinterlacer::FRAME_OK) {
		if (pDeinterlacer->IsDoubleFrame()) {
			if (DstBuffer.m_rtStart >= 0 && DstBuffer.m_rtStop >= 0) {
				rtStop = DstBuffer.m_rtStop;
				DstBuffer.m_rtStop = (DstBuffer.m_rtStart + DstBuffer.m_rtStop) / 2;
			}
		}

		hr = Deliver(pOutSample, &DstBuffer);
		if (FAILED(hr)) {
			goto DeliverEnd;
		}
	}

	pDeinterlacer->FramePostProcess(&DstBuffer, &SrcBuffer, fTopFieldFirst);

	if (pDeinterlacer->IsDoubleFrame()) {
		FrameStatus = pDeinterlacer->GetFrame(&DstBuffer, &SrcBuffer, !fTopFieldFirst);
		if (FrameStatus == CDeinterlacer::FRAME_OK) {
			if (rtStop >= 0) {
				DstBuffer.m_rtStart = DstBuffer.m_rtStop;
				DstBuffer.m_rtStop = rtStop;
			}

			SafeRelease(DstBuffer.m_pSurface);
			pOutSample->Release();
			hr = GetDeliveryBuffer(&pOutSample);
			if (SUCCEEDED(hr)) {
				hr = SetupOutputFrameBuffer(&DstBuffer, pOutSample, pDeinterlacer);
				if (SUCCEEDED(hr)) {
					hr = Deliver(pOutSample, &DstBuffer);
				}
			}
		}
	}

DeliverEnd:
	SafeRelease(DstBuffer.m_pSurface);
	pOutSample->Release();

	return hr;
}

HRESULT CTVTestVideoDecoder::Deliver(IMediaSample *pOutSample, CFrameBuffer *pFrameBuffer)
{
	CAutoLock Lock(&m_csReceive);

	if (pFrameBuffer->m_rtStart < 0 || m_fWaitForKeyFrame) {
		return S_FALSE;
	}

	HRESULT hr;

	if (!m_fDXVAOutput) {
		if (m_pFrameCapture) {
			const BYTE *Buffer[3];
			int Pitch[3];

			Buffer[0] = pFrameBuffer->m_Buffer[0];
			Buffer[1] = pFrameBuffer->m_Buffer[1];
			Buffer[2] = pFrameBuffer->m_Buffer[2];
			Pitch[0] = pFrameBuffer->m_PitchY;
			Pitch[1] = pFrameBuffer->m_PitchC;
			Pitch[2] = pFrameBuffer->m_PitchC;

			DWORD Flags = 0;
			if (pFrameBuffer->m_Flags & FRAME_FLAG_TOP_FIELD_FIRST)
				Flags |= TVTVIDEODEC_FRAME_TOP_FIELD_FIRST;
			if (pFrameBuffer->m_Flags & FRAME_FLAG_REPEAT_FIRST_FIELD)
				Flags |= TVTVIDEODEC_FRAME_REPEAT_FIRST_FIELD;
			if (pFrameBuffer->m_Flags & FRAME_FLAG_PROGRESSIVE_FRAME)
				Flags |= TVTVIDEODEC_FRAME_PROGRESSIVE;
			if (pFrameBuffer->m_Deinterlace == TVTVIDEODEC_DEINTERLACE_WEAVE)
				Flags |= TVTVIDEODEC_FRAME_WEAVE;
			if (pFrameBuffer->m_Flags & FRAME_FLAG_I_FRAME)
				Flags |= TVTVIDEODEC_FRAME_TYPE_I;
			if (pFrameBuffer->m_Flags & FRAME_FLAG_P_FRAME)
				Flags |= TVTVIDEODEC_FRAME_TYPE_P;
			if (pFrameBuffer->m_Flags & FRAME_FLAG_B_FRAME)
				Flags |= TVTVIDEODEC_FRAME_TYPE_B;

			m_pFrameCapture->OnFrame(
				pFrameBuffer->m_Width, pFrameBuffer->m_Height,
				pFrameBuffer->m_Subtype, Buffer, Pitch, Flags);
		}

		if (m_ColorAdjustment.IsEffective()) {
			if (pFrameBuffer->m_Subtype == MEDIASUBTYPE_I420) {
				m_ColorAdjustment.ProcessI420(
					pFrameBuffer->m_Width, pFrameBuffer->m_Height,
					pFrameBuffer->m_Buffer[0], pFrameBuffer->m_Buffer[1], pFrameBuffer->m_Buffer[2],
					pFrameBuffer->m_PitchY, pFrameBuffer->m_PitchC);
			} else if (pFrameBuffer->m_Subtype == MEDIASUBTYPE_NV12) {
				m_ColorAdjustment.ProcessNV12(
					pFrameBuffer->m_Width, pFrameBuffer->m_Height,
					pFrameBuffer->m_Buffer[0], pFrameBuffer->m_Buffer[1], pFrameBuffer->m_PitchY);
			}
		}

		/*
		if (pFrameBuffer->m_Height == 1088) {
			memset(pFrameBuffer->m_Buffer[0] + pFrameBuffer->m_PitchY * 1080, 0x10, pFrameBuffer->m_PitchY * 8);
			if (pFrameBuffer->m_Subtype == MEDIASUBTYPE_I420) {
				memset(pFrameBuffer->m_Buffer[1] + pFrameBuffer->m_PitchC * (1080 / 2), 0x80, pFrameBuffer->m_PitchC * (8 / 2));
				memset(pFrameBuffer->m_Buffer[2] + pFrameBuffer->m_PitchC * (1080 / 2), 0x80, pFrameBuffer->m_PitchC * (8 / 2));
			} else if (pFrameBuffer->m_Subtype == MEDIASUBTYPE_NV12) {
				memset(pFrameBuffer->m_Buffer[1] + pFrameBuffer->m_PitchY * (1080 / 2), 0x80, pFrameBuffer->m_PitchY * (8 / 2));
			}
		}
		*/

		if (!(pFrameBuffer->m_Flags & FRAME_FLAG_SAMPLE_DATA)) {
			BYTE *pOutData;
			hr = pOutSample->GetPointer(&pOutData);
			if (FAILED(hr)) {
				return hr;
			}

			CopySampleBuffer(pOutData, pFrameBuffer, !(pFrameBuffer->m_Flags & FRAME_FLAG_PROGRESSIVE_FRAME));
		}
	}

	REFERENCE_TIME rtStart = pFrameBuffer->m_rtStart;
	REFERENCE_TIME rtStop = pFrameBuffer->m_rtStop;

	CMpeg2DecoderInputPin *pPin = dynamic_cast<CMpeg2DecoderInputPin*>(m_pInput);
	if (pPin) {
		CAutoLock Lock(&pPin->m_csRateLock);

		if (m_RateChange.Rate != pPin->m_RateChange.Rate) {
			m_RateChange.Rate = pPin->m_RateChange.Rate;
			m_RateChange.StartTime = rtStart;
		}
	}

	if (m_RateChange.Rate != 10000) {
		rtStart = m_RateChange.StartTime + (rtStart - m_RateChange.StartTime) * m_RateChange.Rate / 10000;
		rtStop = m_RateChange.StartTime + (rtStop - m_RateChange.StartTime) * m_RateChange.Rate / 10000;
	}

	pOutSample->SetTime(&rtStart, &rtStop);
	pOutSample->SetMediaTime(nullptr, nullptr);

	SetTypeSpecificFlags(pOutSample);

	hr = m_pOutput->Deliver(pOutSample);

	return hr;
}

HRESULT CTVTestVideoDecoder::SetupOutputFrameBuffer(
	CFrameBuffer *pFrameBuffer, IMediaSample *pSample, CDeinterlacer *pDeinterlacer)
{
	HRESULT hr = S_OK;

	if (m_fDXVAOutput) {
		IMFGetService *pMFGetService;

		hr = pSample->QueryInterface(IID_PPV_ARGS(&pMFGetService));
		if (SUCCEEDED(hr)) {
			IDirect3DSurface9 *pSurface;
			hr = pMFGetService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface));
			if (SUCCEEDED(hr)) {
				pFrameBuffer->m_pSurface = pSurface;
			}
			pMFGetService->Release();
		}
	} else {
		const CMediaType &mt = m_pOutput->CurrentMediaType();

		if (!pDeinterlacer->IsRetainFrame()
				&& pDeinterlacer->IsFormatSupported(pFrameBuffer->m_Subtype, mt.subtype)
				&& !m_ColorAdjustment.IsEffective()
				&& !m_pFrameCapture) {
			BITMAPINFOHEADER bmihOut;
			BYTE *pOutData;

			if (GetBitmapInfoHeader(&mt, &bmihOut)
					&& !(bmihOut.biWidth & 15) && bmihOut.biHeight < 0
					&& SUCCEEDED(pSample->GetPointer(&pOutData))
					&& !((uintptr_t)pOutData & 15)) {
				int PlaneSize = bmihOut.biWidth * -bmihOut.biHeight;

				pFrameBuffer->m_Buffer[0] = pOutData;
				pFrameBuffer->m_Buffer[1] = pOutData + PlaneSize;
				pFrameBuffer->m_PitchY = bmihOut.biWidth;
				if (mt.subtype == MEDIASUBTYPE_NV12) {
					pFrameBuffer->m_Buffer[2] = pFrameBuffer->m_Buffer[1];
					pFrameBuffer->m_PitchC = pFrameBuffer->m_PitchY;
				} else {
					pFrameBuffer->m_Buffer[2] = pFrameBuffer->m_Buffer[1] + (PlaneSize >> 2);
					pFrameBuffer->m_PitchC = bmihOut.biWidth >> 1;
				}
				pFrameBuffer->m_Subtype = mt.subtype;
				pFrameBuffer->m_Flags |= FRAME_FLAG_SAMPLE_DATA;
			}
		}
	}

	return hr;
}

HRESULT CTVTestVideoDecoder::CheckInputType(const CMediaType *mtIn)
{
	if (mtIn->formattype == FORMAT_MPEG2_VIDEO
			&& mtIn->pbFormat && mtIn->cbFormat >= sizeof(MPEG2VIDEOINFO)) {
		const MPEG2VIDEOINFO *pmp2vi = (const MPEG2VIDEOINFO*)mtIn->pbFormat;

		if (pmp2vi->cbSequenceHeader >= 4) {
			if ((pmp2vi->dwSequenceHeader[0] & 0x00ffffff) != 0x00010000) {
				return VFW_E_TYPE_NOT_ACCEPTED;
			}

			const BYTE *p = (const BYTE*)pmp2vi->dwSequenceHeader;
			DWORD Sync = 0xffffffff;

			for (DWORD Size = pmp2vi->cbSequenceHeader; Size > 0; Size--) {
				Sync = (Sync << 8) | (*p++);
				if ((Sync & 0xffffff00) == 0x00000100) {
					if ((Sync & 0xff) == 0xb5) {	// sequence extension
						BYTE ChromaFormat = (p[1] & 0x06) >> 1;
						if (ChromaFormat >= 2) {	// 4:2:0 only
							return VFW_E_TYPE_NOT_ACCEPTED;
						}
						break;
					}
				}
			}
		}
	}

	if ((mtIn->majortype == MEDIATYPE_MPEG2_PACK && mtIn->subtype == MEDIASUBTYPE_MPEG2_VIDEO)
			|| (mtIn->majortype == MEDIATYPE_MPEG2_PES && mtIn->subtype == MEDIASUBTYPE_MPEG2_VIDEO)
			|| (mtIn->majortype == MEDIATYPE_Video && mtIn->subtype == MEDIASUBTYPE_MPEG2_VIDEO)
			|| (mtIn->majortype == MEDIATYPE_Video && mtIn->subtype == MEDIASUBTYPE_MPG2)) {
		return S_OK;
	}

	return VFW_E_TYPE_NOT_ACCEPTED;
}

HRESULT CTVTestVideoDecoder::CompleteConnect(PIN_DIRECTION direction, IPin *pReceivePin)
{
	HRESULT hr = __super::CompleteConnect(direction, pReceivePin);
	if (FAILED(hr)) {
		return hr;
	}

	if (direction == PINDIR_OUTPUT) {
		if (m_fDXVADeinterlace && !m_fDXVAConnect) {
			m_Deinterlacer_DXVA.Initialize();
		}
	}

	return S_OK;
}

HRESULT CTVTestVideoDecoder::StartStreaming()
{
	HRESULT hr = __super::StartStreaming();
	if (FAILED(hr)) {
		return hr;
	}

	InitDecode(true);

	return S_OK;
}

HRESULT CTVTestVideoDecoder::StopStreaming()
{
	m_Decoder.Close();

	return __super::StopStreaming();
}

HRESULT CTVTestVideoDecoder::AlterQuality(Quality q)
{
	//TRACE(TEXT("AlterQuality() Proportion %ld Late %lld\n"), q.Proportion, q.Late);
	if (q.Late > 100 * 10000LL) {
		m_fDropFrames = true;
	} else if (q.Late <= 0) {
		m_fDropFrames = false;
	}

	return S_OK;
}

// ISpecifyPropertyPages

STDMETHODIMP CTVTestVideoDecoder::GetPages(CAUUID *pPages)
{
	CheckPointer(pPages, E_POINTER);

	pPages->cElems = 2;
	pPages->pElems = (GUID*)::CoTaskMemAlloc(2 * sizeof(GUID));
	if (pPages->pElems == nullptr) {
		pPages->cElems = 0;
		return E_OUTOFMEMORY;
	}
	pPages->pElems[0] = __uuidof(CTVTestVideoDecoderProp);
	pPages->pElems[1] = __uuidof(CTVTestVideoDecoderStat);

	return S_OK;
}

// ISpecifyPropertyPages2

STDMETHODIMP CTVTestVideoDecoder::CreatePage(const GUID &guid, IPropertyPage **ppPage)
{
	CheckPointer(ppPage, E_POINTER);

	HRESULT hr = S_OK;

	if (guid == __uuidof(CTVTestVideoDecoderProp)) {
		*ppPage = DNew_nothrow CTVTestVideoDecoderProp(nullptr, &hr);
	} else if (guid == __uuidof(CTVTestVideoDecoderStat)) {
		*ppPage = DNew_nothrow CTVTestVideoDecoderStat(nullptr, &hr);
	} else {
		*ppPage = nullptr;
		return E_INVALIDARG;
	}

	if (*ppPage == nullptr) {
		return E_OUTOFMEMORY;
	}

	if (FAILED(hr)) {
		SafeDelete(*ppPage);
		return hr;
	}

	(*ppPage)->AddRef();

	return S_OK;
}

// ITVTestVideoDecoder

STDMETHODIMP CTVTestVideoDecoder::SetEnableDeinterlace(BOOL fEnable)
{
	CAutoLock Lock(&m_csProps);
	TRACE(TEXT("SetEnableDeinterlace() %d\n"), fEnable);
	m_fEnableDeinterlace = fEnable != FALSE;
	return S_OK;
}

STDMETHODIMP_(BOOL) CTVTestVideoDecoder::GetEnableDeinterlace()
{
	return m_fEnableDeinterlace;
}

STDMETHODIMP CTVTestVideoDecoder::SetDeinterlaceMethod(TVTVIDEODEC_DeinterlaceMethod Method)
{
	if (Method < TVTVIDEODEC_DEINTERLACE_FIRST || Method > TVTVIDEODEC_DEINTERLACE_LAST) {
		return E_INVALIDARG;
	}
	CAutoLock Lock(&m_csProps);
	TRACE(TEXT("SetDeinterlaceMethod() %d\n"), Method);
	m_DeinterlaceMethod = Method;
	return S_OK;
}

STDMETHODIMP_(TVTVIDEODEC_DeinterlaceMethod) CTVTestVideoDecoder::GetDeinterlaceMethod()
{
	return m_DeinterlaceMethod;
}

STDMETHODIMP CTVTestVideoDecoder::SetAdaptProgressive(BOOL fEnable)
{
	CAutoLock Lock(&m_csProps);
	TRACE(TEXT("SetAdaptProgressive() %d\n"), fEnable);
	m_fAdaptProgressive = fEnable != FALSE;
	return S_OK;
}

STDMETHODIMP_(BOOL) CTVTestVideoDecoder::GetAdaptProgressive()
{
	return m_fAdaptProgressive;
}

STDMETHODIMP CTVTestVideoDecoder::SetAdaptTelecine(BOOL fEnable)
{
	CAutoLock Lock(&m_csProps);
	TRACE(TEXT("SetAdaptTelecine() %d\n"), fEnable);
	m_fAdaptTelecine = fEnable != FALSE;
	return S_OK;
}

STDMETHODIMP_(BOOL) CTVTestVideoDecoder::GetAdaptTelecine()
{
	return m_fAdaptTelecine;
}

STDMETHODIMP CTVTestVideoDecoder::SetInterlacedFlag(BOOL fEnable)
{
	CAutoLock Lock(&m_csProps);
	TRACE(TEXT("SetInterlacedFlag() %d\n"), fEnable);
	m_fSetInterlacedFlag = fEnable != FALSE;
	return S_OK;
}

STDMETHODIMP_(BOOL) CTVTestVideoDecoder::GetInterlacedFlag()
{
	return m_fSetInterlacedFlag;
}

STDMETHODIMP CTVTestVideoDecoder::SetBrightness(int Brightness)
{
	if (Brightness < TVTVIDEODEC_BRIGHTNESS_MIN || Brightness > TVTVIDEODEC_BRIGHTNESS_MAX) {
		return E_INVALIDARG;
	}
	CAutoLock Lock(&m_csProps);
	m_Brightness = Brightness;
	m_ColorAdjustment.SetBrightness(Brightness);
	return S_OK;
}

STDMETHODIMP_(int) CTVTestVideoDecoder::GetBrightness()
{
	return m_Brightness;
}

STDMETHODIMP CTVTestVideoDecoder::SetContrast(int Contrast)
{
	if (Contrast < TVTVIDEODEC_CONTRAST_MIN || Contrast > TVTVIDEODEC_CONTRAST_MAX) {
		return E_INVALIDARG;
	}
	CAutoLock Lock(&m_csProps);
	m_Contrast = Contrast;
	m_ColorAdjustment.SetContrast(Contrast);
	return S_OK;
}

STDMETHODIMP_(int) CTVTestVideoDecoder::GetContrast()
{
	return m_Contrast;
}

STDMETHODIMP CTVTestVideoDecoder::SetHue(int Hue)
{
	if (Hue < TVTVIDEODEC_HUE_MIN || Hue > TVTVIDEODEC_HUE_MAX) {
		return E_INVALIDARG;
	}
	CAutoLock Lock(&m_csProps);
	m_Hue = Hue;
	m_ColorAdjustment.SetHue(Hue);
	return S_OK;
}

STDMETHODIMP_(int) CTVTestVideoDecoder::GetHue()
{
	return m_Hue;
}

STDMETHODIMP CTVTestVideoDecoder::SetSaturation(int Saturation)
{
	if (Saturation < TVTVIDEODEC_SATURATION_MIN || Saturation > TVTVIDEODEC_SATURATION_MAX) {
		return E_INVALIDARG;
	}
	CAutoLock Lock(&m_csProps);
	m_Saturation = Saturation;
	m_ColorAdjustment.SetSaturation(Saturation);
	return S_OK;
}

STDMETHODIMP_(int) CTVTestVideoDecoder::GetSaturation()
{
	return m_Saturation;
}

STDMETHODIMP CTVTestVideoDecoder::SetNumThreads(int NumThreads)
{
	if (NumThreads < 0 || NumThreads > TVTVIDEODEC_MAX_THREADS) {
		return E_INVALIDARG;
	}
	CAutoLock Lock(&m_csProps);
	TRACE(TEXT("SetNumThreads() %d\n"), NumThreads);
	m_NumThreads = NumThreads;
	return S_OK;
}

STDMETHODIMP_(int) CTVTestVideoDecoder::GetNumThreads()
{
	return m_NumThreads;
}
		HKEY hKey;

STDMETHODIMP CTVTestVideoDecoder::LoadOptions()
{
	if (m_fLocalInstance) {
		return S_FALSE;
	}

	HKEY hKey;
	LONG Result = ::RegOpenKeyEx(HKEY_CURRENT_USER, REGISTRY_KEY_NAME, 0, KEY_READ, &hKey);
	if (Result != ERROR_SUCCESS) {
		return HRESULT_FROM_WIN32(Result);
	}

	DWORD Value;

	if (RegReadDWORD(hKey, KEY_EnableDeinterlace, &Value))
		SetEnableDeinterlace(Value != 0);
	if (RegReadDWORD(hKey, KEY_DeinterlaceMethod, &Value))
		SetDeinterlaceMethod((TVTVIDEODEC_DeinterlaceMethod)Value);
	if (RegReadDWORD(hKey, KEY_AdaptProgressive, &Value))
		SetAdaptProgressive(Value != 0);
	if (RegReadDWORD(hKey, KEY_AdaptTelecine, &Value))
		SetAdaptTelecine(Value != 0);
	if (RegReadDWORD(hKey, KEY_SetInterlacedFlag, &Value))
		SetInterlacedFlag(Value != 0);
	if (RegReadDWORD(hKey, KEY_Brightness, &Value))
		SetBrightness((int)Value);
	if (RegReadDWORD(hKey, KEY_Contrast, &Value))
		SetContrast((int)Value);
	if (RegReadDWORD(hKey, KEY_Hue, &Value))
		SetHue((int)Value);
	if (RegReadDWORD(hKey, KEY_Saturation, &Value))
		SetSaturation((int)Value);
	if (RegReadDWORD(hKey, KEY_NumThreads, &Value))
		SetNumThreads((int)Value);

	::RegCloseKey(hKey);

	return S_OK;
}

STDMETHODIMP CTVTestVideoDecoder::SaveOptions()
{
	if (m_fLocalInstance) {
		return S_FALSE;
	}

	HKEY hKey;
	LONG Result = ::RegCreateKeyEx(
		HKEY_CURRENT_USER, REGISTRY_KEY_NAME, 0, nullptr, 
		REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
		&hKey, nullptr);
	if (Result != ERROR_SUCCESS) {
		return HRESULT_FROM_WIN32(Result);
	}

	CAutoLock Lock(&m_csProps);

	RegWriteDWORD(hKey, KEY_EnableDeinterlace, m_fEnableDeinterlace);
	RegWriteDWORD(hKey, KEY_DeinterlaceMethod, (DWORD)m_DeinterlaceMethod);
	RegWriteDWORD(hKey, KEY_AdaptProgressive, m_fAdaptProgressive);
	RegWriteDWORD(hKey, KEY_AdaptTelecine, m_fAdaptTelecine);
	RegWriteDWORD(hKey, KEY_SetInterlacedFlag, m_fSetInterlacedFlag);
	RegWriteDWORD(hKey, KEY_Brightness, (DWORD)m_Brightness);
	RegWriteDWORD(hKey, KEY_Contrast, (DWORD)m_Contrast);
	RegWriteDWORD(hKey, KEY_Hue, (DWORD)m_Hue);
	RegWriteDWORD(hKey, KEY_Saturation, (DWORD)m_Saturation);
	RegWriteDWORD(hKey, KEY_NumThreads, (DWORD)m_NumThreads);

	::RegCloseKey(hKey);

	return S_OK;
}

STDMETHODIMP CTVTestVideoDecoder::GetStatistics(TVTVIDEODEC_Statistics *pStatistics)
{
	CheckPointer(pStatistics, E_POINTER);

	CAutoLock Lock(&m_csStats);

	DWORD Mask = pStatistics->Mask;

	if (Mask & TVTVIDEODEC_STAT_OUT_SIZE) {
		pStatistics->OutWidth = m_OutDimensions.Width;
		pStatistics->OutHeight = m_OutDimensions.Height;
		pStatistics->OutAspectX = m_OutDimensions.AspectX;
		pStatistics->OutAspectY = m_OutDimensions.AspectY;
	}

	if (Mask & TVTVIDEODEC_STAT_FRAME_COUNT) {
		pStatistics->IFrameCount = m_Statistics.IFrameCount;
		pStatistics->PFrameCount = m_Statistics.PFrameCount;
		pStatistics->BFrameCount = m_Statistics.BFrameCount;
		pStatistics->SkippedFrameCount = m_Statistics.SkippedFrameCount;
	}

	if (Mask & TVTVIDEODEC_STAT_PLAYBACK_RATE) {
		pStatistics->PlaybackRate = m_RateChange.Rate;
	}

	if (Mask & TVTVIDEODEC_STAT_BASE_TIME_PER_FRAME) {
		pStatistics->BaseTimePerFrame = m_AvgTimePerFrame;
	}

	Mask |= TVTVIDEODEC_STAT_ALL;
	pStatistics->Mask = Mask;

	return S_OK;
}

STDMETHODIMP CTVTestVideoDecoder::SetFrameCapture(ITVTestVideoDecoderFrameCapture *pFrameCapture)
{
	CAutoLock Lock(&m_csReceive);

	if (m_pFrameCapture) {
		m_pFrameCapture->Release();
	}
	m_pFrameCapture = pFrameCapture;
	if (m_pFrameCapture) {
		m_pFrameCapture->AddRef();
	}

	return S_OK;
}


// CMpeg2DecoderInputPin

CMpeg2DecoderInputPin::CMpeg2DecoderInputPin(CTransformFilter *pFilter, HRESULT *phr, PCWSTR pName)
	: CTransformInputPin(L"CMpeg2DecoderInputPin", pFilter, phr, pName)
{
	m_CorrectTS = 0;
	m_RateChange.StartTime = INVALID_TIME;
	m_RateChange.Rate = 10000;
}

STDMETHODIMP CMpeg2DecoderInputPin::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
	CheckPointer(ppv, E_POINTER);

	if (riid == IID_IKsPropertySet)
		return GetInterface(static_cast<IKsPropertySet*>(this), ppv);

	return __super::NonDelegatingQueryInterface(riid, ppv);
}

// IMemInputPin

STDMETHODIMP CMpeg2DecoderInputPin::Receive(IMediaSample* pSample)
{
	return __super::Receive(pSample);
}

// IKsPropertySet

STDMETHODIMP CMpeg2DecoderInputPin::Set(
	REFGUID PropSet, ULONG Id, LPVOID pInstanceData, ULONG InstanceLength,
	LPVOID pPropertyData, ULONG DataLength)
{
	if (PropSet == AM_KSPROPSETID_TSRateChange) {
		switch (Id) {
		case AM_RATE_SimpleRateChange:
			{
				AM_SimpleRateChange *p = static_cast<AM_SimpleRateChange*>(pPropertyData);
				if (!m_CorrectTS) {
					return E_PROP_ID_UNSUPPORTED;
				}
				CAutoLock Lock(&m_csRateLock);
				m_RateChange = *p;
			}
			break;

		case AM_RATE_UseRateVersion:
			{
				WORD *p = static_cast<WORD*>(pPropertyData);
				if (*p > 0x0101) {
					return E_PROP_ID_UNSUPPORTED;
				}
			}
			break;

		case AM_RATE_CorrectTS:
			{
				LONG *p = static_cast<LONG*>(pPropertyData);
				m_CorrectTS = *p;
			}
			break;

		default:
			return E_PROP_ID_UNSUPPORTED;
		}
	} else {
		return E_PROP_ID_UNSUPPORTED;
	}

	return S_OK;
}

STDMETHODIMP CMpeg2DecoderInputPin::Get(
	REFGUID PropSet, ULONG Id, LPVOID pInstanceData, ULONG InstanceLength,
	LPVOID pPropertyData, ULONG DataLength, ULONG *pBytesReturned)
{
	if (PropSet == AM_KSPROPSETID_TSRateChange) {
		switch (Id) {
		/*
		case AM_RATE_SimpleRateChange:
			{
				AM_SimpleRateChange *p = static_cast<AM_SimpleRateChange*>(pPropertyData);
			}
			break;
		*/

		case AM_RATE_MaxFullDataRate:
			{
				AM_MaxFullDataRate *p = static_cast<AM_MaxFullDataRate*>(pPropertyData);

				*p = 8 * 10000;
				*pBytesReturned = sizeof(AM_MaxFullDataRate);
			}
			break;

		case AM_RATE_QueryFullFrameRate:
			{
				AM_QueryRate *p = static_cast<AM_QueryRate*>(pPropertyData);

				p->lMaxForwardFullFrame = 8 * 10000;
				p->lMaxReverseFullFrame = 8 * 10000;
				*pBytesReturned = sizeof(AM_QueryRate);
			}
			break;

		/*
		case AM_RATE_QueryLastRateSegPTS:
			{
				REFERENCE_TIME *p = static_cast<REFERENCE_TIME*>(pPropertyData);
			}
			break;
		*/

		default:
			return E_PROP_ID_UNSUPPORTED;
		}
	} else {
		return E_PROP_ID_UNSUPPORTED;
	}

	return S_OK;
}

STDMETHODIMP CMpeg2DecoderInputPin::QuerySupported(REFGUID PropSet, ULONG Id, ULONG *pTypeSupport)
{
	if (PropSet == AM_KSPROPSETID_TSRateChange) {
		switch (Id) {
		case AM_RATE_SimpleRateChange:
			*pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
			break;
		case AM_RATE_MaxFullDataRate:
			*pTypeSupport = KSPROPERTY_SUPPORT_GET;
			break;
		case AM_RATE_UseRateVersion:
			*pTypeSupport = KSPROPERTY_SUPPORT_SET;
			break;
		case AM_RATE_QueryFullFrameRate:
			*pTypeSupport = KSPROPERTY_SUPPORT_GET;
			break;
		case AM_RATE_QueryLastRateSegPTS:
			*pTypeSupport = KSPROPERTY_SUPPORT_GET;
			break;
		case AM_RATE_CorrectTS:
			*pTypeSupport = KSPROPERTY_SUPPORT_SET;
			break;
		default:
			return E_PROP_ID_UNSUPPORTED;
		}
	} else {
		return E_PROP_ID_UNSUPPORTED;
	}

	return S_OK;
}
