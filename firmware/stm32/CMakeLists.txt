# ============================================================================
#  STM32F103C8T6（Blue Pill）+ ESP8266-01S + 按键 —— 裸机初始工程
#
#  构建：CMake + Ninja + arm-none-eabi-gcc（STM32CubeCLT 自带）
#  调试/烧录：WCH-Link（SWD，CLK/DIO 两线）→ OpenOCD
#  产物：build/out/*.elf/.hex/.bin
#
#  CLion 直接打开本目录即可（CMAKE_TOOLCHAIN_FILE 已在 CMakePresets.json 里）
#  命令行：
#     cmake --preset Debug && cmake --build --preset Debug
# ============================================================================
cmake_minimum_required(VERSION 3.20)

project(STM32F103C8_ESP8266_Init
        DESCRIPTION "STM32F103C8T6 + ESP8266 (AT) + key, bare-metal, WCH-Link SWD"
        LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)                     # gnu11
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug / Release" FORCE)
endif()

# Release 默认 -O3 会比 -Os 大不少，这里统一用 -Os 控制体积
set(CMAKE_C_FLAGS_RELEASE "-Os -g" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_DEBUG   "-Og -g3 -gdwarf-2" CACHE STRING "" FORCE)

# ---------------------------------------------------------------------------
# 芯片参数：STM32F103C8T6 = Cortex-M3，64KB Flash / 20KB RAM
# ---------------------------------------------------------------------------
set(MCU_FLAGS -mcpu=cortex-m3 -mthumb)

set(STARTUP_FILE  ${CMAKE_CURRENT_LIST_DIR}/cmake/startup_stm32f103xb.s)
set(LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/cmake/STM32F103C8Tx_FLASH.ld)

# 启动文件含条件汇编，需要过 C 预处理器
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -x assembler-with-cpp")

# ---------------------------------------------------------------------------
# 源码与头文件
# ---------------------------------------------------------------------------
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_LIST_DIR}/App/Src/*.c")
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_LIST_DIR}/Core/Src/*.c")

set(INCLUDES
    "${CMAKE_CURRENT_LIST_DIR}/App/Inc"
    "${CMAKE_CURRENT_LIST_DIR}/Core/Inc")

# ---------------------------------------------------------------------------
# 编译 / 链接选项
#   -ffunction-sections -fdata-sections + --gc-sections：丢掉没用的函数
#   nano.specs：轻量 newlib（相当于 Keil 的 MicroLIB）
#   nosys.specs：关闭半主机
# ---------------------------------------------------------------------------
set(COMMON_COMPILE_OPTS
    ${MCU_FLAGS}
    -Wall
    -fdata-sections -ffunction-sections
    -fno-common -fno-strict-aliasing
    -ffreestanding
    --specs=nano.specs)

set(COMMON_LINK_OPTS
    ${MCU_FLAGS}
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--no-warn-rwx-segments
    -Wl,--print-memory-usage
    -T${LINKER_SCRIPT})

# map 目录要先建好（ld 不会自己创建路径）
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/map")

add_executable(${PROJECT_NAME} ${APP_SOURCES} ${CORE_SOURCES} "${STARTUP_FILE}")
set_source_files_properties("${STARTUP_FILE}" PROPERTIES LANGUAGE ASM)

target_include_directories(${PROJECT_NAME} PRIVATE ${INCLUDES})
target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:C>:${COMMON_COMPILE_OPTS}>)
target_link_options(${PROJECT_NAME} PRIVATE
    ${COMMON_LINK_OPTS}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/map/${PROJECT_NAME}.map,--cref)

# 调试符号：配合 Cortex-Debug + OpenOCD 使用
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}
    ADDITIONAL_CLEAN_FILES "${CMAKE_CURRENT_BINARY_DIR}/out"
    LINK_DEPENDS "${LINKER_SCRIPT};${STARTUP_FILE}")

# 链接后自动产出 hex / bin（hex 给 STM32CubeProgrammer，bin 给 FlyMcu）
set(OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/out")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}"
    COMMAND ${CMAKE_OBJCOPY} -O ihex   "$<TARGET_FILE:${PROJECT_NAME}>" "${OUT_DIR}/${PROJECT_NAME}.hex"
    COMMAND ${CMAKE_OBJCOPY} -O binary "$<TARGET_FILE:${PROJECT_NAME}>" "${OUT_DIR}/${PROJECT_NAME}.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:${PROJECT_NAME}>"
    COMMENT "生成 hex / bin -> ${OUT_DIR}")

# ---------------------------------------------------------------------------
# 构建 → 自动烧录（WCH-Link / OpenOCD）
#   在 CLion 里点 🔨 构建，链接完自动把 hex 烧进芯片并复位运行。
#   没插调试器/没接线时只会在最后打印一条提示，不影响编译本身。
#   想关掉：cmake 时加 -DENABLE_AUTO_FLASH=OFF
# ---------------------------------------------------------------------------
set(OPENOCD_EXE    "C:/Users/lenovo/.eide/tools/openocd_7a1adfbec_mingw32/bin/openocd.exe" CACHE FILEPATH "OpenOCD 路径")
set(OPENOCD_SCRIPT "C:/Users/lenovo/.eide/tools/openocd_7a1adfbec_mingw32/share/openocd/scripts" CACHE PATH "OpenOCD scripts 目录")
option(ENABLE_AUTO_FLASH "构建完成后自动烧录（需要 WCH-Link 在线）" ON)

if(ENABLE_AUTO_FLASH AND EXISTS "${OPENOCD_EXE}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${OPENOCD_EXE}" -s "${OPENOCD_SCRIPT}"
                -f interface/cmsis-dap.cfg
                -f target/stm32f1x.cfg
                -c "program ${OUT_DIR}/${PROJECT_NAME}.hex verify reset exit"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT ">>> 自动烧录 (WCH-Link SWD) <<<"
        VERBATIM)
elseif(ENABLE_AUTO_FLASH)
    message(STATUS "[auto-flash] 未找到 OpenOCD，跳过自动烧录配置")
endif()

# ---------------------------------------------------------------------------
# 独立烧录目标：不依赖代码是否有改动，随时强制重烧一次
#   CLion 运行配置里选 flash，点 🔨 即可；命令行：cmake --build ... --target flash
# ---------------------------------------------------------------------------
add_custom_target(flash
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_CURRENT_BINARY_DIR}" --target ${PROJECT_NAME}
    COMMAND "${OPENOCD_EXE}" -s "${OPENOCD_SCRIPT}"
            -f interface/cmsis-dap.cfg
            -f target/stm32f1x.cfg
            -c "program ${OUT_DIR}/${PROJECT_NAME}.hex verify reset exit"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT ">>> 强制烧录 (WCH-Link SWD) <<<"
    VERBATIM)

message(STATUS "STM32F103C8T6 工程配置完成：Flash 64K / RAM 20K，SWD = WCH-Link(CLK/DIO)")
