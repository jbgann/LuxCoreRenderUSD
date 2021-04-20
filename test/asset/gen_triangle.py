from pxr import Usd, UsdGeom, UsdLux


stage = Usd.Stage.CreateNew("t.usda")

#Add triangle
triXformPrim = UsdGeom.Xform.Define(stage, '/basic')
triangle = UsdGeom.Mesh.Define(stage, '/basic/triangle')
triangle.CreatePointsAttr([(-1,-1,-1),
                           (-1,1,1),
                           (1,-1,1)])
triangle.CreateFaceVertexCountsAttr([3])
triangle.CreateFaceVertexIndicesAttr([0,1,2])
triangle.CreateExtentAttr([(-2,-2,-2),(2,2,2)])

stage.GetRootLayer().Save()
