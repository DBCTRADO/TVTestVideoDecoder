#include "stdafx.h"
#include "Mpeg2DecoderDXVA.h"
#include "Util.h"


#define MAX_VIDEO_WIDTH_HEIGHT        4095
#define MIN_BYTE_TO_READ_MPEG_HEADER  12
#define MAX_SLICE                     175 // (1 to 175 : 0x00000001 to 0x000001AF)
#define SLICE_BUFFER_SIZE             32768

enum StreamType{

		StreamType_Unknown,
		StreamType_AllAudio,
		StreamType_AllVideo,
		StreamType_Reserved,
		StreamType_Private1,
		StreamType_Padding,
		StreamType_Private2,
		StreamType_Audio,
		StreamType_Video,
		StreamType_Data
};

struct MPEG2StreamHeader{

		BYTE stream_id;  // Raw stream_id field.
		StreamType type; // Stream type (audio, video, etc)
		BYTE number;     // Index within the stream type (audio 0, audio 1, etc)
		DWORD sizeBound;
};

struct MPEG2SystemHeader{

		DWORD   rateBound;
		BYTE    cAudioBound;
		BOOL    bFixed;
		BOOL    bCSPS;
		BOOL    bAudioLock;
		BOOL    bVideoLock;
		BYTE    cVideoBound;
		MPEG2StreamHeader streams[2];
};

struct MPEG2PacketHeader{

		BYTE        stream_id;      // Raw stream_id field.
		StreamType  type;           // Stream type (audio, video, etc)
		BYTE        number;         // Index within the stream type (audio 0, audio 1, etc)
		DWORD       cbPacketSize;   // Size of the entire packet (header + payload).
		DWORD       cbPayload;      // Size of the packet payload (packet size - header size).
		BOOL        bHasPTS;        // Did the packet header contain a Presentation Time Stamp (PTS)?
		LONGLONG    PTS;            // Presentation Time Stamp (in 90 kHz clock)
		BOOL        bHasDTS;
		LONGLONG    DTS;
};

const DWORD MPEG1_PACK_HEADER_SIZE = 12;
const DWORD MPEG2_PACK_HEADER_SIZE = 14;
const DWORD MPEG2_SYSTEM_HEADER_MIN_SIZE = 12;       // System header, excluding the stream info.
const DWORD MPEG2_SYSTEM_AND_PACK_HEADER_PREFIX = 6; // This value + header length = total size of the system header or the pack header.
const DWORD MPEG2_SYSTEM_HEADER_STREAM = 3;          // Size of each stream info in the system header.

//--------------------------------------------------------------------------------------------------------------
// Todo check value
const DWORD MPEG2_PACKET_HEADER_MAX_SIZE = 34;          // Maximum size of a packet header.
const DWORD MPEG2_PACKET_HEADER_MAX_STUFFING_BYTE = 16; // Maximum number of stuffing bytes in a packet header.

//const DWORD MPEG1_VIDEO_SEQ_HEADER_MIN_SIZE = 12;       // Minimum length of the video sequence header.
const DWORD MPEG1_VIDEO_SEQ_HEADER_MAX_SIZE = 140;      // Maximum length of the video sequence header.
const DWORD MPEG2_AUDIO_FRAME_HEADER_SIZE = 4;
const DWORD AC3_AUDIO_FRAME_HEADER_SIZE = 7;
//--------------------------------------------------------------------------------------------------------------

struct MPEG2VideoSeqHeader{

		WORD    width;
		WORD    height;
		MFRatio pixelAspectRatio;
		MFRatio frameRate;
		DWORD   bitRate;
		WORD    cbVBV_Buffer;
		BOOL    bConstrained;
		DWORD   cbHeader;
		BYTE    header[MPEG1_VIDEO_SEQ_HEADER_MAX_SIZE];
};

enum MPEG2AudioLayer{

		MPEG2_Audio_Layer1 = 0,
		MPEG2_Audio_Layer2,
		MPEG2_Audio_Layer3
};

enum MPEG2AudioMode{

		MPEG2_Audio_Stereo = 0,
		MPEG2_Audio_JointStereo,
		MPEG2_Audio_DualChannel,
		MPEG2_Audio_SingleChannel
};

enum MPEG2AudioFlags{

		MPEG2_AUDIO_PRIVATE_BIT = 0x01,  // = ACM_MPEG_PRIVATEBIT
		MPEG2_AUDIO_COPYRIGHT_BIT = 0x02,  // = ACM_MPEG_COPYRIGHT
		MPEG2_AUDIO_ORIGINAL_BIT = 0x04,  // = ACM_MPEG_ORIGINALHOME
		MPEG2_AUDIO_PROTECTION_BIT = 0x08,  // = ACM_MPEG_PROTECTIONBIT
};

struct MPEG2AudioFrameHeader{

		MPEG2AudioLayer layer;
		DWORD           dwBitRate;
		DWORD           dwSamplesPerSec;
		WORD            nBlockAlign;
		WORD            nChannels;
		MPEG2AudioMode  mode;
		BYTE            modeExtension;
		BYTE            emphasis;
		WORD            wFlags;
};

// Codes
const DWORD MPEG2_START_CODE_PREFIX = 0x00000100;
const DWORD MPEG2_PACK_START_CODE = 0x000001BA;
const DWORD MPEG2_SYSTEM_HEADER_CODE = 0x000001BB;
const DWORD MPEG2_SEQUENCE_HEADER_CODE = 0x000001B3;
const DWORD MPEG2_STOP_CODE = 0x000001B9;

