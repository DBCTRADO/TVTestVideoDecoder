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


#include "ITVTestVideoDecoder.h"
#include "COMUtil.h"


class __declspec(uuid("48749A3E-2680-4833-93AF-8B93914B376F")) CTVTestVideoDecoderStat
	: public CBasePropertyPage
{
public:
	CTVTestVideoDecoderStat(LPUNKNOWN lpunk, HRESULT *phr);

	static CUnknown * CALLBACK CreateInstance(LPUNKNOWN punk, HRESULT *phr);

// CUnknown

	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void **ppv) override;

// CBasePropertyPage

	HRESULT OnConnect(IUnknown *pUnknown) override;
	HRESULT OnDisconnect() override;
	HRESULT OnActivate() override;
	HRESULT OnApplyChanges() override;
	INT_PTR OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
	COMPointer<ITVTestVideoDecoder> m_Decoder;
	TVTVIDEODEC_Statistics m_Stat;

	~CTVTestVideoDecoderStat();
	void MakeDirty();

	void UpdateOutSize(const TVTVIDEODEC_Statistics &Stat);
	void UpdatePlaybackRate(LONG PlaybackRate);
	void UpdateBaseFPS(LONGLONG BaseTimePerFrame);
	void UpdateMode(DWORD Mode);
	void UpdateDecoderDeviceDescription(LPCWSTR pszDescription);
};
