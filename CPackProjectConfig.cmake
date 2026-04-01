# Per-generator packaging prefix overrides.
# This file is included by CPack at package-build time (CPACK_PROJECT_CONFIG_FILE).
# CPACK_GENERATOR is set to the active generator (e.g. "RPM", "DEB") at that point.

if(CPACK_GENERATOR STREQUAL "RPM")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")

    # Exclude standard directories already owned by the filesystem package
    # to avoid "conflict with file from filesystem-*" errors.
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/local
        /usr/local/bin
        /usr/local/lib
        /usr/local/lib64
        /usr/local/include
        /usr/local/share
        /usr/local/share/php
        "/usr/local/lib/${PYTHON_VERSION_DIR}"
        "/usr/local/lib/${PYTHON_VERSION_DIR}/site-packages"
    )
endif()
