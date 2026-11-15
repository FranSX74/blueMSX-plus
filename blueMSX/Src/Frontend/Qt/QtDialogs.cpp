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
#include "Language.h"
#include "version.h"
}

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
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

void openDialog(FrontendDialog dialog, int)
{
    switch (dialog) {
    case FRONTEND_DIALOG_ABOUT:
        showAbout();
        break;
    case FRONTEND_DIALOG_HELP:
        QDesktopServices::openUrl(QUrl("https://github.com/Hesoten/blueMSX-plus#readme"));
        break;
    default:
        if (mainWindow()) mainWindow()->screen()->showNotice("Not available yet in QblueMSX");
        break;
    }
}

}
