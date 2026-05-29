# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/tmp/workspace/CrashCringle12/itgmania/extern/libjpeg-turbo")
  file(MAKE_DIRECTORY "/tmp/workspace/CrashCringle12/itgmania/extern/libjpeg-turbo")
endif()
file(MAKE_DIRECTORY
  "/tmp/workspace/CrashCringle12/itgmania/build/extern/libjpeg_turbo_project-prefix/src/libjpeg_turbo_project-build"
  "/tmp/workspace/CrashCringle12/itgmania/build/libjpeg-turbo-install"
  "/tmp/workspace/CrashCringle12/itgmania/build/extern/libjpeg_turbo_project-prefix/tmp"
  "/tmp/workspace/CrashCringle12/itgmania/build/extern/libjpeg_turbo_project-prefix/src/libjpeg_turbo_project-stamp"
  "/tmp/workspace/CrashCringle12/itgmania/build/extern/libjpeg_turbo_project-prefix/src"
  "/tmp/workspace/CrashCringle12/itgmania/build/extern/libjpeg_turbo_project-prefix/src/libjpeg_turbo_project-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/tmp/workspace/CrashCringle12/itgmania/build/extern/libjpeg_turbo_project-prefix/src/libjpeg_turbo_project-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/tmp/workspace/CrashCringle12/itgmania/build/extern/libjpeg_turbo_project-prefix/src/libjpeg_turbo_project-stamp${cfgdir}") # cfgdir has leading slash
endif()
