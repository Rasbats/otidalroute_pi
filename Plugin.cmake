# ~~~
# Summary:      Local, non-generic plugin setup
# Copyright (c) 2020-2021 Mike Rossiter
# License:      GPLv3+
# ~~~

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.


# -------- Options ----------

set(OCPN_TEST_REPO
    "mike-rossiter/otidalroute-alpha"
    CACHE STRING "Default repository for untagged builds"
)
set(OCPN_BETA_REPO
    "mike-rossiter/otidalroute-beta"
    CACHE STRING
    "Default repository for tagged builds matching 'beta'"
)
set(OCPN_RELEASE_REPO
    "mike-rossiter/otidalroute-prod"
    CACHE STRING
    "Default repository for tagged builds not matching 'beta'"
)

#
#
# -------  Plugin setup --------
#
set(PKG_NAME otidalroute_pi)
set(PKG_VERSION  1.2.0)
set(PKG_PRERELEASE "")  # Empty, or a tag like 'beta'

set(DISPLAY_NAME otidalroute)    # Dialogs, installer artifacts, ...
set(PLUGIN_API_NAME otidalroute) # As of GetCommonName() in plugin API
set(PKG_SUMMARY "Plan a route with tidal current grib data")
set(PKG_DESCRIPTION [=[
Use tidal current grib files to plan routes. Show tidal current arrows on the chart. Display tidal current data in a table.
]=])

set(PKG_AUTHOR "Mike Rossiter")
set(PKG_IS_OPEN_SOURCE "yes")
set(PKG_HOMEPAGE https://github.com/Rasbats/otidalroute_pi)
set(PKG_INFO_URL https://opencpn.org/OpenCPN/plugins/otidalroute.html)

set(SRC
        src/AboutDialog.cpp
        src/AboutDialog.h
        src/bbox.cpp
        src/bbox.h
        src/GribRecord.cpp
        src/GribRecord.h
        src/GribRecordSet.h
        src/otidalroute_pi.h
        src/otidalroute_pi.cpp
        src/otidalrouteOverlayFactory.cpp
        src/otidalrouteOverlayFactory.h
        src/otidalrouteUIDialogBase.cpp
        src/otidalrouteUIDialogBase.h
        src/otidalrouteUIDialog.cpp
        src/otidalrouteUIDialog.h
        src/icons.h
        src/icons.cpp
        src/tcmgr.cpp
        src/tcmgr.h
        src/NavFunc.cpp
        src/NavFunc.h
        src/routeprop.cpp
        src/routeprop.h
        src/tableroutes.cpp
        src/tableroutes.h

)

set(PKG_API_LIB api-20)  #  A dir in opencpn-libs/ e. g., api-17 or api-16

macro(late_init)
  # Perform initialization after the PACKAGE_NAME library, compilers
  # and ocpn::api is available.
endmacro ()

macro(add_plugin_libraries)
  # Add libraries required by this plugin
  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/tinyxml")
  target_link_libraries(${PACKAGE_NAME} ocpn::tinyxml)

  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/wxJSON")
  target_link_libraries(${PACKAGE_NAME} ocpn::wxjson)

  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/plugin_dc")
  target_link_libraries(${PACKAGE_NAME} ocpn::plugin-dc)

  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/jsoncpp")
  target_link_libraries(${PACKAGE_NAME} ocpn::jsoncpp)

  # The wxsvg library enables SVG overall in the plugin
  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/wxsvg")
  target_link_libraries(${PACKAGE_NAME} ocpn::wxsvg)
endmacro ()
