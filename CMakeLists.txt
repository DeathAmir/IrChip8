cmake_minimum_required(VERSION 3.24)
project(IrChip8 VERSION 1.2.0 LANGUAGES C CXX)

option(IRCHIP8_BUILD_APP "Build the SDL3 desktop emulator" ON)
option(IRCHIP8_BUILD_TESTS "Build IrChip8 core tests" ON)
option(IRCHIP8_BUILD_WEB "Build the SDL3 WebAssembly frontend" OFF)

if(EMSCRIPTEN AND IRCHIP8_BUILD_WEB)
    set(IRCHIP8_BUILD_APP OFF CACHE BOOL "" FORCE)
endif()

add_library(irchip8_core STATIC src/chip8.cpp)
target_include_directories(irchip8_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(irchip8_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(irchip8_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(irchip8_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(EMSCRIPTEN)
    target_compile_options(irchip8_core PRIVATE -Oz -flto -fno-exceptions -fno-rtti)
endif()

if(IRCHIP8_BUILD_APP OR (EMSCRIPTEN AND IRCHIP8_BUILD_WEB))
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
endif()

if(IRCHIP8_BUILD_APP)
    add_executable(IrChip8 src/main.cpp)
    target_link_libraries(IrChip8 PRIVATE irchip8_core SDL3::SDL3)
    target_compile_features(IrChip8 PRIVATE cxx_std_20)
    if(WIN32)
        set_target_properties(IrChip8 PROPERTIES WIN32_EXECUTABLE OFF)
    endif()
    install(TARGETS IrChip8 RUNTIME DESTINATION .)
endif()

if(EMSCRIPTEN AND IRCHIP8_BUILD_WEB)
    function(ir8_web_target target output single_file)
        add_executable(${target} web/sdl_web.cpp)
        target_link_libraries(${target} PRIVATE irchip8_core SDL3::SDL3)
        target_compile_features(${target} PRIVATE cxx_std_20)
        target_compile_options(${target} PRIVATE -Oz -flto -fno-exceptions -fno-rtti)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output}" SUFFIX ".html")
        target_link_options(${target} PRIVATE
            -Oz -flto
            "-sENVIRONMENT=web"
            "-sALLOW_MEMORY_GROWTH=0"
            "-sINITIAL_MEMORY=16777216"
            "-sSTACK_SIZE=131072"
            "-sMALLOC=emmalloc"
            "-sASSERTIONS=0"
            "-sFILESYSTEM=0"
            "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_ir8_alloc','_ir8_free','_ir8_rom','_ir8_run','_ir8_reset','_ir8_hz','_ir8_profile','_ir8_is_running']"
            "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        )
        if(single_file)
            target_link_options(${target} PRIVATE "-sSINGLE_FILE=1" "-sSINGLE_FILE_BINARY_ENCODE=0")
        endif()
    endfunction()

    ir8_web_target(IrChip8Web index FALSE)
    ir8_web_target(IrChip8Standalone IrChip8-Standalone TRUE)
endif()

if(IRCHIP8_BUILD_TESTS)
    enable_testing()
    add_executable(irchip8_tests tests/chip8_tests.cpp)
    target_link_libraries(irchip8_tests PRIVATE irchip8_core)
    target_compile_features(irchip8_tests PRIVATE cxx_std_20)
    add_test(NAME irchip8_core COMMAND irchip8_tests)
endif()
