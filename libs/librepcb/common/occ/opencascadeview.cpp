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
#include <QMenu>
#include <QMouseEvent>
#include <QRubberBand>
#include <QStyleFactory>

#include <OpenGl_GraphicDriver.hxx>

#include "opencascadeview.h"

#include <V3d_View.hxx>

#include <Aspect_Handle.hxx>
#include <Aspect_DisplayConnection.hxx>

#ifdef WNT
    #include <WNT_Window.hxx>
#elif defined(__APPLE__) && !defined(MACOSX_USE_GLX)
    #include <Cocoa_Window.hxx>
#else
    #undef Bool
    #undef CursorShape
    #undef None
    #undef KeyPress
    #undef KeyRelease
    #undef FocusIn
    #undef FocusOut
    #undef FontChange
    #undef Expose
    #include <Xw_Window.hxx>
#endif

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {

static Handle(Graphic3d_GraphicDriver) & GetGraphicDriver() {
  static Handle(Graphic3d_GraphicDriver) aGraphicDriver;
  return aGraphicDriver;
}

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

OpenCascadeView::OpenCascadeView(QWidget* parent) noexcept
  : QGLWidget(parent),
    myXmin(0),
    myYmin(0),
    myXmax(0),
    myYmax(0),
    myCurrentMode(CurAction3d_DynamicRotation),
    myDegenerateModeIsOn(Standard_True),
    myRectBand(nullptr) {
  // No Background
  setBackgroundRole(QPalette::NoRole);

  // Enable the mouse tracking, by default the mouse tracking is disabled.
  setMouseTracking(true);
}

OpenCascadeView::~OpenCascadeView() noexcept {
}

/*******************************************************************************
 *  General Methods
 ******************************************************************************/

void OpenCascadeView::init() {
  // Create Aspect_DisplayConnection
  Handle(Aspect_DisplayConnection) aDisplayConnection =
      new Aspect_DisplayConnection();

  // Get graphic driver if it exists, otherwise initialise it
  if (GetGraphicDriver().IsNull()) {
    GetGraphicDriver() = new OpenGl_GraphicDriver(aDisplayConnection);
  }

  // Get window handle. This returns something suitable for all platforms.
  WId window_handle = static_cast<WId>(winId());

// Create appropriate window for platform
#ifdef WNT
  Handle(WNT_Window) wind =
      new WNT_Window(reinterpret_cast<Aspect_Handle>(window_handle));
#elif defined(__APPLE__) && !defined(MACOSX_USE_GLX)
  Handle(Cocoa_Window) wind = new Cocoa_Window((NSView*)window_handle);
#else
  Handle(Xw_Window) wind =
      new Xw_Window(aDisplayConnection, (Window)window_handle);
#endif

  // Create V3dViewer and V3d_View
  myViewer = new V3d_Viewer(GetGraphicDriver(), Standard_ExtString("viewer3d"));

  myView = myViewer->CreateView();

  myView->SetWindow(wind);
  if (!wind->IsMapped()) wind->Map();

  // Create AISInteractiveContext
  myContext = new AIS_InteractiveContext(myViewer);

  // Set up lights etc
  myViewer->SetDefaultLights();
  myViewer->SetLightOn();

  myView->SetBackgroundColor(Quantity_NOC_BLACK);
  myView->MustBeResized();
  myView->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_GOLD, 0.08,
                          V3d_ZBUFFER);

  myContext->SetDisplayMode(AIS_Shaded, Standard_True);
}

const Handle(AIS_InteractiveContext) & OpenCascadeView::getContext() const {
  return myContext;
}

void OpenCascadeView::paintEvent(QPaintEvent* theEvent) {
  if (myContext.IsNull()) {
    init();
  }

  myView->Redraw();
}

void OpenCascadeView::resizeEvent(QResizeEvent* theEvent) {
  if (!myView.IsNull()) {
    myView->MustBeResized();
  }
}

void OpenCascadeView::fitAll(void) {
  myView->FitAll();
  myView->ZFitAll();
  myView->Redraw();
}

void OpenCascadeView::reset(void) {
  myView->Reset();
}

void OpenCascadeView::pan(void) {
  myCurrentMode = CurAction3d_DynamicPanning;
}

void OpenCascadeView::zoom(void) {
  myCurrentMode = CurAction3d_DynamicZooming;
}

void OpenCascadeView::rotate(void) {
  myCurrentMode = CurAction3d_DynamicRotation;
}

void OpenCascadeView::mousePressEvent(QMouseEvent* theEvent) {
  if (theEvent->button() == Qt::LeftButton) {
    onLButtonDown((theEvent->buttons() | theEvent->modifiers()),
                  theEvent->pos());
  } else if (theEvent->button() == Qt::MidButton) {
    onMButtonDown((theEvent->buttons() | theEvent->modifiers()),
                  theEvent->pos());
  } else if (theEvent->button() == Qt::RightButton) {
    onRButtonDown((theEvent->buttons() | theEvent->modifiers()),
                  theEvent->pos());
  }
}

void OpenCascadeView::mouseReleaseEvent(QMouseEvent* theEvent) {
  if (theEvent->button() == Qt::LeftButton) {
    onLButtonUp(theEvent->buttons() | theEvent->modifiers(), theEvent->pos());
  } else if (theEvent->button() == Qt::MidButton) {
    onMButtonUp(theEvent->buttons() | theEvent->modifiers(), theEvent->pos());
  } else if (theEvent->button() == Qt::RightButton) {
    onRButtonUp(theEvent->buttons() | theEvent->modifiers(), theEvent->pos());
  }
}

void OpenCascadeView::mouseMoveEvent(QMouseEvent* theEvent) {
  onMouseMove(theEvent->buttons(), theEvent->pos());
}

