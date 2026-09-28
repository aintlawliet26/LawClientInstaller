cmake_minimum_required(VERSION 3.24)
project(LawClientInstaller LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "LawClientInstaller is Windows-only.")
endif()

set(LAWCLIENT_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/lawclient.ico")
file(TO_CMAKE_PATH "${LAWCLIENT_ICON_PATH}" LAWCLIENT_ICON_PATH)
configure_file(assets/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)

add_executable(LawClientInstaller WIN32
  src/main.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
)

target_compile_features(LawClientInstaller PRIVATE cxx_std_20)
target_compile_definitions(LawClientInstaller PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(LawClientInstaller PRIVATE assets)
target_link_libraries(LawClientInstaller PRIVATE
  user32 gdi32 dwmapi winhttp bcrypt shell32 ole32 advapi32
)

if(MSVC)
  target_compile_options(LawClientInstaller PRIVATE /O1 /GL /Gy /Gw /permissive- /EHsc /W4)
  target_link_options(LawClientInstaller PRIVATE /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO)
endif()
