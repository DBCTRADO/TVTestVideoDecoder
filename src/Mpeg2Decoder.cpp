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
#include "Mpeg2Decoder.h"
#include "Util.h"
#include "MediaTypes.h"
#include "libmpeg2/libmpeg2/mpeg2_internal.h"


CMpeg2Decoder::CMpeg2Decoder()
	: m_pDec(nullptr)
	, m_NumThreads(0)
{
}

CMpeg2Decoder::~CMpeg2Decoder()
{
	Close();
}

bool CMpeg2Decoder::Open()
{
	Close();

	mpeg2_threads(m_NumThreads);

	m_pDec = mpeg2_init();

	if (!m_pDec)
		return false;

	return true;
}

void CMpeg2Decoder::Close()
{
	if (m_pDec) {
		mpeg2_close(m_pDec);
		m_pDec = nullptr;
	}
}

void CMpeg2Decoder::PutBuffer(const uint8_t *pBuffer, size_t Size)
{
	if (m_pDec && pBuffer && Size)
		mpeg2_buffer(m_pDec, const_cast<uint8_t*>(pBuffer), const_cast<uint8_t*>(pBuffer + Size));
}

mpeg2_state_t CMpeg2Decoder::Parse()
{
	if (!m_pDec)
		return STATE_INVALID;

	return mpeg2_parse(m_pDec);
}

void CMpeg2Decoder::Skip(bool fSkip)
{
	if (m_pDec)
		mpeg2_skip(m_pDec, fSkip);
}

bool CMpeg2Decoder::GetOutputSize(int *pWidth, int *pHeight) const
{
	if (!m_pDec)
		return false;

	const mpeg2_info_t *pInfo = mpeg2_info(m_pDec);

	*pWidth = pInfo->sequence->picture_width;
	*pHeight = pInfo->sequence->picture_height;

	return true;
}

bool CMpeg2Decoder::GetAspectRatio(int *pAspectX, int *pAspectY) const
{
	if (m_pDec) {
		const mpeg2_info_t *pInfo = mpeg2_info(m_pDec);

		if (pInfo->sequence->pixel_width && pInfo->sequence->pixel_height) {
			*pAspectX = pInfo->sequence->picture_width * pInfo->sequence->pixel_width;
			*pAspectY = pInfo->sequence->picture_height * pInfo->sequence->pixel_height;
			ReduceFraction(pAspectX, pAspectY);
			return true;
		}
	}

	return false;
}

bool CMpeg2Decoder::GetFrame(CFrameBuffer *pFrameBuffer) const
{
	if (!m_pDec)
		return false;

	const mpeg2_info_t *pInfo = mpeg2_info(m_pDec);
	if (!pInfo->display_fbuf)
		return false;

	pFrameBuffer->m_Width = pInfo->sequence->picture_width;
	pFrameBuffer->m_Height = pInfo->sequence->picture_height;
	pFrameBuffer->m_PitchY = pInfo->sequence->width;
	pFrameBuffer->m_PitchC = pInfo->sequence->chroma_width;
	pFrameBuffer->m_Buffer[0] = pInfo->display_fbuf->buf[0];
	pFrameBuffer->m_Buffer[1] = pInfo->display_fbuf->buf[1];
	pFrameBuffer->m_Buffer[2] = pInfo->display_fbuf->buf[2];
	pFrameBuffer->m_Subtype = MEDIASUBTYPE_I420;

	return true;
}

bool CMpeg2Decoder::GetFrameFlags(uint32_t *pFlags) const
{
	if (!m_pDec)
		return false;

	const mpeg2_info_t *pInfo = mpeg2_info(m_pDec);
	const uint32_t PicFlags = pInfo->display_picture->flags;
	uint32_t Flags = 0;

	if (PicFlags & PIC_FLAG_TOP_FIELD_FIRST)
		Flags |= FRAME_FLAG_TOP_FIELD_FIRST;
	if (PicFlags & PIC_FLAG_REPEAT_FIRST_FIELD)
		Flags |= FRAME_FLAG_REPEAT_FIRST_FIELD;
	if (PicFlags & PIC_FLAG_PROGRESSIVE_FRAME)
		Flags |= FRAME_FLAG_PROGRESSIVE_FRAME;
	if (pInfo->sequence->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE)
		Flags |= FRAME_FLAG_PROGRESSIVE_SEQUENCE;
	switch (PicFlags & PIC_MASK_CODING_TYPE) {
	case PIC_FLAG_CODING_TYPE_I: Flags |= FRAME_FLAG_I_FRAME; break;
	case PIC_FLAG_CODING_TYPE_P: Flags |= FRAME_FLAG_P_FRAME; break;
	case PIC_FLAG_CODING_TYPE_B: Flags |= FRAME_FLAG_B_FRAME; break;
	}

	*pFlags = Flags;

	return true;
}

const mpeg2_info_t *CMpeg2Decoder::GetMpeg2Info() const
{
	if (!m_pDec)
		return nullptr;
	return mpeg2_info(m_pDec);
}

const mpeg2_picture_t *CMpeg2Decoder::GetPicture() const
{
	if (!m_pDec)
		return nullptr;
	return m_pDec->picture;
}

int CMpeg2Decoder::GetPictureIndex(const mpeg2_picture_t *pPicture) const
{
	if (!m_pDec)
		return -1;

	if (pPicture < &m_pDec->pictures[0] || pPicture > &m_pDec->pictures[3])
		return -1;

	return static_cast<int>(pPicture - &m_pDec->pictures[0]);
}

void CMpeg2Decoder::SetNumThreads(int NumThreads)
{
	m_NumThreads = NumThreads;
}
