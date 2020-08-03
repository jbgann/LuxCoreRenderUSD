#ifndef HDLUXCORE_LIGHT_H
#define HDLUXCORE_LIGHT_H

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/usd/usdLux/tokens.h"
#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdLuxCoreLight : public HdLight {
    public:
        HdLuxCoreLight(SdfPath const& id, TfToken const& lightType)
            : HdLight(id), _lightType(lightType) {
                _created = false;
            }

        ~HdLuxCoreLight() override = default;

        void Sync(HdSceneDelegate* sceneDelegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

        HdDirtyBits GetInitialDirtyBitsMask() const override;

        void Finalize(HdRenderParam* renderParam) override;

        virtual GfMatrix4d GetLightTransform() const {
            return _transform;
        }
        
        virtual float GetIntensity() const {
            return _intensity;
        }

		virtual float GetExposure() const {
			return _exposure;
		}
        
        virtual GfVec3f GetColor() const {
            return _color;
        }

        virtual const TfToken GetLightType() const {
            return _lightType;
        }

        virtual bool GetCreated() const {
            return _created;
        }

        virtual void SetCreated(bool created) {
            _created = created;
        }

	virtual bool GetTreatAsPoint() const {
            return _treatAsPoint;
        }

    private:
        GfMatrix4d _transform;
        float _intensity = 1.0;
	float _exposure = 1.0;
        GfVec3f _color = GfVec3f(1.0f);
        const TfToken _lightType;
        bool _created;
        bool _treatAsPoint;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDLUXCORE_LIGHT_H
