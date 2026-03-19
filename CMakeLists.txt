cmake_minimum_required(VERSION 3.20)
project(CLI-MIDI VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(CLI-MIDI
    src/main.cpp
    src/parser/smf_parser.cpp
    src/parser/tempo_map.cpp
    src/engine/sequencer.cpp
    src/engine/midi_out.cpp
    src/ui/tui.cpp
)

target_include_directories(CLI-MIDI PRIVATE src)

if(WIN32)
    target_link_libraries(CLI-MIDI PRIVATE winmm)
    target_compile_definitions(CLI-MIDI PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
elseif(APPLE)
    find_library(COREMIDI_LIB CoreMIDI REQUIRED)
    find_library(COREFOUNDATION_LIB CoreFoundation REQUIRED)
    target_link_libraries(CLI-MIDI PRIVATE
        ${COREMIDI_LIB} ${COREFOUNDATION_LIB} pthread)
else()  # Linux x64 / ARM
    find_package(ALSA REQUIRED)
    target_link_libraries(CLI-MIDI PRIVATE ALSA::ALSA pthread)
endif()

if(MSVC)
    target_compile_options(CLI-MIDI PRIVATE /W4 /WX- /utf-8
        $<$<CONFIG:Release>:/O2 /GL>)
    target_link_options(CLI-MIDI PRIVATE
        $<$<CONFIG:Release>:/LTCG>)
else()
    target_compile_options(CLI-MIDI PRIVATE -Wall -Wextra
        $<$<CONFIG:Release>:-O3>)
endif()

set_target_properties(CLI-MIDI PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
