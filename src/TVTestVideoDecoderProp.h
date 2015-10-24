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


#include "ITVTestVideoDecoder.h"


class __declspec(uuid("8D11F434-B4DF-458B-34F4-118DDFB48B45")) CTVTestVideoDecoderProp
	: public CBasePropertyPage
{
public:
	CTVTestVideoDecoderProp(LPUNKNOWN lpunk, HRESULT *phr);

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
	struct Settings
	{
		bool fEnableDeinterlace;
		TVTVIDEODEC_DeinterlaceMethod DeinterlaceMethod;
		bool fAdaptProgressive;
		bool fAdaptTelecine;
		bool fSetInterlacedFlag;
		int Brightness;
		int Contrast;
		int Hue;
		int Saturation;
		int NumThreads;
		bool fEnableDXVA2;
	};

	ITVTestVideoDecoder *m_pDecoder;
	Settings m_OldSettings;
	Settings m_NewSettings;
	bool m_fInitialized;

	~CTVTestVideoDecoderProp();
	void MakeDirty();
};
