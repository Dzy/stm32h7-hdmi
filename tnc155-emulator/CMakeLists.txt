cmake_minimum_required(VERSION 3.20)
project(tnc155_emulator VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
endif()

set(TNC155_ROM_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/roms"
    CACHE PATH "Directory containing the TNC155 P1/P2/P3/P4/P5/P6 ROM images")
set(TNC155_ROM5_FILE
    "${TNC155_ROM_DIR}/2340005E_nr5.bin"
    CACHE FILEPATH "Verified IC-P5 / 2340005E ROM image")
set(TNC155_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(TNC155_ROM_HEADER "${TNC155_GENERATED_DIR}/tnc155_rom_images.h")
set(TNC155B_DEFAULT_NVRAM "${CMAKE_CURRENT_SOURCE_DIR}/tnc155b-user-ram.bin")
set(TNC155Q_DEFAULT_NVRAM "${CMAKE_CURRENT_SOURCE_DIR}/tnc155q-user-ram.bin")
set(TNC155_NVRAM_HEADER "${TNC155_GENERATED_DIR}/tnc155_default_user_ram.h")
set(TNC155B_RUNTIME_NVRAM "${CMAKE_CURRENT_BINARY_DIR}/tnc155b-user-ram.bin")
set(TNC155Q_RUNTIME_NVRAM "${CMAKE_CURRENT_BINARY_DIR}/tnc155q-user-ram.bin")
set(TNC155FRANK_RUNTIME_NVRAM "${CMAKE_CURRENT_BINARY_DIR}/tnc155frank-user-ram.bin")

set(TNC155_ROM_FILES
    "${TNC155_ROM_DIR}/2340201A_nr1.rom"
    "${TNC155_ROM_DIR}/2340202C_nr2.rom"
    "${TNC155_ROM_DIR}/2340083E_nr3_finnish.rom"
    "${TNC155_ROM_DIR}/2340204D_nr4.rom"
    "${TNC155_ROM5_FILE}"
    "${TNC155_ROM_DIR}/2346166A_nr6.rom")
list(APPEND TNC155_ROM_FILES
    "${TNC155_ROM_DIR}/Heidenhain-Nr1-2340003MNr.1.BIN"
    "${TNC155_ROM_DIR}/Heidenhain-Nr2-2340003MNr.2.BIN"
    "${TNC155_ROM_DIR}/Heidenhain-Nr3-2340003MNr.3.BIN"
    "${TNC155_ROM_DIR}/Heidenhain-Nr4-2340003MNr.4.BIN"
    "${TNC155_ROM_DIR}/Heidenhain-Nr5-2340003MNr.5.BIN"
    "${TNC155_ROM_DIR}/Heidenhain-Nr6-WF5-23496BFDIPLC.BIN"
    "${TNC155_ROM_DIR}/TNC155FRANK-Nr6-Q-PLC23460102.BIN")

add_custom_command(
    OUTPUT "${TNC155_ROM_HEADER}"
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_roms.py"
            "${TNC155_ROM_DIR}"
            "${TNC155_ROM_HEADER}"
            --p5 "${TNC155_ROM5_FILE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_roms.py" ${TNC155_ROM_FILES}
    VERBATIM)

add_custom_command(
    OUTPUT "${TNC155_NVRAM_HEADER}"
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_nvram.py"
            "${TNC155B_DEFAULT_NVRAM}"
            "${TNC155Q_DEFAULT_NVRAM}"
            "${TNC155_NVRAM_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_nvram.py"
            "${TNC155B_DEFAULT_NVRAM}"
            "${TNC155Q_DEFAULT_NVRAM}"
    VERBATIM)

add_custom_target(tnc155_runtime_nvram ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${TNC155B_DEFAULT_NVRAM}" "${TNC155B_RUNTIME_NVRAM}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${TNC155Q_DEFAULT_NVRAM}" "${TNC155Q_RUNTIME_NVRAM}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${TNC155Q_DEFAULT_NVRAM}" "${TNC155FRANK_RUNTIME_NVRAM}"
    DEPENDS "${TNC155B_DEFAULT_NVRAM}" "${TNC155Q_DEFAULT_NVRAM}"
    COMMENT "Refreshing known-good TNC155B/Q/FRANK user RAM images"
    VERBATIM)

add_library(tnc155_roms STATIC src/roms.c "${TNC155_ROM_HEADER}")
target_include_directories(tnc155_roms
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${TNC155_GENERATED_DIR}")
target_compile_options(tnc155_roms PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tms9995 STATIC src/cpu/tms9995.c)
target_include_directories(tms9995 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(tms9995 PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tnc155_plc STATIC src/plc/plc.c)
target_include_directories(tnc155_plc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(tnc155_plc PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tnc155_plc_io STATIC src/plc/plc_io.c)
target_include_directories(tnc155_plc_io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(tnc155_plc_io PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tnc155_io_backplane STATIC src/io/io_backplane.c)
target_include_directories(tnc155_io_backplane PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(tnc155_io_backplane PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tnc155_i8279 STATIC src/main/i8279.c)
target_include_directories(tnc155_i8279 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(tnc155_i8279 PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tnc155_tms9902 STATIC src/serial/tms9902.c)
target_include_directories(tnc155_tms9902 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(tnc155_tms9902 PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tnc155_main_mapper STATIC src/main/main_mapper.c)
target_include_directories(tnc155_main_mapper PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(tnc155_main_mapper PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tnc155_mainboard STATIC src/main/mainboard.c)
target_include_directories(tnc155_mainboard PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(tnc155_mainboard PUBLIC tms9995 tnc155_roms tnc155_plc tnc155_plc_io tnc155_io_backplane tnc155_i8279 tnc155_tms9902 tnc155_main_mapper)
target_compile_options(tnc155_mainboard PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tnc155_clp_board STATIC src/clp/clp_board.c src/clp/upd7220.c)
target_include_directories(tnc155_clp_board PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(tnc155_clp_board PUBLIC tms9995 tnc155_roms tnc155_tms9902)
target_compile_options(tnc155_clp_board PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tnc155_machine STATIC src/machine/machine.c)
target_include_directories(tnc155_machine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(tnc155_machine PUBLIC tnc155_mainboard tnc155_clp_board)
target_compile_options(tnc155_machine PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tnc155_video STATIC src/clp/video.c)
target_include_directories(tnc155_video PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(tnc155_video PUBLIC tnc155_machine)
target_compile_options(tnc155_video PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(tnc155_serial_keyboard STATIC
    src/frontend/serial_keyboard.c
    src/frontend/serial_keyboard_protocol.c)
target_include_directories(tnc155_serial_keyboard PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(tnc155_serial_keyboard PUBLIC tnc155_i8279)
target_compile_options(tnc155_serial_keyboard PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(SDL2_FOUND)
    add_executable(tnc155 src/frontend/sdl_main.c "${TNC155_NVRAM_HEADER}")
    add_dependencies(tnc155 tnc155_runtime_nvram)
    target_include_directories(tnc155 PRIVATE "${TNC155_GENERATED_DIR}")
    target_link_libraries(tnc155 PRIVATE tnc155_video tnc155_serial_keyboard PkgConfig::SDL2)
    target_compile_options(tnc155 PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    message(STATUS "SDL2 not found: graphical tnc155 frontend will not be built")
endif()

add_executable(tnc155-headless src/frontend/machine_main.c)
target_link_libraries(tnc155-headless PRIVATE tnc155_machine)
target_compile_options(tnc155-headless PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(tnc155-mp-seed tools/seed_machine_parameters.c)
target_link_libraries(tnc155-mp-seed PRIVATE tnc155_machine tnc155_video)
target_compile_options(tnc155-mp-seed PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
# Regression programs use assert() as their test oracle. Keep assertions
# active even when the emulator itself is configured as Release.
add_compile_options(-UNDEBUG)
add_executable(test_roms tests/test_roms.c)
target_link_libraries(test_roms PRIVATE tnc155_roms)
target_compile_options(test_roms PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME embedded_roms COMMAND test_roms)

add_executable(test_tms9995 tests/test_tms9995.c)
target_link_libraries(test_tms9995 PRIVATE tms9995)
target_compile_options(test_tms9995 PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME tms9995_core COMMAND test_tms9995)

add_executable(test_plc tests/test_plc.c)
target_link_libraries(test_plc PRIVATE tnc155_plc tnc155_roms)
target_compile_options(test_plc PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME plc_core COMMAND test_plc)

add_executable(test_plc_io tests/test_plc_io.c)
target_link_libraries(test_plc_io PRIVATE tnc155_plc_io)
target_compile_options(test_plc_io PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME plc_io_board COMMAND test_plc_io)

add_executable(test_io_backplane tests/test_io_backplane.c)
target_link_libraries(test_io_backplane PRIVATE tnc155_io_backplane)
target_compile_options(test_io_backplane PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME io_backplane COMMAND test_io_backplane)

add_executable(test_i8279 tests/test_i8279.c)
target_link_libraries(test_i8279 PRIVATE tnc155_i8279)
target_compile_options(test_i8279 PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME i8279_device COMMAND test_i8279)

add_executable(test_serial_keyboard tests/test_serial_keyboard.c)
target_link_libraries(test_serial_keyboard PRIVATE tnc155_serial_keyboard tnc155_i8279)
target_compile_options(test_serial_keyboard PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME serial_keyboard_protocol COMMAND test_serial_keyboard)

add_executable(test_tms9902 tests/test_tms9902.c)
target_link_libraries(test_tms9902 PRIVATE tnc155_tms9902)
target_compile_options(test_tms9902 PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME tms9902_device COMMAND test_tms9902)

add_executable(test_main_mapper tests/test_main_mapper.c)
target_link_libraries(test_main_mapper PRIVATE tnc155_main_mapper)
target_compile_options(test_main_mapper PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME main_mapper COMMAND test_main_mapper)

add_executable(test_mainboard tests/test_mainboard.c)
target_link_libraries(test_mainboard PRIVATE tnc155_mainboard)
target_compile_options(test_mainboard PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME mainboard_memory COMMAND test_mainboard)

add_executable(test_clp_board tests/test_clp_board.c)
target_link_libraries(test_clp_board PRIVATE tnc155_clp_board)
target_compile_options(test_clp_board PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME clp_board_memory COMMAND test_clp_board)

add_executable(test_machine tests/test_machine.c)
target_link_libraries(test_machine PRIVATE tnc155_machine)
target_compile_options(test_machine PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME combined_machine COMMAND test_machine)

add_executable(test_upd7220 tests/test_upd7220.c)
target_link_libraries(test_upd7220 PRIVATE tnc155_clp_board)
target_compile_options(test_upd7220 PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME upd7220_device COMMAND test_upd7220)

add_executable(test_video tests/test_video.c)
target_link_libraries(test_video PRIVATE tnc155_video tnc155_machine)
add_test(NAME character_video_attributes COMMAND test_video)
