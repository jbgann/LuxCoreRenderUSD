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
#include "pxr/imaging/glf/glew.h"

#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hdLuxCore/renderDelegate.h"
#include "pxr/imaging/hdLuxCore/renderPass.h"
#include "pxr/imaging/hdLuxCore/renderParam.h"

#include <iostream>
using namespace std;

PXR_NAMESPACE_OPEN_SCOPE

HdLuxCoreRenderPass::HdLuxCoreRenderPass(HdRenderIndex *index,
                                       HdRprimCollection const &collection,
                                       HdRenderThread *renderThread,
                                       HdLuxCoreRenderer *renderer,
                                       std::atomic<int> *sceneVersion)
    : HdRenderPass(index, collection)
    , _renderThread(renderThread)
    , _renderer(renderer)
    , _sceneVersion(sceneVersion)
    , _lastSceneVersion(0)
    , _lastSettingsVersion(0)
    , _width(0)
    , _height(0)
    , _viewMatrix(1.0f) // == identity
    , _projMatrix(1.0f) // == identity
    , _aovBindings()
    , _colorBuffer(SdfPath::EmptyPath())
    , _depthBuffer(SdfPath::EmptyPath())
    , _converged(false)
{
    cout << "HdLuxCoreRenderPass::HdLuxCoreRenderPass()\n";
}

HdLuxCoreRenderPass::~HdLuxCoreRenderPass()
{
    cout << "HdLuxCoreRenderPass::~HdLuxCoreRenderPass()\n";
    // Make sure the render thread's not running, in case it's writing
    // to _colorBuffer/_depthBuffer.
    _renderThread->StopRender();
}

bool
HdLuxCoreRenderPass::IsConverged() const
{
    cout << "HdLuxCoreRenderPass::IsConverged()\n";
    // If the aov binding array is empty, the render thread is rendering into
    // _colorBuffer and _depthBuffer.  _converged is set to their convergence
    // state just before blit, so use that as our answer.
    if (_aovBindings.size() == 0) {
        return _converged;
    }

    // Otherwise, check the convergence of all attachments.
    for (size_t i = 0; i < _aovBindings.size(); ++i) {
        if (_aovBindings[i].renderBuffer &&
            !_aovBindings[i].renderBuffer->IsConverged()) {
            return false;
        }
    }
    return true;
}

void
HdLuxCoreRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState,
                             TfTokenVector const &renderTags)
{
    cout << "HdLuxCoreRenderPass::_Execute()\n";

    HdRenderDelegate *renderDelegate = GetRenderIndex()->GetRenderDelegate();
    HdLuxCoreRenderDelegate *renderDelegateLux = reinterpret_cast<HdLuxCoreRenderDelegate*>(renderDelegate);
    HdRenderParam *renderParam = renderDelegate->GetRenderParam();
    Scene *lc_scene = reinterpret_cast<HdLuxCoreRenderParam*>(renderParam)->_scene;

    // Retrieve the LuxCore render session
    RenderSession *lc_session = reinterpret_cast<HdLuxCoreRenderParam*>(renderParam)->_session;

    // Set the width and height to match the current viewport
    GfVec4f viewport = renderPassState->GetViewport();
    if (_width != viewport[2] || _height != viewport[3]) {
        _width = viewport[2];
        _height = viewport[3];
        lc_session->Pause();
        lc_session->Parse(
            luxrays::Property("film.width")(_width) <<
		    luxrays::Property("film.height")(_height)
        );
        lc_session->Resume();
    }

    GfMatrix4d current_inverseViewMatrix = renderPassState->GetWorldToViewMatrix().GetInverse();
    GfMatrix4d current_inverseProjectionMatrix = renderPassState->GetProjectionMatrix().GetInverse();

    // Has the view or projection matrix changed?  Reset the camera if so.
    if (current_inverseViewMatrix != _inverseViewMatrix || current_inverseProjectionMatrix != _inverseProjectionMatrix) {
        _inverseViewMatrix = current_inverseViewMatrix;
        _inverseProjectionMatrix = current_inverseProjectionMatrix;

        // The calculations in the following two code blocks are borrowed from the excellent hdospray project
        // Source: https://github.com/ospray/hdospray
        GfVec3f origin = GfVec3f(0, 0, 0);
        GfVec3f direction = GfVec3f(0, 0, -1);
        GfVec3f up = GfVec3f(0, 1, 0);
        double projectionMatrix[4][4];
        float fieldOfView;

        renderPassState->GetProjectionMatrix().Get(projectionMatrix);
        fieldOfView = (atan(1.0 / projectionMatrix[1][1]) * 180.0 * 2.0) / M_PI;
        direction = _inverseProjectionMatrix.Transform(direction);
        direction = _inverseViewMatrix.TransformDir(direction).GetNormalized();
        up = _inverseViewMatrix.TransformDir(up).GetNormalized();
        origin = _inverseViewMatrix.Transform(origin);

        cout << "scene.camera.lookat.orig: " << origin[0] << " " << origin[1] << " " << origin[2] << "\n" << std::flush;
        // Stopping the session allows the camera to be reset
        lc_session->Stop();
        lc_scene->Parse(luxrays::Properties() <<
            luxrays::Property("scene.camera.type")("perspective") <<
            luxrays::Property("scene.camera.lookat.orig")(origin[0], origin[1], origin[2]) <<
            luxrays::Property("scene.camera.lookat.target")(direction[0], direction[1], direction[2]) <<
            luxrays::Property("scene.camera.up")(up[0], up[1], up[2]) <<
            luxrays::Property("scene.camera.fieldofview")(fieldOfView)
            );
        lc_session->Start();
    }

    lc_session->Pause();
    lc_session->BeginSceneEdit();

    // Create the LuxCore Mesh Prototype
    TfHashMap<std::string, HdLuxCoreMesh*> meshMap = renderDelegateLux->_rprimMap;
    TfHashMap<std::string, HdLuxCoreMesh*>::iterator iter;

    // Instantiate LuxCore mesh instances
    for (iter = meshMap.begin(); iter != meshMap.end(); ++iter) {
        HdLuxCoreMesh *mesh = iter->second;
        if (!lc_scene->IsMeshDefined(mesh->GetId().GetString())) {
            if (mesh->CreateLuxCoreTriangleMesh(renderParam)) {
                // Always instantiate at least one mesh instance for each mesh prototype
                for (size_t i = 0; i <= mesh->GetTransforms().size(); i++)
                {
                    std::string instanceName = mesh->GetId().GetString() + std::to_string(i);
                    lc_scene->Parse(
                        luxrays::Property("scene.objects." + instanceName + ".shape")(mesh->GetId().GetString()) <<
                        luxrays::Property("scene.objects." + instanceName + ".material")("mat_red")
                    );
                }
            }
        }
    }

    lc_session->EndSceneEdit();
    lc_session->Resume();

    // Copy the LuxCore film render into a buffer
    unique_ptr<float[]> pxl_buffer(new float[_width * _height * 3]);
    lc_session->GetFilm().GetOutput<float>(Film::OUTPUT_RGB_IMAGEPIPELINE, pxl_buffer.get(), 0);

    // Draw the buffer to the OpenGL viewport
    glDrawPixels(_width, _height, GL_RGB, GL_FLOAT, &pxl_buffer[0]);
}

PXR_NAMESPACE_CLOSE_SCOPE
