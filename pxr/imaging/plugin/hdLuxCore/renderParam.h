//
// Copyright 2017 Pixar
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
#ifndef HDEMBREE_RENDER_PARAM_H
#define HDEMBREE_RENDER_PARAM_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderThread.h"

#include <luxcore/luxcore.h>

using namespace luxcore;

#include <iostream>
using namespace std;

PXR_NAMESPACE_OPEN_SCOPE

///
/// \class HdLuxCoreRenderParam
///
/// The render delegate can create an object of type HdRenderParam, to pass
/// to each prim during Sync(). HdLuxCore uses this class to pass top-level
/// LuxCore state around.
/// 
class HdLuxCoreRenderParam final : public HdRenderParam {
public:
    HdLuxCoreRenderParam(Scene *scene,
                        RenderConfig *config,
                        RenderSession *session,
                        std::atomic<int> *sceneVersion)
        : _scene(scene), _config(config), _session(session), _sceneVersion(sceneVersion)
        {}
    virtual ~HdLuxCoreRenderParam() = default;

    /// Accessor for the top-level LuxCore scene.
    Scene* AcquireSceneForEdit() {
        cout << "A\n";
        //_renderThread->StopRender();
        cout << "B\n";
        //(*_sceneVersion)++;
        cout << "C\n";
        return _scene;
    }

private:
    /// A handle to the top-level LuxCore scene.
    Scene *_scene;
    RenderConfig *_config;
    RenderSession *_session;
    /// A version counter for edits to _scene.
    std::atomic<int> *_sceneVersion;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDEMBREE_RENDER_PARAM_H
