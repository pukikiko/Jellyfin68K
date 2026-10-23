cmake_minimum_required(VERSION 3.13)
project(Jellyfin68K C)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mactcp
)

add_application(Jellyfin68K
    src/jellyfin.c
    src/http.c
    src/json.c
    src/audio.c
    src/jellyfin.r
    src/mactcp/TCPHi.c
    src/mactcp/TCPRoutines.c
)

set_target_properties(Jellyfin68K PROPERTIES
    COMPILE_OPTIONS "-ffunction-sections;-fdata-sections")

if(CMAKE_SYSTEM_NAME MATCHES Retro68)
    set_target_properties(Jellyfin68K PROPERTIES
        LINK_FLAGS "-Wl,-gc-sections -Wl,--mac-single")
else()
    set_target_properties(Jellyfin68K PROPERTIES
        LINK_FLAGS "-Wl,-gc-sections")
endif()

target_compile_options(Jellyfin68K PRIVATE -O3)
