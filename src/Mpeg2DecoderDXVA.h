#pragma once


#include <d3d9.h>
#include <dxva2api.h>
#include <mfapi.h>
#include "Mpeg2Decoder.h"


#define GROW_BUFFER_SIZE  4096

class CMFBuffer{

  public:
				
				CMFBuffer() : m_pBuffer(NULL), m_dwTotalSize(0), m_dwStartAfterResize(0), m_dwStartPosition(0), m_dwEndPosition(0){}
				~CMFBuffer(){ if(m_pBuffer) delete[] m_pBuffer; }

				HRESULT Initialize();
				HRESULT Initialize(const DWORD);
				HRESULT Reserve(const DWORD);
    HRESULT SetStartPosition(const DWORD);
    HRESULT SetEndPosition(const DWORD);
				//void Reset(){ m_dwStartPosition = 0; m_dwEndPosition = 0; m_dwStartAfterResize = 0; }
				void Reset();

				BYTE* GetReadStartBuffer(){ return m_pBuffer + (m_dwStartPosition + m_dwStartAfterResize); }
				BYTE* GetStartBuffer(){ return m_pBuffer + m_dwStartPosition; }
				DWORD GetBufferSize() const{ assert(m_dwEndPosition >= m_dwStartPosition); return (m_dwEndPosition - m_dwStartPosition); }
				DWORD GetAllocatedSize() const{ return m_dwTotalSize; }
				
  private:

				BYTE* m_pBuffer;

				DWORD m_dwTotalSize;
				DWORD m_dwStartAfterResize;
				
				DWORD m_dwStartPosition;
				DWORD m_dwEndPosition;

				HRESULT SetSize(const DWORD);
};

inline HRESULT CMFBuffer::Initialize(){

		// Initialize only once
		if(m_pBuffer)
				return S_OK;

		m_pBuffer = new (std::nothrow)BYTE[GROW_BUFFER_SIZE];

		if(m_pBuffer == NULL){
				return E_OUTOFMEMORY;
		}

		m_dwTotalSize = GROW_BUFFER_SIZE;

		return S_OK;
}

inline HRESULT CMFBuffer::Initialize(const DWORD dwSize){

		// Initialize only once
		if(m_pBuffer)
				return S_OK;

		m_pBuffer = new (std::nothrow)BYTE[dwSize];

		if(m_pBuffer == NULL){
				return E_OUTOFMEMORY;
		}

		m_dwTotalSize = dwSize;

		return S_OK;
}

inline HRESULT CMFBuffer::Reserve(const DWORD dwSize){

		// Initialize must be called first
		assert(m_pBuffer);

		if(dwSize == 0)
				return S_OK;

		if(dwSize > MAXDWORD - m_dwTotalSize){
				return E_UNEXPECTED;
		}

		return SetSize(dwSize);
}

inline HRESULT CMFBuffer::SetStartPosition(const DWORD dwPos){

		if(dwPos == 0)
				return S_OK;

		if(dwPos > MAXDWORD - m_dwStartPosition){
				return E_UNEXPECTED;
		}

		DWORD dwNewPos = m_dwStartPosition + dwPos;

		if(dwNewPos > m_dwEndPosition){
				return E_UNEXPECTED;
		}

		m_dwStartPosition = dwNewPos;
		
		return S_OK;
}

inline HRESULT CMFBuffer::SetEndPosition(const DWORD dwPos){

		if(dwPos == 0)
				return S_OK;

		if(dwPos > MAXDWORD - m_dwEndPosition){
				return E_UNEXPECTED;
		}

		DWORD dwNewPos = m_dwEndPosition + dwPos;

		if(dwNewPos > m_dwTotalSize){

				return E_UNEXPECTED;
		}

		m_dwEndPosition = dwNewPos;
		m_dwStartAfterResize = 0;
		
		return S_OK;
}

inline void CMFBuffer::Reset(){

		m_dwStartPosition = 0;
		m_dwEndPosition = 0;
		m_dwStartAfterResize = 0;
}

