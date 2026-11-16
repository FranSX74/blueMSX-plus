/*****************************************************************************
** QblueMSX - debugger window
**
** The Windows SimpleDebugger plugin rebuilt on Qt: the disassembly in the
** middle, and dockable panels for the CPU registers and flags, stack,
** callstack, breakpoints and watchpoints, memory, peripheral registers and
** I/O ports. The logic lives in Frontend/Common/DebuggerCore.cpp.
**
** The window is created once and only hidden when closed, so breakpoints
** stay listed (and armed, if the user wants) between openings; it comes
** back by itself when one of them stops the machine.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "QtApp.h"
#include "DebuggerCore.h"

extern "C" {
#include "Language.h"
#include "PlatformPaths.h"
}

#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <memory>

namespace qbm {

namespace {

const char* tr2(const char* english, const char* spanish)
{
    return langGetLanguage() == EMU_LANG_SPANISH ? spanish : english;
}

QString hex(uint32_t v, int digits)
{
    return QString("%1").arg(v, digits, 16, QChar('0')).toUpper();
}

QFont monoFont()
{
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    return f;
}

QColor changedColor(const QWidget* w)
{
    return w->palette().color(QPalette::Base).lightness() < 128 ? QColor(255, 110, 110) : QColor(200, 0, 0);
}

class DebuggerWindow;
DebuggerWindow* theWindow = nullptr;

/* ---- Disassembly --------------------------------------------------------- */

class DisasmView : public QAbstractScrollArea {
public:
    explicit DisasmView(dbg::Session* s) : session(s)
    {
        setFont(monoFont());
        setFocusPolicy(Qt::StrongFocus);
        viewport()->setCursor(Qt::IBeamCursor);
        verticalScrollBar()->setSingleStep(1);
    }

    std::function<void(int address)> toggleBreakpoint;
    std::function<void(const QPoint& global)> contextMenu;
    std::function<void()> cursorMoved;

    void rebuild(bool followPc)
    {
        int cursorAddress = cursorLine >= 0 && cursorLine < (int)lines.size() ? lines[cursorLine].address : -1;
        int top = verticalScrollBar()->value();
        int topAddress = top < (int)lines.size() ? lines[top].address : 0;
        lines = session->hasSnapshot() ? session->listing() : std::vector<dbg::Session::Line>();
        updateScroll();
        cursorLine = cursorAddress >= 0 ? lineOf(cursorAddress) : -1;
        if (followPc && session->hasSnapshot()) showAddress(session->pc, false);
        else verticalScrollBar()->setValue(lineOf(topAddress));
        viewport()->update();
    }

    void setStale(bool on)
    {
        stale = on;
        viewport()->update();
    }

    int cursorAddress() const
    {
        return cursorLine >= 0 && cursorLine < (int)lines.size() ? lines[cursorLine].address : -1;
    }

    /* Puts `address` in view (a third down) and, if asked, the cursor on it. */
    void showAddress(int address, bool moveCursor)
    {
        int line = lineOf(address);
        int visible = visibleLines();
        int pos = verticalScrollBar()->value();
        if (line < pos || line >= pos + visible) verticalScrollBar()->setValue(std::max(0, line - visible / 3));
        if (moveCursor) {
            cursorLine = line;
            if (cursorMoved) cursorMoved();
        }
        viewport()->update();
    }

    bool hasContent() const { return !lines.empty(); }
    void repaintMarks() { viewport()->update(); }

protected:
    void resizeEvent(QResizeEvent*) override { updateScroll(); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(viewport());
        QPalette pal = palette();
        p.fillRect(viewport()->rect(), pal.color(QPalette::Base));
        QFontMetrics fm(font());
        int h = fm.height();
        int cw = fm.horizontalAdvance('0');
        int gutter = h + 4;
        if (lines.empty()) {
            p.setPen(pal.color(QPalette::PlaceholderText));
            p.drawText(gutter, fm.ascent() + 2, tr2("Disassembly unavailable.", "Desensamblado no disponible."));
            return;
        }
        int first = verticalScrollBar()->value();
        QColor text = pal.color(stale ? QPalette::PlaceholderText : QPalette::Text);
        QColor dim = pal.color(QPalette::PlaceholderText);
        p.fillRect(0, 0, gutter, viewport()->height(), pal.color(QPalette::AlternateBase));
        for (int i = 0; i * h < viewport()->height() && first + i < (int)lines.size(); i++) {
            const dbg::Session::Line& l = lines[first + i];
            int y = i * h;
            bool isPc = !l.label && session->hasSnapshot() && l.address == session->pc;
            if (first + i == cursorLine) {
                p.fillRect(gutter, y, viewport()->width(), h, pal.color(QPalette::Highlight).lighter(stale ? 100 : 100));
            }
            else if (isPc) {
                p.fillRect(gutter, y, viewport()->width(), h, QColor(255, 220, 0, stale ? 40 : 90));
            }
            bool selected = first + i == cursorLine;
            QColor fg = selected ? pal.color(QPalette::HighlightedText) : text;
            if (!l.label) {
                int bp = session->findCode(l.address);
                if (bp >= 0) {
                    QRect r(2, y + 2, h - 4, h - 4);
                    p.setRenderHint(QPainter::Antialiasing);
                    p.setPen(QColor(180, 0, 0));
                    p.setBrush(session->breakpoints()[bp].enabled ? QColor(220, 30, 30) : Qt::NoBrush);
                    p.drawEllipse(r);
                    p.setRenderHint(QPainter::Antialiasing, false);
                }
                if (isPc) {
                    QPolygon arrow;
                    int cy = y + h / 2;
                    arrow << QPoint(gutter - h / 2 - 1, cy - h / 4) << QPoint(gutter - 2, cy) << QPoint(gutter - h / 2 - 1, cy + h / 4);
                    p.setPen(QColor(120, 90, 0));
                    p.setBrush(QColor(255, 200, 0));
                    p.drawPolygon(arrow);
                }
                p.setPen(selected ? fg : dim);
                p.drawText(gutter + cw, y + fm.ascent(), hex(l.address, 4) + ":");
                p.drawText(gutter + 7 * cw, y + fm.ascent(), QString::fromLatin1(l.bytes.c_str()));
                p.setPen(fg);
                p.drawText(gutter + 20 * cw, y + fm.ascent(), QString::fromLatin1(l.text.c_str()));
            }
            else {
                p.setPen(selected ? fg : QColor(0, 130, 200));
                p.drawText(gutter + cw, y + fm.ascent(), QString::fromLatin1(l.text.c_str()));
            }
        }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        int line = lineAt(e->pos().y());
        if (line < 0) return;
        QFontMetrics fm(font());
        if (e->pos().x() < fm.height() + 4 && e->button() == Qt::LeftButton && !lines[line].label) {
            if (toggleBreakpoint) toggleBreakpoint(lines[line].address);
            return;
        }
        cursorLine = line;
        viewport()->update();
        if (cursorMoved) cursorMoved();
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        int line = lineAt(e->pos().y());
        if (line >= 0 && !lines[line].label && toggleBreakpoint) toggleBreakpoint(lines[line].address);
    }

