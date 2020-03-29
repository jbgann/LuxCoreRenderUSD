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
    _dirtyBits = dirtyBits;
    _desc = desc;
    _sceneDelegate = sceneDelegate;

    HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
    HdInstancer *instancer = renderIndex.GetInstancer(GetInstancerId());

    if (!GetInstancerId().IsEmpty()) {
            _transforms =
            static_cast<HdLuxCoreInstancer*>(instancer)->
                ComputeInstanceTransforms(GetId());
    } else {
        _transforms = VtMatrix4dArray();
    }

    VtValue value = sceneDelegate->Get(GetId(), HdTokens->points);
    _points = value.Get<VtVec3fArray>();
    _topology = HdMeshTopology(GetMeshTopology(sceneDelegate));
}

bool
HdLuxCoreMesh::CreateLuxCoreTriangleMesh(HdRenderParam* renderParam)
{
    cout << "_CreateLuxCoreTriangleMesh " << std::flush;
    Scene *lc_scene = reinterpret_cast<HdLuxCoreRenderParam*>(renderParam)->_scene;
    RenderSession *lc_session = reinterpret_cast<HdLuxCoreRenderParam*>(renderParam)->_session;

    // Used to name the type of mesh in LuxCore
    SdfPath const& id = GetId();

    if (lc_scene->IsMeshDefined(id.GetString())) {
        return false;
    }

    // Triangulate the input faces.
    HdMeshUtil meshUtil(&_topology, GetId());
    meshUtil.ComputeTriangleIndices(&_triangulatedIndices,
        &_trianglePrimitiveParams);

    // Alloc a LuxCore triangle buffer and copy the USD mesh's triangle indicies into it
    unsigned int *triangle_indicies = (unsigned int *)Scene::AllocTrianglesBuffer(_triangulatedIndices.size());
    triangle_indicies = (unsigned int *)_triangulatedIndices.cdata();

    // todo: rescale camera so we don't have to x 100 these
    // also cast these as above
    float *verticies = (float *)Scene::AllocVerticesBuffer(_points.size());
    for (int i = 0; i < _points.size(); i++) {
        verticies[i*3+0] = _points[i][0];
        verticies[i*3+1] = _points[i][1];
        verticies[i*3+2] = _points[i][2];
    }

    cout << "Points: " << _points << std::flush;
    cout << "Triangulated Indicies: " << _triangulatedIndices << std::flush;

    lc_scene->DefineMesh(id.GetString(), _points.size(), _triangulatedIndices.size(), verticies, triangle_indicies,  NULL, NULL, NULL, NULL);

    return true;
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


PXR_NAMESPACE_CLOSE_SCOPE
