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

#ifndef LIBREPCB_PROJECT_BOARDSHAPE3D_H
#define LIBREPCB_PROJECT_BOARDSHAPE3D_H

/*******************************************************************************
 *  Includes
 ******************************************************************************/
#include <QtCore>

/*******************************************************************************
 *  Namespace / Forward Declarations
 ******************************************************************************/
namespace librepcb {

class OpenCascadeView;

namespace project {

class Board;

/*******************************************************************************
 *  Class BoardShape3D
 ******************************************************************************/

/**
 * @brief The BoardShape3D class
 */
class BoardShape3D final {
  // Q_OBJECT

public:
  // Constructors / Destructor
  BoardShape3D() = delete;
  BoardShape3D(const BoardShape3D& other) = delete;
  explicit BoardShape3D(Board& board);
  ~BoardShape3D() noexcept;

  // General Methods
  void addToView(OpenCascadeView& view) noexcept;

  // Operator Overloadings
  BoardShape3D& operator=(const BoardShape3D& rhs) = delete;

private:
  Board& mBoard;
};

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace project
}  // namespace librepcb

#endif  // LIBREPCB_PROJECT_BOARDSHAPE3D_H
