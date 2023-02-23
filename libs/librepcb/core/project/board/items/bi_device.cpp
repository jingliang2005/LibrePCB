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
#include "bi_device.h"

#include "../../../library/cmp/component.h"
#include "../../../library/dev/device.h"
#include "../../../library/pkg/package.h"
#include "../../../library/sym/symbol.h"
#include "../../../utils/scopeguardlist.h"
#include "../../../utils/transform.h"
#include "../../circuit/circuit.h"
#include "../../circuit/componentinstance.h"
#include "../../erc/ercmsg.h"
#include "../../project.h"
#include "../../projectlibrary.h"
#include "../../projectsettings.h"
#include "../board.h"
#include "bi_footprintpad.h"

#include <QtCore>

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

BI_Device::BI_Device(Board& board, ComponentInstance& compInstance,
                     const Uuid& deviceUuid, const Uuid& footprintUuid,
                     const Point& position, const Angle& rotation, bool mirror,
                     bool loadInitialStrokeTexts)
  : BI_Base(board),
    mCompInstance(compInstance),
    mLibDevice(nullptr),
    mLibPackage(nullptr),
    mLibFootprint(nullptr),
    mPosition(position),
    mRotation(rotation),
    mMirrored(mirror) {
  // get device from library
  mLibDevice = mBoard.getProject().getLibrary().getDevice(deviceUuid);
  if (!mLibDevice) {
    qCritical() << "No device for component:" << mCompInstance.getUuid();
    throw RuntimeError(__FILE__, __LINE__,
                       tr("No device with the UUID \"%1\" found in the "
                          "project's library.")
                           .arg(deviceUuid.toStr()));
  }
  // check if the device matches with the component
  if (mLibDevice->getComponentUuid() !=
      mCompInstance.getLibComponent().getUuid()) {
    throw RuntimeError(
        __FILE__, __LINE__,
        QString("The device \"%1\" does not match with the component"
                "instance \"%2\".")
            .arg(mLibDevice->getUuid().toStr(),
                 mCompInstance.getUuid().toStr()));
  }
  // get package from library
  Uuid packageUuid = mLibDevice->getPackageUuid();
  mLibPackage = mBoard.getProject().getLibrary().getPackage(packageUuid);
  if (!mLibPackage) {
    qCritical() << "No package for component:" << mCompInstance.getUuid();
    throw RuntimeError(__FILE__, __LINE__,
                       tr("No package with the UUID \"%1\" found in "
                          "the project's library.")
                           .arg(packageUuid.toStr()));
  }
  // get footprint from package
  mLibFootprint =
      mLibPackage->getFootprints().get(footprintUuid).get();  // can throw

  // Add initial attributes.
  mAttributes = mLibDevice->getAttributes();

  // Add initial stroke texts.
  if (loadInitialStrokeTexts) {
    for (const StrokeText& text : getDefaultStrokeTexts()) {
      addStrokeText(*new BI_StrokeText(mBoard, text));
    }
  }

  // Check pad-signal-map.
  for (const DevicePadSignalMapItem& item : mLibDevice->getPadSignalMap()) {
    tl::optional<Uuid> signalUuid = item.getSignalUuid();
    if ((signalUuid) && (!mCompInstance.getSignalInstance(*signalUuid))) {
      throw RuntimeError(
          __FILE__, __LINE__,
          QString("Unknown signal \"%1\" found in device \"%2\"")
              .arg(signalUuid->toStr(), mLibDevice->getUuid().toStr()));
    }
  }

  // Load pads.
  for (const FootprintPad& libPad : getLibFootprint().getPads()) {
    if (mPads.contains(libPad.getUuid())) {
      throw RuntimeError(
          __FILE__, __LINE__,
          QString("The footprint pad UUID \"%1\" is defined multiple times.")
              .arg(libPad.getUuid().toStr()));
    }
    if (libPad.getPackagePadUuid() &&
        (!mLibPackage->getPads().contains(*libPad.getPackagePadUuid()))) {
      throw RuntimeError(__FILE__, __LINE__,
                         QString("Pad \"%1\" not found in package \"%2\".")
                             .arg(libPad.getPackagePadUuid()->toStr(),
                                  mLibPackage->getUuid().toStr()));
    }
    if (libPad.getPackagePadUuid() &&
        (!mLibDevice->getPadSignalMap().contains(
            *libPad.getPackagePadUuid()))) {
      throw RuntimeError(__FILE__, __LINE__,
                         QString("Package pad \"%1\" not found in "
                                 "pad-signal-map of device \"%2\".")
                             .arg(libPad.getPackagePadUuid()->toStr(),
                                  mLibDevice->getUuid().toStr()));
    }
    BI_FootprintPad* pad = new BI_FootprintPad(*this, libPad.getUuid());
    mPads.insert(libPad.getUuid(), pad);
  }

  // Create graphics item.
  mGraphicsItem.reset(new BGI_Device(*this));
  mGraphicsItem->setPos(mPosition.toPxQPointF());
  updateGraphicsItemTransform();

  // Emit the "attributesChanged" signal when the board or component instance
  // has emitted it.
  connect(&mBoard, &Board::attributesChanged, this,
          &BI_Device::attributesChanged);
  connect(&mCompInstance, &ComponentInstance::attributesChanged, this,
          &BI_Device::attributesChanged);
}

