/* Device Time Correction Dialog Implementation
 *
 * The Time Corrections dialog (Data / Time Corrections...). It lists every device in the
 * profile and the time corrections saved for them (those for the day the Daily page shows,
 * or all of them), and edits one correction at a time with a live preview on the Daily graphs.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "devicetimecorrectiondialog.h"
#include "ui_devicetimecorrectiondialog.h"
#include "borrowingtimeedit.h"
#include "SleepLib/profiles.h"
#include "uikit/theme.h"
#include <QMessageBox>
#include <QHeaderView>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QRadioButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>

//! \brief An Offset larger than this draws the "use a different correction type" hint
static const qint64 kLargeOffsetThresholdMs = 15LL * 60 * 1000;

//! \brief Database type keys, in the order of the Type combo box
static const QStringList kTypeKeys = { "offset", "travel", "dst", "timezone", "reset" };

//! \brief Number of columns in the corrections table
static const int kHistoryColumns = 6;

/*! \class SelectedRowDelegate
    \brief Draws the selected row of the corrections table in the accent colour

    On a light theme the rows are tinted by correction type, and the style's own selection
    mark is too faint to see over a tint. The selected row is filled with the accent colour
    and its text drawn in the matching ink, on both themes. */
class SelectedRowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if (!(option->state & QStyle::State_Selected)) return;
        const QColor ink = Theme::color(Theme::Role::OnAccent, option->palette);
        option->backgroundBrush = Theme::color(Theme::Role::Accent, option->palette);
        // After the fill: a greyed row sets the Text colour from its foreground.
        option->palette.setColor(QPalette::Text, ink);
        option->palette.setColor(QPalette::HighlightedText, ink);
    }
};

/*! \brief Returns the row background for a correction type
    \param type Database type key
    \param dark True when the palette is dark; rows are then not tinted
    \return An invalid colour for no tint (drift models, and every type on a dark palette) */
static QColor typeTint(const QString& type, bool dark)
{
    QColor light;
    if      (type == "offset")   light = QColor(255, 255, 210);
    else if (type == "timezone") light = QColor(210, 225, 255);
    else if (type == "travel")   light = QColor(210, 255, 225);
    else if (type == "dst")      light = QColor(255, 225, 210);
    else if (type == "reset")    light = QColor(240, 210, 255);
    // No tint on a dark palette: the rows keep the table's own background, and the Type
    // column says what the colour would have.
    return dark ? QColor() : light;
}

//! \brief Returns the Machine a device-list item stands for, or nullptr for a group header
static Machine* machineOf(const QTreeWidgetItem* item)
{
    if (!item) return nullptr;
    QVariant v = item->data(0, Qt::UserRole);
    if (!v.isValid()) return nullptr;
    return reinterpret_cast<Machine*>(v.value<quintptr>());
}

DeviceTimeCorrectionDialog::DeviceTimeCorrectionDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DeviceTimeCorrectionDialog)
{
    ui->setupUi(this);

    connect(ui->deviceSidebar, &QTreeWidget::currentItemChanged,
            this, &DeviceTimeCorrectionDialog::onDeviceChanged);
    connect(ui->btnResetToZero,    &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onResetToZero);
    connect(ui->btnClearCorrection, &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onClearCorrection);
    connect(ui->btnSaveCorrection, &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onSaveStaged);
    connect(ui->btnDiscardChanges, &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onDiscardStaged);
    connect(ui->btnApplyLastNight, &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onApplyLastNight);
    connect(ui->btnDeleteRow,      &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onDeleteRow);
    connect(ui->btnShowInDaily,    &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onShowInDaily);
    connect(ui->historyTable, &QTableWidget::itemSelectionChanged,
            this, &DeviceTimeCorrectionDialog::onHistoryRowSelected);
    connect(ui->historyTable, &QTableWidget::itemClicked,
            this, &DeviceTimeCorrectionDialog::onHistoryRowClicked);
    connect(ui->historyTable, &QTableWidget::itemActivated,
            this, &DeviceTimeCorrectionDialog::onHistoryRowClicked);
    connect(ui->historyTable, &QTableWidget::itemDoubleClicked,
            this, &DeviceTimeCorrectionDialog::onShowInDaily);
    connect(ui->chkAllDevices, &QCheckBox::toggled, this, &DeviceTimeCorrectionDialog::refreshHistory);
    connect(ui->chkAllDays,    &QCheckBox::toggled, this, &DeviceTimeCorrectionDialog::refreshHistory);
    connect(ui->correctionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeviceTimeCorrectionDialog::onTypeChanged);
    connect(ui->offsetTimeEdit, &QTimeEdit::timeChanged,
            this, &DeviceTimeCorrectionDialog::onOffsetTimeChanged);
    connect(ui->btnOffsetSign,  &QPushButton::toggled,
            this, &DeviceTimeCorrectionDialog::onOffsetSignToggled);
    connect(ui->offsetTimeEdit, &BorrowingTimeEdit::steppedPastZero, this, [this](int overshootSecs) {
        bool flippedTo = !ui->btnOffsetSign->isChecked();
        {
            QSignalBlocker b(ui->btnOffsetSign);
            ui->btnOffsetSign->setChecked(flippedTo);
            ui->btnOffsetSign->setText(flippedTo ? "−" : "+");
        }
        ui->offsetTimeEdit->setReversed(flippedTo);
        // timeChanged fires here and onOffsetTimeChanged reads the already-flipped sign
        ui->offsetTimeEdit->setTime(QTime(overshootSecs / 3600, (overshootSecs % 3600) / 60, overshootSecs % 60));
    });
    connect(ui->advStartDate,    &QDateEdit::dateChanged, this, &DeviceTimeCorrectionDialog::onDateControlChanged);
    connect(ui->advEndDate,      &QDateEdit::dateChanged, this, &DeviceTimeCorrectionDialog::onDateControlChanged);
    connect(ui->advEndDateCheck, &QCheckBox::toggled,     this, &DeviceTimeCorrectionDialog::onDateControlChanged);

    ui->offsetTimeEdit->setSelectedSection(QDateTimeEdit::SecondSection);

    ui->historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->historyTable->horizontalHeader()->setStretchLastSection(true);
    ui->historyTable->setItemDelegate(new SelectedRowDelegate(ui->historyTable));

    // The summary and the status line wrap. A wrapped label does not add its extra lines to
    // the layout's minimum height, so without room kept for them the date fields above
    // were squeezed when the text ran to three lines.
    const int lineHeight = fontMetrics().lineSpacing();
    ui->summaryLabel->setMinimumHeight(3 * lineHeight + 6);   // a little over: the third line was clipped
    ui->summaryLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    ui->statusLabel->setMinimumHeight(3 * lineHeight);
    ui->statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
    // Two lines beside Close: its longest message wraps at the dialog's smallest width.
    ui->listStatusLabel->setMinimumHeight(2 * lineHeight + 2 * ui->listStatusLabel->margin() + 2);

    applyTheme();
    m_ready = true;
}

/*! Colours come from the palette, so the labels stay readable on a dark theme. Called again
    from changeEvent() when the theme is switched while the dialog is open. */
