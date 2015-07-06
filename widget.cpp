#include "widget.h"
#include "curve.h"
#include "page.h"
//#include "tictoc.h"
#include "document.h"
#include "commands.h"
#include "tabletapplication.h"

#include <QMouseEvent>
#include <QFileDialog>
#include <QPdfWriter>
#include <QPixmap>
#include <QBitmap>
#include <QUndoCommand>
#include <QScrollArea>
#include <QScrollBar>

#define PAGE_GAP 10.0
#define ZOOM_STEP 1.2

#include <QPainter>
#include <QRectF>

Widget::Widget(QWidget *parent) : QWidget(parent)
{
    currentState = state::IDLE;

    // setup cursors
    QPixmap penCursorBitmap = QPixmap(":/images/penCursor3.png");
    QPixmap penCursorMask  = QPixmap(":/images/penCursor3Mask.png");
    penCursorBitmap.setMask(QBitmap(penCursorMask));
    penCursor = QCursor(penCursorBitmap, -1, -1);

    QPixmap eraserCursorBitmap = QPixmap(":/images/eraserCursor.png");
    QPixmap eraserCursorMask  = QPixmap(":/images/eraserCursorMask.png");
    eraserCursorBitmap.setMask(QBitmap(eraserCursorMask));
    eraserCursor = QCursor(eraserCursorBitmap, -1, -1);

    currentTool = tool::PEN;
    previousTool = tool::PEN;
    setCursor(penCursorBitmap);

    currentDocument = new Document();

    currentPenWidth = 1.41;
    currentColor = QColor(0,0,0);
    zoom = 1;

    currentCOSPos.setX(0.0);
    currentCOSPos.setY(0.0);
    updateAllPageBuffers();
    setGeometry(getWidgetGeometry());

    parent->updateGeometry();
    parent->update();
}

void Widget::updateAllPageBuffers()
{
    pageBuffer.clear();
    for (int buffNum = 0; buffNum < currentDocument->pages.size(); ++buffNum)
    {
        updateBuffer(buffNum);
    }
}

