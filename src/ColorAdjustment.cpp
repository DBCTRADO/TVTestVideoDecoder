/*
 *  TVTest DTV Video Decoder
 *  Copyright (C) 2015-2018 DBCTRADO
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
#include <cmath>
#include "ColorAdjustment.h"
#include "Acceleration.h"


template<typename T> inline T Clamp(T Value, T Min, T Max)
{
	return (Value > Max) ? Max : ((Value < Min) ? Min : Value);
}

CColorAdjustment::CColorAdjustment()
	: m_Brightness(0)
	, m_Contrast(0)
	, m_Hue(0)
	, m_Saturation(0)
	, m_fUpdateYTable(true)
	, m_fUpdateUVTable(true)
{
}

void CColorAdjustment::SetBrightness(int Brightness)
{
	int NewBrightness = Clamp(Brightness, -100, 100);
	if (NewBrightness != m_Brightness) {
		m_Brightness = NewBrightness;
		m_fUpdateYTable = true;
	}
}

void CColorAdjustment::SetContrast(int Contrast)
{
	int NewContrast = Clamp(Contrast, -100, 100);
	if (NewContrast != m_Contrast) {
		m_Contrast = NewContrast;
		m_fUpdateYTable = true;
	}
}

void CColorAdjustment::SetHue(int Hue)
{
	int NewHue = Clamp(Hue, -180, 180);
	if (NewHue != m_Hue) {
		m_Hue = NewHue;
		m_fUpdateUVTable = true;
	}
}

void CColorAdjustment::SetSaturation(int Saturation)
{
	int NewSaturation = Clamp(Saturation, -100, 100);
	if (NewSaturation != m_Saturation) {
		m_Saturation = NewSaturation;
		m_fUpdateUVTable = true;
	}
}

bool CColorAdjustment::IsEffective() const
{
	return m_Brightness != 0
		|| m_Contrast != 0
		|| m_Hue != 0
		|| m_Saturation != 0;
}

void CColorAdjustment::ProcessY(int Width, int Height, uint8_t *pData, int Pitch)
{
	if (m_Brightness != 0 || m_Contrast != 0) {
		if (m_fUpdateYTable) {
			MakeYTable(m_YTable, m_Brightness, m_Contrast);
			m_fUpdateYTable = false;
		}

#ifdef TVTVIDEODEC_SSE2_SUPPORT
		const bool fSSE2 = IsSSE2Enabled();
#endif

		for (int y = 0; y < Height; y++) {
			uint8_t *p = pData;
			int x = 0;

#ifdef TVTVIDEODEC_SSE2_SUPPORT
			if (fSSE2 && !((uintptr_t)p & 15)) {
				const short c = (short)(min((m_Contrast * 512 / 100) + 512, (1 << 16) - 1));
				const short b = (short)((m_Brightness * 255 / 100) + 16);
				const __m128i bc = _mm_set_epi16(b, c, b, c, b, c, b, c);
				const __m128i zero = _mm_setzero_si128();
				const __m128i w16 = _mm_set1_epi16(16);
				const __m128i w512 = _mm_set1_epi16(512);

				for (; x + 16 <= Width; x += 16) {
					__m128i r = _mm_load_si128((const __m128i*)p);

					__m128i rl = _mm_unpacklo_epi8(r, zero);
					__m128i rh = _mm_unpackhi_epi8(r, zero);

					rl = _mm_subs_epi16(rl, w16);
					rh = _mm_subs_epi16(rh, w16);

					__m128i rll = _mm_unpacklo_epi16(rl, w512);
					__m128i rlh = _mm_unpackhi_epi16(rl, w512);
					__m128i rhl = _mm_unpacklo_epi16(rh, w512);
					__m128i rhh = _mm_unpackhi_epi16(rh, w512);

					rll = _mm_madd_epi16(rll, bc);
					rlh = _mm_madd_epi16(rlh, bc);
					rhl = _mm_madd_epi16(rhl, bc);
					rhh = _mm_madd_epi16(rhh, bc);

					rll = _mm_srai_epi32(rll, 9);
					rlh = _mm_srai_epi32(rlh, 9);
					rhl = _mm_srai_epi32(rhl, 9);
					rhh = _mm_srai_epi32(rhh, 9);

					rl = _mm_packs_epi32(rll, rlh);
					rh = _mm_packs_epi32(rhl, rhh);

					r = _mm_packus_epi16(rl, rh);

					_mm_store_si128((__m128i*)p, r);

					p += 16;
				}
			}
#endif

			for (; x < Width; x++) {
				*p = m_YTable[*p];
				p++;
			}

			pData += Pitch;
		}
	}
}

void CColorAdjustment::ProcessI420(
	int Width, int Height,
	uint8_t * restrict pDataY, uint8_t * restrict pDataU, uint8_t * restrict pDataV,
	int PitchY, int PitchC)
{
	ProcessY(Width, Height, pDataY, PitchY);

	if (m_Hue != 0 || m_Saturation != 0) {
		if (m_fUpdateUVTable) {
			MakeUVTable(m_UVTable, m_Hue, m_Saturation);
			m_fUpdateUVTable = false;
		}

		const int WidthC = Width >> 1;
		const int HeightC = Height >> 1;

		for (int y = 0; y < HeightC; y++) {
			uint8_t * restrict u = pDataU, * restrict v = pDataV;

			for (int x = 0; x < WidthC; x++) {
				unsigned int i = (*v << 8) | *u;

				*u++ = m_UVTable[i].u;
				*v++ = m_UVTable[i].v;
			}

			pDataU += PitchC;
			pDataV += PitchC;
		}
	}
}

void CColorAdjustment::ProcessNV12(
	int Width, int Height,
	uint8_t * restrict pDataY, uint8_t * restrict pDataUV, int Pitch)
{
	ProcessY(Width, Height, pDataY, Pitch);

	if (m_Hue != 0 || m_Saturation != 0) {
		if (m_fUpdateUVTable) {
			MakeUVTable(m_UVTable, m_Hue, m_Saturation);
			m_fUpdateUVTable = false;
		}

		const int WidthC = Width >> 1;
		const int HeightC = Height >> 1;

		for (int y = 0; y < HeightC; y++) {
			uint16_t * restrict uv = (uint16_t*)pDataUV;

			for (int x = 0; x < WidthC; x++) {
				*uv = m_UVTable[*uv].uv;
				uv++;
			}

			pDataUV += Pitch;
		}
	}
}

void CColorAdjustment::MakeYTable(uint8_t *pYTable, int Brightness, int Contrast)
{
	int c = Contrast * 512 / 100 + 512;
	int b = Brightness * 255 / 100;

	for (int i = 0; i < 256; i++) {
		int y = ((c * (i - 16)) >> 9) + b + 16;
		//pYTable[i] = (uint8_t)Clamp(y, 16, 235);
		pYTable[i] = (uint8_t)Clamp(y, 0, 255);
	}
}

void CColorAdjustment::MakeUVTable(UV8 *pUVTable, int Hue, int Saturation)
{
	const int sat = Saturation * 512 / 100 + 512;
	const double hue = ((double)Hue * M_PI) / 180.0;
	const int Sin = (int)(std::sin(hue) * 4096.0);
	const int Cos = (int)(std::cos(hue) * 4096.0);

	for (int i = 0; i < 256; i++) {
		for (int j = 0; j < 256; j++) {
			int u = j - 128;
			int v = i - 128;
			int ux = (u * Cos + v * Sin) >> 12;
			v = (v * Cos - u * Sin) >> 12;
			u = ((ux * sat) >> 9) + 128;
			v = ((v * sat) >> 9) + 128;
			pUVTable[(i << 8) | j].u = (uint8_t)Clamp(u, 16, 235);
			pUVTable[(i << 8) | j].v = (uint8_t)Clamp(v, 16, 235);
		}
	}
}
