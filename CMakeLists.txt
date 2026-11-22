cmake_minimum_required(VERSION 3.16)

project(hdparm VERSION 9.65 LANGUAGES C)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "hdparm supports Linux and Windows")
endif()

include(GNUInstallDirs)

option(HDPARM_STATIC "Link hdparm statically" OFF)

add_executable(hdparm
    hdparm.c
    identify.c
    sgio.c
    fwdownload.c
    wdidle3.c
)

set_target_properties(hdparm PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS YES)

if(WIN32)
    set(windows_backends win32/api.c win32/device.c win32/nt.c win32/sat.c
        win32/win9x.c win32/aspi.c win32/ios.c)
    function(windows_profile target)
        target_compile_definitions(${target} PRIVATE WINVER=0x0400 _WIN32_WINNT=0x0400)
        if(MINGW)
            target_compile_definitions(${target} PRIVATE __USE_MINGW_ANSI_STDIO=1)
            target_link_options(${target} PRIVATE -static-libgcc -Wl,--no-insert-timestamp)
            if(CMAKE_SIZEOF_VOID_P EQUAL 4)
                target_link_options(${target} PRIVATE
                    -Wl,--major-subsystem-version,4,--minor-subsystem-version,0)
            endif()
        endif()
        if(MSVC)
            set_property(TARGET ${target} PROPERTY C_STANDARD 17)
            target_compile_definitions(${target} PRIVATE
                _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
            target_compile_options(${target} PRIVATE /std:c17 /W3)
        endif()
    endfunction()
    target_sources(hdparm PRIVATE ${windows_backends} win32/benchmark.c win32/unsupported.c)
    windows_profile(hdparm)
else()
    target_sources(hdparm PRIVATE sysfs.c geom.c fallocate.c fibmap.c dvdspeed.c apt.c)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hdparm PRIVATE
        -Wall -Wextra -Wbad-function-cast -Wcast-align -Wpointer-arith
        -Wcast-qual -Wshadow -Wstrict-prototypes -Wmissing-prototypes
        -Wmissing-declarations -Wwrite-strings -Waggregate-return
        -Wnested-externs -Wtrigraphs
    )
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(hdparm PRIVATE -fkeep-inline-functions)
endif()

if(HDPARM_STATIC)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "HDPARM_STATIC requires GCC or Clang")
    endif()
    target_link_options(hdparm PRIVATE -static)
endif()

if(WIN32)
    install(TARGETS hdparm RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
else()
    install(TARGETS hdparm RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}")
endif()
install(FILES hdparm.8 DESTINATION "${CMAKE_INSTALL_MANDIR}/man8")

include(CTest)
if(BUILD_TESTING AND WIN32)
    function(windows_test name)
        add_executable(${name} ${ARGN})
        windows_profile(${name})
        target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        if(MSVC)
            target_compile_options(${name} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${name} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${name} COMMAND ${name})
    endfunction()
    windows_test(windows_tests tests/windows.c ${windows_backends} win32/benchmark.c)
    windows_test(win9x_tests tests/win9x.c ${windows_backends} win32/benchmark.c)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        windows_test(ios_tests tests/ios.c win9x/driver.c)
    endif()
endif()

option(HDPARM_BUILD_VXD "Build the optional Win9x IOS bridge using Clang, NASM and LLD" OFF)
if(HDPARM_BUILD_VXD)
    include(cmake/VxD.cmake)
endif()