// Stream ID codes
const BYTE MPEG2_STREAMTYPE_ALL_AUDIO = 0xB8;
const BYTE MPEG2_STREAMTYPE_ALL_VIDEO = 0xB9;
const BYTE MPEG2_STREAMTYPE_RESERVED = 0xBC;
const BYTE MPEG2_STREAMTYPE_PRIVATE1 = 0xBD;
const BYTE MPEG2_STREAMTYPE_PADDING = 0xBE;
const BYTE MPEG2_STREAMTYPE_PRIVATE2 = 0xBF;
const BYTE MPEG2_STREAMTYPE_AUDIO_MASK = 0xC0;
const BYTE MPEG2_STREAMTYPE_VIDEO_MASK = 0xE0;
const BYTE MPEG2_STREAMTYPE_DATA_MASK = 0xF0;

const DWORD MPEG_PICTURE_START_CODE = 0x00000100;
const DWORD MPEG_SEQUENCE_HEADER_CODE = 0x000001B3;
const DWORD MPEG_SEQUENCE_EXTENSION_CODE = 0x000001B5;
const DWORD MPEG_SEQUENCE_END_CODE = 0x000001B7;

const DWORD SEQUENCE_EXTENSION_CODE = 0x00000001;
const DWORD SEQUENCE_DISPLAY_EXTENSION_CODE = 0x00000002;
const DWORD QUANTA_EXTENSION_CODE = 0x00000003;
const DWORD SEQUENCE_SCALABLE_EXTENSION_CODE = 0x00000005;
const DWORD PICTURE_DISPLAY_EXTENSION_CODE = 0x00000007;
const DWORD PICTURE_EXTENSION_CODE = 0x00000008;
const DWORD PICTURE_SPATIAL_EXTENSION_CODE = 0x00000009;
const DWORD PICTURE_TEMPORAL_EXTENSION_CODE = 0x00000010;

const DWORD MPEG_CHROMAT_FORMAT_RESERVED = 0x00000000;
const DWORD MPEG_CHROMAT_FORMAT_420 = 0x00000001;
const DWORD MPEG_CHROMAT_FORMAT_422 = 0x00000002;
const DWORD MPEG_CHROMAT_FORMAT_444 = 0x00000003;

#define PICTURE_TYPE_I    1
#define PICTURE_TYPE_P    2
#define PICTURE_TYPE_B    3

#define HAS_FLAG(b, flag) (((b) & (flag)) == (flag))

inline uint32_t MAKE_DWORD(const uint8_t *pData)
{
	return ((uint32_t)pData[0] << 24) | ((uint32_t)pData[1] << 16) | ((uint32_t)pData[2] << 8) | (uint32_t)pData[3];
}


CTSScheduler::CTSScheduler()
		: m_iTotalSlice(0),
		m_iNextSlice(0),
		m_iNextTemporal(0),
		m_iDecodedSlice(0),
		m_iFreeSlice(0),
		m_bPictureI(FALSE)
{
		ZeroMemory(m_sTemporalRef, sizeof(m_sTemporalRef));
		memset(&m_TmpTemporalRef, -1, sizeof(m_TmpTemporalRef));
}

BOOL CTSScheduler::AddFrame(const int iIndex, const int iPType, const int iTemporal){

		if(m_iTotalSlice == NUM_DXVA2_SURFACE)
				return FALSE;

		REFERENCE_TIME rtTime;

		if(m_qTimeStamp.empty()){
				rtTime = -1;
		}
		else{

				const TIMESTAMP_INFO rtInfo = m_qTimeStamp.front();

				if(iPType == PICTURE_TYPE_B && rtInfo.rtDTS != -1 && rtInfo.rtPTS != rtInfo.rtDTS){
						rtTime = -1;
				}
				else{
						m_qTimeStamp.pop();
						rtTime = rtInfo.rtPTS;
				}
		}

		if(m_bPictureI){

				if(m_iNextTemporal == m_TmpTemporalRef.iTemporal){

						m_sTemporalRef[m_iNextSlice].iIndex = m_TmpTemporalRef.iIndex;
						m_sTemporalRef[m_iNextSlice].iPType = m_TmpTemporalRef.iPType;
						m_sTemporalRef[m_iNextSlice].iTemporal = m_TmpTemporalRef.iTemporal;
						m_sTemporalRef[m_iNextSlice].rtTime = m_TmpTemporalRef.rtTime;
						CheckNextSlice();
				}

				memset(&m_TmpTemporalRef, -1, sizeof(m_TmpTemporalRef));
				m_iNextTemporal = 0;
				m_bPictureI = FALSE;
		}

		if(iTemporal == m_iNextTemporal){

				m_sTemporalRef[m_iNextSlice].iIndex = iIndex;
				m_sTemporalRef[m_iNextSlice].iPType = iPType;
				m_sTemporalRef[m_iNextSlice].iTemporal = iTemporal;
				m_sTemporalRef[m_iNextSlice].rtTime = rtTime;
				m_iNextTemporal++;
				CheckNextSlice();
		}
		else{

				if(m_iNextTemporal == m_TmpTemporalRef.iTemporal){

						m_sTemporalRef[m_iNextSlice].iIndex = m_TmpTemporalRef.iIndex;
						m_sTemporalRef[m_iNextSlice].iPType = m_TmpTemporalRef.iPType;
						m_sTemporalRef[m_iNextSlice].iTemporal = m_TmpTemporalRef.iTemporal;
						m_sTemporalRef[m_iNextSlice].rtTime = m_TmpTemporalRef.rtTime;
						m_iNextTemporal++;
						CheckNextSlice();
				}

				m_TmpTemporalRef.iIndex = iIndex;
				m_TmpTemporalRef.iPType = iPType;
				m_TmpTemporalRef.iTemporal = iTemporal;
				m_TmpTemporalRef.rtTime = rtTime;
		}

		m_iTotalSlice++;
		m_iFreeSlice++;

		return TRUE;
}