BI_Device::~BI_Device() noexcept {
  mGraphicsItem.reset();
  qDeleteAll(mPads);
  mPads.clear();
  qDeleteAll(mStrokeTexts);
  mStrokeTexts.clear();
}

/*******************************************************************************
 *  Getters
 ******************************************************************************/

const Uuid& BI_Device::getComponentInstanceUuid() const noexcept {
  return mCompInstance.getUuid();
}

bool BI_Device::isUsed() const noexcept {
  foreach (const BI_FootprintPad* pad, mPads) {
    if (pad->isUsed()) return true;
  }
  return false;
}

QRectF BI_Device::getBoundingRect() const noexcept {
  return mGraphicsItem->sceneTransform().mapRect(mGraphicsItem->boundingRect());
}

BI_Device::MountType BI_Device::determineMountType() const noexcept {
  const QString mountType = getAttributeValue("MOUNT_TYPE").trimmed().toLower();
  if (mountType.isEmpty()) {
    // Auto-detection depending on footprint pads.
    bool hasThtPads = false;
    bool hasSmtPads = false;
    foreach (const BI_FootprintPad* pad, mPads) {
      if (pad->getLibPad().isTht()) {
        hasThtPads = true;
      } else {
        hasSmtPads = true;
      }
    }
    if (hasThtPads) {
      return MountType::Tht;
    } else if (hasSmtPads) {
      return MountType::Smt;
    } else {
      return MountType::None;
    }
  } else if (mountType == "tht") {
    return MountType::Tht;
  } else if (mountType == "smt") {
    return MountType::Smt;
  } else if (mountType == "fiducial") {
    return MountType::Fiducial;
  } else if (mountType == "none") {
    return MountType::None;
  } else {
    return MountType::Other;
  }
}

/*******************************************************************************
 *  StrokeText Methods
 ******************************************************************************/

StrokeTextList BI_Device::getDefaultStrokeTexts() const noexcept {
  // Copy all footprint texts and transform them to the global coordinate system
  // (not relative to the footprint). The original UUIDs are kept for future
  // identification.
  StrokeTextList texts = mLibFootprint->getStrokeTexts();
  Transform transform(*this);
  for (StrokeText& text : texts) {
    text.setPosition(transform.map(text.getPosition()));
    if (getMirrored()) {
      text.setRotation(text.getRotation() +
                       (text.getMirrored() ? -getRotation() : getRotation()));
    } else {
      text.setRotation(text.getRotation() +
                       (text.getMirrored() ? -getRotation() : getRotation()));
    }
    text.setMirrored(transform.map(text.getMirrored()));
    text.setLayerName(transform.map(text.getLayerName()));
  }
  return texts;
}

