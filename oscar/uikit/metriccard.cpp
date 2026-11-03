/* Metric card component
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "metriccard.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include "label.h"
#include "lineicons.h"
#include "theme.h"

namespace UiKit {

namespace {
constexpr int kPadding = 12;        //!< Space inside the card (compact: smaller than Theme::CardPadding)
constexpr int kInnerGap = 4;        //!< Between the card's rows
constexpr int kValuePx = 24;        //!< Pixel size of the large value
constexpr int kArrowSize = 16;      //!< Side of the trend arrow, in pixels
constexpr double kArrowStroke = 3;  //!< Arrow stroke on the 24x24 icon grid
constexpr int kShadowBlur = 18;
constexpr int kShadowOffset = 6;

QColor deltaColour(Trend trend)
{
    switch (trend) {
    case Trend::Good: return Theme::color(Theme::Role::Good);
    case Trend::Bad: return Theme::color(Theme::Role::Bad);
    default: return Theme::color(Theme::Role::Neutral);
    }
}
}

/*! \class TrendArrow
    \brief Angled arrow showing whether the night was above or below the average */
class TrendArrow : public QWidget
{
public:
    explicit TrendArrow(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(kArrowSize, kArrowSize);
    }

    void set(Direction direction, const QColor &colour)
    {
        m_direction = direction;
        m_colour = colour;
        setVisible(direction != Direction::None);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        LineIcon icon = m_direction == Direction::Up ? LineIcon::ArrowUpRight : LineIcon::ArrowDownRight;
        paintLineIcon(p, icon, QRectF(rect()), m_colour, kArrowStroke);
    }

private:
    Direction m_direction = Direction::None;
    QColor m_colour;
};

/*! \class CompareBox
    \brief One "7-day avg / 4.12 ↗" box inside a MetricCard */
class CompareBox : public QWidget
{
public:
    explicit CompareBox(QWidget *parent)
        : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 4, 8, 4);
        layout->setSpacing(0);
        m_label = makeLabel(QString(), Theme::font(Theme::FontRole::Body, 11.5), Theme::Role::Ink3, this);
        layout->addWidget(m_label);
        auto *row = new QHBoxLayout();
        row->setSpacing(6);
        m_avg = makeLabel(QString(), Theme::font(Theme::FontRole::Body, 13.5), Theme::Role::Ink2, this);
        m_delta = makeLabel(QString(), Theme::font(Theme::FontRole::Body, 12.5), Theme::Role::Neutral, this);
        m_arrow = new TrendArrow(this);
        row->addWidget(m_avg);
        row->addWidget(m_delta);
        row->addWidget(m_arrow);
        row->addStretch(1);
        layout->addLayout(row);
        // Only as wide as its content, so the arrow sits right beside the number
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    }

    void set(const QString &label, const QString &avg, const Comparison &c)
    {
        m_label->setText(label);
        m_avg->setText(avg);
        // A real change shows only the arrow; "same" and "no comparison" show text instead
        const QString text = deltaText(c);
        m_delta->setText(text);
        m_delta->setVisible(!text.isEmpty());
        setLabelColour(m_delta, deltaColour(c.trend));
        m_arrow->set(direction(c), deltaColour(c.trend));
    }

    //! \brief On a tinted card the box is a translucent surface instead of the inset colour
    void setOnTint(bool onTint)
    {
        m_onTint = onTint;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor fill = Theme::color(Theme::Role::Surface2);
        if (m_onTint) {
            fill = Theme::color(Theme::Role::Surface);
            fill.setAlphaF(0.7);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(rect()), Theme::SmallRadius, Theme::SmallRadius);
    }

private:
    QLabel *m_label;
    QLabel *m_avg;
    QLabel *m_delta;
    TrendArrow *m_arrow;
    bool m_onTint = false;
};