void CTSScheduler::Reset(){

		while(!m_qTimeStamp.empty()){
				m_qTimeStamp.pop();
		}

		m_iTotalSlice = 0;
		m_iNextSlice = 0;
		m_iNextTemporal = 0;
		m_iDecodedSlice = 0;
		m_iFreeSlice = 0;
		m_bPictureI = FALSE;

		ZeroMemory(m_sTemporalRef, sizeof(m_sTemporalRef));
		memset(&m_TmpTemporalRef, -1, sizeof(m_TmpTemporalRef));
}


static const unsigned char ff_zigzag_direct[64] = {
		0, 1, 8, 16, 9, 2, 3, 10,
		17, 24, 32, 25, 18, 11, 4, 5,
		12, 19, 26, 33, 40, 48, 41, 34,
		27, 20, 13, 6, 7, 14, 21, 28,
		35, 42, 49, 56, 57, 50, 43, 36,
		29, 22, 15, 23, 30, 37, 44, 51,
		58, 59, 52, 45, 38, 31, 39, 46,
		53, 60, 61, 54, 47, 55, 62, 63
};


CMpeg2DecoderDXVA2::CMpeg2DecoderDXVA2()
	: m_pDec(nullptr)
	, m_pDeviceManager(NULL)
	, m_pDecoderService(NULL)
	, m_pVideoDecoder(NULL)
	, m_hD3d9Device(NULL)
	, m_pConfigs(NULL)
	, m_SampleSize(0)
	, m_bHaveOuput(false)
	, m_rtAvgPerFrameInput(0)
	, m_rtTime(0)
	, m_uiWidthInPixels(0)
	, m_uiHeightInPixels(0)
	, m_bSlice(false)
	, m_bMpegHeaderEx(false)
	, m_bProgressive(true)
	, m_TemporalReference(0)
	, m_iCurSurfaceIndex(0)
	, m_pSliceOffset(NULL)
	, m_bFirstPictureI(false)
	, m_iLastForward(-1)
	, m_iLastPictureP(-1)
	, m_bIsDiscontinuity(true)
	, m_bDraining(false)
{
	m_FrameRate.Numerator = m_FrameRate.Denominator = 0;
	m_PixelRatio.Numerator = m_PixelRatio.Denominator = 0;

	for(int i = 0; i < NUM_DXVA2_SURFACE; i++){
			m_pSurface9[i] = NULL;
	}

	InitDxva2Buffers();
}


CMpeg2DecoderDXVA2::~CMpeg2DecoderDXVA2()
{
	Close();
}


bool CMpeg2DecoderDXVA2::Open(IDirect3DDeviceManager9 *pDeviceManager)
{
	Close();

	if (!pDeviceManager)
		return false;

	m_pDec = mpeg2_init();
	if (!m_pDec)
		return false;

	m_pDeviceManager = pDeviceManager;
	m_pDeviceManager->AddRef();

	HRESULT hr;
	HANDLE hDevice;

	hr = pDeviceManager->OpenDeviceHandle(&hDevice);
	if (FAILED(hr)) {
		Close();
		return false;
	}

	m_hD3d9Device = hDevice;

	IDirectXVideoDecoderService *pDecoderService;
	hr = pDeviceManager->GetVideoService(hDevice, IID_PPV_ARGS(&pDecoderService));
	if (FAILED(hr)) {
		Close();
		return false;
	}

	m_pDecoderService = pDecoderService;

	UINT Count;
	GUID *pGuids;
	hr = pDecoderService->GetDecoderDeviceGuids(&Count, &pGuids);
	if (FAILED(hr)) {
		Close();
		return false;
	}

	bool fFound = false;
	for (UINT i = 0; i < Count; i++) {
		if (pGuids[i] == DXVA2_ModeMPEG2and1_VLD) {
			fFound = true;
			break;
		}
	}
	::CoTaskMemFree(pGuids);
	if (!fFound) {
		Close();
		return false;
	}

	D3DFORMAT *pForamts;
	hr = pDecoderService->GetDecoderRenderTargets(DXVA2_ModeMPEG2and1_VLD, &Count, &pFormats);
	if (FAILED(hr)) {
		Close();
		return false;
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
		Close();
		return false;
	}

	return true;
}


void CMpeg2DecoderDXVA2::Close()
{
	SafeRelease(m_pVideoDecoder);
	SafeRelease(m_pDecoderService);

	if (m_hD3d9Device) {
		m_pDeviceManager->CloseDeviceHandle(m_hD3d9Device);
		m_hD3d9Device = nullptr;
	}

	SafeRelease(m_pDeviceManager);

	CMpeg2Decoder::Close();
}


