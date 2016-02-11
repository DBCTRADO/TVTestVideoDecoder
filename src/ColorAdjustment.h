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


class CColorAdjustment
{
public:
	CColorAdjustment();
	void SetBrightness(int Brightness);
	int GetBrightness() const { return m_Brightness; }
	void SetContrast(int Contrast);
	int GetContrast() const { return m_Contrast; }
	void SetHue(int Hue);
	int GetHue() const { return m_Hue; }
	void SetSaturation(int Saturation);
	int GetSaturation() const { return m_Saturation; }
	bool IsEffective() const;
	void ProcessY(int Width, int Height, uint8_t *pData, int Pitch);
	void ProcessI420(
		int Width, int Height,
		uint8_t * restrict pDataY, uint8_t * restrict pDataU, uint8_t * restrict pDataV,
		int PitchY, int PitchC);
	void ProcessNV12(
		int Width, int Height,
		uint8_t * restrict pDataY, uint8_t * restrict pDataUV, int Pitch);

private:
	union UV8 {
		struct {
			uint8_t u;
			uint8_t v;
		};
		uint16_t uv;
	};

	int m_Brightness;
	int m_Contrast;
	int m_Hue;
	int m_Saturation;
	uint8_t m_YTable[256];
	UV8 m_UVTable[256 * 256];
	bool m_fUpdateYTable;
	bool m_fUpdateUVTable;

	static void MakeYTable(uint8_t *pYTable, int Brightness, int Contrast);
	static void MakeUVTable(UV8 *pUVTable, int Hue, int Saturation);
};
