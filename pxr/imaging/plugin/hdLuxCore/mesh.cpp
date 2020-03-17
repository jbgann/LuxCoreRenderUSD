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
#include "pxr/imaging/hdLuxCore/mesh.h"
#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hdLuxCore/renderDelegate.h"
#include "pxr/imaging/hdLuxCore/renderPass.h"
#include "pxr/imaging/hdLuxCore/renderParam.h"
#include "pxr/imaging/hdLuxCore/instancer.h"

#include "pxr/imaging/hd/extComputationUtils.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/smoothNormals.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/pxr.h"

#include "pxr/imaging/hd/aov.h"
#include "pxr/imaging/hd/renderPass.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/imaging/hdLuxCore/renderer.h"
#include "pxr/imaging/hdLuxCore/renderBuffer.h"
#include "pxr/imaging/hdx/compositor.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/usd/sdf/identity.h"


#include <luxcore/luxcore.h>

#include <algorithm> // sort

PXR_NAMESPACE_OPEN_SCOPE

HdLuxCoreMesh::HdLuxCoreMesh(SdfPath const& id,
                           SdfPath const& instancerId)
    : HdMesh(id, instancerId)
    , _adjacencyValid(false)
    , _normalsValid(false)
    , _refined(false)
    , _smoothNormals(false)
    , _doubleSided(false)
    , _cullStyle(HdCullStyleDontCare)
{
}

void
HdLuxCoreMesh::Finalize(HdRenderParam *renderParam)
{
    Scene *lc_scene = reinterpret_cast<HdLuxCoreRenderParam*>(renderParam)->_scene;
    RenderSession *lc_session = reinterpret_cast<HdLuxCoreRenderParam*>(renderParam)->_session;
    SdfPath const& id = GetId();

    lc_session->BeginSceneEdit();
    if (lc_scene->IsMeshDefined(id.GetString())) {
        cout << "Object is being deleted";
        lc_scene->DeleteObject(id.GetString());
    }
    lc_session->EndSceneEdit();
}

HdDirtyBits
HdLuxCoreMesh::GetInitialDirtyBitsMask() const
{
    // The initial dirty bits control what data is available on the first
    // run through _PopulateRtMesh(), so it should list every data item
    // that _PopulateRtMesh requests.
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::InitRepr
        | HdChangeTracker::DirtyPoints
        | HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyCullStyle
        | HdChangeTracker::DirtyDoubleSided
        | HdChangeTracker::DirtyDisplayStyle
        | HdChangeTracker::DirtySubdivTags
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyNormals
        | HdChangeTracker::DirtyInstanceIndex
        ;

    return (HdDirtyBits)mask;
}

HdDirtyBits
HdLuxCoreMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
HdLuxCoreMesh::_InitRepr(TfToken const &reprToken,
                        HdDirtyBits *dirtyBits)
{
    TF_UNUSED(dirtyBits);

    // Create an empty repr.
    _ReprVector::iterator it = std::find_if(_reprs.begin(), _reprs.end(),
                                            _ReprComparator(reprToken));
    if (it == _reprs.end()) {
        _reprs.emplace_back(reprToken, HdReprSharedPtr());
    }
}

void
HdLuxCoreMesh::Sync(HdSceneDelegate *sceneDelegate,
                   HdRenderParam   *renderParam,
                   HdDirtyBits     *dirtyBits,
                   TfToken const   &reprToken)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // XXX: A mesh repr can have multiple repr decs; this is done, for example,
    // when the drawstyle specifies different rasterizing modes between front
    // faces and back faces.
    // With raytracing, this concept makes less sense, but
    // combining semantics of two HdMeshReprDesc is tricky in the general case.
    // For now, HdLuxCoreMesh only respects the first desc; this should be fixed.
    _MeshReprConfig::DescArray descs = _GetReprDesc(reprToken);
    const HdMeshReprDesc &desc = descs[0];

    _PopulateLuxCoreMesh(renderParam, sceneDelegate, dirtyBits, desc);
}


