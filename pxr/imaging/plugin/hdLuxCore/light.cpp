#include "pxr/imaging/hdLuxCore/light.h"
#include "pxr/imaging/hdLuxCore/renderDelegate.h"

using namespace std;

PXR_NAMESPACE_OPEN_SCOPE

void HdLuxCoreLight::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) {
    cout << "HdLuxCoreLight::Sync\n" << std::flush;

    _transform = sceneDelegate->GetTransform(GetId());
    _color = sceneDelegate->GetLightParamValue(GetId(), HdPrimvarRoleTokens->color).Get<GfVec3f>();
    _intensity = sceneDelegate->GetLightParamValue(GetId(), HdLightTokens->intensity).Get<float>();
}

void HdLuxCoreLight::Finalize(HdRenderParam* renderParam) {
}

HdDirtyBits HdLuxCoreLight::GetInitialDirtyBitsMask() const {
    return DirtyBits::DirtyTransform
         | DirtyBits::DirtyParams;
}

PXR_NAMESPACE_CLOSE_SCOPE