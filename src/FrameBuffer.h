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


#include "ITVTestVideoDecoder.h"
#include "MediaTypes.h"


enum : uint32_t {
	FRAME_FLAG_TOP_FIELD_FIRST      = 0x00000001,
	FRAME_FLAG_REPEAT_FIRST_FIELD   = 0x00000002,
	FRAME_FLAG_PROGRESSIVE_FRAME    = 0x00000004,
	FRAME_FLAG_PROGRESSIVE_SEQUENCE = 0x00000008,
	FRAME_FLAG_I_FRAME              = 0x00000010,
	FRAME_FLAG_P_FRAME              = 0x00000020,
	FRAME_FLAG_B_FRAME              = 0x00000040,
	FRAME_FLAG_IPB_FRAME_MASK       = 0x00000070,
	FRAME_FLAG_SAMPLE_DATA          = 0x00000080
};

class CFrameBuffer
{
public:
	int m_Width;
	int m_Height;
	int m_PitchY;
	int m_PitchC;
	uint8_t *m_pBuffer;
	uint8_t *m_Buffer[3];
	interface IDirect3DSurface9 *m_pSurface;
	GUID m_Subtype;
	int m_AspectX;
	int m_AspectY;
	REFERENCE_TIME m_rtStart;
	REFERENCE_TIME m_rtStop;
	uint32_t m_Flags;
	TVTVIDEODEC_DeinterlaceMethod m_Deinterlace;

	CFrameBuffer();
	~CFrameBuffer();
	bool Allocate(int Width, int Height, REFGUID Subtype = MEDIASUBTYPE_I420);
	void Free();
	bool CopyAttributesFrom(const CFrameBuffer *pBuffer);
	bool CopyReferenceTo(CFrameBuffer *pBuffer) const;
};
