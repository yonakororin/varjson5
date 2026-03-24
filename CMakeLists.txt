cmake_minimum_required(VERSION 3.16)

project(varjson5
    VERSION      1.0.0
    DESCRIPTION  "jq-like JSON5 processor with {{vars}} substitution"
    HOMEPAGE_URL "https://github.com/example/varjson5"
    LANGUAGES    CXX
)

# ============================================================================
# Build settings
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g>
)

# ============================================================================
# Target
# ============================================================================

add_executable(varjson5 src/main.cpp)

install(TARGETS varjson5
    RUNTIME DESTINATION bin
)

# ============================================================================
# CPack — common settings
# ============================================================================

set(CPACK_PACKAGE_NAME        "varjson5")
set(CPACK_PACKAGE_VERSION     "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_DESCRIPTION
"varjson5 is a command-line JSON5 processor similar to jq.
It parses JSON5 (comments, trailing commas, unquoted keys, etc.)
and supports Mustache-style {{vars}} variable substitution.")
set(CPACK_PACKAGE_CONTACT     "maintainer@example.com")
set(CPACK_PACKAGE_VENDOR      "varjson5 Project")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")

set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Install into /usr prefix inside the package
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

set(CPACK_STRIP_FILES TRUE)

# ============================================================================
# CPack — DEB
# ============================================================================

set(CPACK_DEBIAN_PACKAGE_MAINTAINER  "maintainer@example.com")
set(CPACK_DEBIAN_PACKAGE_SECTION     "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY    "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE    "${PROJECT_HOMEPAGE_URL}")

# Detect architecture
execute_process(
    COMMAND dpkg --print-architecture
    OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
endif()

set(CPACK_DEBIAN_FILE_NAME
    "${CPACK_PACKAGE_NAME}_${CPACK_PACKAGE_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")

# ============================================================================
# CPack — RPM
# ============================================================================

set(CPACK_RPM_PACKAGE_GROUP    "Applications/Text")
set(CPACK_RPM_PACKAGE_LICENSE  "MIT")
set(CPACK_RPM_PACKAGE_URL      "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_SUMMARY  "${PROJECT_DESCRIPTION}")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")

# Detect architecture (RPM native name: x86_64, aarch64, ...)
execute_process(
    COMMAND rpm --eval %{_arch}
    OUTPUT_VARIABLE CPACK_RPM_PACKAGE_ARCHITECTURE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CPACK_RPM_PACKAGE_ARCHITECTURE)
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(CPACK_RPM_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_ARCHITECTURE}.rpm")

# ============================================================================
# CPack — generator selection
# ============================================================================

# Default: build both DEB and RPM. Override with -DCPACK_GENERATOR=DEB etc.
if(NOT CPACK_GENERATOR)
    set(CPACK_GENERATOR "DEB;RPM")
endif()

include(CPack)
