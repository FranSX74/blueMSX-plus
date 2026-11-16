/*****************************************************************************
** QblueMSX - Qt6 frontend for blueMSX+
**
** A QMainWindow with the blueMSX menus over the emulated screen. The core
** runs on its own thread and talks to the window through the FrontendHooks,
** which only queue work for the UI thread.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "QtApp.h"
#include "FrontendMenus.h"

extern "C" {
#include "PlatformShortcuts.h"
#include "LaunchFile.h"
#include "Language.h"
}

#include <QAction>
#include <QApplication>
#include <QStandardPaths>
#include <QScreen>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWindow>

extern "C" {
#include "Theme.h"
#include "ThemeLoader.h"
}
#include <cstring>

#include <SDL3/SDL.h>
#include <cstdio>

namespace qbm {

static MainWindow* theWindow;
static ThemeView*  theView;     /* set before the themes are first built */

MainWindow* mainWindow() { return theWindow; }

void post(std::function<void()> fn)
{
    QMetaObject::invokeMethod(qApp, std::move(fn), Qt::QueuedConnection);
}

/* ---- Menus ------------------------------------------------------------------- */

static QString menuText(const std::string& s)
{
    /* '&' marks a mnemonic in Qt; file names may contain one. */
    return QString::fromUtf8(s.c_str()).replace('&', "&&");
}

class QtMenuBuilder : public fe::MenuBuilder {
public:
    explicit QtMenuBuilder(QMenu* root) { stack.push_back(root); }
    bool hasDebugger() const override { return true; }

    void item(const std::string& label, int sc, bool enabled, int check,
              std::function<void()> fn) override
    {
        QString text = menuText(label);
        const char* keys = sc >= 0 ? platformShortcutText((ShortcutId)sc) : "";
        if (*keys) text += '\t' + QString::fromUtf8(keys);
        QAction* a = stack.back()->addAction(text);
        a->setEnabled(enabled);
        if (check != fe::NO_CHECK) {
            a->setCheckable(true);
            a->setChecked(check != 0);
        }
        /* Run once the menu has closed: the action may open a modal dialog. */
        QObject::connect(a, &QAction::triggered, [fn]() { QTimer::singleShot(0, fn); });
    }
    void separator() override { stack.back()->addSeparator(); }
    void caption(const std::string& label) override
    {
        QAction* a = stack.back()->addAction(menuText(label));
        a->setEnabled(false);
    }
    bool beginMenu(const std::string& label, bool enabled) override
    {
        QMenu* m = stack.back()->addMenu(menuText(label));
        m->setEnabled(enabled);
        stack.push_back(m);
        return true;
    }
    void endMenu() override { stack.pop_back(); }

private:
    std::vector<QMenu*> stack;
};

void fillMenu(QMenu* menu, FrontendMenu which)
{
    /* clear() leaves the submenus, which are children of the menu. */
    for (QMenu* sub : menu->findChildren<QMenu*>(Qt::FindDirectChildrenOnly)) {
        sub->deleteLater();
    }
    menu->clear();
    QtMenuBuilder b(menu);
    fe::buildMenu(which, b);

    /* QblueMSX's own: the window skin, under Window. */
    if (which == FRONTEND_MENU_ZOOM) {
        Properties* p = platformGetProperties();
        menu->addSeparator();
        QMenu* themes = menu->addMenu(QString::fromUtf8(langPropTheme()));
        std::vector<std::string> names;
        for (ThemeCollection* tc : themeList()) names.push_back(tc->name);
        names.push_back("Qt");
        for (const std::string& name : names) {
            QAction* a = themes->addAction(menuText(name == "Qt" ? "Qt (no skin)" : name));
            bool current = p != NULL && (name == p->settings.themeName ||
                           (p->settings.themeName[0] == 0 && name == names[0]));
            a->setCheckable(true);
            a->setChecked(current);
            QObject::connect(a, &QAction::triggered, [name]() {
                QTimer::singleShot(0, [name]() {
                    Properties* q = platformGetProperties();
                    snprintf(q->settings.themeName, sizeof(q->settings.themeName), "%s", name.c_str());
                    if (theWindow) theWindow->applyWindowSettings();
                });
            });
        }
    }
}

