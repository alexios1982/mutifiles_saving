cmake_minimum_required(VERSION 3.1)
project(multifiles_saving)
set(CMAKE_CXX_STANDARD 11)
# Set build type if not set.
if(NOT CMAKE_BUILD_TYPE )
  set( CMAKE_BUILD_TYPE Release)
endif ()
# Set additional flags.
#-Wall Turns on lots of compiler warning flags, (-Wunused-value etc)
#-Wextra Enables extra flags not enabled by -Wall, such as -Wsign-compare (C only), -Wtype-limits
#pedantic-errors option is to avoid specific compiler extension
set( CMAKE_CXX_FLAGS "-Wall -Wextra -std=c++11 -pedantic-errors")  
set( CMAKE_CXX_FLAGS_DEBUG "-g -O0")                               
set( CMAKE_CXX_FLAGS_RELEASE "-O3")


#section to define executable
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
#message(STATUS "Output path ${EXECUTABLE_OUTPUT_PATH}")
include_directories(${PROJECT_SOURCE_DIR}/src)
add_executable(multifiles_saving src/multifiles_saving.cc)

#section to include external libraries to compile
#to tell cmake to compile the library using its own CMakeLists.txt file
add_subdirectory(${PROJECT_SOURCE_DIR}/lib)
#to be able to use the library's include file
include_directories(${PROJECT_SOURCE_DIR}/lib)
############################################################

#######################################################################
#to be able to compile gstreamer apps
find_package(PkgConfig)
pkg_check_modules(GST REQUIRED gstreamer-1.0>=1.4
  gstreamer-base-1.0>=1.4)
target_include_directories(multifiles_saving PUBLIC ${GST_INCLUDE_DIRS})
target_compile_options(multifiles_saving PUBLIC ${GST_CFLAGS})
#######################################################################

#to link the lib to the executable
target_link_libraries(multifiles_saving PUBLIC
  ${GST_LIBRARIES}
  Utils)
