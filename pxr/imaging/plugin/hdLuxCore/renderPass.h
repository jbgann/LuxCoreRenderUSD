//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#ifndef HDLUXCORE_RENDER_PASS_H
#define HDLUXCORE_RENDER_PASS_H

#include "pxr/pxr.h"

#include "pxr/imaging/hd/aov.h"
#include "pxr/imaging/hd/renderPass.h"
#include "pxr/imaging/hdx/compositor.h"

#include "pxr/base/gf/matrix4d.h"

#include <atomic>

PXR_NAMESPACE_OPEN_SCOPE

typedef boost::shared_ptr<class GlfGLContext> GlfGLContextSharedPtr;

/// \class HdLuxCoreRenderPass
///
/// HdRenderPass represents a single render iteration, rendering a view of the
/// scene (the HdRprimCollection) for a specific viewer (the camera/viewport
/// parameters in HdRenderPassState) to the current draw target.
///
class HdLuxCoreRenderPass final : public HdRenderPass {
public:
    /// Renderpass constructor.
    ///   \param index The render index containing scene data to render.
    ///   \param collection The initial rprim collection for this renderpass.
    ///   \param renderThread A handle to the global render thread.
    ///   \param renderer A handle to the global renderer.
    HdLuxCoreRenderPass(HdRenderIndex *index,
                       HdRprimCollection const &collection,
                       std::atomic<int> *sceneVersion);

    /// Renderpass destructor.
    virtual ~HdLuxCoreRenderPass();

    // -----------------------------------------------------------------------
    // HdRenderPass API

    /// Determine whether the sample buffer has enough samples.
    ///   \return True if the image has enough samples to be considered final.
    virtual bool IsConverged() const override;

protected:

    // -----------------------------------------------------------------------
    // HdRenderPass API

    /// Draw the scene with the bound renderpass state.
    ///   \param renderPassState Input parameters (including viewer parameters)
    ///                          for this renderpass.
    ///   \param renderTags Which rendertags should be drawn this pass.
    virtual void _Execute(HdRenderPassStateSharedPtr const& renderPassState,
                          TfTokenVector const &renderTags) override;

    /// Update internal tracking to reflect a dirty collection.
    virtual void _MarkCollectionDirty() override {}

private:
    // A reference to the global scene version.
    std::atomic<int> *_sceneVersion;

    // The last scene version we rendered with.
    int _lastSceneVersion;

    // The last settings version we rendered with.
    int _lastSettingsVersion;

    // The width of the viewport we're rendering into.
    unsigned int _width;
    // The height of the viewport we're rendering into.
    unsigned int _height;

    // The view matrix: world space to camera space
    GfMatrix4d _viewMatrix;
    // The projection matrix: camera space to NDC space
    GfMatrix4d _projMatrix;

    GfMatrix4d _inverseViewMatrix;
    GfMatrix4d _inverseProjectionMatrix;

    // The list of aov buffers this renderpass should write to.
    HdRenderPassAovBindingVector _aovBindings;

    // Were the color/depth buffer converged the last time we blitted them?
    bool _converged;

    // A compositor utility class, for rendering the final result to the
    // viewport.
    HdxCompositor _compositor;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDLUXCORE_RENDER_PASS_H