mpeg2_state_t CMpeg2DecoderDXVA::Parse()
{
	mpeg2_state_t State = CMpeg2Decoder::Parse();

	switch (State) {
	case STATE_PICTURE:
		break;

	case STATE_SLICE:
		break;
	}

	return State;
}

	HRESULT hr;

	DWORD Parsed, Code, SmallCode;

	m_InputBuffer.Reserve(Size);
	memcpy(m_InputBuffer.GetReadStartBuffer(), pBuffer, Size);
	m_InputBuffer.SetEndPosition(Size);

	do {
		hr = FindNextStartCode(m_InputBuffer.GetStartBuffer(), m_InputBuffer.GetBufferSize(), &Parsed);

		if (m_bSlice) {
			m_SliceBuffer.Reserve(Parsed);
			memcpy(m_SliceBuffer.GetReadStartBuffer(), m_InputBuffer.GetStartBuffer(), Parsed);
			m_SliceBuffer.SetEndPosition(Parsed);
		}
		m_InputBuffer.SetStartPosition(Parsed);

		if (hr == S_FALSE || m_InputBuffer.GetBufferSize() < MIN_BYTE_TO_READ_MPEG_HEADER) {
			hr = S_OK;
			break;
		}

		m_bSlice = false;
		Code = MAKE_DWORD(m_InputBuffer.GetStartBuffer());

		switch (Code) {
		case MPEG_PICTURE_START_CODE:
			if (m_SliceBuffer.GetBufferSize()){

				hr = SchedulePicture();

				m_SliceBuffer.Reset();
				m_VideoParam.uiNumSlices = 0;

				if (m_bHaveOuput)
					return hr;
			}
			hr = PictureCode(m_InputBuffer.GetStartBuffer());
			break;

		case MPEG_SEQUENCE_HEADER_CODE:
			hr = SequenceCode(m_InputBuffer.GetStartBuffer());
			break;

		case MPEG_SEQUENCE_EXTENSION_CODE:
			hr = SequenceExCode(m_InputBuffer.GetStartBuffer());
			break;

		case MPEG_SEQUENCE_END_CODE:
			//Todo
			//LOG_HRESULT(hr = E_FAIL);
			return hr;
			//break;

		default:
			{
				SmallCode = dwCode & 0x000000ff;

				if(SmallCode > 0 && SmallCode <= MAX_SLICE){
					hr = SliceInit(m_InputBuffer.GetStartBuffer());
				}
			}
			break;
		}

	} while (hr == S_OK);

	return hr;
}

HRESULT CMpeg2DecoderDXVA2::FindNextStartCode(uint8_t *pData, size_t Size, size_t *Ate)
{
	HRESULT hr = S_FALSE;

	size_t Left = Size;
	uint32_t Sync = 0;

	while (Left >= 4){
		Sync = (Sync << 8) | *pData;
		if ((Sync & 0x00ffffff) == 0x00000100) {
			hr = S_OK;
			break;
		}
		Left--;
		pData++;
	}

	*Ate = Size - Left;

	return hr;
}

HRESULT cMpeg2DecoderDXVA::PictureCode(const uint8_t *pInputData)
{
	HRESULT hr = S_OK;

	m_TemporalReference = (pInputData[4] << 2) | (pInputData[5] >> 6);

	m_VideoParam.picture_coding_type = (pInputData[5] & 0x38) >> 3;

	if (m_VideoParam.picture_coding_type == PICTURE_TYPE_I) {
		m_VideoParam.intra_pic_flag = 1;
		m_VideoParam.ref_pic_flag = 1;
		m_VideoParam.iForwardRefIdx = -1;
		m_VideoParam.iBackwardRefIdx = -1;
		m_iLastForward = m_iLastPictureP;
		m_cTSScheduler.StatePictureI();
		m_bFirstPictureI = true;
	} else if (m_VideoParam.picture_coding_type == PICTURE_TYPE_P) {
		m_VideoParam.intra_pic_flag = 0;
		m_VideoParam.ref_pic_flag = 1;
		m_VideoParam.iForwardRefIdx = m_iLastPictureP;
		m_VideoParam.iBackwardRefIdx = -1;
		m_iLastForward = m_iLastPictureP;
	} else if (m_VideoParam.picture_coding_type == PICTURE_TYPE_B) {
		m_VideoParam.intra_pic_flag = 0;
		m_VideoParam.ref_pic_flag = 0;
		m_VideoParam.iForwardRefIdx = m_iLastForward;
		m_VideoParam.iBackwardRefIdx = m_iLastPictureP;
	}
	else{
			// picture_coding_type != 1-2-3 (Todo)
			LOG_HRESULT(hr = E_FAIL);
			return hr;
	}

	if (m_bFirstPictureI)
		m_cTSScheduler.CheckPictureTime();

	return hr;
}

HRESULT CMpeg2DecoderDXVA::SequenceCode(const uint8_t *pInputData)
{
	HRESULT hr = S_OK;

	UINT uiFrameWidth = (pInputData[4] << 4) | (pInputData[5] >> 4);
	UINT uiFrameHeight = ((pInputData[5] & 0x0f) << 8) | pInputData[6];

	m_VideoParam.PicWidthInMbs = (uiFrameWidth + 15) / 16;
	m_VideoParam.FrameHeightInMbs = (uiFrameHeight + 15) / 16;

	if (pInputData[11] & 0x02) {
		const uint8_t *pIntra = &pInputData[11];

		for (int i = 0; i < 64; i++) {
			m_VideoParam.QuantMatrixIntra[ff_zigzag_direct[i]] =
				((pIntra[i] & 0x01) << 7) || (pIntra[i + 1] >> 1);
		}

		if (pInputData[75] & 0x01) {
			const BYTE* pInter = &pInputData[76];

			for (int i = 0; i < 64; i++) {
				m_VideoParam.QuantMatrixInter[ff_zigzag_direct[i]] = pInter[i];
			}
		}
	} else if (pInputData[11] & 0x01) {
		const uint8_t *pQuanta = &pInputData[12];

		for (int i = 0; i < 64; i++) {
			m_VideoParam.QuantMatrixInter[ff_zigzag_direct[i]] = pQuanta[i];
		}
	}

	return hr;
}