void DeviceTimeCorrectionDialog::applyTheme()
{
    const QString ink2 = Theme::css(Theme::Role::Ink2);
    ui->activeDateLabel->setStyleSheet(QString("font-style: italic; color: %1;").arg(ink2));
    ui->totalLabel->setStyleSheet(QString("color: %1;").arg(ink2));
    ui->statusLabel->setStyleSheet("font-weight: 600;");
    ui->listStatusLabel->setStyleSheet("font-weight: 600;");
    // Red on a light theme; the dark theme keeps the warning colour, which reads well there.
    const bool dark = Theme::isDark(QGuiApplication::palette());
    ui->largeDriftWarning->setStyleSheet(
        QString("color: %1;").arg(Theme::css(dark ? Theme::Role::Warn : Theme::Role::Bad)));

    // The three panels (device, editor, saved corrections) each have a frame (set in the
    // .ui) and a title bar, so the sections of the dialog can be told apart at a glance.
    // Only the title labels get a style sheet: one on the panels would also restyle the
    // list and the table inside them.
    const QString titleCss =
        QString("background: %1; color: %2; font-weight: 600; padding: 5px 8px;")
            .arg(Theme::css(Theme::Role::AccentSoft), Theme::css(Theme::Role::Ink));
    for (QLabel* title : {ui->titleDevice, ui->titleEditor, ui->titleSaved})
        title->setStyleSheet(titleCss);
}

void DeviceTimeCorrectionDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    // A switch between light and dark while the dialog is open: the label colours, the mode
    // label, the greyed devices and the table rows were all worked out for the old palette.
    if (event->type() != QEvent::PaletteChange || !m_ready || m_inThemeChange) return;
    m_inThemeChange = true;
    applyTheme();
    refreshModeLabel();
    refreshHistory();   // also refreshes the device list
    m_inThemeChange = false;
}

DeviceTimeCorrectionDialog::~DeviceTimeCorrectionDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void DeviceTimeCorrectionDialog::setDate(const QDate& date)
{
    if (m_hasStagedChange && date != m_date) {
        auto btn = QMessageBox::question(this, tr("Unsaved Correction"),
            tr("You have an unsaved correction. Save before moving to %1?")
                .arg(date.toString("yyyy-MM-dd")),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (btn == QMessageBox::Cancel) return;
        if (btn == QMessageBox::Save)   onSaveStaged();
        else                            clearStagedAndRevert();
    } else if (m_hasStagedChange) {
        clearStagedAndRevert();
    }

    if (date != m_date) setStatus(QString());

    m_date = date;
    setWindowTitle(tr("Time Corrections"));

    populateSidebar();
    Machine* mach = currentMachine();
    refreshTitleArea(mach);
    refreshCurrentOffset();
    refreshHistory();   // resets the editor to a new correction dated m_date
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/*! Every correctable device is listed, whether or not it has data on the day shown in
    Daily: a correction can cover other days, and a device hidden because it was not used
    that night could be neither found nor corrected (GitLab #400). The selected device is
    kept across a change of day, so the selection never moves to another device unasked. */
void DeviceTimeCorrectionDialog::populateSidebar()
{
    if (!p_profile) return;

    QTreeWidget* tree = ui->deviceSidebar;
    Machine* prevMachine = currentMachine();

    tree->blockSignals(true);
    tree->clear();

    // "CPAP devices", not "Reference": these devices are corrected like any other, and
    // Drift Analysis uses "reference device" for the trusted non-CPAP device.
    auto* refHeader = new QTreeWidgetItem(tree, QStringList{tr("CPAP devices")});
    refHeader->setFlags(Qt::ItemIsEnabled);
    QFont hf = refHeader->font(0);
    hf.setBold(true);
    refHeader->setFont(0, hf);

    auto* otherHeader = new QTreeWidgetItem(tree, QStringList{tr("Other devices")});
    otherHeader->setFlags(Qt::ItemIsEnabled);
    otherHeader->setFont(0, hf);

    // First choice when nothing was selected before: a device with data on the day shown.
    QTreeWidgetItem* firstCpap      = nullptr;
    QTreeWidgetItem* firstOther     = nullptr;
    QTreeWidgetItem* firstCpapToday = nullptr;
    QTreeWidgetItem* firstOtherToday = nullptr;
    QTreeWidgetItem* prevItem       = nullptr;

    for (Machine* mach : p_profile->GetMachines()) {
        if (!Machine::isCorrectableType(mach->type())) continue;
        const bool hasDataToday = m_date.isValid() && mach->day.contains(m_date);

        QTreeWidgetItem* item;
        if (mach->type() == MT_CPAP) {
            item = new QTreeWidgetItem(refHeader, QStringList{deviceLabel(mach)});
            if (!firstCpap) firstCpap = item;
            if (hasDataToday && !firstCpapToday) firstCpapToday = item;
        } else {
            item = new QTreeWidgetItem(otherHeader, QStringList{deviceLabel(mach)});
            if (!firstOther) firstOther = item;
            if (hasDataToday && !firstOtherToday) firstOtherToday = item;
        }
        item->setData(0, Qt::UserRole, QVariant::fromValue(reinterpret_cast<quintptr>(mach)));
        if (mach == prevMachine) prevItem = item;
    }

    tree->expandAll();
    tree->blockSignals(false);
    refreshSidebarText();

    QTreeWidgetItem* choice = prevItem;
    if (!choice) choice = firstCpapToday;
    if (!choice) choice = firstOtherToday;
    if (!choice) choice = firstCpap;
    if (!choice) choice = firstOther;
    if (choice)
        tree->setCurrentItem(choice);   // onDeviceChanged() sets m_machine when it differs
    else
        m_machine = nullptr;            // no correctable device in the profile
}

void DeviceTimeCorrectionDialog::refreshSidebarText()
{
    QTreeWidget* tree = ui->deviceSidebar;
    const QColor muted = Theme::color(Theme::Role::Ink3, palette());

    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* header = tree->topLevelItem(i);
        for (int j = 0; j < header->childCount(); ++j) {
            QTreeWidgetItem* item = header->child(j);
            Machine* mach = machineOf(item);
            if (!mach) continue;

            // The list shows one line per device; the details are in the tooltip, one per
            // line (rich text, so a long line wraps).
            const QString label = deviceLabel(mach);
            QStringList tip;
            tip << "<b>" + label.toHtmlEscaped() + "</b>";
            if (mach->day.isEmpty()) {
                tip << tr("No data");
            } else {
                const QString first = mach->day.firstKey().toString(Qt::ISODate);
                const QString last  = mach->day.lastKey().toString(Qt::ISODate);
                tip << (first == last ? tr("Data: %1").arg(first)
                                      : tr("Data: %1 to %2").arg(first, last));
            }

            const int count = m_correctionCounts.value(mach, 0);
            tip << (count > 0 ? tr("Corrections: %1").arg(count) : tr("No corrections"));

            const bool noDataToday = m_date.isValid() && !mach->day.contains(m_date);
            if (noDataToday)
                tip << tr("This device has no data on %1, the day shown in Daily. "
                          "You can still add corrections for other dates.")
                           .arg(m_date.toString(Qt::ISODate));

            item->setText(0, label);
            item->setToolTip(0, tip.join("<br>"));
            if (noDataToday)
                item->setForeground(0, muted);
            else
                item->setData(0, Qt::ForegroundRole, QVariant());
        }
    }
}

Machine* DeviceTimeCorrectionDialog::currentMachine() const
{
    // Not the list's current item: that can be a group heading, which is not a device.
    return p_profile ? m_machine : nullptr;
}

void DeviceTimeCorrectionDialog::refreshTitleArea(Machine* mach)
{
    ui->deviceNameLabel->setText(mach ? deviceLabel(mach) : tr("—"));
}

QString DeviceTimeCorrectionDialog::currentTypeName() const
{
    return kTypeKeys.value(ui->correctionTypeCombo->currentIndex(), "offset");
}

QString DeviceTimeCorrectionDialog::formatOffset(qint64 ms) const
{
    bool neg = ms < 0;
    qint64 abs = neg ? -ms : ms;
    qint64 s   = abs / 1000;
    return QString("%1%2:%3:%4")
        .arg(neg ? "-" : "+")
        .arg(s / 3600)
        .arg((s / 60) % 60, 2, 10, QChar('0'))
        .arg(s % 60,         2, 10, QChar('0'));
}

QString DeviceTimeCorrectionDialog::deviceLabel(Machine* mach) const
{
    if (!mach) return QString();
    QString name = (mach->brand() + " " + mach->model()).trimmed();
    if (name.isEmpty()) name = mach->loaderName();
    if (!mach->serial().isEmpty()) name += " (" + mach->serial() + ")";
    return name;
}

QString DeviceTimeCorrectionDialog::typeLabel(const QString& type) const
{
    if (type == "drift") return tr("Drift model");
    int idx = kTypeKeys.indexOf(type);
    return idx < 0 ? type : ui->correctionTypeCombo->itemText(idx);
}

QString DeviceTimeCorrectionDialog::rangeText(const QString& dateFrom, const QString& dateTo) const
{
    if (dateTo.isEmpty())   return tr("from %1 onward").arg(dateFrom);
    if (dateTo == dateFrom) return tr("on %1").arg(dateFrom);
    return tr("from %1 to %2").arg(dateFrom, dateTo);
}

bool DeviceTimeCorrectionDialog::coversDate(const QString& dateFrom, const QString& dateTo, const QDate& date)
{
    if (!date.isValid()) return false;
    QDate from = QDate::fromString(dateFrom, Qt::ISODate);
    QDate to   = dateTo.isEmpty() ? QDate(9999, 12, 31) : QDate::fromString(dateTo, Qt::ISODate);
    return date >= from && date <= to;
}

void DeviceTimeCorrectionDialog::setStatus(const QString& text)
{
    ui->statusLabel->setText(text);
    showListStatus(QString());
}

/*! Show in Daily and Delete Correction are in the Saved Corrections panel, so their result
    is shown below that panel, beside Close, and not in the editor above it. The label is
    framed only while it has a message; an empty frame would look like an input field. */
void DeviceTimeCorrectionDialog::setListStatus(const QString& text)
{
    ui->statusLabel->clear();
    showListStatus(text);
}

void DeviceTimeCorrectionDialog::showListStatus(const QString& text)
{
    ui->listStatusLabel->setText(text);
    ui->listStatusLabel->setFrameShape(text.isEmpty() ? QFrame::NoFrame : QFrame::StyledPanel);
}

void DeviceTimeCorrectionDialog::rebuildMachine(Machine* mach)
{
    Machine::reloadCorrectionsFromDb(mach);
}

void DeviceTimeCorrectionDialog::commitAndRefresh(Machine* mach)
{
    rebuildMachine(mach);
    refreshCurrentOffset();
    refreshHistory();
    emit correctionsChanged();
}

// ---------------------------------------------------------------------------
// Corrections panel
// ---------------------------------------------------------------------------

/*! The device in effect is kept in m_machine, not read from the list's current item. An
    arrow key or a click can make a group heading the current item; the device then stays
    in effect and stays highlighted. Before, the dialog was left with no device: the editor
    still named the last one, but the next refresh emptied the table and Save did nothing. */
void DeviceTimeCorrectionDialog::onDeviceChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    Q_UNUSED(previous);
    Machine* newMach = machineOf(current);
    if (!newMach || newMach == m_machine) {
        // A heading, or back from a heading to the same device: nothing changes. Moving to
        // a heading clears the selection, so show again which device is in effect. Queued,
        // because a mouse click changes the selection after it changes the current item.
        QMetaObject::invokeMethod(this, &DeviceTimeCorrectionDialog::highlightDevice, Qt::QueuedConnection);
        return;
    }

    Machine* prevMach = currentMachine();
    m_machine = newMach;
    if (m_hasStagedChange) {
        resetToNewMode();
        if (prevMach) {
            rebuildMachine(prevMach);
            emit correctionsChanged();
        }
    }
    m_staged = {};
    m_hasStagedChange = false;
    setStatus(QString());
    Machine* mach = currentMachine();
    refreshTitleArea(mach);
    refreshHistory();   // resetToNewMode inside calls refreshCurrentOffset
}

