/* Overview details popup
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "overviewdetailspopup.h"

#include <QCloseEvent>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

#include "Graphs/graphtheme.h"
#include "Graphs/overviewbars.h"
#include "uikit/theme.h"

namespace {

constexpr int kWidth = 300;         //!< Popup width
constexpr int kSwatchWidth = 56;    //!< Width of a part's swatch
constexpr int kSwatchMin = 8;       //!< Smallest swatch height
constexpr int kSwatchMax = 60;      //!< Swatch height of the largest part

double luminance(const QColor &c)
{
    auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

//! \brief \a c darkened (on a light background) or lightened (on a dark one) until text in it reads well
QColor readableOn(QColor c, const QColor &background)
{
    const bool darkBg = luminance(background) < 0.2;
    const QColor towards = darkBg ? QColor(Qt::white) : QColor(Qt::black);
    for (int i = 0; i < 20; ++i) {
        const double a = luminance(c), b = luminance(background);
        if ((qMax(a, b) + 0.05) / (qMin(a, b) + 0.05) >= 4.5) break;
        c = QColor(c.red() + (towards.red() - c.red()) / 6, c.green() + (towards.green() - c.green()) / 6,
                   c.blue() + (towards.blue() - c.blue()) / 6);
    }
    return c;
}

/*! \class Swatch
    \brief One part of the bar, drawn as a short cylinder */
class Swatch : public QWidget
{
public:
    Swatch(const QColor &color, int height, QWidget *parent)
        : QWidget(parent), m_color(color)
    {
        setFixedSize(kSwatchWidth + 8, height);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRectF r((width() - kSwatchWidth) / 2.0, 0, kSwatchWidth, height());
        OverviewBars::paintSegment(p, r, GraphTheme::ink(m_color), true, true);
    }

private:
    QColor m_color;
};

} // namespace

OverviewDetailsPopup::OverviewDetailsPopup(QWidget *parent, const QString &title, const QString &subtitle,
                                           const QColor &headerColor, const QList<DetailRow> &rows)
    : QFrame(parent), m_headerColor(headerColor)
{
    setObjectName("OverviewDetailsPopup");
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(kWidth);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header: drag handle, title and close button
    m_header = new QWidget(this);
    m_header->setObjectName("popupHeader");
    m_header->setAttribute(Qt::WA_StyledBackground, true);
    m_header->setCursor(Qt::SizeAllCursor);
    auto *head = new QHBoxLayout(m_header);
    head->setContentsMargins(12, 6, 6, 6);
    m_title = new QLabel(title, m_header);
    m_title->setObjectName("popupTitle");
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    head->addWidget(m_title, 1);
    auto *close = new QToolButton(m_header);
    close->setObjectName("popupClose");
    close->setText(QStringLiteral("×"));
    close->setToolTip(tr("Close"));
    close->setAccessibleName(tr("Close"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QToolButton::clicked, this, &QWidget::close);
    head->addWidget(close);
    outer->addWidget(m_header);

    auto *body = new QVBoxLayout();
    body->setContentsMargins(12, 8, 12, 12);
    body->setSpacing(8);
    if (!subtitle.isEmpty()) {
        auto *sub = new QLabel(subtitle, this);
        sub->setObjectName("popupSubtitle");
        sub->setWordWrap(true);
        body->addWidget(sub);
    }

    // One row per part: swatch sized to the part, then its value, name and note
    double largest = 0;
    for (const auto &row : rows) largest = qMax(largest, row.size);
    for (const auto &row : rows) {
        auto *line = new QHBoxLayout();
        line->setSpacing(10);
        const int h = largest > 0 ? qBound(kSwatchMin, int(row.size / largest * kSwatchMax + 0.5), kSwatchMax) : kSwatchMin;
        line->addWidget(new Swatch(row.color, h, this), 0, Qt::AlignVCenter);
        auto *text = new QVBoxLayout();
        text->setSpacing(0);
        auto *value = new QLabel(QString("<b>%1</b>&nbsp; %2").arg(row.value.toHtmlEscaped(), row.name.toHtmlEscaped()), this);
        value->setObjectName("popupValue");
        text->addWidget(value);
        if (!row.note.isEmpty()) {
            auto *note = new QLabel(row.note, this);
            note->setObjectName("popupNote");
            note->setWordWrap(true);
            note->setProperty("partColor", row.color);
            text->addWidget(note);
        }
        line->addLayout(text, 1);
        body->addLayout(line);
    }
    if (rows.isEmpty()) {
        auto *none = new QLabel(tr("No details for this day."), this);
        none->setObjectName("popupNote");
        body->addWidget(none);
    }
    outer->addLayout(body);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, 110));
    setGraphicsEffect(shadow);

    applyStyle();
    adjustSize();
}

void OverviewDetailsPopup::applyStyle()
{
    const QPalette pal = QGuiApplication::palette();
    const QString key = pal.color(QPalette::Base).name() + pal.color(QPalette::Text).name();
    if (key == m_styleKey) return;
    m_styleKey = key;

    const QColor header = GraphTheme::ink(m_headerColor.isValid() ? m_headerColor : QColor(0x6D, 0x4B, 0xC9));
    const QColor headerText = OverviewBars::contrastingText(header);
    using Theme::Role;
    setStyleSheet(QString(
        "#OverviewDetailsPopup { background: %1; border: 1px solid %2; border-radius: 10px; }"
        "#popupHeader { background: %3; border-top-left-radius: 9px; border-top-right-radius: 9px; }"
        "#popupTitle { color: %4; }"
        "#popupClose { color: %4; background: transparent; border: none; font-size: 16px; padding: 0 6px; }"
        "#popupClose:hover { background: rgba(255,255,255,0.18); border-radius: 6px; }"
        "#popupSubtitle { color: %5; }"
        "#popupValue { color: %6; }")
        .arg(Theme::css(Role::Surface), Theme::css(Role::Line), header.name(), headerText.name(),
             Theme::css(Role::Ink2), Theme::css(Role::Ink)));

    // Each note in its part's colour, adjusted until it reads well on the popup's background
    const QColor surface = Theme::color(Role::Surface);
    for (QLabel *note : findChildren<QLabel *>("popupNote")) {
        const QColor c = note->property("partColor").value<QColor>();
        note->setStyleSheet(QString("color: %1; font-weight: 600;")
                                .arg(c.isValid() ? readableOn(GraphTheme::ink(c), surface).name() : Theme::css(Role::Ink2)));
    }
}

void OverviewDetailsPopup::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::PaletteChange) {
        applyStyle();
    }
}

void OverviewDetailsPopup::moveWithin(QPoint pos)
{
    if (QWidget *p = parentWidget()) {
        pos.setX(qBound(0, pos.x(), qMax(0, p->width() - width())));
        pos.setY(qBound(0, pos.y(), qMax(0, p->height() - height())));
    }
    move(pos);
}

void OverviewDetailsPopup::mousePressEvent(QMouseEvent *event)
{
    raise();
    setFocus();
    if (event->button() == Qt::LeftButton && m_header->geometry().contains(event->position().toPoint())) {
        m_dragging = true;
        m_dragOffset = event->position().toPoint();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void OverviewDetailsPopup::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        moveWithin(mapToParent(event->position().toPoint()) - m_dragOffset);
        event->accept();
        return;
    }
    QFrame::mouseMoveEvent(event);
}

void OverviewDetailsPopup::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QFrame::mouseReleaseEvent(event);
}

void OverviewDetailsPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QFrame::keyPressEvent(event);
}

void OverviewDetailsPopup::closeEvent(QCloseEvent *event)
{
    emit closed();
    QFrame::closeEvent(event);
}