inline HRESULT CMFBuffer::SetSize(const DWORD dwSize){

		HRESULT hr = S_OK;

		DWORD dwCurrentSize = GetBufferSize();

		// Todo check
		//if(dwCurrentSize == dwSize)
				//return hr;

		DWORD dwRemainingSize = m_dwTotalSize - dwCurrentSize;

		if(dwSize > dwRemainingSize){

				// Grow enough to not go here too many times (avoid lot of new).
				// We could use a multiple of 16 and use an aligned buffer.
				DWORD dwNewTotalSize = dwSize + dwCurrentSize + GROW_BUFFER_SIZE;

				BYTE* pTmp = new (std::nothrow)BYTE[dwNewTotalSize];

				//ZeroMemory(pTmp, dwNewTotalSize);

				if(pTmp != NULL){

						if(m_pBuffer != NULL){

								memcpy(pTmp, GetStartBuffer(), dwCurrentSize);
								delete[] m_pBuffer;
						}
						
						m_pBuffer = pTmp;

						m_dwStartAfterResize = dwCurrentSize;
						m_dwStartPosition = 0;
						m_dwEndPosition = dwCurrentSize;
						m_dwTotalSize = dwNewTotalSize;
				}
				else{

						hr = E_OUTOFMEMORY;
				}
		}
		else{

				if(dwCurrentSize != 0)
				  memcpy(m_pBuffer, GetStartBuffer(), dwCurrentSize);
				  //ZeroMemory(m_pBuffer + dwCurrentSize, m_dwTotalSize - dwCurrentSize);

				m_dwStartAfterResize = dwCurrentSize;
				m_dwStartPosition = 0;
				m_dwEndPosition = dwCurrentSize;
		}

		return hr;
}


class CTSScheduler{

public:

		CTSScheduler();
		~CTSScheduler(){}

		BOOL AddFrame(const int, const int, const int);
		const BOOL IsFull() const{ return m_iTotalSlice == NUM_DXVA2_SURFACE; }
		const BOOL IsEmpty() const{ return m_iTotalSlice == 0; }
		STemporalRef* GetNextFrame();
		void Reset();
		void StatePictureI(){ m_bPictureI = TRUE; }
		int GetFreeIDX() const{ return m_iFreeSlice; }
		void SetTime(const REFERENCE_TIME, const REFERENCE_TIME);
		void CheckPictureTime();

private:

		STemporalRef m_sTemporalRef[NUM_DXVA2_SURFACE];
		STemporalRef m_TmpTemporalRef;
		int m_iTotalSlice;
		int m_iNextSlice;
		int m_iNextTemporal;
		int m_iDecodedSlice;
		int m_iFreeSlice;
		BOOL m_bPictureI;

		struct TIMESTAMP_INFO{

				REFERENCE_TIME rtPTS;
				REFERENCE_TIME rtDTS;
		};

		queue<TIMESTAMP_INFO> m_qTimeStamp;

		void CheckNextSlice(){ m_iNextSlice++; if(m_iNextSlice == NUM_DXVA2_SURFACE) m_iNextSlice = 0; }
};

inline void CTSScheduler::SetTime(const REFERENCE_TIME rtPTS, const REFERENCE_TIME rtDTS){

  #ifdef TRACE_SET_PTS
		  TRACE((L"SetTime = %I64d : %I64d", rtPTS, rtDTS));
  #endif

		assert(rtPTS != -1);

		TIMESTAMP_INFO rtInfo;
		rtInfo.rtPTS = rtPTS;
		rtInfo.rtDTS = rtDTS;

		m_qTimeStamp.push(rtInfo);
}

inline void CTSScheduler::CheckPictureTime(){

		if(m_qTimeStamp.empty()){

				TIMESTAMP_INFO rtInfo = { -1, -1 };
				m_qTimeStamp.push(rtInfo);
		}
}

inline STemporalRef* CTSScheduler::GetNextFrame(){

		if(m_iDecodedSlice == NUM_DXVA2_SURFACE)
				m_iDecodedSlice = 0;

		m_iTotalSlice--;
		m_iFreeSlice = m_sTemporalRef[m_iDecodedSlice].iIndex;

  #ifdef TRACE_MPEG_PTS
		  TRACE((L"Time = %I64d", m_sTemporalRef[m_iDecodedSlice].rtTime));
  #endif

		return &m_sTemporalRef[m_iDecodedSlice++];
}


