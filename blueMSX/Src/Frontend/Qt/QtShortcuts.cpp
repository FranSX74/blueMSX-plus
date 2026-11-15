/*****************************************************************************
** QblueMSX - shortcut editor
**
** Edits the active profile in place (each action has up to three keys) and
** saves it in the Windows profile format, like the ImGui editor.
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
#include "Language.h"
}

#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstring>

namespace qbm {

static QString u8(const char* s) { return QString::fromUtf8(s ? s : ""); }

static bool isModifier(int hid) { return hid >= 0xe0 && hid <= 0xe7; }

class ShortcutDialog : public QDialog {
public:
    ShortcutDialog() : QDialog(mainWindow())
    {
        setWindowTitle(u8(langShortcutConfigTitle()));
        auto* layout = new QVBoxLayout(this);

        auto* top = new QHBoxLayout;
        top->addWidget(new QLabel(u8(langShortcutScheme())));
        profile = new QComboBox;
        top->addWidget(profile, 1);
        save = new QPushButton(u8(langDlgSave()));
        revert = new QPushButton(u8(langDlgCancel()));
        revert->setToolTip(u8(langShortcutDiscardConfig()));
        auto* saveAs = new QPushButton(u8(langDlgSaveAs()));
        saveAs->setToolTip(u8(langShortcutSaveConfigAs()));
        top->addWidget(save);
        top->addWidget(revert);
        top->addWidget(saveAs);
        layout->addLayout(top);

        filter = new QLineEdit;
        filter->setPlaceholderText("Filter");
        filter->setClearButtonEnabled(true);
        layout->addWidget(filter);
        auto* help = new QLabel("Click a key to change it; Delete or right click clears it. Esc cancels.");
        help->setEnabled(false);
        layout->addWidget(help);

        table = new QTableWidget(SC_COUNT, 1 + SHORTCUT_MAX_BINDINGS);
        QStringList headers{ u8(langShortcutDescription()) };
        for (int b = 0; b < SHORTCUT_MAX_BINDINGS; b++) headers << u8(langShortcutKey());
        table->setHorizontalHeaderLabels(headers);
        table->verticalHeader()->hide();
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setSelectionBehavior(QAbstractItemView::SelectItems);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int b = 1; b <= SHORTCUT_MAX_BINDINGS; b++) {
            table->horizontalHeader()->setSectionResizeMode(b, QHeaderView::Fixed);
            table->setColumnWidth(b, 130);
        }
        table->setContextMenuPolicy(Qt::CustomContextMenu);
        layout->addWidget(table, 1);

        connect(profile, &QComboBox::activated, [this]() {
            Properties* p = platformGetProperties();
            snprintf(p->emulation.shortcutProfile, sizeof(p->emulation.shortcutProfile), "%s",
                     profile->currentText().toUtf8().constData());
            platformShortcutsLoad(p->emulation.shortcutProfile);
            dirty = false;
            refresh();
        });
        connect(save, &QPushButton::clicked, [this]() {
            platformShortcutsSave(platformGetProperties()->emulation.shortcutProfile);
            dirty = false;
            refresh();
        });
        connect(revert, &QPushButton::clicked, [this]() {
            platformShortcutsLoad(platformGetProperties()->emulation.shortcutProfile);
            dirty = false;
            refresh();
        });
        connect(saveAs, &QPushButton::clicked, [this]() {
            bool ok = false;
            QString name = QInputDialog::getText(this, u8(langShortcutSaveConfigAs()),
                                                 u8(langShortcutConfigName()), QLineEdit::Normal, QString(), &ok);
            QByteArray n = name.trimmed().toUtf8();
            if (!ok || n.isEmpty() || n.contains('/')) return;
            platformShortcutsSave(n.constData());
            Properties* p = platformGetProperties();
            snprintf(p->emulation.shortcutProfile, sizeof(p->emulation.shortcutProfile), "%s", n.constData());
            dirty = false;
            refresh();
        });
        connect(filter, &QLineEdit::textChanged, [this]() { applyFilter(); });
        connect(table, &QTableWidget::cellClicked, [this](int row, int col) {
            if (col > 0) startWaiting(row, col - 1);
        });
        connect(table, &QTableWidget::customContextMenuRequested, [this](const QPoint& pos) {
            QModelIndex i = table->indexAt(pos);
            if (i.isValid() && i.column() > 0) clear(i.row(), i.column() - 1);
        });
        resize(760, 620);
    }

    void refresh()
    {
        Properties* p = platformGetProperties();
        char names[32][64];
        int n = platformShortcutsListProfiles(names, 32);
        profile->clear();
        for (int i = 0; i < n; i++) profile->addItem(u8(names[i]));
        profile->setCurrentText(u8(p->emulation.shortcutProfile));
        save->setEnabled(dirty);
        revert->setEnabled(dirty);

        for (int i = 0; i < SC_COUNT; i++) {
            auto* desc = new QTableWidgetItem(u8(fe::shortcutDescription(i)));
            table->setItem(i, 0, desc);
            PlatformHotkeySet* set = platformShortcutGet((ShortcutId)i);
            for (int b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
                const PlatformHotkey& h = set->slots[b];
                bool waiting = waitId == i && waitSlot == b;
                QString label = waiting ? u8(langShortcutPressText()) :
                                h.type == HOTKEY_TYPE_KEYBOARD ? u8(platformHotkeyToString(&h)) : "-";
                auto* item = new QTableWidgetItem(label);
                item->setTextAlignment(Qt::AlignCenter);
                QString clash = conflicts(i, h);
                if (!clash.isEmpty()) {
                    item->setForeground(QColor(230, 150, 40));
                    item->setToolTip(u8(langShortcutTooltipAlsoBound()) + "\n" + clash);
                }
                if (waiting) {
                    QFont f = item->font();
                    f.setBold(true);
                    item->setFont(f);
                }
                table->setItem(i, 1 + b, item);
            }
        }
        applyFilter();
    }

protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        if (waitId < 0) {
            QModelIndex i = table->currentIndex();
            if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && i.isValid() && i.column() > 0) {
                clear(i.row(), i.column() - 1);
                return;
            }
            QDialog::keyPressEvent(e);
            return;
        }
        if (e->isAutoRepeat()) return;
        int hid = hidFromKeyEvent(e);
        if (hid == 0) return;
        if (isModifier(hid)) {
            platformShortcutKey(hid, 1);    /* track the modifier, wait for a key */
            return;
        }
        int mods = platformShortcutMods();
        if (e->key() == Qt::Key_Escape && mods == 0) {
            stopWaiting();
            return;
        }
        PlatformHotkeySet* set = platformShortcutGet((ShortcutId)waitId);
        if (set != NULL) {
            set->slots[waitSlot].type = HOTKEY_TYPE_KEYBOARD;
            set->slots[waitSlot].mods = mods;
            set->slots[waitSlot].key  = hid;
            dirty = true;
        }
        stopWaiting();
    }

    void keyReleaseEvent(QKeyEvent* e) override
    {
        int hid = hidFromKeyEvent(e);
        if (isModifier(hid)) platformShortcutKey(hid, 0);
        QDialog::keyReleaseEvent(e);
    }

    void hideEvent(QHideEvent* e) override
    {
        if (waitId >= 0) stopWaiting();
        QDialog::hideEvent(e);
    }

