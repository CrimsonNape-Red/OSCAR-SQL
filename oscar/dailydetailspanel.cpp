/* Daily details panel
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "dailydetailspanel.h"

#include <QCoreApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QVBoxLayout>

#include "SleepLib/common.h"
#include "uikit/callout.h"
#include "uikit/collapsiblecard.h"
#include "uikit/comparison.h"
#include "uikit/metriccard.h"
#include "uikit/panel.h"
#include "uikit/theme.h"
#include "uikit/toggleswitch.h"
#include "SleepLib/session.h"

using namespace DailyDetails;
using UiKit::Callout;

namespace {

//! \brief Text in a theme colour on whatever it sits on
QLabel *label(const QString &text, Theme::Role role, QWidget *parent, bool bold = false, bool wrap = false)
{
    auto *l = new QLabel(text, parent);
    l->setTextFormat(Qt::PlainText);
    if (bold) {
        QFont font = l->font();
        font.setBold(true);
        l->setFont(font);
    }
    l->setWordWrap(wrap);
    // The QLabel selector is needed: Qt styles a widget's tooltip from that widget's style sheet,
    // and a rule with no selector would give the tooltip this text colour on no background.
    l->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg(Theme::css(role)));
    return l;
}

//! \brief A string the Classic page also shows, in the Classic page's translation context.
//! Callers mark the literal with QT_TRANSLATE_NOOP("Daily", ...) so lupdate collects it.
QString classic(const char *text)
{
    return QCoreApplication::translate("Daily", text);
}

//! \brief A card in theme colours with a bold title, holding a column of rows
UiKit::Panel *titledCard(const QString &title, QWidget *parent, QVBoxLayout **column)
{
    auto *card = new UiKit::Panel(Theme::Role::Surface, Theme::Role::Line, Theme::Radius, parent);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(4);
    if (!title.isEmpty()) layout->addWidget(label(title, Theme::Role::Ink, card, true));
    *column = layout;
    return card;
}

//! \brief A small square of a channel's colour
class Swatch : public QWidget
{
public:
    Swatch(const QColor &colour, QWidget *parent) : QWidget(parent), m_colour(colour) { setFixedSize(12, 12); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Theme::color(Theme::Role::Line), 1));
        painter.setBrush(m_colour);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }

private:
    QColor m_colour;
};

//! \brief One row of the Events card; a row that links somewhere is clickable
class EventRowWidget : public QWidget
{
public:
    EventRowWidget(const EventRow &row, QWidget *parent, std::function<void()> onClick)
        : QWidget(parent), m_onClick(std::move(onClick))
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(8);
        layout->addWidget(new Swatch(row.colour, this), 0, Qt::AlignVCenter);
        layout->addWidget(label(row.name, Theme::Role::Ink, this, false, true), 1);
        QLabel *count = label(row.count, Theme::Role::Ink2, this);
        count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        count->setMinimumWidth(QFontMetrics(count->font()).horizontalAdvance("0000"));
        layout->addWidget(count);
        QLabel *rate = label(row.rate, Theme::Role::Ink, this, true);
        rate->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rate->setMinimumWidth(QFontMetrics(rate->font()).horizontalAdvance("000.00%"));
        layout->addWidget(rate);
        setToolTip(row.tooltip);
        if (m_onClick) {
            setCursor(Qt::PointingHandCursor);
            setAttribute(Qt::WA_Hover);
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_onClick || !underMouse()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Theme::color(Theme::Role::Surface2));
        painter.drawRoundedRect(QRectF(rect()), 5, 5);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_onClick && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) m_onClick();
    }

private:
    std::function<void()> m_onClick;
};

} // namespace

DailyDetailsPanel::DailyDetailsPanel(QWidget *parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void DailyDetailsPanel::setPage(const Page &page)
{
    const bool sameDay = page.date.isValid() && page.date == m_date;
    const int scroll = verticalScrollBar()->value();
    m_date = page.date;
    m_cards.clear();    // the cards they point to go when setWidget() replaces the column below

    auto *body = new QWidget();
    body->setObjectName("dailyDetailsBody");
    body->setAttribute(Qt::WA_StyledBackground);
    body->setStyleSheet(QString("#dailyDetailsBody { background: %1; }").arg(Theme::css(Theme::Role::Page)));
    m_column = new QVBoxLayout(body);
    m_column->setContentsMargins(6, 6, 6, 6);
    m_column->setSpacing(8);
    m_body = body;

    switch (page.kind) {
    case DayKind::NoData:
        m_column->addWidget(new Callout(Callout::Kind::Info, classic(QT_TRANSLATE_NOOP("Daily", "No data is available for this day.")), QString(), body));
        break;
    case DayKind::ComplianceOnly: {
        m_column->addWidget(new Callout(Callout::Kind::Warning, classic(QT_TRANSLATE_NOOP("Daily", "This CPAP device does NOT record detailed data")),
                                        classic(QT_TRANSLATE_NOOP("Daily", "Sorry, this device only provides compliance data.")), body));
        QVBoxLayout *column = nullptr;
        auto *card = titledCard(QString(), body, &column);
        column->addWidget(usageRow(page.usage, card));
        m_column->addWidget(card);
        if (page.device.present) m_column->addWidget(deviceCard(page.device));
        // Figures from an oximeter or position sensor, as the Classic page shows them
        if (!page.statistics.rows.isEmpty()) m_column->addWidget(statisticsCard(page.statistics));
        break;
    }
    case DayKind::SessionsOff:
        m_column->addWidget(new Callout(Callout::Kind::Warning, classic(QT_TRANSLATE_NOOP("Daily", "Sessions exist for this day but are switched off.")),
                                        QString(), body));
        // No device card: with no session on, there are no settings to show (as on Classic)
        if (!page.statistics.rows.isEmpty()) m_column->addWidget(statisticsCard(page.statistics));
        break;
    case DayKind::ZeroHours:
        m_column->addWidget(new Callout(Callout::Kind::Info, classic(QT_TRANSLATE_NOOP("Daily", "Impossibly short session")), QString(), body));
        break;
    case DayKind::NonCpapOnly:
        // Data from other devices without CPAP data is normal: no callout, only the cards
        if (page.usage.present) m_column->addWidget(timesCard(page.usage));
        m_column->addWidget(statisticsCard(page.statistics));
        break;
    case DayKind::Cpap:
        m_column->addWidget(ahiCard(page));
        if (page.device.present) m_column->addWidget(deviceCard(page.device));
        if (!page.events.isEmpty()) m_column->addWidget(eventsCard(page.events));
        if (page.statistics.present) m_column->addWidget(statisticsCard(page.statistics));
        break;
    }
    if (page.statistics.summaryOnlyNote && (page.kind == DayKind::Cpap || page.kind == DayKind::NonCpapOnly)) {
        m_column->addWidget(new Callout(Callout::Kind::Info, QString(),
                                        STR_MessageBox_PleaseNote + " " + classic(QT_TRANSLATE_NOOP("Daily", "This day just contains summary data, only limited information is available.")),
                                        body));
    }

    // The cards that start closed, in the order of the Classic page's sections
    if (page.kind == DayKind::Cpap && page.showPie && (!page.pie.isNull() || page.pieZero)) {
        m_column->addWidget(eventBreakdownCard(page));
    }
    if (page.oximeter.present) m_column->addWidget(oximeterCard(page.oximeter));
    if (page.settings.present) m_column->addWidget(settingsCard(page.settings));
    if (page.kind != DayKind::NoData && !page.sessions.isEmpty()) m_column->addWidget(sessionsCard(page.sessions));

    m_column->addStretch(1);

    setWidget(body);    // deletes the previous cards
    if (sameDay) {
        QTimer::singleShot(0, this, [this, scroll] { verticalScrollBar()->setValue(scroll); });
    } else {
        verticalScrollBar()->setValue(0);
    }
}

QStringList DailyDetailsPanel::texts() const
{
    QStringList out;
    if (!m_body) return out;
    for (QLabel *l : m_body->findChildren<QLabel *>()) {
        if (l->isVisibleTo(m_body) && !l->text().isEmpty()) out << l->text();
    }
    return out;
}

QWidget *DailyDetailsPanel::ahiCard(const Page &page)
{
    const Headline &h = page.headline;
    auto *card = new UiKit::MetricCard();
    card->setFlat(true);                // the collapsible card around it draws the frame and tint
    card->setToolTip(h.fullName);
    card->setValue(QString::number(h.value, 'f', 2), tr("events per hour"));
    if (h.hasShares) {
        card->setSubtitle(QString("%1 %2 · %3 %4").arg(STR_TR_OAHI).arg(h.obstructive, 0, 'f', 2)
                              .arg(STR_TR_CAHI).arg(h.central, 0, 'f', 2));
    }
    // The night against the user's recent nights, by the Welcome page's rule
    const UiKit::Comparison c7 = UiKit::compare(h.value, page.avg7, page.nights7, UiKit::Better::Lower);
    const UiKit::Comparison c30 = UiKit::compare(h.value, page.avg30, page.nights30, UiKit::Better::Lower);
    const auto average = [](double avg, int nights) {
        return nights > 0 ? QString::number(avg, 'f', 2) : QString::fromUtf8("—");
    };
    card->setComparisons(tr("7 nights"), average(page.avg7, page.nights7), c7,
                         tr("30 nights"), average(page.avg30, page.nights30), c30);
    card->setFooterWidget(usageRow(page.usage, card));
    auto *outer = static_cast<UiKit::CollapsibleCard *>(collapsible(DC_Ahi, h.name, card));
    outer->setHeaderToolTip(h.fullName);
    // Green or red only when both comparisons agree (the Welcome page's rule)
    switch (UiKit::statusTint(c7, c30)) {
    case UiKit::StatusTint::Good:
        card->setTint(UiKit::MetricCard::Tint::Good);
        outer->setRoles(Theme::Role::GoodTint, Theme::Role::GoodLine);
        break;
    case UiKit::StatusTint::Bad:
        card->setTint(UiKit::MetricCard::Tint::Bad);
        outer->setRoles(Theme::Role::BadTint, Theme::Role::BadLine);
        break;
    case UiKit::StatusTint::None:
        break;
    }
    return outer;
}

QWidget *DailyDetailsPanel::usageRow(const Usage &usage, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *grid = new QGridLayout(row);
    grid->setContentsMargins(0, 6, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(0);
    const int tt = int(usage.seconds);
    const QString hours = QString::asprintf("%02i:%02i:%02i", tt / 3600, (tt / 60) % 60, tt % 60);
    const QString times = usage.present ? usage.start.toString("HH:mm:ss") + QString::fromUtf8(" – ") + usage.end.toString("HH:mm:ss")
                                        : QString::fromUtf8("—");
    const QList<QPair<QString, QString>> cells = {
        { tr("Usage"), hours }, { tr("Sessions"), QString::number(usage.cpapSessions) }, { tr("Start – End"), times } };
    for (int i = 0; i < cells.size(); ++i) {
        grid->addWidget(label(cells[i].first, Theme::Role::Ink2, row), 0, i);
        QLabel *value = label(cells[i].second, Theme::Role::Ink, row, true, i == cells.size() - 1);
        grid->addWidget(value, 1, i, Qt::AlignTop);
    }
    grid->setColumnStretch(cells.size() - 1, 1);
    return row;
}

QWidget *DailyDetailsPanel::timesCard(const Usage &usage)
{
    QVBoxLayout *column = nullptr;
    auto *card = titledCard(QString(), m_body, &column);
    column->addWidget(label(tr("Start \u2013 End"), Theme::Role::Ink2, card));
    column->addWidget(label(usage.start.toString("HH:mm:ss") + QString::fromUtf8(" \u2013 ") + usage.end.toString("HH:mm:ss"),
                            Theme::Role::Ink, card, true));
    return card;
}

QWidget *DailyDetailsPanel::collapsible(DailyCard which, const QString &title, QWidget *content)
{
    auto *card = new UiKit::CollapsibleCard(title, m_body);
    card->setContent(content);
    card->setOpen(DailyDetails::cardOpen(m_cardsOpen, which));
    connect(card, &UiKit::CollapsibleCard::toggled, this, [this, which](bool open) {
        m_cardsOpen = DailyDetails::setCardOpen(m_cardsOpen, which, open);
        emit cardToggled(which, open);
    });
    m_cards.append({which, card});
    return card;
}

void DailyDetailsPanel::setCaptureOpen(bool capture)
{
    const int bits = capture ? DailyDetails::captureCardsOpen(m_cardsOpen) : m_cardsOpen;
    for (const auto &entry : std::as_const(m_cards)) {
        entry.second->setOpen(DailyDetails::cardOpen(bits, entry.first));
    }
}

int DailyDetailsPanel::contentHeight() const
{
    return widget() ? widget()->height() : 0;
}

QWidget *DailyDetailsPanel::deviceCard(const Device &device)
{
    auto *content = new QWidget();
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(26, 0, 0, 2);
    column->setSpacing(2);
    column->addWidget(label(classic(QT_TRANSLATE_NOOP("Daily", "PAP Mode: %1")).arg(device.mode), Theme::Role::Ink, content, false, true));
    const QString pressure = QTextDocumentFragment::fromHtml(device.pressure).toPlainText().simplified();
    if (!pressure.isEmpty()) column->addWidget(label(pressure, Theme::Role::Ink, content, false, true));
    if (device.settingsMissing) {
        column->addWidget(new Callout(Callout::Kind::Warning, QString(),
                                      tr("Mode and Pressure settings missing; yesterday's shown."), content));
    }
    auto *card = static_cast<UiKit::CollapsibleCard *>(
        collapsible(DC_Device, (device.brand + " " + device.model).trimmed(), content));
    card->setHeaderToolTip(device.tooltip);
    return card;
}

QWidget *DailyDetailsPanel::eventsCard(const QVector<EventRow> &events)
{
    auto *card = new QWidget();
    auto *column = new QVBoxLayout(card);
    column->setContentsMargins(0, 0, 0, 2);
    column->setSpacing(0);
    for (const EventRow &row : events) {
        std::function<void()> onClick;
        if (row.link) {
            const ChannelID code = row.code;
            onClick = [this, code] { emit eventClicked(code); };
        }
        column->addWidget(new EventRowWidget(row, card, onClick));
    }
    return collapsible(DC_Events, tr("Events"), card);
}

QWidget *DailyDetailsPanel::statisticsCard(const DailyDetails::Statistics &statistics)
{
    auto *card = new QWidget();
    auto *column = new QVBoxLayout(card);
    column->setContentsMargins(4, 0, 0, 2);
    auto *table = new QWidget(card);
    auto *grid = new QGridLayout(table);
    grid->setContentsMargins(0, 2, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(1);
    const QStringList headings = { STR_TR_Channel, STR_TR_Min, statistics.midName, statistics.percName, statistics.maxName };
    for (int c = 0; c < headings.size(); ++c) {
        QLabel *l = label(headings[c], Theme::Role::Ink2, table, true);
        if (c > 0) l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(l, 0, c);
    }
    int r = 1;
    for (const StatRow &row : statistics.rows) {
        QLabel *name = label(row.label, Theme::Role::Ink, table, false, true);
        name->setToolTip(row.tooltip);
        grid->addWidget(name, r, 0);
        const QStringList values = { row.min, row.mid, row.perc, row.max };
        for (int c = 0; c < values.size(); ++c) {
            QLabel *l = label(values[c], Theme::Role::Ink, table);
            l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(l, r, c + 1);
        }
        ++r;
    }
    grid->setColumnStretch(0, 1);
    column->addWidget(table);

    if (statistics.cpap && !statistics.extras.isEmpty()) {
        auto *extras = new QWidget(card);
        auto *eg = new QGridLayout(extras);
        eg->setContentsMargins(0, 6, 0, 0);
        eg->setVerticalSpacing(1);
        int er = 0;
        for (const auto &extra : statistics.extras) {
            eg->addWidget(label(extra.first, Theme::Role::Ink, extras, false, true), er, 0);
            QLabel *value = label(extra.second, Theme::Role::Ink, extras, true);
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            eg->addWidget(value, er, 1);
            ++er;
        }
        eg->setColumnStretch(0, 1);
        column->addWidget(extras);
    }
    return collapsible(DC_Statistics, classic(QT_TRANSLATE_NOOP("Daily", "Statistics")), card);
}

QWidget *DailyDetailsPanel::eventBreakdownCard(const Page &page)
{
    auto *content = new QWidget();
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(0, 0, 0, 4);
    auto *image = new QLabel(content);
    image->setAlignment(Qt::AlignCenter);
    // The pie, or the "0.0" image when the device scores these events but recorded none. Both
    // have a clear background, so the card's colour shows behind them in light and dark mode.
    image->setPixmap(page.pie.isNull()
                         ? QPixmap::fromImage(zeroEventsImage(Theme::color(Theme::Role::Ink)))
                         : page.pie);
    column->addWidget(image);
    return collapsible(DC_EventBreakdown, classic(QT_TRANSLATE_NOOP("Daily", "Event Breakdown")), content);
}

QWidget *DailyDetailsPanel::oximeterCard(const DailyDetails::Oximeter &oximeter)
{
    auto *content = new QWidget();
    auto *grid = new QGridLayout(content);
    grid->setContentsMargins(26, 0, 0, 2);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(2);
    int r = 0;
    grid->addWidget(label(oximeter.heading, Theme::Role::Ink2, content, false, true), r++, 0, 1, 2);
    for (const TextRow &row : oximeter.rows) {
        switch (row.style) {
        case TextRow::Pair: {
            grid->addWidget(label(row.label, Theme::Role::Ink, content, false, true), r, 0);
            QLabel *value = label(row.value, Theme::Role::Ink, content, true);
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(value, r++, 1);
            break;
        }
        case TextRow::Line:
            grid->addWidget(label(row.label + ": " + row.value, Theme::Role::Ink, content, false, true), r++, 0, 1, 2);
            break;
        case TextRow::Subheading: {
            QLabel *sub = label(row.label, Theme::Role::Ink2, content, false, true);
            QFont italic = sub->font();
            italic.setItalic(true);
            sub->setFont(italic);
            grid->addWidget(sub, r++, 0, 1, 2);
            break;
        }
        }
    }
    if (!oximeter.note.isEmpty()) {
        QLabel *note = label(oximeter.note, Theme::Role::Ink2, content, false, true);
        QFont italic = note->font();
        italic.setItalic(true);
        note->setFont(italic);
        grid->addWidget(note, r++, 0, 1, 2);
    }
    grid->setColumnStretch(0, 1);
    return collapsible(DC_Oximeter, classic(QT_TRANSLATE_NOOP("Daily", "Oximeter Information")), content);
}

QWidget *DailyDetailsPanel::settingsCard(const DailyDetails::Settings &settings)
{
    auto *content = new QWidget();
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(26, 0, 0, 2);
    column->setSpacing(4);
    if (settings.loaderMissing) {
        column->addWidget(new Callout(Callout::Kind::Warning, classic(QT_TRANSLATE_NOOP("Daily", "DEVICE SETTINGS ERROR")), QString(), content));
    } else {
        if (settings.assumed) {
            // The Classic text has its "Please Note:" in bold HTML; the callout shows it plain
            const QString note = QTextDocumentFragment::fromHtml(
                classic(QT_TRANSLATE_NOOP("Daily", "<b>Please Note:</b> All settings shown below are based on assumptions that nothing has changed since previous days."))).toPlainText();
            column->addWidget(new Callout(Callout::Kind::Warning, QString(), note, content));
        }
        auto *table = new QWidget(content);
        auto *grid = new QGridLayout(table);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(2);
        int r = 0;
        for (const TextRow &row : settings.rows) {
            QLabel *name = label(row.label, Theme::Role::Ink2, table, false, true);
            name->setToolTip(row.tooltip);
            grid->addWidget(name, r, 0);
            grid->addWidget(label(row.value, Theme::Role::Ink, table, false, true), r++, 1);
        }
        grid->setColumnStretch(0, 3);
        grid->setColumnStretch(1, 2);
        column->addWidget(table);
    }
    return collapsible(DC_DeviceSettings, classic(QT_TRANSLATE_NOOP("Daily", "Device Settings")), content);
}

QWidget *DailyDetailsPanel::sessionsCard(const QVector<SessionGroup> &groups)
{
    auto *content = new QWidget();
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(4, 0, 0, 2);
    column->setSpacing(6);
    for (const SessionGroup &group : groups) {
        QLabel *heading = label(group.heading, Theme::Role::Ink2, content, true);
        column->addWidget(heading);
        auto *table = new QWidget(content);
        auto *grid = new QGridLayout(table);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(0);
        // The date sits on the line with the session's name, so the table fits a narrow panel
        const QStringList headings = { STR_TR_On, STR_TR_Start, STR_TR_End, classic(QT_TRANSLATE_NOOP("Daily", "Duration")) };
        for (int c = 0; c < headings.size(); ++c) grid->addWidget(label(headings[c], Theme::Role::Ink2, table), 0, c);
        int r = 1;
        for (const SessionRow &row : group.rows) {
            if (!row.on) {
                // A switched-off session: its two lines tinted, beneath the text (added first)
                auto *tint = new QWidget(table);
                tint->setAttribute(Qt::WA_StyledBackground);
                tint->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(Theme::css(Theme::Role::BadTint)));
                grid->addWidget(tint, r, 0, 2, headings.size());
            }
            grid->addWidget(label(row.name + QString::fromUtf8(" \u00B7 ") + row.date, Theme::Role::Ink, table, false, true),
                            r, 0, 1, headings.size());
            auto *toggle = new UiKit::ToggleSwitch(table);
            toggle->setChecked(row.on);
            const QString tip = classic(QT_TRANSLATE_NOOP("Daily", "Click to %1 this session.")).arg(row.on ? classic(QT_TRANSLATE_NOOP("Daily", "disable")) : classic(QT_TRANSLATE_NOOP("Daily", "enable")));
            toggle->setToolTip(tip);
            toggle->setAccessibleName(row.name);
            Session *session = row.session;
            connect(toggle, &QAbstractButton::clicked, this, [this, toggle, session] {
                emit sessionToggled(session);
                // Refused (Clinical Mode): the switch goes back to the session's state
                if (session) toggle->setChecked(session->enabled());
            });
            grid->addWidget(toggle, r + 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
            const QStringList cells = { row.start, row.end, row.duration };
            for (int c = 0; c < cells.size(); ++c) grid->addWidget(label(cells[c], Theme::Role::Ink, table), r + 1, c + 1);
            r += 2;
        }
        grid->setColumnStretch(headings.size() - 1, 1);
        column->addWidget(table);
    }
    return collapsible(DC_SessionInfo, classic(QT_TRANSLATE_NOOP("Daily", "Session Information")), content);
}
