/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  otidalroute Plugin
 * Author:   David Register, Mike Rossiter
 *
 ***************************************************************************
 *   Copyright (C) 2010 by David S. Register   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.             *
 ***************************************************************************
 */

#ifndef _otidalroutePI_H_
#define _otidalroutePI_H_

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#include <wx/glcanvas.h>
#endif  // precompiled headers

#include "ocpn_plugin.h"
#include "otidalrouteOverlayFactory.h"
#include "otidalrouteUIDialog.h"
#include "json/json.h"
#include <wx/datetime.h>
#include "config.h"

extern wxString myVColour[5];

class piDC;

// Define minimum and maximum versions of the grib plugin supported
#define GRIB_MAX_MAJOR 4
#define GRIB_MAX_MINOR 1
#define GRIB_MIN_MAJOR 4
#define GRIB_MIN_MINOR 1

#define ABOUT_AUTHOR_URL "http://mikerossiter.co.uk"

//----------------------------------------------------------------------------------------------------------
//    The PlugIn Class Definition
//----------------------------------------------------------------------------------------------------------

#define otidalroute_TOOL_POSITION \
  -1  // Request default positioning of toolbar tool

static inline bool GribCurrent(GribRecordSet *grib, double lat, double lon,
                               double &C, double &VC) {
  if (!grib) return false;

  if (!GribRecord::getInterpolatedValues(
          VC, C, grib->m_GribRecordPtrArray[Idx_SEACURRENT_VX],
          grib->m_GribRecordPtrArray[Idx_SEACURRENT_VY], lon, lat))
    return false;

  VC *= 3.6 / 1.852;  // knots

  C += 180;
  if (C > 360) C -= 360;
  return true;
}

class otidalroute_pi : public opencpn_plugin_120 {
public:
  otidalroute_pi(void *ppimgr);
  ~otidalroute_pi(void);

  //    The required PlugIn Methods
  int Init(void);
  bool DeInit(void);

  int GetAPIVersionMajor();
  int GetAPIVersionMinor();
  int GetPlugInVersionMajor();
  int GetPlugInVersionMinor();
  wxBitmap *GetPlugInBitmap();
  wxString GetCommonName();
  wxString GetShortDescription();
  wxString GetLongDescription();

  bool GribWind(GribRecordSet *grib, double lat, double lon, double &WG,
                double &VWG);

  // bool GribCurrent(GribRecordSet *grib, double lat, double lon,
  //  double &C, double &VC);

  //    The override PlugIn Methods
  bool RenderOverlay(wxDC &dc, PlugIn_ViewPort *vp);
  bool RenderGLOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp);
  void SetCursorLatLon(double lat, double lon);
  void SendTimelineMessage(wxDateTime time);
  void SetDefaults(void);
  int GetToolbarToolCount(void);
  void ShowPreferencesDialog(wxWindow *parent);
  void OnToolbarToolCallback(int id);

  double GetCursorLon(void) { return m_cursor_lon; }
  double GetCursorLat(void) { return m_cursor_lat; }

  // Other public methods
  void SetotidalrouteDialogX(int x) { m_otidalroute_dialog_x = x; };
  void SetotidalrouteDialogY(int x) { m_otidalroute_dialog_y = x; }
  void SetotidalrouteDialogSizeX(int x) { m_otidalroute_dialog_sx = x; }
  void SetotidalrouteDialogSizeY(int x) { m_otidalroute_dialog_sy = x; }
  void SetColorScheme(PI_ColorScheme cs);

  void OnotidalrouteDialogClose();
  void SetPluginMessage(wxString &message_id, wxString &message_body);

  bool GetCopyRate() { return m_bCopyUseRate; }
  bool GetCopyDirection() { return m_bCopyUseDirection; }
  bool GetCopyColour() { return m_botidalrouteUseHiDef; }

  wxString GetFolderSelected() { return m_CopyFolderSelected; }
  int GetIntervalSelected() { return m_CopyIntervalSelected; }

  otidalrouteOverlayFactory *GetotidalrouteOverlayFactory() {
    return m_potidalrouteOverlayFactory;
  }

  double m_boat_lat, m_boat_lon;

  bool m_bGribValid;
  double m_grib_lat, m_grib_lon;
  double m_tr_spd;
  double m_tr_dir;
  otidalrouteOverlayFactory *m_potidalrouteOverlayFactory;

  wxString StandardPath();
  otidalrouteUIDialog *m_potidalrouteDialog;

private:
  double m_cursor_lat, m_cursor_lon;
  bool LoadConfig(void);
  bool SaveConfig(void);

  wxFileConfig *m_pconfig;
  wxWindow *m_parent_window;

  int m_display_width, m_display_height;
  int m_leftclick_tool_id;

  int m_otidalroute_dialog_x, m_otidalroute_dialog_y;
  int m_otidalroute_dialog_sx, m_otidalroute_dialog_sy;

  // preference data
  bool m_botidalrouteUseHiDef;
  bool m_botidalrouteUseGradualColors;
  bool m_bCopyUseRate;
  bool m_bCopyUseDirection;

  wxString m_CopyFolderSelected;
  int m_CopyIntervalSelected;

  int m_bTimeZone;

  int m_bStartOptions;
  wxString m_RequestConfig;
  wxString *pTC_Dir;

  bool m_botidalrouteShowIcon;

  int m_height;

  bool m_bShowotidalroute;

  int m_position_menu_id;
  int m_table_menu_id;
  wxBitmap m_panelBitmap;
};

//----------------------------------------------------------------------------------------
// Prefrence dialog definition
//----------------------------------------------------------------------------------------

class otidalroutePreferencesDialog : public otidalroutePreferencesDialogBase {
public:
  otidalroutePreferencesDialog(wxWindow *pparent)
      : otidalroutePreferencesDialogBase(pparent) {}
  ~otidalroutePreferencesDialog() {}

private:
};
#endif
