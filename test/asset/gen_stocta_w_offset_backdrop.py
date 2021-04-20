from pxr import Usd, UsdGeom
from math import sqrt

stage = Usd.Stage.CreateNew("stocta_w_offset_backdrop.usda")

triXformPrim = UsdGeom.Xform.Define(stage, '/basic')
triangle = UsdGeom.Mesh.Define(stage, '/basic/tetra1')
triangle.CreatePointsAttr([(1,1,1),
                        (-1,-1,1),
                        (-1,1,-1),
                        (1,-1,-1)])
triangle.CreateFaceVertexCountsAttr([3,3,3,3])
triangle.CreateFaceVertexIndicesAttr([0,1,2,0,1,3,0,2,3,1,2,3])
triangle.CreateExtentAttr([(-2,-2,-2),(2,2,2)])

triangle = UsdGeom.Mesh.Define(stage, '/basic/tetra1')
triangle.CreatePointsAttr([(-1,1,1),
                        (1,-1,1),
                        (1,1,-1),
                        (-1,-1,-1)])
triangle.CreateFaceVertexCountsAttr([3,3,3,3])
triangle.CreateFaceVertexIndicesAttr([0,1,2,0,1,3,0,2,3,1,2,3])
triangle.CreateExtentAttr([(-2,-2,-2),(2,2,2)])

backdropXformPrim = UsdGeom.Xform.Define(stage, '/backdrop')
backdrop = UsdGeom.Mesh.Define(stage,'/backdrop/square')
backdrop.CreatePointsAttr([(-3,-3,0),
                           (-3,3,0),
                           (3,3,0),
                           (3,-3,0)])
backdrop.CreateFaceVertexCountsAttr([4])
backdrop.CreateFaceVertexIndicesAttr([0,1,2,3])
backdrop.CreateExtentAttr([(-5,-5,-5),(5,5,5)])

backdrop_tx = backdrop.AddTranslateOp()
backdrop_tx.Set(value=(0,0,-1))

backdrop_rot = backdrop.AddRotateZOp()
backdrop_rot.Set(value=45)

stage.GetRootLayer().Save()
