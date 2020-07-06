//
// Copyright 2017 Pixar and John Gann
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
#include "pxr/imaging/hdLuxCore/renderDelegate.h"

#include "pxr/imaging/hdLuxCore/instancer.h"
#include "pxr/imaging/hdLuxCore/renderParam.h"
#include "pxr/imaging/hdLuxCore/renderPass.h"
#include "pxr/imaging/hdLuxCore/camera.h"

#include "pxr/imaging/hd/extComputation.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "pxr/imaging/hd/tokens.h"


#include "pxr/imaging/hd/bprim.h"

#include <iostream>
#include <chrono>
#include <ctime>
using namespace std;

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdLuxCoreRenderSettingsTokens, HDLUXCORE_RENDER_SETTINGS_TOKENS);

const TfTokenVector HdLuxCoreRenderDelegate::SUPPORTED_RPRIM_TYPES =
{
    HdPrimTypeTokens->mesh,
};

const TfTokenVector HdLuxCoreRenderDelegate::SUPPORTED_SPRIM_TYPES =
{
    HdPrimTypeTokens->camera,
    HdPrimTypeTokens->extComputation,
    HdPrimTypeTokens->sphereLight
};

// Currently the plugin does not support textures and materials other than the default
// Bprims will need ot be supported if textures are to be implemented
const TfTokenVector HdLuxCoreRenderDelegate::SUPPORTED_BPRIM_TYPES =
{
};

std::mutex HdLuxCoreRenderDelegate::_mutexResourceRegistry;
std::atomic_int HdLuxCoreRenderDelegate::_counterResourceRegistry;
HdResourceRegistrySharedPtr HdLuxCoreRenderDelegate::_resourceRegistry;

/* static */
void
HdLuxCoreRenderDelegate::HandleLuxCoreError(const char* msg)
{
    cout << "void HdLuxCoreRenderDelegate::HandleLuxCoreError(const char* msg)\n";
   return;
}

HdLuxCoreRenderDelegate::HdLuxCoreRenderDelegate()
    : HdRenderDelegate()
    
{
    cout << "HdLuxCoreRenderDelegate::HdLuxCoreRenderDelegate()\n";
    _Initialize();
}

HdLuxCoreRenderDelegate::HdLuxCoreRenderDelegate(
    HdRenderSettingsMap const& settingsMap)
    : HdRenderDelegate(settingsMap)
{
    cout << "HdLuxCoreRenderDelegate::HdLuxCoreRenderDelegate(HdRenderSettingsMap settingsMap)\n";
    _Initialize();
}

void
HdLuxCoreRenderDelegate::_Initialize()
{
    cout << "HdLuxCoreRenderDelegate::_Initialize()\n";
    luxcore::Init();
    
    lc_scene = luxcore::Scene::Create();

    // Initialize reqiured default camera location coordinates
    lc_scene->Parse(luxrays::Properties() <<
                luxrays::Property("scene.camera.type")("perspective") <<
		luxrays::Property("scene.camera.lookat.orig")(-1000.0f , -1000.0f , -1000.0f));

    // LuxCore requires at least one light source in order to initialize the renderer
    lc_scene->Parse(
        luxrays::Property("scene.lights.light1.type")("point") <<
        luxrays::Property("scene.lights.light1.color")(0.0, 1.0, 0.0) <<
        luxrays::Property("scene.lights.light1.gain")(0.0, 0.0, 0.0) <<
        luxrays::Property("scene.lights.light1.direction")(1.0, 1.0, -1.0) <<
        luxrays::Property("scene.lights.light1.position")(-1000.55, -1000.95, -1000.66)
    );

    // Default material used for all renders
    lc_scene->Parse(
        luxrays::Property("scene.materials.mat_default.type")("matte") <<
        luxrays::Property("scene.materials.mat_default.kd")(.75f, .75f, .75f)
    );

    // Use the PATHCPU engine for development
    lc_config = luxcore::RenderConfig::Create(
        luxrays::Property("renderengine.type")("PATHCPU") <<
		luxrays::Property("sampler.type")("RANDOM"),
        lc_scene
    );

    lc_session = luxcore::RenderSession::Create(lc_config);

    // Store top-level objects inside a render param that can be
    // passed to prims during Sync(). Also pass a handle to the render thread.
    _renderParam = std::make_shared<HdLuxCoreRenderParam>(
        lc_scene, lc_config, lc_session, &_sceneVersion);

    // Initialize one resource registry for all plugins
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);

    if (_counterResourceRegistry.fetch_add(1) == 0) {
        _resourceRegistry.reset( new HdResourceRegistry() );
    }
}

