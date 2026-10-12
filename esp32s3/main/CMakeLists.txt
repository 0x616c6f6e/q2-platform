set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Kconfig.projbuild"
  "${CMAKE_CURRENT_LIST_DIR}/idf_component.yml")
if(CMAKE_BUILD_EARLY_EXPANSION)
  idf_component_register(REQUIRES osptek_qspi_amoled espressif__esp_lcd_co5300 esp_lcd q2-kernel esp_timer esp_psram
      esp_driver_spi esp_driver_i2c esp_driver_gpio)
  return()
endif()
if(Q2_GENERATED_DIR)
  set(Q2_APP_CONFIG "${Q2_GENERATED_DIR}/resolved.json")
endif()
if(NOT EXISTS "${Q2_APP_DIR}/factory.json" OR NOT EXISTS "${Q2_APP_CONFIG}")
  message(FATAL_ERROR "Set explicit Q2_APP_DIR and Q2_APP_CONFIG (product JSON) for factory apps")
endif()
set(bundle_dir "${CMAKE_CURRENT_BINARY_DIR}/factory")
file(MAKE_DIRECTORY "${bundle_dir}")
file(GLOB_RECURSE app_inputs CONFIGURE_DEPENDS "${Q2_APP_DIR}/apps/*")
set(bundle_c "${bundle_dir}/q2_factory_bundle.c")
set(bundle_h "${bundle_dir}/q2_factory_bundle.h")
add_custom_command(OUTPUT "${bundle_c}" "${bundle_h}"
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/embed_factory_apps.py"
        --app-dir "${Q2_APP_DIR}" --config "${Q2_APP_CONFIG}" --output "${bundle_dir}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../scripts/embed_factory_apps.py"
        "${Q2_APP_DIR}/factory.json" "${Q2_APP_DIR}/scripts/q2app.py" "${Q2_APP_CONFIG}" ${app_inputs}
    VERBATIM)
add_custom_target(q2_factory_bundle DEPENDS "${bundle_c}" "${bundle_h}")
idf_component_register(SRCS "main.c" "embedded_apps.c" "${bundle_c}" "am178.c" "am178_controls.c"
        "am178_geometry.c" "am178_touch.c" "../../common/log_console.c"
    INCLUDE_DIRS "." "../../common" "${bundle_dir}"
    REQUIRES osptek_qspi_amoled espressif__esp_lcd_co5300 esp_lcd q2-kernel esp_timer esp_psram
        esp_driver_spi esp_driver_i2c esp_driver_gpio)
add_dependencies(${COMPONENT_LIB} q2_factory_bundle)