#define NUM_DXVA2_SURFACE 24

struct VIDEO_PARAMS{

		unsigned int uiNumSlices;
		unsigned int uiBitstreamDataLen;
		const unsigned char* pBitstreamData;
		const unsigned int* pSliceDataOffsets;

		int iCurPictureId;
		int iForwardRefIdx;
		int iBackwardRefIdx;
		int PicWidthInMbs;
		int FrameHeightInMbs;

		int field_pic_flag;
		int bottom_field_flag;
		int second_field;

		int ref_pic_flag;
		int intra_pic_flag;
		int picture_coding_type;
		int f_code[2][2];
		int intra_dc_precision;
		int picture_structure;
		int top_field_first;
		int frame_pred_frame_dct;
		int concealment_motion_vectors;
		int q_scale_type;
		int intra_vlc_format;
		int alternate_scan;
		int repeat_first_field;
		int chroma_420_type;
		int progressive_frame;
		int full_pel_forward_vector;
		int full_pel_backward_vector;

		unsigned char QuantMatrixIntra[64];
		unsigned char QuantMatrixInter[64];
};

struct STemporalRef{

		int iTemporal;
		int iPType;
		int iIndex;
		REFERENCE_TIME rtTime;
};


class CMpeg2DecoderDXVA2 : public CMpeg2Decoder
{
public:
	CMpeg2DecoderDXVA2();
	~CMpeg2DecoderDXVA2();
	bool Open(IDirect3DDeviceManager9 *pDeviceManager);
	void Close() override;
	mpeg2_state_t Parse() override;

private:
	mpeg2dec_t *m_pDec;
	IDirect3DDeviceManager9 *m_pDeviceManager;
	IDirectXVideoDecoderService *m_pDecoderService;
	IDirectXVideoDecoder *m_pVideoDecoder;
	IDirect3DSurface9 *m_pSurface9[NUM_DXVA2_SURFACE];
	HANDLE m_hD3d9Device;
	DXVA2_ConfigPictureDecode* m_pConfigs;
	DXVA2_DecodeExecuteParams m_ExecuteParams;
	DXVA2_DecodeBufferDesc m_BufferDesc[4];
	DXVA_PictureParameters m_PictureParams;
	DXVA_SliceInfo m_SliceInfo[MAX_SLICE];
	DXVA_QmatrixData m_QuantaMatrix;
	DXVA2_VideoDesc m_Dxva2Desc;
	CMFBuffer m_InputBuffer;
	CMFBuffer m_SliceBuffer;
	unsigned int *m_pSliceOffset;
	VIDEO_PARAMS m_VideoParam;
	CTSScheduler m_cTSScheduler;
	DWORD m_TemporalReference;
	DWORD m_iCurSurfaceIndex;
	bool m_bSlice;
	bool m_bMpegHeaderEx;
	bool m_bProgressive;
	bool m_bFirstPictureI;
	int m_iLastForward;
	int m_iLastPictureP;
	bool m_bFreeSurface[NUM_DXVA2_SURFACE];
	DWORD m_SampleSize;
	bool m_bHaveOuput;
	UINT64  m_rtAvgPerFrameInput;
	REFERENCE_TIME m_rtTime;
	UINT32  m_uiWidthInPixels;
	UINT32  m_uiHeightInPixels;
	MFRatio m_FrameRate;
	MFRatio m_PixelRatio;
	bool m_bIsDiscontinuity;
	bool m_bDraining;

	HRESULT FindNextStartCode(uint8_t *pData, size_t Size, size_t *Ate);
	HRESULT PictureCode(const uint8_t *pInputData);
	HRESULT SequenceCode(const uint8_t *pInputData);
	HRESULT SequenceExCode(const uint8_t *pInputData);
	HRESULT SliceInit(const uint8_t *pInputData);
	void InitQuanta();
	void InitDxva2Buffers();
	HRESULT BeginStreaming();
	HRESULT Flush();
	void ReleaseInput();
	HRESULT ConfigureDecoder();
	HRESULT SchedulePicture();
	HRESULT Dxva2DecodePicture();
	WORD GetScaleCode(const uint8_t *pData);
	void InitPictureParams();
	void InitSliceParams();
	void InitQuantaMatrixParams();
};
