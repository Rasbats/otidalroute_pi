/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  otidalroute Object
 * Author:   Mike Rossiter
 *
 ***************************************************************************
 *   Copyright (C) 2016 by Mike Rossiter  *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 *
 */
#include <wx/intl.h>
#include "wx/wx.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/debug.h>
#include <wx/graphics.h>
#include <wx/stdpaths.h>

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "otidalroute_pi.h"
#include "icons.h"
#include <wx/arrimpl.cpp>

#ifdef __WXMSW__
#include <windows.h>
#endif
#include <memory.h>

#include <wx/colordlg.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include "AboutDialog.h"

class GribRecordSet;
class TidalRoute;
class ConfigurationDialog;
class RouteProp;
class AboutDialog;

using namespace std;

#define FAIL(X)  \
  do {           \
    error = X;   \
    goto failed; \
  } while (0)

// date/time in the desired time zone format
static wxString TToString(const wxDateTime date_time, const int time_zone) {
  wxDateTime t(date_time);
  t.MakeFromTimezone(wxDateTime::UTC);
  if (t.IsDST()) t.Subtract(wxTimeSpan(1, 0, 0, 0));
  switch (time_zone) {
    case 0:
      return t.Format(" %a %d-%b-%Y  %H:%M LOC", wxDateTime::Local);
    case 1:
    default:
      return t.Format(" %a %d-%b-%Y %H:%M  UTC", wxDateTime::UTC);
  }
}

static double deg2rad(double degrees) { return M_PI * degrees / 180.0; }

static double rad2deg(double radians) { return 180.0 * radians / M_PI; }

static void CTSWithCurrent(double BG, double& VBG, double C, double VC,
                           double& BC, double VBC) {
  if (VC == 0) {  // short-cut if no current
    BC = BG, VBG = VBC;
    return;
  }

  // Thanks to Geoff Sargent at "tidalstreams.net"

  double B5 = VC / VBC;
  double C1 = deg2rad(BG);
  double C2 = deg2rad(C);

  double C6 = asin(B5 * sin(C1 - C2));
  double B6 = rad2deg(C6);
  if ((BG + B6) > 360) {
    BC = BG + B6 - 360;
  } else {
    BC = BG + B6;
  }
  VBG = (VBC * cos(C6)) + (VC * cos(C1 - C2));
}

static void CMGWithCurrent(double& BG, double& VBG, double C, double VC,
                           double BC, double VBC) {
  if (VC == 0) {  // short-cut if no current
    BG = BC, VBG = VBC;
    return;
  }

  // Thanks to Geoff Sargent at "tidalstreams.net"
  // BUT this function has not been tested !!!

  double B5 = VC / VBC;
  double C1 = deg2rad(BC);
  double C2 = deg2rad(C);

  double B3 = VC;
  double B4 = VBC;

  double C5 = sqr(B3) + sqr(B4) + 2 * B3 * B4 * cos(C1 - C2);
  double D5 = C5;
  double E5 = sqrt(C5);
  double E6 = B3 * sin(C2 - C1) / E5;
  double E7 = asin(E6);
  double E8 = rad2deg(E7);
  if ((BC + E8) > 360) {
    BG = BC + E8 - 360;
  } else {
    BG = BC + E8;
  }
  VBG = E5;
}

#if !wxCHECK_VERSION(2, 9, 4) /* to work with wx 2.8 */
#define SetBitmap SetBitmapLabel
#endif

otidalrouteUIDialog::otidalrouteUIDialog(wxWindow* parent, otidalroute_pi* ppi)
    : otidalrouteUIDialogBase(parent),
      m_ConfigurationDialog(this, wxID_ANY, _("Tidal Routes"),
                            wxDefaultPosition, wxSize(-1, -1),
                            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  pParent = parent;
  pPlugIn = ppi;

  wxFileConfig* pConf = GetOCPNConfigObject();

  if (pConf) {
    pConf->SetPath("/Settings/otidalroute" );

    pConf->Read("otidalrouteUseRate" , &m_bUseRate);
    pConf->Read("otidalrouteUseDirection" , &m_bUseDirection);
    pConf->Read("otidalrouteUseFillColour" , &m_bUseFillColour);

    pConf->Read("VColour0", &myVColour[0], myVColour[0]);
    pConf->Read("VColour1", &myVColour[1], myVColour[1]);
    pConf->Read("VColour2", &myVColour[2], myVColour[2]);
    pConf->Read("VColour3", &myVColour[3], myVColour[3]);
    pConf->Read("VColour4", &myVColour[4], myVColour[4]);

    myUseColour[0] = myVColour[0];
    myUseColour[1] = myVColour[1];
    myUseColour[2] = myVColour[2];
    myUseColour[3] = myVColour[3];
    myUseColour[4] = myVColour[4];
  }

  m_default_configuration_path =
      ppi->StandardPath() + "otidalroute_config.xml";

  if (!OpenXML(m_default_configuration_path, false)) {
    // create directory for plugin files if it doesn't already exist
    wxFileName fn(m_default_configuration_path);
    wxFileName fn2 = fn.GetPath();
    if (!fn.DirExists()) {
      fn2.Mkdir();
      fn.Mkdir();
    }
  }

  m_ConfigurationDialog.pPlugIn = ppi;

  this->Connect(wxEVT_MOVE, wxMoveEventHandler(otidalrouteUIDialog::OnMove));

  m_tSpeed->SetValue("5");
  m_dtNow = wxDateTime::Now();

  wxString initStartDate = m_dtNow.Format("%Y-%m-%d  %H:%M");
  m_textCtrl1->SetValue(initStartDate);

  b_showTidalArrow = false;

  DimeWindow(this);

  Fit();
  SetMinSize(GetBestSize());
}

otidalrouteUIDialog::~otidalrouteUIDialog() {
  wxFileConfig* pConf = GetOCPNConfigObject();
  ;

  if (pConf) {
    pConf->SetPath("/Settings/otidalroute");

    pConf->Write("otidalrouteUseRate" , m_bUseRate);
    pConf->Write("otidalrouteUseDirection" , m_bUseDirection);
    pConf->Write("otidalrouteUseFillColour" , m_bUseFillColour);

    pConf->Write("VColour0", myVColour[0]);
    pConf->Write("VColour1", myVColour[1]);
    pConf->Write("VColour2", myVColour[2]);
    pConf->Write("VColour3", myVColour[3]);
    pConf->Write("VColour4", myVColour[4]);
  }
  SaveXML(m_default_configuration_path);
}

void otidalrouteUIDialog::SetCursorLatLon(double lat, double lon) {
  m_cursor_lon = lon;
  m_cursor_lat = lat;
}

void otidalrouteUIDialog::SetViewPort(PlugIn_ViewPort* vp) {
  if (m_vp == vp) return;

  m_vp = new PlugIn_ViewPort(*vp);
}

void otidalrouteUIDialog::OnClose(wxCloseEvent& event) {
  pPlugIn->OnotidalrouteDialogClose();
}

void otidalrouteUIDialog::OnShowTables(wxCommandEvent& event) {
  b_showTidalArrow = false;
  GetParent()->Refresh();
  m_ConfigurationDialog.Show();
}

void otidalrouteUIDialog::OnDeleteAllRoutes(wxCommandEvent& event) {
  if (m_TidalRoutes.empty()) {
    wxMessageBox(_("No routes have been calculated"));
    return;
  }
  wxMessageDialog mdlg(this, _("Delete all routes?\n"), _("Delete All Routes"),
                       wxYES | wxNO | wxICON_WARNING);
  if (mdlg.ShowModal() == wxID_YES) {
    m_TidalRoutes.clear();
    m_ConfigurationDialog.m_lRoutes->Clear();
  }

  GetParent()->Refresh();
}
void otidalrouteUIDialog::OnMove(wxMoveEvent& event) {
  //    Record the dialog position
  wxPoint p = GetPosition();
  pPlugIn->SetotidalrouteDialogX(p.x);
  pPlugIn->SetotidalrouteDialogY(p.y);

  event.Skip();
}

void otidalrouteUIDialog::OnSize(wxSizeEvent& event) {
  //    Record the dialog size
  wxSize p = event.GetSize();
  pPlugIn->SetotidalrouteDialogSizeX(p.x);
  pPlugIn->SetotidalrouteDialogSizeY(p.y);

  event.Skip();
}

void otidalrouteUIDialog::OpenFile(bool newestFile) {
  m_bUseRate = pPlugIn->GetCopyRate();
  m_bUseDirection = pPlugIn->GetCopyDirection();
  m_bUseFillColour = pPlugIn->GetCopyColour();

  m_FolderSelected = pPlugIn->GetFolderSelected();
  m_IntervalSelected = pPlugIn->GetIntervalSelected();
}

wxString otidalrouteUIDialog::MakeDateTimeLabel(wxDateTime myDateTime) {
  wxDateTime dt = myDateTime;

  wxString s2 = dt.Format( "%Y-%m-%d");
  wxString s = dt.Format("%H:%M");
  wxString dateLabel = s2 + " " + s;

  m_textCtrl1->SetValue(dateLabel);

  return dateLabel;
}

void otidalrouteUIDialog::OnInformation(wxCommandEvent& event) {
  wxFileName fn;
  wxString tmp_path;

  tmp_path = GetPluginDataDir("otidalroute_pi");
  fn.SetPath(tmp_path);
  fn.AppendDir("data");
  fn.SetFullName("TidalRoutingInformation.html");

  wxString infolocation = fn.GetFullPath();
  wxLaunchDefaultBrowser("file:///" + infolocation);
}

void otidalrouteUIDialog::OnAbout(wxCommandEvent& event) {
  AboutDialog dlg(GetParent());
  dlg.ShowModal();
}