private:
    void startWaiting(int row, int slot)
    {
        waitId = row;
        waitSlot = slot;
        platformShortcutsReleaseAll();
        grabKeyboard();
        refresh();
    }

    void stopWaiting()
    {
        waitId = -1;
        releaseKeyboard();
        platformShortcutsReleaseAll();
        refresh();
    }

    void clear(int row, int slot)
    {
        PlatformHotkeySet* set = platformShortcutGet((ShortcutId)row);
        set->slots[slot] = PlatformHotkey{ HOTKEY_TYPE_NONE, 0, 0 };
        dirty = true;
        refresh();
    }

    /* Other actions already using this key. */
    static QString conflicts(int self, const PlatformHotkey& h)
    {
        QStringList out;
        if (h.type != HOTKEY_TYPE_KEYBOARD) return QString();
        for (int i = 0; i < SC_COUNT; i++) {
            if (i == self) continue;
            const PlatformHotkeySet* s = platformShortcutGet((ShortcutId)i);
            for (const auto& o : s->slots) {
                if (o.type == h.type && o.key == h.key && o.mods == h.mods) {
                    out << u8(fe::shortcutDescription(i));
                }
            }
        }
        return out.join('\n');
    }

    void applyFilter()
    {
        QString f = filter->text().trimmed();
        for (int i = 0; i < SC_COUNT; i++) {
            QTableWidgetItem* item = table->item(i, 0);
            table->setRowHidden(i, !f.isEmpty() && item && !item->text().contains(f, Qt::CaseInsensitive));
        }
    }

    QComboBox*    profile;
    QPushButton*  save;
    QPushButton*  revert;
    QLineEdit*    filter;
    QTableWidget* table;
    bool dirty = false;
    int  waitId = -1;
    int  waitSlot = 0;
};

static ShortcutDialog* shortcutDialog;

void openShortcutEditor()
{
    if (shortcutDialog && !shortcutDialog->isVisible()) {
        delete shortcutDialog;
        shortcutDialog = NULL;
    }
    if (shortcutDialog == NULL) shortcutDialog = new ShortcutDialog;
    shortcutDialog->refresh();
    shortcutDialog->show();
    shortcutDialog->raise();
    shortcutDialog->activateWindow();
}

}
