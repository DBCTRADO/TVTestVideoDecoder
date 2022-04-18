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
#include <commctrl.h>
#include "TVTestVideoDecoderStat.h"
#include "Util.h"
#include "Version.h"
#include "resource.h"


#define LTEXT_(text) L##text
#define LTEXT(text)  LTEXT_(text)


CTVTestVideoDecoderStat::CTVTestVideoDecoderStat(LPUNKNOWN lpunk, HRESULT *phr)
	: CBasePropertyPage(L"TVTestVideoDecoderStat", lpunk, IDD_STAT, IDS_STAT_TITLE)
	, m_pDecoder(nullptr)
{
	if (phr)
		*phr = S_OK;
}

CTVTestVideoDecoderStat::~CTVTestVideoDecoderStat()
{
	if (m_pDecoder != nullptr)
		m_pDecoder->Release();
}

CUnknown * CALLBACK CTVTestVideoDecoderStat::CreateInstance(LPUNKNOWN punk, HRESULT *phr)
{
	CTVTestVideoDecoderStat *pPage = DNew_nothrow CTVTestVideoDecoderStat(punk, phr);

	return pPage;
}

STDMETHODIMP CTVTestVideoDecoderStat::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
	if (riid == __uuidof(CTVTestVideoDecoderStat))
		return GetInterface(this, ppv);
	if (riid == __uuidof(IPropertyPage))
		return GetInterface(static_cast<IPropertyPage*>(this), ppv);

	return __super::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CTVTestVideoDecoderStat::OnConnect(IUnknown *pUnknown)
{
	if (pUnknown == nullptr)
		return E_POINTER;
	if (m_pDecoder != nullptr)
		return E_UNEXPECTED;

	ITVTestVideoDecoder *pDecoder;
	HRESULT hr = pUnknown->QueryInterface(IID_PPV_ARGS(&pDecoder));
	if (SUCCEEDED(hr))
		m_pDecoder = pDecoder;

	return hr;
}

HRESULT CTVTestVideoDecoderStat::OnDisconnect()
{
	SafeRelease(m_pDecoder);

	return S_OK;
}

HRESULT CTVTestVideoDecoderStat::OnActivate()
{
	if (m_pDecoder == nullptr)
		return E_UNEXPECTED;

	::ZeroMemory(&m_Stat, sizeof(m_Stat));
	m_Stat.Mask = TVTVIDEODEC_STAT_ALL;
	m_pDecoder->GetStatistics(&m_Stat);

	UpdateOutSize(m_Stat);
	::SetDlgItemInt(m_Dlg, IDC_STAT_I_FRAME_COUNT, m_Stat.IFrameCount, FALSE);
	::SetDlgItemInt(m_Dlg, IDC_STAT_P_FRAME_COUNT, m_Stat.PFrameCount, FALSE);
	::SetDlgItemInt(m_Dlg, IDC_STAT_B_FRAME_COUNT, m_Stat.BFrameCount, FALSE);
	::SetDlgItemInt(m_Dlg, IDC_STAT_SKIPPED_FRAME_COUNT, m_Stat.SkippedFrameCount, FALSE);
	::SetDlgItemInt(m_Dlg, IDC_STAT_REPEAT_FIELD_COUNT, m_Stat.RepeatFieldCount, FALSE);
	UpdatePlaybackRate(m_Stat.PlaybackRate);
	UpdateBaseFPS(m_Stat.BaseTimePerFrame);
	UpdateMode(m_Stat.Mode);
	UpdateDecoderDeviceDescription(m_Stat.HardwareDecoderInfo.Description);

	::SetDlgItemTextW(m_Dlg, IDC_STAT_VERSION,
					  TVTVIDEODEC_FILTER_NAME L" ver." LTEXT(TVTVIDEODEC_VERSION_TEXT)
#ifdef TVTVIDEODEC_VERSION_STATUS
					  L"-" LTEXT(TVTVIDEODEC_VERSION_STATUS)
#endif
					  );

	::SetTimer(m_Dlg, 1, 500, nullptr);

	return S_OK;
}

HRESULT CTVTestVideoDecoderStat::OnApplyChanges()
{
	return S_OK;
}

