cmake_minimum_required(VERSION 3.12)
project(MultiFileProject C)

# 1. Find GStreamer (since you are working with it)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0)

# 2. Get a list of all .c files in the current directory
file(GLOB SOURCE_FILES CONFIGURE DEPENDS "*.c")

# 3. Loop through each file
foreach(SOURCE_FILE ${SOURCE_FILES})
    # Get the filename without the extension (e.g., "player" from "player.c")
    get_filename_component(EXE_NAME ${SOURCE_FILE} NAME_WE)

    # Create the executable
    add_executable(${EXE_NAME} ${SOURCE_FILE})

    # Link GStreamer libraries to this specific executable
    target_include_directories(${EXE_NAME} PUBLIC ${GST_INCLUDE_DIRS})
    target_link_libraries(${EXE_NAME} ${GST_LIBRARIES})
    
    message(STATUS "Created executable: ${EXE_NAME}")
endforeach()