HRESULT CMpeg2DecoderDXVA::SequenceExCode(const uint8_t *pInputData)
{
	HRESULT hr = S_OK;

	m_bMpegHeaderEx = true;
	DWORD dwCodeEx = (pInputData[4] & 0xf0) >> 4;

	if (dwCodeEx == PICTURE_EXTENSION_CODE) {
		m_VideoParam.f_code[0][0] = (pInputData[4] & 0x0f);
		m_VideoParam.f_code[0][1] = ((pInputData[5] & 0xf0) >> 4);
		m_VideoParam.f_code[1][0] = (pInputData[5] & 0x0f);
		m_VideoParam.f_code[1][1] = ((pInputData[6] & 0xf0) >> 4);

		m_VideoParam.intra_dc_precision = ((pInputData[6] & 0x0c) >> 2);
		m_VideoParam.picture_structure = (pInputData[6] & 0x03);
		m_VideoParam.top_field_first = ((pInputData[7] & 0x80) >> 7);
		m_VideoParam.frame_pred_frame_dct = ((pInputData[7] & 0x40) >> 6);
		m_VideoParam.concealment_motion_vectors = ((pInputData[7] & 0x20) >> 5);
		m_VideoParam.q_scale_type = ((pInputData[7] & 0x10) >> 4);
		m_VideoParam.intra_vlc_format = ((pInputData[7] & 0x08) >> 3);
		m_VideoParam.alternate_scan = ((pInputData[7] & 0x04) >> 2);
		m_VideoParam.repeat_first_field = ((pInputData[7] & 0x02) >> 1);
		m_VideoParam.chroma_420_type = (pInputData[7] & 0x01);
		m_VideoParam.progressive_frame = ((pInputData[8] & 0x80) >> 7);
		//m_VideoParam.uiCompositeDisplayFlag = ((pInputData[8] & 0x40) >> 6);

#if 0
		if (m_VideoParam.picture_structure != 3) {
			// Interlaced (Todo)
			LOG_HRESULT(hr = E_FAIL);
		}
#endif
	} else if (dwCodeEx == SEQUENCE_EXTENSION_CODE) {
		UINT uiChromatFmt = ((pInputData[5] & 0x06) >> 1);

		if (uiChromatFmt != MPEG_CHROMAT_FORMAT_420) {
			// Chromat != 420 (Todo)
			LOG_HRESULT(hr = E_FAIL);
		}
	} else {
		// QUANTA_EXTENSION_CODE (Todo)
		if (dwCodeEx == QUANTA_EXTENSION_CODE) {
			LOG_HRESULT(hr = E_FAIL);
		}
	}

	return hr;
}

HRESULT CMpeg2DecoderDXVA::SliceInit(const uint8_t *pInputData)
{
	HRESULT hr = S_OK;

	m_pSliceOffset[m_VideoParam.uiNumSlices] = m_SliceBuffer.GetBufferSize();
	m_VideoParam.uiNumSlices++;
	m_bSlice = true;

	if(FAILED(hr = m_SliceBuffer.Reserve(4))){
		LOG_HRESULT(hr);
		return hr;
	}

	memcpy(m_SliceBuffer.GetReadStartBuffer(), pInputData, 4);

	hr = m_SliceBuffer.SetEndPosition(4);

	return hr;
}

HRESULT CMpeg2DecoderDXVA::BeginStreaming()
{
	if (!m_pDecoderService)
		return E_UNEXPECTED;
	if (m_pVideoDecoder)
		return E_UNEXPECTED;

	HRESULT hr;

	hr = m_InputBuffer.Initialize();
	hr = m_SliceBuffer.Initialize(SLICE_BUFFER_SIZE);

	m_pSliceOffset = DNew_nothrow unsigned int[MAX_SLICE];
	if (!m_pSliceOffset) {
		return E_OUTOFMEMORY;
	}

	hr = m_pDecoderService->CreateSurface(
		m_uiWidthInPixels,
		m_uiHeightInPixels,
		NUM_DXVA2_SURFACE - 1,
		D3DFMT_NV12,
		D3DPOOL_DEFAULT,
		0,
		DXVA2_VideoDecoderRenderTarget,
		m_pSurface9,
		NULL);
	if (FAILED(hr)) {
		TRACE(TEXT("IDirecctXVideoDecoderService::CreateSurface() failed (%x)\n"), hr);
		return hr;
	}

	/*D3DLOCKED_RECT d3dRect;

	for(int i = 0; i < NUM_DXVA2_SURFACE; i++){

	IF_FAILED_RETURN(hr = m_pSurface9[i]->LockRect(&d3dRect, NULL, 0));
	memset(d3dRect.pBits, 0x80, (d3dRect.Pitch * (m_uiHeightInPixels + (m_uiHeightInPixels / 2))));
	IF_FAILED_RETURN(hr = m_pSurface9[i]->UnlockRect());
	}*/

	InitDxva2Buffers();

	IDirectXVideoDecoder *pVideoDecoder;
	hr = m_pDecoderService->CreateVideoDecoder(
		DXVA2_ModeMPEG2and1_VLD,
		&m_Dxva2Desc,
		&m_pConfigs[0],
		m_pSurface9,
		NUM_DXVA2_SURFACE,
		&pVideoDecoder);
	if (FAILED(hr)) {
		return hr;
	}
	m_pVideoDecoder = pVideoDecoder;

	return hr;
}

HRESULT CMpeg2DecoderDXVA::Flush()
{
	if (m_pVideoDecoder) {
		ReleaseInput();

		// Check if we need to lock directx device, if device is not closed before...
		for (int i = 0; i < NUM_DXVA2_SURFACE; i++) {
			SafeRelease(m_pSurface9[i]);
		}

		m_cTSScheduler.Reset();

		m_bProgressive = true;
		m_bHaveOuput = false;
		m_rtTime = 0;
		m_bDraining = false;

		m_pVideoDecoder->Release();
		m_pVideoDecoder = nullptr;
	}

	return S_OK;
}

