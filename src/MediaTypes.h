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


DEFINE_GUID(MEDIASUBTYPE_MPG2, 0x3267706D, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
DEFINE_GUID(MEDIASUBTYPE_I420, 0x30323449, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

#define FOURCC_I420 MAKEFOURCC('I','4','2','0')
#define FOURCC_IMC3 MAKEFOURCC('I','M','C','3')
#define FOURCC_IYUV MAKEFOURCC('I','Y','U','V')
#define FOURCC_NV12 MAKEFOURCC('N','V','1','2')
#define FOURCC_YV12 MAKEFOURCC('Y','V','1','2')
#define FOURCC_dxva MAKEFOURCC('d','x','v','a')

#define D3DFMT_NV12 ((D3DFORMAT)FOURCC_NV12)
#define D3DFMT_IMC3 ((D3DFORMAT)FOURCC_IMC3)
