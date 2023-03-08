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
#include "boarddesignrulecheck.h"

#include "../../../geometry/hole.h"
#include "../../../geometry/stroketext.h"
#include "../../../library/cmp/component.h"
#include "../../../library/pkg/footprint.h"
#include "../../../library/pkg/footprintpad.h"
#include "../../../library/pkg/packagepad.h"
#include "../../../utils/clipperhelpers.h"
#include "../../../utils/toolbox.h"
#include "../../../utils/transform.h"
#include "../../circuit/circuit.h"
#include "../../circuit/componentinstance.h"
#include "../../circuit/netsignal.h"
#include "../../project.h"
#include "../board.h"
#include "../boardlayerstack.h"
#include "../items/bi_airwire.h"
#include "../items/bi_device.h"
#include "../items/bi_footprintpad.h"
#include "../items/bi_hole.h"
#include "../items/bi_netline.h"
#include "../items/bi_netpoint.h"
#include "../items/bi_netsegment.h"
#include "../items/bi_plane.h"
#include "../items/bi_polygon.h"
#include "../items/bi_stroketext.h"
#include "../items/bi_via.h"
#include "boardclipperpathgenerator.h"

#include <QtCore>

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

BoardDesignRuleCheck::BoardDesignRuleCheck(
    Board& board, const BoardDesignRuleCheckSettings& settings,
    QObject* parent) noexcept
  : QObject(parent),
    mBoard(board),
    mSettings(settings),
    mIgnorePlanes(false),
    mProgressPercent(0),
    mProgressStatus(),
    mMessages() {
}

BoardDesignRuleCheck::~BoardDesignRuleCheck() noexcept {
}

/*******************************************************************************
 *  General Methods
 ******************************************************************************/

void BoardDesignRuleCheck::execute(bool quick) {
  emit started();
  emitProgress(2);

  mIgnorePlanes = quick;
  mProgressStatus.clear();
  mMessages.clear();

  if (!quick) {
    rebuildPlanes(12);  // 10%
  }

  checkMinimumCopperWidth(14);  // 2%
  checkCopperCopperClearances(34);  // 20%
  checkCopperBoardClearances(44);  // 10%
  checkCopperHoleClearances(54);  // 10%

  if (!quick) {
    checkMinimumPthAnnularRing(64);  // 10%
    checkMinimumNpthDrillDiameter(66);  // 2%
    checkMinimumNpthSlotWidth(68);  // 2%
    checkMinimumPthDrillDiameter(70);  // 2%
    checkMinimumPthSlotWidth(72);  // 2%
    checkAllowedNpthSlots(74);  // 2%
    checkAllowedPthSlots(76);  // 2%
    checkInvalidPadConnections(78);  // 2%
    checkCourtyardClearances(91);  // 13%
    checkForUnplacedComponents(93);  // 2%
    checkForMissingConnections(95);  // 2%
    checkForStaleObjects(97);  // 2%
  }

  emitStatus(
      tr("Finished with %1 message(s)!", "Count of messages", mMessages.count())
          .arg(mMessages.count()));
  emitProgress(100);
  emit finished();
}

/*******************************************************************************
 *  Private Methods
 ******************************************************************************/

