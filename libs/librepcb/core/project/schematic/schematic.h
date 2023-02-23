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

#ifndef LIBREPCB_CORE_SCHEMATIC_H
#define LIBREPCB_CORE_SCHEMATIC_H

/*******************************************************************************
 *  Includes
 ******************************************************************************/
#include "../../attribute/attributeprovider.h"
#include "../../fileio/filepath.h"
#include "../../fileio/transactionaldirectory.h"
#include "../../types/elementname.h"
#include "../../types/lengthunit.h"
#include "../../types/uuid.h"

#include <QtCore>
#include <QtWidgets>

#include <memory>

/*******************************************************************************
 *  Namespace / Forward Declarations
 ******************************************************************************/
namespace librepcb {

class ComponentInstance;
class GraphicsScene;
class NetSignal;
class Point;
class Project;
class SI_Base;
class SI_NetLabel;
class SI_NetLine;
class SI_NetPoint;
class SI_NetSegment;
class SI_Polygon;
class SI_Symbol;
class SI_SymbolPin;
class SI_Text;
class SchematicSelectionQuery;

/*******************************************************************************
 *  Class Schematic
 ******************************************************************************/

/**
 * @brief The Schematic class represents one schematic page of a project and is
 * always part of a circuit
 *
 * A schematic can contain following items (see ::librepcb::SI_Base and
 * ::librepcb::SGI_Base):
 *  - netsegment:       ::librepcb::SI_NetSegment
 *      - netpoint:     ::librepcb::SI_NetPoint
 *      - netline:      ::librepcb::SI_NetLine
 *      - netlabel:     ::librepcb::SI_NetLabel
 *  - symbol:           ::librepcb::SI_Symbol
 *      - symbol pin:   ::librepcb::SI_SymbolPin
 *  - polygon:          ::librepcb::SI_Polygon
 *  - circle:           TODO
 *  - text:             ::librepcb::SI_Text
 */
class Schematic final : public QObject, public AttributeProvider {
  Q_OBJECT

public:
  // Types

  /**
   * @brief Z Values of all items in a schematic scene (to define the stacking
   * order)
   *
   * These values are used for QGraphicsItem::setZValue() to define the stacking
   * order of all items in a schematic QGraphicsScene. We use integer values,
   * even if the z-value of QGraphicsItem is a qreal attribute...
   *
   * Low number = background, high number = foreground
   */
  enum ItemZValue {
    ZValue_Default = 0,  ///< this is the default value (behind all other items)
    ZValue_TextAnchors,  ///< Z value for ::librepcb::SI_Text anchor lines
    ZValue_Symbols,  ///< Z value for ::librepcb::SI_Symbol items
    ZValue_SymbolPins,  ///< Z value for ::librepcb::SI_SymbolPin items
    ZValue_Polygons,  ///< Z value for ::librepcb::SI_Polygon items
    ZValue_Texts,  ///< Z value for ::librepcb::SI_Text items
    ZValue_NetLabels,  ///< Z value for ::librepcb::SI_NetLabel items
    ZValue_NetLines,  ///< Z value for ::librepcb::SI_NetLine items
    ZValue_HiddenNetPoints,  ///< Z value for hidden
                             ///< ::librepcb::SI_NetPoint items
    ZValue_VisibleNetPoints,  ///< Z value for visible
                              ///< ::librepcb::SI_NetPoint items
  };

  // Constructors / Destructor
  Schematic() = delete;
  Schematic(const Schematic& other) = delete;
  Schematic(Project& project, std::unique_ptr<TransactionalDirectory> directory,
            const QString& directoryName, const Uuid& uuid,
            const ElementName& name);
  ~Schematic() noexcept;

  // Getters: General
  Project& getProject() const noexcept { return mProject; }
  const QString& getDirectoryName() const noexcept { return mDirectoryName; }
  TransactionalDirectory& getDirectory() noexcept { return *mDirectory; }
  GraphicsScene& getGraphicsScene() const noexcept { return *mGraphicsScene; }
  bool isEmpty() const noexcept;