HdLuxCoreRenderDelegate::~HdLuxCoreRenderDelegate()
{
    cout << "HdLuxCoreRenderDelegate::~HdLuxCoreRenderDelegate()\n";
    {
        std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
        if (_counterResourceRegistry.fetch_sub(1) == 1) {
            _resourceRegistry.reset();
        }
    }

    lc_session->Stop();

    _renderParam.reset();
}

HdRenderSettingDescriptorList
HdLuxCoreRenderDelegate::GetRenderSettingDescriptors() const
{
    cout << "HdLuxCoreRenderDelegate::GetRenderSettingDescriptors()\n";
    return _settingDescriptors;
}

HdRenderParam*
HdLuxCoreRenderDelegate::GetRenderParam() const
{
    cout << "HdLuxCoreRenderDelegate::GetRenderParam()\n";
    return _renderParam.get();
}

void
HdLuxCoreRenderDelegate::CommitResources(HdChangeTracker *tracker)
{
    cout << "HdLuxCoreRenderDelegate::CommitResources(HdChangeTracker *tracker)\n";
}

TfTokenVector const&
HdLuxCoreRenderDelegate::GetSupportedRprimTypes() const
{
    cout << "HdLuxCoreRenderDelegate::GetSupportedRprimTypes()\n";
    return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const&
HdLuxCoreRenderDelegate::GetSupportedSprimTypes() const
{
    cout << "HdLuxCoreRenderDelegate::GetSupportedSprimTypes()\n";
    return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const&
HdLuxCoreRenderDelegate::GetSupportedBprimTypes() const
{
    cout << "HdLuxCoreRenderDelegate::GetSupportedBprimTypes()\n";
    return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr
HdLuxCoreRenderDelegate::GetResourceRegistry() const
{
    cout << "HdLuxCoreRenderDelegate::GetResourceRegistry()\n";
    return _resourceRegistry;
}

HdAovDescriptor
HdLuxCoreRenderDelegate::GetDefaultAovDescriptor(TfToken const& name) const
{
    cout << "HdLuxCoreRenderDelegate::GetDefaultAovDescriptor(TfToken const& name) const\n";
    if (name == HdAovTokens->color) {
        return HdAovDescriptor(HdFormatUNorm8Vec4, true,
                               VtValue(GfVec4f(0.0f)));
    } else if (name == HdAovTokens->normal || name == HdAovTokens->Neye) {
        return HdAovDescriptor(HdFormatFloat32Vec3, false,
                               VtValue(GfVec3f(-1.0f)));
    } else if (name == HdAovTokens->depth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(1.0f));
    } else if (name == HdAovTokens->linearDepth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(0.0f));
    } else if (name == HdAovTokens->primId ||
               name == HdAovTokens->instanceId ||
               name == HdAovTokens->elementId) {
        return HdAovDescriptor(HdFormatInt32, false, VtValue(-1));
    } else {
        HdParsedAovToken aovId(name);
        if (aovId.isPrimvar) {
            return HdAovDescriptor(HdFormatFloat32Vec3, false,
                                   VtValue(GfVec3f(0.0f)));
        }
    }

    return HdAovDescriptor();
}

HdRenderPassSharedPtr
HdLuxCoreRenderDelegate::CreateRenderPass(HdRenderIndex *index,
                            HdRprimCollection const& collection)
{
    cout << "HdLuxCoreRenderDelegate::CreateRenderPass(HdRenderIndex *index, HdRprimeCollection const& collection)\n";
    return HdRenderPassSharedPtr(new HdLuxCoreRenderPass(
        index, collection, &_sceneVersion));
}

HdInstancer *
HdLuxCoreRenderDelegate::CreateInstancer(HdSceneDelegate *delegate,
                                        SdfPath const& id,
                                        SdfPath const& instancerId)
{
    cout << "HdLuxCoreRenderDelegate::CreateInstancer(HdSceneDelegate *delegate, SdfPath const& id, SdfPath const& instancerId)\n";
    return new HdLuxCoreInstancer(delegate, id, instancerId);
}

void
HdLuxCoreRenderDelegate::DestroyInstancer(HdInstancer *instancer)
{
    cout << "HdLuxCoreRenderDelegate::DestroyInstancer(HdInstancer *instancer)\n";
    delete instancer;
}

HdRprim *
HdLuxCoreRenderDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId,
                                    SdfPath const& instancerId)
{
    cout << "HdLuxCoreRenderDelegate::CreateRprim(TfToken const& typeId, SdfPath const& rprimId, SdfPath const& instancerId)\n";
    if (typeId == HdPrimTypeTokens->mesh) {
        HdLuxCoreMesh *mesh = new HdLuxCoreMesh(rprimId, instancerId);
        _rprimMap[rprimId.GetString()] = mesh;
        return mesh;
    } else {
        TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());
    }

    return nullptr;
}