void otidalrouteUIDialog::OnSummary(wxCommandEvent& event) {
  TableRoutes* tableroutes = new TableRoutes(
      this, 7000, " Route Summary", wxPoint(200, 200), wxSize(650, 200),
      wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

  wxString RouteName;
  wxString From;
  wxString Towards;
  wxString StartTime;
  wxString EndTime;
  wxString Duration;
  wxString Distance;
  wxString Type;

  if (m_TidalRoutes.empty()) {
    wxMessageBox(_("No routes found. Please make a route"));
    return;
  }

  int in = 0;

  for (std::list<TidalRoute>::iterator it = m_TidalRoutes.begin();
       it != m_TidalRoutes.end(); it++) {
    RouteName = (*it).Name;
    From = (*it).Start;
    Towards = (*it).End;
    StartTime = (*it).StartTime;
    EndTime = (*it).EndTime;
    Duration = (*it).Time;
    Distance = (*it).Distance;
    Type = (*it).Type;

    tableroutes->m_wpList->InsertItem(in, "", -1);
    tableroutes->m_wpList->SetItem(in, 0, RouteName);
    tableroutes->m_wpList->SetItem(in, 1, From);
    tableroutes->m_wpList->SetItem(in, 2, Towards);
    tableroutes->m_wpList->SetItem(in, 3, StartTime);
    tableroutes->m_wpList->SetItem(in, 4, EndTime);
    tableroutes->m_wpList->SetItem(in, 5, Duration);
    tableroutes->m_wpList->SetItem(in, 6, Distance);
    tableroutes->m_wpList->SetItem(in, 7, Type);

    in++;
  }

  tableroutes->Show();

  GetParent()->Refresh();
}

void otidalrouteUIDialog::OnShowRouteTable() {
  wxString name;

  for (std::list<TidalRoute>::iterator it = m_TidalRoutes.begin();
       it != m_TidalRoutes.end(); it++) {
    if (!m_TidalRoutes.empty()) {
      name = (*it).Name;
      break;
    } else {
      wxMessageBox(_("Please select or generate a route"));
      return;
    }
  }

  RouteProp* routetable =
      new RouteProp(this, 7000, _("Tidal Routes"), wxPoint(200, 200),
                    wxSize(650, 800), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

  int in = 0;

  wxString lat;
  wxString lon;
  wxString etd;
  wxString cts;
  wxString smg;
  wxString dis;
  wxString brg;
  wxString set;
  wxString rat;

  routetable->m_PlanSpeedCtl->SetValue(
      pPlugIn->m_potidalrouteDialog->m_tSpeed->GetValue());

  for (std::list<TidalRoute>::iterator it = m_TidalRoutes.begin();
       it != m_TidalRoutes.end(); it++) {
    name = (*it).Name;
    if (m_tRouteName->GetValue() == name) {
      routetable->m_RouteNameCtl->SetValue(name);
      routetable->m_RouteStartCtl->SetValue((*it).Start);
      routetable->m_RouteDestCtl->SetValue((*it).End);

      routetable->m_TotalDistCtl->SetValue((*it).Distance);
      routetable->m_TimeEnrouteCtl->SetValue((*it).Time);
      routetable->m_StartTimeCtl->SetValue((*it).StartTime);
      routetable->m_TypeRouteCtl->SetValue((*it).Type);

      for (std::list<Position>::iterator itp = (*it).m_positionslist.begin();
           itp != (*it).m_positionslist.end(); itp++) {
        name = (*itp).name;
        lat = (*itp).lat;
        lon = (*itp).lon;
        etd = (*itp).time;
        cts = (*itp).CTS;
        smg = (*itp).SMG;
        dis = (*itp).distTo;
        brg = (*itp).brgTo;
        set = (*itp).set;
        rat = (*itp).rate;

        routetable->m_wpList->InsertItem(in, "", -1);
        routetable->m_wpList->SetItem(in, 1, name);
        routetable->m_wpList->SetItem(in, 2, dis);
        routetable->m_wpList->SetItem(in, 4, lat);
        routetable->m_wpList->SetItem(in, 5, lon);
        routetable->m_wpList->SetItem(in, 6, etd);
        routetable->m_wpList->SetItem(in, 8, cts);
        routetable->m_wpList->SetItem(in, 9, set);
        routetable->m_wpList->SetItem(in, 10, rat);

        in++;
      }
    }
  }

  routetable->Show();
}

void otidalrouteUIDialog::GetTable(wxString myRoute) {
  wxString name;

  if (m_TidalRoutes.empty()) {
    wxMessageBox(_("Please select or generate a route"));
    return;
  }
  RouteProp* routetable =
      new RouteProp(this, 7000, _("Tidal Route Table"), wxPoint(200, 200),
                    wxSize(650, 800), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

  int in = 0;

  wxString lat;
  wxString lon;
  wxString etd;
  wxString cts;
  wxString smg;
  wxString dis;
  wxString brg;
  wxString set;
  wxString rat;

  routetable->m_PlanSpeedCtl->SetValue(
      pPlugIn->m_potidalrouteDialog->m_tSpeed->GetValue());

  for (std::list<TidalRoute>::iterator it = m_TidalRoutes.begin();
       it != m_TidalRoutes.end(); it++) {
    name = (*it).Name;
    if (myRoute == name) {
      routetable->m_RouteNameCtl->SetValue(name);
      routetable->m_RouteStartCtl->SetValue((*it).Start);
      routetable->m_RouteDestCtl->SetValue((*it).End);

      routetable->m_TotalDistCtl->SetValue((*it).Distance);
      routetable->m_TimeEnrouteCtl->SetValue((*it).Time);
      routetable->m_StartTimeCtl->SetValue((*it).StartTime);
      routetable->m_TypeRouteCtl->SetValue((*it).Type);

      for (std::list<Position>::iterator itp = (*it).m_positionslist.begin();
           itp != (*it).m_positionslist.end(); itp++) {
        name = (*itp).name;
        lat = (*itp).lat;
        lon = (*itp).lon;
        etd = (*itp).time;
        cts = (*itp).CTS;
        smg = (*itp).SMG;
        dis = (*itp).distTo;
        brg = (*itp).brgTo;
        set = (*itp).set;
        rat = (*itp).rate;

        routetable->m_wpList->InsertItem(in, "", -1);
        routetable->m_wpList->SetItem(in, 1, name);
        routetable->m_wpList->SetItem(in, 2, dis);
        routetable->m_wpList->SetItem(in, 3, brg);
        routetable->m_wpList->SetItem(in, 4, lat);
        routetable->m_wpList->SetItem(in, 5, lon);
        routetable->m_wpList->SetItem(in, 6, etd);
        routetable->m_wpList->SetItem(in, 7, smg);
        routetable->m_wpList->SetItem(in, 8, cts);
        routetable->m_wpList->SetItem(in, 9, set);
        routetable->m_wpList->SetItem(in, 10, rat);

        in++;
      }
    }
  }
  routetable->Show();
}

void otidalrouteUIDialog::GetTides(wxString myRoute) {
  wxString name;

  if (m_TidalRoutes.empty()) {
    wxMessageBox(_("Please select or generate a route"));
    return;
  }

  m_arrowList.clear();  // Prepare for drawing tidal arrows
  Arrow m_arrow;

  int in = 0;

  wxString lat;
  wxString lon;
  wxString etd;
  wxString cts;
  wxString smg;
  wxString dis;
  wxString brg;
  wxString set;
  wxString rate;

  for (std::list<TidalRoute>::iterator it = m_TidalRoutes.begin();
       it != m_TidalRoutes.end(); it++) {
    name = (*it).Name;
    if (myRoute == name) {
      for (std::list<Position>::iterator itp = (*it).m_positionslist.begin();
           itp != (*it).m_positionslist.end(); itp++) {
        name = (*itp).name;
        lat = (*itp).lat;
        lon = (*itp).lon;
        set = (*itp).set;
        rate = (*itp).rate;

        lat.ToDouble(&m_arrow.m_lat);
        lon.ToDouble(&m_arrow.m_lon);
        set.ToDouble(&m_arrow.m_dir);
        rate.ToDouble(&m_arrow.m_force);
        if ((*itp).set != "----" || (*itp).rate != "----")
          m_arrowList.push_back(m_arrow);

        in++;
      }
    }
  }
  b_showTidalArrow = true;
}

void otidalrouteUIDialog::AddChartRoute(wxString myRoute) {

  PlugIn_Route* newRoute =
      new PlugIn_Route;  // for adding a route on OpenCPN chart display
  PlugIn_Waypoint* wayPoint = new PlugIn_Waypoint;

  double lati, loni, value, value1;

  for (std::list<TidalRoute>::iterator it = m_TidalRoutes.begin();
       it != m_TidalRoutes.end(); it++) {
    if ((*it).Name == myRoute) {
      newRoute->m_GUID = (*it).m_GUID;
      newRoute->m_NameString = (*it).Name;
      newRoute->m_StartString = (*it).Start;
      newRoute->m_EndString = (*it).End;

      for (std::list<Position>::iterator itp = (*it).m_positionslist.begin();
           itp != (*it).m_positionslist.end(); itp++) {
        PlugIn_Waypoint* wayPoint = new PlugIn_Waypoint;

        wayPoint->m_MarkName = (*itp).name;
        if (!(*itp).lat.ToDouble(&value)) { /* error! */
        }
        lati = value;
        if (!(*itp).lon.ToDouble(&value1)) { /* error! */
        }
        loni = value1;
        wayPoint->m_lat = lati;
        wayPoint->m_lon = loni;
        wayPoint->m_MarkDescription = (*itp).time;
        wayPoint->m_GUID = (*itp).guid;
        wayPoint->m_IsVisible = (*itp).show_name;
        wayPoint->m_IconName = (*itp).icon_name;

        newRoute->pWaypointList->Append(wayPoint);
      }

      AddPlugInRoute(newRoute, true);

      if ((*it).Type == wxT("ETA")) {
        wxMessageBox(_("ETA Route has been charted!"));
      } else if ((*it).Type == wxT("DR")) {
        wxMessageBox(_("DR Route has been charted!"));
      }
      GetParent()->Refresh();
      break;
    }
  }
}

void otidalrouteUIDialog::AddTidalRoute(TidalRoute tr) {
  m_TidalRoutes.push_back(tr);
  wxString it = tr.Name;
  m_ConfigurationDialog.m_lRoutes->Append(it);
}

void otidalrouteUIDialog::RequestGrib(wxDateTime time) {
  Json::Value v;
  time = time.FromUTC();

  v["Day"] = time.GetDay();
  v["Month"] = time.GetMonth();
  v["Year"] = time.GetYear();
  v["Hour"] = time.GetHour();
  v["Minute"] = time.GetMinute();
  v["Second"] = time.GetSecond();

  Json::FastWriter w;

  SendPluginMessage("GRIB_TIMELINE_RECORD_REQUEST", w.write(v));

  Lock();
  m_bNeedsGrib = false;
  Unlock();
}

void otidalrouteUIDialog::DRCalculate(wxCommandEvent& event) {
  bool fGPX = m_cbGPX->GetValue();
  if (fGPX) {
    CalcDR(event, true, 1);
  } else {
    CalcDR(event, false, 1);
  }
}

void otidalrouteUIDialog::ETACalculate(wxCommandEvent& event) {
  bool fGPX = m_cbGPX->GetValue();
  if (fGPX) {
    CalcETA(event, true, 1);
  } else {
    CalcETA(event, false, 1);
  }
}

void otidalrouteUIDialog::CalcDR(wxCommandEvent& event, bool write_file,
                                 int Pattern) {
  if (m_tRouteName->GetValue() == wxEmptyString) {
    wxMessageBox(_("Please enter a name for the route!"));
    return;
  }

  m_choiceDepartureTimes->SetStringSelection("1");  // we only need one DR route

  TidalRoute tr;  // tidal route for saving in the config file
  PlugIn_Route* newRoute = new PlugIn_Route;  // for immediate use as a route on

  Position ptr;

  wxString m_RouteName;

  gotMyGPXFile = false;  // only load the raw gpx file once for a DR route

  if (m_TidalRoutes.empty()) {
    m_RouteName = m_tRouteName->GetValue() + "." + "DR";
    tr.Name = m_RouteName;
    tr.Type = "DR";
  } else {
    for (std::list<TidalRoute>::iterator it = m_TidalRoutes.begin();
         it != m_TidalRoutes.end(); it++) {
      m_RouteName = m_tRouteName->GetValue() + "." + "DR";
      if ((*it).Name == m_RouteName) {
        wxMessageBox(_("Route name already exists, please edit the name"));
        return;
      } else {
        tr.m_positionslist.clear();
        tr.Name = m_RouteName;
        tr.Type = "DR";
      }
    }
  }

  newRoute->m_NameString = tr.Name;
  newRoute->m_GUID =
      wxString::Format("%i", (int)GetRandomNumber(1, 4000000));

  tr.Start = "Start";
  tr.End = "End";
  tr.m_GUID = newRoute->m_GUID;

  if (OpenXML(gotMyGPXFile)) {
    bool error_occured = false;

    double lat1, lon1;

    int num_hours = 1;
    int n = 0;

    lat1 = 0.0;
    lon1 = 0.0;

    wxString s;
    if (write_file) {
      wxFileDialog dlg(this, _("Export DR Positions in GPX file as"),
                       wxEmptyString, wxEmptyString,
                       "GPX files (*.gpx)|*.gpx|All files (*.*)|*.*",
                       wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
      if (dlg.ShowModal() == wxID_CANCEL) {
        error_occured = true;  // the user changed idea...
        return;
      }

      s = dlg.GetPath();
      if (dlg.GetPath() == wxEmptyString) {
        error_occured = true;
        if (dbg) printf("Empty Path\n");
      }
    }

    // Validate input ranges
    if (!error_occured) {
      if (std::abs(lat1) > 90) {
        error_occured = true;
      }
      if (std::abs(lon1) > 180) {
        error_occured = true;
      }
      if (error_occured) wxMessageBox(_("error in input range validation"));
    }

    // Start writing GPX
    TiXmlDocument doc;
    TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "utf-8", "");
    doc.LinkEndChild(decl);
    TiXmlElement* root = new TiXmlElement("gpx");
    TiXmlElement* Route = new TiXmlElement("rte");
    TiXmlElement* RouteName = new TiXmlElement("name");
    TiXmlText* text4 = new TiXmlText(this->m_tRouteName->GetValue().ToUTF8());

    if (write_file) {
      doc.LinkEndChild(root);
      root->SetAttribute("version", "0.1");
      root->SetAttribute("creator", "otidalroute_pi by Rasbats");
      root->SetAttribute("xmlns:xsi",
                         "http://www.w3.org/2001/XMLSchema-instance");
      root->SetAttribute("xmlns:gpxx",
                         "http://www.garmin.com/xmlschemas/GpxExtensions/v3");
      root->SetAttribute("xsi:schemaLocation",
                         "http://www.topografix.com/GPX/1/1 "
                         "http://www.topografix.com/GPX/1/1/gpx.xsd");
      root->SetAttribute("xmlns:opencpn", "http://www.opencpn.org");
      Route->LinkEndChild(RouteName);
      RouteName->LinkEndChild(text4);
    }

    switch (Pattern) {
      case 1: {
        if (dbg) cout << "DR Calculation\n";
        double speed = 0;

        if (!this->m_tSpeed->GetValue().ToDouble(&speed)) {
          speed = 5.0;
        }  // 5 kts default speed

        double lati, loni;
        double latN[200], lonN[200];
        double latF, lonF;

        Position my_point;

        double value, value1;

        for (std::vector<Position>::iterator it = my_positions.begin();
             it != my_positions.end(); it++) {
          if (!(*it).lat.ToDouble(&value)) { /* error! */
          }
          lati = value;
          if (!(*it).lon.ToDouble(&value1)) { /* error! */
          }
          loni = value1;

          waypointName[n] = (*it).name;
          // wxMessageBox(waypointName[n]);

          latN[n] = lati;
          lonN[n] = loni;

          n++;
        }

        // my_positions.clear();
        n--;

        int routepoints = n + 1;

        double myDist, myBrng;
        myBrng = 0;
        myDist = 0;

        double myLast, route_dist;

        route_dist = 0;
        myLast = 0;
        double total_dist = 0;
        int c;

        lati = latN[0];
        loni = lonN[0];

        double spd, dir;
        spd = 0;
        dir = 0;

        double VBG, VBG1;
        VBG = speed;
        VBG1 = 0;
        int tc_index = 0;
        c = 0;

        double iDist = 0;
        double tdist = 0;  // For accumulating the total distance by adding
                           // the distance for each leg
        double ptrDist = 0;
        int epNumber = 0;
        wxString epName;

        wxDateTime dt, dtCurrent;

        wxString ddt, sdt;
        wxTimeSpan HourSpan;
        HourSpan = wxTimeSpan::Hours(1);

        wxDateTime dtStart, dtEnd;
        wxTimeSpan trTime;
        double trTimeHours;

        sdt = m_textCtrl1->GetValue();  // date/time route starts
        dt.ParseDateTime(sdt);

        sdt = dt.Format("%Y-%m-%d  %H:%M ");
        m_textCtrl1->SetValue(sdt);

        tr.StartTime = sdt;

        dtStart = dt;
        dtCurrent = dt;

        //
        // Time iso distance (3 mins) logic
        //

        /*
                      // We are trying to do three things:
                      //
                      // **Make ptr points on the tidal route for making a
                         route table etc
                      //
                      // **Option to save a GPX file of the calculated route
                      //
                      // **Making a new OpenCPN route for display on the chart
                      //
         */
        int wpn = 0;  // waypoint number
        double timeToRun = 0;
        double timeToWaypoint = 0;
        double waypointDistance;
        double fractpart, intpart;
        int numEP;

        //
        // Loop through the waypoints of the route
        //
        for (wpn; wpn < n; wpn++) {  // loop through the waypoints

          DistanceBearingMercator_Plugin(latN[wpn + 1], lonN[wpn + 1],
                                         latN[wpn], lonN[wpn], &myBrng,
                                         &myDist);

          //
          // set up the waypoint
          //
          //

          PlugIn_Waypoint* newPoint;

          newPoint = new PlugIn_Waypoint(
              latN[wpn], lonN[wpn], "Symbol-Empty", waypointName[wpn]);

          newPoint->m_GUID =
              wxString::Format("%i", (int)GetRandomNumber(1, 4000000));
          newPoint->m_MarkDescription =
              dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
          newRoute->pWaypointList->Append(newPoint);

          //
          // Save the GPX file routepoint
          //

          my_point.lat = wxString::Format("%f", latN[wpn]);
          my_point.lon = wxString::Format("%f", lonN[wpn]);
          my_point.routepoint = 1;
          my_point.wpt_num = waypointName[wpn].mb_str();
          my_point.name = waypointName[wpn].mb_str();
          my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
          my_points.push_back(my_point);

          //
          // Save the route point for the route table
          //

          ptr.name = waypointName[wpn].mb_str();
          ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
          ptr.lat = wxString::Format("%8.4f", latN[wpn]);
          ptr.lon = wxString::Format("%8.4f", lonN[wpn]);
          ptr.guid = newPoint->m_GUID;
          ptr.CTS = wxString::Format("%03.0f", myBrng);
          ptr.SMG = wxString::Format("%5.1f", VBG);

          if (wpn == 0) {
            ptr.distTo = "----";
            ptr.brgTo = "----";
            VBG1 = VBG;
          } else {
            tdist += ptrDist;
            ptr.distTo = wxString::Format("%.4f", ptrDist);
            ptr.brgTo = wxString::Format("%03.0f", myBrng);
          }

          ptr.set = wxString::Format("%03.0f", dir);
          ptr.rate = wxString::Format("%5.1f", spd);
          ptr.icon_name = "Circle";

          tr.m_positionslist.push_back(ptr);

          latF = latN[wpn];  // Position of the last waypoint
          lonF = lonN[wpn];

          if (wpn == 0) {            
            DistanceBearingMercator_Plugin(
                latN[wpn + 1], lonN[wpn + 1], latN[wpn], lonN[wpn], &myBrng,
                &waypointDistance);  // how far to the next waypoint?

            timeToWaypoint = waypointDistance / VBG;

            if (timeToWaypoint < 1) {
              // no space for an EP

              timeToRun = 1 - timeToWaypoint;
              //
              // timeToRun is the part of one hour remaining to run after
              // passing the waypoint
              //
              tr.Start = waypointName[wpn].mb_str();
              dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
              ptrDist = waypointDistance;

            } else {
              tr.Start = waypointName[wpn].mb_str();  // name does not change

              // Move to the EP on this leg with initial VBG (VBG1)
              PositionBearingDistanceMercator_Plugin(
                  latN[wpn], lonN[wpn], myBrng, VBG, &lati, &loni);

              // Move on one hour to the first EP
              dtCurrent = dtCurrent.Add(HourSpan);

              epNumber++;
              epName = "DR" + wxString::Format("%i", epNumber);
              PlugIn_Waypoint* epPoint = new PlugIn_Waypoint(
                  lati, loni, "Symbol-X-Large-Magenta", epName);
              epPoint->m_IconName = "Symbol-X-Large-Magenta";
              epPoint->m_GUID =
                  wxString::Format("%i", (int)GetRandomNumber(1, 4000000));
              newRoute->pWaypointList->Append(
                  epPoint);  // for the OpenCPN display route

              // print DR for the GPX file
              my_point.lat = wxString::Format("%f", lati);
              my_point.lon = wxString::Format("%f", loni);
              my_point.routepoint = 0;
              my_point.wpt_num =
                  "EP" + wxString::Format("%i", epNumber);
              my_point.name = "EP" + wxString::Format("%i", epNumber);
              my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
              my_points.push_back(my_point);

              // print DR for the config file

              ptrDist = VBG;
              tdist += ptrDist;
              ptr.name = epName;
              ptr.lat = wxString::Format("%8.4f", lati);
              ptr.lon = wxString::Format("%8.4f", loni);
              ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
              ptr.guid = epPoint->m_GUID;
              ptr.distTo = wxString::Format("%.4f", ptrDist);
              ptr.brgTo = wxString::Format("%03.0f", myBrng);
              ptr.CTS = wxString::Format("%03.0f", myBrng);
              ptr.SMG = wxString::Format("%5.1f", VBG);
              ptr.set = wxString::Format("%03.0f", dir);
              ptr.rate = wxString::Format("%5.1f", spd);
              ptr.icon_name = "Symbol-X-Large-Magenta";
              tr.m_positionslist.push_back(ptr);

              // work out the number of DR
              // must be more than one DR as we have worked this out already

              DistanceBearingMercator_Plugin(
                  latN[wpn + 1], lonN[wpn + 1], lati, loni, &myBrng,
                  &waypointDistance);  // how far to the next waypoint?

              // How many DR are possible on the first leg?

              timeToWaypoint = waypointDistance / VBG;
              fractpart = modf(timeToWaypoint, &intpart);
              numEP = intpart;  // was intpart + 1

              if (numEP == 0) {
                timeToRun = 1 - timeToWaypoint;
                dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
                ptrDist = timeToWaypoint * VBG;

                //
                // dtCurrent is now the time at waypoint 1
                //
              }

              else {
                latF = lati;
                lonF = loni;

                for (int z = 0; z <= numEP; z++) {
                  ptrDist = VBG;

                  PositionBearingDistanceMercator_Plugin(
                      latF, lonF, myBrng, VBG, &lati,
                      &loni);  // first waypoint of the leg

                  // Time at the next plotted EP
                  dtCurrent = dtCurrent.Add(HourSpan);                 

                  epNumber++;  // Add a DR
                  epName = "DR" + wxString::Format("%i", epNumber);
                  PlugIn_Waypoint* epPoint = new PlugIn_Waypoint(
                      lati, loni, "Symbol-X-Large-Magenta", epName);
                  epPoint->m_IconName = "Symbol-X-Large-Magenta";
                  // epPoint->m_MarkDescription = ddt;
                  epPoint->m_GUID = wxString::Format(
                      "%i", (int)GetRandomNumber(1, 4000000));
                  newRoute->pWaypointList->Append(
                      epPoint);  // for the OpenCPN display route

                  // print mid points for the GPX file
                  my_point.lat = wxString::Format(wxT("%f"), lati);
                  my_point.lon = wxString::Format(wxT("%f"), loni);
                  my_point.routepoint = 0;
                  my_point.wpt_num =
                      "DR" + wxString::Format("%i", epNumber);
                  my_point.name =
                      "DR" + wxString::Format("%i", epNumber);
                  my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                  my_points.push_back(my_point);

                  // print EP for the config file
                  // ptrDist = VBG;
                  tdist += ptrDist;
                  ptr.name = epName;
                  ptr.lat = wxString::Format("%8.4f", lati);
                  ptr.lon = wxString::Format("%8.4f", loni);
                  ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                  ptr.guid = epPoint->m_GUID;
                  ptr.distTo = wxString::Format("%.4f", ptrDist);
                  ptr.brgTo = wxString::Format("%03.0f", myBrng);
                  ptr.CTS = wxString::Format("%03.0f", myBrng);
                  ptr.SMG = wxString::Format("%5.1f", VBG);
                  ptr.set = wxString::Format("%03.0f", dir);
                  ptr.rate = wxString::Format("%5.1f", spd);
                  ptr.icon_name = "Symbol-X-Large-Magenta";
                  tr.m_positionslist.push_back(ptr);

                  DistanceBearingMercator_Plugin(
                      latN[wpn + 1], lonN[wpn + 1], lati, loni, &myBrng,
                      &waypointDistance);  // how far to the next waypoint?

                  timeToWaypoint = waypointDistance / VBG;

                  if (timeToWaypoint < 1) {  // No time for another EP

                    z = numEP + 1;  // to stop the next EP being made

                    timeToRun = 1 - timeToWaypoint;
                    dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
                    ptrDist = timeToWaypoint * VBG;
                  }

                  latF = lati;
                  lonF = loni;
                }
              }
            }
          }

          else {  // *************** After waypoint zero **********
                  // **********************************************

            DistanceBearingMercator_Plugin(
                latN[wpn + 1], lonN[wpn + 1], latN[wpn], lonN[wpn], &myBrng,
                &waypointDistance);  // how far to the next waypoint?

            timeToWaypoint = waypointDistance / VBG;

            if (timeToWaypoint < timeToRun) {
              timeToRun = timeToRun - timeToWaypoint;
              dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
              ptrDist = timeToWaypoint * VBG;
              // Do not add an EP. The next position plotted is the route wpt.

            } else {
              // space for an EP
              // we need the position for the first EP on the new leg ...
              // latloni
              //
              double distEP = timeToRun * VBG;

              PositionBearingDistanceMercator_Plugin(
                  latN[wpn], lonN[wpn], myBrng, distEP, &lati,
                  &loni);  // first DR of the new leg

              //
              // Time at the first DR of the leg
              //
              dtCurrent = AdvanceSeconds(dtCurrent, timeToRun);
              ptrDist = timeToRun * VBG;            

              epNumber++;  // Add an EP
              epName = "DR" + wxString::Format("%i", epNumber);
              PlugIn_Waypoint* epPoint = new PlugIn_Waypoint(
                  lati, loni, "Symbol-X-Large-Magenta", epName);
              epPoint->m_IconName = "Symbol-X-Large-Magenta";
              epPoint->m_GUID =
                  wxString::Format("%i", (int)GetRandomNumber(1, 4000000));
              newRoute->pWaypointList->Append(
                  epPoint);  // for the OpenCPN display route

              // print DR for the GPX file
              my_point.lat = wxString::Format("%f", lati);
              my_point.lon = wxString::Format("%f", loni);
              my_point.routepoint = 0;
              my_point.wpt_num =
                  "DR" + wxString::Format("%i", epNumber);
              my_point.name = "DR" + wxString::Format("%i", epNumber);
              my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
              my_points.push_back(my_point);

              // print DR for the config file
              tdist += ptrDist;
              ptr.name = epName;
              ptr.lat = wxString::Format("%8.4f", lati);
              ptr.lon = wxString::Format("%8.4f", loni);
              ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
              ptr.guid = epPoint->m_GUID;
              ptr.distTo = wxString::Format("%.1f", ptrDist);
              ptr.brgTo = wxString::Format("%03.0f", myBrng);
              ptr.CTS = wxString::Format("%03.0f", myBrng);
              ptr.SMG = wxString::Format("%5.1f", VBG);
              ptr.set = wxString::Format("%03.0f", dir);
              ptr.rate = wxString::Format("%5.1f", spd);
              ptr.icon_name = "Symbol-X-Large-Magenta";
              tr.m_positionslist.push_back(ptr);

              DistanceBearingMercator_Plugin(
                  latN[wpn + 1], lonN[wpn + 1], lati, loni, &myBrng,
                  &waypointDistance);  // how far to the next waypoint?

              latF = lati;
              lonF = loni;

              // Find out if any space for more EP
              timeToWaypoint = waypointDistance / VBG;
              fractpart = modf(timeToWaypoint, &intpart);
              numEP = intpart;

              if (numEP == 0) {
                timeToRun = 1 - timeToWaypoint;
                dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
                ptrDist = timeToWaypoint * VBG;

              } else {
                for (int z = 0; z <= numEP; z++) {
                  ptrDist = VBG;

                  PositionBearingDistanceMercator_Plugin(
                      latF, lonF, myBrng, VBG, &lati,
                      &loni);  // first waypoint of the leg

                  dtCurrent = dtCurrent.Add(HourSpan);

                  epNumber++;
                  epName = "DR" + wxString::Format("%i", epNumber);
                  PlugIn_Waypoint* epPoint = new PlugIn_Waypoint(
                      lati, loni, "Symbol-X-Large-Magenta", epName);
                  epPoint->m_IconName = "Symbol-X-Large-Magenta";
                  epPoint->m_GUID = wxString::Format(
                      "%i", (int)GetRandomNumber(1, 4000000));
                  newRoute->pWaypointList->Append(
                      epPoint);  // for the OpenCPN display route

                  // print mid points for the GPX file
                  my_point.lat = wxString::Format("%f", lati);
                  my_point.lon = wxString::Format("%f", loni);
                  my_point.routepoint = 0;
                  my_point.wpt_num =
                      "DR" + wxString::Format("%i", epNumber);
                  my_point.name =
                      "DR" + wxString::Format("%i", epNumber);
                  my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                  my_points.push_back(my_point);

                  // print EP for the config file
                  // ptrDist = VBG;
                  tdist += ptrDist;
                  ptr.name = epName;
                  ptr.lat = wxString::Format("%8.4f", lati);
                  ptr.lon = wxString::Format("%8.4f", loni);
                  ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                  ptr.guid = epPoint->m_GUID;
                  ptr.distTo = wxString::Format("%.1f", ptrDist);
                  ptr.brgTo = wxString::Format("%03.0f", myBrng);
                  ptr.CTS = wxString::Format("%03.0f", myBrng);
                  ptr.SMG = wxString::Format("%5.1f", VBG);
                  ptr.set = wxString::Format("%03.0f", dir);
                  ptr.rate = wxString::Format("%5.1f", spd);
                  ptr.icon_name = "Symbol-X-Large-Magenta";
                  tr.m_positionslist.push_back(ptr);

                  DistanceBearingMercator_Plugin(
                      latN[wpn + 1], lonN[wpn + 1], lati, loni, &myBrng,
                      &waypointDistance);  // how far to the next waypoint?
                  timeToWaypoint = waypointDistance / VBG;

                  if (timeToWaypoint < 1) {
                    z = numEP + 1;  // to stop the next EP being made

                    timeToRun = 1 - timeToWaypoint;
                    dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
                    ptrDist = timeToWaypoint * VBG;
                  }

                  latF = lati;
                  lonF = loni;
                }
              }
            }
          }  // Finished the waypoints after zero
        }    // Finished all waypoints

        // print the last routepoint
        PlugIn_Waypoint* endPoint = new PlugIn_Waypoint(
            latN[wpn], lonN[wpn], "Circle", waypointName[wpn]);
        endPoint->m_MarkName = waypointName[wpn];
        endPoint->m_MarkDescription =
            dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
        endPoint->m_GUID =
            wxString::Format("%i", (int)GetRandomNumber(1, 4000000));
        newRoute->pWaypointList->Append(endPoint);

        //
        // print the last my_point for writing GPX
        //

        my_point.lat = wxString::Format("%f", latN[wpn]);
        my_point.lon = wxString::Format("%f", lonN[wpn]);
        my_point.routepoint = 1;
        my_point.wpt_num = waypointName[wpn].mb_str();
        my_point.name = waypointName[wpn].mb_str();
        my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
        my_points.push_back(my_point);

        // print the last waypoint detail for the TidalRoute
        tr.EndTime = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");

        trTime = dtCurrent - dtStart;
        trTimeHours = (double)trTime.GetMinutes() / 60;
        tr.Time = wxString::Format("%.1f", trTimeHours);

        ptrDist = waypointDistance;
        tdist += ptrDist;

        tr.Distance = wxString::Format(_("%.1f"), tdist);

        ptr.name = waypointName[wpn].mb_str();
        ptr.lat = wxString::Format("%8.4f", latN[n]);
        ptr.lon = wxString::Format("%8.4f", lonN[n]);
        ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
        ptr.guid = endPoint->m_GUID;
        ptr.set = "----";
        ptr.rate = "----";
        ptr.CTS = "----";
        ptr.SMG = wxString::Format("%5.1f", VBG);
        ptr.distTo = wxString::Format("%.1f", ptrDist);
        ptr.brgTo = wxString::Format("%03.0f", myBrng);
        ptr.icon_name = "Circle";

        tr.m_positionslist.push_back(ptr);
        tr.End = waypointName[wpn].mb_str();
        tr.Type = "DR";
        m_TidalRoutes.push_back(tr);

        // AddPlugInRoute(newRoute); // add the route to OpenCPN routes
        // and display the route on the chart

        SaveXML(m_default_configuration_path);  // add the route and extra
                                                // detail (times, CTS etc)
                                                // to the configuration file

        m_ConfigurationDialog.m_lRoutes->Append(tr.Name);
        m_ConfigurationDialog.Refresh();
        GetParent()->Refresh();

        for (std::vector<Position>::iterator itOut = my_points.begin();
             itOut != my_points.end(); itOut++) {

          double value, value1;
          if (!(*itOut).lat.ToDouble(&value)) { /* error! */
          }
          lati = value;
          if (!(*itOut).lon.ToDouble(&value1)) { /* error! */
          }
          loni = value1;

          if ((*itOut).routepoint == 1) {
            if (write_file) {
              Addpoint(Route, wxString::Format("%f", lati),
                       wxString::Format("%f", loni), (*itOut).name,
                      "Diamond", "WPT");
            }
          } else {
            if ((*itOut).routepoint == 0) {
              if (write_file) {
                Addpoint(Route, wxString::Format("%f", lati),
                         wxString::Format("%f", loni), (*itOut).name,
                         "Symbol-X-Large-Magenta", "WPT");
              }
            }
          }
        }

        my_points.clear();
        break;
      }

      default: {  // Note the colon, not a semicolon
        cout << "Error, bad input, quitting\n";
        break;
      }
    }

    if (write_file) {
      TiXmlElement* Extensions = new TiXmlElement("extensions");

      TiXmlElement* StartN = new TiXmlElement("opencpn:start");
      TiXmlText* text5 = new TiXmlText(waypointName[0].ToUTF8());
      Extensions->LinkEndChild(StartN);
      StartN->LinkEndChild(text5);

      TiXmlElement* EndN = new TiXmlElement("opencpn:end");
      TiXmlText* text6 = new TiXmlText(waypointName[n].ToUTF8());
      Extensions->LinkEndChild(EndN);
      EndN->LinkEndChild(text6);

      Route->LinkEndChild(Extensions);

      root->LinkEndChild(Route);

      wxCharBuffer buffer = s.ToUTF8();
      if (dbg) std::cout << buffer.data() << std::endl;
      doc.SaveFile(buffer.data());
    }

    // end of if no error occured

    if (error_occured == true) {
      wxLogMessage(_("Error in calculation. Please check input!"));
      wxMessageBox(_("Error in calculation. Please check input!"));
    }
  }
  GetParent()->Refresh();
  pPlugIn->m_potidalrouteDialog->Show();

  wxMessageBox(_("DR Route has been calculated!"));
}

void otidalrouteUIDialog::CalcETA(wxCommandEvent& event, bool write_file,
                                  int Pattern) {
  if (m_tRouteName->GetValue() == wxEmptyString) {
    wxMessageBox(_("Please enter a name for the route!"));
    return;
  }

  if (m_textCtrl1->GetValue() == wxEmptyString) {
    wxMessageBox(_("Open the GRIB plugin and select a time!"));
    return;
  }

  wxString s_departureTimes = m_choiceDepartureTimes->GetStringSelection();
  int m_departureTimes = wxAtoi(s_departureTimes);
  int r = 0;
  wxString m_RouteName;
  gotMyGPXFile = false;  // only load the raw gpx file once
  if (OpenXML(gotMyGPXFile)) {
    for (r = 0; r < m_departureTimes; r++) {
      TidalRoute tr;  // tidal route for saving in the config file
      PlugIn_Route* newRoute =
          new PlugIn_Route;  // for immediate use as a route
                             // on OpenCPN chart display

      Position ptr;

      if (m_TidalRoutes.empty()) {
        m_RouteName = m_tRouteName->GetValue() + wxT(".") +
                      wxString::Format(wxT("%i"), r) + wxT(".") + wxT("EP");
        tr.Name = m_RouteName;
        tr.Type = _("ETA");
      } else {
        for (std::list<TidalRoute>::iterator it = m_TidalRoutes.begin();
             it != m_TidalRoutes.end(); it++) {
          m_RouteName = m_tRouteName->GetValue() + wxT(".") +
                        wxString::Format(wxT("%i"), r) + wxT(".") + wxT("EP");
          if ((*it).Name == m_RouteName) {
            wxMessageBox(_("Route name already exists, please edit the name"));
            return;
          } else {
            tr.m_positionslist.clear();
            tr.Name = m_RouteName;
            tr.Type = _("ETA");
          }
        }
      }

      newRoute->m_NameString = tr.Name;
      newRoute->m_GUID =
          wxString::Format("%i", (int)GetRandomNumber(1, 4000000));

      tr.Start = wxT("Start");
      tr.End = wxT("End");
      tr.m_GUID = newRoute->m_GUID;

      bool error_occured = false;

      double lat1, lon1;

      int num_hours = 1;
      int n = 0;

      lat1 = 0.0;
      lon1 = 0.0;

      wxString s;
      if (write_file) {
        wxFileDialog dlg(this, _("Export ETA Positions in GPX file as"),
                         wxEmptyString, wxEmptyString,
                         "GPX files (*.gpx)|*.gpx|All files (*.*)|*.*",
                         wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() == wxID_CANCEL) {
          error_occured = true;  // the user changed idea...
          return;
        }

        s = dlg.GetPath();
        if (dlg.GetPath() == wxEmptyString) {
          error_occured = true;
          if (dbg) printf("Empty Path\n");
        }
      }

      // Validate input ranges
      if (!error_occured) {
        if (std::abs(lat1) > 90) {
          error_occured = true;
        }
        if (std::abs(lon1) > 180) {
          error_occured = true;
        }
        if (error_occured) wxMessageBox(_("error in input range validation"));
      }

      // Start writing GPX
      TiXmlDocument doc;
      TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "utf-8", "");
      doc.LinkEndChild(decl);
      TiXmlElement* root = new TiXmlElement("gpx");
      TiXmlElement* Route = new TiXmlElement("rte");
      TiXmlElement* RouteName = new TiXmlElement("name");
      TiXmlText* text4 = new TiXmlText(this->m_tRouteName->GetValue().ToUTF8());

      if (write_file) {
        doc.LinkEndChild(root);
        root->SetAttribute("version", "0.1");
        root->SetAttribute("creator", "otidalroute_pi by Rasbats");
        root->SetAttribute("xmlns:xsi",
                           "http://www.w3.org/2001/XMLSchema-instance");
        root->SetAttribute("xmlns:gpxx",
                           "http://www.garmin.com/xmlschemas/GpxExtensions/v3");
        root->SetAttribute("xsi:schemaLocation",
                           "http://www.topografix.com/GPX/1/1 "
                           "http://www.topografix.com/GPX/1/1/gpx.xsd");
        root->SetAttribute("xmlns:opencpn", "http://www.opencpn.org");
        Route->LinkEndChild(RouteName);
        RouteName->LinkEndChild(text4);
      }

      switch (Pattern) {
        case 1: {
          if (dbg) cout << "ETA Calculation\n";
          double speed = 0;

          if (!this->m_tSpeed->GetValue().ToDouble(&speed)) {
            speed = 5.0;
          }  // 5 kts default speed

          double lati, loni;
          double latN[200], lonN[200];
          double latF, lonF;

          Position my_point;

          double value, value1;

          for (std::vector<Position>::iterator it = my_positions.begin();
               it != my_positions.end(); it++) {
            if (!(*it).lat.ToDouble(&value)) { /* error! */
            }
            lati = value;
            if (!(*it).lon.ToDouble(&value1)) { /* error! */
            }
            loni = value1;

            waypointName[n] = (*it).name;
            // wxMessageBox(waypointName[n]);

            latN[n] = lati;
            lonN[n] = loni;

            n++;
          }

          // my_positions.clear();
          n--;

          int routepoints = n + 1;

          double myDist, myBrng;
          myBrng = 0;
          myDist = 0;

          double myLast, route_dist;

          route_dist = 0;
          myLast = 0;
          double total_dist = 0;
          int c;

          lati = latN[0];
          loni = lonN[0];

          double VBG, BC, VBG1;
          VBG = 0;
          VBG1 = 0;
          int tc_index = 0;
          c = 0;

          bool m_bGrib;
          double spd, dir;
          spd = 0;
          dir = 0;

          double iDist = 0;
          double tdist = 0;  // For accumulating the total distance by adding
                             // the distance for each leg
          double ptrDist = 0;
          int epNumber = 0;
          wxString epName;

          wxDateTime dt, dtCurrent;

          wxString ddt, sdt;
          wxTimeSpan HourSpan;
          HourSpan = wxTimeSpan::Hours(1);

          wxDateTime dtStart, dtEnd;
          wxTimeSpan trTime;
          double trTimeHours;

          sdt = m_textCtrl1->GetValue();  // date/time route starts
          dt.ParseDateTime(sdt);

          if (r != 0) {
            dt = dt + wxTimeSpan::Hours(1);
          }
          sdt = dt.Format("%Y-%m-%d  %H:%M ");
          m_textCtrl1->SetValue(sdt);

          tr.StartTime = sdt;

          dtStart = dt;
          dtCurrent = dt;

          //
          // Time iso distance (3 mins) logic
          //

          /*
                        // We are trying to do three things:
                        //
                        // **Make ptr points on the tidal route for making a
                           route table etc
                        //
                        // **Option to save a GPX file of the calculated route
                        //
                        // **Making a new OpenCPN route for display on the chart
                        //
           */
          int wpn = 0;  // waypoint number
          double timeToRun = 0;
          double timeToWaypoint = 0;
          double waypointDistance;
          double fractpart, intpart;
          int numEP;

          //
          // Loop through the waypoints of the route
          //
          for (wpn; wpn < n; wpn++) {  // loop through the waypoints

            DistanceBearingMercator_Plugin(latN[wpn + 1], lonN[wpn + 1],
                                           latN[wpn], lonN[wpn], &myBrng,
                                           &myDist);

            // For the tidal current we use the position at the waypoint to
            // estimate the current and use the current time.
            // This is an approximation.

            m_bGrib = GetGribSpdDir(dtCurrent, latN[wpn], lonN[wpn], spd, dir);
            if (!m_bGrib) {
              wxMessageBox(
                  _("Route start date is not compatible with this Grib \n Or "
                    "Grib is not available for part of the route"));
              return;
            }

            CTSWithCurrent(myBrng, VBG, dir, spd, BC,
                           speed);  // VBG = velocity of boat over ground

            //
            // set up the waypoint
            //
            //

            PlugIn_Waypoint* newPoint;

            newPoint = new PlugIn_Waypoint(
                latN[wpn], lonN[wpn], wxT("Symbol-Empty"), waypointName[wpn]);

            newPoint->m_GUID =
                wxString::Format("%i", (int)GetRandomNumber(1, 4000000));
            newPoint->m_MarkDescription =
                dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
            newRoute->pWaypointList->Append(newPoint);

            //
            // Save the GPX file routepoint
            //

            my_point.lat = wxString::Format("%f", latN[wpn]);
            my_point.lon = wxString::Format("%f", lonN[wpn]);
            my_point.routepoint = 1;
            my_point.wpt_num = waypointName[wpn].mb_str();
            my_point.name = waypointName[wpn].mb_str();
            my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
            my_points.push_back(my_point);

            //
            // Save the route point for the route table
            //

            ptr.name = waypointName[wpn].mb_str();
            ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
            ptr.lat = wxString::Format("%8.4f", latN[wpn]);
            ptr.lon = wxString::Format("%8.4f", lonN[wpn]);
            ptr.guid = newPoint->m_GUID;
            ptr.CTS = wxString::Format("%03.0f", BC);
            ptr.SMG = wxString::Format("%5.1f", VBG);

            if (wpn == 0) {
              ptr.distTo = "----";
              ptr.brgTo = "----";
              VBG1 = VBG;
            } else {
              tdist += ptrDist;
              ptr.distTo = wxString::Format("%.4f", ptrDist);
              ptr.brgTo = wxString::Format("%03.0f", myBrng);
            }

            ptr.set = wxString::Format("%03.0f", dir);
            ptr.rate = wxString::Format("%5.1f", spd);
            ptr.icon_name = "Circle";

            tr.m_positionslist.push_back(ptr);

            latF = latN[wpn];  // Position of the last waypoint
            lonF = lonN[wpn];

            if (wpn == 0) {
              VBG1 = VBG;
              DistanceBearingMercator_Plugin(
                  latN[wpn + 1], lonN[wpn + 1], latN[wpn], lonN[wpn], &myBrng,
                  &waypointDistance);  // how far to the next waypoint?

              timeToWaypoint = waypointDistance / VBG1;

              if (timeToWaypoint < 1) {
                // no space for an EP

                timeToRun = 1 - timeToWaypoint;
                //
                // timeToRun is the part of one hour remaining to run after
                // passing the waypoint
                //
                tr.Start = waypointName[wpn].mb_str();
                dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
                ptrDist = waypointDistance;

              } else {
                tr.Start = waypointName[wpn].mb_str();  // name does not change

                // Move to the EP on this leg with initial VBG (VBG1)
                PositionBearingDistanceMercator_Plugin(
                    latN[wpn], lonN[wpn], myBrng, VBG1, &lati, &loni);

                // Move on one hour to the first EP
                dtCurrent = dtCurrent.Add(HourSpan);

                // Find the tidal current at the EP
                m_bGrib = GetGribSpdDir(dtCurrent, lati, loni, spd, dir);
                if (!m_bGrib) {
                  wxMessageBox(
                      _("Route start date is not compatible with this Grib "
                        "\n Or "
                        "Grib is not available for part of the route"));
                  return;
                }

                CTSWithCurrent(myBrng, VBG, dir, spd, BC,
                               speed);  // VBG = velocity of boat over ground

                epNumber++;
                epName = "EP" + wxString::Format(wxT("%i"), epNumber);
                PlugIn_Waypoint* epPoint =
                    new PlugIn_Waypoint(lati, loni, wxT("Triangle"), epName);
                epPoint->m_IconName = wxT("Triangle");
                epPoint->m_GUID = wxString::Format(
                    "%i", (int)GetRandomNumber(1, 4000000));
                newRoute->pWaypointList->Append(
                    epPoint);  // for the OpenCPN display route

                // print EP for the GPX file
                my_point.lat = wxString::Format(wxT("%f"), lati);
                my_point.lon = wxString::Format(wxT("%f"), loni);
                my_point.routepoint = 0;
                my_point.wpt_num =
                    "EP" + wxString::Format("%i", epNumber);
                my_point.name = "EP" + wxString::Format("%i", epNumber);
                my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                my_points.push_back(my_point);

                // print EP for the config file

                ptrDist = VBG1;
                tdist += ptrDist;
                ptr.name = epName;
                ptr.lat = wxString::Format("%8.4f", lati);
                ptr.lon = wxString::Format("%8.4f", loni);
                ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                ptr.guid = epPoint->m_GUID;
                ptr.distTo = wxString::Format("%.4f", ptrDist);
                ptr.brgTo = wxString::Format("%03.0f", myBrng);
                ptr.CTS = wxString::Format("%03.0f", BC);
                ptr.SMG = wxString::Format("%5.1f", VBG);
                ptr.set = wxString::Format("%03.0f", dir);
                ptr.rate = wxString::Format("%5.1f", spd);
                ptr.icon_name = wxT("Triangle");
                tr.m_positionslist.push_back(ptr);

                // work out the number of EP
                // must be more than one EP as we have worked this out already

                DistanceBearingMercator_Plugin(
                    latN[wpn + 1], lonN[wpn + 1], lati, loni, &myBrng,
                    &waypointDistance);  // how far to the next waypoint?

                // How many EP are possible on the first leg?

                timeToWaypoint = waypointDistance / VBG;
                fractpart = modf(timeToWaypoint, &intpart);
                numEP = intpart;  // was intpart + 1

                // wxString sSpeed = wxString::Format("%i", numEP);
                // wxMessageBox(sSpeed);

                if (numEP == 0) {
                  timeToRun = 1 - timeToWaypoint;
                  dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
                  ptrDist = timeToWaypoint * VBG;

                  //
                  // dtCurrent is now the time at waypoint 1
                  //
                }

                else {
                  latF = lati;
                  lonF = loni;

                  for (int z = 0; z <= numEP; z++) {
                    ptrDist = VBG;

                    PositionBearingDistanceMercator_Plugin(
                        latF, lonF, myBrng, VBG, &lati,
                        &loni);  // first waypoint of the leg

                    // Time at the next plotted EP
                    dtCurrent = dtCurrent.Add(HourSpan);

                    // Find the tidal current at the EP
                    m_bGrib = GetGribSpdDir(dtCurrent, lati, loni, spd, dir);
                    if (!m_bGrib) {
                      wxMessageBox(
                          _("Route start date is not compatible with this Grib "
                            "\n Or "
                            "Grib is not available for part of the route"));
                      return;
                    }
                    CTSWithCurrent(
                        myBrng, VBG, dir, spd, BC,
                        speed);  // VBG = velocity of boat over ground

                    epNumber++;  // Add an EP
                    epName = "EP" + wxString::Format(wxT("%i"), epNumber);
                    PlugIn_Waypoint* epPoint = new PlugIn_Waypoint(
                        lati, loni, wxT("Triangle"), epName);
                    epPoint->m_IconName = wxT("Triangle");
                    // epPoint->m_MarkDescription = ddt;
                    epPoint->m_GUID = wxString::Format(
                        "%i", (int)GetRandomNumber(1, 4000000));
                    newRoute->pWaypointList->Append(
                        epPoint);  // for the OpenCPN display route

                    // print mid points for the GPX file
                    my_point.lat = wxString::Format(wxT("%f"), lati);
                    my_point.lon = wxString::Format(wxT("%f"), loni);
                    my_point.routepoint = 0;
                    my_point.wpt_num =
                        "EP" + wxString::Format("%i", epNumber);
                    my_point.name =
                        "EP" + wxString::Format("%i", epNumber);
                    my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                    my_points.push_back(my_point);

                    // print EP for the config file
                    // ptrDist = VBG;
                    tdist += ptrDist;
                    ptr.name = epName;
                    ptr.lat = wxString::Format("%8.4f", lati);
                    ptr.lon = wxString::Format("%8.4f", loni);
                    ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                    ptr.guid = epPoint->m_GUID;
                    ptr.distTo = wxString::Format("%.4f", ptrDist);
                    ptr.brgTo = wxString::Format("%03.0f", myBrng);
                    ptr.CTS = wxString::Format("%03.0f", BC);
                    ptr.SMG = wxString::Format("%5.1f", VBG);
                    ptr.set = wxString::Format("%03.0f", dir);
                    ptr.rate = wxString::Format("%5.1f", spd);
                    ptr.icon_name = wxT("Triangle");
                    tr.m_positionslist.push_back(ptr);

                    DistanceBearingMercator_Plugin(
                        latN[wpn + 1], lonN[wpn + 1], lati, loni, &myBrng,
                        &waypointDistance);  // how far to the next waypoint?

                    timeToWaypoint = waypointDistance / VBG;

                    if (timeToWaypoint < 1) {  // No time for another EP

                      z = numEP + 1;  // to stop the next EP being made

                      timeToRun = 1 - timeToWaypoint;
                      dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
                      ptrDist = timeToWaypoint * VBG;
                    }

                    latF = lati;
                    lonF = loni;
                  }
                }
              }
            }

            else {  // *************** After waypoint zero **********
                    // **********************************************

              DistanceBearingMercator_Plugin(
                  latN[wpn + 1], lonN[wpn + 1], latN[wpn], lonN[wpn], &myBrng,
                  &waypointDistance);  // how far to the next waypoint?

              timeToWaypoint = waypointDistance / VBG;

              if (timeToWaypoint < timeToRun) {
                timeToRun = timeToRun - timeToWaypoint;
                dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
                ptrDist = timeToWaypoint * VBG;
                // Do not add an EP. The next position plotted is the route wpt.

              } else {
                // space for an EP
                // we need the position for the first EP on the new leg ...
                // latloni
                //
                double distEP = timeToRun * VBG;

                PositionBearingDistanceMercator_Plugin(
                    latN[wpn], lonN[wpn], myBrng, distEP, &lati,
                    &loni);  // first EP of the new leg

                //
                // Time at the first EP of the leg
                //
                dtCurrent = AdvanceSeconds(dtCurrent, timeToRun);
                ptrDist = timeToRun * VBG;
                // Find the tidal current at the EP

                m_bGrib = GetGribSpdDir(dtCurrent, lati, loni, spd, dir);
                if (!m_bGrib) {
                  wxMessageBox(
                      _("Route start date is not compatible with this Grib "
                        "\n Or "
                        "Grib is not available for part of the route"));
                  return;
                }

                CTSWithCurrent(myBrng, VBG, dir, spd, BC,
                               speed);  // VBG = velocity of boat over ground

                epNumber++;  // Add an EP
                epName = "EP" + wxString::Format(wxT("%i"), epNumber);
                PlugIn_Waypoint* epPoint =
                    new PlugIn_Waypoint(lati, loni, wxT("Triangle"), epName);
                epPoint->m_IconName = wxT("Triangle");
                epPoint->m_GUID = wxString::Format(
                    "%i", (int)GetRandomNumber(1, 4000000));
                newRoute->pWaypointList->Append(
                    epPoint);  // for the OpenCPN display route

                // print EP for the GPX file
                my_point.lat = wxString::Format(wxT("%f"), lati);
                my_point.lon = wxString::Format(wxT("%f"), loni);
                my_point.routepoint = 0;
                my_point.wpt_num =
                    "EP" + wxString::Format("%i", epNumber);
                my_point.name = "EP" + wxString::Format("%i", epNumber);
                my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                my_points.push_back(my_point);

                // print EP for the config file
                tdist += ptrDist;
                ptr.name = epName;
                ptr.lat = wxString::Format("%8.4f", lati);
                ptr.lon = wxString::Format("%8.4f", loni);
                ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                ptr.guid = epPoint->m_GUID;
                ptr.distTo = wxString::Format("%.1f", ptrDist);
                ptr.brgTo = wxString::Format("%03.0f", myBrng);
                ptr.CTS = wxString::Format("%03.0f", BC);
                ptr.SMG = wxString::Format("%5.1f", VBG);
                ptr.set = wxString::Format("%03.0f", dir);
                ptr.rate = wxString::Format("%5.1f", spd);
                ptr.icon_name = wxT("Triangle");
                tr.m_positionslist.push_back(ptr);

                DistanceBearingMercator_Plugin(
                    latN[wpn + 1], lonN[wpn + 1], lati, loni, &myBrng,
                    &waypointDistance);  // how far to the next waypoint?

                latF = lati;
                lonF = loni;

                // Find out if any space for more EP
                timeToWaypoint = waypointDistance / VBG;
                fractpart = modf(timeToWaypoint, &intpart);
                numEP = intpart;

                if (numEP == 0) {
                  timeToRun = 1 - timeToWaypoint;
                  dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
                  ptrDist = timeToWaypoint * VBG;

                } else {
                  for (int z = 0; z <= numEP; z++) {
                    ptrDist = VBG;

                    PositionBearingDistanceMercator_Plugin(
                        latF, lonF, myBrng, VBG, &lati,
                        &loni);  // first waypoint of the leg

                    dtCurrent = dtCurrent.Add(HourSpan);

                    m_bGrib = GetGribSpdDir(dtCurrent, lati, loni, spd, dir);

                    if (!m_bGrib) {
                      wxMessageBox(
                          _("Route start date is not compatible with this Grib "
                            "\n Or "
                            "Grib is not available for part of the route"));
                      return;
                    }
                    CTSWithCurrent(
                        myBrng, VBG, dir, spd, BC,
                        speed);  // VBG = velocity of boat over ground

                    epNumber++;
                    epName = "EP" + wxString::Format(wxT("%i"), epNumber);
                    PlugIn_Waypoint* epPoint = new PlugIn_Waypoint(
                        lati, loni, wxT("Triangle"), epName);
                    epPoint->m_IconName = wxT("Triangle");
                    epPoint->m_GUID = wxString::Format(
                        "%i", (int)GetRandomNumber(1, 4000000));
                    newRoute->pWaypointList->Append(
                        epPoint);  // for the OpenCPN display route

                    // print mid points for the GPX file
                    my_point.lat = wxString::Format(wxT("%f"), lati);
                    my_point.lon = wxString::Format(wxT("%f"), loni);
                    my_point.routepoint = 0;
                    my_point.wpt_num =
                        "EP" + wxString::Format("%i", epNumber);
                    my_point.name =
                        "EP" + wxString::Format("%i", epNumber);
                    my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                    my_points.push_back(my_point);

                    // print EP for the config file
                    // ptrDist = VBG;
                    tdist += ptrDist;
                    ptr.name = epName;
                    ptr.lat = wxString::Format("%8.4f", lati);
                    ptr.lon = wxString::Format("%8.4f", loni);
                    ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
                    ptr.guid = epPoint->m_GUID;
                    ptr.distTo = wxString::Format("%.1f", ptrDist);
                    ptr.brgTo = wxString::Format("%03.0f", myBrng);
                    ptr.CTS = wxString::Format("%03.0f", BC);
                    ptr.SMG = wxString::Format("%5.1f", VBG);
                    ptr.set = wxString::Format("%03.0f", dir);
                    ptr.rate = wxString::Format("%5.1f", spd);
                    ptr.icon_name = wxT("Triangle");
                    tr.m_positionslist.push_back(ptr);

                    DistanceBearingMercator_Plugin(
                        latN[wpn + 1], lonN[wpn + 1], lati, loni, &myBrng,
                        &waypointDistance);  // how far to the next waypoint?
                    timeToWaypoint = waypointDistance / VBG;

                    if (timeToWaypoint < 1) {
                      z = numEP + 1;  // to stop the next EP being made

                      timeToRun = 1 - timeToWaypoint;
                      dtCurrent = AdvanceSeconds(dtCurrent, timeToWaypoint);
                      ptrDist = timeToWaypoint * VBG;
                    }

                    latF = lati;
                    lonF = loni;
                  }
                }
              }
            }  // Finished the waypoints after zero
          }    // Finished all waypoints

          // print the last routepoint
          PlugIn_Waypoint* endPoint = new PlugIn_Waypoint(
              latN[wpn], lonN[wpn], wxT("Circle"), waypointName[wpn]);
          endPoint->m_MarkName = waypointName[wpn];
          endPoint->m_MarkDescription =
              dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
          endPoint->m_GUID =
              wxString::Format("%i", (int)GetRandomNumber(1, 4000000));
          newRoute->pWaypointList->Append(endPoint);

          //
          // print the last my_point for writing GPX
          //

          my_point.lat = wxString::Format(wxT("%f"), latN[wpn]);
          my_point.lon = wxString::Format(wxT("%f"), lonN[wpn]);
          my_point.routepoint = 1;
          my_point.wpt_num = waypointName[wpn].mb_str();
          my_point.name = waypointName[wpn].mb_str();
          my_point.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
          my_points.push_back(my_point);

          // print the last waypoint detail for the TidalRoute
          tr.EndTime = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");

          trTime = dtCurrent - dtStart;
          trTimeHours = (double)trTime.GetMinutes() / 60;
          tr.Time = wxString::Format(_("%.1f"), trTimeHours);

          ptrDist = waypointDistance;
          tdist += ptrDist;

          tr.Distance = wxString::Format(_("%.1f"), tdist);

          ptr.name = waypointName[wpn].mb_str();
          ptr.lat = wxString::Format("%8.4f", latN[n]);
          ptr.lon = wxString::Format("%8.4f", lonN[n]);
          ptr.time = dtCurrent.Format(" %a %d-%b-%Y  %H:%M");
          ptr.guid = endPoint->m_GUID;
          ptr.set = "----";
          ptr.rate = "----";
          ptr.CTS = "----";
          ptr.SMG = wxString::Format("%5.1f", VBG);
          ptr.distTo = wxString::Format("%.1f", ptrDist);
          ptr.brgTo = wxString::Format("%03.0f", myBrng);
          ptr.icon_name = wxT("Circle");

          tr.m_positionslist.push_back(ptr);
          tr.End = waypointName[wpn].mb_str();
          tr.Type = wxT("ETA");
          m_TidalRoutes.push_back(tr);

          // AddPlugInRoute(newRoute); // add the route to OpenCPN routes
          // and display the route on the chart

          SaveXML(m_default_configuration_path);  // add the route and extra
                                                  // detail (times, CTS etc)
                                                  // to the configuration file

          m_ConfigurationDialog.m_lRoutes->Append(tr.Name);
          m_ConfigurationDialog.Refresh();
          GetParent()->Refresh();

          for (std::vector<Position>::iterator itOut = my_points.begin();
               itOut != my_points.end(); itOut++) {

            double value, value1;
            if (!(*itOut).lat.ToDouble(&value)) { /* error! */
            }
            lati = value;
            if (!(*itOut).lon.ToDouble(&value1)) { /* error! */
            }
            loni = value1;

            if ((*itOut).routepoint == 1) {
              if (write_file) {
                Addpoint(Route, wxString::Format("%f", lati),
                         wxString::Format("%f", loni), (*itOut).name,
                         "Diamond", "WPT");
              }
            } else {
              if ((*itOut).routepoint == 0) {
                if (write_file) {
                  Addpoint(Route, wxString::Format("%f", lati),
                           wxString::Format(wxT("%f"), loni), (*itOut).name,
                          "Triangle", "WPT");
                }
              }
            }
          }

          my_points.clear();
          break;
        }

        default: {  // Note the colon, not a semicolon
          cout << "Error, bad input, quitting\n";
          break;
        }
      }

      if (write_file) {
        TiXmlElement* Extensions = new TiXmlElement("extensions");

        TiXmlElement* StartN = new TiXmlElement("opencpn:start");
        TiXmlText* text5 = new TiXmlText(waypointName[0].ToUTF8());
        Extensions->LinkEndChild(StartN);
        StartN->LinkEndChild(text5);

        TiXmlElement* EndN = new TiXmlElement("opencpn:end");
        TiXmlText* text6 = new TiXmlText(waypointName[n].ToUTF8());
        Extensions->LinkEndChild(EndN);
        EndN->LinkEndChild(text6);

        Route->LinkEndChild(Extensions);

        root->LinkEndChild(Route);

        wxCharBuffer buffer = s.ToUTF8();
        if (dbg) std::cout << buffer.data() << std::endl;
        doc.SaveFile(buffer.data());
      }

      // end of if no error occured

      if (error_occured == true) {
        wxLogMessage(_("Error in calculation. Please check input!"));
        wxMessageBox(_("Error in calculation. Please check input!"));
      }
    }
    GetParent()->Refresh();
    pPlugIn->m_potidalrouteDialog->Show();
  }
  wxMessageBox(_("ETA Routes have been calculated!"));
}

bool otidalrouteUIDialog::OpenXML(bool gotGPXFile) {
  Position my_position;

  if (!gotGPXFile) {
    std::vector<std::unique_ptr<PlugIn_Route_Ex>> routes;
    auto uids = GetRouteGUIDArray();
    for (size_t i = 0; i < uids.size(); i++) {
      routes.push_back(std::move(GetRouteEx_Plugin(uids[i])));
    }

    GetRouteDialog RouteDialog(this, -1, _("Select the route to plan"),
                               wxPoint(200, 200), wxSize(300, 200),
                               wxRESIZE_BORDER);

    RouteDialog.dialogText->InsertColumn(0, "", 0, wxLIST_AUTOSIZE);
    RouteDialog.dialogText->SetColumnWidth(0, 290);
    RouteDialog.dialogText->InsertColumn(1, "", 0, wxLIST_AUTOSIZE);
    RouteDialog.dialogText->SetColumnWidth(1, 0);
    RouteDialog.dialogText->DeleteAllItems();

    int in = 0;
    std::vector<std::string> names;
    for (const auto& r : routes) names.push_back(r->m_NameString.ToStdString());

    for (size_t n = 0; n < names.size(); n++) {
      wxString routeName = names[in];

      RouteDialog.dialogText->InsertItem(in, "", -1);
      RouteDialog.dialogText->SetItem(in, 0, routeName);
      in++;
    }

    long si = -1;
    long itemIndex = -1;
    // int f = 0;

    wxListItem row_info;
    wxString cell_contents_string = wxEmptyString;
    bool foundRoute = false;

    if (RouteDialog.ShowModal() != wxID_OK) {
      return false;
    } else {
      for (;;) {
        itemIndex = RouteDialog.dialogText->GetNextItem(
            itemIndex, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

        if (itemIndex == -1) break;

        // Got the selected item index
        if (RouteDialog.dialogText->IsSelected(itemIndex)) {
          si = itemIndex;
          foundRoute = true;
          break;
        }
      }

      if (foundRoute) {
        // Set what row it is (m_itemId is a member of
        // the regular wxListCtrl class)
        row_info.m_itemId = si;
        // Set what column of that row we want to query
        // for information.
        row_info.m_col = 0;
        // Set text mask
        row_info.m_mask = wxLIST_MASK_TEXT;

        // Get the info and store it in row_info
        // variable.
        RouteDialog.dialogText->GetItem(row_info);
        // Extract the text out that cell
        cell_contents_string = row_info.m_text;
        Position initPoint;
        nextRoutePointIndex = 0;
        bool foundRoute = false;

        for (size_t i = 0; i < uids.size(); i++) {
          thisRoute = GetRouteEx_Plugin(uids[i]);

          if (thisRoute->m_NameString == cell_contents_string) {
            foundRoute = true;
            break;
          }
        }
        if (foundRoute) {
          m_bUsingFollow = true;
          countRoutePoints = thisRoute->pWaypointList->size();
          myList = thisRoute->pWaypointList;

          PlugIn_Waypoint_Ex* myWaypoint;
          theWaypoints.clear();

          wxPlugin_WaypointExListNode* pwpnode = myList->GetFirst();
          while (pwpnode) {
            myWaypoint = pwpnode->GetData();

            theWaypoints.push_back(myWaypoint);

            pwpnode = pwpnode->GetNext();
          }
          my_positions.clear();

          for (size_t n = 0; n < theWaypoints.size(); n++) {
            my_position.name = theWaypoints[n]->m_MarkName;
            wxString dlat = wxString::Format("%f", theWaypoints[n]->m_lat);
            wxString dlon = wxString::Format("%f", theWaypoints[n]->m_lon);
            my_position.lat = dlat;
            my_position.lon = dlon;

            my_positions.push_back(my_position);
          }
          gotMyGPXFile = true;
          return true;

        } else {
          wxMessageBox("Route not found");
          gotMyGPXFile = false;
          return false;
        }
      }
    }
  }
}

void otidalrouteUIDialog::Addpoint(TiXmlElement* Route, wxString ptlat,
                                   wxString ptlon, wxString ptname,
                                   wxString ptsym, wxString pttype) {
  // add point
  TiXmlElement* RoutePoint = new TiXmlElement("rtept");
  RoutePoint->SetAttribute("lat", ptlat.mb_str());
  RoutePoint->SetAttribute("lon", ptlon.mb_str());

  TiXmlElement* Name = new TiXmlElement("name");
  TiXmlText* text = new TiXmlText(ptname.mb_str());
  RoutePoint->LinkEndChild(Name);
  Name->LinkEndChild(text);

  TiXmlElement* Symbol = new TiXmlElement("sym");
  TiXmlText* text1 = new TiXmlText(ptsym.mb_str());
  RoutePoint->LinkEndChild(Symbol);
  Symbol->LinkEndChild(text1);

  TiXmlElement* Type = new TiXmlElement("type");
  TiXmlText* text2 = new TiXmlText(pttype.mb_str());
  RoutePoint->LinkEndChild(Type);
  Type->LinkEndChild(text2);

  Route->LinkEndChild(RoutePoint);
  // done adding point
}

bool otidalrouteUIDialog::GetGribSpdDir(wxDateTime dt, double lat, double lon,
                                        double& spd, double& dir) {
  wxDateTime dtime = dt;

  pPlugIn->m_grib_lat = lat;
  pPlugIn->m_grib_lon = lon;
  RequestGrib(dtime);
  if (pPlugIn->m_bGribValid) {
    spd = pPlugIn->m_tr_spd;
    dir = pPlugIn->m_tr_dir;
    return true;
  } else {
    return false;
  }
}

int otidalrouteUIDialog::GetRandomNumber(int range_min, int range_max) {
  long u = (long)wxRound(
      ((double)rand() / ((double)(RAND_MAX) + 1) * (range_max - range_min)) +
      range_min);
  return (int)u;
}

/* C   - Sea Current Direction over ground
VC  - Velocity of Current

provisions to compute boat movement over ground

BG  - boat direction over ground
BGV - boat speed over ground (gps velocity)
*/

void otidalrouteUIDialog::OverGround(double B, double VB, double C, double VC,
                                     double& BG, double& VBG) {
  if (VC == 0) {  // short-cut if no currents
    BG = B, VBG = VB;
    return;
  }

  double Cx = VC * cos(deg2rad(C));
  double Cy = VC * sin(deg2rad(C));
  double BGx = VB * cos(deg2rad(B)) + Cx;
  double BGy = VB * sin(deg2rad(B)) + Cy;
  BG = rad2deg(atan2(BGy, BGx));
  VBG = distance(BGx, BGy);
}

double otidalrouteUIDialog::AttributeDouble(TiXmlElement* e, const char* name,
                                            double def) {
  const char* attr = e->Attribute(name);
  if (!attr) return def;
  char* end;
  double d = strtod(attr, &end);
  if (end == attr) return def;
  return d;
}

bool otidalrouteUIDialog::OpenXML(wxString filename, bool reportfailure) {
  Position pos;
  list<Position> m_pos;

  TiXmlDocument doc;
  wxString error;

  wxFileName fn(filename);

  SetTitle(_("oTidalRoute"));

  wxProgressDialog* progressdialog = NULL;
  wxDateTime start = wxDateTime::UNow();

  if (!doc.LoadFile(filename.mb_str()))
    FAIL(_("Failed to load file."));
  else {
    TiXmlHandle root(doc.RootElement());

    if (strcmp(root.Element()->Value(), "OpenCPNotidalrouteConfiguration"))
      FAIL(_("Invalid xml file"));

    Positions.clear();

    int count = 0;
    for (TiXmlElement* e = root.FirstChild().Element(); e;
         e = e->NextSiblingElement())
      count++;

    int i = 0;
    for (TiXmlElement* e = root.FirstChild().Element(); e;
         e = e->NextSiblingElement(), i++) {
      if (progressdialog) {
        if (!progressdialog->Update(i)) return true;
      } else {
        wxDateTime now = wxDateTime::UNow();
        /* if it's going to take more than a half second, show a progress
         * dialog
         */
        if ((now - start).GetMilliseconds() > 250 && i < count / 2) {
          progressdialog = new wxProgressDialog(
              _("Load"), _("otidalroute"), count, this,
              wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
        }
      }

      if (!strcmp(e->Value(), "Position")) {
        wxString name = wxString::FromUTF8(e->Attribute("Name"));
        double lat = AttributeDouble(e, "Latitude", NAN);
        double lon = AttributeDouble(e, "Longitude", NAN);

        for (std::vector<RouteMapPosition>::iterator it = Positions.begin();
             it != Positions.end(); it++) {
          if ((*it).Name == name) {
            static bool warnonce = true;
            if (warnonce) {
              warnonce = false;
              wxMessageDialog mdlg(
                  this,
                  _("File contains duplicate position name, discarding\n"),
                  _("otidalroute"), wxOK | wxICON_WARNING);
              mdlg.ShowModal();
            }

            goto skipadd;
          }
        }

      skipadd:;

      }

      else

          if (!strcmp(e->Value(), "TidalRoute")) {
        TidalRoute tr;
        m_pos.clear();
        wxString nm = wxString::FromUTF8(e->Attribute("Name"));
        wxString tp = wxString::FromUTF8(e->Attribute("Type"));
        wxString st = wxString::FromUTF8(e->Attribute("Start"));
        wxString en = wxString::FromUTF8(e->Attribute("End"));
        wxString tm = wxString::FromUTF8(e->Attribute("Time"));
        wxString tms = wxString::FromUTF8(e->Attribute("StartTime"));
        wxString tme = wxString::FromUTF8(e->Attribute("EndTime"));
        wxString dn = wxString::FromUTF8(e->Attribute("Distance"));
        tr.Name = nm;
        tr.Type = tp;
        tr.Start = st;
        tr.End = en;
        tr.Time = tm;
        tr.StartTime = tms;
        tr.EndTime = tme;
        tr.Distance = dn;

        for (TiXmlElement* f = e->FirstChildElement(); f;
             f = f->NextSiblingElement()) {
          if (!strcmp(f->Value(), "Route")) {
            pos.name = wxString::FromUTF8(f->Attribute("Waypoint"));
            pos.lat = wxString::FromUTF8(f->Attribute("Latitude"));
            pos.lon = wxString::FromUTF8(f->Attribute("Longitude"));
            pos.time = wxString::FromUTF8(f->Attribute("ETD"));
            pos.guid = wxString::FromUTF8(f->Attribute("GUID"));
            pos.CTS = wxString::FromUTF8(f->Attribute("CTS"));
            pos.SMG = wxString::FromUTF8(f->Attribute("SMG"));
            pos.distTo = wxString::FromUTF8(f->Attribute("Dist"));
            pos.brgTo = wxString::FromUTF8(f->Attribute("Brng"));
            pos.set = wxString::FromUTF8(f->Attribute("Set"));
            pos.rate = wxString::FromUTF8(f->Attribute("Rate"));
            pos.icon_name = wxString::FromUTF8(f->Attribute("icon_name"));

            m_pos.push_back(pos);
          }
        }
        tr.m_positionslist = m_pos;
        AddTidalRoute(tr);
      }

      else
        FAIL(_("Unrecognized xml node"));
    }
  }

  delete progressdialog;
  return true;
failed:

  if (reportfailure) {
    wxMessageDialog mdlg(this, error, _("otidalroute"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
  }
  return false;
}

void otidalrouteUIDialog::SaveXML(wxString filename) {
  TiXmlDocument doc;
  TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "utf-8", "");
  doc.LinkEndChild(decl);

  TiXmlElement* root = new TiXmlElement("OpenCPNotidalrouteConfiguration");
  doc.LinkEndChild(root);

  char version[24];
  sprintf(version, "%d.%d", PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR);
  root->SetAttribute("version", version);
  root->SetAttribute("creator", "Opencpn otidalroute plugin");

  for (std::vector<RouteMapPosition>::iterator it = Positions.begin();
       it != Positions.end(); it++) {
    TiXmlElement* c = new TiXmlElement("Position");

    c->SetAttribute("Name", (*it).Name.mb_str());
    c->SetAttribute("Latitude",
                    wxString::Format("%.5f", (*it).lat).mb_str());
    c->SetAttribute("Longitude",
                    wxString::Format("%.5f", (*it).lon).mb_str());

    root->LinkEndChild(c);
  }
  for (std::list<TidalRoute>::iterator it = m_TidalRoutes.begin();
       it != m_TidalRoutes.end(); it++) {
    TiXmlElement* TidalRoute = new TiXmlElement("TidalRoute");
    TidalRoute->SetAttribute("Name", (*it).Name);
    TidalRoute->SetAttribute("Type", (*it).Type);
    TidalRoute->SetAttribute("Start", (*it).Start);
    TidalRoute->SetAttribute("End", (*it).End);
    TidalRoute->SetAttribute("Time", (*it).Time);
    TidalRoute->SetAttribute("StartTime", (*it).StartTime);
    TidalRoute->SetAttribute("EndTime", (*it).EndTime);
    TidalRoute->SetAttribute("Distance", (*it).Distance);

    for (std::list<Position>::iterator itp = (*it).m_positionslist.begin();
         itp != (*it).m_positionslist.end(); itp++) {
      TiXmlElement* cp = new TiXmlElement("Route");

      cp->SetAttribute("Waypoint", (*itp).name);
      cp->SetAttribute("Latitude", (*itp).lat);
      cp->SetAttribute("Longitude", (*itp).lon);
      cp->SetAttribute("ETD", (*itp).time);
      cp->SetAttribute("GUID", (*itp).guid);
      cp->SetAttribute("CTS", (*itp).CTS);
      cp->SetAttribute("SMG", (*itp).SMG);
      cp->SetAttribute("Dist", (*itp).distTo);
      cp->SetAttribute("Brng", (*itp).brgTo);
      cp->SetAttribute("Set", (*itp).set);
      cp->SetAttribute("Rate", (*itp).rate);
      cp->SetAttribute("icon_name", (*itp).icon_name);

      TidalRoute->LinkEndChild(cp);
    }

    root->LinkEndChild(TidalRoute);
  }
  /*
  for (std::list<TidalRoute>::iterator it = m_TidalRoutes.begin();
          it != m_TidalRoutes.end(); it++) {
          TiXmlElement *config = new TiXmlElement("Configuration");

          config->SetAttribute("Start", (*it).Start.mb_str());
          config->SetAttribute("End", (*it).End.mb_str());

          root->LinkEndChild(config);
  }
  */
  if (!doc.SaveFile(filename.mb_str())) {
    wxMessageDialog mdlg(this, _("Failed to save xml file: ") + filename,
                         _("otidalroute"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
  }
};

wxDateTime otidalrouteUIDialog::AdvanceSeconds(wxDateTime currentTime,
                                               double HoursToAdvance) {
  int secondsToAdvance = HoursToAdvance * 3600;
  wxTimeSpan SecondsSpan = wxTimeSpan::Seconds(secondsToAdvance);  // One hour
  wxDateTime advancedTime = currentTime.Add(SecondsSpan);
  return advancedTime;
}

GetRouteDialog::GetRouteDialog(wxWindow* parent, wxWindowID id,
                               const wxString& title, const wxPoint& position,
                               const wxSize& size, long style)
    : wxDialog(parent, id, title, position, size, style) {
  wxPoint p;
  wxSize sz;

  sz.SetWidth(size.GetWidth() - 20);
  sz.SetHeight(size.GetHeight() - 70);

  p.x = 6;
  p.y = 2;

  dialogText = new wxListView(this, wxID_ANY, p, sz,
                              wxLC_NO_HEADER | wxLC_REPORT | wxLC_SINGLE_SEL,
                              wxDefaultValidator, wxT(""));
  wxFont pVLFont(wxFontInfo(12).FaceName("Arial"));
  dialogText->SetFont(pVLFont);

  auto sizerlist = new wxBoxSizer(wxVERTICAL);
  sizerlist->Add(-1, -1, 100, wxEXPAND);
  sizerlist->Add(dialogText);

  auto sizer = new wxBoxSizer(wxHORIZONTAL);
  auto flags = wxSizerFlags().Right().Bottom().Border();
  sizer->Add(1, 1, 100, wxEXPAND);  // Expanding spacer
  auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
  sizer->Add(cancel, flags);
  auto m_ok = new wxButton(this, wxID_OK, _("OK"));
  m_ok->Enable(true);
  sizer->Add(m_ok, flags);
  sizerlist->Add(sizer);
  SetSizer(sizerlist);
  Fit();
  SetFocus();
};

enum { WIND, CURRENT };
