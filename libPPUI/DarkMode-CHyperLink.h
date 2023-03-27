#pragma once
#include <atlctrlx.h>
#include "DarkMode.h"

namespace DarkMode {
	template<typename impl_t> class CHyperLinkImpl : public ::CHyperLinkImpl<impl_t> {
	public:
		BEGIN_MSG_MAP_EX(CDarkHyperLinkImpl)
			MESSAGE_HANDLER_EX(DarkMode::msgSetDarkMode(), OnSetDarkMode)
			CHAIN_MSG_MAP(::CHyperLinkImpl<impl_t>)
		END_MSG_MAP()
	private:
		LRESULT OnSetDarkMode(UINT, WPARAM wp, LPARAM) {
			const bool bDark = (wp != 0);
			if ( m_clrLinkBackup == CLR_INVALID ) m_clrLinkBackup = this->m_clrLink;

			if (bDark != m_isDark) {
				m_isDark = bDark;
				m_clrLink = bDark ? DarkMode::GetSysColor(COLOR_HOTLIGHT) : m_clrLinkBackup; 
				Invalidate();
			}

			return 1;
		}
		COLORREF m_clrLinkBackup = CLR_INVALID;
		bool m_isDark = false;
	};


	class CHyperLink : public CHyperLinkImpl<CHyperLink> 	{
	public:
		DECLARE_WND_CLASS(_T("WTL_DarkHyperLink"))
	};

}