void MainWindow::rebuildMenuBar()
{
    std::vector<fe::MainMenu> titles = fe::mainMenus();
    QList<QAction*> current = menus()->actions();
    bool same = current.size() == (int)titles.size();
    for (int i = 0; same && i < (int)titles.size(); i++) {
        same = current[i]->text() == QString::fromUtf8(titles[i].title);
    }
    if (same) return;

    menus()->clear();
    for (const fe::MainMenu& m : titles) {
        QMenu* menu = menus()->addMenu(QString::fromUtf8(m.title));
        FrontendMenu which = m.menu;
        /* Filled now so the menu is never empty, and again on every show. */
        fillMenu(menu, which);
        connect(menu, &QMenu::aboutToShow, menu, [menu, which]() { fillMenu(menu, which); });
    }
}

void MainWindow::popupMenu(FrontendMenu which, int x, int y)
{
    QMenu* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    fillMenu(menu, which);
    QPoint pos;
    if (x >= 0 && y >= 0 && view->currentPage() != NULL) {
        pos = view->mapToGlobal(QPoint(x, y));      /* below the theme button */
    }
    else {
        pos = QCursor::pos();
        if (!geometry().contains(pos)) pos = screen()->mapToGlobal(screen()->rect().center());
    }
    menu->popup(pos);
}

/* ---- Screen -------------------------------------------------------------------- */

ScreenWidget::ScreenWidget(QWidget* parent)
    : QOpenGLWidget(parent), frame(QBM_TEX_WIDTH, QBM_TEX_HEIGHT, QImage::Format_RGB32)
{
    frame.fill(Qt::black);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(PLATFORM_SCREEN_WIDTH, PLATFORM_SCREEN_HEIGHT);

    notice = new QLabel(this);
    notice->setStyleSheet("QLabel { background: rgba(20, 30, 50, 220); color: white;"
                          " border-radius: 4px; padding: 6px 10px; }");
    notice->hide();
    noticeTimer = new QTimer(this);
    noticeTimer->setSingleShot(true);
    connect(noticeTimer, &QTimer::timeout, notice, &QLabel::hide);
}

void ScreenWidget::uploadFrame()
{
    platformRenderFrame(frame.bits(), (int)frame.bytesPerLine(), QBM_RENDER_ZOOM);
    platformDisplayDone();
    /* The core leaves the X byte of XRGB at 0; Qt's RGB32 wants it 0xff, or
    ** the OpenGL paint engine takes the picture for transparent. */
    quint32* px = reinterpret_cast<quint32*>(frame.bits());
    for (qsizetype i = 0, n = qsizetype(frame.width()) * frame.height(); i < n; i++) {
        px[i] |= 0xff000000u;
    }
    frameDirty = true;
    recordShaderFrame();
    update();
}

void ScreenWidget::showNotice(const QString& text)
{
    notice->setText(text);
    notice->adjustSize();
    notice->move((width() - notice->width()) / 2, height() - notice->height() - 16);
    notice->show();
    notice->raise();
    noticeTimer->start(3000);
}

void ScreenWidget::keyPressEvent(QKeyEvent* e)
{
    if (e->isAutoRepeat()) return;
    int hid = hidFromKeyEvent(e);
    if (hid) platformKeyEvent(hid, 1);
}

void ScreenWidget::keyReleaseEvent(QKeyEvent* e)
{
    if (e->isAutoRepeat()) return;
    int hid = hidFromKeyEvent(e);
    if (hid) platformKeyEvent(hid, 0);
}

void ScreenWidget::focusInEvent(QFocusEvent*)  { platformKeyboardFocus(1); }
void ScreenWidget::focusOutEvent(QFocusEvent*) { platformKeyboardFocus(0); }

