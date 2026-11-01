# ============================================================================
#  arm-none-eabi-gcc 交叉编译工具链（STM32F103C8T6 / Cortex-M3 裸机）
#
#  自动按顺序找工具链，找到即用：
#    1) CMake 变量 -DTOOLCHAIN_ROOT=...
#    2) STM32CubeCLT 自带 GNU-tools-for-STM32（本机 E:/ST/STM32CubeCLT_1.18.0）
#    3) EIDE 自带的 gcc_arm（C:/Users/<你>/.eide/tools/gcc_arm）
#    4) PATH 里的 arm-none-eabi-gcc
# ============================================================================
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 裸机没有 _start，试链接必然失败 → 只做静态库级别的编译器探测
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT DEFINED TOOLCHAIN_ROOT)
    file(GLOB _eide_home "$ENV{USERPROFILE}/.eide/tools/gcc_arm")
    set(_candidates
        "E:/ST/STM32CubeCLT_1.18.0/GNU-tools-for-STM32"
        "D:/ST/STM32CubeCLT_1.18.0/GNU-tools-for-STM32"
        "C:/ST/STM32CubeCLT_1.18.0/GNU-tools-for-STM32"
        "$ENV{STM32CUBECLT_PATH}/GNU-tools-for-STM32"
        "$ENV{LOCALAPPDATA}/Programs/xpack-arm-none-eabi-gcc"
        ${_eide_home}
    )
    foreach(_root IN LISTS _candidates)
        if(_root AND EXISTS "${_root}/bin/arm-none-eabi-gcc.exe")
            set(TOOLCHAIN_ROOT "${_root}")
            break()
        endif()
    endforeach()
endif()

if(TOOLCHAIN_ROOT AND EXISTS "${TOOLCHAIN_ROOT}/bin/arm-none-eabi-gcc.exe")
    set(_P "${TOOLCHAIN_ROOT}/bin/arm-none-eabi-")
    message(STATUS "[toolchain] ${TOOLCHAIN_ROOT}/bin/arm-none-eabi-gcc")
else()
    set(_P "arm-none-eabi-")
    message(STATUS "[toolchain] 使用 PATH 中的 arm-none-eabi-gcc")
endif()

set(CMAKE_C_COMPILER   "${_P}gcc")
set(CMAKE_CXX_COMPILER "${_P}g++")
set(CMAKE_ASM_COMPILER "${_P}gcc")
set(CMAKE_OBJCOPY      "${_P}objcopy")
set(CMAKE_OBJDUMP      "${_P}objdump")
set(CMAKE_SIZE         "${_P}size")
set(CMAKE_AR           "${_P}ar")
set(CMAKE_RANLIB       "${_P}ranlib")
set(CMAKE_GDB          "${_P}gdb")

if(CMAKE_HOST_WIN32 AND TOOLCHAIN_ROOT)
    foreach(_v CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_ASM_COMPILER
               CMAKE_OBJCOPY CMAKE_OBJDUMP CMAKE_SIZE CMAKE_AR CMAKE_RANLIB CMAKE_GDB)
        set(${_v} "${${_v}}.exe")
    endforeach()
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_C_FLAGS_INIT   "-mcpu=cortex-m3 -mthumb")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-m3 -mthumb")
