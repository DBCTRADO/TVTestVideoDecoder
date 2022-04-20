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


#include <objbase.h>


template<typename T> inline void SafeRelease(T *&p)
{
	if (p != nullptr) {
		p->Release();
		p = nullptr;
	}
}

template <typename T1, typename T2> bool AreCOMObjectsEqual(T1 *p1, T2 *p2)
{
	bool Result = false;

	if (p1 == p2) {
		Result = true;
	} else if ((p1 == nullptr) || (p2 == nullptr)) {
		Result = false;
	} else {
		IUnknown *pUnk1 = nullptr;
		IUnknown *pUnk2 = nullptr;

		if (SUCCEEDED(p1->QueryInterface(IDD_PPV_ARGS_IUNKNOWN(&pUnk1)))) {
			if (SUCCEEDED(p2->QueryInterface(IDD_PPV_ARGS_IUNKNOWN(&pUnk2)))) {
				Result = (pUnk1 == pUnk2);
				pUnk2->Release();
			}
			pUnk1->Release();
		}
	}

	return Result;
}

template <typename T> void CopyCOMPointer(T *&dst, T *src)
{
	if (src != nullptr)
		src->AddRef();
	if (dst != nullptr)
		dst->Release();
	dst = src;
}

template <typename T> void MoveCOMPointer(T *&dst, T *&src)
{
	if ((dst != nullptr) && !AreCOMObjectsEqual(dst, src))
		dst->Release();
	dst = src;
	src = nullptr;
}

template<typename T> class COMPointer
{
public:
	COMPointer() = default;

	COMPointer(const COMPointer &Src)
	{
		CopyCOMPointer(m_pObject, Src.m_pObject);
	}

	COMPointer(COMPointer &&Src)
	{
		MoveCOMPointer(m_pObject, Src.m_pObject);
	}

	COMPointer(T *pObject)
	{
		CopyCOMPointer(m_pObject, pObject);
	}

	~COMPointer()
	{
		SafeRelease(m_pObject);
	}

	COMPointer & operator = (const COMPointer &rhs)
	{
		CopyCOMPointer(m_pObject, rhs.m_pObject);
		return *this;
	}

	COMPointer & operator = (COMPointer &&rhs)
	{
		MoveCOMPointer(m_pObject, rhs.m_pObject);
		return *this;
	}

	COMPointer & operator = (T *rhs)
	{
		CopyCOMPointer(m_pObject, rhs);
		return *this;
	}

	bool operator == (const COMPointer &rhs) const noexcept
	{
		return m_pObject == rhs.m_pObject;
	}

	bool operator != (const COMPointer &rhs) const noexcept
	{
		return m_pObject != rhs.m_pObject;
	}

	T * operator -> () const noexcept
	{
		return m_pObject;
	}

	T & operator * () const noexcept
	{
		return *m_pObject;
	}

	explicit operator bool () const noexcept
	{
		return m_pObject != nullptr;
	}

	void Release()
	{
		SafeRelease(m_pObject);
	}

	T * Get() const noexcept
	{
		return m_pObject;
	}

	T ** GetPP() noexcept
	{
		Release();
		return &m_pObject;
	}

	void Attach(T *p)
	{
		if (m_pObject != nullptr)
			m_pObject->Release();
		m_pObject = p;
	}

	T * Detach()
	{
		T *p = m_pObject;
		m_pObject = nullptr;
		return p;
	}

	bool IsEqualObject(T *p) const
	{
		return AreCOMObjectsEqual(m_pObject, p);
	}

	bool IsEqualObject(const COMPointer &p) const
	{
		return AreCOMObjectsEqual(m_pObject, p.m_pObject);
	}

	template<typename TQuery> HRESULT QueryInterface(COMPointer<TQuery> *p) const
	{
		if (p == nullptr)
			return E_POINTER;
		p->Release();
		if (m_pObject == nullptr)
			return E_NOINTERFACE;
		return m_pObject->QueryInterface(__uuidof(TQuery), reinterpret_cast<void **>(p->GetPP()));
	}

	template<typename TQuery> HRESULT QueryInterface(TQuery **pp) const
	{
		if (pp == nullptr)
			return E_POINTER;
		*pp = nullptr;
		if (m_pObject == nullptr)
			return E_NOINTERFACE;
		return m_pObject->QueryInterface(__uuidof(TQuery), reinterpret_cast<void **>(pp));
	}

private:
	T *m_pObject = nullptr;
};