void
HdLuxCoreRenderDelegate::DestroyRprim(HdRprim *rPrim)
{
    cout << "HdLuxCoreRenderDelegate::DestroyRprim(HdRprim *rPrim)\n";
    delete rPrim;
}

HdSprim *
HdLuxCoreRenderDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId)
{
    cout << "HdLuxCoreRenderDelegate::CreateSprim(TfToken const& typeId, SdfPath const& sprimId) " << sprimId << "\n";
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdLuxCoreCamera(sprimId);
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(sprimId);
    } else if (typeId == HdPrimTypeTokens->sphereLight) {
        HdLuxCoreLight *light = new HdLuxCoreLight(sprimId, typeId);
        _sprimLightMap[sprimId.GetString()] = light;
        return light;
    } else {
        TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    }

    return nullptr;
}

HdSprim *
HdLuxCoreRenderDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    cout << "HdLuxCoreRenderDelegate::CreateFallbackSprim(TfToken const& typeId)\n";
    // For fallback sprims, create objects with an empty scene path.
    // They'll use default values and won't be updated by a scene delegate.
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->sphereLight) {
        return new HdLuxCoreLight(SdfPath::EmptyPath(), typeId);
    } else {
        TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    }

    return nullptr;
}

void
HdLuxCoreRenderDelegate::DestroySprim(HdSprim *sPrim)
{
    cout << "HdLuxCoreRenderDelegate::DestroySprim(HdSprim *sPrim)\n";

    delete sPrim;
}

HdBprim *
HdLuxCoreRenderDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId)
{
    cout << "HdLuxCoreRenderDelegate::CreateBprim(TfToken const& typeId, SdfPath const& bprimId)\n";

    return nullptr;
}

HdBprim *
HdLuxCoreRenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    cout << "HdLuxCoreRenderDelegate::CreateFallbackBprim(TfToken const& typeId)\n";

    return nullptr;
}

void
HdLuxCoreRenderDelegate::DestroyBprim(HdBprim *bPrim)
{
    cout << "HdLuxCoreRenderDelegate::DestroyBprim(HdBprim *bPrim)\n";
    delete bPrim;
}

PXR_NAMESPACE_CLOSE_SCOPE



int logit(std::string message)
{
    auto timenow = chrono::system_clock::to_time_t(chrono::system_clock::now()); 
    cout << "LOG: " << ctime(&timenow) << " Thread: " << std::this_thread::get_id() << message << endl;

    return 0;
}