/* Wayland lets Qt grab the pointer only for popups (and says so on each
** try); the hidden cursor over the screen is enough there. */
static bool canGrabMouse()
{
    return QGuiApplication::platformName() != "wayland";
}

void ScreenWidget::setMouseCaptured(bool on)
{
    captured = on;
    if (on) {
        setCursor(Qt::BlankCursor);
        if (canGrabMouse()) grabMouse();
    }
    else {
        if (canGrabMouse()) releaseMouse();
        unsetCursor();
    }
}

void ScreenWidget::mouseMoveEvent(QMouseEvent* e)
{
    QPoint pos = e->position().toPoint();
    QPoint d = pos - lastMouse;
    lastMouse = pos;
    float nx = screenArea.width() > 0 ? float(pos.x() - screenArea.x()) / screenArea.width() : 0;
    float ny = screenArea.height() > 0 ? float(pos.y() - screenArea.y()) / screenArea.height() : 0;
    platformMouseMotion(d.x(), d.y(), nx, ny);
}

static int msxButton(Qt::MouseButton b)
{
    return b == Qt::LeftButton ? 1 : b == Qt::MiddleButton ? 2 : 3;
}

void ScreenWidget::mousePressEvent(QMouseEvent* e)   { platformMouseButton(msxButton(e->button()), 1); }
void ScreenWidget::mouseReleaseEvent(QMouseEvent* e) { platformMouseButton(msxButton(e->button()), 0); }

/* ---- Main window ---------------------------------------------------------------- */

MainWindow::MainWindow()
{
    view = new ThemeView(this);
    theView = view;
    setCentralWidget(view);
    setAcceptDrops(true);
    rebuildMenuBar();
    updateTitle();
    screen()->setFocus();
}

void MainWindow::updateTitle()
{
    QString title = "QblueMSX";
    std::string status = fe::machineStatus();
    if (!status.empty()) title += " - " + QString::fromUtf8(status.c_str());
    setWindowTitle(title);
}

/* The theme collection named in the settings; Classic when unknown, none
** for "Qt" (plain window). */
static ThemeCollection* selectedTheme()
{
    Properties* p = platformGetProperties();
    std::vector<ThemeCollection*>& list = themeList();
    if (p != NULL && strcmp(p->settings.themeName, "Qt") == 0) return NULL;
    for (ThemeCollection* tc : list) {
        if (p != NULL && strcmp(tc->name, p->settings.themeName) == 0) return tc;
    }
    return list.empty() ? NULL : list[0];
}

/* The theme page for a zoom, NULL for a plain window. */
static ThemePage* pageForZoom(int zoom)
{
    ThemeCollection* tc = selectedTheme();
    if (tc == NULL) return NULL;
    /* External skins are parsed on first use; their zooms other than the
    ** designed ones are the "normal" page scaled by zoom/2. */
    if (!themeCollectionEnsureLoaded(tc)) return NULL;
    if (zoom < THEME_ZOOM_COUNT) themeCollectionEnsureZoom(tc, zoom);
    Theme* theme = zoom < THEME_ZOOM_COUNT ? tc->zoom[zoom] : NULL;
    if (theme == NULL && zoom != 1) theme = tc->zoom[2];
    return theme ? themeGetCurrentPage(theme) : NULL;
}

static int currentZoom()
{
    Properties* p = platformGetProperties();
    int zoom = p != NULL ? p->video.windowSize - P_VIDEO_SIZEX1 + 1 : 2;
    return qBound(1, zoom, 8);
}

QSize MainWindow::sizeForZoom(int zoom) const
{
    ThemePage* page = pageForZoom(zoom);
    if (page == NULL) {
        return QSize(PLATFORM_SCREEN_WIDTH * zoom, PLATFORM_SCREEN_HEIGHT * zoom + view->menuHeight());
    }
    return QSize(qMax(page->width, page->emuWinX + PLATFORM_SCREEN_WIDTH * zoom),
                 qMax(page->height, page->emuWinY + PLATFORM_SCREEN_HEIGHT * zoom));
}

