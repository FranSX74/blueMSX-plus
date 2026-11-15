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
#include "ThemeLoader.h"
}

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontMetricsF>
#include <QtMath>
#include <cmath>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

/* ---- ArchBitmap / ArchText ---------------------------------------------------- */

/* `scale` > 1: the image holds `scale` pixels per page pixel (glyph fonts
** drawn for a high density screen). Sizes seen by the theme are in page
** pixels. */
struct ArchBitmap {
    QImage image;
    int    scale = 1;
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

/* Pixels per page pixel on the densest screen (1.25 with 125 % scaling). */
static qreal screenScale()
{
    return qApp ? qApp->devicePixelRatio() : 1.0;
}

extern "C" {

int archRGB(int r, int g, int b)
{
    return (r & 0xff) | ((g & 0xff) << 8) | ((b & 0xff) << 16);
}

static ArchBitmap* wrap(QImage image, int scale = 1)
{
    if (image.isNull()) return NULL;
    ArchBitmap* bm = new ArchBitmap;
    bm->image = image.convertToFormat(QImage::Format_RGB32);
    bm->scale = scale;
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
    return wrap(src->image.scaled(w * src->scale, h * src->scale, Qt::IgnoreAspectRatio, Qt::FastTransformation),
                src->scale);
}

ArchBitmap* archBitmapCreateScaledCopySmooth(ArchBitmap* src, int w, int h)
{
    if (src == NULL || w <= 0 || h <= 0) return NULL;
    return wrap(src->image.scaled(w * src->scale, h * src->scale, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
                src->scale);
}

void archBitmapDestroy(ArchBitmap* bm)
{
    delete bm;
}

int archBitmapGetWidth(ArchBitmap* bm)  { return bm ? bm->image.width() / bm->scale : 0; }
int archBitmapGetHeight(ArchBitmap* bm) { return bm ? bm->image.height() / bm->scale : 0; }

static void drawScaled(QPainter* p, ArchBitmap* bm, int xDest, int yDest, int xSrc, int ySrc, int width, int height)
{
    int s = bm->scale;
    if (s == 1) {
        p->drawImage(QPoint(xDest, yDest), bm->image, QRect(xSrc, ySrc, width, height));
        return;
    }
    p->save();
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->drawImage(QRect(xDest, yDest, width, height), bm->image, QRect(xSrc * s, ySrc * s, width * s, height * s));
    p->restore();
}

void archBitmapDraw(ArchBitmap* bm, void* dc, int xDest, int yDest, int xSrc, int ySrc, int width, int height)
{
    if (bm == NULL || dc == NULL) return;
    drawScaled(static_cast<QPainter*>(dc), bm, xDest, yDest, xSrc, ySrc, width, height);
}

void archBitmapCopy(ArchBitmap* dst, int xDest, int yDest, ArchBitmap* src, int xSrc, int ySrc, int width, int height)
{
    if (dst == NULL || src == NULL) return;
    QPainter p(&dst->image);
    p.scale(dst->scale, dst->scale);
    drawScaled(&p, src, xDest, yDest, xSrc, ySrc, width, height);
}

/* Redraws a font strip (the status line's digits and letters) with the UI
** typeface. The strip's own space glyph gives the background (a gradient in
** Classic), the digits give the ink colour, the cap height and the baseline. */
ArchBitmap* archBitmapCreateGlyphFont(ArchBitmap* x2, int frames, int z)
{
    const QImage& src = x2->image;
    int cw = src.width() / frames, ch = src.height();
    if (cw < 3 || frames <= '9' || x2->scale != 1) return NULL;

    QImage space = src.copy(' ' * cw, 0, cw, ch);
    auto dist = [](QRgb a, QRgb b) {
        return qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b)) + qAbs(qBlue(a) - qBlue(b));
    };
    QRgb ink = 0;
    int best = -1, top = ch, bottom = -1;
    for (int c = '0'; c <= '9'; c++) {
        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < cw; x++) {
                int d = dist(src.pixel(c * cw + x, y), space.pixel(x, y));
                if (d > best) { best = d; ink = src.pixel(c * cw + x, y); }
                if (d > 120) { top = qMin(top, y); bottom = qMax(bottom, y); }
            }
        }
    }
    if (bottom < top) return NULL;      /* no digits in this strip */

    /* Page cell size, as loadClassicBitmap would stretch it. */
    int fw = (cw * z + 1) / 2, fh = (ch * z + 1) / 2;
    int s = qMax(2, (int)std::ceil(screenScale()));
    qreal k = z / 2.0 * s;              /* image pixels per x2 pixel */

    QImage out(fw * frames * s, fh * s, QImage::Format_RGB32);
    QImage bg = space.scaled(fw * s, fh * s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPainter p(&out);
    for (int c = 0; c < frames; c++) p.drawImage(c * fw * s, 0, bg);

    QFont font = QApplication::font();
    font.setWeight(QFont::Medium);
    font.setHintingPreference(QFont::PreferNoHinting);
    /* A bit taller than the original glyphs, which leave room in the box;
    ** wide letters are narrowed below to fit the cell. */
    qreal capOrig = (bottom - top + 1) * k;
    qreal capWanted = qMin(capOrig * 1.3, (qreal)(bottom + 1) * k);
    font.setPixelSize(qMax(4, qRound(capWanted)));
    qreal cap = QFontMetricsF(font).capHeight();
    if (cap > 0) font.setPixelSize(qMax(4, qRound(font.pixelSize() * capWanted / cap)));
    QFontMetricsF fm(font);
    p.setFont(font);
    p.setPen(QColor(ink));
    p.setRenderHint(QPainter::TextAntialiasing);
    qreal baseline = qMin((bottom + 1) * k + (capWanted - capOrig) / 2, fh * s - 0.5 * s);
    qreal cell = fw * s;
    for (int c = 33; c < frames; c++) {
        if (c >= 127 && c < 160) continue;
        QString text(QChar((ushort)c));             /* Latin-1 */
        qreal adv = fm.horizontalAdvance(text);
        qreal room = cell - s * 0.5;
        p.save();
        p.setClipRect(QRectF(c * cell, 0, cell, fh * s));
        p.translate(c * cell + cell / 2, baseline);
        if (adv > room) p.scale(room / adv, 1.0);
        p.drawText(QPointF(-adv / 2, 0), text);
        p.restore();
    }
    p.end();
    return wrap(out, s);
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
    qreal dpr = devicePixelRatioF();
    canvas = QImage(qCeil(page->width * dpr), qCeil(page->height * dpr), QImage::Format_RGB32);
    canvas.setDevicePixelRatio(dpr);
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

/* Skins in <dir>/Themes/<name>/theme.xml; only the name is read here, the
** rest when the skin is first used. The user's folders come first, so a
** skin there hides a shipped one of the same name. */
static void addThemesFrom(const char* base)
{
    if (base == NULL || base[0] == 0) return;
    QDir dir(QDir(QString::fromUtf8(base)).filePath("Themes"));
    const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QString& sub : subdirs) {
        ThemeCollection* tc = themeLoadSkeleton(dir.filePath(sub).toUtf8().constData());
        if (tc == NULL) continue;
        bool dup = false;
        for (ThemeCollection* other : builtins) dup = dup || strcmp(other->name, tc->name) == 0;
        if (dup) themeCollectionDestroy(tc);
        else builtins.push_back(tc);
    }
}

std::vector<ThemeCollection*>& themeList()
{
    if (builtins.empty()) {
        builtins.push_back(themeClassicCreate());
        builtins.push_back(themeClassicCreateDark());
        for (ThemeCollection* tc : builtins) tc->loaded = 1;
        const PlatformPaths* paths = platformPaths();
        addThemesFrom(paths->configDir);
        addThemesFrom(paths->legacyDir);
        addThemesFrom(paths->dataDir);
    }
    return builtins;
}


}
