# FindLibNFC.cmake
#
# Find the libnfc library used for PN53x direct-mode NFC reader support.
#
# Defines:
#   LIBNFC_FOUND
#   LIBNFC_INCLUDE_DIR
#   LIBNFC_LIBRARY

if(LIBNFC_INCLUDE_DIR AND LIBNFC_LIBRARY)
  set(LIBNFC_FIND_QUIETLY TRUE)
endif()

include(FindPackageHandleStandardArgs)

find_path(LIBNFC_INCLUDE_DIR
  NAMES nfc/nfc.h)

find_library(LIBNFC_LIBRARY
  NAMES nfc libnfc)

find_package_handle_standard_args(LibNFC
  REQUIRED_VARS LIBNFC_LIBRARY LIBNFC_INCLUDE_DIR)

mark_as_advanced(LIBNFC_INCLUDE_DIR LIBNFC_LIBRARY)