/* At start-up the window must fit the screen it opens on: a saved 4x does
** not fit a 1080p screen, so it opens at the largest zoom that does. */
void MainWindow::fitZoomToScreen()
{
    Properties* p = platformGetProperties();
    if (p == NULL || p->video.windowSize == P_VIDEO_SIZEFULLSCREEN) return;
    QScreen* s = QWidget::screen() ? QWidget::screen() : QGuiApplication::primaryScreen();
    if (s == NULL) return;
    QSize avail = s->availableGeometry().size() - QSize(8, 24);    /* a little room for decorations or a panel */
    int zoom = currentZoom();
    while (zoom > 1) {
        QSize need = sizeForZoom(zoom);
        if (need.width() <= avail.width() && need.height() <= avail.height()) break;
        zoom--;
    }
    if (zoom != currentZoom()) p->video.windowSize = P_VIDEO_SIZEX1 + zoom - 1;
}

void MainWindow::applyWindowSettings()
{
    Properties* p = platformGetProperties();
    bool fullscreen = p != NULL && p->video.windowSize == P_VIDEO_SIZEFULLSCREEN;

    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    if (fullscreen) {
        view->setPage(NULL);
        menus()->hide();
        view->layoutPlain();
        showFullScreen();
        return;
    }
    menus()->show();
    if (isFullScreen()) showNormal();
    int zoom = currentZoom();

    ThemePage* page = pageForZoom(zoom);
    view->setPage(page);
    if (page == NULL) {
        view->layoutPlain();
        resize(sizeForZoom(zoom));
        return;
    }
    /* The page fixes the window: artwork, menu strip and screen. */
    view->setScreenRect(QRect(page->emuWinX, page->emuWinY, PLATFORM_SCREEN_WIDTH * zoom, PLATFORM_SCREEN_HEIGHT * zoom));
    setFixedSize(sizeForZoom(zoom));
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    e->accept();
    qApp->quit();
}

void MainWindow::changeEvent(QEvent* e)
{
    /* Fullscreen left by the window manager: keep the setting in step. */
    if (e->type() == QEvent::WindowStateChange) {
        Properties* p = platformGetProperties();
        if (p != NULL && !isFullScreen() && p->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
            menus()->show();
        }
    }
    if (e->type() == QEvent::ActivationChange) {
        view->setActive(isActiveWindow());
    }
    QMainWindow::changeEvent(e);
}

/* ---- Hooks ---------------------------------------------------------------------- */

static void hookDisplayUpdate()
{
    post([]() {
        if (theWindow) theWindow->screen()->uploadFrame();
        else platformDisplayDone();     /* no window yet: just let the core go on */
    });
}

static void hookWindowUpdate()
{
    post([]() { if (theWindow) theWindow->applyWindowSettings(); });
}

static void hookMenuUpdate(int)
{
    post([]() {
        if (theWindow) {
            theWindow->rebuildMenuBar();
            theWindow->updateTitle();
        }
        refreshDialogs();
    });
}

static void hookEmulationState(int)
{
    post([]() { if (theWindow) theWindow->updateTitle(); });
}

static void hookMessage(FrontendMessageKind kind, const char* text)
{
    fprintf(stderr, "QblueMSX: %s\n", text);
    QString s = QString::fromUtf8(text);
    post([kind, s]() { showMessage(kind, s); });
}

static void hookQuit()
{
    post([]() { if (theWindow) theWindow->close(); });
}

static void hookMouseCapture(int on)
{
    post([on]() { if (theWindow) theWindow->screen()->setMouseCaptured(on != 0); });
}

static void hookShowMenu(FrontendMenu menu, int x, int y)
{
    if (theWindow) theWindow->popupMenu(menu, x, y);
}

static int hookMenuStripHeight()
{
    return theView ? theView->menuHeight() : 0;
}

static void hookShowDialog(FrontendDialog dialog, int arg)
{
    openDialog(dialog, arg);
}

