#include "pxr/imaging/hdLuxCore/light.h"
#include "pxr/imaging/hdLuxCore/renderDelegate.h"

using namespace std;

PXR_NAMESPACE_OPEN_SCOPE

void HdLuxCoreLight::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    logit(BOOST_CURRENT_FUNCTION);

    if (*dirtyBits & HdLight::DirtyTransform)
    {
        VtValue transform = sceneDelegate->Get(GetId(), HdLightTokens->transform);
        if (transform.IsHolding<GfMatrix4d>())
            _transform = transform.Get<GfMatrix4d>();
        else
            _transform = GfMatrix4d(1);
    }

    if (*dirtyBits & HdLight::DirtyParams)
    {
        _color = sceneDelegate->GetLightParamValue(GetId(), HdPrimvarRoleTokens->color).Get<GfVec3f>();
        _intensity = sceneDelegate->GetLightParamValue(GetId(), HdLightTokens->intensity).Get<float>();
        _exposure = sceneDelegate->GetLightParamValue(GetId(), HdLightTokens->exposure).Get<float>();
        _treatAsPoint = sceneDelegate->GetLightParamValue(GetId(), UsdLuxTokens->treatAsPoint).GetWithDefault(false);
    }
}

void HdLuxCoreLight::Finalize(HdRenderParam* renderParam)
{
    logit(BOOST_CURRENT_FUNCTION);
}

HdDirtyBits HdLuxCoreLight::GetInitialDirtyBitsMask() const {
    logit(BOOST_CURRENT_FUNCTION);

    return DirtyBits::DirtyTransform
         | DirtyBits::DirtyParams;
}

PXR_NAMESPACE_CLOSE_SCOPE
