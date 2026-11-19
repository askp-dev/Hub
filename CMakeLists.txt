cmake_minimum_required(VERSION 3.22)
project(Hub VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

# Where the app reads the plugin list. A plain file in this (public) repo, so
# publishing a release is a commit to manifest.json.
set(HUB_MANIFEST_URL "https://raw.githubusercontent.com/askp-dev/hub/main/manifest.json" CACHE STRING "Plugin list URL")

# Same JUCE as V/A. To save disk and download time, point at an existing copy:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE="../V-A/build/_deps/juce-src"
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

set(HUB_DEFS
    HUB_VERSION="${PROJECT_VERSION}"
    HUB_MANIFEST_URL="${HUB_MANIFEST_URL}"
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CURL=$<IF:$<PLATFORM_ID:Linux>,1,0>)

# --- installer core (no UI) ---
add_library(hub_core STATIC Source/core/Installer.cpp)
target_include_directories(hub_core PUBLIC Source)
target_compile_definitions(hub_core PUBLIC ${HUB_DEFS})
target_link_libraries(hub_core PUBLIC juce::juce_core juce::juce_cryptography juce::juce_events
                      PRIVATE juce::juce_recommended_config_flags)
set_target_properties(hub_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- the app ---
juce_add_gui_app(Hub
    PRODUCT_NAME "askp Hub"
    COMPANY_NAME "askp"
    BUNDLE_ID com.askp.hub
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png"
    NEEDS_CURL TRUE
    NEEDS_WEB_BROWSER FALSE)
target_sources(Hub PRIVATE Source/app/Main.cpp Source/app/HubComponent.cpp)
target_compile_definitions(Hub PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)   # headless --install check
target_link_libraries(Hub PRIVATE hub_core juce::juce_gui_basics
                      PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- tests: manifest parsing, versions, install/update/remove in a sandbox ---
enable_testing()
juce_add_console_app(hub_tests PRODUCT_NAME "hub_tests" NEEDS_CURL TRUE)
target_sources(hub_tests PRIVATE tests/hub_tests.cpp)
target_link_libraries(hub_tests PRIVATE hub_core juce::juce_recommended_config_flags)
add_test(NAME hub_tests COMMAND hub_tests)
