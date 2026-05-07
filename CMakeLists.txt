# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{ADF_PATH} OR "$ENV{ADF_PATH}" STREQUAL "")
	message(FATAL_ERROR "ADF_PATH is not set. Install ESP-ADF and set ADF_PATH, e.g. C:/Espressif/tools/esp-adf")
endif()

file(TO_CMAKE_PATH "$ENV{ADF_PATH}" _ADF_PATH)
if(NOT EXISTS "${_ADF_PATH}/CMakeLists.txt")
	file(TO_CMAKE_PATH "$ENV{ADF_PATH}/esp-adf" _ADF_PATH_FALLBACK)
	if(EXISTS "${_ADF_PATH_FALLBACK}/CMakeLists.txt")
		set(_ADF_PATH "${_ADF_PATH_FALLBACK}")
	endif()
	unset(_ADF_PATH_FALLBACK)
endif()

if(NOT EXISTS "${_ADF_PATH}/CMakeLists.txt")
	message(FATAL_ERROR "ADF_PATH is set to '$ENV{ADF_PATH}', but CMakeLists.txt was not found. Expected either '$ENV{ADF_PATH}/CMakeLists.txt' or '$ENV{ADF_PATH}/esp-adf/CMakeLists.txt'.")
endif()

message(STATUS "Using ESP-ADF from: ${_ADF_PATH}")
include(${_ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(inetRadio)

fatfs_create_spiflash_image(storage ../inetradio/site/dist FLASH_IN_PROJECT)