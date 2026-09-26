# Please see README.md for build instructions

cmake_minimum_required(VERSION 3.16.4)

set(CMAKE_SYSTEM_NAME Generic)

set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel)

if(BUILD_KIT)
	message("Build Kit is defined as ${BUILD_KIT}")
endif()

message("CMAKE_CURRENT_BINARY_DIR is defined as ${CMAKE_CURRENT_BINARY_DIR}")

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(isMultiConfig)
    message("Multi-config generator(${CMAKE_GENERATOR}) is active")
else()
    message("Single-config generator(${CMAKE_GENERATOR}) is active")
	if(DEFINED CMAKE_BUILD_TYPE)
	 	message("CMAKE_BUILD_TYPE is defined as ${CMAKE_BUILD_TYPE}")
	elseif(DEFINED BUILD_TYPE)
		set(CMAKE_BUILD_TYPE ${BUILD_TYPE})
		message("CMAKE_BUILD_TYPE is set to BUILD_TYPE ENV ${CMAKE_BUILD_TYPE}")
	else()
		message(FATAL_ERROR "Please specify the CMAKE_BUILD_TYPE with -DCMAKE_BUILD_TYPE=")
	endif()
    #set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE})
	message("CMAKE_RUNTIME_OUTPUT_DIRECTORY is defined as ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endif()

cmake_minimum_required(VERSION 3.16.4)

# Project configuration
project(data_collector_fw
	VERSION 1.0.0
	LANGUAGES C CXX ASM)

option(COLLECTOR_FSP_IO_ENABLED "Enable generated FSP USB PCDC instance" ON)

if(COLLECTOR_FSP_IO_ENABLED)
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ra_gen/common_data.h" COLLECTOR_GENERATED_COMMON_DATA)
	foreach(REQUIRED_TEXT
		"g_basic0"
		"#define VIN_CFG_IMAGE_STRIDE (640)"
		"#define VIN_CFG_BYTES_PER_LINE (1280)"
		"#define VIN_BYTES_PER_FRAME (VIN_CFG_BYTES_PER_LINE * 480)")
		string(FIND "${COLLECTOR_GENERATED_COMMON_DATA}" "${REQUIRED_TEXT}" REQUIRED_TEXT_OFFSET)
		if(REQUIRED_TEXT_OFFSET EQUAL -1)
			message(FATAL_ERROR
				"FSP generated files are not ready for camera collection: missing '${REQUIRED_TEXT}'. "
				"Open configuration.xml with e2 studio 2026-01/FSP 6.4, add USB PCDC, then regenerate the project.")
		endif()
	endforeach()
endif()

set(CMAKE_CROSSCOMPILING "TRUE")
set(CMAKE_EXPORT_COMPILE_COMMANDS "TRUE")

include(${CMAKE_CURRENT_LIST_DIR}/Config.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/GeneratedCfg.cmake)

# If required user can amend RASC generated flags here, e.g. list(REMOVE_ITEM RASC_CMAKE_C_FLAGS "-Wall")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/CollectorSrc.cmake)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/ra/aws/FreeRTOS/FreeRTOS/Source/include
	${CMAKE_CURRENT_SOURCE_DIR}/ra/fsp/src/rm_freertos_port
	${CMAKE_CURRENT_SOURCE_DIR}/ra/fsp/src/r_mipi_csi
	${CMAKE_CURRENT_SOURCE_DIR}/ra/fsp/src/r_vin
	${CMAKE_CURRENT_SOURCE_DIR}/ra_cfg/aws
)

target_compile_definitions(${PROJECT_NAME}.elf PRIVATE LV_CONF_INCLUDE_SIMPLE)

if(COLLECTOR_FSP_IO_ENABLED)
	target_compile_definitions(${PROJECT_NAME}.elf PRIVATE COLLECTOR_FSP_IO_ENABLED)
endif()
