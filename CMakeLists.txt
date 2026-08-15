cmake_minimum_required(VERSION 3.24)
project(IrChip8 VERSION 1.1.0 LANGUAGES C CXX)

option(IRCHIP8_BUILD_APP "Build the SDL desktop emulator" ON)
option(IRCHIP8_BUILD_TESTS "Build IrChip8 core tests" ON)
option(IRCHIP8_BUILD_WEB "Build the browser WebAssembly frontend" OFF)

if(EMSCRIPTEN AND IRCHIP8_BUILD_WEB)
    set(IRCHIP8_BUILD_APP OFF CACHE BOOL "" FORCE)
endif()

add_library(irchip8_core STATIC
    src/chip8.cpp
)

target_include_directories(irchip8_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(irchip8_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(irchip8_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(irchip8_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EMSCRIPTEN)
    target_compile_options(irchip8_core PRIVATE -Oz -flto -fno-exceptions -fno-rtti)
endif()

if(IRCHIP8_BUILD_APP)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz
        URL_HASH SHA256=f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL3)

    add_executable(IrChip8
        src/main.cpp
    )
    target_link_libraries(IrChip8 PRIVATE irchip8_core SDL3::SDL3-static)
    target_compile_features(IrChip8 PRIVATE cxx_std_20)

    if(WIN32)
        set_target_properties(IrChip8 PROPERTIES WIN32_EXECUTABLE OFF)
    endif()

    install(TARGETS IrChip8 RUNTIME DESTINATION .)
endif()

if(EMSCRIPTEN AND IRCHIP8_BUILD_WEB)
    add_executable(irchip8_web
        web/wasm_bridge.cpp
    )
    target_link_libraries(irchip8_web PRIVATE irchip8_core)
    target_compile_features(irchip8_web PRIVATE cxx_std_20)
    target_compile_options(irchip8_web PRIVATE -Oz -flto -fno-exceptions -fno-rtti)
    set_target_properties(irchip8_web PROPERTIES OUTPUT_NAME "irchip8" SUFFIX ".js")

    target_link_options(irchip8_web PRIVATE
        -Oz
        -flto
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sEXPORT_NAME=createIrChip8Module"
        "-sENVIRONMENT=web"
        "-sFILESYSTEM=0"
        "-sALLOW_MEMORY_GROWTH=0"
        "-sINITIAL_MEMORY=2097152"
        "-sSTACK_SIZE=65536"
        "-sMALLOC=emmalloc"
        "-sASSERTIONS=0"
        "-sINVOKE_RUN=0"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free','_irchip8_probe_rom','_irchip8_probe_profile','_irchip8_probe_confidence','_irchip8_probe_superchip_hits','_irchip8_probe_vip_hits','_irchip8_probe_chip48_hits','_irchip8_load_rom','_irchip8_reset','_irchip8_cycle_n','_irchip8_tick_timers','_irchip8_set_key','_irchip8_framebuffer','_irchip8_display_width','_irchip8_display_height','_irchip8_draw_pending','_irchip8_clear_draw_pending','_irchip8_sound_active','_irchip8_halted','_irchip8_error','_irchip8_active_profile']"
    )
endif()

if(IRCHIP8_BUILD_TESTS)
    enable_testing()
    add_executable(irchip8_tests tests/chip8_tests.cpp)
    target_link_libraries(irchip8_tests PRIVATE irchip8_core)
    target_compile_features(irchip8_tests PRIVATE cxx_std_20)
    add_test(NAME irchip8_core COMMAND irchip8_tests)
endif()