MetricCard::MetricCard(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPadding, kPadding, kPadding, kPadding);
    layout->setSpacing(kInnerGap);

    m_title = makeLabel(QString(), Theme::font(Theme::FontRole::Body, 14), Theme::Role::Ink2, this);
    layout->addWidget(m_title);

    m_value = new QLabel(this);
    m_value->setTextFormat(Qt::RichText);
    setLabelColour(m_value, Theme::Role::Ink);
    layout->addWidget(m_value);

    m_sub = makeLabel(QString(), Theme::font(Theme::FontRole::Body, 13), Theme::Role::Ink2, this, true);
    layout->addWidget(m_sub);

    m_extra = new QVBoxLayout();
    m_extra->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(m_extra);

    // Comparisons sit at the bottom so they line up across cards of different heights
    layout->addStretch(1);
    layout->addSpacing(kInnerGap);

    m_compareRow = new QWidget(this);
    auto *cmp = new QHBoxLayout(m_compareRow);
    cmp->setContentsMargins(0, 0, 0, 0);
    cmp->setSpacing(6);
    m_box7 = new CompareBox(m_compareRow);
    m_box30 = new CompareBox(m_compareRow);
    cmp->addWidget(m_box7);
    cmp->addWidget(m_box30);
    cmp->addStretch(1);
    layout->addWidget(m_compareRow);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(kShadowBlur);
    shadow->setOffset(0, kShadowOffset);
    shadow->setColor(Theme::color(Theme::Role::Shadow));
    setGraphicsEffect(shadow);
}

void MetricCard::setTitle(const QString &title)
{
    m_title->setText(title);
    setAccessibleName(title);
}

void MetricCard::setValue(const QString &value, const QString &unit)
{
    QFont valueFont = Theme::font(Theme::FontRole::Display, kValuePx, QFont::Medium);
    QFont unitFont = Theme::font(Theme::FontRole::Body, 13);
    QString html = QString("<span style=\"font-family:'%1'; font-size:%2px; font-weight:500;\">%3</span>")
                       .arg(valueFont.family()).arg(kValuePx).arg(value.toHtmlEscaped());
    if (!unit.isEmpty()) {
        html += QString("<span style=\"font-family:'%1'; font-size:13px; color:%2;\">&nbsp;%3</span>")
                    .arg(unitFont.family(), Theme::css(Theme::Role::Ink3), unit.toHtmlEscaped());
    }
    m_value->setText(html);
}

void MetricCard::setSubtitle(const QString &subtitle)
{
    m_sub->setText(subtitle);
    m_sub->setVisible(!subtitle.isEmpty());
}

void MetricCard::setComparisons(const QString &label7, const QString &avg7, const Comparison &c7,
                                const QString &label30, const QString &avg30, const Comparison &c30)
{
    m_box7->set(label7, avg7, c7);
    m_box30->set(label30, avg30, c30);
}

void MetricCard::setShowComparisons(bool show)
{
    m_compareRow->setVisible(show);
}

void MetricCard::setExtraWidget(QWidget *widget)
{
    widget->setParent(this);
    m_extra->addWidget(widget);
}

void MetricCard::setTint(Tint tint)
{
    m_tint = tint;
    bool tinted = tint != Tint::None;
    m_box7->setOnTint(tinted);
    m_box30->setOnTint(tinted);
    update();
}

void MetricCard::paintEvent(QPaintEvent *)
{
    using Theme::Role;
    Role fill = Role::Surface, border = Role::Line;
    switch (m_tint) {
    case Tint::Good: fill = Role::GoodTint; border = Role::GoodLine; break;
    case Tint::Bad: fill = Role::BadTint; border = Role::BadLine; break;
    case Tint::Breathing: fill = Role::GroupBreath; border = Role::GroupBreathLine; break;
    case Tint::Mask: fill = Role::GroupMask; border = Role::GroupMaskLine; break;
    case Tint::Usage: fill = Role::GroupUsage; border = Role::GroupUsageLine; break;
    case Tint::None: break;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::color(border), 1));
    p.setBrush(Theme::color(fill));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Theme::Radius, Theme::Radius);
}

} // namespace UiKit