static void hookWindowCommand(FrontendWindowCommand cmd)
{
    if (theWindow == NULL) return;
    switch (cmd) {
    case FRONTEND_WINDOW_MINIMIZE: theWindow->showMinimized(); break;
    case FRONTEND_WINDOW_MAXIMIZE: theWindow->showMaximized(); break;
    case FRONTEND_WINDOW_CLOSE:    theWindow->close(); break;
    case FRONTEND_WINDOW_START_MOVE:
        if (QWindow* w = theWindow->windowHandle()) w->startSystemMove();
        break;
    case FRONTEND_WINDOW_THEME_PAGE:
        /* Called while the skin is drawing a button press: relayout after. */
        QTimer::singleShot(0, []() { if (theWindow) theWindow->applyWindowSettings(); });
        break;
    }
}

}

/* Dropping a ROM, disk or tape on the window launches it. */
class DropFilter : public QObject {
protected:
    bool eventFilter(QObject* obj, QEvent* e) override
    {
        if (e->type() == QEvent::DragEnter) {
            auto* de = static_cast<QDragEnterEvent*>(e);
            if (de->mimeData()->hasUrls()) {
                de->acceptProposedAction();
                return true;
            }
        }
        else if (e->type() == QEvent::Drop) {
            auto* de = static_cast<QDropEvent*>(e);
            for (const QUrl& url : de->mimeData()->urls()) {
                if (!url.isLocalFile()) continue;
                QByteArray file = url.toLocalFile().toUtf8();
                QTimer::singleShot(0, [file]() {
                    tryLaunchUnknownFile(platformGetProperties(), file.constData(), 1);
                });
                break;
            }
            de->acceptProposedAction();
            return true;
        }
        return QObject::eventFilter(obj, e);
    }
};

using namespace qbm;

int main(int argc, char** argv)
{
    QApplication::setApplicationName("QblueMSX");
    QApplication::setOrganizationName("bluemsx");
    QApplication app(argc, argv);
    /* Name the desktop entry only once it is installed: the portal
    ** complains about an app ID it cannot find. */
    if (!QStandardPaths::locate(QStandardPaths::ApplicationsLocation, "org.bluemsx.qbluemsx.desktop").isEmpty()) {
        QApplication::setDesktopFileName("org.bluemsx.qbluemsx");
    }

    FrontendHooks hooks = {};
    hooks.displayUpdate  = hookDisplayUpdate;
    hooks.windowUpdate   = hookWindowUpdate;
    hooks.menuUpdate     = hookMenuUpdate;
    hooks.emulationState = hookEmulationState;
    hooks.message        = hookMessage;
    hooks.quit           = hookQuit;
    hooks.mouseCapture   = hookMouseCapture;
    hooks.showMenu       = hookShowMenu;
    hooks.showDialog     = hookShowDialog;
    hooks.windowCommand  = hookWindowCommand;
    hooks.chooseFile     = chooseFile;
    hooks.chooseFromList = chooseFromList;
    hooks.menuStripHeight = hookMenuStripHeight;
    hooks.recordFrame    = []() {
        post([]() { if (theWindow) theWindow->screen()->shadeReplayFrames(); });
    };

    /* Qt has parsed its own options out of argv by now. */
    int rc = platformInit(argc, argv, &hooks);
    if (rc != 0) {
        /* Show an error queued before there was a window, then leave. */
        QCoreApplication::processEvents();
        platformShutdown();
        return rc < 0 ? 1 : 0;
    }

    MainWindow window;
    theWindow = &window;
    DropFilter drops;
    window.installEventFilter(&drops);
    window.fitZoomToScreen();
    window.applyWindowSettings();
    window.show();

    /* Gamepads come through SDL, which the platform layer set up. SDL also
    ** turns SIGINT and SIGTERM into a quit event. */
    QTimer sdlPump;
    QObject::connect(&sdlPump, &QTimer::timeout, []() {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            platformHandleSdlEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT && theWindow) theWindow->close();
        }
    });
    sdlPump.start(8);

    testStart();
    rc = app.exec();

    theWindow = NULL;
    platformShutdown();
    return rc;
}
