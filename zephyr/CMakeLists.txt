cmake_minimum_required(VERSION 3.20.0)
set(BUILD_VERSION "v3.7.0" CACHE STRING "Zephyr build version")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(goose_zephyr C)

# The C that `goose --m32 -o app.c program.goose` wrote: a single file of
# plain libc (GS_FREESTANDING). goose_zephyr.h, included ahead of it, names
# its entry point for harness.c and supplies the POSIX time declarations
# newlib keeps back.
set(GOOSE_APP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/app.c" CACHE FILEPATH "Generated Goose C source file")
target_sources(app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/harness.c"
  "${GOOSE_APP_SRC}"
)
target_include_directories(app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_source_files_properties("${GOOSE_APP_SRC}" PROPERTIES
  COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/goose_zephyr.h"
)
target_compile_options(app PRIVATE
  -Wno-unused-function
  -Wno-unused-variable
  -Wno-unused-but-set-variable
  -Wno-parentheses
)