    void contextMenuEvent(QContextMenuEvent* e) override
    {
        int line = lineAt(e->pos().y());
        if (line >= 0) {
            cursorLine = line;
            viewport()->update();
            if (cursorMoved) cursorMoved();
        }
        if (contextMenu) contextMenu(e->globalPos());
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (lines.empty()) return;
        int line = cursorLine < 0 ? verticalScrollBar()->value() : cursorLine;
        int page = std::max(1, visibleLines() - 1);
        switch (e->key()) {
        case Qt::Key_Up:       line--; break;
        case Qt::Key_Down:     line++; break;
        case Qt::Key_PageUp:   line -= page; break;
        case Qt::Key_PageDown: line += page; break;
        default: QAbstractScrollArea::keyPressEvent(e); return;
        }
        line = std::max(0, std::min((int)lines.size() - 1, line));
        cursorLine = line;
        int pos = verticalScrollBar()->value();
        if (line < pos) verticalScrollBar()->setValue(line);
        else if (line >= pos + visibleLines()) verticalScrollBar()->setValue(line - visibleLines() + 1);
        viewport()->update();
        if (cursorMoved) cursorMoved();
    }

private:
    int visibleLines() const { return std::max(1, viewport()->height() / QFontMetrics(font()).height()); }

    int lineAt(int y) const
    {
        int line = verticalScrollBar()->value() + y / QFontMetrics(font()).height();
        return line >= 0 && line < (int)lines.size() ? line : -1;
    }

    int lineOf(int address) const
    {
        /* The first line at or after `address` that is not a label. */
        int lo = 0, hi = (int)lines.size() - 1, best = hi;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (lines[mid].address >= address) {
                best = mid;
                hi = mid - 1;
            }
            else lo = mid + 1;
        }
        while (best + 1 < (int)lines.size() && lines[best].label) best++;
        return std::max(0, best);
    }

    void updateScroll()
    {
        verticalScrollBar()->setRange(0, std::max(0, (int)lines.size() - visibleLines()));
        verticalScrollBar()->setPageStep(visibleLines());
    }

    dbg::Session* session;
    std::vector<dbg::Session::Line> lines;
    int cursorLine = -1;
    bool stale = false;
};

/* ---- Hex memory view ----------------------------------------------------- */

class HexView : public QAbstractScrollArea {
public:
    HexView()
    {
        setFont(monoFont());
        setFocusPolicy(Qt::StrongFocus);
    }

    std::function<void(uint32_t offset, uint8_t value)> write;

    void setData(const std::vector<uint8_t>* d, const std::vector<uint8_t>* prev, uint32_t start, bool editable)
    {
        data = d;
        previous = prev;
        base = start;
        canEdit = editable;
        if (data == nullptr || cursor >= (int)data->size()) cursor = -1;
        updateScroll();
        viewport()->update();
    }

    void setStale(bool on)
    {
        stale = on;
        viewport()->update();
    }

    int cursorOffset() const { return cursor; }

    void showOffset(int offset, bool select)
    {
        if (data == nullptr || offset < 0 || offset >= (int)data->size()) return;
        int row = offset / perRow;
        int pos = verticalScrollBar()->value();
        if (row < pos || row >= pos + visibleRows()) verticalScrollBar()->setValue(row);
        if (select) {
            cursor = offset;
            lowNibble = false;
        }
        viewport()->update();
    }

protected:
    void resizeEvent(QResizeEvent*) override
    {
        QFontMetrics fm(font());
        int cw = fm.horizontalAdvance('0');
        int avail = viewport()->width() / cw - 8;
        perRow = 8;
        while (perRow * 2 * 4 <= avail) perRow *= 2;     /* 3 hex + 1 ascii per byte */
        perRow = std::min(perRow, 32);
        updateScroll();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(viewport());
        QPalette pal = palette();
        p.fillRect(viewport()->rect(), pal.color(QPalette::Base));
        QFontMetrics fm(font());
        int h = fm.height(), cw = fm.horizontalAdvance('0');
        if (data == nullptr || data->empty()) {
            p.setPen(pal.color(QPalette::PlaceholderText));
            p.drawText(cw, fm.ascent() + 2, tr2("Memory unavailable.", "Memoria no disponible."));
            return;
        }
        QColor text = pal.color(stale ? QPalette::PlaceholderText : QPalette::Text);
        QColor dim = pal.color(QPalette::PlaceholderText);
        QColor red = changedColor(this);
        int addrDigits = base + data->size() > 0x10000 ? 6 : 4;
        int hexX = (addrDigits + 2) * cw;
        int asciiX = hexX + (perRow * 3 + 1) * cw;
        int first = verticalScrollBar()->value();
        for (int r = 0; r * h < viewport()->height(); r++) {
            int offset = (first + r) * perRow;
            if (offset >= (int)data->size()) break;
            int y = r * h + fm.ascent();
            p.setPen(dim);
            p.drawText(cw / 2, y, hex(base + offset, addrDigits));
            for (int i = 0; i < perRow && offset + i < (int)data->size(); i++) {
                int o = offset + i;
                uint8_t v = (*data)[o];
                bool changed = previous != nullptr && o < (int)previous->size() && (*previous)[o] != v;
                if (o == cursor) {
                    p.fillRect(hexX + i * 3 * cw - 1, r * h, 2 * cw + 2, h, pal.color(QPalette::Highlight));
                    p.fillRect(asciiX + i * cw, r * h, cw, h, pal.color(QPalette::Highlight).lighter(130));
                    p.setPen(pal.color(QPalette::HighlightedText));
                }
                else p.setPen(changed && !stale ? red : text);
                p.drawText(hexX + i * 3 * cw, y, hex(v, 2));
                p.setPen(o == cursor ? pal.color(QPalette::HighlightedText) : (changed && !stale ? red : dim));
                p.drawText(asciiX + i * cw, y, QString(QChar(v >= 32 && v < 127 ? v : '.')));
            }
        }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (data == nullptr) return;
        QFontMetrics fm(font());
        int cw = fm.horizontalAdvance('0');
        int addrDigits = base + data->size() > 0x10000 ? 6 : 4;
        int hexX = (addrDigits + 2) * cw;
        int asciiX = hexX + (perRow * 3 + 1) * cw;
        int row = verticalScrollBar()->value() + e->pos().y() / fm.height();
        int col = -1;
        if (e->pos().x() >= hexX && e->pos().x() < asciiX - cw) col = (e->pos().x() - hexX) / (3 * cw);
        else if (e->pos().x() >= asciiX) col = (e->pos().x() - asciiX) / cw;
        if (col < 0 || col >= perRow) return;
        int offset = row * perRow + col;
        if (offset >= (int)data->size()) return;
        cursor = offset;
        lowNibble = false;
        viewport()->update();
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (data == nullptr || cursor < 0) {
            QAbstractScrollArea::keyPressEvent(e);
            return;
        }
        int size = (int)data->size();
        int move = 0;
        switch (e->key()) {
        case Qt::Key_Left:     move = -1; break;
        case Qt::Key_Right:    move = 1; break;
        case Qt::Key_Up:       move = -perRow; break;
        case Qt::Key_Down:     move = perRow; break;
        case Qt::Key_PageUp:   move = -perRow * visibleRows(); break;
        case Qt::Key_PageDown: move = perRow * visibleRows(); break;
        default: break;
        }
        if (move != 0) {
            cursor = std::max(0, std::min(size - 1, cursor + move));
            lowNibble = false;
            showOffset(cursor, false);
            return;
        }
        QString t = e->text();
        bool ok = false;
        int nibble = t.size() == 1 ? t.toInt(&ok, 16) : -1;
        if (ok && canEdit && !stale && write) {
            uint8_t v = (*data)[cursor];
            v = lowNibble ? (uint8_t)((v & 0xf0) | nibble) : (uint8_t)((v & 0x0f) | (nibble << 4));
            int at = cursor;
            bool advance = lowNibble;
            lowNibble = !lowNibble;
            write(at, v);           /* rereads the snapshot: data changes */
            if (advance && cursor < size - 1) cursor++;
            showOffset(cursor, false);
            return;
        }
        QAbstractScrollArea::keyPressEvent(e);
    }

private:
    int visibleRows() const { return std::max(1, viewport()->height() / QFontMetrics(font()).height()); }

