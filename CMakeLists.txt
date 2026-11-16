cmake_minimum_required(VERSION 3.16)

project(hdparm VERSION 9.65 LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "hdparm currently supports Linux only")
endif()

include(GNUInstallDirs)

option(HDPARM_STATIC "Link hdparm statically" OFF)

add_executable(hdparm
    hdparm.c
    identify.c
    sgio.c
    sysfs.c
    geom.c
    fallocate.c
    fibmap.c
    fwdownload.c
    dvdspeed.c
    wdidle3.c
    apt.c
)

set_target_properties(hdparm PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
)

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

install(TARGETS hdparm RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}")
install(FILES hdparm.8 DESTINATION "${CMAKE_INSTALL_MANDIR}/man8")
