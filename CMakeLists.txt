project(luxcorerenderusd)

cmake_minimum_required(VERSION 3.16.0)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "64-bit builds not supported: check compiler or Windows shell environment.")
endif()

# Set paths from the environment
set(USD_ROOT $ENV{USD_ROOT})
set(LUXCORE_ROOT $ENV{LUXCORE_ROOT})

# Needed for boost and tbb
SET(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} ${USD_ROOT}/include)
SET(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ${USD_ROOT}/lib ${LUXCORE_ROOT}/lib)

# Disable ptex support for now
set(PXR_ENABLE_PTEX_SUPPORT FALSE)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
                      ${CMAKE_SOURCE_DIR}/cmake/defaults
                      ${CMAKE_SOURCE_DIR}/cmake/modules
                      ${CMAKE_SOURCE_DIR}/cmake/macros)

include(Options)
include(ProjectDefaults)
include(Packages)

# This has to be defined after Packages is included, because it relies on the
# discovered path to the python executable.
set(PXR_PYTHON_SHEBANG "${PYTHON_EXECUTABLE}" 
    CACHE 
    STRING
    "Replacement path for Python #! line."
)

# CXXDefaults will set a variety of variables for the project.
# Consume them here. This is an effort to keep the most common
# build files readable.
include(CXXDefaults)
add_definitions(${_PXR_CXX_DEFINITIONS})
set(CMAKE_CXX_FLAGS "${_PXR_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")

include(Public)

set(CMAKE_CXX_STANDARD 14)

# For some reason Python detection doesn't use a directory
get_filename_component(PYTHON_LIBRARY_DIR ${PYTHON_LIBRARY} DIRECTORY)
link_directories(${PYTHON_LIBRARY_DIR})

if (WIN32)
    add_library(LuxCore SHARED IMPORTED)
    set_target_properties(LuxCore PROPERTIES
        IMPORTED_LOCATION "${LUXCORE_ROOT}/lib/luxcore.dll"
        IMPORTED_IMPLIB "${LUXCORE_ROOT}/lib/luxcore.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${LUXCORE_ROOT}/include"
    )
else()
    add_library(LuxCore SHARED IMPORTED)
    set_target_properties(LuxCore PROPERTIES
        IMPORTED_LOCATION "${LUXCORE_ROOT}/lib/libluxcore.so"
        INTERFACE_INCLUDE_DIRECTORIES "${LUXCORE_ROOT}/include"
    )
endif()

add_subdirectory(pxr/imaging/plugin/hdLuxCore)
