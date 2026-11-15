/*****************************************************************************
** QblueMSX - properties
**
** The same pages as the ImGui frontend, on tabs. Changes apply as they are
** made; settings are saved when the window closes. Each control reads its
** value back from the Properties when anything changes, so controls that
** depend on others (and changes made from menus or shortcuts) stay in step.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "QtApp.h"

extern "C" {
#include "PlatformShortcuts.h"
#include "Actions.h"
#include "ArrayList.h"
#include "Emulator.h"
#include "FileHistory.h"
#include "Language.h"
#include "Machine.h"
#include "PrinterIO.h"
}

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <cstring>

namespace qbm {

static QString u8(const char* s) { return QString::fromUtf8(s ? s : ""); }

/* Runs `fn` on the properties and applies what changed. */
static void change(const std::function<void(Properties*)>& fn)
{
    Properties* p = platformGetProperties();
    Properties old = *p;
    fn(p);
    if (memcmp(&old, p, sizeof(Properties)) != 0) platformApplyProperties(&old);
    refreshDialogs();
}

/* A slider that goes back to `reset` on a right click. */
class ResetSlider : public QSlider {
public:
    ResetSlider(int reset) : QSlider(Qt::Horizontal), resetValue(reset) {}
protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::RightButton && resetValue >= 0) {
            setValue(resetValue);
            return;
        }
        QSlider::mousePressEvent(e);
    }
private:
    int resetValue;
};

/* ---- Page builder ------------------------------------------------------------- */

