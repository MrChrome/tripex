cmake_minimum_required(VERSION 3.20)
project(Tripex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependencies (Homebrew on Apple Silicon)
# ---------------------------------------------------------------------------
list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/sdl2")

find_package(SDL2 REQUIRED)

find_library(OPENGL_LIBRARY  OpenGL  REQUIRED)
find_library(COCOA_LIBRARY   Cocoa   REQUIRED)

# ---------------------------------------------------------------------------
# Source files
# Exclude Windows-only files; add macOS replacements.
# ---------------------------------------------------------------------------
file(GLOB ALL_CPP "src/Tripex/*.cpp")

set(EXCLUDE_SOURCES
    "${CMAKE_SOURCE_DIR}/src/Tripex/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/Tripex/RendererDirect3d.cpp"
)
foreach(f IN LISTS EXCLUDE_SOURCES)
    list(REMOVE_ITEM ALL_CPP "${f}")
endforeach()

# macOS-specific additions
list(APPEND ALL_CPP
    "${CMAKE_SOURCE_DIR}/src/Tripex/main_sdl.cpp"
    "${CMAKE_SOURCE_DIR}/src/Tripex/RendererOpenGL.cpp"
    "${CMAKE_SOURCE_DIR}/src/Tripex/AudioDeviceSDL.cpp"
)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(Tripex MACOSX_BUNDLE ${ALL_CPP})

target_include_directories(Tripex PRIVATE
    src/Tripex
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(Tripex PRIVATE
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARY}
    ${COCOA_LIBRARY}
)

# Silence Apple's OpenGL deprecation warnings (OpenGL still works fine)
target_compile_definitions(Tripex PRIVATE GL_SILENCE_DEPRECATION)

# Suppress common Clang warnings that fire on the legacy C++ style
target_compile_options(Tripex PRIVATE
    -Wno-unused-parameter
    -Wno-implicit-int-float-conversion
    -Wno-deprecated-declarations
)

# ---------------------------------------------------------------------------
# macOS .app bundle metadata
# ---------------------------------------------------------------------------
set_target_properties(Tripex PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME        "Tripex"
    MACOSX_BUNDLE_GUI_IDENTIFIER     "com.tripex.app"
    MACOSX_BUNDLE_BUNDLE_VERSION     "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_INFO_PLIST         "${CMAKE_SOURCE_DIR}/Info.plist.in"
)