void OpenCascadeView::wheelEvent(QWheelEvent* theEvent) {
  onMouseWheel(theEvent->buttons(), theEvent->delta(), theEvent->pos());
}

void OpenCascadeView::onLButtonDown(const int theFlags, const QPoint thePoint) {
  // Save the current mouse coordinate in min.
  myXmin = thePoint.x();
  myYmin = thePoint.y();
  myXmax = thePoint.x();
  myYmax = thePoint.y();

  if (myCurrentMode == CurAction3d_DynamicRotation) {
    myView->StartRotation(thePoint.x(), thePoint.y());
  }
}

void OpenCascadeView::onMButtonDown(const int theFlags, const QPoint thePoint) {
  // Save the current mouse coordinate in min.
  myXmin = thePoint.x();
  myYmin = thePoint.y();
  myXmax = thePoint.x();
  myYmax = thePoint.y();
}

void OpenCascadeView::onRButtonDown(const int theFlags, const QPoint thePoint) {
}

void OpenCascadeView::onMouseWheel(const int theFlags, const int theDelta,
                                   const QPoint thePoint) {
  Standard_Integer aFactor = 16;

  Standard_Integer aX = thePoint.x();
  Standard_Integer aY = thePoint.y();

  if (theDelta > 0) {
    aX += aFactor;
    aY += aFactor;
  } else {
    aX -= aFactor;
    aY -= aFactor;
  }

  myView->Zoom(thePoint.x(), thePoint.y(), aX, aY);
}

void OpenCascadeView::addItemInPopup(QMenu* theMenu) {
}

void OpenCascadeView::popup(const int x, const int y) {
}

void OpenCascadeView::onLButtonUp(const int theFlags, const QPoint thePoint) {
  // Hide the QRubberBand
  if (myRectBand) {
    myRectBand->hide();
  }

  // Ctrl for multi selection.
  if (thePoint.x() == myXmin && thePoint.y() == myYmin) {
    if (theFlags & Qt::ControlModifier) {
      multiInputEvent(thePoint.x(), thePoint.y());
    } else {
      inputEvent(thePoint.x(), thePoint.y());
    }
  }
}

void OpenCascadeView::onMButtonUp(const int theFlags, const QPoint thePoint) {
  if (thePoint.x() == myXmin && thePoint.y() == myYmin) {
    panByMiddleButton(thePoint);
  }
}

void OpenCascadeView::onRButtonUp(const int theFlags, const QPoint thePoint) {
  popup(thePoint.x(), thePoint.y());
}

void OpenCascadeView::onMouseMove(const int theFlags, const QPoint thePoint) {
  // Draw the rubber band.
  // drawRubberBand(myXmin, myYmin, thePoint.x(), thePoint.y());
  //
  // dragEvent(thePoint.x(), thePoint.y());

  // Ctrl for multi selection.
  // if (theFlags & Qt::ControlModifier) {
  //  multiMoveEvent(thePoint.x(), thePoint.y());
  //} else {
  //  moveEvent(thePoint.x(), thePoint.y());
  //}

  if (theFlags & Qt::LeftButton) {
    myView->Rotation(thePoint.x(), thePoint.y());
  } else if (theFlags & Qt::MidButton) {
    myView->Pan(thePoint.x() - myXmax, myYmax - thePoint.y());
    myXmax = thePoint.x();
    myYmax = thePoint.y();
  }
}

void OpenCascadeView::dragEvent(const int x, const int y) {
  myContext->Select(myXmin, myYmin, x, y, myView, Standard_True);

  emit selectionChanged();
}

void OpenCascadeView::multiDragEvent(const int x, const int y) {
  myContext->ShiftSelect(myXmin, myYmin, x, y, myView, Standard_True);

  emit selectionChanged();
}

void OpenCascadeView::inputEvent(const int x, const int y) {
  Q_UNUSED(x);
  Q_UNUSED(y);

  myContext->Select(Standard_True);

  emit selectionChanged();
}

void OpenCascadeView::multiInputEvent(const int x, const int y) {
  Q_UNUSED(x);
  Q_UNUSED(y);

  myContext->ShiftSelect(Standard_True);

  emit selectionChanged();
}

void OpenCascadeView::moveEvent(const int x, const int y) {
  myContext->MoveTo(x, y, myView, Standard_True);
}

void OpenCascadeView::multiMoveEvent(const int x, const int y) {
  myContext->MoveTo(x, y, myView, Standard_True);
}

void OpenCascadeView::drawRubberBand(const int minX, const int minY,
                                     const int maxX, const int maxY) {
  QRect aRect;

  // Set the rectangle correctly.
  (minX < maxX) ? (aRect.setX(minX)) : (aRect.setX(maxX));
  (minY < maxY) ? (aRect.setY(minY)) : (aRect.setY(maxY));

  aRect.setWidth(abs(maxX - minX));
  aRect.setHeight(abs(maxY - minY));

  if (!myRectBand) {
    myRectBand = new QRubberBand(QRubberBand::Rectangle, this);

    // setStyle is important, set to windows style will just draw
    // rectangle frame, otherwise will draw a solid rectangle.
    myRectBand->setStyle(QStyleFactory::create("windows"));
  }

  myRectBand->setGeometry(aRect);
  myRectBand->show();
}

void OpenCascadeView::panByMiddleButton(const QPoint& thePoint) {
  Standard_Integer aCenterX = 0;
  Standard_Integer aCenterY = 0;

  QSize aSize = size();

  aCenterX = aSize.width() / 2;
  aCenterY = aSize.height() / 2;

  myView->Pan(aCenterX - thePoint.x(), thePoint.y() - aCenterY);
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace librepcb