INT_PTR CTVTestVideoDecoderStat::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_TIMER:
		if (m_pDecoder) {
			TVTVIDEODEC_Statistics Stat = {};

			Stat.Mask = TVTVIDEODEC_STAT_ALL;
			m_pDecoder->GetStatistics(&Stat);

			if (Stat.OutWidth != m_Stat.OutWidth
					|| Stat.OutHeight != m_Stat.OutHeight
					|| Stat.OutAspectX != m_Stat.OutAspectX
					|| Stat.OutAspectY != m_Stat.OutAspectY)
				UpdateOutSize(Stat);
			if (Stat.IFrameCount != m_Stat.IFrameCount)
				::SetDlgItemInt(hwnd, IDC_STAT_I_FRAME_COUNT, Stat.IFrameCount, FALSE);
			if (Stat.PFrameCount != m_Stat.PFrameCount)
				::SetDlgItemInt(hwnd, IDC_STAT_P_FRAME_COUNT, Stat.PFrameCount, FALSE);
			if (Stat.BFrameCount != m_Stat.BFrameCount)
				::SetDlgItemInt(hwnd, IDC_STAT_B_FRAME_COUNT, Stat.BFrameCount, FALSE);
			if (Stat.SkippedFrameCount != m_Stat.SkippedFrameCount)
				::SetDlgItemInt(hwnd, IDC_STAT_SKIPPED_FRAME_COUNT, Stat.SkippedFrameCount, FALSE);
			if (Stat.RepeatFieldCount != m_Stat.RepeatFieldCount)
				::SetDlgItemInt(hwnd, IDC_STAT_REPEAT_FIELD_COUNT, Stat.RepeatFieldCount, FALSE);
			if (Stat.PlaybackRate != m_Stat.PlaybackRate)
				UpdatePlaybackRate(Stat.PlaybackRate);
			if (Stat.BaseTimePerFrame != m_Stat.BaseTimePerFrame)
				UpdateBaseFPS(Stat.BaseTimePerFrame);
			if (Stat.Mode != m_Stat.Mode)
				UpdateMode(Stat.Mode);
			if (::lstrcmp(Stat.HardwareDecoderInfo.Description, m_Stat.HardwareDecoderInfo.Description) != 0)
				UpdateDecoderDeviceDescription(m_Stat.HardwareDecoderInfo.Description);

			m_Stat = Stat;
		}
		return TRUE;
	}

	return __super::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

void CTVTestVideoDecoderStat::UpdateOutSize(const TVTVIDEODEC_Statistics &Stat)
{
	TCHAR szText[64];

	::wsprintf(szText, TEXT("%d x %d (%d:%d)"),
			   Stat.OutWidth, Stat.OutHeight, Stat.OutAspectX, Stat.OutAspectY);
	::SetDlgItemText(m_Dlg, IDC_STAT_OUT_SIZE, szText);
}

void CTVTestVideoDecoderStat::UpdatePlaybackRate(LONG PlaybackRate)
{
	TCHAR szText[64];

	::wsprintf(szText, TEXT("%d.%02d"),
			   PlaybackRate / 100, abs(PlaybackRate % 100));
	::SetDlgItemText(m_Dlg, IDC_STAT_PLAYBACK_RATE, szText);
}

void CTVTestVideoDecoderStat::UpdateBaseFPS(LONGLONG BaseTimePerFrame)
{
	int FPS = BaseTimePerFrame ? (int)(1000000000LL / BaseTimePerFrame) : 0;
	TCHAR szText[64];

	::wsprintf(szText, TEXT("%d.%02d"), FPS / 100, abs(FPS % 100));
	::SetDlgItemText(m_Dlg, IDC_STAT_BASE_FPS, szText);
}

void CTVTestVideoDecoderStat::UpdateMode(DWORD Mode)
{
	::SetDlgItemText(
		m_Dlg, IDC_STAT_MODE,
		(Mode & TVTVIDEODEC_MODE_DXVA2) ? TEXT("DXVA2") :
		(Mode & TVTVIDEODEC_MODE_D3D11) ? TEXT("D3D11") :
		TEXT("Software"));
}

void CTVTestVideoDecoderStat::UpdateDecoderDeviceDescription(LPCWSTR pszDescription)
{
	::SetDlgItemText(m_Dlg, IDC_STAT_HARDWARE_DECODER_DESCRIPTION, pszDescription);
}

void CTVTestVideoDecoderStat::MakeDirty()
{
	m_bDirty = TRUE;
	if (m_pPageSite) {
		m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
	}
}
