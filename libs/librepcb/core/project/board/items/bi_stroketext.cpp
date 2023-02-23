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
#include "bi_stroketext.h"

#include "../../../attribute/attributesubstitutor.h"
#include "../../../font/strokefontpool.h"
#include "../../../geometry/stroketext.h"
#include "../../../graphics/graphicsscene.h"
#include "../../../graphics/linegraphicsitem.h"
#include "../../../graphics/stroketextgraphicsitem.h"
#include "../../project.h"
#include "../board.h"
#include "../boardlayerstack.h"
#include "bi_device.h"

#include <QtCore>

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

BI_StrokeText::BI_StrokeText(Board& board, const StrokeText& text)
  : BI_Base(board),
    mDevice(nullptr),
    mText(new StrokeText(text)),
    mGraphicsItem(
        new StrokeTextGraphicsItem(*mText, mBoard.getLayerStack(), getFont())),
    mAnchorGraphicsItem(new LineGraphicsItem()),
    mOnStrokeTextEditedSlot(*this, &BI_StrokeText::strokeTextEdited) {
  mText->onEdited.attach(mOnStrokeTextEditedSlot);

  mGraphicsItem->setAttributeProvider(getAttributeProvider());
  updateGraphicsItems();

  // connect to the "attributes changed" signal of the board
  connect(&mBoard, &Board::attributesChanged, this,
          &BI_StrokeText::boardOrDeviceAttributesChanged);
}

BI_StrokeText::~BI_StrokeText() noexcept {
}

/*******************************************************************************
 *  General Methods
 ******************************************************************************/

void BI_StrokeText::setDevice(BI_Device* device) noexcept {
  if (mDevice) {
    disconnect(mDevice, &BI_Device::attributesChanged, this,
               &BI_StrokeText::boardOrDeviceAttributesChanged);
  }

  mDevice = device;
  mGraphicsItem->setAttributeProvider(getAttributeProvider());
  updateGraphicsItems();

  // Text might need to be updated if device attributes have changed.
  if (mDevice) {
    connect(mDevice, &BI_Device::attributesChanged, this,
            &BI_StrokeText::boardOrDeviceAttributesChanged);
  }
}

const AttributeProvider* BI_StrokeText::getAttributeProvider() const noexcept {
  if (mDevice) {
    return mDevice;
  } else {
    return &mBoard;
  }
}

QVector<Path> BI_StrokeText::generatePaths() const {
  const QString text = AttributeSubstitutor::substitute(mText->getText(),
                                                        getAttributeProvider());
  return mText->generatePaths(getFont(), text);
}

void BI_StrokeText::updateGraphicsItems() noexcept {
  // update z-value
  Board::ItemZValue zValue = Board::ZValue_Texts;
  if (GraphicsLayer::isTopLayer(*mText->getLayerName())) {
    zValue = Board::ZValue_TextsTop;
  } else if (GraphicsLayer::isBottomLayer(*mText->getLayerName())) {
    zValue = Board::ZValue_TextsBottom;
  }
  mGraphicsItem->setZValue(static_cast<qreal>(zValue));
  mAnchorGraphicsItem->setZValue(static_cast<qreal>(zValue));

  // show anchor line only if there is a footprint and the text is selected
  if (mDevice && isSelected()) {
    mAnchorGraphicsItem->setLine(mText->getPosition(), mDevice->getPosition());
    mAnchorGraphicsItem->setLayer(
        mBoard.getLayerStack().getLayer(*mText->getLayerName()));
  } else {
    mAnchorGraphicsItem->setLayer(nullptr);
  }
}

void BI_StrokeText::addToBoard() {
  if (isAddedToBoard()) {
    throw LogicError(__FILE__, __LINE__);
  }
  BI_Base::addToBoard(mGraphicsItem.data());
  mBoard.getGraphicsScene().addItem(*mAnchorGraphicsItem);
}

void BI_StrokeText::removeFromBoard() {
  if (!isAddedToBoard()) {
    throw LogicError(__FILE__, __LINE__);
  }
  BI_Base::removeFromBoard(mGraphicsItem.data());
  mBoard.getGraphicsScene().removeItem(*mAnchorGraphicsItem);
}

/*******************************************************************************
 *  Inherited from BI_Base
 ******************************************************************************/

const Point& BI_StrokeText::getPosition() const noexcept {
  return mText->getPosition();
}

const StrokeFont& BI_StrokeText::getFont() const {
  return mBoard.getProject().getStrokeFonts().getFont(
      mBoard.getDefaultFontName());  // can throw
}

QPainterPath BI_StrokeText::getGrabAreaScenePx() const noexcept {
  return mGraphicsItem->sceneTransform().map(mGraphicsItem->shape());
}

const Uuid& BI_StrokeText::getUuid() const noexcept {
  return mText->getUuid();
}

bool BI_StrokeText::isSelectable() const noexcept {
  const GraphicsLayer* layer =
      mBoard.getLayerStack().getLayer(*mText->getLayerName());
  return layer && layer->isVisible();
}

void BI_StrokeText::setSelected(bool selected) noexcept {
  BI_Base::setSelected(selected);
  mGraphicsItem->setSelected(selected);
  updateGraphicsItems();
}

/*******************************************************************************
 *  Private Slots
 ******************************************************************************/

void BI_StrokeText::boardOrDeviceAttributesChanged() {
  mGraphicsItem->updateText();
}

/*******************************************************************************
 *  Private Methods
 ******************************************************************************/

void BI_StrokeText::strokeTextEdited(const StrokeText& text,
                                     StrokeText::Event event) noexcept {
  Q_UNUSED(text);
  switch (event) {
    case StrokeText::Event::LayerNameChanged:
    case StrokeText::Event::PositionChanged:
      updateGraphicsItems();
      break;
    default:
      break;
  }
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace librepcb
