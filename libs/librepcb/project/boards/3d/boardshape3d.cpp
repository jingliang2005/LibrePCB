/*
 * LibrePCB - Professional EDA for everyone!
 * Copyright (C) 2013 LibrePCB Developers, see AUTHORS.md for contributors.
 * https://librepcb.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*******************************************************************************
 *  Includes
 ******************************************************************************/
#include "boardshape3d.h"

#include <AIS_Shape.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Lin2d.hxx>
#include <gp_Pln.hxx>
#include <librepcb/common/geometry/hole.h>
#include <librepcb/common/graphics/graphicslayer.h>
#include <librepcb/common/occ/opencascadeview.h>
#include <librepcb/library/pkg/footprintpad.h>
#include <librepcb/library/pkg/package.h>
#include <librepcb/project/boards/board.h>
#include <librepcb/project/boards/drc/boardclipperpathgenerator.h>
#include <librepcb/project/boards/items/bi_device.h>
#include <librepcb/project/boards/items/bi_footprint.h>
#include <librepcb/project/boards/items/bi_footprintpad.h>
#include <librepcb/project/boards/items/bi_hole.h>
#include <librepcb/project/boards/items/bi_netline.h>
#include <librepcb/project/boards/items/bi_netsegment.h>
#include <librepcb/project/circuit/circuit.h>
#include <librepcb/project/project.h>