void Widget::updateBuffer(int buffNum)
{
    Page page = currentDocument->pages.at(buffNum);
    int pixelWidth = zoom * page.getWidth();
    int pixelHeight = zoom * page.getHeight();
    QImage image(pixelWidth, pixelHeight, QImage::Format_ARGB8565_Premultiplied);

    image.fill(page.backgroundColor);

    QPainter painter;
    painter.begin(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

//    currentDocument->paintPage(buffNum, painter, zoom);
    currentDocument->pages[buffNum].paint(painter, zoom);

    painter.end();

    if (pageBuffer.length() <= buffNum)
    {
        pageBuffer.append(image);
    } else {
        pageBuffer.replace(buffNum, image);
    }
}

void Widget::updateBufferRegion(int buffNum, QRectF clipRect)
{
    QPainter painter;
    painter.begin(&pageBuffer[buffNum]);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(clipRect);
    painter.setClipping(true);

    painter.fillRect(clipRect, currentDocument->pages.at(buffNum).backgroundColor);
//    painter.fillRect(clipRect, QColor("black"));

//    currentDocument->paintPage(buffNum, painter, zoom);

    QRectF paintRect = QRectF(clipRect.topLeft() / zoom, clipRect.bottomRight() / zoom);
    currentDocument->pages[buffNum].paint(painter, zoom, paintRect);

    painter.end();
}

void Widget::drawOnBuffer(QPointF from, QPointF to, qreal pressure)
{
    QPen pen;

    QPainter painter;
    painter.begin(&pageBuffer[drawingOnPage]);
    painter.setRenderHint(QPainter::Antialiasing, true);

    pen.setWidthF(zoom * currentPenWidth * pressure);
    pen.setColor(currentColor);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(zoom * from, zoom * to);

    painter.end();
}


QRect Widget::getWidgetGeometry()
{
    int width = 0;
    int height = 0;
    for (int i = 0; i < pageBuffer.size(); ++i)
    {
        height += pageBuffer[i].height() + PAGE_GAP;
        if (pageBuffer[i].width() > width)
            width = pageBuffer[i].width();
    }
    height -= PAGE_GAP;
    return QRect(0, 0, width, height);
}

void Widget::paintEvent(QPaintEvent *)
{
    QRect widgetGeometry = getWidgetGeometry();
    resize(widgetGeometry.width(), widgetGeometry.height());
//    tic();
    QPalette p(palette());
//    p.setColor(QPalette::Background, Qt::lightGray);
    setAutoFillBackground(true);
    setPalette(p);

    QPainter painter(this);
//    painter.setRenderHint(QPainter::Antialiasing, true);

//    painter.translate(currentCOSPos);

//    qreal currentYPos = 0.0;

    for (int i = 0; i < pageBuffer.size(); ++i)
    {
        QRectF rectSource;
        rectSource.setTopLeft(QPointF(0.0,0.0));
        rectSource.setWidth(pageBuffer.at(i).width());
        rectSource.setHeight(pageBuffer.at(i).height());

        QRectF rectTarget;
//        rectTarget.setTopLeft(QPointF(0.0, currentYPos));
        rectTarget.setTopLeft(QPointF(0.0, 0.0));
        rectTarget.setWidth(pageBuffer.at(i).width());
        rectTarget.setHeight(pageBuffer.at(i).height());

        painter.drawImage(rectTarget, pageBuffer.at(i), rectSource);

//        if ((selecting || selected) && i == currentSelection.pageNum)
        if ((currentState == state::SELECTING || currentState == state::SELECTED || currentState == state::MODIFYING_SELECTION) && i == currentSelection.pageNum)
        {
            currentSelection.paint(painter, zoom);
        }


//        currentYPos += (rectSource.height() + PAGE_GAP*zoom);
        painter.translate(QPointF(0.0, rectSource.height() + PAGE_GAP));
    }
//    std::cerr << "paint "; toc();
}

void Widget::mouseAndTabletEvent(QPointF mousePos, Qt::MouseButton button, Qt::MouseButtons buttons, QTabletEvent::PointerType pointerType, QEvent::Type eventType, qreal pressure, bool tabletEvent)
{
    int pageNum = getPageFromMousePos(mousePos);
    QPointF pagePos = getPagePosFromMousePos(mousePos, pageNum);

    if (tabletEvent)
    {
        pressure = minWidthMultiplier + pressure * (maxWidthMultiplier - minWidthMultiplier);
    }

    if (eventType == QEvent::MouseButtonRelease)
    {
        setPreviousTool();
    }

    if ((currentState == state::IDLE || currentState == state::SELECTED) && buttons & Qt::MiddleButton && pointerType == QTabletEvent::Pen)
    {
        if (eventType == QEvent::MouseButtonPress && button == Qt::MiddleButton)
        {
            if (currentTool != tool::HAND)
            {
                previousTool = currentTool;
            }
            previousMousePos = mousePos;
            emit hand();
            return;
        }
        if (eventType == QEvent::MouseMove)
        {
            int dx = mousePos.x() - previousMousePos.x();
            int dy = mousePos.y() - previousMousePos.y();

            scrollArea->horizontalScrollBar()->setValue(scrollArea->horizontalScrollBar()->value() - dx);
            scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->value() - dy);

            mousePos -= QPointF(dx,dy);

            previousMousePos = mousePos;
            return;
        }
        return;
    }


    if (currentState == state::SELECTED)
    {
        if (eventType == QEvent::MouseButtonPress)
        {
            if (currentSelection.selectionPolygon.containsPoint(pagePos, Qt::OddEvenFill) && currentSelection.pageNum == pageNum)
            {
                // move selection
                startMovingSelection(mousePos);
                return;
            } else {
                letGoSelection();
                update();
            }
        }
        if (eventType == QEvent::MouseMove)
        {

        }
        if (eventType == QEvent::MouseButtonRelease)
        {
            setPreviousTool();
        }
    }

    if (currentState == Widget::state::IDLE && button == Qt::RightButton)
    {
        previousTool = currentTool;
        startSelecting(mousePos);
        emit select();
        return;
    }


    if (currentState == state::MODIFYING_SELECTION)
    {
        if (eventType == QEvent::MouseMove)
        {
            continueMovingSelection(mousePos);
            return;
        }
        if (eventType == QEvent::MouseButtonRelease)
        {
            currentState = state::SELECTED;
            setPreviousTool();
        }
    }

    if (currentState == state::SELECTING)
    {
        if (eventType == QEvent::MouseButtonPress)
        {

        }
        if (eventType == QEvent::MouseMove)
        {
            continueSelecting(mousePos);
            return;
        }
        if (eventType == QEvent::MouseButtonRelease)
        {
            stopSelecting(mousePos);
            setPreviousTool();
            return;
        }
    }

    if (currentState == state::DRAWING)
    {
        if (eventType == QEvent::MouseButtonPress)
        {

        }
        if (eventType == QEvent::MouseMove)
        {
            continueDrawing(mousePos, pressure);
            return;
        }
        if (eventType == QEvent::MouseButtonRelease)
        {
            stopDrawing(mousePos, pressure);
            setPreviousTool();
            return;
        }
    }

    if (currentState == state::IDLE)
    {
        if (eventType == QEvent::MouseButtonPress)
        {
            if (pointerType == QTabletEvent::Pen)
            {
                if (currentTool == tool::PEN)
                {
                    startDrawing(mousePos, pressure);
                    return;
                }
                if (currentTool == tool::ERASER)
                {
                    erase(mousePos);
                    return;
                }
                if (currentTool == tool::SELECT)
                {
                    startSelecting(mousePos);
                    return;
                }
                if (currentTool == tool::HAND)
                {
                    previousMousePos = mousePos;
                }
            }
            if (pointerType == QTabletEvent::Eraser)
            {
                previousTool = currentTool;
                emit eraser();
                erase(mousePos);
            }
        }
        if (eventType == QEvent::MouseMove)
        {
            if (pointerType == QTabletEvent::Eraser || currentTool == tool::ERASER)
            {
                erase(mousePos);
            }
            if (currentTool == tool::HAND)
            {
                int dx = mousePos.x() - previousMousePos.x();
                int dy = mousePos.y() - previousMousePos.y();

                scrollArea->horizontalScrollBar()->setValue(scrollArea->horizontalScrollBar()->value() - dx);
                scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->value() - dy);

                mousePos -= QPointF(dx,dy);

                previousMousePos = mousePos;
            }
        }
        if (eventType == QEvent::MouseButtonRelease)
        {
            setPreviousTool();
        }
    }
}

