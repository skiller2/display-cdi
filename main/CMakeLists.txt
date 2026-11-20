# Interfaz exportada por EEZ Studio (main/ui)
file(GLOB_RECURSE EEZ_UI_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp")

idf_component_register(
    SRCS
        "main.c"
        "wifi_config.c"
        "sys_info.c"
        "display_config.c"
        "../components/waveshare_rgb_lcd_port.c"
        ${EEZ_UI_SRCS}
    INCLUDE_DIRS
        "."
        "ui"
        "../components")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -Wno-unused-function)
# El motor de EEZ Flow (eez-flow.cpp) genera warnings que ESP-IDF trata como error
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/ui/eez-flow.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-error;-w")

idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
elseif("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
endif()

if(lvgl_lib)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()
