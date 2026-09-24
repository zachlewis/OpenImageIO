# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

if (JXL_FOUND)
    add_oiio_plugin (jxlinput.cpp jxloutput.cpp
                     INCLUDE_DIRS ${JXL_INCLUDES}
                     LINK_LIBRARIES ${JXL_LIBRARIES}
                     DEFINITIONS "USE_JXL")
    if (OIIO_BUILD_TESTS AND BUILD_TESTING)
        fancy_add_executable (
            NAME jxl_profile_test
            SRC ${PROJECT_SOURCE_DIR}/testsuite/jxl/src/jxl_profile_test.cpp
            INCLUDE_DIRS ${JXL_INCLUDES}
            LINK_LIBRARIES OpenImageIO ${JXL_LIBRARIES}
            FOLDER "Unit Tests"
            NO_INSTALL)
    endif ()
else()
    message (WARNING "JPEG XL plugin will not be built")
endif()
