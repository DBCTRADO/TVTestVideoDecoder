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
#include "Util.h"


// GCD (Greatest Common Denominator)
static int GCD(int m, int n)
{
	if (m != 0 && n != 0) {
		int r;

		do {
			r = m % n;
			m = n;
			n = r;
		} while (r != 0);
	} else {
		m = 0;
	}

	return m;
}

void ReduceFraction(int *pNum, int *pDenom)
{
	if (*pDenom < 0) {
		*pDenom = -*pDenom;
		*pNum = -*pNum;
	}

	if (*pDenom != 0 && *pNum != 0) {
		int d = GCD(*pDenom, *pNum);

		if (d != 0) {
			*pNum /= d;
			*pDenom /= d;
		}
	}
}

bool IsVideoInfo(const AM_MEDIA_TYPE *pmt)
{
	return pmt != nullptr
		&& (pmt->formattype == FORMAT_VideoInfo || pmt->formattype == FORMAT_MPEGVideo)
		&& pmt->cbFormat >= sizeof(VIDEOINFOHEADER)
		&& pmt->pbFormat != nullptr;
}

bool IsVideoInfo2(const AM_MEDIA_TYPE *pmt)
{
	return pmt != nullptr
		&& (pmt->formattype == FORMAT_VideoInfo2 || pmt->formattype == FORMAT_MPEG2_VIDEO)
		&& pmt->cbFormat >= sizeof(VIDEOINFOHEADER2)
		&& pmt->pbFormat != nullptr;
}

bool IsMpeg1VideoInfo(const AM_MEDIA_TYPE *pmt)
{
	return pmt != nullptr
		&& pmt->formattype == FORMAT_MPEGVideo
		&& pmt->cbFormat >= sizeof(MPEG1VIDEOINFO)
		&& pmt->pbFormat != nullptr;
}

bool IsMpeg2VideoInfo(const AM_MEDIA_TYPE *pmt)
{
	return pmt != nullptr
		&& pmt->formattype == FORMAT_MPEG2_VIDEO
		&& pmt->cbFormat >= sizeof(MPEG2VIDEOINFO)
		&& pmt->pbFormat != nullptr;
}

bool GetAvgTimePerFrame(const AM_MEDIA_TYPE *pmt, REFERENCE_TIME *prtAvgTimePerFrame)
{
	REFERENCE_TIME rt;

	if (IsVideoInfo(pmt) || IsVideoInfo2(pmt)) {
		rt = ((const VIDEOINFOHEADER*)pmt->pbFormat)->AvgTimePerFrame;
	} else {
		return false;
	}

	if (rt < 1)
		rt = 1;

	*prtAvgTimePerFrame = rt;

	return true;
}

bool GetBitmapInfoHeader(const AM_MEDIA_TYPE *pmt, BITMAPINFOHEADER *pbmih)
{
	if (!pbmih)
		return false;

	if (IsVideoInfo(pmt)) {
		const VIDEOINFOHEADER *pvih = (const VIDEOINFOHEADER*)pmt->pbFormat;
		*pbmih = pvih->bmiHeader;
		return true;
	} else if (IsVideoInfo2(pmt)) {
		const VIDEOINFOHEADER2 *pvih2 = (const VIDEOINFOHEADER2*)pmt->pbFormat;
		*pbmih = pvih2->bmiHeader;
		return true;
	}

	ZeroMemory(pbmih, sizeof(BITMAPINFOHEADER));

	return false;
}

BITMAPINFOHEADER *GetBitmapInfoHeader(AM_MEDIA_TYPE *pmt)
{
	if (IsVideoInfo(pmt)) {
		VIDEOINFOHEADER *pvih = (VIDEOINFOHEADER*)pmt->pbFormat;
		return &pvih->bmiHeader;
	} else if (IsVideoInfo2(pmt)) {
		VIDEOINFOHEADER2 *pvih2 = (VIDEOINFOHEADER2*)pmt->pbFormat;
		return &pvih2->bmiHeader;
	}

	return nullptr;
}

const BITMAPINFOHEADER *GetBitmapInfoHeader(const AM_MEDIA_TYPE *pmt)
{
	if (IsVideoInfo(pmt)) {
		const VIDEOINFOHEADER *pvih = (const VIDEOINFOHEADER*)pmt->pbFormat;
		return &pvih->bmiHeader;
	} else if (IsVideoInfo2(pmt)) {
		const VIDEOINFOHEADER2 *pvih2 = (const VIDEOINFOHEADER2*)pmt->pbFormat;
		return &pvih2->bmiHeader;
	}

	return nullptr;
}

CLSID GetConnectedFilterCLSID(CBasePin *pPin)
{
	CLSID clsid = GUID_NULL;

	if (pPin) {
		IPin *pConnectedPin = pPin->GetConnected();

		if (pConnectedPin) {
			PIN_INFO pi;

			if (SUCCEEDED(pConnectedPin->QueryPinInfo(&pi))) {
				if (pi.pFilter) {
					pi.pFilter->GetClassID(&clsid);
					pi.pFilter->Release();
				}
			}
		}
	}

	return clsid;
}

bool IsMediaTypeInterlaced(const AM_MEDIA_TYPE *pmt)
{
	return IsVideoInfo2(pmt)
		&& (((const VIDEOINFOHEADER2*)pmt->pbFormat)->dwInterlaceFlags & AMINTERLACE_IsInterlaced);
}

SIZE GetDXVASurfaceSize(const SIZE &Size, bool fMPEG2)
{
	/*
		NOTE: from ffmpeg
		decoding MPEG-2 requires additional alignment on some Intel GPUs,
		but it causes issues for H.264 on certain AMD GPUs.....
	*/
	const int Align = fMPEG2 ? 32 : 16;

	SIZE AlignedSize;
	AlignedSize.cx = (Size.cx + (Align - 1)) & ~(Align - 1);
	AlignedSize.cy = (Size.cy + (Align - 1)) & ~(Align - 1);

	return AlignedSize;
}

bool IsWindows8OrGreater()
{
	OSVERSIONINFOEX osvi = {sizeof(OSVERSIONINFOEX)};
	osvi.dwMajorVersion = 6;
	osvi.dwMinorVersion = 2;

	DWORDLONG ConditionMask = 0;
	ConditionMask |= ::VerSetConditionMask(ConditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
	ConditionMask |= ::VerSetConditionMask(ConditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);

	return ::VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION, ConditionMask) != FALSE;
}