void DeviceTimeCorrectionDialog::highlightDevice()
{
    QTreeWidget* tree = ui->deviceSidebar;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* header = tree->topLevelItem(i);
        for (int j = 0; j < header->childCount(); ++j) {
            QTreeWidgetItem* item = header->child(j);
            if (m_machine && machineOf(item) == m_machine && !item->isSelected())
                item->setSelected(true);
        }
    }
}

void DeviceTimeCorrectionDialog::refreshCurrentOffset()
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid()) {
        ui->largeDriftWarning->setText("");
        return;
    }
    // Show the row's offset when one is loaded (viewing or staging).
    // In new-correction mode show 0 — the historyTable lists existing corrections.
    qint64 displayMs = (m_staged.machineId != 0) ? m_staged.offsetMs : 0;
    bool negative = (displayMs < 0);

    // QSignalBlockers on both to prevent re-entrant loops:
    // setChecked() → onOffsetSignToggled() → onOffsetTimeChanged() → previewStaged() → here
    // setTime()    → onOffsetTimeChanged() → previewStaged() → here
    {
        QSignalBlocker bs(ui->btnOffsetSign);
        ui->btnOffsetSign->setChecked(negative);
        ui->btnOffsetSign->setText(negative ? "−" : "+");
    }
    static constexpr qint64 kMaxDisplayMs = 86399999LL; // QTime max: 23:59:59.999
    bool overflows = qAbs(displayMs) > kMaxDisplayMs;
    qint64 clampedMs = overflows ? kMaxDisplayMs : qAbs(displayMs);
    {
        QSignalBlocker bt(ui->offsetTimeEdit);
        ui->offsetTimeEdit->setTime(QTime(0, 0, 0).addMSecs(clampedMs));
    }
    ui->offsetTimeEdit->setReversed(negative);

    if (overflows) {
        ui->largeDriftWarning->setText(
            tr("Offset exceeds 24 hours and cannot be displayed precisely."));
        return;
    }
    bool isOffset = (ui->correctionTypeCombo->currentIndex() == 0);
    bool showWarning = isOffset && qAbs(displayMs) > kLargeOffsetThresholdMs;
    ui->largeDriftWarning->setText(showWarning
        ? tr("Large offset — consider using a different correction type.") : "");
}

/*! By default the table lists the corrections of the selected device that are in force on
    the day shown in Daily. "Show all devices" and "Show all days with corrections" widen
    it, so a correction that does not cover the day shown can still be seen, found and
    edited (GitLab #400); rows that do not apply to that day are greyed. The label above the
    table says how many saved corrections are not listed, so none is hidden silently. */
