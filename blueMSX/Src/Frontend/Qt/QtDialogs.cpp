/*****************************************************************************
** QblueMSX - Qt frontend: file choosers, lists, messages and small dialogs
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
#include "Actions.h"
#include "AudioMixer.h"
#include "Casette.h"
#include "Emulator.h"
#include "Language.h"
#include "version.h"
}

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QSlider>
#include <QTableWidget>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QMessageBox>
#include <QUrl>
#include <QVBoxLayout>

#include <cstdlib>
#include <cstring>

namespace qbm {

static QString u8(const char* s) { return QString::fromUtf8(s ? s : ""); }

/* "Label|*.rom;*.ri|Label 2|*.zip" -> "Label (*.rom *.ri)", "Label 2 (*.zip)" */
static QStringList qtFilters(const char* filters)
{
    QStringList out;
    if (filters == NULL) return out;
    QStringList parts = u8(filters).split('|');
    for (int i = 0; i + 1 < parts.size(); i += 2) {
        QString patterns = parts[i + 1];
        patterns.replace(';', ' ');
        out << parts[i] + " (" + patterns + ")";
    }
    return out;
}

/* BLUEMSX_TEST runs need Qt's own dialog, which the script can drive. */
static bool useNativeDialogs()
{
    return getenv("BLUEMSX_TEST") == NULL;
}