void Widget::tabletEvent(QTabletEvent *event)
{
    event->accept();

//    event->setAccepted(true);

    QPointF mousePos = QPointF(event->hiResGlobalX(), event->hiResGlobalY()) - mapToGlobal(QPoint(0,0));
    qreal pressure = event->pressure();

    QEvent::Type eventType;
    if (event->type() == QTabletEvent::TabletPress)
    {
        eventType = QEvent::MouseButtonPress;
    }
    if (event->type() == QTabletEvent::TabletMove)
    {
        eventType = QEvent::MouseMove;
    }
    if (event->type() == QTabletEvent::TabletRelease)
    {
        eventType = QEvent::MouseButtonRelease;
    }

    mouseAndTabletEvent(mousePos, event->button(), event->buttons(), event->pointerType(), eventType, pressure, true);

    penDown = true;
}

void Widget::mousePressEvent(QMouseEvent *event)
{
    bool usingTablet = static_cast<TabletApplication*>(qApp)->isUsingTablet();

    if (!usingTablet)
    {
        if (!penDown)
        {
            QPointF mousePos = event->localPos();
            qreal pressure = 1;

            mouseAndTabletEvent(mousePos, event->button(), event->buttons(), QTabletEvent::Pen, event->type(), pressure, false);
        }
    }
}

void Widget::mouseMoveEvent(QMouseEvent *event)
{
    bool usingTablet = static_cast<TabletApplication*>(qApp)->isUsingTablet();

    if (!usingTablet)
    {
        if (!penDown)
        {
            QPointF mousePos = event->localPos();
            qreal pressure = 1;

            mouseAndTabletEvent(mousePos, event->button(), event->buttons(), QTabletEvent::Pen, event->type(), pressure, false);
        }
    }
}

void Widget::mouseReleaseEvent(QMouseEvent *event)
{
    bool usingTablet = static_cast<TabletApplication*>(qApp)->isUsingTablet();

    if (!usingTablet)
    {
        if (!penDown)
        {
            QPointF mousePos = event->localPos();
            qreal pressure = 1;

            mouseAndTabletEvent(mousePos, event->button(), event->buttons(), QTabletEvent::Pen, event->type(), pressure, false);
        }
    }
    penDown = false;
}

