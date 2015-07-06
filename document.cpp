#include "document.h"

#include "qcompressor.h"

#include <QPdfWriter>
#include <QPageSize>
#include <iostream>
#include <QXmlStreamReader>
#include <QFile>
#include <QFileInfo>
#include <QErrorMessage>

#include <zlib.h>

// static members

const QColor Document::black = QColor(0,0,0);
const QColor Document::blue  = QColor(51,51,204);
const QColor Document::red   = QColor(255,0,0);
const QColor Document::green = QColor(0,128,0);
const QColor Document::gray  = QColor(128,128,128);
const QColor Document::lightblue = QColor(0,192,255);
const QColor Document::lightgreen = QColor(0,255,0);
const QColor Document::magenta= QColor(255,0,255);
const QColor Document::orange = QColor(255,128,0);
const QColor Document::yellow = QColor(255,255,0);
const QColor Document::white  = QColor(255,255,255);

Document::Document()
{
    for (int i = 0; i < 1; ++i)
    {
        Page nextPage;
        pages.append(nextPage);
    }
    setDocumentChanged(false);
}

void Document::paintPage(int pageNum, QPainter &painter, qreal zoom)
{
    pages[pageNum].paint(painter, zoom);
}

void Document::exportPDF(QString fileName)
{
//    QPdfWriter pdfWriter("/Users/tom/Desktop/qpdfwriter.pdf");
    QPdfWriter pdfWriter(fileName);
    pdfWriter.setPageSize(QPageSize(QSize(pages[0].getWidth(), pages[0].getHeight())));
    pdfWriter.setResolution(72);
    pdfWriter.pageLayout().setUnits(QPageLayout::Point);
    QPainter painter;

    std::cout << "PDF " << pdfWriter.colorCount() << std::endl;

    painter.begin(&pdfWriter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (int pageNum = 0; pageNum < pages.size(); ++pageNum)
    {
        paintPage(pageNum, painter, 1.0);

        if (pageNum+1 < pages.size())
        {
            pdfWriter.setPageSize(QPageSize(QSize(pages[pageNum+1].getWidth(), pages[pageNum+1].getHeight())));
            pdfWriter.newPage();
        }
    }
    painter.end();
}

bool Document::loadXOJ(QString fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }

    QXmlStreamReader reader;

    // check if it is a gzipped xoj
    QByteArray s = file.read(2);
    if (s.size() == 2)
    {
        if (s.at(0) == static_cast<char>(0x1f) && s.at(1) == static_cast<char>(0x8b))
        {
            // this is a gzipped file
            file.reset();
            QByteArray compressedData = file.readAll();
            QByteArray uncompressedData;
            if (!QCompressor::gzipDecompress(compressedData, uncompressedData))
            {
                return false;
            }
            reader.addData(uncompressedData);
        } else {
            file.reset();
            reader.setDevice(&file);
        }
    } else {
        return false;
    }

    pages.clear();

    while (!reader.atEnd())
    {
        reader.readNext();
        if (reader.name() == "page" && reader.tokenType() == QXmlStreamReader::StartElement)
        {
            QXmlStreamAttributes attributes = reader.attributes();
            QStringRef width = attributes.value("", "width");
            QStringRef height = attributes.value("", "height");
            Page newPage;
            newPage.setWidth(width.toDouble());
            newPage.setHeight(height.toDouble());

            pages.append(newPage);
        }
        if(reader.name() == "background" && reader.tokenType() == QXmlStreamReader::StartElement)
        {
            QXmlStreamAttributes attributes = reader.attributes();
            QStringRef color = attributes.value("", "color");
            QColor newColor = stringToColor(color.toString());
            pages.last().backgroundColor = newColor;
        }
        if (reader.name() == "stroke" && reader.tokenType() == QXmlStreamReader::StartElement)
        {
            QXmlStreamAttributes attributes = reader.attributes();
            QStringRef tool = attributes.value("", "tool");
            if (tool == "pen")
            {
                Curve newCurve;
                QStringRef color = attributes.value("", "color");
                newCurve.color = stringToColor(color.toString());
                QStringRef strokeWidth = attributes.value("", "width");
                QStringList strokeWidthList = strokeWidth.toString().split(" ");
                newCurve.penWidth = strokeWidthList.at(0).toDouble();
                newCurve.pressures.append(newCurve.penWidth / strokeWidthList.at(0).toDouble());
                for (int i = 1; i < strokeWidthList.size(); ++i)
                {
                    newCurve.pressures.append(2 * strokeWidthList.at(i).toDouble() / newCurve.penWidth - newCurve.pressures.at(i-1));
                }
                QString elementText = reader.readElementText();
                QStringList elementTextList = elementText.split(" ");
                for (int i = 0; i+1 < elementTextList.size(); i = i + 2)
                {
                    newCurve.points.append(QPointF(elementTextList.at(i).toDouble(), elementTextList.at(i+1).toDouble()));
                }
                while (newCurve.points.size() > newCurve.pressures.size())
                {
                    newCurve.pressures.append(1.0);
                }
                pages.last().curves.append(newCurve);
            }
        }
    }

    QFileInfo fileInfo(file);
    file.close();

    if (reader.hasError())
    {
        return false;
    } else {
        path = fileInfo.absolutePath();
        docName = fileInfo.completeBaseName();
        return true;
    }

}

