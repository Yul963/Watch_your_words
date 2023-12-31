﻿cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(WITH_CUDA "Build with CUDA support. (Windows, CUDA toolkit required)" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_qt(COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

set(USE_SYSTEM_CURL
    OFF
    CACHE STRING "Use system cURL")

if(USE_SYSTEM_CURL)
  find_package(CURL REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${CURL_LIBRARIES}")
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC "${CURL_INCLUDE_DIRS}")
else()
  include(cmake/BuildMyCurl.cmake)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE libcurl)
endif()

include(cmake/BuildWhispercpp.cmake)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Whispercpp)

include(cmake/BuildRapidJSON.cmake)
include_directories(${RAPIDJSON_INCLUDE_DIR})
add_dependencies(${CMAKE_PROJECT_NAME} rapidjson)

execute_process(
    COMMAND ${CMAKE_SOURCE_DIR}/vcpkg.bat
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
)

if(result EQUAL 0)
    message("Command executed successfully")
    message("Output: ${output}")
else()
    message("Command failed with error code: ${result}")
endif()

#include(cmake/vcpkg.cmake)
include(${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake)
find_package(google_cloud_cpp_common CONFIG REQUIRED)
find_package(google_cloud_cpp_googleapis CONFIG REQUIRED)
find_package(google_cloud_cpp_grpc_utils CONFIG REQUIRED)
find_package(google_cloud_cpp_speech CONFIG REQUIRED)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/vcpkg/installed/x64-windows/bin/
          DESTINATION "obs-plugins/64bit")
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    google-cloud-cpp::common
    google-cloud-cpp::api_log_protos
    google-cloud-cpp::api_auth_protos
    google-cloud-cpp::grpc_utils
    google-cloud-cpp::speech
    google-cloud-cpp::speech_protos
    google-cloud-cpp::cloud_speech_protos
)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE "src/plugin-main.c" "src/source.cpp" "src/source.c" "src/speech-to-text/whisper-processing.cpp"
          "src/model-utils/model-downloader.cpp" "src/model-utils/model-downloader-ui.cpp" 
          "src/frequency-utils/frequency-dock.cpp"
          "src/delay-filters/audio_delay.c" "src/delay-filters/audio_delay.cpp" "src/delay-filters/video_delay.c"
          "src/speech-to-text/google-stt-processing.cpp" "src/utils/util.cpp")

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
