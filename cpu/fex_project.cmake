# Included by FEX's project() (CMAKE_PROJECT_FEX_INCLUDE, see build.sh): FEX's CMake files expect to
# be the top-level project, so libbbcpu is added to FEX's build once all of its targets exist.
# EVAL expands the path now; deferred arguments are expanded when the call runs.
cmake_language(EVAL CODE "cmake_language(DEFER DIRECTORY \"${CMAKE_SOURCE_DIR}\" CALL include \"${CMAKE_CURRENT_LIST_DIR}/bbcpu.cmake\")")

# Bionic (Android NDK) build: FEX's CMake knows Linux and Windows only. Android is Linux on the
# kernel side; the toolchain file's ANDROID variable stays set for anything that needs to tell.
if(ANDROID AND CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(CMAKE_SYSTEM_NAME Linux)
endif()