void CMpeg2DecoderDXVA::ReleaseInput()
{
	if(m_pSliceOffset == NULL)
		return;

	m_InputBuffer.Reset();
	m_SliceBuffer.Reset();

	m_bSlice = FALSE;
	m_TemporalReference = 0;
	m_iCurSurfaceIndex = 0;
	m_bFirstPictureI = FALSE;
	m_iLastForward = -1;
	m_iLastPictureP = -1;
	m_bIsDiscontinuity = TRUE;

	SAFE_DELETE_ARRAY(m_pSliceOffset);
}

void CMpeg2DecoderDXVA::InitQuanta()
{
	memset(m_VideoParam.QuantMatrixInter, 16, 64);

	// Zigzag apply, need to learn more...
	static const BYTE btQuantIntra[64] = {
		 8, 16, 16, 19, 16, 19, 22, 22, 22, 22,
		22, 22, 26, 24, 26, 27, 27, 27, 26, 26,
		26, 26, 27, 27, 27, 29, 29, 29, 34, 34,
		34, 29, 29, 29, 27, 27, 29, 29, 32, 32,
		34, 34, 37, 38, 37, 35, 35, 34, 35, 38,
		38, 40, 40, 40, 48, 48, 46, 46, 56, 56,
		58, 69, 69, 83
	};

	memcpy(m_VideoParam.QuantMatrixIntra, btQuantIntra, 64);
}

void CMpeg2DecoderDXVA::InitDxva2Buffers()
{
	memset(&m_ExecuteParams, 0, sizeof(DXVA2_DecodeExecuteParams));
	memset(m_BufferDesc, 0, 4 * sizeof(DXVA2_DecodeBufferDesc));
	memset(&m_PictureParams, 0, sizeof(DXVA_PictureParameters));
	memset(m_SliceInfo, 0, MAX_SLICE * sizeof(DXVA_SliceInfo));
	memset(&m_QuantaMatrix, 0, sizeof(DXVA_QmatrixData));
	memset(&m_VideoParam, 0, sizeof(VIDEO_PARAMS));
	memset(&m_Dxva2Desc, 0, sizeof(DXVA2_VideoDesc));
	memset(&m_bFreeSurface, 1, sizeof(m_bFreeSurface));

	m_ExecuteParams.NumCompBuffers = 4;

	m_BufferDesc[0].CompressedBufferType = DXVA2_PictureParametersBufferType;
	m_BufferDesc[0].DataSize = sizeof(DXVA_PictureParameters);

	m_BufferDesc[1].CompressedBufferType = DXVA2_InverseQuantizationMatrixBufferType;
	m_BufferDesc[1].DataSize = sizeof(DXVA_QmatrixData);

	m_BufferDesc[2].CompressedBufferType = DXVA2_BitStreamDateBufferType;

	m_BufferDesc[3].CompressedBufferType = DXVA2_SliceControlBufferType;

	m_ExecuteParams.pCompressedBuffers = m_BufferDesc;

	m_VideoParam.intra_dc_precision = 0;
	m_VideoParam.picture_structure = 3;
	m_VideoParam.top_field_first = false;
	m_VideoParam.frame_pred_frame_dct = 1;
	m_VideoParam.concealment_motion_vectors = 0;
	m_VideoParam.q_scale_type = 0;
	m_VideoParam.intra_vlc_format = 0;
	m_VideoParam.alternate_scan = 0;
	m_VideoParam.repeat_first_field = 0;
	m_VideoParam.chroma_420_type = 1;
	m_VideoParam.progressive_frame = 1;
	m_VideoParam.f_code[0][0] = 15;
	m_VideoParam.f_code[0][1] = 15;
	m_VideoParam.f_code[1][0] = 15;
	m_VideoParam.f_code[1][1] = 15;

	// Check : already initialized
	DXVA2_Frequency Dxva2Freq;
	Dxva2Freq.Numerator = m_FrameRate.Numerator;
	Dxva2Freq.Denominator = m_FrameRate.Denominator;

	m_Dxva2Desc.SampleWidth = m_uiWidthInPixels;
	m_Dxva2Desc.SampleHeight = m_uiHeightInPixels;

	m_Dxva2Desc.SampleFormat.SampleFormat = MFVideoInterlace_MixedInterlaceOrProgressive;

	m_Dxva2Desc.Format = static_cast<D3DFORMAT>(D3DFMT_NV12);
	m_Dxva2Desc.InputSampleFreq = Dxva2Freq;
	m_Dxva2Desc.OutputFrameFreq = Dxva2Freq;

	InitQuanta();
}

HRESULT CMpeg2DecoderDXVA2::ConfigureDecoder()
{
	if (!m_pDecoderService)
		return E_UNEXPECTED;

	HRESULT hr;

	hr = Flush();

	if (m_pConfigs) {
		::CoTaskMemFree(m_pConfigs);
		m_pConfigs = NULL;
	}

	memset(&m_Dxva2Desc, 0, sizeof(DXVA2_VideoDesc));

	DXVA2_Frequency Dxva2Freq;
	Dxva2Freq.Numerator = m_FrameRate.Numerator;
	Dxva2Freq.Denominator = m_FrameRate.Denominator;

	m_Dxva2Desc.SampleWidth = m_uiWidthInPixels;
	m_Dxva2Desc.SampleHeight = m_uiHeightInPixels;

	m_Dxva2Desc.SampleFormat.SampleFormat = MFVideoInterlace_MixedInterlaceOrProgressive;

	m_Dxva2Desc.Format = static_cast<D3DFORMAT>(D3DFMT_NV12);
	m_Dxva2Desc.InputSampleFreq = Dxva2Freq;
	m_Dxva2Desc.OutputFrameFreq = Dxva2Freq;

	UINT Count;

	hr = m_pDecoderService->GetDecoderConfigurations(DXVA2_ModeMPEG2and1_VLD, &m_Dxva2Desc, NULL, &Count, &m_pConfigs);
	if (FAILED(hr)) {
		return hr;
	}

	if (Count == 0) {
		return E_FAIL;
	}

	return hr;
}