void DeviceTimeCorrectionDialog::refreshHistory()
{
    // Block signals for the entire population so Qt's auto-selection of row 0
    // (fired by setRowCount when the current index was invalid) cannot trigger
    // onHistoryRowSelected while the table is being filled.
    ui->historyTable->blockSignals(true);
    ui->historyTable->setRowCount(0);
    m_historyRows.clear();
    m_historyMachines.clear();
    m_correctionCounts.clear();
    if (!p_profile) {
        ui->historyTable->blockSignals(false);
        return;
    }

    const bool onlyDevice = !ui->chkAllDevices->isChecked();
    const bool onlyActive = !ui->chkAllDays->isChecked() && m_date.isValid();
    Machine* selected = currentMachine();
    int total = 0;   // all active corrections, listed or not

    struct Entry {
        DeviceTimeCorrectionData row;
        Machine* mach;
        QString  device;
    };
    QList<Entry> entries;

    DeviceTimeCorrectionRepository repo;
    for (Machine* mach : p_profile->GetMachines()) {
        if (!Machine::isCorrectableType(mach->type()) || mach->getDatabaseId() <= 0) continue;
        const QList<DeviceTimeCorrectionData> rows = repo.findActive(mach->getDatabaseId());
        m_correctionCounts[mach] = rows.size();
        total += rows.size();
        if (onlyDevice && mach != selected) continue;
        const QString device = deviceLabel(mach);
        for (const auto& r : rows) {
            if (onlyActive && !coversDate(r.dateFrom, r.dateTo, m_date)) continue;
            entries.append({r, mach, device});
        }
    }

    // ISO dates sort correctly as text.
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.row.dateFrom != b.row.dateFrom) return a.row.dateFrom < b.row.dateFrom;
        if (a.device != b.device)             return a.device < b.device;
        return a.row.id < b.row.id;
    });

    const bool dark = Theme::isDark(palette());
    const QColor muted = Theme::color(Theme::Role::Ink3, palette());
    int greyed = 0;   // rows listed that do not apply to the day shown in Daily
    QFont boldFont = ui->historyTable->font();
    boldFont.setBold(true);
    ui->historyTable->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const auto& r = entries[i].row;
        m_historyRows.append(r);
        m_historyMachines.append(entries[i].mach);

        QString appliedDateLocal;
        {
            QDateTime utc = QDateTime::fromString(r.appliedAt, Qt::ISODate);
            utc.setTimeSpec(Qt::UTC);
            appliedDateLocal = utc.toLocalTime().toString("yyyy-MM-dd");
        }

        const QStringList cells = {
            entries[i].device,
            r.dateFrom,
            r.dateTo.isEmpty() ? tr("no end") : r.dateTo,
            typeLabel(r.type),
            r.type == "drift" ? tr("varies") : formatOffset(r.offsetMs),
            appliedDateLocal
        };

        // Rows that apply to the day shown are bold; rows for other dates are greyed, as
        // devices with no data on that day are in the device list. A row looks the same
        // whichever boxes are ticked.
        const bool applies    = coversDate(r.dateFrom, r.dateTo, m_date);
        const bool otherDates = m_date.isValid() && !applies;
        if (otherDates) ++greyed;
        const QColor bg = typeTint(r.type, dark);
        for (int c = 0; c < kHistoryColumns; ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            if (applies)    item->setFont(boldFont);
            if (otherDates) item->setForeground(muted);
            if (bg.isValid()) item->setBackground(bg);
            ui->historyTable->setItem(i, c, item);
        }
    }

    // What is listed, then how many saved corrections the two boxes leave out.
    QString heading;
    const QString day = m_date.toString(Qt::ISODate);
    if (total == 0) {
        heading = tr("No corrections have been saved.");
    } else if (onlyActive) {
        if (entries.isEmpty())
            heading = onlyDevice ? tr("No corrections for the selected device on %1, the day shown in Daily.").arg(day)
                                 : tr("No corrections on %1, the day shown in Daily.").arg(day);
        else
            heading = onlyDevice ? tr("Corrections for the selected device on %1, the day shown in Daily.").arg(day)
                                 : tr("Corrections on %1, the day shown in Daily.").arg(day);
    } else {
        if (entries.isEmpty())
            heading = tr("No corrections for the selected device.");   // only reachable with onlyDevice
        else
            heading = onlyDevice ? tr("All corrections for the selected device.") : tr("All saved corrections.");
        if (greyed > 0)
            heading += " " + tr("Highlighted rows apply to %1, the day shown in Daily.").arg(day);
    }
    const int hidden = total - entries.size();
    qDebug() << "Time Corrections: listing" << entries.size() << "of" << total << "corrections; device"
             << (selected ? deviceLabel(selected) : QStringLiteral("(none)")) << "day" << day
             << "all devices" << !onlyDevice << "all days" << ui->chkAllDays->isChecked();
    if (hidden > 0)
        heading += " " + tr("Other saved corrections not listed: %1.").arg(hidden);
    ui->activeDateLabel->setText(heading);

    refreshSidebarText();
    ui->historyTable->blockSignals(false);

    if (!m_hasStagedChange) {
        resetToNewMode();
    } else {
        // Keep the row being edited selected across a refresh (a filter was toggled).
        if (m_staged.id != 0) selectHistoryRowById(m_staged.id);
        updateControlStates();
    }
}

void DeviceTimeCorrectionDialog::selectHistoryRowById(qint64 id)
{
    for (int k = 0; k < m_historyRows.size(); ++k) {
        if (m_historyRows[k].id != id) continue;
        {
            QSignalBlocker b(ui->historyTable);
            ui->historyTable->setCurrentCell(k, 0);
        }
        ui->historyTable->scrollToItem(ui->historyTable->item(k, 0));
        break;
    }
    updateControlStates();
}

void DeviceTimeCorrectionDialog::effectiveDateRange(QString& dateFrom, QString& dateTo) const
{
    dateFrom = ui->advStartDate->date().toString(Qt::ISODate);
    // Open-ended rows are always stored as NULL (empty string) in the database.
    dateTo   = ui->advEndDateCheck->isChecked()
               ? QString()
               : ui->advEndDate->date().toString(Qt::ISODate);
}

bool DeviceTimeCorrectionDialog::datesValid() const
{
    return ui->advEndDateCheck->isChecked()
        || ui->advEndDate->date() >= ui->advStartDate->date();
}

// ---------------------------------------------------------------------------
// Staged preview helpers
// ---------------------------------------------------------------------------

void DeviceTimeCorrectionDialog::updateControlStates()
{
    ui->btnSaveCorrection->setEnabled(m_hasStagedChange && datesValid());
    ui->btnDiscardChanges->setEnabled(m_hasStagedChange);

    ui->advEndDate->setEnabled(!ui->advEndDateCheck->isChecked());

    int selRow = ui->historyTable->currentRow();
    bool hasRow = selRow >= 0 && selRow < m_historyRows.size();
    bool isDrift = hasRow && m_historyRows[selRow].type == "drift";
    ui->btnDeleteRow->setEnabled(hasRow && !isDrift);
    ui->btnShowInDaily->setEnabled(hasRow);

    refreshSummary();
}

