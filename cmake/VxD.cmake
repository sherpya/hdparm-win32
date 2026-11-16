if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The optional VxD build currently requires a Linux host")
endif()
find_program(HDPARM_VXD_CLANG NAMES clang-19 clang REQUIRED)
find_program(HDPARM_VXD_NASM NAMES nasm REQUIRED)
find_program(HDPARM_VXD_LLD NAMES lld-link-19 lld-link REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(vxd_dir "${CMAKE_CURRENT_BINARY_DIR}/vxd")
set(vxd_src "${CMAKE_CURRENT_SOURCE_DIR}/win9x")
set(vxd_tools "${CMAKE_CURRENT_SOURCE_DIR}/tools/vxd")
add_custom_command(OUTPUT "${vxd_dir}/HDPARM.VXD"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${vxd_dir}"
    COMMAND "${HDPARM_VXD_NASM}" -f win32 "${vxd_src}/glue.asm" -o "${vxd_dir}/glue.obj"
    COMMAND "${HDPARM_VXD_CLANG}" --target=i386-pc-windows-msvc -march=i386
        -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-avx -mno-x87
        -ffreestanding -fno-stack-protector -fno-builtin -Os -Wall -Wextra -Werror
        -c "${vxd_src}/driver.c" -o "${vxd_dir}/driver.obj"
    COMMAND "${HDPARM_VXD_LLD}" /nologo /dll /noentry /nodefaultlib /machine:x86
        /base:0x10000000 /timestamp:0 /opt:noref /merge:.rdata=.text /merge:.data=.text
        /section:.text,RWE "/out:${vxd_dir}/hdparm.dll"
        "${vxd_dir}/glue.obj" "${vxd_dir}/driver.obj"
    COMMAND "${Python3_EXECUTABLE}" -I -B "${vxd_tools}/pe2le.py"
        "${vxd_dir}/hdparm.dll" "${vxd_dir}/HDPARM.VXD" --name=HDPARM
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_vxd.py" "${vxd_dir}/HDPARM.VXD"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_vxd.py" "${vxd_src}/driver.c" "${vxd_src}/glue.asm" "${vxd_src}/protocol.h"
        "${vxd_src}/ios_abi.h" "${vxd_tools}/pe2le.py" "${vxd_tools}/lewrite.py"
    VERBATIM)
add_custom_target(hdparm_vxd ALL DEPENDS "${vxd_dir}/HDPARM.VXD")
install(FILES "${vxd_dir}/HDPARM.VXD" DESTINATION "${CMAKE_INSTALL_BINDIR}")
