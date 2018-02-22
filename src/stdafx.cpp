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


void DebugTrace(LPCTSTR pszFormat, ...)
{
	TCHAR szText[1024];
	int Length;

	SYSTEMTIME st;
	::GetLocalTime(&st);
	Length = ::_stprintf_s(
		szText, _countof(szText),
		TEXT("%02d/%02d %02d:%02d:%02d TVTVideoDec > "),
		st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	va_list Args;
	va_start(Args, pszFormat);
	Length += ::_vstprintf_s(szText + Length, _countof(szText) - 1 - Length, pszFormat, Args);
	szText[Length] = TEXT('\n');
	szText[Length + 1] = TEXT('\0');
	va_end(Args);

	::OutputDebugString(szText);
}
