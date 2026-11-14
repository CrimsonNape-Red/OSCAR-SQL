/* Daily top bar
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "dailytoolbar.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QToolButton>

#include "Graphs/graphtheme.h"
#include "uikit/theme.h"

/*! \class DailyToolbar::DeviceLine
    \brief Paints "name · settings", the name bold, cut short at the right to fit

    Painted rather than a QLabel so that its text never asks the bar for more width. */
class DailyToolbar::DeviceLine : public QWidget
{
public:
    explicit DeviceLine(QWidget *parent) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

    void set(const QString &name, const QString &settings)
    {
        m_name = name;
        m_settings = name.isEmpty() ? QString() : settings;
        setToolTip(fullText());
        update();
    }

    QString fullText() const
    {
        if (m_name.isEmpty()) return QString();
        return m_settings.isEmpty() ? m_name : m_name + QStringLiteral(" · ") + m_settings;
    }

    QSize sizeHint() const override { return QSize(0, fontMetrics().height() + 4); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_name.isEmpty()) return;
        QPainter painter(this);
        QFont bold = font();
        bold.setBold(true);
        const QFontMetrics boldMetrics(bold);
        const QFontMetrics plainMetrics(font());
        const int available = width();
        const int y = (height() - plainMetrics.height()) / 2 + plainMetrics.ascent();

        painter.setPen(Theme::color(Theme::Role::Ink));
        painter.setFont(bold);
        const QString name = boldMetrics.elidedText(m_name, Qt::ElideRight, available);
        painter.drawText(0, y, name);
        int x = boldMetrics.horizontalAdvance(name);
        if (name != m_name || m_settings.isEmpty()) return;

        painter.setFont(font());
        painter.setPen(Theme::color(Theme::Role::Ink2));
        const QString rest = plainMetrics.elidedText(QStringLiteral(" · ") + m_settings, Qt::ElideRight, available - x);
        if (rest.size() > 3) painter.drawText(x, y, rest);
    }

private:
    QString m_name;
    QString m_settings;
};

//! \brief Two stacked boxes, for the button that saves and restores the graph layout (as on Overview)
static QIcon layoutIcon(const QColor &colour, qreal scale)
{
    QPixmap pixmap(QSize(16, 16) * scale);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(colour, 1.5));
    painter.drawRoundedRect(QRectF(2, 2.25, 12, 4.5), 1, 1);
    painter.drawRoundedRect(QRectF(2, 9.25, 12, 4.5), 1, 1);
    return QIcon(pixmap);
}

DailyToolbar::DailyToolbar(QWidget *parent)
    : QWidget(parent)
{
    m_row = new QHBoxLayout(this);
    m_row->setContentsMargins(4, 4, 4, 4);
    m_row->setSpacing(6);

    m_device = new DeviceLine(this);
    m_row->addWidget(m_device, 1);

    m_layout = new QToolButton(this);
    m_layout->setToolTip(tr("Layout"));
    m_layout->setAccessibleName(tr("Layout"));
    m_layout->setIconSize(QSize(16, 16));
    m_help = new QToolButton(this);
    m_help->setText(QStringLiteral("?"));
    m_help->setToolTip(tr("Hint"));
    m_help->setAccessibleName(tr("Hint"));
    for (QToolButton *button : { m_layout, m_help }) {
        button->setFixedSize(30, 30);
        button->setFocusPolicy(Qt::TabFocus);
        m_row->addWidget(button);
    }
    connect(m_layout, &QToolButton::clicked, this, &DailyToolbar::layoutClicked);
    connect(m_help, &QToolButton::clicked, this, &DailyToolbar::helpClicked);
    restyle();
}

void DailyToolbar::setDeviceLine(const QString &name, const QString &settings)
{
    m_device->set(name, settings);
}

QString DailyToolbar::deviceLine() const
{
    return m_device->fullText();
}

void DailyToolbar::adoptCombos(QComboBox *events, QComboBox *graphs)
{
    const int at = m_row->indexOf(m_layout);
    for (QComboBox *combo : { graphs, events }) {     // inserted in front of Layout: events first
        if (!combo) continue;
        combo->setParent(this);
        m_row->insertWidget(at, combo);
        combo->show();
    }
    if (events && graphs) QWidget::setTabOrder(events, graphs);
    if (graphs) QWidget::setTabOrder(graphs, m_layout);
    QWidget::setTabOrder(m_layout, m_help);
}

void DailyToolbar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) restyle();
}

void DailyToolbar::restyle()
{
    // The same boxes as the Layout and Help buttons on Modern Overview
    const QString box = QString(
        "QToolButton { border: 1px solid %1; border-radius: 8px; background: %2; color: %3;"
        " font-weight: bold; padding: 0; }"
        "QToolButton:hover { border-color: %4; }"
        "QToolButton:pressed { background: %5; }")
        .arg(Theme::css(GraphTheme::outline()), Theme::css(Theme::Role::Surface), Theme::css(Theme::Role::Ink),
             Theme::css(Theme::Role::Accent), Theme::css(Theme::Role::AccentSoft));
    m_layout->setStyleSheet(box);
    m_help->setStyleSheet(box);
    m_layout->setIcon(layoutIcon(Theme::color(Theme::Role::Ink), devicePixelRatioF()));
    m_device->update();
}
