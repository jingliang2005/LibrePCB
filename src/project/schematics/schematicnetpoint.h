/*
 * EDA4U - Professional EDA for everyone!
 * Copyright (C) 2013 Urban Bruhin
 * http://eda4u.ubruhin.ch/
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

#ifndef PROJECT_SCHEMATICNETPOINT_H
#define PROJECT_SCHEMATICNETPOINT_H

/*****************************************************************************************
 *  Includes
 ****************************************************************************************/

#include <QtCore>
#include <QtWidgets>
#include "../erc/if_ercmsgprovider.h"
#include "../../common/file_io/if_xmlserializableobject.h"
#include "../../common/cadscene.h"
#include "../../common/units/all_length_units.h"
#include "../../common/exceptions.h"

/*****************************************************************************************
 *  Forward Declarations
 ****************************************************************************************/

class SchematicLayer;

namespace project {
class Circuit;
class Schematic;
class NetSignal;
class SchematicNetLine;
class SchematicNetPoint;
class SymbolInstance;
class SymbolPinInstance;
class ErcMsg;
}

/*****************************************************************************************
 *  Class SchematicNetPointGraphicsItem
 ****************************************************************************************/

namespace project {

/**
 * @brief The SchematicNetPointGraphicsItem class
 */
class SchematicNetPointGraphicsItem final : public QGraphicsItem
{
    public:

        // Types

        /// to make  qgraphicsitem_cast() working
        enum {Type = CADScene::Type_SchematicNetPoint};

        // Constructors / Destructor
        explicit SchematicNetPointGraphicsItem(Schematic& schematic,
                                               SchematicNetPoint& point) noexcept;

        ~SchematicNetPointGraphicsItem() noexcept;

        // Getters
        SchematicNetPoint& getNetPoint() const {return mPoint;}

        // Inherited from QGraphicsItem
        int type() const {return Type;} ///< to make  qgraphicsitem_cast() working
        QRectF boundingRect() const {return sBoundingRect;}
        void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

        // General Methods
        void updateCacheAndRepaint() noexcept;

    private:

        // make some methods inaccessible...
        SchematicNetPointGraphicsItem() = delete;
        SchematicNetPointGraphicsItem(const SchematicNetPointGraphicsItem& other) = delete;
        SchematicNetPointGraphicsItem& operator=(const SchematicNetPointGraphicsItem& rhs) = delete;

        // Attributes
        Schematic& mSchematic;
        SchematicNetPoint& mPoint;
        SchematicLayer* mLayer;

        // Cached Attributes
        bool mPointVisible;

        // Static Stuff
        static QRectF sBoundingRect;
};

} // namespace project

/*****************************************************************************************
 *  Class SchematicNetPoint
 ****************************************************************************************/

namespace project {

/**
 * @brief The SchematicNetPoint class
 */
class SchematicNetPoint final : public QObject, public IF_ErcMsgProvider,
                                public IF_XmlSerializableObject
{
        Q_OBJECT
        DECLARE_ERC_MSG_CLASS_NAME(SchematicNetPoint)

    public:

        // Constructors / Destructor
        explicit SchematicNetPoint(Schematic& schematic, const XmlDomElement& domElement) throw (Exception);
        explicit SchematicNetPoint(Schematic& schematic, NetSignal& netsignal, const Point& position) throw (Exception);
        explicit SchematicNetPoint(Schematic& schematic, SymbolInstance& symbol, const QUuid& pin) throw (Exception);
        ~SchematicNetPoint() noexcept;

        // Getters
        const QUuid& getUuid() const noexcept {return mUuid;}
        bool isAttached() const noexcept {return mAttached;}
        const Point& getPosition() const noexcept {return mPosition;}
        NetSignal* getNetSignal() const noexcept {return mNetSignal;}
        SymbolInstance* getSymbolInstance() const noexcept {return mSymbolInstance;}
        SymbolPinInstance* getPinInstance() const noexcept {return mPinInstance;}
        const QList<SchematicNetLine*>& getLines() const noexcept {return mLines;}

        // Setters

        /**
         * @brief Change the netsignal of this netpoint
         *
         * @warning - This method must always be called from inside an UndoCommand!
         *          - This method must be called also on attached netpoints
         *
         * @param netsignal     A reference to the new netsignal
         *
         * @throw Exception     This method throws an exception in case of an error
         */
        void setNetSignal(NetSignal& netsignal) throw (Exception);
        void setPosition(const Point& position) noexcept;

        // General Methods
        void detachFromPin() throw (Exception);
        void attachToPin(SymbolInstance* symbol, SymbolPinInstance* pin) throw (Exception);
        void updateLines() const noexcept;
        void registerNetLine(SchematicNetLine* netline) noexcept;
        void unregisterNetLine(SchematicNetLine* netline) noexcept;
        void addToSchematic() throw (Exception);
        void removeFromSchematic() throw (Exception);
        XmlDomElement* serializeToXmlDomElement() const throw (Exception);

        // Static Methods
        static const Length& getCircleRadius() noexcept {return sCircleRadius;}
        static uint extractFromGraphicsItems(const QList<QGraphicsItem*>& items,
                                             QList<SchematicNetPoint*>& netpoints,
                                             bool floatingPoints = true,
                                             bool attachedPoints = true,
                                             bool floatingPointsFromFloatingLines = false,
                                             bool attachedPointsFromFloatingLines = false,
                                             bool floatingPointsFromAttachedLines = false,
                                             bool attachedPointsFromAttachedLines = false,
                                             bool attachedPointsFromSymbols = false) noexcept;


    private:

        // make some methods inaccessible...
        SchematicNetPoint();
        SchematicNetPoint(const SchematicNetPoint& other);
        SchematicNetPoint& operator=(const SchematicNetPoint& rhs);

        // Private Methods
        void init() throw (Exception);
        bool checkAttributesValidity() const noexcept;


        // General
        Circuit& mCircuit;
        Schematic& mSchematic;
        SchematicNetPointGraphicsItem* mGraphicsItem;

        // Attributes
        QUuid mUuid;
        bool mAttached;
        Point mPosition;
        NetSignal* mNetSignal;
        SymbolInstance* mSymbolInstance;    ///< only needed if mAttached == true
        SymbolPinInstance* mPinInstance;    ///< only needed if mAttached == true

        // Misc
        QList<SchematicNetLine*> mLines;    ///< all registered netlines

        /// @brief The ERC message for dead netpoints
        QScopedPointer<ErcMsg> mErcMsgDeadNetPoint;


        // Static Members
        static const Length sCircleRadius;
};

} // namespace project

#endif // PROJECT_SCHEMATICNETPOINT_H
