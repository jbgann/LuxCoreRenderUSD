from pxr import Usd, UsdGeom, UsdLux


stage = Usd.Stage.CreateNew("toscl.usda")

#Add triangle
triXformPrim = UsdGeom.Xform.Define(stage, '/basic')
triangle = UsdGeom.Mesh.Define(stage, '/basic/triangle')
triangle.CreatePointsAttr([(-1,-1,-1),
                           (-1,1,1),
                           (1,-1,1)])
triangle.CreateFaceVertexCountsAttr([3])
triangle.CreateFaceVertexIndicesAttr([0,1,2])
triangle.CreateExtentAttr([(-2,-2,-2),(2,2,2)])

#Add square backdrop
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

#Add non-default camera
cam = UsdGeom.Camera.Define(stage,"/camera1")

cam_tx = cam.AddTranslateOp()
cam_tx.Set(value=(0,-12,12))

cam_rotx = cam.AddRotateXOp()
cam_rotx.Set(value=45)

#Add non-default light
light = UsdLux.SphereLight.Define(stage,"/pointlight")

light.CreateTreatAsPointAttr(True)

light_tx=light.AddTranslateOp()
light_tx.Set(value=(-5,5,5))

stage.GetRootLayer().Save()