/*! The summary says in words what Save will store, and names the device with its serial
    number, so a correction cannot go to the wrong device or the wrong dates unnoticed. It
    also says when the preview cannot be seen because the dates or the device have nothing
    on the day shown in Daily. The total is the sum of every correction in force for the
    device on that day, the staged one included. */
void DeviceTimeCorrectionDialog::refreshSummary()
{
    Machine* mach = currentMachine();
    if (!mach) {
        ui->summaryLabel->setText(QString());
        ui->totalLabel->setText(QString());
        return;
    }

    const QString device = deviceLabel(mach);
    const QString day    = m_date.toString(Qt::ISODate);
    const qint64  ms     = (m_staged.machineId != 0) ? m_staged.offsetMs : 0;
    int selRow = ui->historyTable->currentRow();
    const bool driftSelected = selRow >= 0 && selRow < m_historyRows.size()
                               && m_historyRows[selRow].type == "drift" && m_staged.machineId == 0;

    QString summary;
    bool warn = false;
    if (driftSelected) {
        // The mode label already says where a drift model is edited.
    } else if (!datesValid()) {
        summary = tr("The To date is before the From date.");
        warn = true;
    } else if (ms == 0 && m_staged.id == 0) {
        summary = tr("Enter a time correction for %1.").arg(device);
    } else if (ms == 0) {
        summary = tr("A time of zero removes this correction from %1.").arg(device);
    } else {
        QString from, to;
        effectiveDateRange(from, to);
        summary = tr("Shifts %1 by %2 %3.").arg(device, formatOffset(ms), rangeText(from, to));
        if (m_date.isValid() && !coversDate(from, to, m_date)) {
            summary += " " + tr("These dates do not include %1, the day shown in Daily, "
                                "so the graphs will not change.").arg(day);
        } else if (m_date.isValid() && !mach->day.contains(m_date)) {
            summary += " " + tr("This device has no data on %1, so the graphs will not change.").arg(day);
        }
    }
    ui->summaryLabel->setStyleSheet(warn
        ? QString("color: %1;").arg(Theme::css(Theme::Role::Warn)) : QString());
    ui->summaryLabel->setText(summary);

    ui->totalLabel->setText(m_date.isValid()
        ? tr("Total of all corrections for this device on %1: %2")
              .arg(day, formatOffset(mach->correctionMs(m_date)))
        : QString());
}

void DeviceTimeCorrectionDialog::previewStaged(Machine* mach)
{
    DeviceTimeCorrectionRepository repo;
    QList<DeviceTimeCorrectionData> dbRows = repo.findActive(mach->getDatabaseId());

    QString previewFrom, previewTo;
    effectiveDateRange(previewFrom, previewTo);
    QString previewType = currentTypeName();

    QList<TimeCorrectionRow> rows;
    rows.reserve(dbRows.size() + 1);
    for (const auto& d : dbRows) {
        bool exclude = (m_staged.id != 0)
            ? (d.id == m_staged.id)
            : (d.type == previewType && d.c1 == 0.0 &&
               d.dateFrom == previewFrom &&
               (previewTo.isEmpty() ? d.dateTo.isEmpty() : d.dateTo == previewTo));
        if (exclude) continue;
        TimeCorrectionRow r;
        r.dateFrom = QDate::fromString(d.dateFrom, Qt::ISODate);
        r.dateTo   = d.dateTo.isEmpty() ? QDate() : QDate::fromString(d.dateTo, Qt::ISODate);
        // The type must be carried over: Machine::correctionMs() tells a drift model from a
        // constant correction by it. Same conversion as Machine::reloadCorrectionsFromDb().
        r.type     = d.type;
        r.offsetMs = d.offsetMs;
        r.c0Ms     = d.c0Ms;
        r.c1       = (d.type == "drift" && d.c1 >= 1.0) ? d.c1 - 1.0 : d.c1;
        rows.append(r);
    }

    if (m_staged.offsetMs != 0) {
        TimeCorrectionRow staged;
        staged.dateFrom = QDate::fromString(previewFrom, Qt::ISODate);
        staged.dateTo   = previewTo.isEmpty() ? QDate() : QDate::fromString(previewTo, Qt::ISODate);
        staged.type     = previewType;
        staged.offsetMs = m_staged.offsetMs;
        rows.append(staged);
    }

    // Nothing is written here, so the table is left alone and keeps its selection.
    mach->rebuildCorrections(rows);
    refreshCurrentOffset();
    refreshSummary();
    emit correctionsChanged();
}

void DeviceTimeCorrectionDialog::clearStagedAndRevert()
{
    resetToNewMode();
    Machine* mach = currentMachine();
    if (mach) {
        rebuildMachine(mach);
        emit correctionsChanged();
    }
    refreshSummary();   // the total no longer includes the staged correction
}

void DeviceTimeCorrectionDialog::resetToNewMode()
{
    m_hasStagedChange = false;
    m_staged = {};
    m_datesTouched = false;

    {
        QSignalBlocker b(ui->historyTable);
        ui->historyTable->clearSelection();
        ui->historyTable->setCurrentItem(nullptr);
    }

    {
        QSignalBlocker b1(ui->correctionTypeCombo);
        QSignalBlocker b3(ui->advStartDate);
        QSignalBlocker b4(ui->advEndDate);
        QSignalBlocker b5(ui->advEndDateCheck);

        ui->correctionTypeCombo->setCurrentIndex(0);
        if (m_date.isValid()) {
            ui->advStartDate->setDate(m_date);
            ui->advEndDate->setDate(m_date);
            ui->advStartDate->setCurrentSection(QDateTimeEdit::DaySection);
            ui->advEndDate->setCurrentSection(QDateTimeEdit::DaySection);
        }
        ui->advEndDateCheck->setChecked(false);
    }

    refreshCurrentOffset();
    refreshModeLabel();
    updateControlStates();
}

void DeviceTimeCorrectionDialog::refreshModeLabel()
{
    const QString italic = QStringLiteral("color: %1; font-style: italic;");
    int selRow = ui->historyTable->currentRow();
    if (selRow >= 0 && selRow < m_historyRows.size() && m_historyRows[selRow].type == "drift"
            && m_staged.machineId == 0) {
        ui->modeLabel->setText(tr("Drift model — use Drift Analysis dialog to edit"));
        ui->modeLabel->setStyleSheet(italic.arg(Theme::css(Theme::Role::Ink2)));
        return;
    }
    if (m_staged.id == 0) {
        ui->modeLabel->setText(tr("New correction"));
        ui->modeLabel->setStyleSheet(italic.arg(Theme::css(Theme::Role::Good)));
    } else {
        QString toStr = m_staged.dateTo.isEmpty() ? tr("open") : m_staged.dateTo;
        QString desc = QString("%1  %2 – %3").arg(typeLabel(m_staged.type), m_staged.dateFrom, toStr);
        ui->modeLabel->setText(tr("Editing: %1").arg(desc));
        ui->modeLabel->setStyleSheet(italic.arg(Theme::css(Theme::Role::Warn)));
    }
}

