cmake_minimum_required(VERSION 3.15)

# Set the project name to your project name, my project isn't very descriptive
project(leandro_gui CXX C)
include(cmake/StandardProjectSettings.cmake)
include(cmake/PreventInSourceBuilds.cmake)
include(get_cpm.cmake)

set(CMAKE_CXX_STANDARD 20)
# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
  option(ENABLE_BUILD_WITH_TIME_TRACE "Enable -ftime-trace to generate time tracing .json files on clang" OFF)
  if(ENABLE_BUILD_WITH_TIME_TRACE)
    add_compile_definitions(project_options INTERFACE -ftime-trace)
  endif()
endif()

# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
add_library(project_warnings INTERFACE)

# standard compiler warnings
include(cmake/CompilerWarnings.cmake)
set_project_warnings(project_warnings)

# sanitizer options if supported by compiler
include(cmake/Sanitizers.cmake)
enable_sanitizers(project_options)


# allow for static analysis options
include(cmake/StaticAnalyzers.cmake)

option(BUILD_SHARED_LIBS "Enable compilation of shared libraries" OFF)
option(ENABLE_TESTING "Enable Test Builds" ON)
option(ENABLE_FUZZING "Enable Fuzzing Builds" OFF)

# Very basic PCH example
option(ENABLE_PCH "Enable Precompiled Headers" OFF)
if(ENABLE_PCH)
  # This sets a global PCH parameter, each project will build its own PCH, which is a good idea if any #define's change
  #
  # consider breaking this out per project as necessary
  target_precompile_headers(
    project_options
    INTERFACE
    <vector>
    <string>
    <map>
    <utility>)
endif()


if(ENABLE_TESTING)
  enable_testing()
  message("Building Tests. Be sure to check out test/constexpr_tests for constexpr testing")
  add_subdirectory(test)
endif()

if(ENABLE_FUZZING)
  message("Building Fuzz Tests, using fuzzing sanitizer https://www.llvm.org/docs/LibFuzzer.html")
  add_subdirectory(fuzz_test)
endif()

add_library(glad STATIC glade/src/glad.c)
target_include_directories(glad PUBLIC glade/include)

CPMAddPackage(
    NAME glfw
    GITHUB_REPOSITORY glfw/glfw
    GIT_TAG 3.3.2
    )
CPMAddPackage(
    NAME imgui
    GITHUB_REPOSITORY ocornut/imgui
    VERSION 1.79
    DOWNLOAD_ONLY TRUE
    )
if(imgui_ADDED)
    set(IMGUI_DIR ${CMAKE_CURRENT_BINARY_DIR}/myimgui)
    file(COPY ${imgui_SOURCE_DIR}/ DESTINATION ${IMGUI_DIR})
    file(COPY imconfig.h DESTINATION ${IMGUI_DIR})

    add_library(imgui STATIC
        ${IMGUI_DIR}/imconfig.h
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui.h
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_internal.h
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imstb_rectpack.h
        ${IMGUI_DIR}/imstb_textedit.h
        ${IMGUI_DIR}/imstb_truetype.h
        )
    target_include_directories(imgui PUBLIC ${IMGUI_DIR})
    target_link_libraries(imgui PUBLIC project_options)

    add_library(imgui_sdlopengl STATIC
        ${IMGUI_DIR}/examples/imgui_impl_opengl3.cpp
        ${IMGUI_DIR}/examples/imgui_impl_opengl3.h
        ${IMGUI_DIR}/examples/imgui_impl_sdl.cpp
        ${IMGUI_DIR}/examples/imgui_impl_sdl.h
        )
    target_link_libraries(imgui_sdlopengl PUBLIC imgui glad SDL2-static project_options)
    target_include_directories(imgui_sdlopengl PUBLIC ${IMGUI_DIR}/examples)

    add_library(imgui_glfwopengl STATIC
        ${IMGUI_DIR}/examples/imgui_impl_opengl3.cpp
        ${IMGUI_DIR}/examples/imgui_impl_opengl3.h
        ${IMGUI_DIR}/examples/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/examples/imgui_impl_glfw.h
        )
    target_link_libraries(imgui_glfwopengl PUBLIC imgui glad glfw project_options)
    target_include_directories(imgui_glfwopengl PUBLIC ${IMGUI_DIR}/examples)
endif()
CPMAddPackage(
    NAME implot
    GITHUB_REPOSITORY epezent/implot
    VERSION 0.8
    DOWNLOAD_ONLY TRUE
    )
