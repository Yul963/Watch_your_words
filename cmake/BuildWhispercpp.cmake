include(ExternalProject)

set(CMAKE_OSX_ARCHITECTURES_ "arm64$<SEMICOLON>x86_64")

set(WHISPERCPP_VERSION 1.4.2)

if(${CMAKE_BUILD_TYPE} STREQUAL Release OR ${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
  set(Whispercpp_BUILD_TYPE Release)
else()
  set(Whispercpp_BUILD_TYPE Debug)
endif()

# On linux add the `-fPIC` flag to the compiler
if(UNIX AND NOT APPLE)
  set(WHISPER_EXTRA_CXX_FLAGS "-fPIC")
  set(WHISPER_ADDITIONAL_CMAKE_ARGS -DWHISPER_BLAS=OFF -DWHISPER_CUBLAS=OFF -DWHISPER_OPENBLAS=OFF -DWHISPER_NO_AVX=ON
                                    -DWHISPER_NO_AVX2=ON)
endif()

if(WIN32)
    if(WITH_CUDA)
        #set(CUDA_TOOLKIT_ROOT_DIR "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.2")
        #set(WHISPER_ADDITIONAL_ENV "CUDAToolkit_ROOT=${CUDA_TOOLKIT_ROOT_DIR}")
        set(WHISPER_ADDITIONAL_CMAKE_ARGS -DWHISPER_CUBLAS=ON) #-DCMAKE_GENERATOR_TOOLSET=cuda=${CUDA_TOOLKIT_ROOT_DIR})
    endif()

    ExternalProject_Add(
      Whispercpp_Build
      DOWNLOAD_EXTRACT_TIMESTAMP true
      #URL "https://github.com/ggerganov/whisper.cpp/archive/v${WHISPERCPP_VERSION}.zip"
      GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
      GIT_TAG f96e1c5b7865e01fece99f69286d922d949a260d
      BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE}
      BUILD_BYPRODUCTS
      <INSTALL_DIR>/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX}
      <INSTALL_DIR>/bin/${CMAKE_SHARED_LIBRARY_PREFIX}whisper${CMAKE_SHARED_LIBRARY_SUFFIX}
      <INSTALL_DIR>/lib/${CMAKE_IMPORT_LIBRARY_PREFIX}whisper${CMAKE_IMPORT_LIBRARY_SUFFIX}
      CMAKE_GENERATOR ${CMAKE_GENERATOR}
      INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE} && ${CMAKE_COMMAND} -E
                      copy <BINARY_DIR>/${Whispercpp_BUILD_TYPE}/whisper${CMAKE_STATIC_LIBRARY_SUFFIX} <INSTALL_DIR>/lib
      CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env ${WHISPER_ADDITIONAL_ENV} ${CMAKE_COMMAND} <SOURCE_DIR> -B <BINARY_DIR> -G
        ${CMAKE_GENERATOR} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_BUILD_TYPE=${Whispercpp_BUILD_TYPE}
        -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM} -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
        -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES_} -DCMAKE_CXX_FLAGS=${WHISPER_EXTRA_CXX_FLAGS}
        -DCMAKE_C_FLAGS=${WHISPER_EXTRA_CXX_FLAGS} -DBUILD_SHARED_LIBS=ON -DWHISPER_BUILD_TESTS=OFF
        -DWHISPER_BUILD_EXAMPLES=OFF ${WHISPER_ADDITIONAL_CMAKE_ARGS})
else()
    ExternalProject_Add(
      Whispercpp_Build
      DOWNLOAD_EXTRACT_TIMESTAMP true
      #URL "https://github.com/ggerganov/whisper.cpp/archive/v${WHISPERCPP_VERSION}.zip"
      GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
      GIT_TAG f96e1c5b7865e01fece99f69286d922d949a260d
      BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE}
      BUILD_BYPRODUCTS <INSTALL_DIR>/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX}
      CMAKE_GENERATOR ${CMAKE_GENERATOR}
      INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE}
      CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} <SOURCE_DIR> -B <BINARY_DIR> -G
        ${CMAKE_GENERATOR} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_BUILD_TYPE=${Whispercpp_BUILD_TYPE}
        -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM} -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
        -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES_} -DCMAKE_CXX_FLAGS=${WHISPER_EXTRA_CXX_FLAGS}
        -DCMAKE_C_FLAGS=${WHISPER_EXTRA_CXX_FLAGS} -DBUILD_SHARED_LIBS=OFF -DWHISPER_BUILD_TESTS=OFF
        -DWHISPER_BUILD_EXAMPLES=OFF ${WHISPER_ADDITIONAL_CMAKE_ARGS})
endif()

ExternalProject_Get_Property(Whispercpp_Build INSTALL_DIR)

if(WIN32)
  add_library(Whispercpp::Whisper SHARED IMPORTED)
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/bin/${CMAKE_SHARED_LIBRARY_PREFIX}whisper${CMAKE_SHARED_LIBRARY_SUFFIX})
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES IMPORTED_IMPLIB ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})

  install(FILES ${INSTALL_DIR}/bin/${CMAKE_SHARED_LIBRARY_PREFIX}whisper${CMAKE_SHARED_LIBRARY_SUFFIX}
          DESTINATION "obs-plugins/64bit")
else()
  # on Linux and MacOS add the static Whisper library to the link line
  add_library(Whispercpp::Whisper STATIC IMPORTED)
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES IMPORTED_LOCATION
               ${INSTALL_DIR}/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})
endif(WIN32)

add_library(Whispercpp INTERFACE)
add_dependencies(Whispercpp Whispercpp_Build)
target_link_libraries(Whispercpp INTERFACE Whispercpp::Whisper)
set_target_properties(Whispercpp::Whisper PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
if(APPLE)
  target_link_libraries(Whispercpp INTERFACE "-framework Accelerate")
endif(APPLE)
