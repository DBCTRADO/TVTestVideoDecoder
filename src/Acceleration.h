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


#if defined(_M_IX86) || defined(_M_AMD64)

#define ACCEL_MMX    0x00000001
#define ACCEL_SSE    0x00000002
#define ACCEL_SSE2   0x00000004
#define ACCEL_SSE3   0x00000008
#define ACCEL_SSSE3  0x00000010
#define ACCEL_SSE4_1 0x00000020
#define ACCEL_SSE4_2 0x00000040
#define ACCEL_AVX    0x00000080
#define ACCEL_AVX2   0x00000100

uint32_t GetSupportedAccels();
uint32_t GetEnabledAccels();
uint32_t EnableAccels(uint32_t Accels, uint32_t Mask = 0xffffffff);

bool IsSSEEnabled();
bool IsSSE2Enabled();
bool IsSSE3Enabled();
bool IsSSSE3Enabled();
bool IsSSE4_1Enabled();
bool IsSSE4_2Enabled();
bool IsAVXEnabled();
bool IsAVX2Enabled();

#endif