bool Document::saveXOJ(QString fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }

    QXmlStreamWriter writer;

    writer.setAutoFormatting(true);

    writer.setDevice(&file);

    writer.writeStartDocument("1.0", false);
    writer.writeStartElement("xournal");
    writer.writeAttribute(QXmlStreamAttribute("version", "0.4.8"));

    writer.writeStartElement("title");
    writer.writeCharacters("Xournal document - see http://math.mit.edu/~auroux/software/xournal/");
    writer.writeEndElement();

    for (int i = 0; i < pages.size(); ++i)
    {
        writer.writeStartElement("page");
        writer.writeAttribute(QXmlStreamAttribute("width" , QString::number(pages[i].getWidth())));
        writer.writeAttribute(QXmlStreamAttribute("height", QString::number(pages[i].getHeight())));
        writer.writeEmptyElement("background");
        writer.writeAttribute(QXmlStreamAttribute("type", "solid"));
//        writer.writeAttribute(QXmlStreamAttribute("color", "white"));
        writer.writeAttribute(QXmlStreamAttribute("color", toRGBA(pages[i].backgroundColor.name(QColor::HexArgb))));
        writer.writeAttribute(QXmlStreamAttribute("style", "plain"));
        writer.writeStartElement("layer");

        for (int j = 0; j < pages[i].curves.size(); ++j)
        {
            writer.writeStartElement("stroke");
            writer.writeAttribute(QXmlStreamAttribute("tool", "pen"));
            writer.writeAttribute(QXmlStreamAttribute("color", toRGBA(pages[i].curves[j].color.name(QColor::HexArgb))));
            qreal width = pages[i].curves[i].penWidth;
            QString widthString;
            widthString.append(QString::number(width));
            for (int k = 0; k < pages[i].curves[j].pressures.size()-1; ++k)
            {
                qreal p0 = pages[i].curves[j].pressures[k];
                qreal p1 = pages[i].curves[j].pressures[k+1];
                widthString.append(' ');
                widthString.append(QString::number(0.5 * (p0+p1) * width));
            }
            writer.writeAttribute(QXmlStreamAttribute("width", widthString));
            for (int k = 0; k < pages[i].curves[j].points.size(); ++k)
            {
                writer.writeCharacters(QString::number(pages[i].curves[j].points[k].x()));
                writer.writeCharacters(" ");
                writer.writeCharacters(QString::number(pages[i].curves[j].points[k].y()));
                writer.writeCharacters(" ");
            }
            writer.writeEndElement(); // closing "stroke"
        }

        writer.writeEndElement(); // closing "layer"
        writer.writeEndElement(); // closing "page"
    }

    writer.writeEndDocument();

    QFileInfo fileInfo(file);

    file.close();

    if (writer.hasError())
    {
        return false;
    } else {
        setDocumentChanged(false);


        path = fileInfo.absolutePath();
        docName = fileInfo.completeBaseName();
        return true;
    }

}

bool Document::setDocName(QString newDocName)
{
    // check for special characters not to be used in filenames ... (probably not)
    docName = newDocName;
    return true;
}

QString Document::getDocName()
{
    return docName;
}

bool Document::setPath(QString newPath)
{
    path = newPath;
    return true;
}

QString Document::getPath()
{
    return path;
}

bool Document::getDocumentChanged()
{
    return documentChanged;
}

void Document::setDocumentChanged(bool changed)
{
    documentChanged = changed;
}

QString Document::toARGB(QString rgba)
{
    // #RRGGBBAA
    // 012345678
    QString argb;
    if (rgba.length() == 9)
    {
        argb.append('#');
        argb.append(rgba.mid(7,2));
        argb.append(rgba.mid(1,6));
    } else {
        argb = QString("");
    }

    return argb;
}

QString Document::toRGBA(QString argb)
{
    // #AARRGGBB
    // 012345678
    QString rgba;
    if (argb.length() == 9)
    {
        rgba.append('#');
        rgba.append(argb.mid(3,6));
        rgba.append(argb.mid(1,2));
    } else {
        rgba = QString("");
    }

    return rgba;
}

QColor Document::stringToColor(QString colorString)
{
    QColor color;
    if (colorString.left(1).compare("#") == 0)
    {
        color = QColor(toARGB(colorString));
    } else {
        for (int i = 0; i < standardColors.size(); ++i)
        {
            if (standardColorNames[i].compare(colorString) == 0)
            {
                color = standardColors.at(i);
            }
        }
    }
    return color;
}