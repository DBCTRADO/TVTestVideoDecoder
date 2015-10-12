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

#define IDD_PROP							100
#define IDD_STAT							101

#define IDC_PROP_DEINTERLACE_ENABLE			1000
#define IDC_PROP_DEINTERLACE_DISABLE		1001
#define IDC_PROP_DEINTERLACE_METHOD_LABEL	1002
#define IDC_PROP_DEINTERLACE_METHOD			1003
#define IDC_PROP_ADAPT_PROGRESSIVE			1004
#define IDC_PROP_ADAPT_TELECINE				1005
#define IDC_PROP_SET_INTERLACED_FLAG		1006
#define IDC_PROP_DEINTERLACE_NOTE			1007
#define IDC_PROP_BRIGHTNESS_SLIDER			1008
#define IDC_PROP_BRIGHTNESS					1009
#define IDC_PROP_BRIGHTNESS_SPIN			1010
#define IDC_PROP_CONTRAST_SLIDER			1011
#define IDC_PROP_CONTRAST					1012
#define IDC_PROP_CONTRAST_SPIN				1013
#define IDC_PROP_HUE_SLIDER					1014
#define IDC_PROP_HUE						1015
#define IDC_PROP_HUE_SPIN					1016
#define IDC_PROP_SATURATION_SLIDER			1017
#define IDC_PROP_SATURATION					1018
#define IDC_PROP_SATURATION_SPIN			1019
#define IDC_PROP_RESET_COLOR_ADJUSTMENT		1020
#define IDC_PROP_NUM_THREADS				1021

#define IDC_STAT_OUT_SIZE					1000
#define IDC_STAT_I_FRAME_COUNT				1001
#define IDC_STAT_P_FRAME_COUNT				1002
#define IDC_STAT_B_FRAME_COUNT				1003
#define IDC_STAT_SKIPPED_FRAME_COUNT		1004
#define IDC_STAT_PLAYBACK_RATE				1005
#define IDC_STAT_BASE_FPS					1006
#define IDC_STAT_VERSION					1007

#define IDS_PROP_TITLE						2000
#define IDS_STAT_TITLE						2001
#define IDS_DEINTERLACE_FIRST				2010
#define IDS_DEINTERLACE_WEAVE				(IDS_DEINTERLACE_FIRST + 0)
#define IDS_DEINTERLACE_BLEND				(IDS_DEINTERLACE_FIRST + 1)
#define IDS_DEINTERLACE_BOB					(IDS_DEINTERLACE_FIRST + 2)
#define IDS_DEINTERLACE_ELA					(IDS_DEINTERLACE_FIRST + 3)
#define IDS_DEINTERLACE_YADIF				(IDS_DEINTERLACE_FIRST + 4)
#define IDS_DEINTERLACE_YADIF_BOB			(IDS_DEINTERLACE_FIRST + 5)
#define IDS_DEINTERLACE_LAST				(IDS_DEINTERLACE_FIRST + 5)
#define IDS_THREADS_AUTO					2020
#define IDS_UNINSTALL_SUCCEEDED				2100
#define IDS_UNINSTALL_FAILED				2101
