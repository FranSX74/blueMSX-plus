/*****************************************************************************
** QblueMSX - scripted checks
**
** BLUEMSX_TEST="3 menu File/Cartridge Slot 1/Insert; 4 snap /tmp/a.png; 6 quit"
** Times are seconds from start-up. Commands:
**   menu A/B/C     trigger a menu item by its path of (translated) titles;
**                  the shortcut text after a tab is ignored
**   popup MENU     open a core popup menu (FrontendMenu number)
**   dialog N [ARG] open a dialog (FrontendDialog number, PropPage for 0)
**   key NAME       press and release an MSX key (SDL scancode name)
**   qkey NAME      press and release a key in the focused Qt widget
**                  (Return, Escape, Tab, Up, Down, Left, Right, Space)
**   text STR       type text into the focused Qt widget
**   click X Y      left click at X,Y of the active window (logical pixels)
**   combo N TEXT   choose the first item starting with TEXT in the Nth
**                  visible combo box of the active window (from 0)
**   shortcut SEQ   trigger the action of the active window bound to SEQ
**                  (F10, Ctrl+B... as QKeySequence spells it) or titled SEQ
**   focus NAME     give the keyboard focus to the widget named NAME
**   tab TEXT       show the tab titled TEXT (tab widgets, tabbed docks)
**   snap FILE      save the active window (dialog, popup or main) as an image
**   snapgl FILE    save what the emulated screen widget shows
**   render CAP MP4 render a replay to video (Create video clip, no file dialogs)
**   quit
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "QtApp.h"

#include <QAction>
#include <QComboBox>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QTabBar>
#include <QTimer>

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace qbm {

struct Step { double t; QString cmd, arg; };

static std::vector<Step> steps;
static size_t next;
static QElapsedTimer clock;

static QString plain(const QString& text)
{
    QString s = text.section('\t', 0, 0);
    s.replace("&&", "\x01").remove('&').replace("\x01", "&");
    return s.trimmed();
}

static QAction* findAction(const QList<QAction*>& actions, const QString& title)
{
    for (QAction* a : actions) {
        if (!a->isSeparator() && plain(a->text()) == title) return a;
    }
    return NULL;
}

static void triggerMenuPath(const QString& path)
{
    QStringList parts = path.split('/');
    QList<QAction*> actions = mainWindow()->menus()->actions();
    for (int i = 0; i < parts.size(); i++) {
        QAction* a = findAction(actions, parts[i]);
        if (a == NULL) {
            fprintf(stderr, "test: no menu item '%s'\n", parts[i].toUtf8().constData());
            return;
        }
        if (QMenu* m = a->menu()) {
            Q_EMIT m->aboutToShow();          /* fills the menu */
            actions = m->actions();
            continue;
        }
        if (i != parts.size() - 1) {
            fprintf(stderr, "test: '%s' is not a submenu\n", parts[i].toUtf8().constData());
            return;
        }
        a->trigger();
    }
}

static QWidget* activeWindow()
{
    /* Wayland may not report a modeless dialog as active: take the newest
    ** visible top-level dialog. */
    QWidget* dialog = NULL;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w->isVisible() && w->isWindow() && w != mainWindow() && qobject_cast<QDialog*>(w)) dialog = w;
    }
    if (QWidget* w = QApplication::activePopupWidget()) return w;
    if (QWidget* w = QApplication::activeModalWidget()) return w;
    if (dialog) return dialog;
    if (QWidget* w = QApplication::activeWindow()) return w;
    return mainWindow();
}

static void sendKey(QWidget* w, int key, const QString& text = QString())
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &release);
}

