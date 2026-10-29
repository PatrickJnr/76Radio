cmake_minimum_required(VERSION 3.25)

project(76Radio VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "76Radio builds only for Windows")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

function(radio76_target_defaults target)
    target_compile_options(${target} PRIVATE
        /W4
        /permissive-
        /EHsc
        /utf-8
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX
    )
endfunction()

add_library(radio76_media STATIC
    src/native/media/src/icy_metadata.cpp
    src/native/media/src/media_service.cpp
    src/native/media/src/media_types.cpp
    src/native/media/src/scaleform_payload.cpp
    src/native/media/src/stream_catalog.cpp
    src/native/media/src/stream_player.cpp
    src/native/media/src/windows_media_backend.cpp
)

target_include_directories(radio76_media PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/media/include"
)

target_link_libraries(radio76_media PUBLIC WindowsApp.lib windowscodecs.lib winhttp.lib)

radio76_target_defaults(radio76_media)

add_library(radio76_bridge STATIC
    src/native/bridge/src/bridge_contract.cpp
)

target_include_directories(radio76_bridge PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/bridge/include"
)

target_link_libraries(radio76_bridge PUBLIC radio76_media)

radio76_target_defaults(radio76_bridge)

add_library(radio76_settings STATIC
    src/native/settings/src/settings.cpp
    src/native/settings/src/log_file.cpp
)

target_include_directories(radio76_settings PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/settings/include"
)

radio76_target_defaults(radio76_settings)

add_library(radio76_runtime STATIC
    src/native/runtime/src/native_runtime.cpp
)

target_include_directories(radio76_runtime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime/include"
)

target_link_libraries(radio76_runtime PUBLIC radio76_bridge)

radio76_target_defaults(radio76_runtime)

add_library(radio76_channel STATIC
    src/native/channel/src/document_channel.cpp
)

target_include_directories(radio76_channel PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/channel/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime/include"
)

target_link_libraries(radio76_channel PUBLIC radio76_runtime radio76_settings)

radio76_target_defaults(radio76_channel)

add_library(radio76_surface SHARED
    src/native/surface/src/proxy_exports.cpp
    src/native/surface/src/surface_module.cpp
    src/native/surface/surface_module.def
)

target_link_libraries(radio76_surface PRIVATE radio76_channel radio76_settings shell32)

radio76_target_defaults(radio76_surface)

set_target_properties(radio76_surface PROPERTIES
    OUTPUT_NAME "version"
    PREFIX ""
)

add_executable(radio76_tests src/native/media/tests/media_tests.cpp)

target_link_libraries(radio76_tests PRIVATE radio76_media)

target_include_directories(radio76_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/testing/include"
)

radio76_target_defaults(radio76_tests)

add_executable(radio76_bridge_tests src/native/bridge/tests/bridge_contract_tests.cpp)

target_link_libraries(radio76_bridge_tests PRIVATE radio76_runtime)

target_include_directories(radio76_bridge_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/testing/include"
)

radio76_target_defaults(radio76_bridge_tests)

add_executable(radio76_channel_tests src/native/channel/tests/document_channel_tests.cpp)

target_link_libraries(radio76_channel_tests PRIVATE radio76_channel)

target_include_directories(radio76_channel_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/testing/include"
)

radio76_target_defaults(radio76_channel_tests)

add_executable(radio76_settings_tests src/native/settings/tests/settings_tests.cpp)

target_link_libraries(radio76_settings_tests PRIVATE radio76_settings)

target_include_directories(radio76_settings_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/testing/include"
)

radio76_target_defaults(radio76_settings_tests)

add_executable(radio76_surface_tests src/native/surface/tests/surface_module_tests.cpp)

target_include_directories(radio76_surface_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/testing/include"
)

radio76_target_defaults(radio76_surface_tests)

enable_testing()

add_test(NAME radio76_media COMMAND radio76_tests)
add_test(NAME radio76_bridge COMMAND radio76_bridge_tests)
add_test(NAME radio76_channel COMMAND radio76_channel_tests)
add_test(NAME radio76_settings COMMAND radio76_settings_tests)
add_test(NAME radio76_surface COMMAND radio76_surface_tests $<TARGET_FILE:radio76_surface>)