static void closeOpenTimezoneRows(DeviceTimeCorrectionRepository& repo, qint64 machineId,
                                  const QString& newFrom)
{
    QDate newDate = QDate::fromString(newFrom, Qt::ISODate);
    for (const auto& r : repo.findActive(machineId)) {
        if (r.type != "timezone") continue;
        if (!r.dateTo.isEmpty()) continue;
        QDate from = QDate::fromString(r.dateFrom, Qt::ISODate);
        if (from >= newDate)
            repo.markUndone(r.id);
        else
            repo.updateDateTo(r.id, newDate.addDays(-1).toString(Qt::ISODate));
    }
}

/*! After the correction is written the result is reported in the status line and the new
    row is selected in the table, so a Save always shows something, including when the
    correction does not affect the day shown in Daily (GitLab #400). */
void DeviceTimeCorrectionDialog::onSaveStaged()
{
    if (!m_hasStagedChange || !datesValid()) return;
    Machine* mach = currentMachine();
    if (!mach || mach->getDatabaseId() <= 0) return;

    // Always read date range and type from the current UI state at save time.
    // The staged row may have been created before the user set the date range.
    QString dateFrom, dateTo;
    effectiveDateRange(dateFrom, dateTo);
    QString type = currentTypeName();
    const qint64 offsetMs = m_staged.offsetMs;
    const bool   wasEdit  = (m_staged.id != 0);

    DeviceTimeCorrectionRepository repo;

    if (type == "timezone")
        closeOpenTimezoneRows(repo, mach->getDatabaseId(), dateFrom);

    if (m_staged.id != 0)
        repo.markUndone(m_staged.id);

    const qint64 newId = repo.upsertTyped(mach->getDatabaseId(), dateFrom, dateTo, type, offsetMs);

    m_hasStagedChange = false;
    m_staged = {};
    updateControlStates();
    commitAndRefresh(mach);

    if (newId < 0) {
        QMessageBox::warning(this, tr("Time Corrections"), tr("Failed to save correction."));
        return;
    }
    if (offsetMs == 0) {
        setStatus(wasEdit ? tr("Correction removed: its time was set to zero.")
                          : tr("No correction stored: the time is zero."));
        return;
    }

    QString msg = tr("Saved: %1 of %2 for %3, %4.")
        .arg(typeLabel(type), formatOffset(offsetMs), deviceLabel(mach), rangeText(dateFrom, dateTo));
    if (m_date.isValid() && !coversDate(dateFrom, dateTo, m_date)) {
        msg += " " + tr("It does not apply to %1, the day shown in Daily, so the graphs have not changed.")
                         .arg(m_date.toString(Qt::ISODate));
    } else if (m_date.isValid() && !mach->day.contains(m_date)) {
        msg += " " + tr("This device has no data on %1, so the graphs have not changed.")
                         .arg(m_date.toString(Qt::ISODate));
    }

    // A correction for other dates is not in the default list. Widen the list so the new
    // row can be seen and selected; without this it would seem to vanish (GitLab #400).
    bool listed = false;
    for (const auto& r : m_historyRows)
        if (r.id == newId) { listed = true; break; }
    if (!listed)
        ui->chkAllDays->setChecked(true);   // toggled() refreshes the table

    setStatus(msg);
    selectHistoryRowById(newId);
}

void DeviceTimeCorrectionDialog::onDiscardStaged()
{
    clearStagedAndRevert();
    refreshHistory();
}

void DeviceTimeCorrectionDialog::onClearCorrection()
{
    clearStagedAndRevert();
    resetToNewMode();
}

void DeviceTimeCorrectionDialog::applyNudge(qint64 deltaMs)
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid() || mach->getDatabaseId() <= 0) return;

    QString type = currentTypeName();
    QString dateFrom, dateTo;
    effectiveDateRange(dateFrom, dateTo);

    if (!m_hasStagedChange) {
        m_staged.machineId = mach->getDatabaseId();
        if (m_staged.id == 0) {
            // New-entry mode: initialize staged position and seed offset from any
            // existing row at the same position.
            m_staged.dateFrom = dateFrom;
            m_staged.dateTo   = dateTo;
            m_staged.type     = type;
            m_staged.offsetMs = 0;
            DeviceTimeCorrectionRepository repo;
            for (const auto& r : repo.findActive(mach->getDatabaseId())) {
                if (r.type != type || r.c1 != 0.0 || r.dateFrom != dateFrom) continue;
                bool sameTo = dateTo.isEmpty() ? r.dateTo.isEmpty() : (r.dateTo == dateTo);
                if (sameTo) { m_staged.offsetMs = r.offsetMs; break; }
            }
        }
        // Edit mode (id != 0): dateFrom/dateTo/type/offsetMs already seeded by
        // populateControlsFromRow — preserve them.
        m_hasStagedChange = true;
        updateControlStates();
    }

    m_staged.offsetMs += deltaMs;
    previewStaged(mach);
}

void DeviceTimeCorrectionDialog::onNudgeMinus1h() { applyNudge(-3600000LL); }
void DeviceTimeCorrectionDialog::onNudgeMinus1m() { applyNudge(-60000LL);   }
void DeviceTimeCorrectionDialog::onNudgeMinus1s() { applyNudge(-1000LL);    }
void DeviceTimeCorrectionDialog::onNudgePlus1s()  { applyNudge(1000LL);     }
void DeviceTimeCorrectionDialog::onNudgePlus1m()  { applyNudge(60000LL);    }
void DeviceTimeCorrectionDialog::onNudgePlus1h()  { applyNudge(3600000LL);  }

void DeviceTimeCorrectionDialog::onOffsetTimeChanged(const QTime &time)
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid() || mach->getDatabaseId() <= 0) return;

    qint64 absMs = static_cast<qint64>(QTime(0, 0, 0).msecsTo(time));
    bool negative = ui->btnOffsetSign->isChecked();

    if (!m_hasStagedChange) {
        m_staged.machineId = mach->getDatabaseId();
        if (m_staged.id == 0) {
            QString df, dt;
            effectiveDateRange(df, dt);
            m_staged.dateFrom = df;
            m_staged.dateTo   = dt;
            m_staged.type     = currentTypeName();
        }
        m_hasStagedChange = true;
        setStatus(QString());
        updateControlStates();
    }

    m_staged.offsetMs = negative ? -absMs : absMs;
    previewStaged(mach);
}

void DeviceTimeCorrectionDialog::onOffsetSignToggled(bool checked)
{
    ui->btnOffsetSign->setText(checked ? "−" : "+");
    ui->offsetTimeEdit->setReversed(checked);
    qint64 absMs = static_cast<qint64>(QTime(0, 0, 0).msecsTo(ui->offsetTimeEdit->time()));
    if (absMs == 0) return;
    onOffsetTimeChanged(ui->offsetTimeEdit->time());
}

void DeviceTimeCorrectionDialog::onResetToZero()
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid() || mach->getDatabaseId() <= 0) return;

    m_staged.machineId = mach->getDatabaseId();
    if (m_staged.id == 0) {
        QString dateFrom, dateTo;
        effectiveDateRange(dateFrom, dateTo);
        m_staged.dateFrom = dateFrom;
        m_staged.dateTo   = dateTo;
        m_staged.type     = currentTypeName();
    }
    m_staged.offsetMs = 0;
    m_hasStagedChange = true;
    setStatus(QString());
    updateControlStates();
    {
        QSignalBlocker b(ui->offsetTimeEdit);
        ui->offsetTimeEdit->setTime(QTime(0, 0, 0));
    }
    {
        QSignalBlocker b(ui->btnOffsetSign);
        ui->btnOffsetSign->setChecked(false);
        ui->btnOffsetSign->setText("+");
    }
    ui->offsetTimeEdit->setReversed(false);
    previewStaged(mach);
}

