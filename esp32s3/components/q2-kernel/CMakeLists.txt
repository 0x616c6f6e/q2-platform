if(CMAKE_BUILD_EARLY_EXPANSION)
  idf_component_register()
  return()
endif()
# SDK integration belongs to Q2-SoC. Q2-Kernel has no ESP-IDF component metadata.
include("${Q2_KERNEL_DIR}/cmake/sources.cmake")
set(sources ${Q2_CORE_SOURCES} ${Q2_RUNTIME_SOURCES})
# Product registers LVGL, panel controls and factory apps; hardware services remain explicit.
list(APPEND sources components/display/lvgl.c components/display/image_decode.c)
list(TRANSFORM sources PREPEND "${Q2_KERNEL_DIR}/")
list(APPEND sources ${Q2_LUA_SOURCES} "${Q2_KERNEL_DIR}/third_party/cjson/cJSON.c")
file(GLOB_RECURSE lvgl_sources "${Q2_KERNEL_DIR}/third_party/lvgl/src/*.c")
set(font_c "")
if(Q2_ENABLE_CJK_FONT)
  set(font_c "${CMAKE_CURRENT_BINARY_DIR}/chinese_font.c")
  add_custom_command(OUTPUT "${font_c}"
      COMMAND "${PYTHON}" "${Q2_KERNEL_DIR}/scripts/embed_chinese_font.py"
          "${Q2_KERNEL_DIR}/assets/fonts/q2-cjk-regular.ttf" "${font_c}"
      DEPENDS "${Q2_KERNEL_DIR}/scripts/embed_chinese_font.py"
          "${Q2_KERNEL_DIR}/assets/fonts/q2-cjk-regular.ttf" "${Q2_KERNEL_DIR}/assets/fonts/manifest.json"
      VERBATIM)
endif()
idf_component_register(SRCS ${sources} ${lvgl_sources} ${font_c}
    INCLUDE_DIRS "${Q2_KERNEL_DIR}/include"
    PRIV_INCLUDE_DIRS "${Q2_KERNEL_DIR}/src/core" "${Q2_KERNEL_DIR}/third_party/lua"
        "${Q2_KERNEL_DIR}/third_party/cjson" "${Q2_KERNEL_DIR}/third_party/littlefs"
        "${Q2_KERNEL_DIR}/third_party/fatfs" "${Q2_KERNEL_DIR}/components/storage" "${Q2_KERNEL_DIR}/third_party/lvgl"
        "${Q2_KERNEL_DIR}/components/display")
target_compile_definitions(${COMPONENT_LIB} PRIVATE LUAI_MAXCCALLS=32 LUAI_MAXSTACK=2048
    CJSON_NESTING_LIMIT=16 LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)
if(Q2_GENERATED_DIR)
  target_include_directories(${COMPONENT_LIB} PUBLIC "${Q2_GENERATED_DIR}")
  target_compile_definitions(${COMPONENT_LIB} PUBLIC Q2_USE_PROJECT_CONFIG=1
    Q2_ENABLE_LOG=$<BOOL:${Q2_ENABLE_LOG}>)
else()
  target_compile_definitions(${COMPONENT_LIB} PUBLIC Q2_ENABLE_LOG=$<BOOL:${CONFIG_Q2_ENABLE_LOG}>)
endif()

# Use the SDK allocator (with configured PSRAM) instead of the PC's 16 MiB static LVGL pool.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_CONF_INCLUDE_SIMPLE=1
    Q2_ENABLE_CJK_FONT=$<BOOL:${Q2_ENABLE_CJK_FONT}>
    LV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB)
