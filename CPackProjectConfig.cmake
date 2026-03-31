# Per-generator packaging prefix overrides.
# This file is included by CPack at package-build time (CPACK_PROJECT_CONFIG_FILE).
# CPACK_GENERATOR is set to the active generator (e.g. "RPM", "DEB") at that point.

if(CPACK_GENERATOR STREQUAL "RPM")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
endif()
