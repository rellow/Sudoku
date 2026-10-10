cmake_minimum_required(VERSION 3.11)

project(SudokuProject)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

# Select an architecture explicitly; no preset silently represents the P620.
set(SUDOKU_PLATFORM "ZEN_4" CACHE STRING "Target: ALDER_LAKE_DDR4, ALDER_LAKE_DDR5, RAPTOR_LAKE, ZEN_4, P620_ZEN3_DDR4")
set_property(CACHE SUDOKU_PLATFORM PROPERTY STRINGS ALDER_LAKE_DDR4 ALDER_LAKE_DDR5 RAPTOR_LAKE ZEN_4 P620_ZEN3_DDR4)
if(NOT SUDOKU_PLATFORM MATCHES "^(ALDER_LAKE_DDR4|ALDER_LAKE_DDR5|RAPTOR_LAKE|ZEN_4|P620_ZEN3_DDR4)$")
  message(FATAL_ERROR "Unknown SUDOKU_PLATFORM: ${SUDOKU_PLATFORM}")
endif()
add_compile_definitions(COMPILE_${SUDOKU_PLATFORM})
if(SUDOKU_PLATFORM STREQUAL "P620_ZEN3_DDR4")
  message(WARNING "P620 preset is UNCALIBRATED: exploratory timing only. Automated function inference is disabled.")
endif()

add_compile_options(-Wall -Wextra -O0)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(sudoku)
