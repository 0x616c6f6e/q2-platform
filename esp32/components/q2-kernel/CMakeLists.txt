if(CMAKE_BUILD_EARLY_EXPANSION)
  idf_component_register()
  return()
endif()
# SDK integration belongs to Q2-Platform. Q2-Kernel has no ESP-IDF component metadata.
include("${Q2_KERNEL_DIR}/cmake/sources.cmake")
set(sources ${Q2_CORE_SOURCES} ${Q2_RUNTIME_SOURCES})
if(NOT Q2_GENERATED_DIR OR Q2_WITH_STORAGE)
  list(APPEND sources ${Q2_STORAGE_SOURCES})
endif()
if(NOT Q2_GENERATED_DIR)
  list(APPEND sources components/display/display.c)
endif()
list(TRANSFORM sources PREPEND "${Q2_KERNEL_DIR}/")
list(APPEND sources ${Q2_LUA_SOURCES} "${Q2_KERNEL_DIR}/third_party/cjson/cJSON.c")
idf_component_register(SRCS ${sources}
    INCLUDE_DIRS "${Q2_KERNEL_DIR}/include"
    PRIV_INCLUDE_DIRS "${Q2_KERNEL_DIR}/src/core" "${Q2_KERNEL_DIR}/third_party/lua"
        "${Q2_KERNEL_DIR}/third_party/cjson" "${Q2_KERNEL_DIR}/third_party/littlefs"
        "${Q2_KERNEL_DIR}/third_party/fatfs" "${Q2_KERNEL_DIR}/components/storage")
target_compile_definitions(${COMPONENT_LIB} PRIVATE LUAI_MAXCCALLS=32 LUAI_MAXSTACK=2048
    CJSON_NESTING_LIMIT=16 LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)
if(Q2_GENERATED_DIR)
  target_include_directories(${COMPONENT_LIB} PUBLIC "${Q2_GENERATED_DIR}")
  target_compile_definitions(${COMPONENT_LIB} PUBLIC Q2_USE_PROJECT_CONFIG=1
    Q2_ENABLE_LOG=$<BOOL:${Q2_ENABLE_LOG}>)
else()
  target_compile_definitions(${COMPONENT_LIB} PUBLIC Q2_ENABLE_LOG=$<BOOL:${CONFIG_Q2_ENABLE_LOG}>)
endif()
