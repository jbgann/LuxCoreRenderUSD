project(luxcorerenderusd)

cmake_minimum_required(VERSION 3.16.0)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "64-bit builds not supported: check compiler or Windows shell environment.")
endif()

# For debug only
add_compile_definitions(TBB_USE_THREADING_TOOLS=0)
add_compile_definitions(TBB_USE_ASSERT=0)

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

# Visual Studio
set(CMAKE_CXX_STANDARD 14)

# For some reason Python detection doesn't use a directory
get_filename_component(PYTHON_LIBRARY_DIR ${PYTHON_LIBRARY} DIRECTORY)
link_directories(${PYTHON_LIBRARY_DIR})

# Add USD Libraries from the environment
set(USD_ROOT $ENV{USD_ROOT})
include_directories(${USD_ROOT}/include)
link_directories(${USD_ROOT}/lib)

# Add LuxCore libraries from the environment
set(LUXCORE_ROOT $ENV{LUXCORE_ROOT})
add_library(LuxCore SHARED IMPORTED)
set_target_properties(LuxCore PROPERTIES
    IMPORTED_LOCATION "${LUXCORE_ROOT}/lib/luxcore.dll"
    IMPORTED_IMPLIB "${LUXCORE_ROOT}/lib/luxcore.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${LUXCORE_ROOT}/include"
)

add_subdirectory(pxr/imaging/plugin/hdLuxCore)