void Widget::setPreviousTool()
{
    if (previousTool == tool::PEN)
    {
        emit pen();
    }
    if (previousTool == tool::ERASER)
    {
        emit eraser();
    }
    if (previousTool == tool::SELECT)
    {
        emit select();
    }
    if (previousTool == tool::HAND)
    {
        emit hand();
    }

    previousTool = tool::NONE;
}

void Widget::startSelecting(QPointF mousePos)
{
    int pageNum = getPageFromMousePos(mousePos);
    QPointF pagePos = getPagePosFromMousePos(mousePos, pageNum);

    Selection newSelection;

    newSelection.pageNum = pageNum;
    newSelection.setWidth(currentDocument->pages[pageNum].getWidth());
    newSelection.setHeight(currentDocument->pages[pageNum].getHeight());
    newSelection.selectionPolygon.append(pagePos);

    currentSelection = newSelection;

//    selecting = true;
    currentState = state::SELECTING;
    selectingOnPage = pageNum;
}

void Widget::continueSelecting(QPointF mousePos)
{
    int pageNum = selectingOnPage;
    QPointF pagePos = getPagePosFromMousePos(mousePos, pageNum);

    currentSelection.selectionPolygon.append(pagePos);

    update();
}

void Widget::stopSelecting(QPointF mousePos)
{
    int pageNum = selectingOnPage;
    QPointF pagePos = getPagePosFromMousePos(mousePos, pageNum);

    currentSelection.selectionPolygon.append(pagePos);

    QVector<int> curvesInSelection;

    for (int i = 0; i < currentDocument->pages[pageNum].curves.size(); ++i)
    {
        Curve curve = currentDocument->pages[pageNum].curves.at(i);
        bool containsCurve = true;
        for (int j = 0; j < curve.points.size(); ++j)
        {
            if (!currentSelection.selectionPolygon.containsPoint(curve.points.at(j), Qt::OddEvenFill)) {
                containsCurve = false;
            }
        }
        if (containsCurve)
        {
            curvesInSelection.append(i);
        }
    }

    if (curvesInSelection.size() == 0)
    {
        // nothing selected
        currentState = state::IDLE;
    } else {
        // something selected
        currentState = state::SELECTED;

        for (int i = curvesInSelection.size()-1; i >= 0; --i)
        {
            currentSelection.curves.prepend(currentDocument->pages[pageNum].curves.at(curvesInSelection.at(i)));
        }
        currentSelection.finalize();

        CreateSelectionCommand* selectCommand = new CreateSelectionCommand(this, pageNum, curvesInSelection);
        undoStack.push(selectCommand);
    }
}

void Widget::letGoSelection()
{
    int pageNum = currentSelection.pageNum;
    ReleaseSelectionCommand* releaseCommand = new ReleaseSelectionCommand(this, pageNum);
    undoStack.push(releaseCommand);
    updateBuffer(pageNum);
    update();
}

void Widget::startDrawing(QPointF mousePos, qreal pressure)
{
    currentDocument->setDocumentChanged(true);
    emit modified();

    int pageNum = getPageFromMousePos(mousePos);
    QPointF pagePos = getPagePosFromMousePos(mousePos, pageNum);

    Curve newCurve;
    newCurve.points.append(pagePos);
    newCurve.pressures.append(pressure);
    newCurve.penWidth = currentPenWidth;
    newCurve.color = currentColor;
    currentCurve = newCurve;
//    drawing = true;
    currentState = state::DRAWING;

    previousMousePos = mousePos;
    drawingOnPage = pageNum;
}

void Widget::continueDrawing(QPointF mousePos, qreal pressure)
{
    QPointF pagePos = getPagePosFromMousePos(mousePos, drawingOnPage);
    QPointF previousPagePos = getPagePosFromMousePos(previousMousePos, drawingOnPage);

    currentCurve.points.append(pagePos);
    currentCurve.pressures.append(pressure);
    drawOnBuffer(previousPagePos, pagePos, pressure);

    QRect updateRect(previousMousePos.toPoint(), mousePos.toPoint());
    int rad = currentPenWidth * zoom / 2 + 2;
    updateRect = updateRect.normalized().adjusted(-rad, -rad, +rad, +rad);

    update(updateRect);
//    update();

    previousMousePos = mousePos;
}

