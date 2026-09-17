# A2DP Windows Bridge (A2DPWB)
# Captures system audio via WASAPI, encodes with SSC/AAC/SBC,
# and streams over Bluetooth via BTstack (WinUSB).
# Core library (a2dpwb_core) is framework-agnostic; GUI frontend via wxWidgets.

# --- wxWidgets (FetchContent) ---
include(FetchContent)
FetchContent_Declare(
    wxWidgets
    GIT_REPOSITORY https://github.com/wxWidgets/wxWidgets.git
    GIT_TAG        v3.2.6
    GIT_SHALLOW    TRUE
)
set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
set(wxUSE_WEBVIEW OFF CACHE BOOL "" FORCE)
set(wxUSE_STC OFF CACHE BOOL "" FORCE)
set(wxUSE_RIBBON OFF CACHE BOOL "" FORCE)
set(wxUSE_PROPGRID OFF CACHE BOOL "" FORCE)
set(wxUSE_AUI OFF CACHE BOOL "" FORCE)
set(wxUSE_RICHTEXT OFF CACHE BOOL "" FORCE)
set(wxUSE_MEDIACTRL OFF CACHE BOOL "" FORCE)
set(wxUSE_XRC OFF CACHE BOOL "" FORCE)
set(wxUSE_HTML OFF CACHE BOOL "" FORCE)
set(wxUSE_HELP OFF CACHE BOOL "" FORCE)
set(wxUSE_WXHTML_HELP OFF CACHE BOOL "" FORCE)
set(wxBUILD_COMPATIBILITY "3.0" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(wxWidgets)

# --- Embed language files at configure time (needed by core localization) ---
file(GLOB LANG_FILES "${CMAKE_CURRENT_SOURCE_DIR}/lang/*.json")
set(EMBEDDED_LANG_ENTRIES "")
foreach(LANG_FILE ${LANG_FILES})
    get_filename_component(LANG_CODE ${LANG_FILE} NAME_WE)
    file(READ ${LANG_FILE} LANG_CONTENT)
    string(APPEND EMBEDDED_LANG_ENTRIES "        {\"${LANG_CODE}\", R\"LANG_JSON(${LANG_CONTENT})LANG_JSON\"},\n")
endforeach()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/embedded_langs.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_langs.h
    @ONLY
)

# ==========================================================================
# Core library — framework-agnostic backend (no wxWidgets dependency)
# Consumed by both the wxWidgets exe and the WinUI 3 app.
# ==========================================================================
add_library(a2dpwb_core STATIC
    src/a2dp_service.cpp
    src/btstack_transport.cpp
    src/wasapi_capture.cpp
    src/resampler.cpp
    src/a2dp_sbc_encoder.cpp
    src/aac_encoder.cpp
    src/ssc_encoder.cpp
    src/bt_device.cpp
    src/bt_adapter_enum.cpp
    src/audio_device_enum.cpp
    src/btstack_link_key_db_file.c
    src/config_path.cpp
    src/app_settings.cpp
    src/profile_manager.cpp
    src/zadig_helper.cpp
    src/driver_mode.cpp
    src/driver_switch.cpp
    src/localization.cpp
)

target_include_directories(a2dpwb_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/extern/json/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# fdk-aac includes
if(TARGET fdk-aac)
    target_include_directories(a2dpwb_core PUBLIC
        ${CMAKE_SOURCE_DIR}/extern/fdk-aac/libAACenc/include
        ${CMAKE_SOURCE_DIR}/extern/fdk-aac/libSYS/include
    )
endif()

# Third-party static libraries
if(TARGET btstack)
    target_link_libraries(a2dpwb_core PUBLIC btstack)
endif()
if(TARGET sbc_codec)
    target_link_libraries(a2dpwb_core PUBLIC sbc_codec)
endif()
if(TARGET fdk-aac)
    target_link_libraries(a2dpwb_core PUBLIC fdk-aac)
endif()

# Windows system libraries
target_link_libraries(a2dpwb_core PUBLIC
    ole32          # COM (CoInitializeEx, CoCreateInstance)
    uuid           # COM interface UUIDs (__uuidof)
    avrt           # Multimedia scheduling (AvSetMmThreadCharacteristics)
    bthprops       # Bluetooth device enumeration (BluetoothFindFirstDevice)
    ws2_32         # Winsock (Bluetooth address types)
    propsys        # Property store (PKEY_Device_FriendlyName)
    version        # GetFileVersionInfo (friendly app names)
    setupapi       # SetupDi (Zadig USB enumeration)
    shcore         # SetProcessDpiAwareness
    winmm          # timeBeginPeriod/timeEndPeriod (timer resolution)
)

# Compile definitions
target_compile_definitions(a2dpwb_core PUBLIC
    _WIN32_WINNT=0x0A00   # Windows 10+
    NOMINMAX               # Avoid min/max macro conflicts
    WIN32_LEAN_AND_MEAN    # Reduce Windows header bloat
    APP_VERSION="${PROJECT_VERSION}"
)

# fdk-aac available
if(TARGET fdk-aac)
    target_compile_definitions(a2dpwb_core PUBLIC AAC_ENCODER_AVAILABLE=1)
endif()

if(MSVC)
    target_compile_options(a2dpwb_core PRIVATE /W3 /utf-8)
else()
    target_compile_options(a2dpwb_core PRIVATE -Wall -Wextra)
endif()

# ==========================================================================
# wxWidgets application (links against a2dpwb_core)
# ==========================================================================
add_executable(A2DPWB WIN32
    src/main.cpp
    src/wx_app.cpp
    src/wx_main_frame.cpp
    src/wx_profile_dialog.cpp
    src/wx_settings_dialog.cpp
    src/wx_about_dialog.cpp
    src/wx_firmware_dialog.cpp
    src/wx_zadig_dialog.cpp
    src/theme_manager.cpp
    src/system_integration.cpp
    resources/app.rc
)

set_target_properties(A2DPWB PROPERTIES OUTPUT_NAME "SSCOnWindows-${PROJECT_VERSION}")

target_link_libraries(A2DPWB PRIVATE a2dpwb_core wx::core wx::base)
target_link_libraries(A2DPWB PRIVATE shell32)   # Shell_NotifyIcon (system tray)

if(MSVC)
    target_compile_options(A2DPWB PRIVATE /W3 /utf-8)
    target_link_options(A2DPWB PRIVATE /ENTRY:mainCRTStartup)
    target_link_options(A2DPWB PRIVATE /MANIFEST:NO)
else()
    target_compile_options(A2DPWB PRIVATE -Wall -Wextra)
endif()
