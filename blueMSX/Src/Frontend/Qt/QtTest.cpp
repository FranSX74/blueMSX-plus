/*****************************************************************************
** QblueMSX - scripted checks
**
** BLUEMSX_TEST="3 menu File/Cartridge Slot 1/Insert; 4 snap /tmp/a.png; 6 quit"
** Times are seconds from start-up. Commands:
**   menu A/B/C     trigger a menu item by its path of (translated) titles;
**                  the shortcut text after a tab is ignored
**   popup MENU     open a core popup menu (FrontendMenu number)
**   key NAME       press and release an MSX key (SDL scancode name)
**   qkey NAME      press and release a key in the focused Qt widget
**                  (Return, Escape, Tab, Up, Down, Left, Right, Space)
**   text STR       type text into the focused Qt widget
**   click X Y      left click at X,Y of the active window (logical pixels)
**   snap FILE      save the active window (dialog, popup or main) as an image
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
#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
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
    QList<QAction*> actions = mainWindow()->menuBar()->actions();
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
    if (QWidget* w = QApplication::activePopupWidget()) return w;
    if (QWidget* w = QApplication::activeModalWidget()) return w;
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
        QPointF local = target->mapFrom(top, pos);
        QPointF global = target->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(target, &press);
        QApplication::sendEvent(target, &release);
    }
    else if (st.cmd == "snap") {
        QWidget* w = activeWindow();
        if (!w->grab().save(st.arg)) {
            fprintf(stderr, "test: cannot save %s\n", st.arg.toUtf8().constData());
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