void
HdLuxCoreMesh::_CreateLuxCoreTriangleMesh(HdRenderParam* renderParam)
{
    Scene *lc_scene = reinterpret_cast<HdLuxCoreRenderParam*>(renderParam)->_scene;
    RenderSession *lc_session = reinterpret_cast<HdLuxCoreRenderParam*>(renderParam)->_session;

    // Used to name the type of mesh in LuxCore
    SdfPath const& id = GetId();

    // Triangulate the input faces.
    HdMeshUtil meshUtil(&_topology, GetId());
    meshUtil.ComputeTriangleIndices(&_triangulatedIndices,
        &_trianglePrimitiveParams);

    // Alloc a LuxCore triangle buffer and copy the USD mesh's triangle indicies into it
    unsigned int *triangle_indicies = (unsigned int *)Scene::AllocTrianglesBuffer(_triangulatedIndices.size());
    triangle_indicies = (unsigned int *)_triangulatedIndices.cdata();

    // also cast these as above
    float *verticies = (float *)Scene::AllocVerticesBuffer(_points.size());
    for (int i = 0; i < _points.size(); i++) {
        verticies[i*3+0] = _points[i][0];
        verticies[i*3+1] = _points[i][1];
        verticies[i*3+2] = _points[i][2];
    }

    cout << "Mesh points: " << _points << "\n" << std::flush;

    // Create the Mesh prototype in LuxCore
    lc_session->Pause();
    lc_session->BeginSceneEdit();
    lc_scene->DefineMesh(id.GetString(), _points.size(), _triangulatedIndices.size(), verticies, triangle_indicies,  NULL, NULL, NULL, NULL);
    lc_session->EndSceneEdit();
    lc_session->Resume();
}

void
HdLuxCoreMesh::_UpdatePrimvarSources(HdSceneDelegate* sceneDelegate,
                                    HdDirtyBits dirtyBits)
{
    HD_TRACE_FUNCTION();
    SdfPath const& id = GetId();

    // Update _primvarSourceMap, our local cache of raw primvar data.
    // This function pulls data from the scene delegate, but defers processing.
    //
    // While iterating primvars, we skip "points" (vertex positions) because
    // the points primvar is processed by _PopulateRtMesh. We only call
    // GetPrimvar on primvars that have been marked dirty.
    //
    // Currently, hydra doesn't have a good way of communicating changes in
    // the set of primvars, so we only ever add and update to the primvar set.

    HdPrimvarDescriptorVector primvars;
    for (size_t i=0; i < HdInterpolationCount; ++i) {
        HdInterpolation interp = static_cast<HdInterpolation>(i);
        primvars = GetPrimvarDescriptors(sceneDelegate, interp);
        for (HdPrimvarDescriptor const& pv: primvars) {
            if (HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name) &&
                pv.name != HdTokens->points) {
                _primvarSourceMap[pv.name] = {
                    GetPrimvar(sceneDelegate, pv.name),
                    interp
                };
            }
        }
    }
}

TfTokenVector
HdLuxCoreMesh::_UpdateComputedPrimvarSources(HdSceneDelegate* sceneDelegate,
                                            HdDirtyBits dirtyBits)
{
    HD_TRACE_FUNCTION();

    SdfPath const& id = GetId();

    // Get all the dirty computed primvars
    HdExtComputationPrimvarDescriptorVector dirtyCompPrimvars;
    for (size_t i=0; i < HdInterpolationCount; ++i) {
        HdExtComputationPrimvarDescriptorVector compPrimvars;
        HdInterpolation interp = static_cast<HdInterpolation>(i);
        compPrimvars = sceneDelegate->GetExtComputationPrimvarDescriptors
                                    (GetId(),interp);

        for (auto const& pv: compPrimvars) {
            if (HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name)) {
                dirtyCompPrimvars.emplace_back(pv);
            }
        }
    }

    if (dirtyCompPrimvars.empty()) {
        return TfTokenVector();
    }

    HdExtComputationUtils::ValueStore valueStore
        = HdExtComputationUtils::GetComputedPrimvarValues(
            dirtyCompPrimvars, sceneDelegate);

    TfTokenVector compPrimvarNames;
    // Update local primvar map and track the ones that were computed
    for (auto const& compPrimvar : dirtyCompPrimvars) {
        auto const it = valueStore.find(compPrimvar.name);
        if (!TF_VERIFY(it != valueStore.end())) {
            continue;
        }

        compPrimvarNames.emplace_back(compPrimvar.name);
        if (compPrimvar.name == HdTokens->points) {
            _points = it->second.Get<VtVec3fArray>();
                        _normalsValid = false;
        } else {
            _primvarSourceMap[compPrimvar.name] = {it->second,
                                                compPrimvar.interpolation};
        }
    }

    return compPrimvarNames;
}

