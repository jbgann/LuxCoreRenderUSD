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
                                       std::atomic<int> *sceneVersion)
    : HdRenderPass(index, collection)
    , _sceneVersion(sceneVersion)
    , _lastSceneVersion(0)
    , _lastSettingsVersion(0)
    , _width(0)
    , _height(0)
    , _viewMatrix(1.0f) // == identity
    , _projMatrix(1.0f) // == identity
    , _aovBindings()
    , _converged(false)
{
    cout << "HdLuxCoreRenderPass::HdLuxCoreRenderPass()\n";
}

HdLuxCoreRenderPass::~HdLuxCoreRenderPass()
{
    cout << "HdLuxCoreRenderPass::~HdLuxCoreRenderPass()\n";
}

bool
HdLuxCoreRenderPass::IsConverged() const
{
    cout << "HdLuxCoreRenderPass::IsConverged()\n";

    return _converged;
}

void
HdLuxCoreRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState,
                             TfTokenVector const &renderTags)
{
    logit("HdLuxCoreRenderPass::_Execute()");

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
        _converged = false;
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
                TfMatrix4dVector transforms = mesh->GetTransforms();
                // We can assume that there will always be one transform per mesh prototype
                for (size_t i = 0; i < transforms.size(); i++)
                {
                    std::string instanceName = mesh->GetId().GetString() + std::to_string(i);
                    lc_scene->Parse(
                        luxrays::Property("scene.objects." + instanceName + ".shape")(mesh->GetId().GetString()) <<
                        luxrays::Property("scene.objects." + instanceName + ".material")("mat_red")
                    );
                    GfMatrix4d *t = transforms[i];
                    GfMatrix4f m = GfMatrix4f(*t);
                    lc_scene->UpdateObjectTransformation(instanceName, m.GetArray());
                }
            }
        }
    }

    // Render any lighting
    TfHashMap<std::string, HdLuxCoreLight*> lightMap = renderDelegateLux->_sprimLightMap;
    TfHashMap<std::string, HdLuxCoreLight*>::iterator l_iter;

    for (l_iter = lightMap.begin(); l_iter != lightMap.end(); ++l_iter) {
        HdLuxCoreLight *light = l_iter->second;
        if (!light->GetCreated()) {
            light->SetCreated(true);
            GfMatrix4d transform = light->GetLightTransform();
            std::string light_id = light->GetId().GetString();
            GfVec3f color = light->GetColor();
            lc_scene->Parse(
                luxrays::Property("scene.lights." + light_id + ".type")("point") <<
                luxrays::Property("scene.lights." + light_id + ".color")(color[0], color[1], color[2]) <<
                luxrays::Property("scene.lights." + light_id + ".position")(transform[3][0], transform[3][1], transform[3][2])
            );
        }
    }

    lc_session->EndSceneEdit();
    lc_session->Resume();

    // Determine if the scene has finished rendering
    if (lc_session->HasDone())
        _converged = true;

    // Copy the LuxCore film render into a buffer
    unique_ptr<float[]> pxl_buffer(new float[_width * _height * 3]);
    lc_session->GetFilm().GetOutput<float>(Film::OUTPUT_RGB_IMAGEPIPELINE, pxl_buffer.get(), 0);

    // Draw the buffer to the OpenGL viewport
    glDrawPixels(_width, _height, GL_RGB, GL_FLOAT, &pxl_buffer[0]);
}

PXR_NAMESPACE_CLOSE_SCOPE
