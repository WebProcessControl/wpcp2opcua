cmake_minimum_required(VERSION 2.8)

project(wpcp2opcua)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")
SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
SET(CMAKE_GENERATED_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/generated)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})


if (MSVC)
  set(ADDITIONAL_COMPILER_FLAGS "/Wall /wd4820")

  if (MSVC_VERSION LESS 1600)
    set(ADDITIONAL_COMPILER_FLAGS "${ADDITIONAL_COMPILER_FLAGS} /wd4255")
    include_directories(win32compat)
  endif ()

  if (NOT MSVC_VERSION LESS 1500)
    set(ADDITIONAL_COMPILER_FLAGS "${ADDITIONAL_COMPILER_FLAGS} /MP")
  endif ()
endif ()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ADDITIONAL_COMPILER_FLAGS}")


include(ExternalProject)

ExternalProject_Add(
  libwpcp
  GIT_REPOSITORY https://github.com/WebProcessControl/libwpcp.git
  GIT_TAG v0.1.0
  CMAKE_ARGS "-DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>" "-DWPCP_BUILD_LIBWEBSOCKETS=ON" "-DWPCP_BUILD_CHECK=ON"
)

ExternalProject_Get_Property(libwpcp INSTALL_DIR)
ExternalProject_Get_Property(libwpcp SOURCE_DIR)

add_library(wpcp STATIC IMPORTED)
add_library(wpcp-lws STATIC IMPORTED)
add_dependencies(wpcp libwpcp)
add_dependencies(wpcp-lws libwpcp)
set_property(TARGET wpcp PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}wpcp${CMAKE_STATIC_LIBRARY_SUFFIX})
set_property(TARGET wpcp-lws PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}wpcp-lws${CMAKE_STATIC_LIBRARY_SUFFIX})

set(LIBWPCP_INCLUDE_DIRS ${SOURCE_DIR}/include)
set(LIBWPCP_LIBRARIES wpcp wpcp-lws)

include_directories(${LIBWPCP_INCLUDE_DIRS})

set(WPCP2OPCUA_SOURCES
  channel.c
  convert.c
  main.c
  main.h
  pubsub.c
  rw.c
)

set(ENVPROGRAMFILES "PROGRAMFILES(X86)") 
set(UaSdkCppDir "$ENV{${ENVPROGRAMFILES}}/UnifiedAutomation/UaSdkCppBundleEval")
include_directories("${UaSdkCppDir}/include/uastack")
link_directories("${UaSdkCppDir}/lib")
set(UA_STACK_LIB uastack)

add_executable(wpcp2opcua ${WPCP2OPCUA_SOURCES})
set_property(TARGET wpcp2opcua PROPERTY COMPILE_DEFINITIONS _UA_STACK_USE_DLL)
target_link_libraries(wpcp2opcua ${LIBWPCP_LIBRARIES} ${UA_STACK_LIB})