void Widget::stopDrawing(QPointF mousePos, qreal pressure)
{
    QPointF pagePos = getPagePosFromMousePos(mousePos, drawingOnPage);

    currentCurve.points.append(pagePos);
    currentCurve.pressures.append(pressure);

//    currentDocument->pages[drawingOnPage].curves.append(currentCurve);
    AddCurveCommand* addCommand = new AddCurveCommand(this, drawingOnPage, currentCurve);
    undoStack.push(addCommand);

//    drawing = false;
    currentState = state::IDLE;

//    updateBuffer(drawingOnPage);
    update();
}

void Widget::erase(QPointF mousePos)
{
    int pageNum = getPageFromMousePos(mousePos);
    QPointF pagePos = getPagePosFromMousePos(mousePos, pageNum);

    QList<Curve> *curves = &(currentDocument->pages[pageNum].curves);

    qreal eraserWidth = 10;

    QLineF lineA = QLineF(pagePos + QPointF(-eraserWidth,-eraserWidth) / 2, pagePos + QPointF( eraserWidth,  eraserWidth) / 2);
    QLineF lineB = QLineF(pagePos + QPointF( eraserWidth,-eraserWidth) / 2, pagePos + QPointF(-eraserWidth,  eraserWidth) / 2); // lineA and lineB form a cross X

    QRectF rectE = QRectF(pagePos + QPointF(-eraserWidth, eraserWidth) / 2, pagePos + QPointF( eraserWidth, -eraserWidth) / 2);

    QVector<int> curvesToDelete;
    QPointF iPoint;

    for (int i = 0; i < curves->size(); ++i)
    {
        const Curve curve = curves->at(i);
        for (int j = 0; j < curve.points.length()-1; ++j)
        {
            QLineF line = QLineF(curve.points.at(j), curve.points.at(j+1));
            if (line.intersect(lineA, &iPoint) == QLineF::BoundedIntersection ||
                line.intersect(lineB, &iPoint) == QLineF::BoundedIntersection ||
                rectE.contains(curve.points.at(j)) ||
                rectE.contains(curve.points.at(j+1)))
            {
                curvesToDelete.append(i);
                break;
            }
        }
    }

    if (curvesToDelete.size() > 0)
    {
        currentDocument->setDocumentChanged(true);
        emit modified();

        QRect updateRect;
        std::sort(curvesToDelete.begin(), curvesToDelete.end(), std::greater<int>());
        for (int i = 0; i < curvesToDelete.size(); ++i)
        {
            updateRect = updateRect.united(currentDocument->pages[pageNum].curves.at(curvesToDelete.at(i)).points.boundingRect().toRect());
            RemoveCurveCommand* removeCommand = new RemoveCurveCommand(this, pageNum, curvesToDelete[i]);
            undoStack.push(removeCommand);
        }
    }
}

void Widget::startMovingSelection(QPointF mousePos)
{
    currentDocument->setDocumentChanged(true);
    emit modified();

    int pageNum = getPageFromMousePos(mousePos);
    previousPagePos = getPagePosFromMousePos(mousePos, pageNum);
    currentState = state::MODIFYING_SELECTION;
}

void Widget::continueMovingSelection(QPointF mousePos)
{
    int pageNum = getPageFromMousePos(mousePos);
    QPointF pagePos = getPagePosFromMousePos(mousePos, pageNum);
//    currentSelection.move(1 * (pagePos - previousPagePos));

    QPointF delta = (pagePos - previousPagePos);

    QTransform transform;
    transform = transform.translate(delta.x(), delta.y());

//    currentSelection.transform(transform, pageNum);
    TransformSelectionCommand* transSelectCommand = new TransformSelectionCommand(this, pageNum, transform);
    undoStack.push(transSelectCommand);

    previousPagePos = pagePos;
    update();
}