class Page : public QScrollArea {
public:
    explicit Page(std::vector<std::function<void()>>* syncs) : syncs(syncs)
    {
        auto* body = new QWidget;
        column = new QVBoxLayout(body);
        setWidget(body);
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    void finish() { column->addStretch(1); }

    QFormLayout* group(const char* title)
    {
        auto* box = new QGroupBox(u8(title));
        form = new QFormLayout(box);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        column->addWidget(box);
        return form;
    }

    void onSync(std::function<void()> fn)
    {
        fn();
        syncs->push_back(std::move(fn));
    }

    QCheckBox* check(const char* label, int* value, std::function<bool()> enabled = nullptr)
    {
        auto* box = new QCheckBox(u8(label));
        form->addRow(box);
        QObject::connect(box, &QCheckBox::toggled, [value](bool on) {
            change([value, on](Properties*) { *value = on ? 1 : 0; });
        });
        onSync([box, value, enabled]() {
            QSignalBlocker b(box);
            box->setChecked(*value != 0);
            if (enabled) box->setEnabled(enabled());
        });
        return box;
    }

    QComboBox* combo(const char* label, int* value, const QStringList& items,
                     std::function<bool()> enabled = nullptr)
    {
        auto* box = new QComboBox;
        box->addItems(items);
        form->addRow(u8(label), box);
        QObject::connect(box, &QComboBox::activated, [value](int i) {
            change([value, i](Properties*) { *value = i; });
        });
        onSync([box, value, enabled]() {
            QSignalBlocker b(box);
            box->setCurrentIndex(*value >= 0 && *value < box->count() ? *value : 0);
            if (enabled) box->setEnabled(enabled());
        });
        return box;
    }

    /* `text` formats the value shown beside the slider; `visible` hides the row. */
    QSlider* slider(const char* label, int* value, int lo, int hi, int reset,
                    std::function<QString(int)> text, std::function<bool()> visible = nullptr)
    {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* s = new ResetSlider(reset);
        s->setRange(lo, hi);
        auto* shown = new QLabel;
        shown->setMinimumWidth(shown->fontMetrics().horizontalAdvance("00.000 MHz (000%)"));
        h->addWidget(s, 1);
        h->addWidget(shown);
        if (reset >= 0) s->setToolTip("Right click to reset");
        form->addRow(u8(label), row);
        QWidget* labelWidget = form->labelForField(row);
        QObject::connect(s, &QSlider::valueChanged, [value, shown, text](int v) {
            shown->setText(text(v));
            change([value, v](Properties*) { *value = v; });
        });
        onSync([s, shown, value, text, visible, row, labelWidget]() {
            QSignalBlocker b(s);
            s->setValue(*value);
            shown->setText(text(*value));
            if (visible) {
                row->setVisible(visible());
                if (labelWidget) labelWidget->setVisible(visible());
            }
        });
        return s;
    }

    QLineEdit* text(const char* label, char* value, int size, std::function<bool()> visible = nullptr)
    {
        auto* edit = new QLineEdit;
        form->addRow(u8(label), edit);
        QWidget* labelWidget = form->labelForField(edit);
        QObject::connect(edit, &QLineEdit::editingFinished, [edit, value, size]() {
            QByteArray s = edit->text().toUtf8();
            change([&](Properties*) { snprintf(value, size, "%s", s.constData()); });
        });
        onSync([edit, value, visible, labelWidget]() {
            if (!edit->hasFocus()) edit->setText(u8(value));
            if (visible) {
                edit->setVisible(visible());
                if (labelWidget) labelWidget->setVisible(visible());
            }
        });
        return edit;
    }

    QPushButton* button(const QString& label, std::function<void()> fn)
    {
        auto* b = new QPushButton(label);
        QObject::connect(b, &QPushButton::clicked, [fn]() { post(fn); });
        form->addRow(b);
        return b;
    }

    QVBoxLayout* column;
    QFormLayout* form = nullptr;
    std::vector<std::function<void()>>* syncs;
};

static QString pct(int v) { return QString("%1%").arg(v); }
static QString num(int v) { return QString::number(v); }

/* ---- Pages -------------------------------------------------------------------- */

static QWidget* pageEmulation(std::vector<std::function<void()>>* syncs)
{
    Properties* p = platformGetProperties();
    auto* page = new Page(syncs);

    QFormLayout* form = page->group(langPropEmuGeneralGB());
    auto* machine = new QComboBox;
    {
        ArrayList* list = arrayListCreate();
        machineFillAvailable(list, 1);
        for (int i = 0; i < arrayListGetSize(list); i++) {
            machine->addItem(u8((const char*)arrayListGetObject(list, i)));
        }
        arrayListDestroy(list);
    }
    auto* reset = new QPushButton(u8(langMenuRunHardReset()));
    reset->setToolTip("The machine changes on the next hard reset.");
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(machine, 1);
    h->addWidget(reset);
    form->addRow(u8(langPropEmuFamilyText()), row);
    QObject::connect(machine, &QComboBox::activated, [machine]() {
        QByteArray name = machine->currentText().toUtf8();
        change([&](Properties* q) {
            snprintf(q->emulation.machineName, sizeof(q->emulation.machineName), "%s", name.constData());
        });
    });
    QObject::connect(reset, &QPushButton::clicked, []() { post([]() { actionEmuResetHard(); }); });
    page->onSync([machine, reset, p]() {
        QSignalBlocker b(machine);
        machine->setCurrentText(u8(p->emulation.machineName));
        reset->setVisible(emulatorGetState() != EMU_STOPPED);
    });

    page->group(langPropEmuSpeedGB());
    page->slider(langPropEmuSpeedText(), &p->emulation.speed, 0, 100, 50, [](int v) {
        int hz = emulatorLogFrequencyToHz(v);
        return QString::asprintf("%d.%03d MHz (%d%%)", hz / 1000000, (hz / 1000) % 1000, hz * 10 / 357954);
    });
    page->slider(langPropEmuVdpCmdSpeedText(), &p->emulation.vdpCmdSpeed, 0, 100, -1, pct);
    page->combo(langPropVideoFreqText(), &p->emulation.vdpSyncMode,
                { u8(langPropVideoFreqAuto()), "50 Hz", "60 Hz" });

    page->group(langPropEmuBoostText());
    page->check(langPropEmuFdcTiming(), &p->emulation.enableFdcTiming);
    page->check(langPropEmuHddSdBoost(), &p->emulation.enableHddSdBoost);
    page->check(langPropEmuCasBoost(), &p->emulation.enableCasBoost);
    page->check(langPropEmuNoSpriteLimits(), &p->emulation.noSpriteLimits);
    page->check(langPropEmuReversePlay(), &p->emulation.reverseEnable);

    page->group(langPropEmuFrontSwitchGB());
    page->check(langPropEmuFrontSwitch(), &p->emulation.frontSwitch);
    page->check(langPropEmuPauseSwitch(), &p->emulation.pauseSwitch);
    page->check(langPropEmuAudioSwitch(), &p->emulation.audioSwitch);
    page->check(langShortcutEnableMsxKeyboardQuirk(), &p->keyboard.enableKeyboardQuirk);

    page->finish();
    return page;
}

static QWidget* pageVideo(std::vector<std::function<void()>>* syncs)
{
    Properties* p = platformGetProperties();
    auto* page = new Page(syncs);

    page->group(langPropMonMonGB());
    page->combo(langPropMonTypeText(), &p->video.monitorColor,
                { u8(langEnumVideoMonColor()), u8(langEnumVideoMonGrey()), u8(langEnumVideoMonGreen()),
                  u8(langEnumVideoMonAmber()) });
    page->combo(langPropMonEmuText(), &p->video.monitorType,
                { u8(langEnumVideoEmuNone()), u8(langEnumVideoEmuMonitor()), u8(langEnumVideoEmuYc()),
                  u8(langEnumVideoEmuYcBlur()), u8(langEnumVideoEmuComp()), u8(langEnumVideoEmuCompBlur()),
                  u8(langEnumVideoEmuScale2x()), u8(langEnumVideoEmuHq2x()) });
    page->combo(langPropPerfFrameSkipText(), &p->video.frameSkip,
                { u8(langEnumVideoFrameskip0()), u8(langEnumVideoFrameskip1()), u8(langEnumVideoFrameskip2()),
                  u8(langEnumVideoFrameskip3()), u8(langEnumVideoFrameskip4()), u8(langEnumVideoFrameskip5()) });

    page->group(langPropMonEffectsGB());
    page->check(langPropMonHorizStretch(), &p->video.horizontalStretch);
    page->check(langPropMonVertStretch(), &p->video.verticalStretch);
    page->check(langPropMonDeInterlace(), &p->video.deInterlace);
    page->check(langPropMonBlendFrames(), &p->video.blendFrames);
    page->check(langPropMonScanlines(), &p->video.scanlinesEnable);
    page->slider(langPropMonScanlinesDepth(), &p->video.scanlinesPct, 0, 100, -1, pct,
                 [p]() { return p->video.scanlinesEnable != 0; });
    page->check(langPropMonColorGhosting(), &p->video.colorSaturationEnable);
    page->slider("", &p->video.colorSaturationWidth, 0, 4, -1, num,
                 [p]() { return p->video.colorSaturationEnable != 0; });

    page->group(langPropVideo());
    page->slider(langPropMonBrightness(), &p->video.brightness, 0, 200, 100, num);
    page->slider(langPropMonContrast(), &p->video.contrast, 0, 200, 100, num);
    page->slider(langPropMonSaturation(), &p->video.saturation, 0, 200, 100, num);
    page->slider(langPropMonGamma(), &p->video.gamma, 0, 200, 100, num);

    page->finish();
    return page;
}

static QWidget* pageSound(std::vector<std::function<void()>>* syncs)
{
    static const int bufSizes[] = { 5, 10, 15, 25, 50, 75, 100, 150, 200 };
    /* Same labels as the Win32 dialog, indexed by PROP_*_BACKEND_*. */
    static const char* const ym[] = { "openMSX", "original blueMSX", "emu2413", "Nuked OPLL" };
    static const char* const y8950[] = { "original blueMSX", "emu8950", "openMSX" };
    Properties* p = platformGetProperties();
    auto* page = new Page(syncs);

    QFormLayout* form = page->group(langPropPerfAudioDrvGB());
    auto* buf = new QComboBox;
    for (int ms : bufSizes) buf->addItem(QString("%1 ms").arg(ms));
    form->addRow(u8(langPropPerfAudioBufSzText()), buf);
    QObject::connect(buf, &QComboBox::activated, [](int i) {
        change([i](Properties* q) { q->sound.bufSize = bufSizes[i]; });
    });
    page->onSync([buf, p]() {
        int idx = 0;
        while (idx < 8 && p->sound.bufSize > bufSizes[idx]) idx++;
        QSignalBlocker b(buf);
        buf->setCurrentIndex(idx);
    });
    page->check("Stereo", &p->sound.stereo);
    page->check("Master", &p->sound.masterEnable);
    page->slider("Master", &p->sound.masterVolume, 0, 100, -1, num);
    page->button(u8(langMenuToolsMixer()), []() { actionToolsShowMixer(); });

    page->group(langPropSndChipEmuGB());
    auto stopped = []() { return emulatorGetState() == EMU_STOPPED; };
    page->check(langPropSndMsxMusic(), &p->sound.chip.enableYM2413, stopped);
    page->check(langPropSndMsxAudio(), &p->sound.chip.enableY8950, stopped);
    page->check(langPropSndMoonsound(), &p->sound.chip.enableMoonsound, stopped);
    auto* hint = new QLabel(u8(langPropSoundChipsHint()));
    hint->setWordWrap(true);
    hint->setEnabled(false);
    page->form->addRow(hint);
    page->onSync([hint, stopped]() { hint->setVisible(!stopped()); });

    form = page->group(langPropSoundChipsActive());
    auto backendRow = [&](const char* title, std::function<QString()> name, std::function<void()> cycle) {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel;
        auto* next = new QToolButton;
        next->setText(">");
        h->addWidget(label, 1);
        h->addWidget(next);
        form->addRow(title, row);
        QObject::connect(next, &QToolButton::clicked, [cycle]() {
            post([cycle]() { cycle(); refreshDialogs(); });
        });
        page->onSync([label, name]() { label->setText(name()); });
    };
    backendRow("MSX-MUSIC", [p]() {
        int a = p->sound.chip.ym2413BackendActive;
        return QString(a >= 0 && a < 4 ? ym[a] : "?");
    }, []() { actionYm2413BackendCycle(); });
    backendRow("MSX-AUDIO", [p]() {
        int a = p->sound.chip.y8950BackendActive;
        return QString(a >= 0 && a < 3 ? y8950[a] : "?");
    }, []() { actionY8950BackendCycle(); });

    /* The original blueMSX OPLL has its own output filter, so the analog
    ** filter is bypassed with it (YM2413.cpp), as the Win32 dialog shows. */
    auto filterUsed = [p]() { return p->sound.chip.ym2413BackendActive != PROP_YM2413_BACKEND_OPENMSX_2; };
    QComboBox* filter = page->combo(langPropSndOpllAnalogText(), &p->sound.chip.ym2413AnalogFilterMode,
        { u8(langEnumOpllFilterOff()), u8(langEnumOpllFilterBright()), u8(langEnumOpllFilterClear()),
          u8(langEnumOpllFilterStandard()), u8(langEnumOpllFilterSoft()), u8(langEnumOpllFilterMellow()),
          u8(langEnumOpllFilterCustom()) }, filterUsed);
    page->onSync([filter, filterUsed]() {
        filter->setToolTip(filterUsed() ? QString() : "Not used with the original blueMSX MSX-MUSIC backend.");
    });
    page->slider(langPropSndOpllAnalogLpfText(), &p->sound.chip.ym2413AnalogFilterLpfHz, 1000, 15000, -1,
                 [](int v) { return QString("%1 Hz").arg(v); },
                 [p]() { return p->sound.chip.ym2413AnalogFilterMode == PROP_OPLL_FILTER_CUSTOM; });

    page->finish();
    return page;
}

static QWidget* pagePorts(std::vector<std::function<void()>>* syncs)
{
    Properties* p = platformGetProperties();
    auto* page = new Page(syncs);

    page->group(langPropPortsLptGB());
    page->combo(langPropPortsLptText(), &p->ports.Lpt.type,
                { u8(langPropPortsNone()), u8(langPropPortsSimplCovox()), u8(langPropPortsFile()) });
    page->text(langPropPortsFile(), p->ports.Lpt.fileName, sizeof(p->ports.Lpt.fileName),
               [p]() { return p->ports.Lpt.type == P_LPT_FILE; });
    QPushButton* browse = page->button("...", []() {
        FrontendFileRequest req = {};
        char out[512];
        req.kind = FRONTEND_FILE_SAVE_GENERIC;
        req.save = 1;
        req.allowCreate = 1;
        req.title = langPropPortsOpenLogFile();
        if (chooseFile(&req, out, sizeof(out))) {
            change([&](Properties* q) { snprintf(q->ports.Lpt.fileName, sizeof(q->ports.Lpt.fileName), "%s", out); });
        }
    });
    page->onSync([browse, p]() { browse->setVisible(p->ports.Lpt.type == P_LPT_FILE); });
    page->combo(langPropPortsEmulateMsxPrn(), &p->ports.Lpt.emulation,
                { "Raw", "MSX printer", "SVI printer", "Epson FX-80" });

    page->group(langPropPortsComGB());
    page->combo(langPropPortsCom1Text(), &p->ports.Com.type, { u8(langPropPortsNone()), u8(langPropPortsComFile()) });
    page->text(langPropPortsFile(), p->ports.Com.fileName, sizeof(p->ports.Com.fileName),
               [p]() { return p->ports.Com.type == P_COM_FILE; });

    page->group(langPropSndMidiOutGB());
    page->combo("MIDI out", &p->sound.MidiOut.type, { u8(langPropPortsNone()), u8(langPropPortsFile()) });
    page->text(langPropPortsFile(), p->sound.MidiOut.fileName, sizeof(p->sound.MidiOut.fileName),
               [p]() { return p->sound.MidiOut.type == P_MIDI_FILE; });

    page->finish();
    return page;
}

static QWidget* pageSettings(std::vector<std::function<void()>>* syncs)
{
    Properties* p = platformGetProperties();
    auto* page = new Page(syncs);

    page->group(langPropSetFileHistoryGB());
    page->slider(langPropSetFileHistorySize(), &p->filehistory.count, 0, MAX_HISTORY, -1, num);
    page->button(u8(langPropSetFileHistoryClear()), []() {
        if (QMessageBox::question(mainWindow(), u8(langPropSetFileHistoryGB()), u8(langPropClearFileHistory()))
            != QMessageBox::Yes) return;
        change([](Properties* q) {
            for (int i = 0; i < MAX_HISTORY; i++) {
                q->filehistory.cartridge[0][i][0] = 0;
                q->filehistory.cartridge[1][i][0] = 0;
                q->filehistory.diskdrive[0][i][0] = 0;
                q->filehistory.diskdrive[1][i][0] = 0;
                q->filehistory.cassette[0][i][0] = 0;
            }
        });
    });
    page->check(langPropEjectMediaOnExit(), &p->emulation.ejectMediaOnExit);
    page->check(langPropScreenshotPng(), &p->settings.usePngScreenshots);

    QFormLayout* form = page->group(langShortcutConfigTitle());
    auto* profile = new QComboBox;
    form->addRow(u8(langShortcutScheme()), profile);
    QObject::connect(profile, &QComboBox::activated, [profile]() {
        QByteArray name = profile->currentText().toUtf8();
        change([&](Properties* q) {
            snprintf(q->emulation.shortcutProfile, sizeof(q->emulation.shortcutProfile), "%s", name.constData());
        });
    });
    page->onSync([profile, p]() {
        char names[32][64];
        int n = platformShortcutsListProfiles(names, 32);
        QSignalBlocker b(profile);
        profile->clear();
        for (int i = 0; i < n; i++) profile->addItem(u8(names[i]));
        profile->setCurrentText(u8(p->emulation.shortcutProfile));
    });
    page->button(u8(langMenuToolsShortcuts()), []() { actionToolsShowShorcutEditor(); });

    page->group(langMenuPropsLanguage());
    QPushButton* lang = page->button("", []() { actionOptionsShowLanguage(); });
    page->onSync([lang, p]() { lang->setText(u8(langToName((EmuLanguageType)p->language, 1))); });

    page->finish();
    return page;
}

/* ---- Window ------------------------------------------------------------------- */

class PropertiesDialog : public QDialog {
public:
    PropertiesDialog() : QDialog(mainWindow())
    {
        setWindowTitle(u8(langPropTitle()));
        auto* layout = new QVBoxLayout(this);
        tabs = new QTabWidget;
        tabs->addTab(pageEmulation(&syncs), u8(langPropEmulation()));
        tabs->addTab(pageVideo(&syncs), u8(langPropVideo()));
        tabs->addTab(pageSound(&syncs), u8(langPropSound()));
        tabs->addTab(pagePorts(&syncs), u8(langPropPorts()));
        tabs->addTab(pageSettings(&syncs), u8(langPropSettings()));
        layout->addWidget(tabs);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        buttons->button(QDialogButtonBox::Close)->setText(u8(langDlgClose()));
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        connect(this, &QDialog::finished, []() { platformSaveProperties(); });
        resize(640, 680);
    }

