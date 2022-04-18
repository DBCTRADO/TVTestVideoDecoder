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
#include "TVTestVideoDecoderProp.h"
#include "Util.h"
#include "resource.h"


static void EnableDlgItem(HWND hDlg, int ID, bool fEnable)
{
	::EnableWindow(::GetDlgItem(hDlg, ID), fEnable);
}

static void EnableDlgItems(HWND hDlg, int FirstID, int LastID, bool fEnable)
{
	for (int i = FirstID; i <= LastID; i++) {
		EnableDlgItem(hDlg, i, fEnable);
	}
}


CTVTestVideoDecoderProp::CTVTestVideoDecoderProp(LPUNKNOWN lpunk, HRESULT *phr)
	: CBasePropertyPage(L"TVTestVideoDecoderProp", lpunk, IDD_PROP, IDS_PROP_TITLE)
	, m_pDecoder(nullptr)
	, m_fInitialized(false)
{
	if (phr)
		*phr = S_OK;
}

CTVTestVideoDecoderProp::~CTVTestVideoDecoderProp()
{
	if (m_pDecoder != nullptr)
		m_pDecoder->Release();
}

CUnknown * CALLBACK CTVTestVideoDecoderProp::CreateInstance(LPUNKNOWN punk, HRESULT *phr)
{
	CTVTestVideoDecoderProp *pPage = DNew_nothrow CTVTestVideoDecoderProp(punk, phr);

	return pPage;
}

STDMETHODIMP CTVTestVideoDecoderProp::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
	if (riid == __uuidof(CTVTestVideoDecoderProp))
		return GetInterface(this, ppv);
	if (riid == __uuidof(IPropertyPage))
		return GetInterface(static_cast<IPropertyPage*>(this), ppv);

	return __super::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CTVTestVideoDecoderProp::OnConnect(IUnknown *pUnknown)
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

HRESULT CTVTestVideoDecoderProp::OnDisconnect()
{
	if (m_pDecoder) {
		m_pDecoder->SetEnableDeinterlace(m_OldSettings.fEnableDeinterlace);
		m_pDecoder->SetDeinterlaceMethod(m_OldSettings.DeinterlaceMethod);
		m_pDecoder->SetAdaptProgressive(m_OldSettings.fAdaptProgressive);
		m_pDecoder->SetAdaptTelecine(m_OldSettings.fAdaptTelecine);
		m_pDecoder->SetInterlacedFlag(m_OldSettings.fSetInterlacedFlag);
		m_pDecoder->SetBrightness(m_OldSettings.Brightness);
		m_pDecoder->SetContrast(m_OldSettings.Contrast);
		m_pDecoder->SetHue(m_OldSettings.Hue);
		m_pDecoder->SetSaturation(m_OldSettings.Saturation);
		m_pDecoder->SetNumThreads(m_OldSettings.NumThreads);
		m_pDecoder->SetEnableDXVA2(m_OldSettings.fEnableDXVA2);

		ITVTestVideoDecoder2 *pDecoder2;
		if (SUCCEEDED(m_pDecoder->QueryInterface(IID_PPV_ARGS(&pDecoder2)))) {
			pDecoder2->SetEnableD3D11(m_OldSettings.fEnableD3D11);
			pDecoder2->Release();
		}

		m_pDecoder->SaveOptions();

		m_pDecoder->Release();
		m_pDecoder = nullptr;
	}

	m_fInitialized = false;

	return S_OK;
}

