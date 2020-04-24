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
#ifndef HDLUXCORE_RENDER_DELEGATE_H
#define HDLUXCORE_RENDER_DELEGATE_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/imaging/hdLuxCore/renderer.h"
#include "pxr/imaging/hdLuxCore/mesh.h"
#include "pxr/imaging/hdLuxCore/light.h"

#include <luxcore/luxcore.h>
#include <mutex>


int logit(std::string message);

PXR_NAMESPACE_OPEN_SCOPE

class HdLuxCoreRenderParam;

#define HDLUXCORE_RENDER_SETTINGS_TOKENS \
    (enableAmbientOcclusion)            \
    (enableSceneColors)                 \
    (ambientOcclusionSamples)

// Also: HdRenderSettingsTokens->convergedSamplesPerPixel

TF_DECLARE_PUBLIC_TOKENS(HdLuxCoreRenderSettingsTokens, HDLUXCORE_RENDER_SETTINGS_TOKENS);

///
/// \class HdLuxCoreRenderDelegate
///
/// Render delegates provide renderer-specific functionality to the render
/// index, the main hydra state management structure. The render index uses
/// the render delegate to create and delete scene primitives, which include
/// geometry and also non-drawable objects. The render delegate is also
/// responsible for creating renderpasses, which know how to draw this
/// renderer's scene primitives.
///
/// Primitives in Hydra are split into Rprims (drawables), Sprims (state
/// objects like cameras and materials), and Bprims (buffer objects like
/// textures). The minimum set of primitives a renderer needs to support is
/// one Rprim (so the scene's not empty) and the "camera" Sprim, which is
/// required by HdxRenderTask, the task implementing basic hydra drawing.
///
/// A render delegate can report which prim types it supports via
/// GetSupportedRprimTypes() (and Sprim, Bprim), and well-behaved applications
/// won't call CreateRprim() (Sprim, Bprim) for prim types that aren't
/// supported. The core hydra prim types are "mesh", "basisCurves", and
/// "points", but a custom render delegate and a custom scene delegate could
/// add support for other prims such as implicit surfaces or volumes.
///
/// HdLuxCore Rprims create LuxCore geometry objects in the render delegate's
/// top-level LuxCore scene; and HdLuxCore's render pass draws by casting rays
/// into the top-level scene. The renderpass writes to the currently bound GL
/// framebuffer.
///
/// The render delegate also has a hook for the main hydra execution algorithm
/// (HdEngine::Execute()): between HdRenderIndex::SyncAll(), which pulls new
/// scene data, and execution of tasks, the engine calls back to
/// CommitResources(). This can be used to commit GPU buffers or, in HdLuxCore's
/// case, to do a final build of the BVH.
///
class HdLuxCoreRenderDelegate final : public HdRenderDelegate {
public:
    /// Render delegate constructor. This method creates the RTC device and
    /// scene, and links LuxCore error handling to hydra error handling.
    HdLuxCoreRenderDelegate();
    /// Render delegate constructor. This method creates the RTC device and
    /// scene, and links LuxCore error ahndling to hydra error handling.
    /// It also populates initial render settings.
    HdLuxCoreRenderDelegate(HdRenderSettingsMap const& settingsMap);
    /// Render delegate destructor. This method destroys the RTC device and
    /// scene.
    virtual ~HdLuxCoreRenderDelegate();

    /// Return this delegate's render param.
    ///   \return A shared instance of HdLuxCoreRenderParam.
    virtual HdRenderParam *GetRenderParam() const override;

    /// Return a list of which Rprim types can be created by this class's
    /// CreateRprim.
    virtual const TfTokenVector &GetSupportedRprimTypes() const override;
    /// Return a list of which Sprim types can be created by this class's
    /// CreateSprim.
    virtual const TfTokenVector &GetSupportedSprimTypes() const override;
    /// Return a list of which Bprim types can be created by this class's
    /// CreateBprim.
    virtual const TfTokenVector &GetSupportedBprimTypes() const override;

    /// Returns the HdResourceRegistry instance used by this render delegate.
    virtual HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    /// Returns a list of user-configurable render settings.
    /// This is a reflection API for the render settings dictionary; it need
    /// not be exhaustive, but can be used for populating application settings
    /// UI.
    virtual HdRenderSettingDescriptorList
        GetRenderSettingDescriptors() const override;

