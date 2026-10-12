# Optional product input. Standalone consumers do not require the parent workspace.
if(NOT TARGET q2_platform_config)
  add_library(q2_platform_config INTERFACE)
  target_include_directories(q2_platform_config INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../common")
  if(Q2_GENERATED_DIR)
    target_include_directories(q2_platform_config INTERFACE "${Q2_GENERATED_DIR}")
    target_compile_definitions(q2_platform_config INTERFACE Q2_USE_PROJECT_CONFIG=1)
  else()
    option(Q2_PLATFORM_KEYS "Register named keys" ON)
    option(Q2_PLATFORM_APPS "Enable foreground APP manager" ON)
    option(Q2_PLATFORM_WEB "Enable loopback key simulator" ON)
    target_compile_definitions(q2_platform_config INTERFACE
      Q2_COMPONENT_STORAGE=$<BOOL:${Q2_WITH_STORAGE}>
      Q2_COMPONENT_SQLITE=$<BOOL:${Q2_WITH_SQLITE}>
      Q2_COMPONENT_KEYS=$<BOOL:${Q2_PLATFORM_KEYS}>
      Q2_COMPONENT_APPS=$<BOOL:${Q2_PLATFORM_APPS}>
      Q2_WEB_ENABLED=$<BOOL:${Q2_PLATFORM_WEB}>)
  endif()
endif()
