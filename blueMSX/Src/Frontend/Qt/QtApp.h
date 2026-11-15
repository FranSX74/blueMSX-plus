/*****************************************************************************
** QblueMSX - Qt6 frontend for blueMSX+
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#ifndef QBLUEMSX_APP_H
#define QBLUEMSX_APP_H

extern "C" {
#include "Platform.h"
#include "Properties.h"
}

#include <QImage>
#include <QMainWindow>
#include <QOpenGLWidget>
#include <functional>

class QKeyEvent;
class QLabel;
class QMenu;
class QTimer;

namespace qbm {

#define QBM_RENDER_ZOOM 2
#define QBM_TEX_WIDTH   (PLATFORM_SCREEN_WIDTH * QBM_RENDER_ZOOM)
#define QBM_TEX_HEIGHT  (PLATFORM_SCREEN_HEIGHT * QBM_RENDER_ZOOM)

/* Runs `fn` on the UI thread once control is back in the event loop. Safe
** from any thread. */
void post(std::function<void()> fn);

int hidFromKeyEvent(const QKeyEvent* e);

/* ---- The emulated screen --------------------------------------------------- */

class ScreenWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit ScreenWidget(QWidget* parent = nullptr);

    /* Renders the latest frame (UI thread, on the core's request). */
    void uploadFrame();
    void setMouseCaptured(bool on);
    void showNotice(const QString& text);
    QRect screenRect() const { return screenArea; }

protected:
    void paintGL() override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    bool focusNextPrevChild(bool) override { return false; }   /* Tab is an MSX key */

private:
    QImage frame;
    QRect  screenArea;
    QPoint lastMouse;
    bool   captured = false;
    QLabel* notice;
    QTimer* noticeTimer;
};

/* ---- Main window -------------------------------------------------------------- */

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

    ScreenWidget* screen() const { return screenWidget; }
    void applyWindowSettings();
    void rebuildMenuBar();
    void popupMenu(FrontendMenu menu);
    void updateTitle();

protected:
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    ScreenWidget* screenWidget;
};

MainWindow* mainWindow();

/* Fills `menu` with one of the shared blueMSX menus. */
void fillMenu(QMenu* menu, FrontendMenu which);

/* ---- Dialogs (QtDialogs.cpp) ------------------------------------------------- */

int  chooseFile(const FrontendFileRequest* req, char* out, int outSize);
int  chooseFromList(FrontendListRequest* req);
void showMessage(FrontendMessageKind kind, const QString& text);
void openDialog(FrontendDialog dialog, int arg);

/* ---- Scripted checks (QtTest.cpp) --------------------------------------------- */

void testStart();

}

#endif