HRESULT CMpeg2DecoderDXVA2::SchedulePicture()
{
	if (!m_bFirstPictureI) {
		return S_OK;
	}

	assert(m_cTSScheduler.IsFull() == FALSE);

	m_VideoParam.iCurPictureId = m_iCurSurfaceIndex++;

	if (m_iCurSurfaceIndex == NUM_DXVA2_SURFACE)
		m_iCurSurfaceIndex = 0;

	m_VideoParam.uiBitstreamDataLen = m_SliceBuffer.GetBufferSize();
	m_VideoParam.pBitstreamData = m_SliceBuffer.GetStartBuffer();
	m_VideoParam.pSliceDataOffsets = m_pSliceOffset;

	m_VideoParam.iCurPictureId = m_cTSScheduler.GetFreeIDX();

	m_cTSScheduler.AddFrame(m_VideoParam.iCurPictureId, m_VideoParam.picture_coding_type, m_TemporalReference);

	if (m_cTSScheduler.IsFull()) {
		m_bHaveOuput = true;
	}

  #ifdef TRACE_TIME_REFERENCE
  		if(m_MpegPictureParams.picture_coding_type == 1)
  				TRACE((L"decodePicture : I"));
  		else if(m_MpegPictureParams.picture_coding_type == 2)
  				TRACE((L"decodePicture : P"));
  		else if(m_MpegPictureParams.picture_coding_type == 3)
  				TRACE((L"decodePicture : B"));
  		else
  				TRACE((L"decodePicture : ERROR"));
  #endif

	HRESULT hr;

	hr = Dxva2DecodePicture();
	if (FAILED(hr)) {
		return hr;
	}

	if (m_VideoParam.picture_coding_type != PICTURE_TYPE_B)
		m_iLastPictureP = m_VideoParam.iCurPictureId;

	return hr;
}

HRESULT CMpeg2DecoderDXVA2::Dxva2DecodePicture()
{
	HRESULT hr;

	hr = m_pDeviceManager->TestDevice(m_hD3d9Device);
	if (FAILED(hr)) {
		return hr;
	}

	IDirect3DDevice9 *pDevice;

	hr = m_pDeviceManager->LockDevice(m_hD3d9Device, &pDevice, TRUE);
	if (FAILED(hr)) {
		return hr;
	}

	// We don't handle this case... Don't know why, but this isn't a problem...
	// It seems that EVR leaves the sample useable before we decode onto the surface, and before Invoke is called.
	// We need to schelude samples differently, to be sure there is a free surface (TimeStamp scheduler was first adapted to Cuda decoder).
	// assert(m_bFreeSurface[m_VideoParam.iCurPictureId]);

	// Can stay in infinite loop...
	do {
		hr = m_pVideoDecoder->BeginFrame(m_pSurface9[m_VideoParam.iCurPictureId], NULL);
	} while (hr != S_OK && hr == E_PENDING);

	if (FAILED(hr)) {
		pDevice->Release();
		return hr;
	}

	void *pBuffer = NULL;
	UINT uiSize = 0;

	// Picture
	InitPictureParams();
	hr = m_pVideoDecoder->GetBuffer(DXVA2_PictureParametersBufferType, &pBuffer, &uiSize);
	assert(sizeof(DXVA_PictureParameters) <= uiSize);
	memcpy(pBuffer, &m_PictureParams, sizeof(DXVA_PictureParameters));
	hr = m_pVideoDecoder->ReleaseBuffer(DXVA2_PictureParametersBufferType);

	// QuantaMatrix
	InitQuantaMatrixParams();
	hr = m_pVideoDecoder->GetBuffer(DXVA2_InverseQuantizationMatrixBufferType, &pBuffer, &uiSize);
	_ASSERT(sizeof(DXVA_QmatrixData) <= uiSize);
	memcpy(pBuffer, &m_QuantaMatrix, sizeof(DXVA_QmatrixData));
	hr = m_pVideoDecoder->ReleaseBuffer(DXVA2_InverseQuantizationMatrixBufferType);

	// BitStream
	hr = m_pVideoDecoder->GetBuffer(DXVA2_BitStreamDateBufferType, &pBuffer, &uiSize);
	_ASSERT(m_VideoParam.uiBitstreamDataLen <= uiSize);
	memcpy(pBuffer, m_VideoParam.pBitstreamData, m_VideoParam.uiBitstreamDataLen);
	hr = m_pVideoDecoder->ReleaseBuffer(DXVA2_BitStreamDateBufferType);

	// Slices
	InitSliceParams();
	hr = m_pVideoDecoder->GetBuffer(DXVA2_SliceControlBufferType, &pBuffer, &uiSize);
	_ASSERT(m_VideoParam.uiNumSlices * sizeof(DXVA_SliceInfo) <= uiSize);
	memcpy(pBuffer, &m_SliceInfo, m_VideoParam.uiNumSlices * sizeof(DXVA_SliceInfo));
	hr = m_pVideoDecoder->ReleaseBuffer(DXVA2_SliceControlBufferType);

	// CompBuffers
	m_BufferDesc[2].DataSize = m_VideoParam.uiBitstreamDataLen;
	m_BufferDesc[3].DataSize = m_VideoParam.uiNumSlices * sizeof(DXVA_SliceInfo);

#ifdef TRACE_DXVA2
	  TraceConfigPictureDecode();
	  TracePictureParams();
	  TraceQuantaMatrix();
	  TraceSlices();
	  TraceCompBuffers();
#endif

	hr = m_pVideoDecoder->Execute(&m_ExecuteParams);

	hr = m_pVideoDecoder->EndFrame(NULL);

	hr = m_pDeviceManager->UnlockDevice(m_hD3d9Device, FALSE);

	m_bFreeSurface[m_VideoParam.iCurPictureId] = FALSE;

	pDevice->Release();

	return hr;
}