void BoardDesignRuleCheck::rebuildPlanes(int progressEnd) {
  emitStatus(tr("Rebuild planes..."));
  mBoard.rebuildAllPlanes();
  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkMinimumCopperWidth(int progressEnd) {
  const UnsignedLength minWidth = mSettings.getMinCopperWidth();
  if (minWidth == 0) {
    return;
  }

  emitStatus(tr("Check minimum copper width..."));

  // stroke texts
  foreach (const BI_StrokeText* text, mBoard.getStrokeTexts()) {
    const GraphicsLayer* layer =
        mBoard.getLayerStack().getLayer(*text->getText().getLayerName());
    if ((!layer) || (!layer->isCopperLayer()) || (!layer->isEnabled())) {
      continue;
    }
    if (text->getText().getStrokeWidth() < minWidth) {
      QVector<Path> locations;
      Transform transform(text->getText());
      foreach (Path path, transform.map(text->generatePaths())) {
        locations += path.toOutlineStrokes(PositiveLength(
            qMax(*text->getText().getStrokeWidth(), Length(50000))));
      }
      emitMessage(std::make_shared<DrcMsgMinimumWidthViolation>(*text, minWidth,
                                                                locations));
    }
  }

  // planes
  foreach (const BI_Plane* plane, mBoard.getPlanes()) {
    const GraphicsLayer* layer =
        mBoard.getLayerStack().getLayer(*plane->getLayerName());
    if ((!layer) || (!layer->isCopperLayer()) || (!layer->isEnabled())) {
      continue;
    }
    if (plane->getMinWidth() < minWidth) {
      const QVector<Path> locations =
          plane->getOutline().toClosedPath().toOutlineStrokes(
              PositiveLength(200000));
      emitMessage(std::make_shared<DrcMsgMinimumWidthViolation>(
          *plane, minWidth, locations));
    }
  }

  // devices
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    foreach (const BI_StrokeText* text, device->getStrokeTexts()) {
      // Do *not* mirror layer since it is independent of the device!
      const GraphicsLayer* layer =
          mBoard.getLayerStack().getLayer(*text->getText().getLayerName());
      if ((!layer) || (!layer->isCopperLayer()) || (!layer->isEnabled())) {
        continue;
      }
      if (text->getText().getStrokeWidth() < minWidth) {
        QVector<Path> locations;
        Transform transform(text->getText());
        foreach (Path path, transform.map(text->generatePaths())) {
          locations += path.toOutlineStrokes(PositiveLength(
              qMax(*text->getText().getStrokeWidth(), Length(50000))));
        }
        emitMessage(std::make_shared<DrcMsgMinimumWidthViolation>(
            *text, minWidth, locations));
      }
    }
  }

  // netlines
  foreach (const BI_NetSegment* netsegment, mBoard.getNetSegments()) {
    foreach (const BI_NetLine* netline, netsegment->getNetLines()) {
      if ((!netline->getLayer().isCopperLayer()) ||
          (!netline->getLayer().isEnabled())) {
        continue;
      }
      if (netline->getWidth() < minWidth) {
        const QVector<Path> locations{Path::obround(
            netline->getStartPoint().getPosition(),
            netline->getEndPoint().getPosition(), netline->getWidth())};
        emitMessage(std::make_shared<DrcMsgMinimumWidthViolation>(
            *netline, minWidth, locations));
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkCopperCopperClearances(int progressEnd) {
  const UnsignedLength clearance = mSettings.getMinCopperCopperClearance();
  if (clearance == 0) {
    return;
  }

  emitStatus(tr("Check copper clearances..."));

  // Calculate offset to be applied to each object.
  const Length offset =
      std::max(((clearance - maxArcTolerance()) / 2) - Length(1), Length(0));

  // Determine all copper layers.
  QSet<QString> layers;
  foreach (const GraphicsLayer* layer, mBoard.getLayerStack().getAllLayers()) {
    if (layer->isCopperLayer() && layer->isEnabled()) {
      layers.insert(layer->getName());
    }
  }

  // Determine the area of each copper object.
  struct Item {
    const BI_Base* item;
    const Polygon* polygon;  // Only relevant if item is a BI_Device
    const Circle* circle;  // Only relevant if item is a BI_Device
    QString layer;  // Empty = THT
    const NetSignal* netSignal;  // nullptr = no net
    ClipperLib::Paths areas;
  };
  QVector<Item> items;

  // Net segments.
  foreach (const BI_NetSegment* netSegment, mBoard.getNetSegments()) {
    // vias.
    foreach (const BI_Via* via, netSegment->getVias()) {
      BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
      gen.addVia(*via, offset);
      items.append(Item{via, nullptr, nullptr, QString(),
                        via->getNetSegment().getNetSignal(), gen.getPaths()});
    }

    // Net lines.
    foreach (const BI_NetLine* netLine, netSegment->getNetLines()) {
      if (layers.contains(netLine->getLayer().getName())) {
        BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
        gen.addNetLine(*netLine, offset);
        items.append(
            Item{netLine, nullptr, nullptr, netLine->getLayer().getName(),
                 netLine->getNetSegment().getNetSignal(), gen.getPaths()});
      }
    }
  }

  // Planes.
  if (!mIgnorePlanes) {
    foreach (const BI_Plane* plane, mBoard.getPlanes()) {
      if (layers.contains(*plane->getLayerName())) {
        BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
        gen.addPlane(*plane);
        ClipperLib::Paths paths = gen.getPaths();
        ClipperHelpers::offset(paths, offset, maxArcTolerance());
        items.append(Item{plane, nullptr, nullptr, *plane->getLayerName(),
                          &plane->getNetSignal(), paths});
      }
    }
  }

  // Board polygons.
  foreach (const BI_Polygon* polygon, mBoard.getPolygons()) {
    if (layers.contains(*polygon->getPolygon().getLayerName())) {
      BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
      gen.addPolygon(*polygon);
      ClipperLib::Paths paths = gen.getPaths();
      ClipperHelpers::offset(paths, offset, maxArcTolerance());
      items.append(Item{polygon, nullptr, nullptr,
                        *polygon->getPolygon().getLayerName(), nullptr, paths});
    }
  }

  // Board stroke texts.
  foreach (const BI_StrokeText* strokeText, mBoard.getStrokeTexts()) {
    if (layers.contains(*strokeText->getText().getLayerName())) {
      BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
      gen.addStrokeText(*strokeText, offset);
      items.append(Item{strokeText, nullptr, nullptr,
                        *strokeText->getText().getLayerName(), nullptr,
                        gen.getPaths()});
    }
  }

  // Devices.
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    const Transform transform(*device);

    // Pads.
    foreach (const BI_FootprintPad* pad, device->getPads()) {
      foreach (const QString& layer, layers) {
        if (pad->isOnLayer(layer)) {
          BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
          gen.addPad(*pad, transform, layer, offset);
          items.append(Item{pad, nullptr, nullptr, layer,
                            pad->getCompSigInstNetSignal(), gen.getPaths()});
        }
      }
    }

    // Polygons.
    for (const Polygon& polygon : device->getLibFootprint().getPolygons()) {
      if (layers.contains(*polygon.getLayerName())) {
        BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
        gen.addPolygon(polygon, transform);
        ClipperLib::Paths paths = gen.getPaths();
        ClipperHelpers::offset(paths, offset, maxArcTolerance());
        items.append(Item{device, &polygon, nullptr, *polygon.getLayerName(),
                          nullptr, paths});
      }
    }

    // Circles.
    for (const Circle& circle : device->getLibFootprint().getCircles()) {
      if (layers.contains(*circle.getLayerName())) {
        BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
        gen.addCircle(circle, transform, offset);
        items.append(Item{device, nullptr, &circle, *circle.getLayerName(),
                          nullptr, gen.getPaths()});
      }
    }

    // Stroke texts.
    foreach (const BI_StrokeText* strokeText, device->getStrokeTexts()) {
      if (layers.contains(*strokeText->getText().getLayerName())) {
        BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
        gen.addStrokeText(*strokeText, offset);
        items.append(Item{strokeText, nullptr, nullptr,
                          *strokeText->getText().getLayerName(), nullptr,
                          gen.getPaths()});
      }
    }
  }

  // Now check for intersections.
  auto lastItem = items.isEmpty() ? items.end() : std::prev(items.end());
  for (auto it1 = items.begin(); it1 != lastItem; it1++) {
    for (auto it2 = it1 + 1; it2 != items.end(); it2++) {
      if (((it1->netSignal != it2->netSignal) || (!it1->netSignal) ||
           (!it2->netSignal)) &&
          (it1->layer.isEmpty() || it2->layer.isEmpty() ||
           (it1->layer == it2->layer))) {
        const std::unique_ptr<ClipperLib::PolyTree> intersections =
            ClipperHelpers::intersect(it1->areas, it2->areas);
        const ClipperLib::Paths paths =
            ClipperHelpers::flattenTree(*intersections);
        if (!paths.empty()) {
          const QVector<Path> locations = ClipperHelpers::convert(paths);
          emitMessage(std::make_shared<DrcMsgCopperCopperClearanceViolation>(
              it1->layer, it1->netSignal, *it1->item, it1->polygon, it1->circle,
              it2->layer, it2->netSignal, *it2->item, it2->polygon, it2->circle,
              clearance, locations));
        }
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkCopperBoardClearances(int progressEnd) {
  const UnsignedLength clearance = mSettings.getMinCopperBoardClearance();
  if (clearance == 0) {
    return;
  }

  emitStatus(tr("Check board clearances..."));

  // Determine restricted area around board outline.
  const PositiveLength clearanceWidth(
      std::max(clearance + clearance - maxArcTolerance() - 1, Length(1)));
  ClipperLib::Paths restrictedArea;
  foreach (const BI_Polygon* polygon, mBoard.getPolygons()) {
    if (polygon->getPolygon().getLayerName() == GraphicsLayer::sBoardOutlines) {
      const ClipperLib::Paths paths = ClipperHelpers::convert(
          polygon->getPolygon().getPath().toOutlineStrokes(clearanceWidth),
          maxArcTolerance());
      restrictedArea.insert(restrictedArea.end(), paths.begin(), paths.end());
    }
  }
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    Transform transform(*device);
    for (const Polygon& polygon : device->getLibFootprint().getPolygons()) {
      if (polygon.getLayerName() == GraphicsLayer::sBoardOutlines) {
        const ClipperLib::Paths paths = ClipperHelpers::convert(
            transform.map(polygon.getPath()).toOutlineStrokes(clearanceWidth),
            maxArcTolerance());
        restrictedArea.insert(restrictedArea.end(), paths.begin(), paths.end());
      }
    }
  }
  ClipperHelpers::unite(restrictedArea, ClipperLib::pftNonZero);

  // Helper for the actual check.
  QVector<Path> locations;
  auto intersects = [&restrictedArea,
                     &locations](const ClipperLib::Paths& paths) {
    std::unique_ptr<ClipperLib::PolyTree> intersections =
        ClipperHelpers::intersect(restrictedArea, paths);
    locations =
        ClipperHelpers::convert(ClipperHelpers::flattenTree(*intersections));
    return (!locations.isEmpty());
  };

  // Check net segments.
  foreach (const BI_NetSegment* netSegment, mBoard.getNetSegments()) {
    // Check vias.
    foreach (const BI_Via* via, netSegment->getVias()) {
      BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
      gen.addVia(*via);
      if (intersects(gen.getPaths())) {
        emitMessage(std::make_shared<DrcMsgCopperBoardClearanceViolation>(
            *via, clearance, locations));
      }
    }

    // Check net lines.
    foreach (const BI_NetLine* netLine, netSegment->getNetLines()) {
      BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
      gen.addNetLine(*netLine);
      if (intersects(gen.getPaths())) {
        emitMessage(std::make_shared<DrcMsgCopperBoardClearanceViolation>(
            *netLine, clearance, locations));
      }
    }
  }

  // Check planes.
  if (!mIgnorePlanes) {
    foreach (const BI_Plane* plane, mBoard.getPlanes()) {
      BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
      gen.addPlane(*plane);
      if (intersects(gen.getPaths())) {
        emitMessage(std::make_shared<DrcMsgCopperBoardClearanceViolation>(
            *plane, clearance, locations));
      }
    }
  }

  // Check board polygons.
  foreach (const BI_Polygon* polygon, mBoard.getPolygons()) {
    const GraphicsLayer* layer =
        mBoard.getLayerStack().getLayer(*polygon->getPolygon().getLayerName());
    if (layer && layer->isCopperLayer() && layer->isEnabled()) {
      BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
      gen.addPolygon(*polygon);
      if (intersects(gen.getPaths())) {
        emitMessage(std::make_shared<DrcMsgCopperBoardClearanceViolation>(
            nullptr, polygon->getPolygon(), clearance, locations));
      }
    }
  }

  // Check board stroke texts.
  foreach (const BI_StrokeText* strokeText, mBoard.getStrokeTexts()) {
    const GraphicsLayer* layer =
        mBoard.getLayerStack().getLayer(*strokeText->getText().getLayerName());
    if (layer && layer->isCopperLayer() && layer->isEnabled()) {
      BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
      gen.addStrokeText(*strokeText);
      if (intersects(gen.getPaths())) {
        emitMessage(std::make_shared<DrcMsgCopperBoardClearanceViolation>(
            nullptr, strokeText->getText(), clearance, locations));
      }
    }
  }

  // Check devices.
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    const Transform transform(*device);

    // Check pads.
    foreach (const BI_FootprintPad* pad, device->getPads()) {
      foreach (const GraphicsLayer* layer,
               mBoard.getLayerStack().getAllLayers()) {
        if (layer->isCopperLayer() && layer->isEnabled() &&
            pad->isOnLayer(layer->getName())) {
          BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
          gen.addPad(*pad, transform, layer->getName());
          if (intersects(gen.getPaths())) {
            emitMessage(std::make_shared<DrcMsgCopperBoardClearanceViolation>(
                *pad, clearance, locations));
          }
        }
      }
    }

    // Check polygons.
    for (const Polygon& polygon : device->getLibFootprint().getPolygons()) {
      const GraphicsLayer* layer =
          mBoard.getLayerStack().getLayer(*polygon.getLayerName());
      if (layer && layer->isCopperLayer() && layer->isEnabled()) {
        BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
        gen.addPolygon(polygon, transform);
        if (intersects(gen.getPaths())) {
          emitMessage(std::make_shared<DrcMsgCopperBoardClearanceViolation>(
              device, polygon, clearance, locations));
        }
      }
    }

    // Check circles.
    for (const Circle& circle : device->getLibFootprint().getCircles()) {
      const GraphicsLayer* layer =
          mBoard.getLayerStack().getLayer(*circle.getLayerName());
      if (layer && layer->isCopperLayer() && layer->isEnabled()) {
        BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
        gen.addCircle(circle, transform);
        if (intersects(gen.getPaths())) {
          emitMessage(std::make_shared<DrcMsgCopperBoardClearanceViolation>(
              device, circle, clearance, locations));
        }
      }
    }

    // Check stroke texts.
    foreach (const BI_StrokeText* strokeText, device->getStrokeTexts()) {
      const GraphicsLayer* layer = mBoard.getLayerStack().getLayer(
          *strokeText->getText().getLayerName());
      if (layer && layer->isCopperLayer() && layer->isEnabled()) {
        BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
        gen.addStrokeText(*strokeText);
        if (intersects(gen.getPaths())) {
          emitMessage(std::make_shared<DrcMsgCopperBoardClearanceViolation>(
              device, strokeText->getText(), clearance, locations));
        }
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkCopperHoleClearances(int progressEnd) {
  const UnsignedLength clearance = mSettings.getMinCopperNpthClearance();
  if (clearance == 0) {
    return;
  }

  emitStatus(tr("Check hole clearances..."));

  // Determine tha areas where copper is available on *any* layer.
  ClipperLib::Paths copperAreas;
  for (const GraphicsLayer* l : mBoard.getLayerStack().getAllLayers()) {
    if (l->isCopperLayer() && l->isEnabled()) {
      ClipperHelpers::unite(copperAreas, getCopperPaths(*l, {}));
    }
  }

  // Helper for the actual check.
  QVector<Path> locations;
  auto intersects = [this, &clearance, &copperAreas, &locations](
                        const Hole& hole, const Transform& transform) {
    BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
    gen.addHole(hole, transform, clearance - *maxArcTolerance() - Length(1));
    std::unique_ptr<ClipperLib::PolyTree> intersections =
        ClipperHelpers::intersect(copperAreas, gen.getPaths());
    locations =
        ClipperHelpers::convert(ClipperHelpers::flattenTree(*intersections));
    return (!locations.isEmpty());
  };

  // Check board holes.
  foreach (const BI_Hole* hole, mBoard.getHoles()) {
    if (intersects(hole->getHole(), Transform())) {
      emitMessage(std::make_shared<DrcMsgCopperHoleClearanceViolation>(
          nullptr, hole->getHole(), clearance, locations));
    }
  }

  // Check footprint holes.
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    const Transform transform(*device);
    for (const Hole& hole : device->getLibFootprint().getHoles()) {
      if (intersects(hole, transform)) {
        emitMessage(std::make_shared<DrcMsgCopperHoleClearanceViolation>(
            device, hole, clearance, locations));
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkMinimumPthAnnularRing(int progressEnd) {
  const UnsignedLength annularWidth = mSettings.getMinPthAnnularRing();
  if (annularWidth == 0) {
    return;
  }

  emitStatus(tr("Check minimum PTH annular rings..."));

  // Determine tha areas where copper is available on *all* layers.
  QList<ClipperLib::Paths> thtCopperAreas;
  for (const GraphicsLayer* l : mBoard.getLayerStack().getAllLayers()) {
    if (l->isCopperLayer() && l->isEnabled()) {
      thtCopperAreas.append(getCopperPaths(*l, {}));
    }
  }
  std::unique_ptr<ClipperLib::PolyTree> thtCopperAreaIntersections =
      ClipperHelpers::intersect(thtCopperAreas);
  const ClipperLib::Paths thtCopperAreaPaths =
      ClipperHelpers::treeToPaths(*thtCopperAreaIntersections);

  // Check via annular rings.
  foreach (const BI_NetSegment* netsegment, mBoard.getNetSegments()) {
    foreach (const BI_Via* via, netsegment->getVias()) {
      // Determine via area including minimum annular ring.
      const Length diameter = via->getDrillDiameter() + (*annularWidth * 2) - 1;
      if (diameter <= 0) {
        continue;
      }
      const ClipperLib::Paths areas{ClipperHelpers::convert(
          Path::circle(PositiveLength(diameter)).translated(via->getPosition()),
          maxArcTolerance())};

      // Check if there's not a 100% overlap.
      const std::unique_ptr<ClipperLib::PolyTree> remainingAreasTree =
          ClipperHelpers::subtractToTree(areas, thtCopperAreaPaths);
      const ClipperLib::Paths remainingAreas =
          ClipperHelpers::flattenTree(*remainingAreasTree);
      if (!remainingAreas.empty()) {
        const QVector<Path> locations = ClipperHelpers::convert(remainingAreas);
        emitMessage(std::make_shared<DrcMsgMinimumAnnularRingViolation>(
            *via, annularWidth, locations));
      }
    }
  }

  // Check pad annular rings.
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    foreach (const BI_FootprintPad* pad, device->getPads()) {
      // Determine hole areas including minimum annular ring.
      const Transform transform(*pad);
      ClipperLib::Paths areas;
      for (const PadHole& hole : pad->getLibPad().getHoles()) {
        const Length diameter = hole.getDiameter() + (*annularWidth * 2) - 1;
        if (diameter <= 0) {
          continue;
        }
        foreach (const Path& area,
                 hole.getPath()->toOutlineStrokes(PositiveLength(diameter))) {
          ClipperHelpers::unite(
              areas,
              ClipperHelpers::convert(transform.map(area), maxArcTolerance()));
        }
      }

      // Check if there's not a 100% overlap.
      const std::unique_ptr<ClipperLib::PolyTree> remainingAreasTree =
          ClipperHelpers::subtractToTree(areas, thtCopperAreaPaths);
      const ClipperLib::Paths remainingAreas =
          ClipperHelpers::flattenTree(*remainingAreasTree);
      if (!remainingAreas.empty()) {
        const QVector<Path> locations = ClipperHelpers::convert(remainingAreas);
        emitMessage(std::make_shared<DrcMsgMinimumAnnularRingViolation>(
            *pad, annularWidth, locations));
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkMinimumNpthDrillDiameter(int progressEnd) {
  const UnsignedLength minDiameter = mSettings.getMinNpthDrillDiameter();
  if (minDiameter == 0) {
    return;
  }

  emitStatus(tr("Check minimum NPTH drill diameters..."));

  // Board holes.
  foreach (const BI_Hole* hole, mBoard.getHoles()) {
    if ((!hole->getHole().isSlot()) &&
        (hole->getHole().getDiameter() < minDiameter)) {
      emitMessage(std::make_shared<DrcMsgMinimumDrillDiameterViolation>(
          nullptr, hole->getHole(), minDiameter,
          getHoleLocation(hole->getHole())));
    }
  }

  // Package holes.
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    Transform transform(*device);
    for (const Hole& hole : device->getLibFootprint().getHoles()) {
      if ((!hole.isSlot()) && (hole.getDiameter() < *minDiameter)) {
        emitMessage(std::make_shared<DrcMsgMinimumDrillDiameterViolation>(
            device, hole, minDiameter, getHoleLocation(hole, transform)));
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkMinimumNpthSlotWidth(int progressEnd) {
  const UnsignedLength minWidth = mSettings.getMinNpthSlotWidth();
  if (minWidth == 0) {
    return;
  }

  emitStatus(tr("Check minimum NPTH slot width..."));

  // Board holes.
  foreach (const BI_Hole* hole, mBoard.getHoles()) {
    if ((hole->getHole().isSlot()) &&
        (hole->getHole().getDiameter() < minWidth)) {
      emitMessage(std::make_shared<DrcMsgMinimumSlotWidthViolation>(
          nullptr, hole->getHole(), minWidth,
          getHoleLocation(hole->getHole())));
    }
  }

  // Package holes.
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    Transform transform(*device);
    for (const Hole& hole : device->getLibFootprint().getHoles()) {
      if ((hole.isSlot()) && (hole.getDiameter() < *minWidth)) {
        emitMessage(std::make_shared<DrcMsgMinimumSlotWidthViolation>(
            device, hole, minWidth, getHoleLocation(hole, transform)));
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkMinimumPthDrillDiameter(int progressEnd) {
  const UnsignedLength minDiameter = mSettings.getMinPthDrillDiameter();
  if (minDiameter == 0) {
    return;
  }

  emitStatus(tr("Check minimum PTH drill diameters..."));

  // Vias.
  foreach (const BI_NetSegment* netsegment, mBoard.getNetSegments()) {
    foreach (const BI_Via* via, netsegment->getVias()) {
      if (via->getDrillDiameter() < minDiameter) {
        const QVector<Path> locations{Path::circle(via->getDrillDiameter())
                                          .translated(via->getPosition())};
        emitMessage(std::make_shared<DrcMsgMinimumDrillDiameterViolation>(
            *via, minDiameter, locations));
      }
    }
  }

  // Pads.
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    foreach (const BI_FootprintPad* pad, device->getPads()) {
      for (const PadHole& hole : pad->getLibPad().getHoles()) {
        if (hole.getDiameter() < *minDiameter) {
          PositiveLength diameter(qMax(*hole.getDiameter(), Length(50000)));
          const QVector<Path> locations{
              Path::circle(diameter).translated(pad->getPosition())};
          emitMessage(std::make_shared<DrcMsgMinimumDrillDiameterViolation>(
              *pad, hole, minDiameter, locations));
        }
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkMinimumPthSlotWidth(int progressEnd) {
  const UnsignedLength minWidth = mSettings.getMinPthSlotWidth();
  if (minWidth == 0) {
    return;
  }

  emitStatus(tr("Check minimum PTH slot width..."));

  // Pads.
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    Transform devTransform(*device);
    foreach (const BI_FootprintPad* pad, device->getPads()) {
      Transform padTransform(pad->getLibPad().getPosition(),
                             pad->getLibPad().getRotation());
      for (const PadHole& hole : pad->getLibPad().getHoles()) {
        if ((hole.isSlot()) && (hole.getDiameter() < *minWidth)) {
          emitMessage(std::make_shared<DrcMsgMinimumSlotWidthViolation>(
              *pad, hole, minWidth,
              getHoleLocation(hole, padTransform, devTransform)));
        }
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkAllowedNpthSlots(int progressEnd) {
  const BoardDesignRuleCheckSettings::AllowedSlots allowed =
      mSettings.getAllowedNpthSlots();
  if (allowed == BoardDesignRuleCheckSettings::AllowedSlots::Any) {
    return;
  }

  emitStatus(tr("Check for disallowed NPTH slots..."));

  // Board holes.
  foreach (const BI_Hole* hole, mBoard.getHoles()) {
    if (requiresHoleSlotWarning(hole->getHole(), allowed)) {
      emitMessage(std::make_shared<DrcMsgForbiddenSlot>(
          *hole, getHoleLocation(hole->getHole())));
    }
  }

  // Package holes.
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    Transform transform(*device);
    for (const Hole& hole : device->getLibFootprint().getHoles()) {
      if (requiresHoleSlotWarning(hole, allowed)) {
        emitMessage(std::make_shared<DrcMsgForbiddenSlot>(
            *device, hole, getHoleLocation(hole, transform)));
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkAllowedPthSlots(int progressEnd) {
  const BoardDesignRuleCheckSettings::AllowedSlots allowed =
      mSettings.getAllowedPthSlots();
  if (allowed == BoardDesignRuleCheckSettings::AllowedSlots::Any) {
    return;
  }

  emitStatus(tr("Check for disallowed PTH slots..."));

  // Pads.
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    Transform devTransform(*device);
    foreach (const BI_FootprintPad* pad, device->getPads()) {
      Transform padTransform(pad->getLibPad().getPosition(),
                             pad->getLibPad().getRotation());
      for (const PadHole& hole : pad->getLibPad().getHoles()) {
        if (requiresHoleSlotWarning(hole, allowed)) {
          emitMessage(std::make_shared<DrcMsgForbiddenSlot>(
              *pad, hole, getHoleLocation(hole, padTransform, devTransform)));
        }
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkInvalidPadConnections(int progressEnd) {
  emitStatus(tr("Check pad connections..."));

  // Pads.
  foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
    foreach (const BI_FootprintPad* pad, device->getPads()) {
      QSet<const GraphicsLayer*> connectedLayers;
      foreach (const BI_NetLine* netLine, pad->getNetLines()) {
        connectedLayers.insert(&netLine->getLayer());
      }
      foreach (const GraphicsLayer* layer, connectedLayers) {
        bool isOriginInCopper = false;
        foreach (const PadGeometry& geometry,
                 pad->getGeometryOnLayer(layer->getName())) {
          if (geometry.toFilledQPainterPathPx().contains(QPointF(0, 0))) {
            isOriginInCopper = true;
            break;
          }
        }
        if (!isOriginInCopper) {
          const QVector<Path> locations{
              Path::circle(PositiveLength(500000))
                  .translated(pad->getPosition()),
          };
          emitMessage(std::make_shared<DrcMsgInvalidPadConnection>(*pad, *layer,
                                                                   locations));
        }
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkCourtyardClearances(int progressEnd) {
  emitStatus(tr("Check courtyard clearances..."));

  auto layers = mBoard.getLayerStack().getLayers(
      {GraphicsLayer::sTopCourtyard, GraphicsLayer::sBotCourtyard});
  foreach (const GraphicsLayer* layer, layers) {
    // determine device courtyard areas
    QMap<const BI_Device*, ClipperLib::Paths> deviceCourtyards;
    foreach (const BI_Device* device, mBoard.getDeviceInstances()) {
      deviceCourtyards.insert(device, getDeviceCourtyardPaths(*device, layer));
    }

    // check clearances
    for (int i = 0; i < deviceCourtyards.count(); ++i) {
      const BI_Device* dev1 = deviceCourtyards.keys()[i];
      Q_ASSERT(dev1);
      const ClipperLib::Paths& paths1 = deviceCourtyards[dev1];
      for (int k = i + 1; k < deviceCourtyards.count(); ++k) {
        const BI_Device* dev2 = deviceCourtyards.keys()[k];
        Q_ASSERT(dev2);
        const ClipperLib::Paths& paths2 = deviceCourtyards[dev2];
        const std::unique_ptr<ClipperLib::PolyTree> intersections =
            ClipperHelpers::intersect(paths1, paths2);
        const QVector<Path> locations = ClipperHelpers::convert(
            ClipperHelpers::flattenTree(*intersections));
        if (!locations.isEmpty()) {
          emitMessage(std::make_shared<DrcMsgCourtyardOverlap>(*dev1, *dev2,
                                                               locations));
        }
      }
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkForUnplacedComponents(int progressEnd) {
  emitStatus(tr("Check for unplaced components..."));

  foreach (const ComponentInstance* cmp,
           mBoard.getProject().getCircuit().getComponentInstances()) {
    const BI_Device* dev =
        mBoard.getDeviceInstanceByComponentUuid(cmp->getUuid());
    if ((!dev) && (!cmp->getLibComponent().isSchematicOnly())) {
      emitMessage(std::make_shared<DrcMsgMissingDevice>(*cmp));
    }
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkForMissingConnections(int progressEnd) {
  emitStatus(tr("Check for missing connections..."));

  // No check based on copper paths implemented yet -> return existing airwires
  // instead.
  mBoard.forceAirWiresRebuild();
  foreach (const BI_AirWire* airWire, mBoard.getAirWires()) {
    const QVector<Path> locations{Path::obround(airWire->getP1().getPosition(),
                                                airWire->getP2().getPosition(),
                                                PositiveLength(50000))};
    emitMessage(std::make_shared<DrcMsgMissingConnection>(
        airWire->getP1(), airWire->getP2(), airWire->getNetSignal(),
        locations));
  }

  emitProgress(progressEnd);
}

void BoardDesignRuleCheck::checkForStaleObjects(int progressEnd) {
  emitStatus(tr("Check for stale objects..."));

  foreach (const BI_NetSegment* netSegment, mBoard.getNetSegments()) {
    // Warn about empty net segments.
    if (!netSegment->isUsed()) {
      emitMessage(std::make_shared<DrcMsgEmptyNetSegment>(*netSegment));
    }

    // Warn about net points without any net lines.
    foreach (const BI_NetPoint* netPoint, netSegment->getNetPoints()) {
      if (!netPoint->isUsed()) {
        const QVector<Path> locations{Path::circle(PositiveLength(300000))
                                          .translated(netPoint->getPosition())};
        emitMessage(
            std::make_shared<DrcMsgUnconnectedJunction>(*netPoint, locations));
      }
    }
  }

  emitProgress(progressEnd);
}

template <typename THole>
bool BoardDesignRuleCheck::requiresHoleSlotWarning(
    const THole& hole, BoardDesignRuleCheckSettings::AllowedSlots allowed) {
  if (hole.isCurvedSlot() &&
      (allowed < BoardDesignRuleCheckSettings::AllowedSlots::Any)) {
    return true;
  } else if (hole.isMultiSegmentSlot() &&
             (allowed < BoardDesignRuleCheckSettings::AllowedSlots::
                            MultiSegmentStraight)) {
    return true;
  } else if (hole.isSlot() &&
             (allowed < BoardDesignRuleCheckSettings::AllowedSlots::
                            SingleSegmentStraight)) {
    return true;
  } else {
    return false;
  }
}

const ClipperLib::Paths& BoardDesignRuleCheck::getCopperPaths(
    const GraphicsLayer& layer, const QSet<const NetSignal*>& netsignals) {
  const auto key = qMakePair(&layer, netsignals);
  if (!mCachedPaths.contains(key)) {
    BoardClipperPathGenerator gen(mBoard, maxArcTolerance());
    gen.addCopper(layer.getName(), netsignals, mIgnorePlanes);
    mCachedPaths[key] = gen.getPaths();
  }
  return mCachedPaths[key];
}

ClipperLib::Paths BoardDesignRuleCheck::getDeviceCourtyardPaths(
    const BI_Device& device, const GraphicsLayer* layer) {
  ClipperLib::Paths paths;
  Transform transform(device);
  for (const Polygon& polygon : device.getLibFootprint().getPolygons()) {
    GraphicsLayerName polygonLayer = transform.map(polygon.getLayerName());
    if (polygonLayer != layer->getName()) {
      continue;
    }
    Path path = transform.map(polygon.getPath());
    ClipperHelpers::unite(paths,
                          ClipperHelpers::convert(path, maxArcTolerance()));
  }
  for (const Circle& circle : device.getLibFootprint().getCircles()) {
    GraphicsLayerName circleLayer = transform.map(circle.getLayerName());
    if (circleLayer != layer->getName()) {
      continue;
    }
    Point absolutePos = transform.map(circle.getCenter());
    ClipperHelpers::unite(
        paths,
        ClipperHelpers::convert(Path::circle(circle.getDiameter()),
                                maxArcTolerance()));
  }
  return paths;
}

template <typename THole>
QVector<Path> BoardDesignRuleCheck::getHoleLocation(
    const THole& hole, const Transform& transform1,
    const Transform& transform2) const noexcept {
  return transform2.map(
      transform1.map(hole.getPath())->toOutlineStrokes(hole.getDiameter()));
}

void BoardDesignRuleCheck::emitProgress(int percent) noexcept {
  mProgressPercent = percent;
  emit progressPercent(percent);
}

void BoardDesignRuleCheck::emitStatus(const QString& status) noexcept {
  mProgressStatus.append(status);
  emit progressStatus(status);
  qApp->processEvents();
}

void BoardDesignRuleCheck::emitMessage(
    const std::shared_ptr<const RuleCheckMessage>& msg) noexcept {
  mMessages.append(msg);
  emit progressMessage(msg->getMessage());
}

QString BoardDesignRuleCheck::formatLength(const Length& length) const
    noexcept {
  return Toolbox::floatToString(length.toMm(), 6, QLocale()) % "mm";
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace librepcb