HRESULT CTVTestVideoDecoderProp::OnActivate()
{
	if (m_pDecoder == nullptr)
		return E_UNEXPECTED;

	m_OldSettings.fEnableDeinterlace = m_pDecoder->GetEnableDeinterlace() != FALSE;
	m_OldSettings.DeinterlaceMethod = m_pDecoder->GetDeinterlaceMethod();
	m_OldSettings.fAdaptProgressive = m_pDecoder->GetAdaptProgressive() != FALSE;
	m_OldSettings.fAdaptTelecine = m_pDecoder->GetAdaptTelecine() != FALSE;
	m_OldSettings.fSetInterlacedFlag = m_pDecoder->GetInterlacedFlag() != FALSE;
	m_OldSettings.Brightness = m_pDecoder->GetBrightness();
	m_OldSettings.Contrast = m_pDecoder->GetContrast();
	m_OldSettings.Hue = m_pDecoder->GetHue();
	m_OldSettings.Saturation = m_pDecoder->GetSaturation();
	m_OldSettings.NumThreads = m_pDecoder->GetNumThreads();
	m_OldSettings.fEnableDXVA2 = m_pDecoder->GetEnableDXVA2() != FALSE;

	ITVTestVideoDecoder2 *pDecoder2;
	if (SUCCEEDED(m_pDecoder->QueryInterface(IID_PPV_ARGS(&pDecoder2)))) {
		m_OldSettings.fEnableD3D11 = pDecoder2->GetEnableD3D11() != FALSE;
		pDecoder2->Release();
	} else {
		m_OldSettings.fEnableD3D11 = false;
	}

	m_NewSettings = m_OldSettings;

	for (int i = IDS_DECODER_FIRST; i <= IDS_DECODER_LAST; i++) {
		if (i == IDS_DECODER_D3D11 && !IsWindows8OrGreater())
			break;
		TCHAR szText[64];
		::LoadString(g_hInst, i, szText, _countof(szText));
		::SendDlgItemMessage(m_Dlg, IDC_PROP_DECODER, CB_ADDSTRING, 0, (LPARAM)szText);
	}
	::SendDlgItemMessage(
		m_Dlg, IDC_PROP_DECODER, CB_SETCURSEL,
		m_OldSettings.fEnableDXVA2 ? DECODER_DXVA2 :
		m_OldSettings.fEnableD3D11 ? DECODER_D3D11 :
		DECODER_SOFTWARE, 0);

	::CheckRadioButton(m_Dlg, IDC_PROP_DEINTERLACE_ENABLE, IDC_PROP_DEINTERLACE_DISABLE,
					   m_OldSettings.fEnableDeinterlace ? IDC_PROP_DEINTERLACE_ENABLE : IDC_PROP_DEINTERLACE_DISABLE);

	for (int i = IDS_DEINTERLACE_FIRST; i <= IDS_DEINTERLACE_LAST; i++) {
		TCHAR szText[64];
		::LoadString(g_hInst, i, szText, _countof(szText));
		::SendDlgItemMessage(m_Dlg, IDC_PROP_DEINTERLACE_METHOD, CB_ADDSTRING, 0, (LPARAM)szText);
	}
	::SendDlgItemMessage(m_Dlg, IDC_PROP_DEINTERLACE_METHOD, CB_SETCURSEL, (WPARAM)m_OldSettings.DeinterlaceMethod, 0);

	::CheckDlgButton(m_Dlg, IDC_PROP_ADAPT_PROGRESSIVE, m_OldSettings.fAdaptProgressive ? BST_CHECKED : BST_UNCHECKED);
	::CheckDlgButton(m_Dlg, IDC_PROP_ADAPT_TELECINE, m_OldSettings.fAdaptTelecine ? BST_CHECKED : BST_UNCHECKED);

	::CheckDlgButton(m_Dlg, IDC_PROP_SET_INTERLACED_FLAG,
					 m_OldSettings.fSetInterlacedFlag ? BST_CHECKED : BST_UNCHECKED);

	EnableDlgItems(m_Dlg, IDC_PROP_DEINTERLACE_METHOD_LABEL, IDC_PROP_DEINTERLACE_METHOD,
				   m_OldSettings.fEnableDeinterlace);
	EnableDlgItems(m_Dlg, IDC_PROP_ADAPT_PROGRESSIVE, IDC_PROP_ADAPT_TELECINE,
				   m_OldSettings.fEnableDeinterlace
				   && m_OldSettings.DeinterlaceMethod != TVTVIDEODEC_DEINTERLACE_WEAVE);
	EnableDlgItems(m_Dlg, IDC_PROP_SET_INTERLACED_FLAG, IDC_PROP_DEINTERLACE_NOTE,
				   !m_OldSettings.fEnableDeinterlace);

	::SendDlgItemMessage(m_Dlg, IDC_PROP_BRIGHTNESS_SLIDER, TBM_SETRANGEMIN,
						 FALSE, TVTVIDEODEC_BRIGHTNESS_MIN);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_BRIGHTNESS_SLIDER, TBM_SETRANGEMAX,
						 FALSE, TVTVIDEODEC_BRIGHTNESS_MAX);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_BRIGHTNESS_SLIDER, TBM_SETPAGESIZE, 0, 10);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_BRIGHTNESS_SLIDER, TBM_SETTICFREQ, 10, 0);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_BRIGHTNESS_SLIDER, TBM_SETPOS,
						 FALSE, m_OldSettings.Brightness);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_BRIGHTNESS_SPIN, UDM_SETRANGE32,
						 TVTVIDEODEC_BRIGHTNESS_MIN, TVTVIDEODEC_BRIGHTNESS_MAX);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_BRIGHTNESS_SPIN, UDM_SETPOS32,
						 0, m_OldSettings.Brightness);
	::SetDlgItemInt(m_Dlg, IDC_PROP_BRIGHTNESS, m_OldSettings.Brightness, TRUE);

	::SendDlgItemMessage(m_Dlg, IDC_PROP_CONTRAST_SLIDER, TBM_SETRANGEMIN,
						 FALSE, TVTVIDEODEC_CONTRAST_MIN);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_CONTRAST_SLIDER, TBM_SETRANGEMAX,
						 FALSE, TVTVIDEODEC_CONTRAST_MAX);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_CONTRAST_SLIDER, TBM_SETPAGESIZE, 0, 10);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_CONTRAST_SLIDER, TBM_SETTICFREQ, 10, 0);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_CONTRAST_SLIDER, TBM_SETPOS,
						 FALSE, m_OldSettings.Contrast);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_CONTRAST_SPIN, UDM_SETRANGE32,
						 TVTVIDEODEC_CONTRAST_MIN, TVTVIDEODEC_CONTRAST_MAX);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_CONTRAST_SPIN, UDM_SETPOS32,
						 0, m_OldSettings.Contrast);
	::SetDlgItemInt(m_Dlg, IDC_PROP_CONTRAST, m_OldSettings.Contrast, TRUE);

	::SendDlgItemMessage(m_Dlg, IDC_PROP_HUE_SLIDER, TBM_SETRANGEMIN,
						 FALSE, TVTVIDEODEC_HUE_MIN);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_HUE_SLIDER, TBM_SETRANGEMAX,
						 FALSE, TVTVIDEODEC_HUE_MAX);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_HUE_SLIDER, TBM_SETPAGESIZE, 0, 10);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_HUE_SLIDER, TBM_SETTICFREQ, 10, 0);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_HUE_SLIDER, TBM_SETPOS,
						 FALSE, m_OldSettings.Hue);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_HUE_SPIN, UDM_SETRANGE32,
						 TVTVIDEODEC_HUE_MIN, TVTVIDEODEC_HUE_MAX);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_HUE_SPIN, UDM_SETPOS32,
						 0, m_OldSettings.Hue);
	::SetDlgItemInt(m_Dlg, IDC_PROP_HUE, m_OldSettings.Hue, TRUE);

	::SendDlgItemMessage(m_Dlg, IDC_PROP_SATURATION_SLIDER, TBM_SETRANGEMIN,
						 FALSE, TVTVIDEODEC_SATURATION_MIN);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_SATURATION_SLIDER, TBM_SETRANGEMAX,
						 FALSE, TVTVIDEODEC_SATURATION_MAX);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_SATURATION_SLIDER, TBM_SETPAGESIZE, 0, 10);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_SATURATION_SLIDER, TBM_SETTICFREQ, 10, 0);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_SATURATION_SLIDER, TBM_SETPOS,
						 FALSE, m_OldSettings.Saturation);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_SATURATION_SPIN, UDM_SETRANGE32,
						 TVTVIDEODEC_SATURATION_MIN, TVTVIDEODEC_SATURATION_MAX);
	::SendDlgItemMessage(m_Dlg, IDC_PROP_SATURATION_SPIN, UDM_SETPOS32,
						 0, m_OldSettings.Saturation);
	::SetDlgItemInt(m_Dlg, IDC_PROP_SATURATION, m_OldSettings.Saturation, TRUE);

	TCHAR szText[64];
	::LoadString(g_hInst, IDS_THREADS_AUTO, szText, _countof(szText));
	::SendDlgItemMessage(m_Dlg, IDC_PROP_NUM_THREADS, CB_ADDSTRING, 0, (LPARAM)szText);
	for (int i = 1; i <= min(TVTVIDEODEC_MAX_THREADS, 16) ; i++) {
		::wsprintf(szText, TEXT("%d"), i);
		::SendDlgItemMessage(m_Dlg, IDC_PROP_NUM_THREADS, CB_ADDSTRING, 0, (LPARAM)szText);
	}
	::SendDlgItemMessage(m_Dlg, IDC_PROP_NUM_THREADS, CB_SETCURSEL, m_OldSettings.NumThreads, 0);

	m_fInitialized = true;

	return S_OK;
}

