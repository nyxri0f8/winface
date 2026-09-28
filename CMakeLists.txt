cmake_minimum_required(VERSION 3.20)
project(WinFace CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static CRT: the DLL loaded into LogonUI must not depend on a VC++ redistributable being installed
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE NOMINMAX ORT_API_MANUAL_INIT)
add_compile_options(/W4 /permissive- /EHsc /utf-8 /guard:cf /sdl)
add_link_options(/guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)

set(ORT_DIR ${CMAKE_SOURCE_DIR}/third_party/onnxruntime-win-x64-1.24.4)

add_library(fgengine STATIC
    engine/camera.cpp engine/geom.cpp engine/onnx.cpp engine/facemesh.cpp engine/recog.cpp engine/decide.cpp
    engine/profiles.cpp)
target_include_directories(fgengine PUBLIC ${ORT_DIR}/include)
target_link_libraries(fgengine PUBLIC ${ORT_DIR}/lib/onnxruntime.lib)

# the credential provider loaded by LogonUI.exe (lock screen) / CredUI
add_library(WinFaceCP SHARED
    cp/dll.cpp cp/common.cpp cp/helpers.cpp cp/secret.cpp cp/scanner.cpp cp/overlay.cpp cp/credential.cpp cp/provider.cpp
    cp/intruder.cpp
    cp/WinFaceCP.def)
target_link_libraries(WinFaceCP PRIVATE fgengine delayimp.lib)
target_link_options(WinFaceCP PRIVATE /DELAYLOAD:onnxruntime.dll)

add_executable(fgcli tools/fgcli.cpp)
target_link_libraries(fgcli PRIVATE fgengine)

add_executable(fgsetup tools/fgsetup.cpp cp/common.cpp cp/secret.cpp cp/intruder.cpp)
target_link_libraries(fgsetup PRIVATE fgengine shlwapi.lib)

add_executable(fgcredtest tools/fgcredtest.cpp)

# dev only (not installed): fgsetup's FaceGate -> WinFace migration on scratch folders + a scratch HKCU key
add_executable(fgsetup_migratetest tools/fgsetup.cpp cp/common.cpp cp/secret.cpp cp/intruder.cpp)
target_link_libraries(fgsetup_migratetest PRIVATE fgengine shlwapi.lib)
target_compile_definitions(fgsetup_migratetest PRIVATE WINFACE_MIGRATE_TEST)

# unlock sound player: its own process so the sound outlives LogonUI (no window, no arguments)
add_executable(fgsound WIN32 tools/fgsound.cpp)

# dev check of the lock-screen overlay, provider DLL and sound (renders the real HUD code; not installed)
add_executable(fgoverlaytest tools/fgoverlaytest.cpp cp/overlay.cpp cp/common.cpp cp/intruder.cpp)
target_link_libraries(fgoverlaytest PRIVATE fgengine shlwapi.lib)

foreach(t fgcli fgsetup)
    add_custom_command(TARGET ${t} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_DIR}/lib/onnxruntime.dll $<TARGET_FILE_DIR:${t}>)
endforeach()