WORD CMpeg2DecoderDXVA2::GetScaleCode(const uint8_t *pData)
{
	/*if(pData[2] != 0x01)
	return 0;*/

	WORD code = pData[4] >> 3;

	if (pData[4] & 0x04) {
		// Todo : extra slice flag
	}

	//TRACE((L"Slice : %d - Code : %d", pData[3], code));

	return code;
}

void CMpeg2DecoderDXVA2::InitPictureParams()
{
	m_PictureParams.wDecodedPictureIndex = m_VideoParam.iCurPictureId;
	m_PictureParams.wForwardRefPictureIndex = m_VideoParam.iForwardRefIdx;
	m_PictureParams.wBackwardRefPictureIndex = m_VideoParam.iBackwardRefIdx;

	m_PictureParams.bPicIntra = m_VideoParam.intra_pic_flag;
	m_PictureParams.bPicBackwardPrediction = !m_VideoParam.ref_pic_flag;
	m_PictureParams.bPicScanMethod = m_VideoParam.alternate_scan;

	m_PictureParams.wBitstreamFcodes = (m_VideoParam.f_code[0][0] << 12) | (m_VideoParam.f_code[0][1] << 8) | (m_VideoParam.f_code[1][0] << 4) | (m_VideoParam.f_code[1][1]);

	m_PictureParams.wBitstreamPCEelements =
		(m_VideoParam.intra_dc_precision << 14) | (m_VideoParam.picture_structure << 12) | (m_VideoParam.top_field_first << 11) |
		(m_VideoParam.frame_pred_frame_dct << 10) | (m_VideoParam.concealment_motion_vectors << 9) | (m_VideoParam.q_scale_type << 8) | (m_VideoParam.intra_vlc_format << 7) |
		(m_VideoParam.alternate_scan << 6) | (m_VideoParam.repeat_first_field << 5) | (m_VideoParam.chroma_420_type << 4) | (m_VideoParam.progressive_frame << 3) |
		(m_VideoParam.full_pel_forward_vector << 2) | (m_VideoParam.full_pel_backward_vector << 1);

	// Can be set only once
	m_PictureParams.wPicWidthInMBminus1 = m_VideoParam.PicWidthInMbs - 1;
	m_PictureParams.wPicHeightInMBminus1 = m_VideoParam.FrameHeightInMbs - 1;
	m_PictureParams.bMacroblockWidthMinus1 = 15;
	m_PictureParams.bMacroblockHeightMinus1 = 15;
	m_PictureParams.bBlockWidthMinus1 = 7;
	m_PictureParams.bBlockHeightMinus1 = 7;
	m_PictureParams.bBPPminus1 = 7;
	m_PictureParams.bPicStructure = 3;
	m_PictureParams.bChromaFormat = 1;
	m_PictureParams.bPicScanFixed = 1;
	m_PictureParams.bReservedBits = 0;
}

void CMpeg2DecoderDXVA2::InitSliceParams()
{
	_ASSERT(m_VideoParam.uiNumSlices <= MAX_SLICE);

	int iMB = 0;
	int iLastSlice = m_VideoParam.uiNumSlices - 2;

	for (int i = 0; i < m_VideoParam.uiNumSlices; i++) {
		m_SliceInfo[i].wHorizontalPosition = 0;
		m_SliceInfo[i].wVerticalPosition = i;
		m_SliceInfo[i].dwSliceBitsInBuffer = 8 * (i < iLastSlice ? (m_pSliceOffset[i + 1] - m_pSliceOffset[i]) : (m_VideoParam.uiBitstreamDataLen - m_pSliceOffset[i]));// Todo extra flag...
		m_SliceInfo[i].dwSliceDataLocation = m_pSliceOffset[i];
		m_SliceInfo[i].bStartCodeBitOffset = 0;
		m_SliceInfo[i].bReservedBits = 0;
		m_SliceInfo[i].wMBbitOffset = 38;// We must check intra slice flag information, see GetScaleCode...
		m_SliceInfo[i].wNumberMBsInSlice = iMB;
		m_SliceInfo[i].wQuantizerScaleCode = GetScaleCode(&m_VideoParam.pBitstreamData[m_pSliceOffset[i]]);
		m_SliceInfo[i].wBadSliceChopping = 0;

		iMB += m_VideoParam.PicWidthInMbs;
	}
}

void CMpeg2DecoderDXVA2::InitQuantaMatrixParams()
{
	for (int i = 0; i < 4; i++)
		m_QuantaMatrix.bNewQmatrix[i] = 1;

	for (int i = 0; i < 64; i++) {
		m_QuantaMatrix.Qmatrix[0][i] = m_VideoParam.QuantMatrixIntra[i];
		m_QuantaMatrix.Qmatrix[1][i] = m_VideoParam.QuantMatrixInter[i];
		m_QuantaMatrix.Qmatrix[2][i] = m_VideoParam.QuantMatrixIntra[i];
		m_QuantaMatrix.Qmatrix[3][i] = m_VideoParam.QuantMatrixInter[i];
	}
}
