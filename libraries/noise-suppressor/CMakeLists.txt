cmake_minimum_required(VERSION 3.28...3.30)

project(streamhub-noise-suppressor-tests VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# O supressor é incorporado à DLL principal do OBS. Esta pasta mantém a
# implementação reutilizável e o teste DSP; não gera uma segunda DLL.
set(RNNOISE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/rnnoise" CACHE PATH "Fonte RNNoise mantida no projeto")

add_library(streamhub-noise-rnnoise OBJECT
  "${RNNOISE_SOURCE_DIR}/src/celt_lpc.c"
  "${RNNOISE_SOURCE_DIR}/src/denoise.c"
  "${RNNOISE_SOURCE_DIR}/src/kiss_fft.c"
  "${RNNOISE_SOURCE_DIR}/src/pitch.c"
  "${RNNOISE_SOURCE_DIR}/src/rnn.c"
  "${RNNOISE_SOURCE_DIR}/src/rnn_data.c"
  "${RNNOISE_SOURCE_DIR}/src/rnn_reader.c"
)
target_include_directories(streamhub-noise-rnnoise PUBLIC
  "${RNNOISE_SOURCE_DIR}/include"
  "${RNNOISE_SOURCE_DIR}/src"
)
target_compile_definitions(streamhub-noise-rnnoise PUBLIC COMPILE_OPUS)
set_target_properties(streamhub-noise-rnnoise PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

include(CTest)
option(BUILD_TESTING "Build DSP tests" ON)
if(BUILD_TESTING)
  add_executable(streamhub-noise-suppressor-dsp-test
    tests/test_dsp.c
    src/noise-suppressor-dsp.c
    $<TARGET_OBJECTS:streamhub-noise-rnnoise>
  )
  target_include_directories(streamhub-noise-suppressor-dsp-test PRIVATE
    src
    "${RNNOISE_SOURCE_DIR}/include"
  )
  add_test(NAME streamhub-noise-suppressor-dsp COMMAND streamhub-noise-suppressor-dsp-test)
endif()