    void updateScroll()
    {
        int rows = data == nullptr ? 0 : ((int)data->size() + perRow - 1) / perRow;
        verticalScrollBar()->setRange(0, std::max(0, rows - visibleRows()));
        verticalScrollBar()->setPageStep(visibleRows());
    }

    const std::vector<uint8_t>* data = nullptr;
    const std::vector<uint8_t>* previous = nullptr;
    uint32_t base = 0;
    bool canEdit = false;
    bool stale = false;
    int perRow = 16;
    int cursor = -1;
    bool lowNibble = false;
};

/* ---- Small helpers -------------------------------------------------------- */

QTableWidget* makeTable(const QStringList& headers)
{
    auto* t = new QTableWidget(0, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(QFontMetrics(monoFont()).height() + 4);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setFont(monoFont());
    return t;
}

QTableWidgetItem* cell(const QString& text, bool editable = false, bool changed = false, const QWidget* ref = nullptr)
{
    auto* item = new QTableWidgetItem(text);
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (editable) f |= Qt::ItemIsEditable;
    item->setFlags(f);
    if (changed && ref) item->setForeground(changedColor(ref));
    return item;
}

/* ---- Breakpoint / watchpoint entry ---------------------------------------- */

bool askWatchpoint(QWidget* parent, dbg::Session* s, dbg::Breakpoint* bp)
{
    QDialog d(parent);
    d.setWindowTitle(tr2("Add New Watchpoint", "Añadir watchpoint"));
    auto* form = new QFormLayout(&d);
    auto* type = new QComboBox;
    type->addItems({ tr2("Memory (CPU)", "Memoria (CPU)"), "VRAM", tr2("I/O port", "Puerto de E/S") });
    auto* address = new QLineEdit;
    auto* condition = new QComboBox;
    condition->addItems({ tr2("Any access", "Cualquier acceso"), "== ", "!= ", "< ", "> " });
    auto* value = new QLineEdit;
    auto* size = new QComboBox;
    size->addItems({ "1", "2", "4" });
    form->addRow(tr2("Type:", "Tipo:"), type);
    form->addRow(tr2("Address:", "Dirección:"), address);
    form->addRow(tr2("Condition:", "Condición:"), condition);
    form->addRow(tr2("Value:", "Valor:"), value);
    form->addRow(tr2("Size:", "Tamaño:"), size);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    auto sync = [=]() {
        value->setEnabled(condition->currentIndex() != 0);
        size->setEnabled(type->currentIndex() != 2);
    };
    QObject::connect(condition, &QComboBox::currentIndexChanged, sync);
    QObject::connect(type, &QComboBox::currentIndexChanged, sync);
    sync();
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, [&]() {
        int a = s->parseAddress(address->text().toStdString());
        bool ok = true;
        int v = condition->currentIndex() == 0 ? 0 : value->text().remove('#').remove('$').toInt(&ok, 16);
        if (a < 0 || !ok) {
            QMessageBox::warning(&d, d.windowTitle(), tr2("Not a valid address or value.", "Dirección o valor no válidos."));
            return;
        }
        bp->type = (dbg::Breakpoint::Type)(type->currentIndex() + 1);
        bp->address = a;
        bp->condition = (dbg::Breakpoint::Condition)condition->currentIndex();
        bp->value = v;
        bp->size = type->currentIndex() == 2 ? 1 : size->currentText().toInt();
        bp->enabled = true;
        d.accept();
    });
    return d.exec() == QDialog::Accepted;
}

/* ---- The window ------------------------------------------------------------ */

class DebuggerWindow : public QMainWindow {
public:
    DebuggerWindow() : session([]() { post([]() { if (theWindow) theWindow->coreEvent(); }); })
    {
        setWindowTitle(QString("blueMSX+ - ") + tr2("Debugger", "Depurador"));
        setDockNestingEnabled(true);
        resize(1200, 860);

        disasm = new DisasmView(&session);
        disasm->setObjectName("disassembly");
        setCentralWidget(disasm);
        disasm->toggleBreakpoint = [this](int a) { session.toggleCode(a); breakpointsChanged(); };
        disasm->cursorMoved = [this]() { updateActions(); };
        disasm->contextMenu = [this](const QPoint& at) { disasmMenu->exec(at); };

        buildPanels();
        buildActions();
        statusBar()->addWidget(stateLabel = new QLabel);
        statusBar()->addPermanentWidget(pcLabel = new QLabel);

        settingsPath = QString::fromUtf8(platformPaths()->configDir) + "/debugger.ini";
        QSettings ini(settingsPath, QSettings::IniFormat);
        restoreGeometry(ini.value("geometry").toByteArray());
        restoreState(ini.value("state").toByteArray());
        session.symbols.shown = ini.value("showSymbols", false).toBool();
        asmFlags = ini.value("asmFlags", true).toBool();
        if (ini.value("checkVram", false).toBool()) session.vramCheck(true);

        session.update();
        refreshAll(true);
    }

    void coreEvent()
    {
        dbg::State before = session.state();
        bool changed = session.update();
        if (!changed && session.state() == before && !session.hasSnapshot()) {
            updateActions();
            return;
        }
        if (session.state() == dbg::PAUSED && changed && !isVisible() && !session.breakpoints().empty()) {
            show();         /* one of our breakpoints stopped the machine */
            raise();
        }
        refreshAll(true);
    }

