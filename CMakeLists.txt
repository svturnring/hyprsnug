cmake_minimum_required(VERSION 3.27)

# Versioning: Semantic Versioning 2.0.0, see "Versioning" in AGENTS.md.
# VERSION (repository root) is the single source of truth: the project version
# and the version string compiled into the plugin are both derived from it, so
# never hardcode a version here.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" HYPRSNUG_VERSION)
string(STRIP "${HYPRSNUG_VERSION}" HYPRSNUG_VERSION)

# project(VERSION) accepts only the numeric core, while pre-release and
# build-metadata suffixes (0.2.0-beta.1, 0.2.0+build.7) stay in the string that
# the plugin reports.
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" HYPRSNUG_VERSION_CORE "${HYPRSNUG_VERSION}")
if(HYPRSNUG_VERSION_CORE STREQUAL "")
    message(FATAL_ERROR "VERSION does not start with a MAJOR.MINOR.PATCH core: '${HYPRSNUG_VERSION}'")
endif()

project(hyprsnug
    DESCRIPTION "hyprsnug plugin for Hyprland"
    VERSION ${HYPRSNUG_VERSION_CORE}
)

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SRC "*.cpp")

add_library(hyprsnug SHARED ${SRC})
target_compile_definitions(hyprsnug PRIVATE HYPRSNUG_VERSION="${HYPRSNUG_VERSION}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
    libdrm
    libinput
    libudev
    pangocairo
    pixman-1
    wayland-server
    xkbcommon
)
target_link_libraries(hyprsnug PRIVATE rt PkgConfig::deps)

install(TARGETS hyprsnug)
