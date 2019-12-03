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

#include "pxr/imaging/hdLuxCore/config.h"
#include "pxr/imaging/hdLuxCore/instancer.h"
#include "pxr/imaging/hdLuxCore/renderParam.h"
#include "pxr/imaging/hdLuxCore/renderPass.h"

#include "pxr/imaging/hd/extComputation.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/imaging/hdLuxCore/mesh.h"
//XXX: Add other Rprim types later
#include "pxr/imaging/hd/camera.h"
//XXX: Add other Sprim types later
#include "pxr/imaging/hd/bprim.h"
//XXX: Add bprim types

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
};

const TfTokenVector HdLuxCoreRenderDelegate::SUPPORTED_BPRIM_TYPES =
{
    HdPrimTypeTokens->renderBuffer,
};

std::mutex HdLuxCoreRenderDelegate::_mutexResourceRegistry;
std::atomic_int HdLuxCoreRenderDelegate::_counterResourceRegistry;
HdResourceRegistrySharedPtr HdLuxCoreRenderDelegate::_resourceRegistry;

/* static */
void
HdLuxCoreRenderDelegate::HandleLuxCoreError(const char* msg)
{
   return;
}

static void _RenderCallback(HdLuxCoreRenderer *renderer,
                            HdRenderThread *renderThread)
{
    renderer->Clear();
    renderer->Render(renderThread);
}

HdLuxCoreRenderDelegate::HdLuxCoreRenderDelegate()
    : HdRenderDelegate()
{
    _Initialize();
}

HdLuxCoreRenderDelegate::HdLuxCoreRenderDelegate(
    HdRenderSettingsMap const& settingsMap)
    : HdRenderDelegate(settingsMap)
{
    _Initialize();
}

void
HdLuxCoreRenderDelegate::_Initialize()
{
    luxcore::Init();
    
    lc_scene = luxcore::Scene::Create();

    lc_scene->Parse(
				luxcore::Property("scene.camera.lookat.orig")(1.f , 6.f , 3.f) <<
				luxcore::Property("scene.camera.lookat.target")(0.f , 0.f , .5f) <<
				luxcore::Property("scene.camera.fieldofview")(60.f));

    lc_config = luxcore::RenderConfig::Create(
        Property("renderengine.type")("PATHCPU") <<
		Property("sampler.type")("RANDOM"),
        lc_scene
    );

    lc_session = luxcore::RenderSession::Create(lc_config);

    // Store top-level embree objects inside a render param that can be
    // passed to prims during Sync(). Also pass a handle to the render thread.
    _renderParam = std::make_shared<HdLuxCoreRenderParam>(
        lc_scene, lc_config, lc_session, &_sceneVersion);

    // Initialize one resource registry for all embree plugins
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);

    if (_counterResourceRegistry.fetch_add(1) == 0) {
        _resourceRegistry.reset( new HdResourceRegistry() );
    }
}

HdLuxCoreRenderDelegate::~HdLuxCoreRenderDelegate()
{
    // Clean the resource registry only when it is the last Embree delegate
    {
        std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
        if (_counterResourceRegistry.fetch_sub(1) == 1) {
            _resourceRegistry.reset();
        }
    }

    // Destroy embree library and scene state.
    _renderParam.reset();
}

HdRenderSettingDescriptorList
HdLuxCoreRenderDelegate::GetRenderSettingDescriptors() const
{
    return _settingDescriptors;
}

HdRenderParam*
HdLuxCoreRenderDelegate::GetRenderParam() const
{
    return _renderParam.get();
}

void
HdLuxCoreRenderDelegate::CommitResources(HdChangeTracker *tracker)
{
}

TfTokenVector const&
HdLuxCoreRenderDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const&
HdLuxCoreRenderDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const&
HdLuxCoreRenderDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr
HdLuxCoreRenderDelegate::GetResourceRegistry() const
{
    return _resourceRegistry;
}

HdAovDescriptor
HdLuxCoreRenderDelegate::GetDefaultAovDescriptor(TfToken const& name) const
{
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
    return HdRenderPassSharedPtr(new HdLuxCoreRenderPass(
        index, collection, _renderParam, &_sceneVersion));
}

HdInstancer *
HdLuxCoreRenderDelegate::CreateInstancer(HdSceneDelegate *delegate,
                                        SdfPath const& id,
                                        SdfPath const& instancerId)
{
    return new HdLuxCoreInstancer(delegate, id, instancerId);
}

void
HdLuxCoreRenderDelegate::DestroyInstancer(HdInstancer *instancer)
{
    delete instancer;
}

HdRprim *
HdLuxCoreRenderDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId,
                                    SdfPath const& instancerId)
{
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdLuxCoreMesh(rprimId, instancerId);
    } else {
        TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());
    }

    return nullptr;
}

void
HdLuxCoreRenderDelegate::DestroyRprim(HdRprim *rPrim)
{
    delete rPrim;
}

HdSprim *
HdLuxCoreRenderDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId)
{
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(sprimId);
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(sprimId);
    } else {
        TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    }

    return nullptr;
}

HdSprim *
HdLuxCoreRenderDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    // For fallback sprims, create objects with an empty scene path.
    // They'll use default values and won't be updated by a scene delegate.
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(SdfPath::EmptyPath());
    } else {
        TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    }

    return nullptr;
}

void
HdLuxCoreRenderDelegate::DestroySprim(HdSprim *sPrim)
{
    delete sPrim;
}

HdBprim *
HdLuxCoreRenderDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdLuxCoreRenderBuffer(bprimId);
    } else {
        TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    }
    return nullptr;
}

HdBprim *
HdLuxCoreRenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdLuxCoreRenderBuffer(SdfPath::EmptyPath());
    } else {
        TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    }
    return nullptr;
}

void
HdLuxCoreRenderDelegate::DestroyBprim(HdBprim *bPrim)
{
    delete bPrim;
}

PXR_NAMESPACE_CLOSE_SCOPE
