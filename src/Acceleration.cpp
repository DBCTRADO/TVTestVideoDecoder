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
#include "Acceleration.h"


#if defined(_M_IX86) || defined(_M_AMD64)

typedef DWORD64 (WINAPI *GetEnabledXStateFeatures_Ptr)();

#ifndef XSTATE_MASK_AVX
#define XSTATE_MASK_AVX XSTATE_MASK_GSSE
#endif

class CCpuAccel
{
public:
	uint32_t m_Supported = 0;
	uint32_t m_Enabled = 0;

	CCpuAccel()
	{
		int info[4];

		__cpuid(info, 0x00000000);
		int Basic = info[0];
		__cpuid(info, 0x00000001);
		if (info[3] & 0x00800000)
			m_Supported |= ACCEL_MMX;
		if (info[3] & 0x02000000)
			m_Supported |= ACCEL_SSE;
		if (info[3] & 0x04000000)
			m_Supported |= ACCEL_SSE2;
		if (info[2] & 0x00000001)
			m_Supported |= ACCEL_SSE3;
		if (info[2] & 0x00000200)
			m_Supported |= ACCEL_SSSE3;
		if (info[2] & 0x00080000)
			m_Supported |= ACCEL_SSE4_1;
		if (info[2] & 0x00100000)
			m_Supported |= ACCEL_SSE4_2;
		if (info[2] & 0x10000000)
			m_Supported |= ACCEL_AVX;
		if (Basic >= 7) {
			__cpuidex(info, 7, 0);
			if (info[1] & 0x00000020)
				m_Supported |= ACCEL_AVX2;
		}

		bool fAVX = false;
		if (::IsProcessorFeaturePresent(PF_XSAVE_ENABLED)) {
			GetEnabledXStateFeatures_Ptr pGetEnabledXStateFeatures =
				(GetEnabledXStateFeatures_Ptr)
					::GetProcAddress(::GetModuleHandle(TEXT("kernel32.dll")),
									 "GetEnabledXStateFeatures");
			if (pGetEnabledXStateFeatures
					&& (pGetEnabledXStateFeatures() & XSTATE_MASK_AVX)) {
				fAVX = true;
			}
		}
		if (!fAVX)
			m_Supported &= ~(ACCEL_AVX | ACCEL_AVX2);

		m_Enabled = m_Supported;
	}
};

static CCpuAccel g_CpuAccel;


uint32_t GetSupportedAccels()
{
	return g_CpuAccel.m_Supported;
}

uint32_t GetEnabledAccels()
{
	return g_CpuAccel.m_Enabled;
}

uint32_t EnableAccels(uint32_t Accels, uint32_t Mask)
{
	return g_CpuAccel.m_Enabled =
		(((Accels & Mask) | (g_CpuAccel.m_Enabled & ~Mask)) & g_CpuAccel.m_Supported);
}

bool IsSSEEnabled()
{
	return (g_CpuAccel.m_Enabled & ACCEL_SSE) != 0;
}

bool IsSSE2Enabled()
{
	return (g_CpuAccel.m_Enabled & ACCEL_SSE2) != 0;
}

bool IsSSE3Enabled()
{
	return (g_CpuAccel.m_Enabled & ACCEL_SSE3) != 0;
}

bool IsSSSE3Enabled()
{
	return (g_CpuAccel.m_Enabled & ACCEL_SSSE3) != 0;
}

bool IsSSE4_1Enabled()
{
	return (g_CpuAccel.m_Enabled & ACCEL_SSE4_1) != 0;
}

bool IsSSE4_2Enabled()
{
	return (g_CpuAccel.m_Enabled & ACCEL_SSE4_2) != 0;
}

bool IsAVXEnabled()
{
	return (g_CpuAccel.m_Enabled & ACCEL_AVX) != 0;
}

bool IsAVX2Enabled()
{
	return (g_CpuAccel.m_Enabled & ACCEL_AVX2) != 0;
}

#endif