void DeviceTimeCorrectionDialog::onApplyLastNight()
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid() || mach->getDatabaseId() <= 0) return;

    QString todayStr = m_date.toString(Qt::ISODate);

    DeviceTimeCorrectionRepository repo;
    QList<DeviceTimeCorrectionData> allActive = repo.findActive(mach->getDatabaseId());

    QList<DeviceTimeCorrectionData> sourceRows;
    for (int d = 1; d <= 7 && sourceRows.isEmpty(); ++d) {
        QString dayStr = m_date.addDays(-d).toString(Qt::ISODate);
        for (const auto& r : allActive) {
            if (r.dateFrom == dayStr && r.dateTo == dayStr && r.c1 == 0.0)
                sourceRows.append(r);
        }
    }

    if (sourceRows.isEmpty()) {
        QMessageBox::information(this, tr("Apply Last Offset"),
            tr("No single-night corrections found in the past 7 days."));
        return;
    }

    for (const auto& r : sourceRows)
        repo.upsertTyped(mach->getDatabaseId(), todayStr, todayStr, r.type, r.offsetMs);

    commitAndRefresh(mach);
    setStatus(tr("Copied the corrections of %1 to %2 for %3.")
                  .arg(sourceRows.first().dateFrom, todayStr, deviceLabel(mach)));
}

DeviceTimeCorrectionDialog::DeleteChoice
DeviceTimeCorrectionDialog::showDeleteRangeDialog(const DeviceTimeCorrectionData& row)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Delete Correction"));
    auto* layout = new QVBoxLayout(&dlg);

    QString toStr = row.dateTo.isEmpty() ? tr("open-ended") : row.dateTo;
    layout->addWidget(new QLabel(
        tr("Delete correction spanning %1 – %2").arg(row.dateFrom, toStr)));

    auto* rbClose = new QRadioButton(
        tr("Close from %1 onward (ends %2, keeps prior dates)")
        .arg(m_date.toString(Qt::ISODate),
             m_date.addDays(-1).toString(Qt::ISODate)));
    auto* rbSplit = new QRadioButton(
        tr("Remove only %1 (splits into two rows)").arg(m_date.toString(Qt::ISODate)));
    auto* rbAll   = new QRadioButton(tr("Remove entirely (marks all dates undone)"));

    rbClose->setChecked(true);
    layout->addWidget(rbClose);
    layout->addWidget(rbSplit);
    layout->addWidget(rbAll);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return DeleteCancelled;
    if (rbSplit->isChecked()) return SplitOut;
    if (rbAll->isChecked())   return RemoveEntirely;
    return CloseForward;
}

void DeviceTimeCorrectionDialog::splitOutDate(const DeviceTimeCorrectionData& row)
{
    DeviceTimeCorrectionRepository repo;
    QDate from = QDate::fromString(row.dateFrom, Qt::ISODate);
    QDate to   = row.dateTo.isEmpty() ? QDate() : QDate::fromString(row.dateTo, Qt::ISODate);

    repo.markUndone(row.id);

    if (from < m_date) {
        DeviceTimeCorrectionData left = row;
        left.id     = 0;
        left.dateTo = m_date.addDays(-1).toString(Qt::ISODate);
        repo.create(left);
    }

    QDate afterDate = m_date.addDays(1);
    if (!to.isValid() || afterDate <= to) {
        DeviceTimeCorrectionData right = row;
        right.id       = 0;
        right.dateFrom = afterDate.toString(Qt::ISODate);
        repo.create(right);
    }
}

/*! A correction that covers the day shown in Daily is deleted as before: at once for a
    single night, or after the "close / split / remove" choice for a range, both of which
    work relative to that day. A correction that does not cover that day cannot be closed
    or split at it, and its removal cannot be seen on the graphs, so it is removed whole
    after a confirmation. */