HRESULT CTVTestVideoDecoderProp::OnApplyChanges()
{
	m_OldSettings = m_NewSettings;
	return S_OK;
}

INT_PTR CTVTestVideoDecoderProp::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_PROP_DECODER:
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				if (m_pDecoder) {
					const int Decoder = (int)::SendDlgItemMessage(hwnd, IDC_PROP_DECODER, CB_GETCURSEL, 0, 0);
					const bool fEnableDXVA2 = Decoder == DECODER_DXVA2;
					const bool fEnableD3D11 = Decoder == DECODER_D3D11;
					if (fEnableDXVA2 != m_NewSettings.fEnableDXVA2 || fEnableD3D11 != m_NewSettings.fEnableD3D11) {
						m_NewSettings.fEnableDXVA2 = fEnableDXVA2;
						m_NewSettings.fEnableD3D11 = fEnableD3D11;
#if 0
						m_pDecoder->SetEnableDXVA2(fEnableDXVA2);
						ITVTestVideoDecoder2 *pDecoder2;
						if (SUCCEEDED(m_pDecoder->QueryInterface(IID_PPV_ARGS(&pDecoder2)))) {
							pDecoder2->SetEnableD3D11(fEnableD3D11);
							pDecoder2->Release();
						}
#endif
						MakeDirty();
					}
				}
			}
			return TRUE;

		case IDC_PROP_DEINTERLACE_ENABLE:
		case IDC_PROP_DEINTERLACE_DISABLE:
			if (m_pDecoder) {
				bool fEnableDeinterlace =
					::IsDlgButtonChecked(hwnd, IDC_PROP_DEINTERLACE_ENABLE) == BST_CHECKED;
				if (fEnableDeinterlace != m_NewSettings.fEnableDeinterlace) {
					m_NewSettings.fEnableDeinterlace = fEnableDeinterlace;
					m_pDecoder->SetEnableDeinterlace(fEnableDeinterlace);
					MakeDirty();
					EnableDlgItems(hwnd, IDC_PROP_DEINTERLACE_METHOD_LABEL, IDC_PROP_DEINTERLACE_METHOD,
								   fEnableDeinterlace);
					EnableDlgItems(m_Dlg, IDC_PROP_ADAPT_PROGRESSIVE, IDC_PROP_ADAPT_TELECINE,
								   fEnableDeinterlace
								   && m_NewSettings.DeinterlaceMethod != TVTVIDEODEC_DEINTERLACE_WEAVE);
					EnableDlgItems(hwnd, IDC_PROP_SET_INTERLACED_FLAG, IDC_PROP_DEINTERLACE_NOTE,
								   !fEnableDeinterlace);
				}
			}
			return TRUE;

		case IDC_PROP_DEINTERLACE_METHOD:
			if (HIWORD(wParam) == CBN_SELCHANGE && m_pDecoder) {
				TVTVIDEODEC_DeinterlaceMethod DeinterlaceMethod =
					(TVTVIDEODEC_DeinterlaceMethod)::SendDlgItemMessage(
						hwnd, IDC_PROP_DEINTERLACE_METHOD, CB_GETCURSEL, 0, 0);
				if (DeinterlaceMethod >= 0
						&& DeinterlaceMethod != m_NewSettings.DeinterlaceMethod) {
					m_NewSettings.DeinterlaceMethod = DeinterlaceMethod;
					m_pDecoder->SetDeinterlaceMethod(DeinterlaceMethod);
					MakeDirty();
					EnableDlgItems(m_Dlg, IDC_PROP_ADAPT_PROGRESSIVE, IDC_PROP_ADAPT_TELECINE,
								   m_NewSettings.fEnableDeinterlace
								   && DeinterlaceMethod != TVTVIDEODEC_DEINTERLACE_WEAVE);
				}
			}
			return TRUE;

		case IDC_PROP_ADAPT_PROGRESSIVE:
			if (m_pDecoder) {
				bool fAdaptProgressive =
					::IsDlgButtonChecked(hwnd, IDC_PROP_ADAPT_PROGRESSIVE) == BST_CHECKED;
				if (fAdaptProgressive != m_NewSettings.fAdaptProgressive) {
					m_NewSettings.fAdaptProgressive = fAdaptProgressive;
					m_pDecoder->SetAdaptProgressive(fAdaptProgressive);
					MakeDirty();
				}
			}
			return TRUE;

		case IDC_PROP_ADAPT_TELECINE:
			if (m_pDecoder) {
				bool fAdaptTelecine =
					::IsDlgButtonChecked(hwnd, IDC_PROP_ADAPT_TELECINE) == BST_CHECKED;
				if (fAdaptTelecine != m_NewSettings.fAdaptTelecine) {
					m_NewSettings.fAdaptTelecine = fAdaptTelecine;
					m_pDecoder->SetAdaptTelecine(fAdaptTelecine);
					MakeDirty();
				}
			}
			return TRUE;

		case IDC_PROP_SET_INTERLACED_FLAG:
			if (m_pDecoder) {
				bool fSetInterlacedFlag =
					::IsDlgButtonChecked(hwnd, IDC_PROP_SET_INTERLACED_FLAG) == BST_CHECKED;
				if (fSetInterlacedFlag != m_NewSettings.fSetInterlacedFlag) {
					m_NewSettings.fSetInterlacedFlag = fSetInterlacedFlag;
					m_pDecoder->SetInterlacedFlag(fSetInterlacedFlag);
					MakeDirty();
				}
			}
			return TRUE;

		case IDC_PROP_BRIGHTNESS:
			if (HIWORD(wParam) == EN_CHANGE && m_pDecoder && m_fInitialized) {
				BOOL fOK;
				int Brightness = ::GetDlgItemInt(hwnd, IDC_PROP_BRIGHTNESS, &fOK, TRUE);
				if (fOK && Brightness != m_NewSettings.Brightness) {
					::SendDlgItemMessage(hwnd, IDC_PROP_BRIGHTNESS_SLIDER, TBM_SETPOS, TRUE, Brightness);
					m_NewSettings.Brightness = Brightness;
					m_pDecoder->SetBrightness(Brightness);
					MakeDirty();
				}
			}
			return TRUE;

		case IDC_PROP_CONTRAST:
			if (HIWORD(wParam) == EN_CHANGE && m_pDecoder && m_fInitialized) {
				BOOL fOK;
				int Contrast = ::GetDlgItemInt(hwnd, IDC_PROP_CONTRAST, &fOK, TRUE);
				if (fOK && Contrast != m_NewSettings.Contrast) {
					::SendDlgItemMessage(hwnd, IDC_PROP_CONTRAST_SLIDER, TBM_SETPOS, TRUE, Contrast);
					m_NewSettings.Contrast = Contrast;
					m_pDecoder->SetContrast(Contrast);
					MakeDirty();
				}
			}
			return TRUE;

		case IDC_PROP_HUE:
			if (HIWORD(wParam) == EN_CHANGE && m_pDecoder && m_fInitialized) {
				BOOL fOK;
				int Hue = ::GetDlgItemInt(hwnd, IDC_PROP_HUE, &fOK, TRUE);
				if (fOK && Hue != m_NewSettings.Hue) {
					::SendDlgItemMessage(hwnd, IDC_PROP_HUE_SLIDER, TBM_SETPOS, TRUE, Hue);
					m_NewSettings.Hue = Hue;
					m_pDecoder->SetHue(Hue);
					MakeDirty();
				}
			}
			return TRUE;

		case IDC_PROP_SATURATION:
			if (HIWORD(wParam) == EN_CHANGE && m_pDecoder && m_fInitialized) {
				BOOL fOK;
				int Saturation = ::GetDlgItemInt(hwnd, IDC_PROP_SATURATION, &fOK, TRUE);
				if (fOK && Saturation != m_NewSettings.Saturation) {
					::SendDlgItemMessage(hwnd, IDC_PROP_SATURATION_SLIDER, TBM_SETPOS, TRUE, Saturation);
					m_NewSettings.Saturation = Saturation;
					m_pDecoder->SetSaturation(Saturation);
					MakeDirty();
				}
			}
			return TRUE;

		case IDC_PROP_RESET_COLOR_ADJUSTMENT:
			if (m_NewSettings.Brightness != 0)
				::SetDlgItemInt(hwnd, IDC_PROP_BRIGHTNESS, 0, TRUE);
			if (m_NewSettings.Contrast != 0)
				::SetDlgItemInt(hwnd, IDC_PROP_CONTRAST, 0, TRUE);
			if (m_NewSettings.Hue != 0)
				::SetDlgItemInt(hwnd, IDC_PROP_HUE, 0, TRUE);
			if (m_NewSettings.Saturation != 0)
				::SetDlgItemInt(hwnd, IDC_PROP_SATURATION, 0, TRUE);
			return TRUE;

		case IDC_PROP_NUM_THREADS:
			if (HIWORD(wParam) == CBN_SELCHANGE && m_pDecoder) {
				int NumThreads = (int)::SendDlgItemMessage(hwnd, IDC_PROP_NUM_THREADS, CB_GETCURSEL, 0, 0);
				if (NumThreads >= 0 && NumThreads != m_NewSettings.NumThreads) {
					m_NewSettings.NumThreads = NumThreads;
					m_pDecoder->SetNumThreads(NumThreads);
					MakeDirty();
				}
			}
			return TRUE;
		}
		break;

	case WM_HSCROLL:
		if ((HWND)lParam == ::GetDlgItem(hwnd, IDC_PROP_BRIGHTNESS_SLIDER)) {
			::SetDlgItemInt(hwnd, IDC_PROP_BRIGHTNESS,
				(int)::SendDlgItemMessage(hwnd, IDC_PROP_BRIGHTNESS_SLIDER, TBM_GETPOS, 0, 0),
				TRUE);
		} else if ((HWND)lParam == ::GetDlgItem(hwnd, IDC_PROP_CONTRAST_SLIDER)) {
			::SetDlgItemInt(hwnd, IDC_PROP_CONTRAST,
				(int)::SendDlgItemMessage(hwnd, IDC_PROP_CONTRAST_SLIDER, TBM_GETPOS, 0, 0),
				TRUE);
		} else if ((HWND)lParam == ::GetDlgItem(hwnd, IDC_PROP_HUE_SLIDER)) {
			::SetDlgItemInt(hwnd, IDC_PROP_HUE,
				(int)::SendDlgItemMessage(hwnd, IDC_PROP_HUE_SLIDER, TBM_GETPOS, 0, 0),
				TRUE);
		} else if ((HWND)lParam == ::GetDlgItem(hwnd, IDC_PROP_SATURATION_SLIDER)) {
			::SetDlgItemInt(hwnd, IDC_PROP_SATURATION,
				(int)::SendDlgItemMessage(hwnd, IDC_PROP_SATURATION_SLIDER, TBM_GETPOS, 0, 0),
				TRUE);
		}
		return TRUE;
	}

	return __super::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

void CTVTestVideoDecoderProp::MakeDirty()
{
	m_bDirty = TRUE;
	if (m_pPageSite) {
		m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
	}
}
