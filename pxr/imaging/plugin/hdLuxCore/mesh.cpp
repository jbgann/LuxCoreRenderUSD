//
// Copyright 2017 Pixar, John Gann, LuxCore
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

#if defined(_OPENMP)

#include <omp.h>
#include <opensubdiv/osd/ompEvaluator.h>
#define OSD_EVALUATOR Osd::OmpEvaluator

#else

#include <opensubdiv/osd/cpuEvaluator.h>
#define OSD_EVALUATOR Osd::CpuEvaluator

#endif

#include <opensubdiv/version.h>
#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/patchMap.h>
#include <opensubdiv/far/patchTable.h>
#include <opensubdiv/far/patchTableFactory.h>
#include <opensubdiv/far/stencilTableFactory.h>
#include <opensubdiv/far/topologyRefinerFactory.h>
#include <opensubdiv/osd/cpuPatchTable.h>
#include <opensubdiv/osd/cpuEvaluator.h>
#include <opensubdiv/osd/cpuVertexBuffer.h>

#include <luxcore/luxcore.h>
#include <luxrays/utils/utils.h>

#include <algorithm> // sort


PXR_NAMESPACE_OPEN_SCOPE

using namespace OpenSubdiv;
using namespace luxrays;

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

    logit("HdLuxCoreMesh::Sync");

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
        //_transforms = static_cast<HdLuxCoreInstancer*>(instancer)->ComputeInstanceTransforms(GetId()); 
    } else {
        GfMatrix4d *transform = new GfMatrix4d(sceneDelegate->GetTransform(GetId()));
        //_transforms = VtMatrix4dArray();
        _transforms.push_back(transform);
    }

	// Get the mesh complexity level for OpenSubdiv
	HdDisplayStyle const displayStyle = GetDisplayStyle(sceneDelegate);
	_refineLevel = displayStyle.refineLevel;

    VtValue value = sceneDelegate->Get(GetId(), HdTokens->points);
    _points = value.Get<VtVec3fArray>();
    _topology = HdMeshTopology(GetMeshTopology(sceneDelegate));

	VtValue normals_value = sceneDelegate->Get(GetId(), HdTokens->normals);
	_normals = normals_value.Get<VtVec3fArray>();

}