int Widget::getPageFromMousePos(QPointF mousePos)
{
    qreal y = mousePos.y() - currentCOSPos.y();
    int pageNum = 0;
    while (y > zoom * (currentDocument->pages[pageNum].getHeight() ) + PAGE_GAP)
    {
        y -= (currentDocument->pages[pageNum].getHeight() * zoom) + PAGE_GAP;
        pageNum += 1;
        if (pageNum >= currentDocument->pages.size())
        {
            pageNum = currentDocument->pages.size() - 1;
            break;
        }
    }
    return pageNum;
}

int Widget::getCurrentPage()
{
    QPoint globalMousePos = parentWidget()->mapToGlobal(QPoint(0,0)) + QPoint(parentWidget()->size().width()/2, parentWidget()->size().height()/2);
    QPoint pos = this->mapFromGlobal(globalMousePos);
    int pageNum = this->getPageFromMousePos(pos);


    return pageNum;
//    return getPageFromMousePos(QPointF(0.0, 2.0));
}

QPointF Widget::getPagePosFromMousePos(QPointF mousePos, int pageNum)
{
    qreal x = mousePos.x();
    qreal y = mousePos.y();
    for (int i = 0; i < pageNum; ++i)
    {
//        y -= (currentDocument->pages[i].getHeight() * zoom + PAGE_GAP); // THIS DOESN'T WORK PROPERLY (should be floor(...getHeight(), or just use pageBuffer[i].height())
        y -= (pageBuffer[i].height() + PAGE_GAP);
    }
//    y -= (pageNum) * (currentDocument->pages[0].getHeight() * zoom + PAGE_GAP);

//    QPointF pagePos = (QPointF(x,y) - currentCOSPos) / zoom;
    QPointF pagePos = (QPointF(x,y)) / zoom;


    return pagePos;
}

void Widget::newFile()
{
//    if (currentDocument->getDocumentChanged())
//    {
//        return;
//    }

    letGoSelection();

    delete currentDocument;
    currentDocument = new Document();
    pageBuffer.clear();
    undoStack.clear();
    updateAllPageBuffers();
    QRect widgetGeometry = getWidgetGeometry();
    resize(widgetGeometry.width(), widgetGeometry.height());

    emit modified();

    update();
}

void Widget::zoomIn()
{
//    QSize widgetSize = this->size();
//    QPointF widgetMidPoint = QPointF(widgetSize.width(), widgetSize.height());
//    currentCOSPos = currentCOSPos + (1 - ZOOM_STEP) * (0.5 * widgetMidPoint - currentCOSPos);
    zoom *= ZOOM_STEP;
    if (zoom > MAX_ZOOM)
        zoom = MAX_ZOOM;
    updateAllPageBuffers();
    setGeometry(getWidgetGeometry());
    update();

}

void Widget::zoomOut()
{
//    QSize widgetSize = this->size();
//    QPointF widgetMidPoint = QPointF(widgetSize.width(), widgetSize.height());
//    currentCOSPos = currentCOSPos + (1 - 1/ZOOM_STEP) * (0.5 * widgetMidPoint - currentCOSPos);
    zoom /= ZOOM_STEP;
    if (zoom < MIN_ZOOM)
        zoom = MIN_ZOOM;
    updateAllPageBuffers();
    setGeometry(getWidgetGeometry());
    update();

}

void Widget::zoomTo(qreal newZoom)
{
    if (newZoom >= MIN_ZOOM && newZoom <= MAX_ZOOM)
        zoom = newZoom;
}

void Widget::zoomFitWidth()
{
    QSize widgetSize = this->parentWidget()->size();
    int pageNum = getCurrentPage();
    zoom = widgetSize.width() / currentDocument->pages[pageNum].getWidth();

    updateAllPageBuffers();
    setGeometry(getWidgetGeometry());
    update();
}

void Widget::pageFirst()
{
    scrollDocumentToPageNum(0);
}

void Widget::pageLast()
{
    scrollDocumentToPageNum(currentDocument->pages.size()-1);
}

void Widget::pageUp()
{
//    int pageNum = getPageFromMousePos(QPointF(0.0,1.0)); // curret upper page displayed
    int pageNum = getCurrentPage();
    --pageNum;
    scrollDocumentToPageNum(pageNum);
}

void Widget::pageDown()
{
    //    int pageNum = getPageFromMousePos(QPointF(0.0,1.0)); // curret upper page displayed
    int pageNum = getCurrentPage();
    ++pageNum;

    if (pageNum >= currentDocument->pages.size())
    {
        pageAddEnd();
    }

    scrollDocumentToPageNum(pageNum);
}

