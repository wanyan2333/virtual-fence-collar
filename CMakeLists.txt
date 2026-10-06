cmake_minimum_required(VERSION 3.16)
project(virtual_fence_collar C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# ---- core: pure, hardware-independent logic (no I/O, no malloc) -----------
add_library(core STATIC
  core/nmea.c
  core/geofence.c
  core/collar_sm.c
  core/power.c
  core/telemetry.c)
target_include_directories(core PUBLIC core)
if(NOT WIN32 OR MINGW)
  target_link_libraries(core PUBLIC m)
endif()

# ---- host simulator: core + app + host HAL --------------------------------
add_executable(collar_sim
  app/collar_app.c
  app/main_host.c
  hal/host/hal_host.c)
target_include_directories(collar_sim PRIVATE app hal hal/host)
target_link_libraries(collar_sim PRIVATE core)

# ---- unit tests (Unity) ----------------------------------------------------
enable_testing()
add_library(unity STATIC third_party/unity/unity.c)
target_include_directories(unity PUBLIC third_party/unity)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)
# Third-party code: don't hold it to our warning flags.
target_compile_options(unity PRIVATE -w)

foreach(name nmea geofence collar_sm power telemetry)
  add_executable(test_${name} tests/unit/test_${name}.c)
  target_link_libraries(test_${name} PRIVATE core unity)
  add_test(NAME test_${name} COMMAND test_${name})
endforeach()