int chooseFile(const FrontendFileRequest* req, char* out, int outSize)
{
    QFileDialog dlg(mainWindow(), u8(req->title), u8(req->defaultDir));
    if (req->directory) {
        dlg.setFileMode(QFileDialog::Directory);
        dlg.setOption(QFileDialog::ShowDirsOnly);
    }
    else if (req->save || req->allowCreate) {
        dlg.setFileMode(QFileDialog::AnyFile);
    }
    else {
        dlg.setFileMode(QFileDialog::ExistingFile);
    }
    dlg.setAcceptMode(req->save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    if (!useNativeDialogs()) dlg.setOption(QFileDialog::DontUseNativeDialog);
    QStringList filters = qtFilters(req->filters);
    if (!filters.isEmpty()) {
        filters << QString::fromUtf8(langFileAll()) + " (*)";
        dlg.setNameFilters(filters);
    }
    if (req->defaultName && *req->defaultName) dlg.selectFile(u8(req->defaultName));

    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return 0;
    QByteArray path = dlg.selectedFiles().first().toUtf8();
    if (path.size() >= outSize) return 0;
    memcpy(out, path.constData(), path.size() + 1);
    return 1;
}

int chooseFromList(FrontendListRequest* req)
{
    QDialog dlg(mainWindow());
    dlg.setWindowTitle(u8(req->title));
    auto* layout = new QVBoxLayout(&dlg);
    if (req->description) {
        auto* label = new QLabel(u8(req->description));
        label->setWordWrap(true);
        layout->addWidget(label);
    }
    if (req->zipName) {
        auto* zip = new QLabel(u8(fe::baseName(req->zipName)));
        zip->setEnabled(false);
        layout->addWidget(zip);
    }
    auto* list = new QListWidget;
    for (int i = 0; i < req->count; i++) list->addItem(u8(req->items[i]));
    list->setCurrentRow(req->selected >= 0 ? req->selected : 0);
    layout->addWidget(list);
    QCheckBox* reset = NULL;
    if (req->autostart >= 0) {
        reset = new QCheckBox(u8(langDlgZipReset()));
        reset->setChecked(req->autostart > 0);
        layout->addWidget(reset);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(u8(langDlgOK()));
    buttons->button(QDialogButtonBox::Cancel)->setText(u8(langDlgCancel()));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    dlg.resize(420, 360);
    list->setFocus();

    if (dlg.exec() != QDialog::Accepted || list->currentRow() < 0) return -1;
    if (reset) req->autostart = reset->isChecked() ? 1 : 0;
    return list->currentRow();
}

void showMessage(FrontendMessageKind kind, const QString& text)
{
    switch (kind) {
    case FRONTEND_MESSAGE_ERROR:
        QMessageBox::critical(mainWindow(), "QblueMSX", text);
        break;
    case FRONTEND_MESSAGE_WARNING:
        QMessageBox::warning(mainWindow(), "QblueMSX", text);
        break;
    default:
        if (mainWindow()) mainWindow()->screen()->showNotice(text);
        break;
    }
}

static void showAbout()
{
    QString text = QString("<h3>QblueMSX</h3>"
                           "<p>blueMSX+ %1, Qt %2</p>"
                           "<p>%3</p><p>%4</p>"
                           "<p>%5 GNU General Public License, version 2 or later.</p>"
                           "<p><a href=\"https://github.com/Hesoten/blueMSX-plus\">"
                           "https://github.com/Hesoten/blueMSX-plus</a></p>")
                       .arg(BLUE_MSX_VERSION, qVersion(), u8(langDlgAboutForkNote()).toHtmlEscaped(),
                            u8(langDlgAboutOrigDevel()).toHtmlEscaped(), u8(langDlgAboutLisence()).toHtmlEscaped());
    QMessageBox box(mainWindow());
    box.setWindowTitle(u8(langDlgAboutTitle()));
    box.setTextFormat(Qt::RichText);
    box.setText(text);
    box.exec();
}

/* ---- Language ---------------------------------------------------------------- */

static QDialog* languageDialog;

static void openLanguage()
{
    delete languageDialog;
    auto* dlg = languageDialog = new QDialog(mainWindow());
    dlg->setWindowTitle(u8(langDlgLangTitle()));
    auto* layout = new QVBoxLayout(dlg);
    layout->addWidget(new QLabel(u8(langDlgLangLangText())));
    auto* list = new QListWidget;
    Properties* p = platformGetProperties();
    for (int i = 0; ; i++) {
        EmuLanguageType t = langGetType(i);
        if (t == EMU_LANG_UNKNOWN) break;
        auto* item = new QListWidgetItem(u8(langToName(t, 1)));
        item->setData(Qt::UserRole, (int)t);
        list->addItem(item);
        if (p->language == t) list->setCurrentItem(item);
    }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(u8(langDlgOK()));
    buttons->button(QDialogButtonBox::Cancel)->setText(u8(langDlgCancel()));
    layout->addWidget(buttons);
    auto choose = [dlg, list]() {
        QListWidgetItem* item = list->currentItem();
        dlg->close();
        if (item == NULL) return;
        EmuLanguageType t = (EmuLanguageType)item->data(Qt::UserRole).toInt();
        post([t]() {
            Properties* q = platformGetProperties();
            if (q->language == t) return;
            Properties old = *q;
            q->language = t;
            langSetLanguage(t);
            platformApplyProperties(&old);
            platformSaveProperties();
            if (mainWindow()) {
                mainWindow()->rebuildMenuBar();
                mainWindow()->applyWindowSettings();    /* the skin has language-dependent tooltips */
            }
            refreshDialogs();
        });
    };
    QObject::connect(buttons, &QDialogButtonBox::accepted, dlg, choose);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    QObject::connect(list, &QListWidget::itemDoubleClicked, dlg, choose);
    dlg->resize(300, 420);
    dlg->show();
}

/* ---- Cassette position --------------------------------------------------------- */

static void openTape()
{
    static const char* const types[] = { "ASCII", "Binary", "Basic", "Custom" };
    bool suspended = false;
    if (emulatorGetState() == EMU_RUNNING) {
        emulatorSuspend();
        suspended = true;
    }

    QDialog dlg(mainWindow());
    dlg.setWindowTitle(u8(langDlgTapeTitle()));
    auto* layout = new QVBoxLayout(&dlg);
    auto* position = new QLabel;
    layout->addWidget(position);
    auto* table = new QTableWidget;
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({ u8(langDlgTabPosition()), u8(langDlgTabType()), u8(langDlgTabFilename()) });
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);

    int count = 0;
    TapeContent* tc = tapeGetContent(&count);
    table->setRowCount(tc ? count : 0);
    UInt32 cur = tapeGetCurrentPos();
    for (int i = 0; tc && i < count; i++) {
        char name[9];
        memcpy(name, tc[i].fileName, 8);
        name[8] = 0;
        table->setItem(i, 0, new QTableWidgetItem(QString::number(tc[i].pos)));
        table->setItem(i, 1, new QTableWidgetItem(types[tc[i].type & 3]));
        table->setItem(i, 2, new QTableWidgetItem(u8(name)));
        if ((UInt32)tc[i].pos == cur) table->selectRow(i);
    }
    auto showPos = [position]() {
        position->setText(QString("%1 %2 / %3").arg(u8(langDlgTapeCurrentPos()))
                              .arg(tapeGetCurrentPos()).arg(tapeGetLength()));
    };
    showPos();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(u8(langDlgOK()));
    buttons->button(QDialogButtonBox::Cancel)->setText(u8(langDlgCancel()));
    auto* rewind = buttons->addButton(u8(langMenuCasRewind()), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    QObject::connect(rewind, &QPushButton::clicked, [&]() {
        tapeSetCurrentPos(0);
        table->clearSelection();
        showPos();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(table, &QTableWidget::cellDoubleClicked, &dlg, &QDialog::accept);
    dlg.resize(420, 400);

    if (dlg.exec() == QDialog::Accepted && tc) {
        int row = table->currentRow();
        if (row >= 0 && row < count && table->selectionModel()->hasSelection()) tapeSetCurrentPos(tc[row].pos);
    }
    if (suspended) emulatorResume();
}

/* ---- Mixer ------------------------------------------------------------------------ */

class MixerDialog : public QDialog {
public:
    MixerDialog() : QDialog(mainWindow())
    {
        static const char* const names[MIXER_CHANNEL_TYPE_COUNT] = {
            "PSG", "SCC", "MSX-MUSIC", "MSX-AUDIO", "MoonSound", "Yamaha SFG", "Keyboard", "PCM", "I/O",
            "MIDI", "Cassette",
        };
        setWindowTitle(u8(langMenuToolsMixer()));
        auto* grid = new QGridLayout(this);
        Properties* p = platformGetProperties();

        auto* master = new QCheckBox("Master");
        auto* masterVol = new QSlider(Qt::Horizontal);
        masterVol->setRange(0, 100);
        auto* stereo = new QCheckBox("Stereo");
        grid->addWidget(master, 0, 0);
        grid->addWidget(masterVol, 0, 1);
        grid->addWidget(stereo, 0, 2);
        connect(master, &QCheckBox::toggled, [p](bool on) {
            p->sound.masterEnable = on;
            mixerEnableMaster(platformGetMixer(), on);
        });
        connect(masterVol, &QSlider::valueChanged, [p](int v) {
            p->sound.masterVolume = v;
            mixerSetMasterVolume(platformGetMixer(), v);
        });
        connect(stereo, &QCheckBox::toggled, []() { post([]() { actionVolumeToggleStereo(); refreshDialogs(); }); });
        syncs.push_back([=]() {
            QSignalBlocker a(master), b(masterVol), c(stereo);
            master->setChecked(p->sound.masterEnable != 0);
            masterVol->setValue(p->sound.masterVolume);
            stereo->setChecked(p->sound.stereo != 0);
        });

        auto* pan = new QLabel("Pan");
        pan->setAlignment(Qt::AlignCenter);
        grid->addWidget(pan, 1, 2);
        int row = 2;
        for (int i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
            if (names[i] == NULL) continue;
            MixerChannel* ch = &p->sound.mixerChannel[i];
            auto* on = new QCheckBox(names[i]);
            auto* vol = new QSlider(Qt::Horizontal);
            vol->setRange(0, 100);
            auto* bal = new QSlider(Qt::Horizontal);
            bal->setRange(0, 100);
            bal->setToolTip("Double click to centre");
            bal->installEventFilter(this);
            grid->addWidget(on, row, 0);
            grid->addWidget(vol, row, 1);
            grid->addWidget(bal, row, 2);
            row++;
            connect(on, &QCheckBox::toggled, [ch, i](bool v) {
                ch->enable = v;
                mixerEnableChannelType(platformGetMixer(), i, v);
            });
            connect(vol, &QSlider::valueChanged, [ch, i](int v) {
                ch->volume = v;
                mixerSetChannelTypeVolume(platformGetMixer(), i, v);
            });
            connect(bal, &QSlider::valueChanged, [ch, i](int v) {
                ch->pan = v;
                mixerSetChannelTypePan(platformGetMixer(), i, v);
            });
            syncs.push_back([=]() {
                QSignalBlocker a(on), b(vol), c(bal);
                on->setChecked(ch->enable != 0);
                vol->setValue(ch->volume);
                bal->setValue(ch->pan);
            });
        }
        grid->setColumnStretch(1, 3);
        grid->setColumnStretch(2, 1);
        connect(this, &QDialog::finished, []() { platformSaveProperties(); });
        resize(480, 0);
    }

    void sync() { for (auto& fn : syncs) fn(); }

protected:
    bool eventFilter(QObject* obj, QEvent* e) override
    {
        if (e->type() == QEvent::MouseButtonDblClick) {
            static_cast<QSlider*>(obj)->setValue(50);
            return true;
        }
        return QDialog::eventFilter(obj, e);
    }

private:
    std::vector<std::function<void()>> syncs;
};

static MixerDialog* mixerDialog;

static void openMixer()
{
    if (mixerDialog && !mixerDialog->isVisible()) {
        delete mixerDialog;
        mixerDialog = NULL;
    }
    if (mixerDialog == NULL) mixerDialog = new MixerDialog;
    mixerDialog->sync();
    mixerDialog->show();
    mixerDialog->raise();
    mixerDialog->activateWindow();
}

void refreshDialogs()
{
    refreshProperties();
    if (mixerDialog && mixerDialog->isVisible()) mixerDialog->sync();
}

/* ---- Dispatch ----------------------------------------------------------------------- */

/* ---- Create video clip: progress of a replay rendering to MP4 ---------------- */

static void openRenderProgress()
{
    auto* dlg = new QDialog(mainWindow());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowModality(Qt::ApplicationModal);
    dlg->setWindowTitle(QString::fromUtf8(langMenuVideoRender()));
    auto* layout = new QVBoxLayout(dlg);
    auto* text = new QLabel;
    auto* bar = new QProgressBar;
    bar->setRange(0, 1000);
    bar->setTextVisible(false);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8(langDlgCancel()));
    layout->addWidget(text);
    layout->addWidget(bar);
    layout->addWidget(buttons);
    dlg->resize(420, dlg->sizeHint().height());

    auto update = [dlg, text, bar]() {
        if (platformReplayRenderDone()) {
            platformReplayRenderStop(0);
            dlg->close();
            return;
        }
        int amount = platformReplayRenderProgress();
        if (amount < 0) {
            dlg->close();
            return;
        }
        bar->setValue(amount);
        text->setText(QString("%1 %2.%3%").arg(QString::fromUtf8(langDlgAmountCompleted()))
                          .arg(amount / 10).arg(amount % 10));
    };
    update();
    auto* timer = new QTimer(dlg);
    QObject::connect(timer, &QTimer::timeout, dlg, update);
    /* Cancel and the window's close button: stop, keep what was written. */
    QObject::connect(dlg, &QDialog::finished, [](int) {
        if (platformReplayRendering()) platformReplayRenderStop(1);
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    timer->start(100);
    dlg->show();
}

void openDialog(FrontendDialog dialog, int arg)
{
    switch (dialog) {
    case FRONTEND_DIALOG_PROPERTIES:   openProperties(arg); break;
    case FRONTEND_DIALOG_LANGUAGE:     openLanguage(); break;
    case FRONTEND_DIALOG_ABOUT:        showAbout(); break;
    case FRONTEND_DIALOG_HELP:
        QDesktopServices::openUrl(QUrl("https://github.com/Hesoten/blueMSX-plus#readme"));
        break;
    case FRONTEND_DIALOG_CASSETTE_POS: openTape(); break;
    case FRONTEND_DIALOG_SHORTCUTS:    openShortcutEditor(); break;
    case FRONTEND_DIALOG_MIXER:        openMixer(); break;
    case FRONTEND_DIALOG_RENDER_VIDEO: openRenderProgress(); break;
    default:
        if (mainWindow()) mainWindow()->screen()->showNotice("Not available yet in QblueMSX");
        break;
    }
}

}
