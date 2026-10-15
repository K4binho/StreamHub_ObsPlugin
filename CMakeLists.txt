cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-32.2.1/build_x64/libobs)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-32.2.1/build_x64/deps/w32-pthreads)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-32.2.1/build_x64/UI/obs-frontend-api)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  # WebSockets NÃO entra aqui de propósito: o pacote Qt pré-compilado que o
  # OBS baixa não inclui esse módulo (precisaria ser compilado manualmente
  # à parte). O dock de chat do StreamHub usa long-polling HTTP via
  # Qt6::Network (QNetworkAccessManager), que já vem com o pacote.
  find_package(Qt6 COMPONENTS Widgets Core Network)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  ./src/pch.h
  ./src/obs-multi-rtmp.h
  ./src/obs-multi-rtmp.cpp 
  ./src/edit-widget.h
  ./src/edit-widget.cpp
  ./src/push-widget.h
  ./src/push-widget.cpp
  ./src/json-util.hpp
  ./src/output-config.h
  ./src/output-config.cpp
  ./src/obs-properties-widget.h
  ./src/obs-properties-widget.cpp
  ./src/protocols.h
  ./src/protocols.cpp
  ./src/helpers.h
  ./src/helpers.cpp
  ./src/streamhub-launcher.h
  ./src/streamhub-launcher.cpp
  ./src/streamhub-node-provision.h
  ./src/streamhub-node-provision.cpp
  ./src/streamhub-chat-dock.h
  ./src/streamhub-chat-dock.cpp
  ./src/streamhub-chat-settings.h
  ./src/streamhub-chat-settings.cpp
  ./src/streamhub-chat-admin.h
  ./src/streamhub-chat-admin.cpp
  ./src/streamhub-control-dock.h
  ./src/streamhub-control-dock.cpp
  ./src/streamhub-youtube-direct.h
  ./src/streamhub-youtube-direct.cpp
  ./src/streamhub-platforms.h
  ./src/streamhub-platforms.cpp
  ./src/streamhub-theme-installer.h
  ./src/streamhub-theme-installer.cpp
  ./src/streamhub-native-theme.h
  ./src/streamhub-native-theme.cpp
  ./src/streamhub-bundle.h
  ./src/streamhub-bundle.cpp
  ./src/streamhub-updater.h
  ./src/streamhub-updater.cpp
  ./qrc/streamhub-data.qrc
  ./noise-suppressor/src/noise-suppressor-dsp.c
  ./noise-suppressor/src/noise-suppressor-dsp.h
  ./noise-suppressor/src/noise-suppressor-filter.c
)

# Build RNNoise into the main OBS module so the filter is available from the
# StreamHub DLL alone (no second plugin DLL or runtime RNNoise dependency).
set(RNNOISE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-32.2.1/plugins/obs-filters/rnnoise")
add_library(streamhub-rnnoise OBJECT
  "${RNNOISE_SOURCE_DIR}/src/celt_lpc.c"
  "${RNNOISE_SOURCE_DIR}/src/denoise.c"
  "${RNNOISE_SOURCE_DIR}/src/kiss_fft.c"
  "${RNNOISE_SOURCE_DIR}/src/pitch.c"
  "${RNNOISE_SOURCE_DIR}/src/rnn.c"
  "${RNNOISE_SOURCE_DIR}/src/rnn_data.c"
  "${RNNOISE_SOURCE_DIR}/src/rnn_reader.c"
)
target_include_directories(streamhub-rnnoise PUBLIC
  "${RNNOISE_SOURCE_DIR}/include"
  "${RNNOISE_SOURCE_DIR}/src"
)
target_compile_definitions(streamhub-rnnoise PUBLIC COMPILE_OPUS)
set_target_properties(streamhub-rnnoise PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED TRUE
  POSITION_INDEPENDENT_CODE TRUE
)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE $<TARGET_OBJECTS:streamhub-rnnoise>)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  ./noise-suppressor/src
  "${RNNOISE_SOURCE_DIR}/include"
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED TRUE)

include(CTest)
if(BUILD_TESTING)
  add_executable(streamhub-noise-suppressor-dsp-test
    noise-suppressor/tests/test_dsp.c
    noise-suppressor/src/noise-suppressor-dsp.c
    $<TARGET_OBJECTS:streamhub-rnnoise>
  )
  target_include_directories(streamhub-noise-suppressor-dsp-test PRIVATE
    noise-suppressor/src
    "${RNNOISE_SOURCE_DIR}/include"
  )
  set_target_properties(streamhub-noise-suppressor-dsp-test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED TRUE)
  if(NOT WIN32)
    target_link_libraries(streamhub-noise-suppressor-dsp-test PRIVATE m)
  endif()
  add_test(NAME streamhub-noise-suppressor-dsp COMMAND streamhub-noise-suppressor-dsp-test)
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  ./dep/nlohmann-json
  .deps/obs-studio-32.2.1/libobs
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(WIN32)
  add_executable(streamhub-updater-helper WIN32 tools/streamhub-updater-helper.cpp)
  target_compile_features(streamhub-updater-helper PRIVATE cxx_std_20)
  target_link_libraries(streamhub-updater-helper PRIVATE bcrypt shell32)
  set_target_properties(streamhub-updater-helper PROPERTIES OUTPUT_NAME "streamhub-updater-helper")
  add_dependencies(${CMAKE_PROJECT_NAME} streamhub-updater-helper)

  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:streamhub-updater-helper>"
            "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/streamhub-updater-helper.exe"
    VERBATIM
  )

  add_custom_command(
    TARGET streamhub-updater-helper
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:streamhub-updater-helper>"
            "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/streamhub-updater-helper.exe"
    VERBATIM
  )

  install(TARGETS streamhub-updater-helper RUNTIME DESTINATION "${_name}/bin/64bit")
endif()

# Helper is included in Windows plugin builds for release packaging.

###############################################################################
# End of project targets
###############################################################################
