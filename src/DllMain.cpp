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
#include "TVTestVideoDecoder.h"
#include "TVTestVideoDecoderProp.h"
#include "TVTestVideoDecoderStat.h"
#include "Util.h"
#include "MediaTypes.h"
#include "Version.h"
#include "resource.h"


static const AMOVIESETUP_MEDIATYPE sudPinTypesIn[] = {
	{&MEDIATYPE_MPEG2_PACK, &MEDIASUBTYPE_MPEG2_VIDEO},
	{&MEDIATYPE_MPEG2_PES,  &MEDIASUBTYPE_MPEG2_VIDEO},
	{&MEDIATYPE_Video,      &MEDIASUBTYPE_MPEG2_VIDEO},
	{&MEDIATYPE_Video,      &MEDIASUBTYPE_MPG2},
};

static const AMOVIESETUP_MEDIATYPE sudPinTypesOut[] = {
	{&MEDIATYPE_Video, &MEDIASUBTYPE_NV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_I420},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_IYUV},
};

static const AMOVIESETUP_PIN sudpPins[] = {
	{L"Input",  FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, nullptr, _countof(sudPinTypesIn),  sudPinTypesIn},
	{L"Output", FALSE, TRUE,  FALSE, FALSE, &CLSID_NULL, nullptr, _countof(sudPinTypesOut), sudPinTypesOut},
};

static const AMOVIESETUP_FILTER sudFilter[] = {
	{&__uuidof(ITVTestVideoDecoder), TVTVIDEODEC_FILTER_NAME, MERIT_DO_NOT_USE + 1, _countof(sudpPins), sudpPins},
};

CFactoryTemplate g_Templates[] = {
	{
		sudFilter[0].strName,
		sudFilter[0].clsID,
		CTVTestVideoDecoder::CreateInstance,
		nullptr,
		&sudFilter[0]
	},
	{
		TVTVIDEODEC_FILTER_NAME L" Properties",
		&__uuidof(CTVTestVideoDecoderProp),
		CTVTestVideoDecoderProp::CreateInstance,
		nullptr,
		nullptr
	},
	{
		TVTVIDEODEC_FILTER_NAME L" Statistics",
		&__uuidof(CTVTestVideoDecoderStat),
		CTVTestVideoDecoderStat::CreateInstance,
		nullptr,
		nullptr
	},
};

int g_cTemplates = _countof(g_Templates);


STDAPI DllRegisterServer()
{
	return AMovieDllRegisterServer2(TRUE);
}

STDAPI DllUnregisterServer()
{
	return AMovieDllRegisterServer2(FALSE);
}


extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
#ifdef _DEBUG
	if (dwReason == DLL_PROCESS_ATTACH) {
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

		DbgSetModuleLevel(LOG_TRACE, DWORD_MAX);
		DbgSetModuleLevel(LOG_ERROR, DWORD_MAX);
	}
#endif

	return DllEntryPoint(hModule, dwReason, lpReserved);
}


TVTVIDEODEC_EXPORT void CALLBACK UninstallW(
	HWND hwnd, HINSTANCE hinst, LPWSTR lpszCmdLine, int nCmdShow)
{
	bool fSilent = false;

	if (lpszCmdLine[0]) {
		int ArgCount;
		LPWSTR *pArgs = ::CommandLineToArgvW(lpszCmdLine, &ArgCount);

		for (int i = 0; i < ArgCount; i++) {
			if (pArgs[i][0] == L'/' || pArgs[i][0] == L'-') {
				if (::lstrcmpiW(pArgs[i] + 1, L"silent") == 0) {
					fSilent = true;
				}
			}
		}

		::LocalFree(pArgs);
	}

	LONG Result = ::RegDeleteKey(HKEY_CURRENT_USER, REGISTRY_KEY_NAME);

	if (Result == ERROR_SUCCESS || Result == ERROR_FILE_NOT_FOUND) {
		HKEY hKey;

		if (::RegOpenKeyEx(HKEY_CURRENT_USER, REGISTRY_PARENT_KEY_NAME,
						   0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
			DWORD SubKeys, Values;
			bool bEmpty =
				::RegQueryInfoKey(
					hKey, nullptr, nullptr, nullptr, &SubKeys, nullptr,
					nullptr, &Values, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS
				&& SubKeys == 0 && Values == 0;
			::RegCloseKey(hKey);
			if (bEmpty) {
				::RegDeleteKey(HKEY_CURRENT_USER, REGISTRY_PARENT_KEY_NAME);
			}
		}
	}

	if (!fSilent) {
		WCHAR szText[256];

		if (Result == ERROR_SUCCESS || Result == ERROR_FILE_NOT_FOUND) {
			::LoadStringW(g_hInst, IDS_UNINSTALL_SUCCEEDED, szText, _countof(szText));
			::MessageBoxW(nullptr, szText, TVTVIDEODEC_FILTER_NAME, MB_OK);
		} else {
			::LoadStringW(g_hInst, IDS_UNINSTALL_FAILED, szText, _countof(szText));
			::MessageBoxW(nullptr, szText, TVTVIDEODEC_FILTER_NAME, MB_OK | MB_ICONEXCLAMATION);
		}
	}
}


TVTVIDEODEC_EXPORT BOOL WINAPI TVTestVideoDecoder_GetInfo(TVTestVideoDecoderInfo *pInfo)
{
	if (!pInfo || pInfo->HostVersion != TVTVIDEODEC_HOST_VERSION)
		return FALSE;

	pInfo->ModuleVersion = TVTVIDEODEC_VERSION_(
		TVTVIDEODEC_VERSION_MAJOR, TVTVIDEODEC_VERSION_MINOR, TVTVIDEODEC_VERSION_REV);
	pInfo->InterfaceVersion = TVTVIDEODEC_INTERFACE_VERSION;

	return TRUE;
}

TVTVIDEODEC_EXPORT HRESULT WINAPI TVTestVideoDecoder_CreateInstance(ITVTestVideoDecoder **ppDecoder)
{
	CheckPointer(ppDecoder, E_POINTER);

	HRESULT hr = S_OK;

	*ppDecoder = DNew_nothrow CTVTestVideoDecoder(nullptr, &hr, true);

	if (!*ppDecoder)
		return E_OUTOFMEMORY;

	if (FAILED(hr)) {
		SafeDelete(*ppDecoder);
		return hr;
	}

	(*ppDecoder)->AddRef();

	return S_OK;
}
