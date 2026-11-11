cmake_minimum_required(VERSION 3.20)
project(create-alias LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
    add_compile_options(/utf-8)
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" PLUGIN_VERSION)
if(NOT PLUGIN_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must be a numeric semantic version")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" PLUGIN_MANIFEST)
string(JSON PLUGIN_IDENTITY GET "${PLUGIN_MANIFEST}" name)
if(NOT PLUGIN_IDENTITY STREQUAL PROJECT_NAME)
    message(FATAL_ERROR "manifest name must match the native binary name for Windows loading")
endif()
set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../4D-Plugin-SDK/4D Plugin API")
set(PLUGIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/create-alias-test/Plugins")

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    add_library(alias-native STATIC macos.mm)
    target_compile_options(alias-native PRIVATE -fobjc-arc)
    target_link_libraries(alias-native PUBLIC "-framework Foundation")
elseif(WIN32)
    add_library(alias-native STATIC windows.cpp arguments.cpp)
    target_compile_definitions(alias-native PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(alias-native PUBLIC ole32 shell32 uuid)
else()
    message(FATAL_ERROR "Only macOS and Windows are supported")
endif()
target_include_directories(alias-native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(alias-native PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(create-alias MODULE adapter.cpp "${SDK_DIR}/4DPluginAPI.c")
target_include_directories(create-alias PRIVATE "${SDK_DIR}")
target_compile_definitions(create-alias PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(create-alias PRIVATE alias-native)
set_source_files_properties("${SDK_DIR}/4DPluginAPI.c" PROPERTIES LANGUAGE CXX)
if(APPLE)
    set_source_files_properties("${SDK_DIR}/4DPluginAPI.c" PROPERTIES
        COMPILE_OPTIONS "-Wno-multichar;-Wno-int-conversion")
    target_link_libraries(create-alias PRIVATE "-framework CoreGraphics")
    target_sources(create-alias PRIVATE manifest.json constants.xlf)
    set_source_files_properties(manifest.json constants.xlf PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(create-alias PROPERTIES
        BUNDLE TRUE BUNDLE_EXTENSION bundle
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.miyako.create-alias"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PLUGIN_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PLUGIN_VERSION}"
        LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_ROOT}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${PLUGIN_ROOT}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${PLUGIN_ROOT}")
else()
    set_target_properties(create-alias PROPERTIES PREFIX "" SUFFIX ".4DX"
        LINK_FLAGS "/DEF:\"${SDK_DIR}/4DPluginAPI.def\""
        LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_ROOT}/create-alias/Contents/Windows64"
        RUNTIME_OUTPUT_DIRECTORY "${PLUGIN_ROOT}/create-alias/Contents/Windows64"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PLUGIN_ROOT}/create-alias/Contents/Windows64"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PLUGIN_ROOT}/create-alias/Contents/Windows64")
    add_custom_command(TARGET create-alias POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_ROOT}/create-alias/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/constants.xlf"
            "${PLUGIN_ROOT}/create-alias/Contents/Resources")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(unicode-tests tests/unicode.cpp)
    target_include_directories(unicode-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME unicode COMMAND unicode-tests)
    if(APPLE)
        add_executable(native-tests tests/macos.mm)
        target_compile_options(native-tests PRIVATE -fobjc-arc)
        add_test(NAME finder-alias COMMAND native-tests)
    else()
        add_executable(argv-recorder tests/argv-recorder.cpp)
        add_executable(native-tests tests/windows.cpp)
        add_dependencies(native-tests argv-recorder)
        add_test(NAME windows-shortcut COMMAND native-tests $<TARGET_FILE:argv-recorder>)
    endif()
    target_link_libraries(native-tests PRIVATE alias-native)
endif()
