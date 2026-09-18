# - Try to find libdw (elfutils)
# Once done this will define
#
#  LIBDW_FOUND - system has libdw
#  LIBDW_INCLUDE_DIRS - the libdw include directory
#  LIBDW_LIBRARIES - Link these to use libdw
#
# Provides the imported target Dw::Dw.
#
# libdw-dev on Debian/Ubuntu, elfutils-devel on Fedora/RHEL/Rocky.

if (LIBDW_LIBRARIES AND LIBDW_INCLUDE_DIRS)
    set(LIBDW_FIND_QUIETLY ON)
endif (LIBDW_LIBRARIES AND LIBDW_INCLUDE_DIRS)

find_path(LIBDW_INCLUDE_DIRS
        NAMES
        elfutils/libdwfl.h
        PATHS
        /usr/include
        /usr/local/include
        /opt/local/include
        /opt/include
        ENV CPATH)

find_library(LIBDW_DW_LIBRARY
        NAMES
        dw
        PATHS
        /usr/lib
        /usr/lib64
        /usr/local/lib
        /usr/local/lib64
        /opt/local/lib
        /opt/usr/lib64
        ENV LIBRARY_PATH
        ENV LD_LIBRARY_PATH)

find_library(LIBDW_ELF_LIBRARY
        NAMES
        elf
        PATHS
        /usr/lib
        /usr/lib64
        /usr/local/lib
        /usr/local/lib64
        /opt/local/lib
        /opt/usr/lib64
        ENV LIBRARY_PATH
        ENV LD_LIBRARY_PATH)

include(FindPackageHandleStandardArgs)

# handle the QUIETLY and REQUIRED arguments and set LIBDW_FOUND to TRUE if all listed variables are TRUE
find_package_handle_standard_args(Dw DEFAULT_MSG
        LIBDW_DW_LIBRARY
        LIBDW_ELF_LIBRARY
        LIBDW_INCLUDE_DIRS)

mark_as_advanced(LIBDW_INCLUDE_DIRS LIBDW_LIBRARIES LIBDW_DW_LIBRARY LIBDW_ELF_LIBRARY)

if(Dw_FOUND AND NOT TARGET Dw::Dw)
    add_library(Dw::Dw INTERFACE IMPORTED)
    set_target_properties(Dw::Dw PROPERTIES
        INTERFACE_LINK_LIBRARIES "${LIBDW_DW_LIBRARY};${LIBDW_ELF_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBDW_INCLUDE_DIRS}")
endif()
