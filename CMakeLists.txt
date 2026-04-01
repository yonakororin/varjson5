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
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=native>
    $<$<CONFIG:Debug>:-g>
)

include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
if(_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# ============================================================================
# Targets
# ============================================================================

# Shared library (FFI-ready, C API exported)
add_library(varjson5lib SHARED src/varjson5.cpp)
target_include_directories(varjson5lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
set_target_properties(varjson5lib PROPERTIES
    OUTPUT_NAME varjson5
    VERSION     ${PROJECT_VERSION}
    SOVERSION   1
)

# Executable
add_executable(varjson5 src/main.cpp)
target_include_directories(varjson5 PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(varjson5 PRIVATE varjson5lib)
# Embed RPATH relative to the binary so libvarjson5.so is found regardless
# of install prefix (/usr or /usr/local).
set_target_properties(varjson5 PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
)

install(TARGETS varjson5lib
    LIBRARY DESTINATION lib
)
install(TARGETS varjson5
    RUNTIME DESTINATION bin
)
install(FILES include/varjson5.h
    DESTINATION include
)

# ============================================================================
# Python / PHP bindings installation
# ============================================================================

# Detect Python3 version to build the correct site-packages path
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    set(_py_ver "${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}")
else()
    execute_process(
        COMMAND python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
        OUTPUT_VARIABLE _py_ver
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT _py_ver)
        set(_py_ver "3")
    endif()
endif()

# Cache for use in CPackProjectConfig.cmake
set(PYTHON_VERSION_DIR "python${_py_ver}"
    CACHE STRING "Python versioned directory name (e.g. python3.11)" FORCE)
set(PYTHON_SITE_PACKAGES "lib/${PYTHON_VERSION_DIR}/site-packages"
    CACHE STRING "Python site-packages path relative to install prefix" FORCE)

install(FILES python/VarJson5.py
    DESTINATION "${PYTHON_SITE_PACKAGES}"
)

install(FILES php/VarJson5.php
    DESTINATION "share/php"
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

# Install into /usr by default; RPM overrides this to /usr/local via CPackProjectConfig.cmake
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/CPackProjectConfig.cmake")

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
