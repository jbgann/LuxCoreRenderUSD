set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdLuxCore)
add_custom_target(shared_libs)

set(optionalPublicClasses "")

if (OPENSUBDIV_HAS_GLSL_TRANSFORM_FEEDBACK)
    add_definitions(-DOPENSUBDIV_HAS_GLSL_TRANSFORM_FEEDBACK)
endif()
if (OPENSUBDIV_HAS_GLSL_COMPUTE)
    add_definitions(-DOPENSUBDIV_HAS_GLSL_COMPUTE)
endif()

pxr_plugin(hdLuxCore
    LIBRARIES
        ar
        arch
        sdf
        trace
        plug
        tf
        vt
        gf
        glf
        work
        hf
        hd
        hdSt
        hdx
        usdLux
        usdUtils
        pxOsd
        ${Boost_LIBRARIES} 
        ${RIF_LIBRARY}
        ${OPENSUBDIV_LIBRARIES}
        ${TBB_tbb_LIBRARY}
        ${OPENGL_gl_LIBRARY}
        LuxCore

    INCLUDE_DIRS
        ${RIF_LOCATION_INCLUDE}
        ${GLEW_INCLUDE_DIR}
        ${OPENSUBDIV_INCLUDE_DIR}
        ${Boost_INCLUDE_DIR} 
        ${TBB_INCLUDE_DIRS}

    PUBLIC_CLASSES
        rendererPlugin
        renderDelegate
        instancer
        sampler
        renderPass
        mesh
        light
        camera

    PUBLIC_HEADERS
        renderParam.h

    RESOURCE_FILES
        plugInfo.json
)

# Install the resource file for USD
configure_file(plugInfo.json ${USD_ROOT}/plugin/usd/hdLuxCore/resources/plugInfo.json @ONLY)
