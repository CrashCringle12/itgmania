# FindPCSCLite.cmake
#
# Find the pcsclite library (PC/SC Lite) used for NFC/smart-card reader support.
#
# Once done this will define:
#   PCSCLITE_FOUND        - system has pcsclite
#   PCSCLITE_INCLUDE_DIR  - pcsclite include directory
#   PCSCLITE_LIBRARY      - pcsclite library

if(PCSCLITE_INCLUDE_DIR AND PCSCLITE_LIBRARY)
  set(PCSCLITE_FIND_QUIETLY TRUE)
endif()

if(NOT WIN32 AND NOT APPLE)
  include(FindPkgConfig)
  pkg_check_modules(PCSCLITE libpcsclite)
  if(PCSCLITE_FOUND)
    set(PCSCLITE_LIBRARY ${PCSCLITE_LIBRARIES}
        CACHE FILEPATH "Path to the pcsclite library")
    set(PCSCLITE_INCLUDE_DIR ${PCSCLITE_INCLUDEDIR}
        CACHE PATH "Path to the pcsclite includes")
  endif()
endif()

if(NOT PCSCLITE_INCLUDE_DIR)
  find_path(PCSCLITE_INCLUDE_DIR
    NAMES winscard.h
    PATH_SUFFIXES PCSC pcsclite)
endif()

if(NOT PCSCLITE_LIBRARY)
  if(WIN32)
    # WinSCard is part of the Windows SDK.
    find_library(PCSCLITE_LIBRARY NAMES WinSCard winscard)
    if(NOT PCSCLITE_LIBRARY)
      # WinSCard.lib ships with every Windows SDK; provide a fallback.
      set(PCSCLITE_LIBRARY "WinSCard.lib")
    endif()
  elseif(APPLE)
    # macOS ships PCSC as a framework.
    find_library(PCSCLITE_LIBRARY NAMES PCSC
                 PATHS /System/Library/Frameworks
                 PATH_SUFFIXES .framework)
  else()
    find_library(PCSCLITE_LIBRARY NAMES pcsclite)
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PCSCLITE
  DEFAULT_MSG
  PCSCLITE_LIBRARY
  PCSCLITE_INCLUDE_DIR)

mark_as_advanced(PCSCLITE_INCLUDE_DIR PCSCLITE_LIBRARY)
