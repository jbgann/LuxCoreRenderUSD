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
    logit(BOOST_CURRENT_FUNCTION);
}

HdLuxCoreRenderPass::~HdLuxCoreRenderPass()
{
    logit(BOOST_CURRENT_FUNCTION);
}

bool
HdLuxCoreRenderPass::IsConverged() const
{
    logit(BOOST_CURRENT_FUNCTION);

    return _converged;
}

void
HdLuxCoreRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState,
                             TfTokenVector const &renderTags)
{
    logit(BOOST_CURRENT_FUNCTION);

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
        GfVec3d origin = GfVec3d(0, 0, 0);
        GfVec3d direction = GfVec3d(0, 0, -1);
        GfVec3d up = GfVec3d(0, 1, 0);
        double projectionMatrix[4][4];
        double fieldOfView;

        renderPassState->GetProjectionMatrix().Get(projectionMatrix);
        fieldOfView = (atan(1.0 / projectionMatrix[1][1]) * 180.0 * 2.0) / M_PI;
        direction = _inverseProjectionMatrix.Transform(direction);
        direction = _inverseViewMatrix.TransformDir(direction).GetNormalized();
        up = _inverseViewMatrix.TransformDir(up).GetNormalized();
        origin = _inverseViewMatrix.Transform(origin);

        // Stopping the session allows the camera to be reset
        lc_session->Stop();
        lc_scene->Parse(luxrays::Properties() <<
            luxrays::Property("scene.camera.type")("perspective") <<
            luxrays::Property("scene.camera.lookat.orig")(origin[0], origin[1], origin[2]) <<
            luxrays::Property("scene.camera.lookat.target")(direction[0], direction[1], direction[2]) <<
            luxrays::Property("scene.camera.up")(up[0], up[1], up[2]) <<
            luxrays::Property("scene.camera.fieldofview")(32.0)
            );
        lc_session->Start();
            cout << "C:" << origin[0] << " " << origin[1] << " " << origin[2] << "\r\n";
    }

    lc_session->Pause();
    lc_session->BeginSceneEdit();

    // Create the LuxCore Mesh Prototype
    TfHashMap<std::string, HdLuxCoreMesh*> meshMap = renderDelegateLux->_rprimMap;
    TfHashMap<std::string, HdLuxCoreMesh*>::iterator iter;

    // Instantiate LuxCore mesh instances
    for (iter = meshMap.begin(); iter != meshMap.end(); ++iter) {
        HdLuxCoreMesh *mesh = iter->second;
		TfMatrix4dVector transforms = mesh->GetTransforms();;

		if (!lc_scene->IsMeshDefined(mesh->GetId().GetString())) {
			mesh->CreateLuxCoreTriangleMesh(renderParam);
		}

		if (mesh->IsVisible() && mesh->GetInstancesRendered() != transforms.size()) {
			// We can assume that there will always be one transform per mesh prototype
			for (size_t i = 0; i < transforms.size(); i++)
			{
				GfMatrix4d *t = transforms[i];
				GfMatrix4f m = GfMatrix4f(*t);

				std::string instanceName = mesh->GetId().GetString() + std::to_string(i);
				lc_scene->Parse(
					luxrays::Property("scene.objects." + instanceName + ".shape")(mesh->GetId().GetString()) <<
					luxrays::Property("scene.objects." + instanceName + ".material")("mat_default")
				);
				lc_scene->UpdateObjectTransformation(instanceName, m.GetArray());
			}
			mesh->SetInstancesRendered(transforms.size());
		}
		else {
			if (!mesh->IsVisible() && mesh->GetInstancesRendered() > 0) {
				// We can assume that there will always be one transform per mesh prototype
				for (size_t i = 0; i < transforms.size(); i++)
				{
					GfMatrix4d *t = transforms[i];
					GfMatrix4f m = GfMatrix4f(*t);

					std::string instanceName = mesh->GetId().GetString() + std::to_string(i);
					// Work around a bug in LuxCore -- remove the previous transformation so
					// it doesn't get re-added if/when the object is re-instanced
					lc_scene->UpdateObjectTransformation(instanceName, m.GetInverse().GetArray());
					lc_scene->DeleteObject(instanceName);
				}
				mesh->SetInstancesRendered(0);
			}
		}
    }

    // Render any lighting
    TfHashMap<std::string, HdLuxCoreLight*> lightMap = renderDelegateLux->_sprimLightMap;
    TfHashMap<std::string, HdLuxCoreLight*>::iterator l_iter;
    std::string light_type;

    // If we already have lighting, remove the default light
    if (lightMap.size() > 0) {
        lc_scene->DeleteLight("light_default");
    }

    for (l_iter = lightMap.begin(); l_iter != lightMap.end(); ++l_iter) {
        HdLuxCoreLight *light = l_iter->second;
        if (!light->GetCreated()) {
            light->SetCreated(true);
            GfMatrix4d transform = light->GetLightTransform();
            std::string light_id = light->GetId().GetString();
            GfVec3f color = light->GetColor();
            if (light->GetTreatAsPoint())
                light_type = "point";
            else
                light_type = "sphere";
            lc_scene->Parse(
                luxrays::Property("scene.lights." + light_id + ".type")(light_type) <<
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

    lc_session->GetFilm().SaveOutputs();
    // Copy the LuxCore film render into a buffer
    unique_ptr<float[]> pxl_buffer(new float[_width * _height * 3]);
    lc_session->GetFilm().GetOutput<float>(Film::OUTPUT_RGB_IMAGEPIPELINE, pxl_buffer.get(), 0);

    // Draw the buffer to the OpenGL viewport
    glDrawPixels(_width, _height, GL_RGB, GL_FLOAT, &pxl_buffer[0]);
}

PXR_NAMESPACE_CLOSE_SCOPE