    void saveSettings()
    {
        QSettings ini(settingsPath, QSettings::IniFormat);
        ini.setValue("geometry", saveGeometry());
        ini.setValue("state", saveState());
        ini.setValue("showSymbols", session.symbols.shown);
        ini.setValue("asmFlags", asmFlags);
        ini.setValue("checkVram", session.vramCheck());
    }

protected:
    void closeEvent(QCloseEvent* e) override
    {
        bool armed = false;
        for (const dbg::Breakpoint& bp : session.breakpoints()) armed |= bp.enabled;
        if (armed) {
            auto answer = QMessageBox::question(this, windowTitle(),
                tr2("Breakpoints are still set. Remove them before closing?",
                    "Aún hay puntos de ruptura. ¿Quitarlos antes de cerrar?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel) {
                e->ignore();
                return;
            }
            if (answer == QMessageBox::Yes) {
                session.removeAll();
                breakpointsChanged();
            }
        }
        saveSettings();
        e->accept();        /* hidden, not destroyed */
    }

private:
    /* ---- Panels ---------------------------------------------------------- */

    QDockWidget* dock(const char* objectName, const char* en, const char* es, QWidget* body, Qt::DockWidgetArea area)
    {
        auto* d = new QDockWidget(tr2(en, es), this);
        d->setObjectName(objectName);
        d->setWidget(body);
        addDockWidget(area, d);
        docks.push_back(d);
        return d;
    }

    void buildPanels()
    {
        /* Registers + flags */
        auto* regBody = new QWidget;
        auto* regLayout = new QVBoxLayout(regBody);
        regLayout->setContentsMargins(2, 2, 2, 2);
        regTable = makeTable({ tr2("Reg", "Reg"), tr2("Value", "Valor"), tr2("Reg", "Reg"), tr2("Value", "Valor") });
        regTable->horizontalHeader()->setStretchLastSection(false);
        regTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        regTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        regLayout->addWidget(regTable, 1);
        auto* flagRow = new QHBoxLayout;
        for (int i = 0; i < 8; i++) {
            auto* b = new QPushButton;
            b->setCheckable(true);
            b->setFont(monoFont());
            b->setMinimumWidth(10);
            flagRow->addWidget(b);
            flagButtons.push_back(b);
            QObject::connect(b, &QPushButton::clicked, [this, i]() { flagClicked(i); });
        }
        regLayout->addLayout(flagRow);
        QObject::connect(regTable, &QTableWidget::itemChanged, [this](QTableWidgetItem* item) {
            if (filling) return;
            int index = item->data(Qt::UserRole).toInt();
            bool ok;
            uint32_t v = item->text().remove('#').remove('$').toUInt(&ok, 16);
            if (ok) session.writeCpu(index, v);
            post([this]() { refreshAll(false); });
        });
        dock("registers", "CPU Registers", "Registros de la CPU", regBody, Qt::RightDockWidgetArea);

        /* Stack */
        stackTable = makeTable({ tr2("Address", "Dirección"), tr2("Value", "Valor") });
        stackTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        dock("stack", "Stack", "Pila", stackTable, Qt::RightDockWidgetArea);

        /* Callstack */
        callTable = makeTable({ tr2("Return", "Retorno"), tr2("Call", "Llamada") });
        callTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        QObject::connect(callTable, &QTableWidget::cellDoubleClicked, [this](int row, int) {
            QTableWidgetItem* item = callTable->item(row, 0);
            if (item) disasm->showAddress(item->data(Qt::UserRole).toInt(), true);
        });
        dock("callstack", "Callstack", "Pila de llamadas", callTable, Qt::RightDockWidgetArea);

        /* Breakpoints */
        auto* bpBody = new QWidget;
        auto* bpLayout = new QVBoxLayout(bpBody);
        bpLayout->setContentsMargins(2, 2, 2, 2);
        bpTable = makeTable({ "", tr2("Type", "Tipo"), tr2("Address", "Dirección"), tr2("Condition", "Condición"), tr2("Size", "Tamaño") });
        bpTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        bpLayout->addWidget(bpTable, 1);
        QObject::connect(bpTable, &QTableWidget::itemChanged, [this](QTableWidgetItem* item) {
            if (filling || item->column() != 0) return;
            session.setEnabled(item->row(), item->checkState() == Qt::Checked);
            post([this]() { breakpointsChanged(); });
        });
        QObject::connect(bpTable, &QTableWidget::cellDoubleClicked, [this](int row, int) {
            if (row < 0 || row >= (int)session.breakpoints().size()) return;
            const dbg::Breakpoint& bp = session.breakpoints()[row];
            if (bp.type == dbg::Breakpoint::CODE) disasm->showAddress(bp.address, true);
        });
        bpTable->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(bpTable, &QWidget::customContextMenuRequested, [this](const QPoint& at) {
            QMenu m;
            int row = bpTable->currentRow();
            QAction* del = m.addAction(tr2("Remove", "Quitar"));
            del->setEnabled(row >= 0);
            m.addSeparator();
            m.addAction(actAddBp);
            m.addAction(actAddWp);
            m.addSeparator();
            m.addAction(actEnableAll);
            m.addAction(actDisableAll);
            m.addAction(actRemoveAll);
            if (m.exec(bpTable->viewport()->mapToGlobal(at)) == del) {
                session.remove(row);
                breakpointsChanged();
            }
        });
        dock("breakpoints", "Breakpoints", "Puntos de ruptura", bpBody, Qt::BottomDockWidgetArea);

        /* Memory */
        auto* memBody = new QWidget;
        auto* memLayout = new QVBoxLayout(memBody);
        memLayout->setContentsMargins(2, 2, 2, 2);
        memCombo = new QComboBox;
        memLayout->addWidget(memCombo);
        hexView = new HexView;
        hexView->setObjectName("hexview");
        memLayout->addWidget(hexView, 1);
        hexView->write = [this](uint32_t offset, uint8_t v) {
            int view = memCombo->currentIndex();
            session.writeMemory(view, offset, { v });
            post([this]() { refreshAll(false); });
        };
        QObject::connect(memCombo, &QComboBox::currentIndexChanged, [this](int) { fillMemory(); });
        dock("memory", "Memory", "Memoria", memBody, Qt::BottomDockWidgetArea);

        /* Peripheral registers */
        auto* periBody = new QWidget;
        auto* periLayout = new QVBoxLayout(periBody);
        periLayout->setContentsMargins(2, 2, 2, 2);
        periCombo = new QComboBox;
        periLayout->addWidget(periCombo);
        periTable = makeTable({ tr2("Register", "Registro"), tr2("Value", "Valor") });
        periTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        periLayout->addWidget(periTable, 1);
        QObject::connect(periCombo, &QComboBox::currentIndexChanged, [this](int) { fillPeripherals(); });
        QObject::connect(periTable, &QTableWidget::itemChanged, [this](QTableWidgetItem* item) {
            if (filling) return;
            bool ok;
            uint32_t v = item->text().remove('#').remove('$').toUInt(&ok, 16);
            if (ok) session.writePeripheral(periCombo->currentIndex(), item->row(), v);
            post([this]() { refreshAll(false); });
        });
        dock("peripherals", "Peripheral Registers", "Registros de periféricos", periBody, Qt::BottomDockWidgetArea);

        /* I/O ports */
        auto* ioBody = new QWidget;
        auto* ioLayout = new QVBoxLayout(ioBody);
        ioLayout->setContentsMargins(2, 2, 2, 2);
        ioCombo = new QComboBox;
        ioLayout->addWidget(ioCombo);
        ioTable = makeTable({ tr2("Port", "Puerto"), "R/W", tr2("Value", "Valor") });
        ioTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        ioLayout->addWidget(ioTable, 1);
        QObject::connect(ioCombo, &QComboBox::currentIndexChanged, [this](int) { fillIoPorts(); });
        QObject::connect(ioTable, &QTableWidget::itemChanged, [this](QTableWidgetItem* item) {
            if (filling) return;
            bool ok;
            uint32_t v = item->text().remove('#').remove('$').toUInt(&ok, 16);
            if (ok) session.writeIoPort(ioCombo->currentIndex(), item->row(), v);
            post([this]() { refreshAll(false); });
        });
        dock("ioports", "I/O Ports", "Puertos de E/S", ioBody, Qt::BottomDockWidgetArea);

        tabifyDockWidget(docks[3], docks[4]);       /* breakpoints | memory | peripherals | I/O */
        tabifyDockWidget(docks[4], docks[5]);
        tabifyDockWidget(docks[5], docks[6]);
        docks[4]->raise();
        resizeDocks({ docks[0] }, { 380 }, Qt::Horizontal);
        resizeDocks({ docks[0], docks[1], docks[2] }, { 330, 170, 170 }, Qt::Vertical);
        resizeDocks({ docks[4] }, { 230 }, Qt::Vertical);
    }

    /* ---- Actions --------------------------------------------------------- */

    QAction* action(QMenu* menu, const char* en, const char* es, const QKeySequence& key,
                    std::function<void()> fn, int icon = -1)
    {
        auto* a = new QAction(tr2(en, es), this);
        if (!key.isEmpty()) {
            a->setShortcut(key);
            a->setShortcutContext(Qt::WindowShortcut);
        }
        if (icon >= 0) a->setIcon(glyph(icon));
        QObject::connect(a, &QAction::triggered, [fn]() { fn(); });
        if (menu) menu->addAction(a);
        addAction(a);
        return a;
    }

    void buildActions()
    {
        QMenu* file = menuBar()->addMenu(tr2("&File", "&Archivo"));
        actLoadSym = action(file, "Load Symbol File...", "Cargar archivo de símbolos...", {}, [this]() { loadSymbols(); });
        actSaveAsm = action(file, "Save Disassembly...", "Guardar desensamblado...", {}, [this]() { saveListing(); });
        actSaveMem = action(file, "Save Memory...", "Guardar memoria...", {}, [this]() { saveMemoryFile(); });
        file->addSeparator();
        action(file, "Close", "Cerrar", QKeySequence("Ctrl+W"), [this]() { close(); });

        QMenu* debug = menuBar()->addMenu(tr2("&Debug", "&Depurar"));
        actContinue = action(debug, "Start/Continue", "Iniciar/Continuar", QKeySequence("F5"), [this]() { session.run(); }, G_PLAY);
        actBreak = action(debug, "Break All", "Pausar", QKeySequence("Ctrl+Alt+Pause"), [this]() { session.pause(); }, G_PAUSE);
        actStop = action(debug, "Stop Debugging", "Detener", QKeySequence("Shift+F5"), [this]() { session.stop(); }, G_STOP);
        actRestart = action(debug, "Restart", "Reiniciar", QKeySequence("Ctrl+Shift+F5"), [this]() { session.restart(); }, G_RESTART);
        debug->addSeparator();
        actStepBack = action(debug, "Step Back", "Paso atrás", QKeySequence("Ctrl+F11"), [this]() { session.stepBack(); }, G_BACK);
        actStepInto = action(debug, "Step Into", "Paso a paso", QKeySequence("F11"), [this]() { session.stepInto(); }, G_INTO);
        actStepOver = action(debug, "Step Over", "Saltar llamada", QKeySequence("F10"), [this]() { session.stepOver(); }, G_OVER);
        actStepOut = action(debug, "Step Out", "Salir de la rutina", QKeySequence("Shift+F11"), [this]() { session.stepOut(); }, G_OUT);
        actRunTo = action(debug, "Run To Cursor", "Ejecutar hasta el cursor", QKeySequence("Shift+F10"), [this]() {
            if (disasm->cursorAddress() >= 0) session.runTo((uint16_t)disasm->cursorAddress());
        }, G_RUNTO);
        actShowNext = action(debug, "Show Next Statement", "Mostrar la siguiente instrucción", QKeySequence("Home"), [this]() {
            if (session.hasSnapshot()) disasm->showAddress(session.pc, true);
        });
        debug->addSeparator();
        actSymbols = action(debug, "Show Symbol Information", "Mostrar símbolos", QKeySequence("F8"), [this]() {
            session.symbols.shown = !session.symbols.shown;
            refreshAll(false);
        });
        actSymbols->setCheckable(true);
        debug->addSeparator();
        actFind = action(debug, "Find...", "Buscar...", QKeySequence("Ctrl+F"), [this]() { findInMemory(true); });
        actFindNext = action(debug, "Find Next", "Buscar siguiente", QKeySequence("F3"), [this]() { findInMemory(false); });
        actGoto = action(debug, "Go To...", "Ir a...", QKeySequence("Ctrl+G"), [this]() { gotoAddress(); });
        debug->addSeparator();
        actAddBp = action(debug, "Add New Breakpoint...", "Añadir punto de ruptura...", QKeySequence("Ctrl+B"), [this]() { addBreakpoint(); });
        actAddWp = action(debug, "Add New Watchpoint...", "Añadir watchpoint...", QKeySequence("Ctrl+Shift+W"), [this]() {
            dbg::Breakpoint bp;
            if (askWatchpoint(this, &session, &bp)) {
                session.add(bp);
                breakpointsChanged();
            }
        });
        actToggleBp = action(debug, "Set/Remove Breakpoint", "Poner/quitar punto de ruptura", QKeySequence("F9"), [this]() {
            if (disasm->cursorAddress() >= 0) {
                session.toggleCode(disasm->cursorAddress());
                breakpointsChanged();
            }
        });
        actEnableBp = action(debug, "Enable/Disable Breakpoint", "Activar/desactivar punto de ruptura", QKeySequence("Shift+F9"), [this]() {
            if (disasm->cursorAddress() >= 0) {
                session.toggleCodeEnabled(disasm->cursorAddress());
                breakpointsChanged();
            }
        });
        actRemoveAll = action(debug, "Remove All Breakpoints", "Quitar todos los puntos de ruptura", QKeySequence("Ctrl+Shift+F9"), [this]() {
            session.removeAll();
            breakpointsChanged();
        });
        actEnableAll = action(debug, "Enable All Breakpoints", "Activar todos los puntos de ruptura", {}, [this]() {
            session.enableAll(true);
            breakpointsChanged();
        });
        actDisableAll = action(debug, "Disable All Breakpoints", "Desactivar todos los puntos de ruptura", {}, [this]() {
            session.enableAll(false);
            breakpointsChanged();
        });
        debug->addSeparator();
        actAsmFlags = action(debug, "Show Assembly Flags", "Banderas como en ensamblador", QKeySequence("Ctrl+M"), [this]() {
            asmFlags = !asmFlags;
            fillRegisters();
            updateActions();
        });
        actAsmFlags->setCheckable(true);
        actVram = action(debug, "Break on too fast VRAM access", "Parar en accesos a VRAM demasiado rápidos", QKeySequence("Ctrl+V"), [this]() {
            session.vramCheck(!session.vramCheck());
            updateActions();
        });
        actVram->setCheckable(true);

        QMenu* window = menuBar()->addMenu(tr2("&Window", "&Ventana"));
        for (QDockWidget* d : docks) window->addAction(d->toggleViewAction());

        QToolBar* tb = addToolBar(tr2("Debug", "Depurar"));
        tb->setObjectName("toolbar");
        tb->setIconSize(QSize(18, 18));
        for (QAction* a : { actContinue, actBreak, actStop, actRestart }) tb->addAction(a);
        tb->addSeparator();
        for (QAction* a : { actShowNext, actStepBack, actStepInto, actStepOver, actStepOut, actRunTo }) tb->addAction(a);
        tb->addSeparator();
        for (QAction* a : { actToggleBp, actEnableBp, actRemoveAll }) tb->addAction(a);
        actShowNext->setIcon(glyph(G_NEXT));
        actToggleBp->setIcon(glyph(G_BP));
        actEnableBp->setIcon(glyph(G_BP_OFF));
        actRemoveAll->setIcon(glyph(G_BP_CLEAR));

        disasmMenu = new QMenu(this);
        disasmMenu->addAction(actToggleBp);
        disasmMenu->addAction(actEnableBp);
        disasmMenu->addAction(actRunTo);
        disasmMenu->addSeparator();
        disasmMenu->addAction(actShowNext);
        disasmMenu->addAction(actGoto);
    }

    /* Toolbar pictures drawn here, so they read the same on every style. */
    enum { G_PLAY, G_PAUSE, G_STOP, G_RESTART, G_NEXT, G_BACK, G_INTO, G_OVER, G_OUT, G_RUNTO, G_BP, G_BP_OFF, G_BP_CLEAR };

    static QIcon glyph(int g)
    {
        QIcon icon;
        for (int disabled = 0; disabled < 2; disabled++) {
            QPixmap pm(32, 32);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            auto col = [disabled](int r, int g, int b) {
                if (!disabled) return QColor(r, g, b);
                int grey = (r * 30 + g * 59 + b * 11) / 100;
                return QColor(grey, grey, grey, 110);
            };
            QColor blue = col(40, 110, 200), green = col(30, 150, 50), red = col(210, 40, 40);
            QColor yellow = col(240, 190, 0), dark = col(60, 60, 60);
            QPen line(blue, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            auto arrowHead = [&p](QPointF tip, QPointF dir, QColor c) {
                QPointF n(-dir.y(), dir.x());
                QPolygonF tri;
                tri << tip << tip - dir * 8 + n * 6 << tip - dir * 8 - n * 6;
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawPolygon(tri);
            };
            auto dot = [&p, dark](qreal x, qreal y) {
                p.setPen(Qt::NoPen);
                p.setBrush(dark);
                p.drawEllipse(QPointF(x, y), 3.2, 3.2);
            };
            switch (g) {
            case G_PLAY:
                p.setPen(Qt::NoPen);
                p.setBrush(green);
                p.drawPolygon(QPolygonF({ QPointF(8, 5), QPointF(27, 16), QPointF(8, 27) }));
                break;
            case G_PAUSE:
                p.setPen(Qt::NoPen);
                p.setBrush(blue);
                p.drawRect(QRectF(7, 6, 6, 20));
                p.drawRect(QRectF(19, 6, 6, 20));
                break;
            case G_STOP:
                p.setPen(Qt::NoPen);
                p.setBrush(red);
                p.drawRect(QRectF(7, 7, 18, 18));
                break;
            case G_RESTART:
                p.setPen(line);
                p.drawArc(QRectF(6, 6, 20, 20), 100 * 16, 290 * 16);
                arrowHead(QPointF(20, 5), QPointF(1, 0), blue);
                break;
            case G_NEXT:
                p.setPen(QPen(col(140, 100, 0), 1.5));
                p.setBrush(yellow);
                p.drawPolygon(QPolygonF({ QPointF(4, 11), QPointF(16, 11), QPointF(16, 5), QPointF(28, 16),
                                          QPointF(16, 27), QPointF(16, 21), QPointF(4, 21) }));
                break;
            case G_BACK:
                p.setPen(line);
                p.drawArc(QRectF(8, 4, 18, 18), 270 * 16, 270 * 16);
                arrowHead(QPointF(9, 21), QPointF(0, 1), blue);
                dot(17, 27);
                break;
            case G_INTO:
                p.setPen(line);
                p.drawLine(QPointF(16, 3), QPointF(16, 16));
                arrowHead(QPointF(16, 22), QPointF(0, 1), blue);
                dot(16, 27);
                break;
            case G_OVER:
                p.setPen(line);
                p.drawArc(QRectF(6, 6, 20, 20), 0, 180 * 16);
                arrowHead(QPointF(26, 22), QPointF(0, 1), blue);
                dot(16, 26);
                break;
            case G_OUT:
                p.setPen(line);
                p.drawLine(QPointF(16, 29), QPointF(16, 14));
                arrowHead(QPointF(16, 6), QPointF(0, -1), blue);
                dot(16, 29);
                break;
            case G_RUNTO:
                p.setPen(line);
                p.drawLine(QPointF(4, 16), QPointF(18, 16));
                arrowHead(QPointF(25, 16), QPointF(1, 0), blue);
                p.setPen(QPen(dark, 2.5));
                p.drawLine(QPointF(28, 6), QPointF(28, 26));
                break;
            case G_BP:
            case G_BP_OFF:
            case G_BP_CLEAR:
                p.setPen(QPen(col(160, 0, 0), 2));
                p.setBrush(g == G_BP_OFF ? QBrush(Qt::NoBrush) : QBrush(red));
                p.drawEllipse(QRectF(5, 5, 22, 22));
                if (g == G_BP_CLEAR) {
                    p.setPen(QPen(col(255, 255, 255), 3, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(QPointF(11, 11), QPointF(21, 21));
                    p.drawLine(QPointF(21, 11), QPointF(11, 21));
                }
                break;
            }
            p.end();
            icon.addPixmap(pm, disabled ? QIcon::Disabled : QIcon::Normal);
        }
        return icon;
    }

    void updateActions()
    {
        dbg::State s = session.state();
        bool paused = s == dbg::PAUSED;
        bool cursor = disasm->cursorAddress() >= 0;
        actContinue->setText(s == dbg::STOPPED ? tr2("Start", "Iniciar") : tr2("Continue", "Continuar"));
        actContinue->setEnabled(s != dbg::RUNNING);
        actBreak->setEnabled(s == dbg::RUNNING);
        actStop->setEnabled(s != dbg::STOPPED);
        actRestart->setEnabled(s != dbg::STOPPED);
        actStepBack->setEnabled(paused);
        actStepInto->setEnabled(paused);
        actStepOver->setEnabled(paused && session.hasSnapshot());
        actStepOut->setEnabled(paused && session.returnAddress() >= 0);
        actRunTo->setEnabled(paused && cursor);
        actShowNext->setEnabled(session.hasSnapshot());
        actToggleBp->setEnabled(s != dbg::STOPPED && cursor);
        actEnableBp->setEnabled(s != dbg::STOPPED && cursor && session.findCode(disasm->cursorAddress()) >= 0);
        actAddBp->setEnabled(s != dbg::STOPPED);
        actAddWp->setEnabled(s != dbg::STOPPED);
        bool any = !session.breakpoints().empty();
        actRemoveAll->setEnabled(any);
        actEnableAll->setEnabled(any);
        actDisableAll->setEnabled(any);
        actSaveAsm->setEnabled(session.hasSnapshot());
        actSaveMem->setEnabled(session.hasSnapshot() && memCombo->currentIndex() >= 0);
        actFind->setEnabled(session.hasSnapshot());
        actFindNext->setEnabled(session.hasSnapshot() && !lastSearch.isEmpty());
        actSymbols->setChecked(session.symbols.shown);
        actAsmFlags->setChecked(asmFlags);
        actVram->setChecked(session.vramCheck());

        static const char* names[][2] = { { "Running", "En marcha" }, { "Paused", "En pausa" }, { "Stopped", "Detenido" } };
        int n = s == dbg::RUNNING ? 0 : s == dbg::PAUSED ? 1 : 2;
        stateLabel->setText(tr2(names[n][0], names[n][1]));
        pcLabel->setText(session.hasSnapshot() ? QString("PC %1  SP %2").arg(hex(session.pc, 4), hex(session.sp, 4)) : QString());
    }

    /* ---- Filling the panels ---------------------------------------------- */

    void refreshAll(bool followPc)
    {
        bool stale = session.state() != dbg::PAUSED;
        disasm->rebuild(followPc && !stale);
        disasm->setStale(stale);
        hexView->setStale(stale);
        fillRegisters();
        fillStack();
        fillCallstack();
        fillBreakpoints();
        fillCombos();
        fillMemory();
        fillPeripherals();
        fillIoPorts();
        regTable->setEnabled(!stale);
        periTable->setEnabled(!stale);
        ioTable->setEnabled(!stale);
        for (QPushButton* b : flagButtons) b->setEnabled(!stale && session.hasSnapshot());
        updateActions();
    }

    void breakpointsChanged()
    {
        fillBreakpoints();
        disasm->repaintMarks();
        updateActions();
    }

    void fillRegisters()
    {
        filling = true;
        const auto& regs = session.cpu;
        int rows = ((int)regs.size() + 1) / 2;
        regTable->setRowCount(session.hasSnapshot() ? rows : 0);
        for (int i = 0; i < (int)regs.size() && session.hasSnapshot(); i++) {
            const dbg::Register& r = regs[i];
            int row = i % rows, col = (i / rows) * 2;
            regTable->setItem(row, col, cell(QString::fromStdString(r.name)));
            bool editable = r.name != "CLKH" && r.name != "CLKL" && r.name != "CNT";
            QTableWidgetItem* v = cell(hex(r.value, r.width / 4), editable, r.changed, this);
            v->setData(Qt::UserRole, i);
            regTable->setItem(row, col + 1, v);
        }
        filling = false;

        std::vector<dbg::Session::Flag> flags = session.flags(asmFlags);
        for (int i = 0; i < (int)flagButtons.size(); i++) {
            QPushButton* b = flagButtons[i];
            b->setVisible(i < (int)flags.size());
            if (i < (int)flags.size()) {
                b->setText(QString::fromStdString(flags[i].name));
                b->setChecked(flags[i].on);
            }
        }
    }

    void flagClicked(int i)
    {
        session.toggleFlag(i, asmFlags);
        refreshAll(false);
    }

    void fillStack()
    {
        stackTable->setRowCount(0);
        int row = 0;
        for (const dbg::Session::StackEntry& e : session.stackEntries()) {
            stackTable->insertRow(row);
            stackTable->setItem(row, 0, cell(hex(e.address, 4)));
            const char* label = session.symbols.find(e.value);
            stackTable->setItem(row, 1, cell(hex(e.value, 4) + (label ? QString("  ") + label : QString()), false, e.changed, this));
            row++;
        }
    }

    void fillCallstack()
    {
        callTable->setRowCount(0);
        int row = 0;
        for (const dbg::Session::CallEntry& e : session.callEntries()) {
            callTable->insertRow(row);
            const char* label = session.symbols.find(e.ret);
            QTableWidgetItem* r = cell(label ? QString(label) : hex(e.ret, 4));
            r->setData(Qt::UserRole, (int)e.from);
            callTable->setItem(row, 0, r);
            callTable->setItem(row, 1, cell(QString::fromUtf8(e.text.c_str())));
            row++;
        }
    }

    void fillBreakpoints()
    {
        filling = true;
        const auto& list = session.breakpoints();
        bpTable->setRowCount((int)list.size());
        for (int i = 0; i < (int)list.size(); i++) {
            const dbg::Breakpoint& bp = list[i];
            auto* on = new QTableWidgetItem;
            on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            on->setCheckState(bp.enabled ? Qt::Checked : Qt::Unchecked);
            bpTable->setItem(i, 0, on);
            bpTable->setItem(i, 1, cell(bp.typeName()));
            const char* label = bp.type == dbg::Breakpoint::CODE || bp.type == dbg::Breakpoint::MEMORY
                              ? session.symbols.find((uint16_t)bp.address) : nullptr;
            QString addr = label ? QString(label)
                         : "#" + hex(bp.address, bp.type == dbg::Breakpoint::VRAM ? 5 : bp.type == dbg::Breakpoint::IO ? 2 : 4);
            bpTable->setItem(i, 2, cell(addr));
            bpTable->setItem(i, 3, cell(QString::fromStdString(bp.conditionText())));
            bpTable->setItem(i, 4, cell(bp.type == dbg::Breakpoint::CODE ? QString() : QString::number(bp.size)));
        }
        bpTable->resizeColumnToContents(0);
        bpTable->horizontalHeader()->setStretchLastSection(false);
        bpTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        filling = false;
    }

    /* The combos keep their choice by name across stops and machines. */
    void fillCombo(QComboBox* combo, const QStringList& names)
    {
        QStringList now;
        for (int i = 0; i < combo->count(); i++) now << combo->itemText(i);
        if (now == names) return;
        QString keep = combo->currentText();
        QSignalBlocker b(combo);
        combo->clear();
        combo->addItems(names);
        int i = combo->findText(keep);
        combo->setCurrentIndex(i >= 0 ? i : (names.isEmpty() ? -1 : 0));
    }

    void fillCombos()
    {
        if (!session.hasSnapshot()) return;
        QStringList m, p, io;
        for (const auto& v : session.memories) m << QString::fromStdString(v.name);
        for (const auto& v : session.peripherals) p << QString::fromStdString(v.name);
        for (const auto& v : session.ioPorts) io << QString::fromStdString(v.name);
        fillCombo(memCombo, m);
        fillCombo(periCombo, p);
        fillCombo(ioCombo, io);
    }

    void fillMemory()
    {
        int i = memCombo->currentIndex();
        if (!session.hasSnapshot() || i < 0 || i >= (int)session.memories.size()) {
            if (session.state() == dbg::STOPPED) hexView->setData(nullptr, nullptr, 0, false);
            return;
        }
        const dbg::MemoryView& v = session.memories[i];
        hexView->setData(&v.data, &v.previous, v.start, !v.writeProtected && session.state() == dbg::PAUSED);
        updateActions();
    }

    void fillPeripherals()
    {
        filling = true;
        int i = periCombo->currentIndex();
        periTable->setRowCount(0);
        if (session.hasSnapshot() && i >= 0 && i < (int)session.peripherals.size()) {
            const auto& regs = session.peripherals[i].regs;
            periTable->setRowCount((int)regs.size());
            for (int k = 0; k < (int)regs.size(); k++) {
                periTable->setItem(k, 0, cell(QString::fromStdString(regs[k].name)));
                periTable->setItem(k, 1, cell(hex(regs[k].value, std::max(2, regs[k].width / 4)), true, regs[k].changed, this));
            }
        }
        filling = false;
    }

    void fillIoPorts()
    {
        filling = true;
        int i = ioCombo->currentIndex();
        ioTable->setRowCount(0);
        if (session.hasSnapshot() && i >= 0 && i < (int)session.ioPorts.size()) {
            const auto& ports = session.ioPorts[i].ports;
            static const char* dir[] = { "", "R", "W", "R/W" };
            ioTable->setRowCount((int)ports.size());
            for (int k = 0; k < (int)ports.size(); k++) {
                ioTable->setItem(k, 0, cell("#" + hex(ports[k].port, 2)));
                ioTable->setItem(k, 1, cell(dir[ports[k].direction & 3]));
                ioTable->setItem(k, 2, cell(hex(ports[k].value, 2), (ports[k].direction & 2) != 0, ports[k].changed, this));
            }
        }
        filling = false;
    }

    /* ---- Commands with dialogs ------------------------------------------- */

    void addBreakpoint()
    {
        bool ok;
        QString text = QInputDialog::getText(this, tr2("Add New Breakpoint", "Añadir punto de ruptura"),
                                             tr2("Address or Label:", "Dirección o etiqueta:"), QLineEdit::Normal,
                                             disasm->cursorAddress() >= 0 ? "#" + hex(disasm->cursorAddress(), 4) : QString(), &ok);
        if (!ok) return;
        int a = session.parseAddress(text.toStdString());
        if (a < 0) {
            QMessageBox::warning(this, windowTitle(), tr2("Not a valid address or label.", "Dirección o etiqueta no válida."));
            return;
        }
        if (session.findCode(a) < 0) session.toggleCode(a);
        breakpointsChanged();
    }

    void gotoAddress()
    {
        bool ok;
        QString text = QInputDialog::getText(this, tr2("Go To Address", "Ir a la dirección"),
                                             tr2("Address or Label:", "Dirección o etiqueta:"), QLineEdit::Normal, QString(), &ok);
        if (!ok) return;
        int a = session.parseAddress(text.toStdString());
        if (a < 0) {
            QMessageBox::warning(this, windowTitle(), tr2("Not a valid address or label.", "Dirección o etiqueta no válida."));
            return;
        }
        disasm->showAddress(a, true);
        /* The CPU's view in the memory panel follows. */
        int i = memCombo->currentIndex();
        if (i >= 0 && i < (int)session.memories.size() && session.memories[i].data.size() == 0x10000) {
            hexView->showOffset(a, true);
        }
    }

    void findInMemory(bool ask)
    {
        if (ask || lastSearch.isEmpty()) {
            bool ok;
            QString text = QInputDialog::getText(this, tr2("Find", "Buscar"),
                tr2("String or Value (\"text\" or hex bytes):", "Texto o valor (\"texto\" o bytes en hex):"),
                QLineEdit::Normal, lastSearch, &ok);
            if (!ok || text.isEmpty()) return;
            lastSearch = text;
        }
        int view = memCombo->currentIndex();
        int at = session.find(view, lastSearch.toStdString(), hexView->cursorOffset());
        if (at < 0) {
            statusBar()->showMessage(tr2("Not found.", "No encontrado."), 3000);
            return;
        }
        for (QDockWidget* d : docks) if (d->objectName() == "memory") { d->show(); d->raise(); }
        hexView->showOffset(at, true);
        updateActions();
    }

    void loadSymbols()
    {
        QString file = pickFile(false, tr2("Open Symbol File", "Abrir archivo de símbolos"),
                                tr2("Symbol Files (*.sym *.SYM);;All Files (*)",
                                    "Archivos de símbolos (*.sym *.SYM);;Todos los archivos (*)"));
        if (file.isEmpty()) return;
        lastDir = QFileInfo(file).absolutePath();
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) return;
        if (!session.symbols.empty()) {
            auto answer = QMessageBox::question(this, windowTitle(), tr2("Replace old symbols?", "¿Sustituir los símbolos anteriores?"));
            if (answer == QMessageBox::Yes) session.symbols.clear();
        }
        session.symbols.append(f.readAll().toStdString());
        session.symbols.shown = true;
        statusBar()->showMessage(QString(tr2("%1 symbols", "%1 símbolos")).arg(session.symbols.size()), 4000);
        refreshAll(false);
    }

    /* Qt's own dialog under BLUEMSX_TEST, so a script can drive it; a save
    ** asks before overwriting. */
    QString pickFile(bool save, const QString& title, const QString& filters)
    {
        QFileDialog dlg(this, title, lastDir, filters);
        dlg.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
        dlg.setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
        if (getenv("BLUEMSX_TEST") != nullptr) dlg.setOption(QFileDialog::DontUseNativeDialog);
        if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return QString();
        return dlg.selectedFiles().first();
    }

    QString askSave(const char* en, const char* es, const char* filterEn, const char* filterEs, const char* ext)
    {
        QString file = pickFile(true, tr2(en, es), tr2(filterEn, filterEs));
        if (file.isEmpty()) return file;
        if (QFileInfo(file).suffix().isEmpty()) file += ext;
        lastDir = QFileInfo(file).absolutePath();
        return file;
    }

    void saveListing()
    {
        QString file = askSave("Save Disassembly", "Guardar desensamblado", "Assembly (*.asm);;All Files (*)",
                               "Ensamblador (*.asm);;Todos los archivos (*)", ".asm");
        if (!file.isEmpty() && !session.saveListing(file.toStdString())) {
            QMessageBox::warning(this, windowTitle(), tr2("Could not write the file.", "No se pudo escribir el archivo."));
        }
    }

    void saveMemoryFile()
    {
        QString file = askSave("Save Memory", "Guardar memoria", "Binary (*.bin);;All Files (*)",
                               "Binario (*.bin);;Todos los archivos (*)", ".bin");
        if (!file.isEmpty() && !session.saveMemory(memCombo->currentIndex(), file.toStdString())) {
            QMessageBox::warning(this, windowTitle(), tr2("Could not write the file.", "No se pudo escribir el archivo."));
        }
    }

    dbg::Session session;
    DisasmView* disasm;
    HexView* hexView;
    QTableWidget *regTable, *stackTable, *callTable, *bpTable, *periTable, *ioTable;
    QComboBox *memCombo, *periCombo, *ioCombo;
    std::vector<QPushButton*> flagButtons;
    std::vector<QDockWidget*> docks;
    QMenu* disasmMenu;
    QLabel *stateLabel, *pcLabel;
    QAction *actLoadSym, *actSaveAsm, *actSaveMem, *actContinue, *actBreak, *actStop, *actRestart;
    QAction *actStepBack, *actStepInto, *actStepOver, *actStepOut, *actRunTo, *actShowNext, *actSymbols;
    QAction *actFind, *actFindNext, *actGoto, *actAddBp, *actAddWp, *actToggleBp, *actEnableBp;
    QAction *actRemoveAll, *actEnableAll, *actDisableAll, *actAsmFlags, *actVram;
    QString settingsPath, lastDir, lastSearch;
    bool asmFlags = true;
    bool filling = false;
};

}

void openDebugger()
{
    if (theWindow == nullptr) {
        theWindow = new DebuggerWindow;
        QObject::connect(qApp, &QApplication::aboutToQuit, []() {
            if (theWindow == nullptr) return;
            if (theWindow->isVisible()) theWindow->saveSettings();
            delete theWindow;
            theWindow = nullptr;
        });
    }
    theWindow->show();
    theWindow->raise();
    theWindow->activateWindow();
}

}