#include <BRepBuilderAPI.hxx>
#include <BRepLib.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {
namespace project {

TopoDS_Wire clipperPathToPolygon(const ClipperLib::Path& path,
                                 const Length& z) {
  BRepBuilderAPI_MakePolygon aPolygon;
  for (const ClipperLib::IntPoint& point : path) {
    aPolygon.Add(gp_Pnt(point.X / 1e6, point.Y / 1e6, z.toMm()));
  }
  aPolygon.Close();
  return aPolygon.Wire();
}

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

BoardShape3D::BoardShape3D(Board& board) : mBoard(board) {
}

BoardShape3D::~BoardShape3D() noexcept {
}

/*******************************************************************************
 *  Public Methods
 ******************************************************************************/

void BoardShape3D::addToView(OpenCascadeView& view) noexcept {
  TopoDS_Shape boardShape;
  TopoDS_Shape viaShape;
  TopoDS_Shape topCopperShape;
  TopoDS_Shape bottomCopperShape;

  // board outline
  {
    BoardClipperPathGenerator gen(mBoard, PositiveLength(5000));
    gen.addBoardOutline();
    for (const ClipperLib::Path& path : gen.getPaths()) {
      TopoDS_Face face = BRepBuilderAPI_MakeFace(
          clipperPathToPolygon(path, Length::fromMm(-0.8)));
      boardShape = BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, 1.6));
    }
  }

  // holes
  foreach (const BI_Hole* hole, mBoard.getHoles()) {
    BRepPrimAPI_MakeCylinder cylinderMaker(
        gp_Ax2(gp_Pnt(hole->getPosition().getX().toMm(),
                      hole->getPosition().getY().toMm(), -0.8),
               gp_Dir(0, 0, 1)),
        hole->getHole().getDiameter()->toMm() / 2, 1.6);
    TopoDS_Shape cylinder = cylinderMaker.Shape();
    BRepAlgoAPI_Cut cutMaker(boardShape, cylinder);
    boardShape = cutMaker.Shape();
  }

  TopoDS_Shape resistor, lqfp32;
  {
    STEPControl_Reader reader;
    reader.ReadFile("/home/urban/tmp/R_0603_1608Metric.step");
    reader.TransferRoots();
    resistor = reader.OneShape();
  }

  {
    STEPControl_Reader reader;
    reader.ReadFile("/home/urban/tmp/LQFP-32_7x7mm_P0.8mm.step");
    reader.TransferRoots();
    lqfp32 = reader.OneShape();
  }

  // pads
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    tl::optional<TopoDS_Shape> model;
    QString pkg = *device->getLibPackage().getNames().getDefaultValue();
    if (pkg.contains("(0603)")) {
      model = resistor;
    } else if (pkg.contains("LQFP")) {
      model = lqfp32;
    }

    if (model) {
      gp_Trsf t1;
      t1.SetRotation(gp_Ax1(), device->getRotation().toRad());
      BRepBuilderAPI_Transform tt1(*model, t1);

      gp_Trsf t2;
      t2.SetTranslation(gp_Vec(device->getPosition().getX().toMm(),
                               device->getPosition().getY().toMm(), 0.8));
      BRepBuilderAPI_Transform tt2(tt1.Shape(), t2);

      Handle(AIS_Shape) aaa = new AIS_Shape(tt2.Shape());
      aaa->SetColor(Quantity_NOC_GRAY);
      view.getContext()->Display(aaa, Standard_True);
    }

    foreach (const BI_FootprintPad* pad, device->getFootprint().getPads()) {
      if (pad->getLibPad().getBoardSide() ==
          library::FootprintPad::BoardSide::THT) {
        // hole
        {
          BRepPrimAPI_MakeCylinder cylinderMaker(
              gp_Ax2(gp_Pnt(pad->getPosition().getX().toMm(),
                            pad->getPosition().getY().toMm(), -0.8),
                     gp_Dir(0, 0, 1)),
              (pad->getLibPad().getDrillDiameter()->toMm() / 2) + 0.035, 1.6);
          TopoDS_Shape cylinder = cylinderMaker.Shape();
          BRepAlgoAPI_Cut cutMaker(boardShape, cylinder);
          boardShape = cutMaker.Shape();
        }
        // plating
        {
          // BRepPrimAPI_MakeCylinder cylinderMaker1(
          //    gp_Ax2(gp_Pnt(pad->getPosition().getX().toMm(),
          //                  pad->getPosition().getY().toMm(), -0.8),
          //           gp_Dir(0, 0, 1)),
          //    (pad->getLibPad().getDrillDiameter()->toMm() / 2) + 0.035, 1.6);
          // viaShape = BRepAlgoAPI_Fuse(viaShape, cylinderMaker1.Shape());

          // BRepPrimAPI_MakeCylinder cylinderMaker2(
          //    gp_Ax2(gp_Pnt(pad->getPosition().getX().toMm(),
          //                  pad->getPosition().getY().toMm(), -0.8),
          //           gp_Dir(0, 0, -1)),
          //    pad->getLibPad().getWidth()->toMm() / 2, 0.035);
          // cylinder = BRepAlgoAPI_Fuse(cylinder, cylinderMaker2.Shape());
          //
          // BRepPrimAPI_MakeCylinder cylinderMaker3(
          //    gp_Ax2(gp_Pnt(pad->getPosition().getX().toMm(),
          //                  pad->getPosition().getY().toMm(), 0.8),
          //           gp_Dir(0, 0, 1)),
          //    pad->getLibPad().getWidth()->toMm() / 2, 0.035);
          // cylinder = BRepAlgoAPI_Fuse(cylinder, cylinderMaker3.Shape());
          //
          // BRepPrimAPI_MakeCylinder cylinderMaker(
          //    gp_Ax2(gp_Pnt(pad->getPosition().getX().toMm(),
          //                  pad->getPosition().getY().toMm(), -0.9),
          //           gp_Dir(0, 0, 1)),
          //    (pad->getLibPad().getDrillDiameter()->toMm() / 2), 1.8);
          // BRepAlgoAPI_Cut cutMaker(cylinder, cylinderMaker.Shape());
          //
          // Handle(AIS_Shape) anAisSolid = new AIS_Shape(cutMaker.Shape());
          // anAisSolid->SetColor(Quantity_NOC_GOLD);
          // view.getContext()->Display(anAisSolid, Standard_True);
        }
      } else {
        // qreal dir = (pad->getLibPad().getBoardSide() ==
        //             library::FootprintPad::BoardSide::TOP)
        //                ? 1
        //                : -1;
        // if (device->getIsMirrored()) dir *= -1;
        // BRepPrimAPI_MakeCylinder cylinderMaker(
        //    gp_Ax2(gp_Pnt(pad->getPosition().getX().toMm(),
        //                  pad->getPosition().getY().toMm(), 0.8 * dir),
        //           gp_Dir(0, 0, dir)),
        //    pad->getLibPad().getWidth()->toMm() / 2, 0.035);
        // Handle(AIS_Shape) anAisSolid = new AIS_Shape(cylinderMaker.Shape());
        // anAisSolid->SetColor(Quantity_NOC_GOLD);
        // view.getContext()->Display(anAisSolid, Standard_True);
      }
    }
  }

  Handle(AIS_Shape) boardShapeSolid = new AIS_Shape(boardShape);
  boardShapeSolid->SetColor(Quantity_NOC_DARKGREEN);
  view.getContext()->Display(boardShapeSolid, Standard_True);

  Handle(AIS_Shape) viaShapeSolid = new AIS_Shape(viaShape);
  viaShapeSolid->SetColor(Quantity_NOC_GOLD);
  view.getContext()->Display(viaShapeSolid, Standard_True);

  BRep_Builder aB;
  TopoDS_Compound aResComp;
  aB.MakeCompound(aResComp);

  // The arguments order is: where to add, what to add.
  aB.Add(aResComp, boardShape);

  // traces
  // foreach (const BI_NetSegment* netsegment, mBoard.getNetSegments()) {
  //  foreach (const BI_NetLine* netline, netsegment->getNetLines()) {
  //    qreal dir  = netline->getLayer().isTopLayer() ? 1 : -1;
  //    Point p1   = netline->getStartPoint().getPosition();
  //    Point p2   = netline->getEndPoint().getPosition();
  //    Path  path = Path::obround(p1, p2, netline->getWidth());
  //    BRepBuilderAPI_MakePolygon aPolygon;
  //    foreach (const Vertex& vertex, path.getVertices()) {
  //      aPolygon.Add(gp_Pnt(vertex.getPos().getX().toMm(),
  //                          vertex.getPos().getY().toMm(), 0.8 * dir));
  //    }
  //    aPolygon.Close();
  //    TopoDS_Wire  wire = aPolygon.Wire();
  //    TopoDS_Face  face = BRepBuilderAPI_MakeFace(wire);
  //    TopoDS_Shape shape =
  //        BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, 0.035 * dir));
  //    //Handle(AIS_Shape) anAisSolid = new AIS_Shape(shape);
  //    //anAisSolid->SetColor(Quantity_NOC_GOLD);
  //    //view.getContext()->Display(anAisSolid, Standard_True);
  //  }
  //}

  QList<NetSignal*> netsignals =
      mBoard.getProject().getCircuit().getNetSignals().values();
  netsignals.append(nullptr);
  foreach (const NetSignal* netsignal, netsignals) {
    BoardClipperPathGenerator gen(mBoard, PositiveLength(5000));
    gen.addCopper(GraphicsLayer::sTopCopper, netsignal);
    for (const ClipperLib::Path& path : gen.getPaths()) {
      TopoDS_Face face = BRepBuilderAPI_MakeFace(
          clipperPathToPolygon(path, Length::fromMm(0.8)));
      TopoDS_Shape shape = BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, 0.035));
      Handle(AIS_Shape) anAisSolid = new AIS_Shape(shape);
      anAisSolid->SetColor(Quantity_NOC_GOLD);
      view.getContext()->Display(anAisSolid, Standard_True);

      aB.Add(aResComp, shape);
    }
  }

  // traces bottom
  foreach (const NetSignal* netsignal, netsignals) {
    BoardClipperPathGenerator gen(mBoard, PositiveLength(5000));
    gen.addCopper(GraphicsLayer::sBotCopper, netsignal);
    for (const ClipperLib::Path& path : gen.getPaths()) {
      TopoDS_Face face = BRepBuilderAPI_MakeFace(
          clipperPathToPolygon(path, Length::fromMm(-0.8)));
      TopoDS_Shape shape = BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, -0.035));
      Handle(AIS_Shape) anAisSolid = new AIS_Shape(shape);
      anAisSolid->SetColor(Quantity_NOC_GOLD);
      view.getContext()->Display(anAisSolid, Standard_True);

      aB.Add(aResComp, shape);
    }
  }

  // auto app = XCAFApp_Application::GetApplication();
  // Handle(TDocStd_Document) doc;
  // app->NewDocument("MDTV-XCAF", doc);
  // auto assy       = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  // auto assy_label = assy->NewShape();
  // BRepBuilderAPI::Precision(1.0e-6);
  //
  // assy->AddShape(aResComp);
  //
  // STEPCAFControl_Writer aWriter;
  // aWriter.SetColorMode(Standard_True);
  // aWriter.SetNameMode(Standard_True);
  // aWriter.Transfer(doc, STEPControl_AsIs);
  // aWriter.Write("/home/urban/test.stp");
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace project
}  // namespace librepcb
