cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sl825)

target_sources(app PRIVATE src/main.c)

# Include source directory
target_include_directories(app PRIVATE src)
target_include_directories(app PRIVATE src/m_event)
target_include_directories(app PRIVATE src/m_ble)
target_include_directories(app PRIVATE src/common)

# Application directories
add_subdirectory(src/m_event)
add_subdirectory(src/m_ble)
add_subdirectory(src/common)

zephyr_library_include_directories(.)