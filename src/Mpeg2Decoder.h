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


#include "libmpeg2/vc++/config.h"
#include "libmpeg2/include/mpeg2.h"
#include "FrameBuffer.h"
#include "Common.h"


class CMpeg2Decoder
{
public:
	struct PictureStatus
	{
		REFERENCE_TIME rtStart = INVALID_TIME;
	};

	CMpeg2Decoder();
	virtual ~CMpeg2Decoder();
	virtual bool Open();
	virtual void Close();
	void PutBuffer(const uint8_t *pBuffer, size_t Size);
	virtual mpeg2_state_t Parse();
	void Skip(bool fSkip);
	bool GetOutputSize(int *pWidth, int *pHeight) const;
	bool GetAspectRatio(int *pAspectX, int *pAspectY) const;
	bool GetFrame(CFrameBuffer *pFrameBuffer) const;
	bool GetFrameFlags(uint32_t *pFlags) const;
	const mpeg2_info_t *GetMpeg2Info() const;
	const mpeg2_picture_t *GetPicture() const;
	PictureStatus &GetPictureStatus(const mpeg2_picture_t *pPicture);
	void SetNumThreads(int NumThreads);

protected:
	mpeg2dec_t *m_pDec;
	int m_NumThreads;
	PictureStatus m_PictureStatus[4];

	int GetPictureIndex(const mpeg2_picture_t *pPicture) const;
};