  // Getters: Attributes
  const Uuid& getUuid() const noexcept { return mUuid; }
  const ElementName& getName() const noexcept { return mName; }
  const PositiveLength& getGridInterval() const noexcept {
    return mGridInterval;
  }
  const LengthUnit& getGridUnit() const noexcept { return mGridUnit; }

  // Setters: Attributes
  void setName(const ElementName& name) noexcept;
  void setGridInterval(const PositiveLength& interval) noexcept {
    mGridInterval = interval;
  }
  void setGridUnit(const LengthUnit& unit) noexcept { mGridUnit = unit; }

  // Symbol Methods
  const QMap<Uuid, SI_Symbol*>& getSymbols() const noexcept { return mSymbols; }
  void addSymbol(SI_Symbol& symbol);
  void removeSymbol(SI_Symbol& symbol);

  // NetSegment Methods
  const QMap<Uuid, SI_NetSegment*>& getNetSegments() const noexcept {
    return mNetSegments;
  }
  void addNetSegment(SI_NetSegment& netsegment);
  void removeNetSegment(SI_NetSegment& netsegment);

  // Polygon Methods
  const QMap<Uuid, SI_Polygon*>& getPolygons() const noexcept {
    return mPolygons;
  }
  void addPolygon(SI_Polygon& polygon);
  void removePolygon(SI_Polygon& polygon);

  // Text Methods
  const QMap<Uuid, SI_Text*>& getTexts() const noexcept { return mTexts; }
  void addText(SI_Text& text);
  void removeText(SI_Text& text);

  // General Methods
  void addToProject();
  void removeFromProject();
  void save();
  void saveViewSceneRect(const QRectF& rect) noexcept { mViewRect = rect; }
  const QRectF& restoreViewSceneRect() const noexcept { return mViewRect; }
  void selectAll() noexcept;
  void setSelectionRect(const Point& p1, const Point& p2,
                        bool updateItems) noexcept;
  void clearSelection() const noexcept;
  void updateAllNetLabelAnchors() noexcept;
  std::unique_ptr<SchematicSelectionQuery> createSelectionQuery() const
      noexcept;

  // Inherited from AttributeProvider
  /// @copydoc ::librepcb::AttributeProvider::getBuiltInAttributeValue()
  QString getBuiltInAttributeValue(const QString& key) const noexcept override;
  /// @copydoc ::librepcb::AttributeProvider::getAttributeProviderParents()
  QVector<const AttributeProvider*> getAttributeProviderParents() const
      noexcept override;

  // Operator Overloadings
  Schematic& operator=(const Schematic& rhs) = delete;
  bool operator==(const Schematic& rhs) noexcept { return (this == &rhs); }
  bool operator!=(const Schematic& rhs) noexcept { return (this != &rhs); }

signals:
  void symbolAdded(SI_Symbol& symbol);
  void symbolRemoved(SI_Symbol& symbol);

  /// @copydoc AttributeProvider::attributesChanged()
  void attributesChanged() override;

private:
  // General
  Project& mProject;  ///< A reference to the Project object (from the ctor)
  const QString mDirectoryName;
  std::unique_ptr<TransactionalDirectory> mDirectory;
  bool mIsAddedToProject;

  QScopedPointer<GraphicsScene> mGraphicsScene;
  QRectF mViewRect;

  // Attributes
  Uuid mUuid;
  ElementName mName;
  PositiveLength mGridInterval;
  LengthUnit mGridUnit;

  QMap<Uuid, SI_Symbol*> mSymbols;
  QMap<Uuid, SI_NetSegment*> mNetSegments;
  QMap<Uuid, SI_Polygon*> mPolygons;
  QMap<Uuid, SI_Text*> mTexts;
};

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace librepcb

#endif