    void sync() { for (auto& fn : syncs) fn(); }
    QTabWidget* tabs;

private:
    std::vector<std::function<void()>> syncs;
};

static PropertiesDialog* propertiesDialog;

void openProperties(int page)
{
    int tab;
    switch (page) {
    case PROP_EMULATION:                  tab = 0; break;
    /* blueMSX opens its Video page as PROP_PERFORMANCE and the effects
    ** as PROP_VIDEO; both live on the Video tab here. */
    case PROP_VIDEO: case PROP_D3D:
    case PROP_PERFORMANCE:                tab = 1; break;
    case PROP_SOUND: case PROP_MIDI:      tab = 2; break;
    case PROP_PORTS:                      tab = 3; break;
    default:                              tab = 4; break;
    }
    /* Labels follow the language: build it anew each time it opens. */
    if (propertiesDialog && !propertiesDialog->isVisible()) {
        delete propertiesDialog;
        propertiesDialog = NULL;
    }
    if (propertiesDialog == NULL) propertiesDialog = new PropertiesDialog;
    propertiesDialog->sync();
    propertiesDialog->tabs->setCurrentIndex(tab);
    propertiesDialog->show();
    propertiesDialog->raise();
    propertiesDialog->activateWindow();
}

void refreshProperties()
{
    if (propertiesDialog && propertiesDialog->isVisible()) propertiesDialog->sync();
}

}