void BI_Device::addStrokeText(BI_StrokeText& text) {
  if ((mStrokeTexts.values().contains(&text)) ||
      (&text.getBoard() != &mBoard)) {
    throw LogicError(__FILE__, __LINE__);
  }
  if (mStrokeTexts.contains(text.getUuid())) {
    throw RuntimeError(
        __FILE__, __LINE__,
        QString("There is already a stroke text with the UUID \"%1\"!")
            .arg(text.getUuid().toStr()));
  }
  text.setDevice(this);
  if (isAddedToBoard()) {
    text.addToBoard();  // can throw
  }
  mStrokeTexts.insert(text.getUuid(), &text);
  text.setSelected(isSelected());
}

void BI_Device::removeStrokeText(BI_StrokeText& text) {
  if (mStrokeTexts.value(text.getUuid()) != &text) {
    throw LogicError(__FILE__, __LINE__);
  }
  if (isAddedToBoard()) {
    text.removeFromBoard();  // can throw
  }
  mStrokeTexts.remove(text.getUuid());
}

/*******************************************************************************
 *  General Methods
 ******************************************************************************/

void BI_Device::setPosition(const Point& pos) noexcept {
  if (pos != mPosition) {
    mPosition = pos;
    mGraphicsItem->setPos(pos.toPxQPointF());
    foreach (BI_FootprintPad* pad, mPads) {
      pad->updatePosition();
      mBoard.scheduleAirWiresRebuild(pad->getCompSigInstNetSignal());
    }
    foreach (BI_StrokeText* text, mStrokeTexts) { text->updateGraphicsItems(); }
  }
}

void BI_Device::setRotation(const Angle& rot) noexcept {
  if (rot != mRotation) {
    mRotation = rot;
    updateGraphicsItemTransform();
    foreach (BI_FootprintPad* pad, mPads) {
      pad->updatePosition();
      mBoard.scheduleAirWiresRebuild(pad->getCompSigInstNetSignal());
    }
  }
}

void BI_Device::setMirrored(bool mirror) {
  if (mirror != mMirrored) {
    if (isUsed()) {
      throw LogicError(__FILE__, __LINE__);
    }
    mMirrored = mirror;
    updateGraphicsItemTransform();
    mGraphicsItem->updateBoardSide();
    foreach (BI_FootprintPad* pad, mPads) {
      pad->updatePosition();
      mBoard.scheduleAirWiresRebuild(pad->getCompSigInstNetSignal());
    }
  }
}

void BI_Device::setAttributes(const AttributeList& attributes) noexcept {
  if (attributes != mAttributes) {
    mAttributes = attributes;
    emit attributesChanged();
  }
}

void BI_Device::addToBoard() {
  if (isAddedToBoard()) {
    throw LogicError(__FILE__, __LINE__);
  }
  ScopeGuardList sgl(mPads.count() + mStrokeTexts.count() + 1);
  mCompInstance.registerDevice(*this);  // can throw
  sgl.add([&]() { mCompInstance.unregisterDevice(*this); });
  foreach (BI_FootprintPad* pad, mPads) {
    pad->addToBoard();  // can throw
    sgl.add([pad]() { pad->removeFromBoard(); });
  }
  foreach (BI_StrokeText* text, mStrokeTexts) {
    text->addToBoard();  // can throw
    sgl.add([text]() { text->removeFromBoard(); });
  }
  BI_Base::addToBoard(mGraphicsItem.data());
  sgl.dismiss();
}

