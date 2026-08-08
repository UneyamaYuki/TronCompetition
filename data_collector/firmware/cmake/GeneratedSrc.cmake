# This file was automatically generated and updated by RASC and should not be edited by the user.
# Use CMakeLists.txt to override the settings in this file 

#source directories
file(GLOB_RECURSE Source_Files 
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_gen/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_gen/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)


SET(ALL_FILES ${Source_Files})


add_executable(${PROJECT_NAME}.elf
	${ALL_FILES}
)

target_compile_options(${PROJECT_NAME}.elf
                       PRIVATE
                       $<$<CONFIG:Debug>:${RASC_DEBUG_FLAGS}>
                       $<$<CONFIG:Release>:${RASC_RELEASE_FLAGS}>
                       $<$<CONFIG:MinSizeRel>:${RASC_MIN_SIZE_RELEASE_FLAGS}>
                       $<$<CONFIG:RelWithDebInfo>:${RASC_RELEASE_WITH_DEBUG_INFO}>)

target_compile_options(${PROJECT_NAME}.elf PRIVATE  $<$<COMPILE_LANGUAGE:C>:${RASC_CMAKE_C_FLAGS}>)
target_compile_options(${PROJECT_NAME}.elf PRIVATE  $<$<COMPILE_LANGUAGE:CXX>:${RASC_CMAKE_CXX_FLAGS}>)

target_link_options(${PROJECT_NAME}.elf PRIVATE $<$<LINK_LANGUAGE:C>:${RASC_CMAKE_EXE_LINKER_FLAGS}>)
target_link_options(${PROJECT_NAME}.elf PRIVATE $<$<LINK_LANGUAGE:CXX>:${RASC_CMAKE_EXE_LINKER_FLAGS}>)

target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ${RASC_CMAKE_DEFINITIONS})

target_include_directories(${PROJECT_NAME}.elf
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/arm/CMSIS_6/CMSIS/Core/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/fsp/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/fsp/inc/api
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/fsp/inc/instances
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/fsp/src/rm_lvgl_port
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/demos
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/examples
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/display
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/draw
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/font
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/indev
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/layouts
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/libs
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/misc
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/osal
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/others
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/stdlib
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/themes
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/3dtexture
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/animimage
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/arc
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/bar
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/button
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/buttonmatrix
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/calendar
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/canvas
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/chart
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/checkbox
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/dropdown
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/image
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/imagebutton
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/keyboard
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/label
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/led
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/line
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/list
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/lottie
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/menu
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/msgbox
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/objx_templ
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/property
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/roller
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/scale
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/slider
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/span
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/spinbox
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/spinner
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/switch
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/table
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/tabview
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/textarea
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/tileview
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/lvgl/lvgl/src/widgets/win
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/tes/dave2d/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_cfg/fsp_cfg
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_cfg/fsp_cfg/bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_cfg/fsp_cfg/lvgl/lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_cfg/fsp_cfg/middleware
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_gen
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/
)

target_link_directories(${PROJECT_NAME}.elf
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/script
)

target_link_libraries(${PROJECT_NAME}.elf
    PRIVATE
    
)

add_custom_command(
    TARGET
        ${PROJECT_NAME}.elf
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O srec ${PROJECT_NAME}.elf ${PROJECT_NAME}.srec
    COMMENT "Creating S-record file in ${PROJECT_BINARY_DIR}"
)


# Pre-build step: run RASC to generate project content if configuration.xml is changed
add_custom_command(
    OUTPUT
        configuration.xml.stamp
    COMMAND
        echo "Running RASC for generating project ${PROJECT_NAME} content since modification is detected in configuration.xml:"
    COMMAND
        echo ${RASC_EXE_PATH}  -nosplash --launcher.suppressErrors --generate --devicefamily ra --compiler GCC --toolchainversion ${CMAKE_C_COMPILER_VERSION} ${CMAKE_CURRENT_SOURCE_DIR}/configuration.xml
    COMMAND
        ${RASC_EXE_PATH}  -nosplash --launcher.suppressErrors --generate --devicefamily ra --compiler GCC --toolchainversion ${CMAKE_C_COMPILER_VERSION} ${CMAKE_CURRENT_SOURCE_DIR}/configuration.xml 2> rasc_cmd_log.txt
    COMMAND
        ${CMAKE_COMMAND} -E touch configuration.xml.stamp
    COMMENT
        "RASC pre-build to generate project content for ${PROJECT_NAME}"
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/configuration.xml
)

add_custom_target(generate_content_${PROJECT_NAME}
  DEPENDS configuration.xml.stamp
)

add_dependencies(${PROJECT_NAME}.elf generate_content_${PROJECT_NAME})


# Post-build step: run RASC to generate the SmartBundle file
add_custom_command(
	OUTPUT
         ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.sbd
    COMMAND
        echo "Running RASC post-build to generate Smart Bundle file for ${PROJECT_NAME}:"
    COMMAND
        echo ${RASC_EXE_PATH} -nosplash --launcher.suppressErrors --gensmartbundle --devicefamily ra --compiler GCC --toolchainversion ${CMAKE_C_COMPILER_VERSION}  ${CMAKE_CURRENT_SOURCE_DIR}/configuration.xml ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf 
    COMMAND
        ${RASC_EXE_PATH} -nosplash --launcher.suppressErrors --gensmartbundle --devicefamily ra --compiler GCC --toolchainversion ${CMAKE_C_COMPILER_VERSION}  ${CMAKE_CURRENT_SOURCE_DIR}/configuration.xml ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf  2> rasc_cmd_log.txt
)

add_custom_target(generate_sbd_${PROJECT_NAME} ALL
	DEPENDS
		${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.sbd
		${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf
	VERBATIM
)

add_dependencies(generate_sbd_${PROJECT_NAME} ${PROJECT_NAME}.elf)


add_custom_command(
    TARGET
        ${PROJECT_NAME}.elf
    POST_BUILD
    COMMAND
        echo ${RASC_EXE_PATH} -nosplash --launcher.suppressErrors --gensmartbundle --devicefamily ra --compiler GCC --toolchainversion ${CMAKE_C_COMPILER_VERSION}  ${CMAKE_CURRENT_SOURCE_DIR}/configuration.xml ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf 
    COMMAND
        ${RASC_EXE_PATH} -nosplash --launcher.suppressErrors --gensmartbundle --devicefamily ra --compiler GCC --toolchainversion ${CMAKE_C_COMPILER_VERSION}  ${CMAKE_CURRENT_SOURCE_DIR}/configuration.xml ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf  2> rasc_cmd_log.txt
	VERBATIM
)
