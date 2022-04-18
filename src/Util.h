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


template<typename T> inline void SafeDelete(T *&p)
{
	if (p) {
		delete p;
		p = nullptr;
	}
}

template<typename T> inline void SafeDeleteArray(T *&p)
{
	if (p) {
		delete [] p;
		p = nullptr;
	}
}

template<typename T> inline void SafeRelease(T *&p)
{
	if (p) {
		p->Release();
		p = nullptr;
	}
}

inline int PopCount(uint32_t v)
{
	// return _mm_popcnt_u32(v);
	v = v - ((v >> 1) & 0x55555555);
	v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
	return (((v + (v >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

void ReduceFraction(int *pNum, int *pDenom);

bool IsVideoInfo(const AM_MEDIA_TYPE *pmt);
bool IsVideoInfo2(const AM_MEDIA_TYPE *pmt);
bool IsMpeg1VideoInfo(const AM_MEDIA_TYPE *pmt);
bool IsMpeg2VideoInfo(const AM_MEDIA_TYPE *pmt);
bool GetAvgTimePerFrame(const AM_MEDIA_TYPE *pmt, REFERENCE_TIME *prtAvgTimePerFrame);
bool GetBitmapInfoHeader(const AM_MEDIA_TYPE *pmt, BITMAPINFOHEADER *pbmih);
BITMAPINFOHEADER *GetBitmapInfoHeader(AM_MEDIA_TYPE *pmt);
const BITMAPINFOHEADER *GetBitmapInfoHeader(const AM_MEDIA_TYPE *pmt);
CLSID GetConnectedFilterCLSID(CBasePin *pPin);
bool IsMediaTypeInterlaced(const AM_MEDIA_TYPE *pmt);
SIZE GetDXVASurfaceSize(const SIZE &Size, bool fMPEG2);
bool IsWindows8OrGreater();
