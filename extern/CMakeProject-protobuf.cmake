set(protobuf_INSTALL OFF CACHE INTERNAL "Disable protobuf install targets")
set(protobuf_BUILD_TESTS OFF CACHE INTERNAL "Disable protobuf tests")
set(protobuf_BUILD_CONFORMANCE OFF CACHE INTERNAL "Disable protobuf conformance")
set(protobuf_BUILD_EXAMPLES OFF CACHE INTERNAL "Disable protobuf examples")
set(protobuf_BUILD_PROTOBUF_BINARIES ON CACHE INTERNAL "Build protobuf runtime")
set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE INTERNAL "Skip protoc in main ITGmania build")
set(protobuf_BUILD_LIBPROTOC OFF CACHE INTERNAL "Skip libprotoc")
set(protobuf_BUILD_LIBUPB OFF CACHE INTERNAL "Skip upb runtime")
set(protobuf_BUILD_SHARED_LIBS OFF CACHE INTERNAL "Use static protobuf libs")
set(protobuf_WITH_ZLIB OFF CACHE INTERNAL "Disable zlib in protobuf runtime")
set(ABSL_PROPAGATE_CXX_STD ON CACHE INTERNAL "Use parent C++ standard for Abseil")
# Protobuf's pinned dependency metadata selects Abseil when no installed
# package is available. No protoc binary is built by the engine build.
set(CMAKE_CXX_STANDARD 17)

add_subdirectory("profile-protobuf" EXCLUDE_FROM_ALL)

# Treat protobuf headers as external when compiling consumers. This keeps
# MSVC's intentional alignment/padding and member-shadowing diagnostics in
# dependency headers from being reported under the engine's /W4 settings.
# Diagnostics in the engine and in protobuf's own compilation stay enabled.
set_property(TARGET libprotobuf APPEND PROPERTY
  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
  "$<TARGET_PROPERTY:libprotobuf,INTERFACE_INCLUDE_DIRECTORIES>")

# RepeatedString's validation lambda returns for both Utf8Type values, but
# MSVC reports C4715 for its template instantiations because the exhaustive
# switch has no trailing return. Limit the suppression to that upstream file.
set_property(SOURCE
  "${CMAKE_CURRENT_SOURCE_DIR}/profile-protobuf/src/google/protobuf/generated_message_tctable_lite.cc"
  TARGET_DIRECTORY libprotobuf APPEND PROPERTY COMPILE_OPTIONS
  "$<$<CXX_COMPILER_ID:MSVC>:/wd4715>")

set_property(TARGET "libprotobuf" PROPERTY FOLDER "External Libraries")
if(TARGET "libprotobuf-lite")
  set_property(TARGET "libprotobuf-lite" PROPERTY FOLDER "External Libraries")
endif()