static void run(const Step& st)
{
    if (st.cmd == "menu") {
        triggerMenuPath(st.arg);
    }
    else if (st.cmd == "popup") {
        mainWindow()->popupMenu((FrontendMenu)st.arg.toInt());
    }
    else if (st.cmd == "dialog") {
        openDialog((FrontendDialog)st.arg.section(' ', 0, 0).toInt(), st.arg.section(' ', 1, 1).toInt());
    }
    else if (st.cmd == "key") {
        SDL_Scancode sc = SDL_GetScancodeFromName(st.arg.toUtf8().constData());
        platformKeyEvent(sc, 1);
        QTimer::singleShot(100, [sc]() { platformKeyEvent(sc, 0); });
    }
    else if (st.cmd == "qkey") {
        static const struct { const char* name; int key; } keys[] = {
            { "Return", Qt::Key_Return }, { "Escape", Qt::Key_Escape }, { "Tab", Qt::Key_Tab },
            { "Up", Qt::Key_Up }, { "Down", Qt::Key_Down }, { "Left", Qt::Key_Left },
            { "Right", Qt::Key_Right }, { "Space", Qt::Key_Space },
        };
        QWidget* w = QApplication::focusWidget() ? QApplication::focusWidget() : activeWindow();
        for (const auto& k : keys) {
            if (st.arg == k.name) sendKey(w, k.key);
        }
    }
    else if (st.cmd == "text") {
        QWidget* w = QApplication::focusWidget() ? QApplication::focusWidget() : activeWindow();
        for (QChar c : st.arg) sendKey(w, 0, QString(c));
    }
    else if (st.cmd == "click") {
        QStringList xy = st.arg.split(' ');
        QWidget* top = activeWindow();
        QPoint pos(xy.value(0).toInt(), xy.value(1).toInt());
        QWidget* target = top->childAt(pos);
        if (target == NULL) target = top;
        if (getenv("BLUEMSX_TEST_VERBOSE")) fprintf(stderr, "test: click on %s\n", target->metaObject()->className());
        QPointF local = target->mapFrom(top, pos);
        QPointF global = target->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(target, &press);
        QApplication::sendEvent(target, &release);
    }
    else if (st.cmd == "combo") {
        int n = st.arg.section(' ', 0, 0).toInt();
        QString text = st.arg.section(' ', 1);
        int seen = 0;
        for (QComboBox* box : activeWindow()->findChildren<QComboBox*>()) {
            if (!box->isVisibleTo(activeWindow()) || seen++ != n) continue;
            int i = box->findText(text, Qt::MatchStartsWith);
            if (i < 0) {
                fprintf(stderr, "test: no item %s\n", text.toUtf8().constData());
                break;
            }
            box->setCurrentIndex(i);
            Q_EMIT box->activated(i);
            break;
        }
    }
    else if (st.cmd == "shortcut") {
        QKeySequence seq(st.arg);
        bool found = false;
        for (QAction* a : activeWindow()->findChildren<QAction*>()) {
            if (a->shortcut() == seq || a->text().remove('&') == st.arg) {
                if (a->isEnabled()) QTimer::singleShot(0, a, &QAction::trigger);   /* may open a modal dialog */
                else fprintf(stderr, "test: %s is disabled\n", st.arg.toUtf8().constData());
                found = true;
                break;
            }
        }
        if (!found) fprintf(stderr, "test: no action for %s\n", st.arg.toUtf8().constData());
    }
    else if (st.cmd == "focus") {
        QWidget* w = activeWindow()->findChild<QWidget*>(st.arg);
        if (w) w->setFocus(Qt::OtherFocusReason);
        else fprintf(stderr, "test: no widget %s\n", st.arg.toUtf8().constData());
    }
    else if (st.cmd == "tab") {
        bool found = false;
        for (QTabBar* bar : activeWindow()->findChildren<QTabBar*>()) {
            for (int i = 0; i < bar->count() && !found; i++) {
                if (bar->tabText(i).remove('&') == st.arg) {
                    bar->setCurrentIndex(i);
                    found = true;
                }
            }
        }
        if (!found) fprintf(stderr, "test: no tab %s\n", st.arg.toUtf8().constData());
    }
    else if (st.cmd == "snap") {
        QWidget* w = activeWindow();
        if (!w->grab().save(st.arg)) {
            fprintf(stderr, "test: cannot save %s\n", st.arg.toUtf8().constData());
        }
    }
    else if (st.cmd == "snapgl") {
        mainWindow()->screen()->grabFramebuffer().save(st.arg);
    }
    else if (st.cmd == "render") {
        QStringList files = st.arg.split(' ');
        if (platformReplayRenderStart(files.value(0).toUtf8().constData(), files.value(1).toUtf8().constData())) {
            openDialog(FRONTEND_DIALOG_RENDER_VIDEO, 0);
        }
    }
    else if (st.cmd == "quit") {
        QApplication::closeAllWindows();
        qApp->quit();
    }
}

void testStart()
{
    const char* env = getenv("BLUEMSX_TEST");
    if (env == NULL) return;
    for (const QString& part : QString::fromUtf8(env).split(';', Qt::SkipEmptyParts)) {
        QString s = part.trimmed();
        QString t = s.section(' ', 0, 0);
        QString cmd = s.section(' ', 1, 1);
        QString arg = s.section(' ', 2);
        if (!cmd.isEmpty()) steps.push_back(Step{ t.toDouble(), cmd, arg });
    }
    clock.start();
    auto* timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, []() {
        double now = clock.elapsed() / 1000.0;
        while (next < steps.size() && steps[next].t <= now) {
            Step st = steps[next++];
            run(st);    /* may block in a modal dialog; the timer keeps firing there */
        }
    });
    timer->start(50);
}

}