// The following block is adapted from the LuxCore rendering system
template <unsigned int DIMENSIONS> static Osd::CpuVertexBuffer *BuildBuffer(
	const Far::StencilTable *stencilTable, const float *data,
	const unsigned int count, const unsigned int totalCount) {
	Osd::CpuVertexBuffer *buffer = Osd::CpuVertexBuffer::Create(DIMENSIONS, totalCount);

	Osd::BufferDescriptor desc(0, DIMENSIONS, DIMENSIONS);
	Osd::BufferDescriptor newDesc(count * DIMENSIONS, DIMENSIONS, DIMENSIONS);

	// Pack the control vertex data at the start of the vertex buffer
	// and update every time control data changes
	buffer->UpdateData(data, 0, count);

	// Refine points (coarsePoints -> refinedPoints)
	OSD_EVALUATOR::EvalStencils(buffer, desc,
		buffer, newDesc,
		stencilTable);

	return buffer;
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

	// TODO: See if we can use the mesh type
	if (id.GetString().rfind("/sphere", 0) == 0 && _refineLevel > 0) {

		// The following OpenSubdiv code is adapted from the LuxCore rendering system

		// -- BEGIN OPEN SUBDIBV -- //

		Sdc::Options options;
		options.SetVtxBoundaryInterpolation(Sdc::Options::VTX_BOUNDARY_EDGE_AND_CORNER);

		Far::TopologyDescriptor desc;
		desc.numVertices = _points.size();
		desc.numFaces = _triangulatedIndices.size();
		vector<int> vertPerFace(desc.numFaces, 3);
		desc.numVertsPerFace = &vertPerFace[0];
		desc.vertIndicesPerFace = (const int *)_triangulatedIndices.cdata();

		// Look for mesh boundary edges
		unordered_map<Edge, unsigned int, EdgeHashFunction> edgesMap;
		const unsigned int triCount = _triangulatedIndices.size();
		const Triangle *tris = (const Triangle *)_triangulatedIndices.cdata();;

		// Count how many times an edge is shared
		for (unsigned int i = 0; i < triCount; ++i) {
			const Triangle &tri = tris[i];

			const Edge edge0(tri.v[0], tri.v[1]);
			if (edgesMap.find(edge0) != edgesMap.end())
				edgesMap[edge0] += 1;
			else
				edgesMap[edge0] = 1;

			const Edge edge1(tri.v[1], tri.v[2]);
			if (edgesMap.find(edge1) != edgesMap.end())
				edgesMap[edge1] += 1;
			else
				edgesMap[edge1] = 1;

			const Edge edge2(tri.v[2], tri.v[0]);
			if (edgesMap.find(edge2) != edgesMap.end())
				edgesMap[edge2] += 1;
			else
				edgesMap[edge2] = 1;
		}

		vector<bool> isBoundaryVertex(desc.numVertices, false);
		vector<Far::Index> cornerVertexIndices;
		vector<float> cornerWeights;
		for (auto em : edgesMap) {
			if (em.second == 1) {
				// It is a boundary edge

				const Edge &e = em.first;

				if (!isBoundaryVertex[e.vIndex[0]]) {
					cornerVertexIndices.push_back(e.vIndex[0]);
					cornerWeights.push_back(10.f);
					isBoundaryVertex[e.vIndex[0]] = true;
				}

				if (!isBoundaryVertex[e.vIndex[1]]) {
					cornerVertexIndices.push_back(e.vIndex[1]);
					cornerWeights.push_back(10.f);
					isBoundaryVertex[e.vIndex[1]] = true;
				}
			}
		}

		// Initialize TopologyDescriptor corners if I have some
		if (cornerVertexIndices.size() > 0) {
			desc.numCorners = cornerVertexIndices.size();
			desc.cornerVertexIndices = &cornerVertexIndices[0];
			desc.cornerWeights = &cornerWeights[0];
		}


		// Instantiate a Far::TopologyRefiner from the descriptor
		Sdc::SchemeType type = Sdc::SCHEME_LOOP;
		Far::TopologyRefiner *refiner = Far::TopologyRefinerFactory<Far::TopologyDescriptor>::Create(desc,
			Far::TopologyRefinerFactory<Far::TopologyDescriptor>::Options(type, options));

		// Complexity
		refiner->RefineUniform(Far::TopologyRefiner::UniformOptions(_refineLevel + 1));

		Far::StencilTableFactory::Options stencilOptions;
		stencilOptions.generateOffsets = true;
		stencilOptions.generateIntermediateLevels = false;

		const Far::StencilTable *stencilTable = Far::StencilTableFactory::Create(*refiner, stencilOptions);

		Far::PatchTableFactory::Options patchOptions;
		patchOptions.SetEndCapType(
			Far::PatchTableFactory::Options::ENDCAP_BSPLINE_BASIS);

		const Far::PatchTable *patchTable =
			Far::PatchTableFactory::Create(*refiner, patchOptions);

		// Append local point stencils
		if (const Far::StencilTable *localPointStencilTable =
			patchTable->GetLocalPointStencilTable()) {
			if (const Far::StencilTable *combinedTable =
				Far::StencilTableFactory::AppendLocalPointStencilTable(
					*refiner, stencilTable, localPointStencilTable)) {
				delete stencilTable;
				stencilTable = combinedTable;
			}
		}

		// Setup a buffer for vertex primvar data
		const unsigned int vertsCount = refiner->GetLevel(0).GetNumVertices();
		const unsigned int totalVertsCount = vertsCount + refiner->GetNumVerticesTotal();

		// Vertices
		Osd::CpuVertexBuffer *vertsBuffer = BuildBuffer<3>(
			stencilTable, (const float *)_points.cdata(),
			vertsCount, totalVertsCount);

		// Normals
		Osd::CpuVertexBuffer *normsBuffer = nullptr;
		if (_normals.size() > 0) {
			normsBuffer = BuildBuffer<3>(
				stencilTable, (const float *)_normals.cdata(),
				vertsCount, totalVertsCount);
		}

		// New triangles
		unsigned int newTrisCount = 0;
		for (int array = 0; array < patchTable->GetNumPatchArrays(); ++array)
			for (int patch = 0; patch < patchTable->GetNumPatches(array); ++patch)
				++newTrisCount;

		VtVec3iArray newTris = VtVec3iArray(newTrisCount);

		unsigned int triIndex = 0;
		unsigned int maxVertIndex = 0;
		for (int array = 0; array < patchTable->GetNumPatchArrays(); ++array) {
			for (int patch = 0; patch < patchTable->GetNumPatches(array); ++patch) {
				const Far::ConstIndexArray faceVerts =
					patchTable->GetPatchVertices(array, patch);

				assert(faceVerts.size() == 3);
				newTris[triIndex][0] = faceVerts[0] - vertsCount;
				newTris[triIndex][1] = faceVerts[1] - vertsCount;
				newTris[triIndex][2] = faceVerts[2] - vertsCount;

				maxVertIndex = Max((int)maxVertIndex, Max(newTris[triIndex][0], Max(newTris[triIndex][1], newTris[triIndex][2])));

				++triIndex;
			}
		}

		// I don't sincerely know how to get this obvious value out of OpenSubdiv
		const u_int newVertsCount = maxVertIndex + 1;

		// New vertices
		VtVec3fArray newVerts = VtVec3fArray(newVertsCount);
		const float *refinedVerts = vertsBuffer->BindCpuBuffer() + 3 * vertsCount;

		for (unsigned int i = 0; i < newVertsCount; i++) {
			newVerts[i][0] = refinedVerts[i * 3 + 0];
			newVerts[i][1] = refinedVerts[i * 3 + 1];
			newVerts[i][2] = refinedVerts[i * 3 + 2];
		}

		_triangulatedIndices = newTris;
		_points = newVerts;

		// -- END OPEN SUBDIBV -- //
	}

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

    //cout << "Points: " << _points << std::flush;
    //cout << "Triangulated Indicies: " << _triangulatedIndices << std::flush;

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

bool
HdLuxCoreMesh::IsValidTransform(GfMatrix4f m)
{
    float *items = m.GetArray();

    for (int i = 0; i < 16; i++) {
        if (isinf(items[i]))
            return false;
        if (items[i] == -0)
            return false;
    }

    return true;
}


PXR_NAMESPACE_CLOSE_SCOPE
