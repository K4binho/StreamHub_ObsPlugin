cmake_minimum_required(VERSION 3.28...3.30)

project(obs-noise-suppressor VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(OBS_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/cmake" CACHE PATH "Directory containing libobsConfig.cmake")
set(OBS_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/obs-deps-2026-07-15-x64" CACHE PATH "OBS dependency prefix")
set(RNNOISE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/obs-studio-32.2.1/plugins/obs-filters/rnnoise" CACHE PATH "Internal RNNoise source directory")

list(PREPEND CMAKE_PREFIX_PATH "${OBS_CMAKE_DIR}" "${OBS_DEPS_DIR}")
find_package(libobs REQUIRED CONFIG)

add_library(obs-noise-rnnoise OBJECT)
target_sources(
  obs-noise-rnnoise
  PRIVATE
    "${RNNOISE_SOURCE_DIR}/src/celt_lpc.c"
    "${RNNOISE_SOURCE_DIR}/src/denoise.c"
    "${RNNOISE_SOURCE_DIR}/src/kiss_fft.c"
    "${RNNOISE_SOURCE_DIR}/src/pitch.c"
    "${RNNOISE_SOURCE_DIR}/src/rnn.c"
    "${RNNOISE_SOURCE_DIR}/src/rnn_data.c"
    "${RNNOISE_SOURCE_DIR}/src/rnn_reader.c"
)
target_include_directories(obs-noise-rnnoise PUBLIC "${RNNOISE_SOURCE_DIR}/include" "${RNNOISE_SOURCE_DIR}/src")
target_compile_definitions(obs-noise-rnnoise PUBLIC COMPILE_OPUS)
set_target_properties(obs-noise-rnnoise PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

add_library(obs-noise-suppressor MODULE)
target_sources(
  obs-noise-suppressor
  PRIVATE
    src/noise-suppressor-dsp.c
    src/noise-suppressor-dsp.h
    src/noise-suppressor-filter.c
)
target_include_directories(obs-noise-suppressor PRIVATE src "${RNNOISE_SOURCE_DIR}/include")
target_sources(obs-noise-suppressor PRIVATE $<TARGET_OBJECTS:obs-noise-rnnoise>)
target_link_libraries(obs-noise-suppressor PRIVATE OBS::libobs)
set_target_properties(
  obs-noise-suppressor
  PROPERTIES
    PREFIX ""
    OUTPUT_NAME "obs-noise-suppressor"
    VERSION "0"
    SOVERSION "${PROJECT_VERSION}"
)

if(WIN32)
  set_target_properties(obs-noise-suppressor PROPERTIES SUFFIX ".dll")
endif()

include(CTest)
option(BUILD_TESTING "Build DSP tests" ON)
if(BUILD_TESTING)
  add_executable(obs-noise-suppressor-dsp-test tests/test_dsp.c src/noise-suppressor-dsp.c)
  target_include_directories(obs-noise-suppressor-dsp-test PRIVATE src "${RNNOISE_SOURCE_DIR}/include")
  target_sources(obs-noise-suppressor-dsp-test PRIVATE $<TARGET_OBJECTS:obs-noise-rnnoise>)
  if(NOT WIN32)
    target_link_libraries(obs-noise-suppressor-dsp-test PRIVATE m)
  endif()
  add_test(NAME obs-noise-suppressor-dsp COMMAND obs-noise-suppressor-dsp-test)
endif()

include(GNUInstallDirs)
install(
  TARGETS obs-noise-suppressor
  RUNTIME DESTINATION "obs-plugins/64bit"
  LIBRARY DESTINATION "obs-plugins"
)
install(FILES LICENSE-RNNOISE.txt DESTINATION "obs-noise-suppressor")
