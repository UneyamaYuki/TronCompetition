file(GLOB_RECURSE COLLECTOR_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_gen/*.c)

if(COLLECTOR_FSP_IO_ENABLED)
    set(COLLECTOR_USB_DESCRIPTOR_TEMPLATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ra/fsp/src/r_usb_pcdc/r_usb_pcdc_descriptor.c.template)
    set(COLLECTOR_USB_DESCRIPTOR_SOURCE
        ${CMAKE_CURRENT_BINARY_DIR}/usb_pcdc_descriptor.c)
    configure_file(${COLLECTOR_USB_DESCRIPTOR_TEMPLATE}
        ${COLLECTOR_USB_DESCRIPTOR_SOURCE} COPYONLY)
    list(APPEND COLLECTOR_SOURCES ${COLLECTOR_USB_DESCRIPTOR_SOURCE})
endif()

list(APPEND COLLECTOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/board_cfg_switch.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/board_greenpak.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/board_hw_cfg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/board_i2c_master.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/board_sdram.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/camera_thread_entry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/collector_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/collector_board_state.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/collector_buttons.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/collector_disabled_threads.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/collector_platform.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/collector_platform_fsp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/collector_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/collector_state.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common_init.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal_entry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/jlink_console.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ospi_b_commands.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ospi_b_ep.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ospi_commands.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ov5640.c)

list(FILTER COLLECTOR_SOURCES EXCLUDE REGEX "[/\\\\]ra[/\\\\]lvgl[/\\\\]")
list(FILTER COLLECTOR_SOURCES EXCLUDE REGEX "[/\\\\]ra[/\\\\]tes[/\\\\]")
list(FILTER COLLECTOR_SOURCES EXCLUDE REGEX "[/\\\\]rm_lvgl_port[/\\\\]")
list(FILTER COLLECTOR_SOURCES EXCLUDE REGEX "[/\\\\]src[/\\\\](display_thread_entry|main_menu_thread_entry|menu_.*|tp_thread_entry|touch_FT5316|board_mon_thread_entry|blinky_thread_entry)\\.c$")
list(FILTER COLLECTOR_SOURCES EXCLUDE REGEX "[/\\\\]ra_gen[/\\\\](display_thread|main_menu_thread|board_mon_thread|tp_thread|blinky_thread)\\.c$")

add_executable(${PROJECT_NAME}.elf ${COLLECTOR_SOURCES})

target_compile_options(${PROJECT_NAME}.elf PRIVATE
    $<$<CONFIG:Debug>:${RASC_DEBUG_FLAGS}>
    $<$<CONFIG:Release>:${RASC_RELEASE_FLAGS}>
    $<$<CONFIG:MinSizeRel>:${RASC_MIN_SIZE_RELEASE_FLAGS}>
    $<$<CONFIG:RelWithDebInfo>:${RASC_RELEASE_WITH_DEBUG_INFO}>
    $<$<COMPILE_LANGUAGE:C>:${RASC_CMAKE_C_FLAGS}>)

target_link_options(${PROJECT_NAME}.elf PRIVATE ${RASC_CMAKE_EXE_LINKER_FLAGS})
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ${RASC_CMAKE_DEFINITIONS})

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/arm/CMSIS_6/CMSIS/Core/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/fsp/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/fsp/inc/api
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/fsp/inc/instances
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/tes/dave2d/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_cfg/fsp_cfg
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_cfg/fsp_cfg/bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_cfg/fsp_cfg/lvgl/lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_cfg/fsp_cfg/middleware
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_gen
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})

target_link_directories(${PROJECT_NAME}.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/script)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O srec ${PROJECT_NAME}.elf ${PROJECT_NAME}.srec
    COMMENT "Creating S-record file in ${PROJECT_BINARY_DIR}")

if(RASC_EXE_PATH)
    add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
        COMMAND echo "Running RASC post-build to generate Smart Bundle file for ${PROJECT_NAME}:"
        COMMAND echo ${RASC_EXE_PATH} -nosplash --launcher.suppressErrors --gensmartbundle --devicefamily ra --compiler GCC --toolchainversion ${CMAKE_C_COMPILER_VERSION} ${CMAKE_CURRENT_SOURCE_DIR}/configuration.xml ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf
        COMMAND ${RASC_EXE_PATH} -nosplash --launcher.suppressErrors --gensmartbundle --devicefamily ra --compiler GCC --toolchainversion ${CMAKE_C_COMPILER_VERSION} ${CMAKE_CURRENT_SOURCE_DIR}/configuration.xml ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf
        VERBATIM)
endif()