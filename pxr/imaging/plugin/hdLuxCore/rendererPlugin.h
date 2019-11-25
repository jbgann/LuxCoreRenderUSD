#ifndef HDLUXCORE_RENDERER_PLUGIN_H
#define HDLUXCORE_RENDERER_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/imaging/hdx/rendererPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE


class HdLuxCoreRendererPlugin final : public HdxRendererPlugin {
public:
    HdLuxCoreRendererPlugin() = default;
    virtual ~HdLuxCoreRendererPlugin() = default;

    virtual HdRenderDelegate *CreateRenderDelegate() override;

    virtual HdRenderDelegate *CreateRenderDelegate(
        HdRenderSettingsMap const& settingsMap) override;

    virtual void DeleteRenderDelegate(
        HdRenderDelegate *renderDelegate) override;
    
    virtual bool IsSupported() const override;

private:
    HdLuxCoreRendererPlugin(const HdLuxCoreRendererPlugin&)             = delete;
    HdLuxCoreRendererPlugin &operator =(const HdLuxCoreRendererPlugin&) = delete;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDLUXCORE_RENDERER_PLUGIN_H