void BI_Device::removeFromBoard() {
  if (!isAddedToBoard()) {
    throw LogicError(__FILE__, __LINE__);
  }
  ScopeGuardList sgl(mPads.count() + mStrokeTexts.count() + 1);
  foreach (BI_FootprintPad* pad, mPads) {
    pad->removeFromBoard();  // can throw
    sgl.add([pad]() { pad->addToBoard(); });
  }
  foreach (BI_StrokeText* text, mStrokeTexts) {
    text->removeFromBoard();  // can throw
    sgl.add([text]() { text->addToBoard(); });
  }
  mCompInstance.unregisterDevice(*this);  // can throw
  sgl.add([&]() { mCompInstance.registerDevice(*this); });
  BI_Base::removeFromBoard(mGraphicsItem.data());
  sgl.dismiss();
}

void BI_Device::serialize(SExpression& root) const {
  if (!checkAttributesValidity()) throw LogicError(__FILE__, __LINE__);

  root.appendChild(mCompInstance.getUuid());
  root.ensureLineBreak();
  root.appendChild("lib_device", mLibDevice->getUuid());
  root.ensureLineBreak();
  root.appendChild("lib_footprint", mLibFootprint->getUuid());
  root.ensureLineBreak();
  mPosition.serialize(root.appendList("position"));
  root.appendChild("rotation", mRotation);
  root.appendChild("mirror", mMirrored);
  root.ensureLineBreak();
  mAttributes.serialize(root);
  root.ensureLineBreak();
  for (const BI_StrokeText* obj : mStrokeTexts) {
    root.ensureLineBreak();
    obj->getText().serialize(root.appendList("stroke_text"));
  }
  root.ensureLineBreak();
}

/*******************************************************************************
 *  Inherited from AttributeProvider
 ******************************************************************************/

QString BI_Device::getUserDefinedAttributeValue(const QString& key) const
    noexcept {
  if (std::shared_ptr<const Attribute> attr = mAttributes.find(key)) {
    return attr->getValueTr(true);
  } else {
    return QString();
  }
}

QString BI_Device::getBuiltInAttributeValue(const QString& key) const noexcept {
  if (key == QLatin1String("DEVICE")) {
    return *mLibDevice->getNames().value(getLocaleOrder());
  } else if (key == QLatin1String("PACKAGE")) {
    return *mLibPackage->getNames().value(getLocaleOrder());
  } else if (key == QLatin1String("FOOTPRINT")) {
    return *mLibFootprint->getNames().value(getLocaleOrder());
  } else {
    return QString();
  }
}

QVector<const AttributeProvider*> BI_Device::getAttributeProviderParents() const
    noexcept {
  return QVector<const AttributeProvider*>{&mBoard, &mCompInstance};
}

/*******************************************************************************
 *  Inherited from BI_Base
 ******************************************************************************/

QPainterPath BI_Device::getGrabAreaScenePx() const noexcept {
  return mGraphicsItem->sceneTransform().map(mGraphicsItem->shape());
}

bool BI_Device::isSelectable() const noexcept {
  return mGraphicsItem->isSelectable();
}

void BI_Device::setSelected(bool selected) noexcept {
  BI_Base::setSelected(selected);
  mGraphicsItem->setSelected(selected);
  foreach (BI_FootprintPad* pad, mPads)
    pad->setSelected(selected);
  foreach (BI_StrokeText* text, mStrokeTexts)
    text->setSelected(selected);
}

/*******************************************************************************
 *  Private Methods
 ******************************************************************************/

bool BI_Device::checkAttributesValidity() const noexcept {
  if (mLibDevice == nullptr) return false;
  if (mLibPackage == nullptr) return false;
  return true;
}

void BI_Device::updateGraphicsItemTransform() noexcept {
  QTransform t;
  if (mMirrored) t.scale(qreal(-1), qreal(1));
  t.rotate(-mRotation.toDeg());
  mGraphicsItem->setTransform(t);
}

const QStringList& BI_Device::getLocaleOrder() const noexcept {
  return getProject().getSettings().getLocaleOrder();
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace librepcb
