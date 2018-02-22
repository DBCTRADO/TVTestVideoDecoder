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

#pragma once


#include "ITVTestVideoDecoder.h"
#include "Deinterlace.h"
#include "Deinterlace_Yadif.h"


class CDeinterlacerSet
{
public:
	CDeinterlacerSet();
	void InitDeinterlacers();

protected:
	CDeinterlacer *m_Deinterlacers[TVTVIDEODEC_DEINTERLACE_LAST + 1];
	CDeinterlacer_Weave m_Deinterlacer_Weave;
	CDeinterlacer_Blend m_Deinterlacer_Blend;
	CDeinterlacer_Bob m_Deinterlacer_Bob;
	CDeinterlacer_ELA m_Deinterlacer_ELA;
	CDeinterlacer_Yadif m_Deinterlacer_Yadif;
	CDeinterlacer_Yadif m_Deinterlacer_YadifBob;
};