    /// Create a renderpass. Hydra renderpasses are responsible for drawing
    /// a subset of the scene (specified by the "collection" parameter) to the
    /// current framebuffer. This class creates objects of type
    /// HdLuxCoreRenderPass, which draw using LuxCore's raycasting API.
    ///   \param index The render index this renderpass will be bound to.
    ///   \param collection A specifier for which parts of the scene should
    ///                     be drawn.
    ///   \return An LuxCore renderpass object.
    virtual HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex *index,
                HdRprimCollection const& collection) override;

    /// Create an instancer. Hydra instancers store data needed for an
    /// instanced object to draw itself multiple times.
    ///   \param delegate The scene delegate providing data for this
    ///                   instancer.
    ///   \param id The scene graph ID of this instancer, used when pulling
    ///             data from a scene delegate.
    ///   \param instancerId If specified, the instancer at this id uses
    ///                      this instancer as a prototype.
    ///   \return An LuxCore instancer object.
    virtual HdInstancer *CreateInstancer(HdSceneDelegate *delegate,
                                         SdfPath const& id,
                                         SdfPath const& instancerId);

    /// Destroy an instancer created with CreateInstancer.
    ///   \param instancer The instancer to be destroyed.
    virtual void DestroyInstancer(HdInstancer *instancer);

    /// Create a hydra Rprim, representing scene geometry. This class creates
    /// LuxCore-specialized geometry containers like HdLuxCoreMesh which map
    /// scene data to LuxCore scene graph objects.
    ///   \param typeId The rprim type to create. This must be one of the types
    ///                 from GetSupportedRprimTypes().
    ///   \param rprimId The scene graph ID of this rprim, used when pulling
    ///                  data from a scene delegate.
    ///   \param instancerId If specified, the instancer at this id uses the
    ///                      new rprim as a prototype.
    ///   \return An LuxCore rprim object.
    virtual HdRprim *CreateRprim(TfToken const& typeId,
                                 SdfPath const& rprimId,
                                 SdfPath const& instancerId) override;

    /// Destroy an Rprim created with CreateRprim.
    ///   \param rPrim The rprim to be destroyed.
    virtual void DestroyRprim(HdRprim *rPrim) override;

    /// Create a hydra Sprim, representing scene or viewport state like cameras
    /// or lights.
    ///   \param typeId The sprim type to create. This must be one of the types
    ///                 from GetSupportedSprimTypes().
    ///   \param sprimId The scene graph ID of this sprim, used when pulling
    ///                  data from a scene delegate.
    ///   \return An LuxCore sprim object.
    virtual HdSprim *CreateSprim(TfToken const& typeId,
                                 SdfPath const& sprimId) override;

    /// Create a hydra Sprim using default values, and with no scene graph
    /// binding.
    ///   \param typeId The sprim type to create. This must be one of the types
    ///                 from GetSupportedSprimTypes().
    ///   \return An LuxCore fallback sprim object.
    virtual HdSprim *CreateFallbackSprim(TfToken const& typeId) override;

    /// Destroy an Sprim created with CreateSprim or CreateFallbackSprim.
    ///   \param sPrim The sprim to be destroyed.
    virtual void DestroySprim(HdSprim *sPrim) override;

    /// Create a hydra Bprim, representing data buffers such as textures.
    ///   \param typeId The bprim type to create. This must be one of the types
    ///                 from GetSupportedBprimTypes().
    ///   \param bprimId The scene graph ID of this bprim, used when pulling
    ///                  data from a scene delegate.
    ///   \return An LuxCore bprim object.
    virtual HdBprim *CreateBprim(TfToken const& typeId,
                                 SdfPath const& bprimId) override;

    /// Create a hydra Bprim using default values, and with no scene graph
    /// binding.
    ///   \param typeId The bprim type to create. This must be one of the types
    ///                 from GetSupportedBprimTypes().
    ///   \return An LuxCore fallback bprim object.
    virtual HdBprim *CreateFallbackBprim(TfToken const& typeId) override;

    /// Destroy a Bprim created with CreateBprim or CreateFallbackBprim.
    ///   \param bPrim The bprim to be destroyed.
    virtual void DestroyBprim(HdBprim *bPrim) override;

    /// This function is called after new scene data is pulled during prim
    /// Sync(), but before any tasks (such as draw tasks) are run, and gives the
    /// render delegate a chance to transfer any invalidated resources to the
    /// rendering kernel.
    ///   \param tracker The change tracker passed to prim Sync().
    virtual void CommitResources(HdChangeTracker *tracker) override;

    /// This function tells the scene which material variant to reference.
    /// LuxCore doesn't currently use materials but raytraced backends generally
    /// specify "full".
    ///   \return A token specifying which material variant this renderer
    ///           prefers.
    virtual TfToken GetMaterialBindingPurpose() const override {
        return HdTokens->full;
    }

    /// This function returns the default AOV descriptor for a given named AOV.
    /// This mechanism lets the renderer decide things like what format
    /// a given AOV will be written as.
    ///   \param name The name of the AOV whose descriptor we want.
    ///   \return A descriptor specifying things like what format the AOV
    ///           output buffer should be.
    virtual HdAovDescriptor
        GetDefaultAovDescriptor(TfToken const& name) const override;

    // A map of rprims
    TfHashMap<std::string, HdLuxCoreMesh*> _rprimMap;
    // A map of sprim Lights
    TfHashMap<std::string, HdLuxCoreLight*> _sprimLightMap;
private:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;

    /// Resource registry used in this render delegate
    static std::mutex _mutexResourceRegistry;
    static std::atomic_int _counterResourceRegistry;
    static HdResourceRegistrySharedPtr _resourceRegistry;

    // This class does not support copying.
    HdLuxCoreRenderDelegate(const HdLuxCoreRenderDelegate &)             = delete;
    HdLuxCoreRenderDelegate &operator =(const HdLuxCoreRenderDelegate &) = delete;

    // LuxCore initialization routine.
    void _Initialize();
    luxrays::Properties lc_props;
    luxcore::RenderConfig *lc_config;
    luxcore::RenderSession *lc_session;
    luxcore::Scene *lc_scene;
    // A version counter for edits to _scene.
    std::atomic<int> _sceneVersion;

    // A shared HdLuxCoreRenderParam object that stores top-level LuxCore state;
    // passed to prims during Sync().
    std::shared_ptr<HdLuxCoreRenderParam> _renderParam;

    // A background render thread for running the actual renders in. The
    // render thread object manages synchronization between the scene data
    // and the background-threaded renderer.
    HdRenderThread _renderThread;

    // An LuxCore renderer object, to perform the actual raytracing.
    HdLuxCoreRenderer _renderer;

    // A list of render setting exports.
    HdRenderSettingDescriptorList _settingDescriptors;

    // A callback that interprets LuxCore error codes and injects them into
    // the hydra logging system.
    static void HandleLuxCoreError(const char *msg);
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDLUXCORE_RENDER_DELEGATE_H
