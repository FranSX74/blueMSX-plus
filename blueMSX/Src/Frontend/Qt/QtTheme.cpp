/*****************************************************************************
** QblueMSX - the blueMSX skin engine on Qt
**
** The theme code (Src/Theme) lays out and animates the classic blueMSX
** window; it draws through ArchBitmap/ArchText, implemented here with a
** QPainter as the "device context". ThemeView keeps the page in an off-screen
** image, refreshes it from a timer as Windows does, and hosts the menu bar
** and the emulated screen at the places the page gives them.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "QtApp.h"

extern "C" {
#include "ArchBitmap.h"
#include "ArchText.h"
#include "Theme.h"
#include "ThemeClassic.h"
}

#include <QFont>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

/* ---- ArchBitmap / ArchText ---------------------------------------------------- */

struct ArchBitmap {
    QImage image;
};

struct ArchText {
    int height;
    int color;
    int right;
};

static QColor colorRef(int c)
{
    /* archRGB values are Windows COLORREFs: 0x00bbggrr. */
    return QColor(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff);
}

extern "C" {

int archRGB(int r, int g, int b)
{
    return (r & 0xff) | ((g & 0xff) << 8) | ((b & 0xff) << 16);
}

static ArchBitmap* wrap(QImage image)
{
    if (image.isNull()) return NULL;
    ArchBitmap* bm = new ArchBitmap;
    bm->image = image.convertToFormat(QImage::Format_RGB32);
    return bm;
}

ArchBitmap* archBitmapCreate(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::black);
    return wrap(image);
}

ArchBitmap* archBitmapCreateFromFile(const char* filename)
{
    return wrap(QImage(QString::fromUtf8(filename)));
}

ArchBitmap* archBitmapCreateFromId(int)
{
    return NULL;    /* no Windows resources; ThemeClassic loads files */
}

ArchBitmap* archBitmapCreateScaledCopy(ArchBitmap* src, int w, int h)
{
    if (src == NULL || w <= 0 || h <= 0) return NULL;
    return wrap(src->image.scaled(w, h, Qt::IgnoreAspectRatio, Qt::FastTransformation));
}

