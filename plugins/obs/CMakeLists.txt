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
  ./src/app/pch.h
  ./src/app/obs-multi-rtmp.h
  ./src/app/obs-multi-rtmp.cpp
  ./src/streaming/edit-widget.h
  ./src/streaming/edit-widget.cpp
  ./src/streaming/push-widget.h
  ./src/streaming/push-widget.cpp
  ./src/core/json-util.hpp
  ./src/streaming/output-config.h
  ./src/streaming/output-config.cpp
  ./src/streaming/obs-properties-widget.h
  ./src/streaming/obs-properties-widget.cpp
  ./src/streaming/protocols.h
  ./src/streaming/protocols.cpp
  ./src/core/helpers.h
  ./src/core/helpers.cpp
  ./src/core/streamhub-launcher.h
  ./src/core/streamhub-launcher.cpp
  ./src/core/streamhub-node-provision.h
  ./src/core/streamhub-node-provision.cpp
  ./src/chat/streamhub-chat-dock.h
  ./src/chat/streamhub-chat-dock.cpp
  ./src/chat/streamhub-chat-settings.h
  ./src/chat/streamhub-chat-settings.cpp
  ./src/chat/streamhub-chat-admin.h
  ./src/chat/streamhub-chat-admin.cpp
  ./src/broadcast/streamhub-control-dock.h
  ./src/broadcast/streamhub-control-dock.cpp
  ./src/broadcast/streamhub-broadcast-request.h
  ./src/broadcast/streamhub-broadcast-request.cpp
  ./src/broadcast/streamhub-category-cover-service.h
  ./src/broadcast/streamhub-category-cover-service.cpp
  ./src/broadcast/streamhub-youtube-direct.h
  ./src/broadcast/streamhub-youtube-direct.cpp
  ./src/streaming/streamhub-platforms.h
  ./src/streaming/streamhub-platforms.cpp
  ./src/ui/streamhub-theme-installer.h
  ./src/ui/streamhub-theme-installer.cpp
  ./src/ui/streamhub-native-theme.h
  ./src/ui/streamhub-native-theme.cpp
  ./src/core/streamhub-bundle.h
  ./src/core/streamhub-bundle.cpp
  ./src/core/streamhub-updater.h
  ./src/core/streamhub-updater.cpp
  ./qrc/streamhub-data.qrc
  ../../libraries/noise-suppressor/src/noise-suppressor-dsp.c
  ../../libraries/noise-suppressor/src/noise-suppressor-dsp.h
  ../../libraries/noise-suppressor/src/noise-suppressor-filter.c
)

# Build RNNoise into the main OBS module so the filter is available from the
# StreamHub DLL alone (no second plugin DLL or runtime RNNoise dependency).
set(RNNOISE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/rnnoise")
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
  ./src/audio
  "${RNNOISE_SOURCE_DIR}/include"
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED TRUE)

include(CTest)
if(BUILD_TESTING)
  add_executable(streamhub-noise-suppressor-dsp-test
    ../../tests/audio/test_noise_suppressor_dsp.c
    ../../libraries/noise-suppressor/src/noise-suppressor-dsp.c
    $<TARGET_OBJECTS:streamhub-rnnoise>
  )
  target_include_directories(streamhub-noise-suppressor-dsp-test PRIVATE
    src/audio
    ../../libraries/noise-suppressor/src
    "${RNNOISE_SOURCE_DIR}/include"
  )
  set_target_properties(streamhub-noise-suppressor-dsp-test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED TRUE)
  if(NOT WIN32)
    target_link_libraries(streamhub-noise-suppressor-dsp-test PRIVATE m)
  endif()
  add_test(NAME streamhub-noise-suppressor-dsp COMMAND streamhub-noise-suppressor-dsp-test)
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  ./src
  ./src/app
  ./src/core
  ./src/streaming
  ./src/chat
  ./src/broadcast
  ./src/ui
  ./src/audio
  ../../third_party/nlohmann-json
  .deps/obs-studio-32.2.1/libobs
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(WIN32)
  add_executable(streamhub-updater-helper WIN32 ../../tools/streamhub-updater-helper.cpp)
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