void Widget::pageAddBefore()
{
    int pageNum = getCurrentPage();
    AddPageCommand* addPageCommand = new AddPageCommand(this, pageNum);
    undoStack.push(addPageCommand);
    setGeometry(getWidgetGeometry());
    update();
    currentDocument->setDocumentChanged(true);
    emit modified();
}

void Widget::pageAddAfter()
{
    int pageNum = getCurrentPage() + 1;
    AddPageCommand* addPageCommand = new AddPageCommand(this, pageNum);
    undoStack.push(addPageCommand);
    setGeometry(getWidgetGeometry());
    update();
    currentDocument->setDocumentChanged(true);

    emit modified();
}

void Widget::pageAddBeginning()
{
    AddPageCommand* addPageCommand = new AddPageCommand(this, 0);
    undoStack.push(addPageCommand);
    setGeometry(getWidgetGeometry());
    update();
    currentDocument->setDocumentChanged(true);

    emit modified();
}

void Widget::pageAddEnd()
{
    AddPageCommand* addPageCommand = new AddPageCommand(this, currentDocument->pages.size());
    undoStack.push(addPageCommand);
    setGeometry(getWidgetGeometry());
    update();
    currentDocument->setDocumentChanged(true);

    emit modified();
}

void Widget::pageRemove()
{
    if (currentDocument->pages.size() > 1)
    {
        int pageNum = getCurrentPage();
        RemovePageCommand* removePageCommand = new RemovePageCommand(this, pageNum);
        undoStack.push(removePageCommand);
        setGeometry(getWidgetGeometry());
        update();
        currentDocument->setDocumentChanged(true);
        emit modified();
    }
}

void Widget::scrollDocumentToPageNum(int pageNum)
{
    if (pageNum >= currentDocument->pages.size())
    {
        return; // page doesn't exist
    }
    if (pageNum < 0)
    {
        pageNum = 0;
    }
    qreal y = 0.0;
//    qreal x = currentCOSPos.x();
    for (int i = 0; i < pageNum; ++i)
    {
        y += (currentDocument->pages[i].getHeight()) * zoom + PAGE_GAP;
    }

    scrollArea->verticalScrollBar()->setValue(y);

//    currentCOSPos = QPointF(x, y);
//    updateAllPageBuffers();
//    update();
}


void Widget::setCurrentTool(tool toolID)
{
    currentTool = toolID;
    if (toolID == tool::PEN)
        setCursor(penCursor);
    if (toolID == tool::ERASER)
        setCursor(eraserCursor);
    if (toolID == tool::SELECT)
        setCursor(Qt::CrossCursor);
    if (toolID == tool::HAND)
        setCursor(Qt::OpenHandCursor);
}

void Widget::setDocument(Document* newDocument)
{
    delete currentDocument;
    currentDocument = newDocument;
    undoStack.clear();
    pageBuffer.clear();
    zoomFitWidth();
}

void Widget::copy()
{
    clipboard = currentSelection;
    update();
}

void Widget::paste()
{
    if (currentState == state::SELECTED)
    {
        letGoSelection();
    }
    currentSelection = clipboard;
    currentSelection.pageNum = getCurrentPage();
    currentState = state::SELECTED;
    update();
}

void Widget::cut()
{
    clipboard = currentSelection;
    currentSelection = Selection();
    currentState = state::IDLE;
    update();
}

void Widget::undo()
{
    if (undoStack.canUndo() && (currentState == state::IDLE || currentState == state::SELECTED))
    {
        undoStack.undo();
    }
}

void Widget::redo()
{
    if (undoStack.canRedo() && (currentState == state::IDLE || currentState == state::SELECTED))
    {
        undoStack.redo();
    }
}

void Widget::setCurrentState(state newState)
{
    currentState = newState;
}

Widget::state Widget::getCurrentState()
{
    return currentState;
}

void Widget::setCurrentColor(QColor newColor)
{
    currentColor = newColor;
    if (currentState == state::SELECTED)
    {
        ChangeColorOfSelectionCommand *changeColorCommand = new ChangeColorOfSelectionCommand(this, newColor);
        undoStack.push(changeColorCommand);
    }
}