ArchBitmap* archBitmapCreateScaledCopySmooth(ArchBitmap* src, int w, int h)
{
    if (src == NULL || w <= 0 || h <= 0) return NULL;
    return wrap(src->image.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

void archBitmapDestroy(ArchBitmap* bm)
{
    delete bm;
}

int archBitmapGetWidth(ArchBitmap* bm)  { return bm ? bm->image.width() : 0; }
int archBitmapGetHeight(ArchBitmap* bm) { return bm ? bm->image.height() : 0; }

void archBitmapDraw(ArchBitmap* bm, void* dc, int xDest, int yDest, int xSrc, int ySrc, int width, int height)
{
    if (bm == NULL || dc == NULL) return;
    static_cast<QPainter*>(dc)->drawImage(QPoint(xDest, yDest), bm->image, QRect(xSrc, ySrc, width, height));
}

void archBitmapCopy(ArchBitmap* dst, int xDest, int yDest, ArchBitmap* src, int xSrc, int ySrc, int width, int height)
{
    if (dst == NULL || src == NULL) return;
    QPainter p(&dst->image);
    p.drawImage(QPoint(xDest, yDest), src->image, QRect(xSrc, ySrc, width, height));
}

ArchText* archTextCreate(int height, int color, int rightAligned)
{
    return new ArchText{ height, color, rightAligned };
}

void archTextDestroy(ArchText* text)
{
    delete text;
}

void archTextDraw(ArchText* text, void* dc, int xDest, int yDest, int width, int height, char* string)
{
    if (text == NULL || dc == NULL || string == NULL) return;
    QPainter* p = static_cast<QPainter*>(dc);
    QFont font = p->font();
    font.setPixelSize(text->height > 0 ? text->height : 12);
    p->save();
    p->setFont(font);
    p->setPen(colorRef(text->color));
    p->drawText(QRect(xDest, yDest, width, height),
                (text->right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop, QString::fromUtf8(string));
    p->restore();
}

/* Embedded native windows (used by a few external themes). */
void* archObjectCreate(char*, void*, int, int, int, int, intptr_t, intptr_t) { return NULL; }
void  archObjectDestroy(char*, void*) {}

}

/* ---- ThemeView ------------------------------------------------------------------ */

namespace qbm {

ThemeView::ThemeView(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    bar = new QMenuBar(this);
    screenWidget = new ScreenWidget(this);
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ThemeView::refresh);
}

int ThemeView::menuHeight() const
{
    return bar->sizeHint().height();
}

void ThemeView::setPage(ThemePage* p)
{
    if (page) themePageActivate(page, NULL);
    page = p;
    if (page == NULL) {
        timer->stop();
        layoutPlain();
        return;
    }
    themePageActivate(page, this);
    canvas = QImage(page->width, page->height, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    {
        QPainter painter(&canvas);
        themePageDraw(page, &painter, NULL);
    }

    QPalette pal = bar->palette();
    pal.setColor(QPalette::Window, colorRef((int)page->menu.color));
    pal.setColor(QPalette::Button, colorRef((int)page->menu.color));
    pal.setColor(QPalette::WindowText, colorRef((int)page->menu.textColor));
    pal.setColor(QPalette::ButtonText, colorRef((int)page->menu.textColor));
    pal.setColor(QPalette::Highlight, colorRef((int)page->menu.focusColor));
    bar->setPalette(pal);
    bar->setAutoFillBackground(true);
    bar->setGeometry(page->menu.x, page->menu.y, page->menu.width, menuHeight());
    bar->show();
    timer->start(100);
    update();
}

void ThemeView::setScreenRect(const QRect& r)
{
    screenWidget->setGeometry(r);
}

void ThemeView::layoutPlain()
{
    bar->setPalette(QPalette());
    bar->setAutoFillBackground(false);
    int top = bar->isVisible() ? menuHeight() : 0;
    bar->setGeometry(0, 0, width(), menuHeight());
    screenWidget->setGeometry(0, top, width(), height() - top);
}

void ThemeView::resizeEvent(QResizeEvent*)
{
    if (page == NULL) layoutPlain();
}

/* Draws on the page image. A theme button may run an action that opens a
** modal dialog while its painter is still active; the refresh timer firing
** inside that dialog's event loop just skips a beat. */
void ThemeView::paintTheme(const std::function<void(QPainter*)>& fn)
{
    if (page == NULL || painting) return;
    painting = true;
    {
        QPainter painter(&canvas);
        fn(&painter);
    }
    painting = false;
    update();
}

void ThemeView::refresh()
{
    paintTheme([this](QPainter* p) { themePageUpdate(page, p); });
}

void ThemeView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (page == NULL) {
        p.fillRect(rect(), Qt::black);
        return;
    }
    p.drawImage(0, 0, canvas);
}

void ThemeView::mouseMoveEvent(QMouseEvent* e)
{
    if (page == NULL) return;
    QPoint pos = e->position().toPoint();
    paintTheme([this, pos](QPainter* p) { themePageMouseMove(page, p, pos.x(), pos.y()); });
    const char* tip = themePageHoverSliderText(page, pos.x(), pos.y());
    setToolTip(tip ? QString::fromUtf8(tip) : QString());
}

void ThemeView::mousePressEvent(QMouseEvent* e)
{
    if (page == NULL || e->button() != Qt::LeftButton) return;
    QPoint pos = e->position().toPoint();
    paintTheme([this, pos](QPainter* p) { themePageMouseButtonDown(page, p, pos.x(), pos.y()); });
}

void ThemeView::mouseReleaseEvent(QMouseEvent* e)
{
    if (page == NULL || e->button() != Qt::LeftButton) return;
    QPoint pos = e->position().toPoint();
    paintTheme([this, pos](QPainter* p) { themePageMouseButtonUp(page, p, pos.x(), pos.y()); });
}

void ThemeView::setActive(bool active)
{
    paintTheme([this, active](QPainter* p) { themePageSetActive(page, p, active); });
}

/* ---- Theme list ------------------------------------------------------------------ */

static std::vector<ThemeCollection*> builtins;

std::vector<ThemeCollection*>& themeList()
{
    if (builtins.empty()) {
        builtins.push_back(themeClassicCreate());
        builtins.push_back(themeClassicCreateDark());
    }
    return builtins;
}


}
