cmake_minimum_required(VERSION 3.13)
project(Jellyfin68K C)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/mactcp)

add_application(Hello68K
    hello.c
    hello.r
)

add_application(NetTest
    net_test.c
    net_test.r
    mactcp/TCPHi.c
    mactcp/TCPRoutines.c
)

add_application(NetProbe
    net_probe.c
    net_probe.r
    mactcp/TCPHi.c
    mactcp/TCPRoutines.c
)

add_application(MP3Test
    mp3_test.c
    mp3_test.r
)

add_application(LaunchJF
    launchjf.c
    launchjf.r
)

add_application(VolDump
    voldump.c
    voldump.r
)

add_application(Jellyfin68K
    jellyfin.c
    http.c
    json.c
    audio.c
    jellyfin.r
    mactcp/TCPHi.c
    mactcp/TCPRoutines.c
)

foreach(target Hello68K NetTest NetProbe MP3Test Jellyfin68K VolDump LaunchJF)
    set_target_properties(${target} PROPERTIES COMPILE_OPTIONS "-ffunction-sections;-fdata-sections")
    if(CMAKE_SYSTEM_NAME MATCHES Retro68)
        set_target_properties(${target} PROPERTIES
            LINK_FLAGS "-Wl,-gc-sections -Wl,--mac-single")
    else()
        set_target_properties(${target} PROPERTIES
            LINK_FLAGS "-Wl,-gc-sections")
    endif()
endforeach()

target_compile_options(NetTest PRIVATE -O2)


target_compile_options(Jellyfin68K PRIVATE -O3)
