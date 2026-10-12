cmake_minimum_required(VERSION 3.22)
project(Q2_Embedded_Apps_Tests C)
if(NOT EXISTS "${Q2_KERNEL_DIR}/CMakeLists.txt" OR NOT EXISTS "${Q2_APP_CONFIG}" OR NOT Q2_APP_DIR)
  message(FATAL_ERROR "Supply explicit Q2_KERNEL_DIR, Q2_APP_DIR and Q2_APP_CONFIG")
endif()
set(Q2_WITH_LVGL ON CACHE BOOL "" FORCE)
set(Q2_WITH_STORAGE OFF CACHE BOOL "" FORCE)
set(Q2_ENABLE_CJK_FONT OFF CACHE BOOL "" FORCE)
set(Q2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${Q2_KERNEL_DIR}" kernel)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(bundle_dir "${CMAKE_CURRENT_BINARY_DIR}/factory")
file(MAKE_DIRECTORY "${bundle_dir}")
file(GLOB_RECURSE app_inputs CONFIGURE_DEPENDS "${Q2_APP_DIR}/apps/*")
add_custom_command(OUTPUT "${bundle_dir}/q2_factory_bundle.c" "${bundle_dir}/q2_factory_bundle.h"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/embed_factory_apps.py"
    --app-dir "${Q2_APP_DIR}" --config "${Q2_APP_CONFIG}" --output "${bundle_dir}"
  DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../scripts/embed_factory_apps.py" "${Q2_APP_CONFIG}"
    "${Q2_APP_DIR}/factory.json" "${Q2_APP_DIR}/scripts/q2app.py" ${app_inputs}
  VERBATIM)
add_executable(q2_embedded_apps_tests test_embedded_apps.c ../main/embedded_apps.c
  "${bundle_dir}/q2_factory_bundle.c" ../../common/log_console.c)
target_include_directories(q2_embedded_apps_tests PRIVATE ../main ../../common "${bundle_dir}")
target_link_libraries(q2_embedded_apps_tests PRIVATE q2_runtime q2_lvgl)
target_compile_options(q2_embedded_apps_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
enable_testing()
add_test(NAME embedded_factory_navigation COMMAND q2_embedded_apps_tests)

# Separate output directories allow pixel-for-pixel comparison without racing screenshot writes.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/partial" "${CMAKE_CURRENT_BINARY_DIR}/full")
set_tests_properties(embedded_factory_navigation PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/partial")
add_test(NAME embedded_factory_full_frame COMMAND q2_embedded_apps_tests full)
set_tests_properties(embedded_factory_full_frame PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/full")
foreach(scene homepage settings settings-scrolled)
  add_test(NAME embedded_factory_same_pixels_${scene} COMMAND "${CMAKE_COMMAND}" -E compare_files
    "${CMAKE_CURRENT_BINARY_DIR}/partial/${scene}.ppm" "${CMAKE_CURRENT_BINARY_DIR}/full/${scene}.ppm")
  set_tests_properties(embedded_factory_same_pixels_${scene} PROPERTIES
    DEPENDS "embedded_factory_navigation;embedded_factory_full_frame")
endforeach()
