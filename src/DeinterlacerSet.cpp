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
#include "DeinterlacerSet.h"


CDeinterlacerSet::CDeinterlacerSet()
	: m_Deinterlacer_Yadif(false)
	, m_Deinterlacer_YadifBob(true)
{
	m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_WEAVE    ] = &m_Deinterlacer_Weave;
	m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_BLEND    ] = &m_Deinterlacer_Blend;
	m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_BOB      ] = &m_Deinterlacer_Bob;
	m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_ELA      ] = &m_Deinterlacer_ELA;
	m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_YADIF    ] = &m_Deinterlacer_Yadif;
	m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_YADIF_BOB] = &m_Deinterlacer_YadifBob;
}

void CDeinterlacerSet::InitDeinterlacers()
{
	for (int i = 0; i < _countof(m_Deinterlacers); i++) {
		m_Deinterlacers[i]->Initialize();
	}
}
