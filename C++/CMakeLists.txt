cmake_minimum_required(VERSION 3.10)
project(VitalApp)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find Zlib
find_package(ZLIB REQUIRED)

# Add source files
set(VITAL_APP_SOURCES vital_list.cpp)
set(VITAL_TRKS_SOURCES vital_trks.cpp)

# Create executables
add_executable(vital_app ${VITAL_APP_SOURCES})
add_executable(vital_trks ${VITAL_TRKS_SOURCES})

# Link against the static library and Zlib
target_link_libraries(vital_app PRIVATE ${CMAKE_SOURCE_DIR}/libvitalutils.a ZLIB::ZLIB)
target_link_libraries(vital_trks PRIVATE ${CMAKE_SOURCE_DIR}/libvitalutils.a ZLIB::ZLIB)

# Include headers
target_include_directories(vital_app PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(vital_trks PRIVATE ${CMAKE_SOURCE_DIR})
