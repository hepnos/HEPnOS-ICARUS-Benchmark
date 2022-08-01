project (HEPnOS_ICARUS_Benchmark)
cmake_minimum_required (VERSION 3.11)

# add our cmake module directory to the path
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
     "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# C++14
set (CMAKE_CXX_STANDARD 14)

set (CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-isystem")
include_directories (${CMAKE_CURRENT_SOURCE_DIR}/include
                     ${CMAKE_CURRENT_SOURCE_DIR}/src)

# MPI
find_package (MPI REQUIRED)
set (libraries ${libraries} MPI::MPI_C)

# TCLAP
find_package (TCLAP REQUIRED)
include_directories (${TCLAP_INCLUDE_PATH})

# SPDLOG
find_package (spdlog REQUIRED)
set (libraries ${libraries} spdlog::spdlog)

# Boost
find_package (Boost REQUIRED COMPONENTS serialization)
set (libraries ${libraries} Boost::serialization)

# HEPnOS
find_package (hepnos REQUIRED)
set (libraries ${libraries} hepnos)

# Executables
add_executable (hepnos-icarus-benchmark src/Benchmark.cpp)
target_link_libraries (hepnos-icarus-benchmark ${libraries})

install (TARGETS hepnos-icarus-benchmark
         DESTINATION bin)