if(implot_ADDED)
    add_library(implot STATIC
        ${implot_SOURCE_DIR}/implot.h
        ${implot_SOURCE_DIR}/implot_internal.h
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
        )
    target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
    target_link_libraries(implot PUBLIC imgui project_options)
endif()

CPMAddPackage(
    NAME fmt
    GITHUB_REPOSITORY fmtlib/fmt
    GIT_TAG 7.1.2
    )
CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    VERSION 1.8.1
    OPTIONS
        SPDLOG_FMT_EXTERNAL
    )
CPMAddPackage(
    NAME magic_enum
    GITHUB_REPOSITORY Neargye/magic_enum
    VERSION 0.6.6
    )
CPMAddPackage(
    NAME libserialport
    URL http://sigrok.org/download/source/libserialport/libserialport-0.1.1.tar.gz
    VERSION 0.1.1
    DOWNLOAD_ONLY TRUE
    )
if(libserialport_ADDED)
    set(LSER ${CMAKE_CURRENT_BINARY_DIR}/libserialport_b)
    file(COPY ${libserialport_SOURCE_DIR}/ DESTINATION ${LSER})

    if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        include(CheckSymbolExists)
        include(CheckTypeSize)
        check_symbol_exists(BOTHER "linux/termios.h" HAVE_DECL_BOTHER)
        set(CMAKE_EXTRA_INCLUDE_FILES linux/serial.h)
        check_type_size("struct serial_struct" STRUCT_SERIAL_STRUCT LANGUAGE C)
        set(CMAKE_EXTRA_INCLUDE_FILES linux/termios.h)
        check_type_size("struct termios2" STRUCT_TERMIOS2 LANGUAGE C)
        check_type_size("((struct termios2*)0)->c_ispeed" STRUCT_TERMIOS2_C_ISPEED LANGUAGE C)
        check_type_size("((struct termios2*)0)->c_ospeed" STRUCT_TERMIOS2_C_OSPEED LANGUAGE C)
        check_type_size("((struct termios*)0)->c_ispeed" STRUCT_TERMIOS_C_ISPEED LANGUAGE C)
        check_type_size("((struct termios*)0)->c_ospeed" STRUCT_TERMIOS_C_OSPEED LANGUAGE C)
        check_type_size("struct termiox" STRUCT_TERMIOX LANGUAGE C)
        set(CMAKE_EXTRA_INCLUDE_FILES)

    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    endif()

    set(SP_PACKAGE_VERSION_MAJOR "0")
    set(SP_PACKAGE_VERSION_MINOR 1)
    set(SP_PACKAGE_VERSION_MICRO 1)
    set(SP_PACKAGE_VERSION_STRING "${SP_PACKAGE_VERSION_MAJOR}.${SP_PACKAGE_VERSION_MINOR}.${SP_PACKAGE_VERSION_MICRO}")
    set(SP_LIB_VERSION_CURRENT 1)
    set(SP_LIB_VERSION_REVISION 0)
    set(SP_LIB_VERSION_AGE 1)
    set(SP_LIB_VERSION_STRING "${SP_LIB_VERSION_CURRENT}:${SP_LIB_VERSION_REVISION}:${SP_LIB_VERSION_AGE}")

    configure_file(my_libserialport/config.h.in ${LSER}/config.h @ONLY)
    configure_file(my_libserialport/libserialport.h.in ${LSER}/libserialport.h @ONLY)

    add_library(serialport STATIC
        ${LSER}/config.h
        ${LSER}/serialport.c
        ${LSER}/libserialport.h
        )
    target_include_directories(serialport PUBLIC ${LSER})


    if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        target_sources(serialport PUBLIC ${LSER}/linux.c ${LSER}/linux_termios.c)
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
        target_sources(serialport PUBLIC ${LSER}/windows.c)
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_sources(serialport PUBLIC ${LSER}/macosx.c)
    endif()

endif()

add_executable(leandro_gui main.cpp)
target_link_libraries(leandro_gui PUBLIC
    project_options
    project_warnings
    serialport
    spdlog::spdlog
    fmt::fmt
    magic_enum::magic_enum
    imgui_glfwopengl
    implot
    )
target_compile_features(leandro_gui PUBLIC cxx_std_20)
target_compile_definitions(leandro_gui PUBLIC SPDLOG_FMT_EXTERNAL)

option(ENABLE_UNITY "Enable Unity builds of projects" OFF)
if(ENABLE_UNITY)
  # Add for any project you want to apply unity builds for
  set_target_properties(leandro_gui PROPERTIES UNITY_BUILD ON)
endif()