void
HdLuxCoreMesh::_PopulateLuxCoreMesh(HdRenderParam* renderParam,
                              HdSceneDelegate* sceneDelegate,
                              HdDirtyBits*     dirtyBits,
                              HdMeshReprDesc const &desc)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();
    Scene *lc_scene = reinterpret_cast<HdLuxCoreRenderParam*>(renderParam)->_scene;
    RenderSession *lc_session = reinterpret_cast<HdLuxCoreRenderParam*>(renderParam)->_session;

    ////////////////////////////////////////////////////////////////////////
    // 1. Pull scene data.
    TfTokenVector computedPrimvars =
        _UpdateComputedPrimvarSources(sceneDelegate, *dirtyBits);

    bool pointsIsComputed =
        std::find(computedPrimvars.begin(), computedPrimvars.end(),
                  HdTokens->points) != computedPrimvars.end();
    if (!pointsIsComputed &&
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        VtValue value = sceneDelegate->Get(id, HdTokens->points);
        _points = value.Get<VtVec3fArray>();
        _normalsValid = false;
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        // When pulling a new topology, we don't want to overwrite the
        // refine level or subdiv tags, which are provided separately by the
        // scene delegate, so we save and restore them.
        PxOsdSubdivTags subdivTags = _topology.GetSubdivTags();
        int refineLevel = _topology.GetRefineLevel();
        _topology = HdMeshTopology(GetMeshTopology(sceneDelegate), refineLevel);
        _topology.SetSubdivTags(subdivTags);
        _adjacencyValid = false;
    }
    if (HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id) &&
        _topology.GetRefineLevel() > 0) {
        _topology.SetSubdivTags(sceneDelegate->GetSubdivTags(id));
    }
    if (HdChangeTracker::IsDisplayStyleDirty(*dirtyBits, id)) {
        HdDisplayStyle const displayStyle = sceneDelegate->GetDisplayStyle(id);
        _topology = HdMeshTopology(_topology,
            displayStyle.refineLevel);
    }

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        cout << "HdChangeTracker::IsTransformDirty: True" << std::flush;
        _transform = GfMatrix4f(0.1,0.f,0.f, 0.f, 0.f,0.1,0.f, 0.f, 0.f,0.f,0.1f, 0.f, 0.f, 0.f, 0.f, 1.f);
        cout << "_transform: " << _transform << "\n" << std::flush;
        //_transform = GfMatrix4f(sceneDelegate->GetTransform(id));
        cout << "_transform: " << _transform << "\n" << std::flush;
        cout << "_transform data: " << _transform.data() << "\n" << std::flush;
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        _UpdateVisibility(sceneDelegate, dirtyBits);
    }

    if (HdChangeTracker::IsCullStyleDirty(*dirtyBits, id)) {
        _cullStyle = GetCullStyle(sceneDelegate);
    }
    if (HdChangeTracker::IsDoubleSidedDirty(*dirtyBits, id)) {
        _doubleSided = IsDoubleSided(sceneDelegate);
    }
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals) ||
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths) ||
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->primvar)) {
        _UpdatePrimvarSources(sceneDelegate, *dirtyBits);
    }

    ////////////////////////////////////////////////////////////////////////
    // 2. Resolve drawstyles

    // The repr defines a set of geometry styles for drawing the mesh
    // (see hd/enums.h). We're ignoring points and wireframe for now, so
    // HdMeshGeomStyleSurf maps to subdivs and everything else maps to
    // HdMeshGeomStyleHull (coarse triangulated mesh).
    bool doRefine = (desc.geomStyle == HdMeshGeomStyleSurf);

    // If the subdivision scheme is "none", force us to not refine.
    doRefine = doRefine && (_topology.GetScheme() != PxOsdOpenSubdivTokens->none);

    // If the refine level is 0, triangulate instead of subdividing.
    doRefine = doRefine && (_topology.GetRefineLevel() > 0);

    // The repr defines whether we should compute smooth normals for this mesh:
    // per-vertex normals taken as an average of adjacent faces, and
    // interpolated smoothly across faces.
    _smoothNormals = !desc.flatShadingEnabled;

    // If the subdivision scheme is "none" or "bilinear", force us not to use
    // smooth normals.
    _smoothNormals = _smoothNormals &&
        (_topology.GetScheme() != PxOsdOpenSubdivTokens->none) &&
        (_topology.GetScheme() != PxOsdOpenSubdivTokens->bilinear);

    // If the scene delegate has provided authored normals, force us to not use
    // smooth normals.
    bool authoredNormals = false;
    if (_primvarSourceMap.count(HdTokens->normals) > 0) {
        authoredNormals = true;
    }
    _smoothNormals = _smoothNormals && !authoredNormals;

    ////////////////////////////////////////////////////////////////////////
    // 3. Populate LuxCore prototype object.

    // If the topology has changed, or the value of doRefine has changed, we
    // need to create or recreate the embree mesh object.
    // _GetInitialDirtyBits() ensures that the topology is dirty the first time
    // this function is called, so that the embree mesh is always created.
    bool newMesh = false;
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id) ||
        doRefine != _refined) {

        newMesh = true;

        // Destroy the old mesh, if it exists.
        //if (_rtcMeshScene != nullptr &&
        //    _rtcMeshId != RTC_INVALID_GEOMETRY_ID) {
                // Kate: We should delete LuxCore prototype context here
            // Delete the prototype context first...
            /*
            TF_FOR_ALL(it, _GetPrototypeContext()->primvarMap) {
                delete it->second;
            }
            delete _GetPrototypeContext();
            // then the prototype geometry.
            rtcDeleteGeometry(_rtcMeshScene, _rtcMeshId);
            _rtcMeshId = RTC_INVALID_GEOMETRY_ID;
            */
        //}

        // Populate either a subdiv or a triangle mesh object. The helper
        // functions will take care of populating topology buffers.
        if (doRefine) {
            // Probably need LuxCore 2.3 to create a subdiv...
            //_CreateEmbreeSubdivMesh(_rtcMeshScene);
        } else {
            _CreateLuxCoreTriangleMesh(renderParam);
        }
        _refined = doRefine;

        _normalsValid = false;
    }

    // Update the smooth normals in steps:
    // 1. If the topology is dirty, update the adjacency table, a processed
    //    form of the topology that helps calculate smooth normals quickly.
    // 2. If the points are dirty, update the smooth normal buffer itself.
    if (_smoothNormals && !_adjacencyValid) {
        _adjacency.BuildAdjacencyTable(&_topology);
        _adjacencyValid = true;
        // If we rebuilt the adjacency table, force a rebuild of normals.
        _normalsValid = false;
    }
    if (_smoothNormals && !_normalsValid) {
        _computedNormals = Hd_SmoothNormals::ComputeSmoothNormals(
            &_adjacency, _points.size(), _points.cdata());
        _normalsValid = true;

        // Create a sampler for the "normals" primvar. If there are authored
        // normals, the smooth normals flag has been suppressed, so it won't
        // be overwritten by the primvar population below.
        //_CreatePrimvarSampler(HdTokens->normals, VtValue(_computedNormals),
        //    HdInterpolationVertex, _refined);
    }

    // Populate primvars if they've changed or we recreated the mesh.
    TF_FOR_ALL(it, _primvarSourceMap) {
        if (newMesh ||
            HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, it->first)) {
            //_CreatePrimvarSampler(it->first, it->second.data,
            //        it->second.interpolation, _refined);
        }
    }

    ////////////////////////////////////////////////////////////////////////
    // 4. Populate LuxCore instance objects.

    // If the mesh is instanced, create one new instance per transform.
    // Note: The current instancer invalidation tracking makes it hard
    // to tell whether transforms will be dirty, so this code
    // pulls them every frame.

        cout << "GetInstancerId().GetString(): " << GetInstancerId().GetString();

        // Retrieve instance transforms from the instancer.
        HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
        HdInstancer *instancer =
            renderIndex.GetInstancer(GetInstancerId());
        VtMatrix4dArray transforms = VtMatrix4dArray();

        lc_session->Pause();
        lc_session->BeginSceneEdit();

        // If we have instances get their transforms
        if (!GetInstancerId().IsEmpty()) {
            transforms =
            static_cast<HdLuxCoreInstancer*>(instancer)->
                ComputeInstanceTransforms(GetId());
            cout << "first branch" << std::flush;
            // Clear old instances from LuxCore
            for (size_t i = 0; i < _total_instances; i++)
            {
                std::string instanceName = id.GetString() + std::to_string(i);
                lc_scene->DeleteObject(instanceName);
            }

            for (size_t i = 0; i < transforms.size(); i++)
            {
                std::string instanceName = id.GetString() + std::to_string(i);
                lc_scene->Parse(
                    luxrays::Property("scene.objects." + instanceName + ".shape")(id.GetString()) <<
                    luxrays::Property("scene.objects." + instanceName + ".material")("mat_red")
                );

                // TODO: Manipulate the tranforms
                //GfMatrix4f matf = _transform * GfMatrix4f(transforms[i]);
                //_GetInstanceContext(scene, i)->objectToWorldMatrix = matf;
            }
        } else {
            std::string instanceName = id.GetString();
            float trans[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            lc_scene->SetMeshAppliedTransformation(instanceName, trans);
            lc_scene->UpdateObjectTransformation(instanceName, trans);
            // TODO: Lets not make the first instance special
            
            lc_scene->Parse(
                luxrays::Property("scene.objects." + instanceName + ".shape")(id.GetString()) <<
                luxrays::Property("scene.objects." + instanceName + ".material")("mat_red")
            );
            //lc_scene->SetMeshAppliedTransformation(instanceName, _transform.data());
            
        }

        lc_session->EndSceneEdit();
        lc_session->Resume();

        _total_instances = transforms.size();

    // Clean all dirty bits.
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

PXR_NAMESPACE_CLOSE_SCOPE