void DeviceTimeCorrectionDialog::onDeleteRow()
{
    int tableRow = ui->historyTable->currentRow();
    if (tableRow < 0 || tableRow >= m_historyRows.size()) return;
    Machine* mach = m_historyMachines[tableRow];
    if (!mach || mach->getDatabaseId() <= 0) return;

    // A copy: the lists are rebuilt by commitAndRefresh() below.
    const DeviceTimeCorrectionData row = m_historyRows[tableRow];
    if (row.type == "drift") return;
    DeviceTimeCorrectionRepository repo;

    const QString what = tr("%1 of %2 for %3, %4")
        .arg(typeLabel(row.type), formatOffset(row.offsetMs), deviceLabel(mach),
             rangeText(row.dateFrom, row.dateTo));
    const bool covers = coversDate(row.dateFrom, row.dateTo, m_date);
    QString status = tr("Deleted: %1.").arg(what);

    if (row.dateFrom == row.dateTo || !covers) {
        if (!covers) {
            auto btn = QMessageBox::question(this, tr("Delete Correction"),
                tr("Delete this correction?\n\n%1").arg(what),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (btn != QMessageBox::Yes) return;
        }
        if (!repo.markUndone(row.id)) {
            QMessageBox::warning(this, tr("Time Corrections"), tr("Failed to delete correction."));
            return;
        }
    } else {
        DeleteChoice choice = showDeleteRangeDialog(row);
        switch (choice) {
        case CloseForward:
            if (row.dateFrom == m_date.toString(Qt::ISODate)) {
                repo.markUndone(row.id);
            } else {
                const QString lastDay = m_date.addDays(-1).toString(Qt::ISODate);
                repo.updateDateTo(row.id, lastDay);
                status = tr("The correction now ends on %1.").arg(lastDay);
            }
            break;
        case SplitOut:
            splitOutDate(row);
            status = tr("%1 was taken out of the correction.").arg(m_date.toString(Qt::ISODate));
            break;
        case RemoveEntirely:
            repo.markUndone(row.id);
            break;
        case DeleteCancelled:
            return;
        }
    }
    // Any unsaved edit is dropped with the deletion: it may have been an edit of this row.
    m_hasStagedChange = false;
    m_staged = {};
    commitAndRefresh(mach);
    setListStatus(status);
}

/*! Daily is asked for the first day in the correction's dates on which the device has
    data, so the correction's effect can be seen. When the device has no data on those
    dates (a correction entered for the future, say) the status line says so instead. */
void DeviceTimeCorrectionDialog::onShowInDaily()
{
    int tableRow = ui->historyTable->currentRow();
    if (tableRow < 0 || tableRow >= m_historyRows.size()) return;
    Machine* mach = m_historyMachines[tableRow];
    if (!mach) return;

    const DeviceTimeCorrectionData row = m_historyRows[tableRow];
    const QDate from = QDate::fromString(row.dateFrom, Qt::ISODate);
    const QDate to   = row.dateTo.isEmpty() ? QDate() : QDate::fromString(row.dateTo, Qt::ISODate);
    if (!from.isValid()) return;

    auto it = mach->day.lowerBound(from);
    if (it == mach->day.end() || (to.isValid() && it.key() > to)) {
        setListStatus(tr("%1 has no data on the dates this correction covers, so there is nothing to show in Daily.")
                          .arg(deviceLabel(mach)));
        return;
    }
    const QDate target = it.key();

    if (target == m_date) {
        setListStatus(tr("Daily already shows %1.").arg(target.toString(Qt::ISODate)));
        return;
    }

    emit dateRequested(target);   // Daily loads the day, then calls setDate() back
}

// ---------------------------------------------------------------------------
// Click-to-load helpers
// ---------------------------------------------------------------------------

void DeviceTimeCorrectionDialog::populateControlsFromRow(const DeviceTimeCorrectionData& row)
{
    if (row.type == "drift") {
        // Drift rows are managed exclusively by the Drift Analysis dialog.
        // Display them as read-only; do not allow editing or saving from here.
        m_staged = {};
        m_hasStagedChange = false;
        refreshModeLabel();
        updateControlStates();
        return;
    }

    // Seed m_staged so onAnyControlChanged and refreshCurrentOffset use this row's values.
    Machine* mach = currentMachine();
    if (mach && mach->getDatabaseId() > 0) {
        m_staged.id        = row.id;
        m_staged.machineId = mach->getDatabaseId();
        m_staged.dateFrom  = row.dateFrom;
        m_staged.dateTo    = row.dateTo;
        m_staged.type      = row.type;
        m_staged.offsetMs  = row.offsetMs;
    }

    {
        QSignalBlocker b1(ui->correctionTypeCombo);
        QSignalBlocker b3(ui->advStartDate);
        QSignalBlocker b4(ui->advEndDate);
        QSignalBlocker b5(ui->advEndDateCheck);

        int typeIdx = kTypeKeys.indexOf(row.type);
        ui->correctionTypeCombo->setCurrentIndex(typeIdx < 0 ? 0 : typeIdx);

        QDate from = QDate::fromString(row.dateFrom, Qt::ISODate);
        ui->advStartDate->setDate(from);
        if (row.dateTo.isEmpty()) {
            // No end. The To field is disabled; give it the day shown in Daily, the likely
            // end date if the user unticks "No end" to close the correction.
            ui->advEndDateCheck->setChecked(true);
            ui->advEndDate->setDate((m_date.isValid() && m_date >= from) ? m_date : from);
        } else {
            ui->advEndDateCheck->setChecked(false);
            ui->advEndDate->setDate(QDate::fromString(row.dateTo, Qt::ISODate));
        }
        ui->advStartDate->setCurrentSection(QDateTimeEdit::DaySection);
        ui->advEndDate->setCurrentSection(QDateTimeEdit::DaySection);
    }

    refreshCurrentOffset();
    refreshModeLabel();
    updateControlStates();
}

/*! Called when the type or the dates change. Editing a loaded correction stages it. A staged
    correction is previewed again, so the graphs and the total follow the new dates. */
void DeviceTimeCorrectionDialog::onAnyControlChanged()
{
    if (!m_hasStagedChange && m_staged.machineId != 0) {
        m_hasStagedChange = true;
        refreshCurrentOffset();
    }
    if (m_hasStagedChange && datesValid()) {
        Machine* mach = currentMachine();
        if (mach && mach->getDatabaseId() > 0) previewStaged(mach);
    }
    updateControlStates();
}

void DeviceTimeCorrectionDialog::onTypeChanged()
{
    // A time-zone error lasts until the clock is fixed, so a new one has no end by default.
    // The other types default to the single night. Dates the user has set are left alone.
    if (m_staged.id == 0 && !m_datesTouched) {
        QSignalBlocker b(ui->advEndDateCheck);
        ui->advEndDateCheck->setChecked(currentTypeName() == "timezone");
    }
    refreshCurrentOffset();
    onAnyControlChanged();
}

void DeviceTimeCorrectionDialog::onDateControlChanged()
{
    m_datesTouched = true;

    // Moving the From date past the To date, or switching "No end" off, pulls the To date
    // along. A To date typed before the From date is left for the user to finish; Save is
    // disabled and the summary says why.
    if (sender() != ui->advEndDate && !ui->advEndDateCheck->isChecked()
            && ui->advEndDate->date() < ui->advStartDate->date()) {
        QSignalBlocker b(ui->advEndDate);
        ui->advEndDate->setDate(ui->advStartDate->date());
    }
    onAnyControlChanged();
}

void DeviceTimeCorrectionDialog::onHistoryRowSelected()
{
    int tableRow = ui->historyTable->currentRow();
    if (tableRow < 0 || tableRow >= m_historyRows.size()) return;

    Machine* rowMachine = m_historyMachines[tableRow];
    Machine* curMachine = currentMachine();

    if (rowMachine != curMachine) {
        qint64 targetId = m_historyRows[tableRow].id;

        QTreeWidget* tree = ui->deviceSidebar;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* header = tree->topLevelItem(i);
            for (int j = 0; j < header->childCount(); ++j) {
                QTreeWidgetItem* child = header->child(j);
                if (machineOf(child) == rowMachine) {
                    // Switch device; onDeviceChanged → refreshHistory rebuilds the table
                    // synchronously and leaves the editor on a new correction.
                    tree->setCurrentItem(child);

                    // Now select the specific row that was clicked.
                    // Block itemSelectionChanged to avoid re-entering this slot.
                    for (int k = 0; k < m_historyRows.size(); ++k) {
                        if (m_historyRows[k].id == targetId) {
                            ui->historyTable->blockSignals(true);
                            ui->historyTable->setCurrentCell(k, 0);
                            ui->historyTable->blockSignals(false);
                            populateControlsFromRow(m_historyRows[k]);
                            refreshModeLabel();
                            break;
                        }
                    }
                    return;
                }
            }
        }
        return;
    }

    // Same device: discard any staged change, then load from this row
    if (m_hasStagedChange) {
        Machine* mach = currentMachine();
        resetToNewMode();
        if (mach) {
            rebuildMachine(mach);
            emit correctionsChanged();
        }
        // resetToNewMode() cleared the selection; put it back on the clicked row.
        QSignalBlocker b(ui->historyTable);
        ui->historyTable->setCurrentCell(tableRow, 0);
    }
    populateControlsFromRow(m_historyRows[tableRow]);
}

/*! After a Save the new row is selected but the editor is on a new correction, so that the
    next correction entered is added rather than replacing the one just saved. A click on
    that row (or Enter) changes no selection, so it is loaded into the editor here. */
void DeviceTimeCorrectionDialog::onHistoryRowClicked(QTableWidgetItem* item)
{
    if (!item) return;
    int tableRow = item->row();
    if (tableRow < 0 || tableRow >= m_historyRows.size()) return;
    if (tableRow != ui->historyTable->currentRow()) return;
    const DeviceTimeCorrectionData& row = m_historyRows[tableRow];
    if (row.type == "drift" || row.id == m_staged.id) return;   // already shown
    onHistoryRowSelected();
}

void DeviceTimeCorrectionDialog::done(int result)
{
    // The Close button and Esc end the dialog without a close event reaching closeEvent().
    if (m_hasStagedChange)
        clearStagedAndRevert();
    QDialog::done(result);
}

void DeviceTimeCorrectionDialog::closeEvent(QCloseEvent* event)
{
    if (m_hasStagedChange)
        clearStagedAndRevert();
    QDialog::closeEvent(event);
}
