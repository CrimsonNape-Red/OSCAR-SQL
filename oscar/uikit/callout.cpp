/* Callout
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "callout.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include "lineicons.h"

namespace UiKit {

namespace {
//! \brief The callout's icon, in the warning or accent colour
class CalloutIcon : public QWidget
{
public:
    CalloutIcon(Callout::Kind kind, QWidget *parent) : QWidget(parent), m_kind(kind)
    {
        setFixedSize(20, 20);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const bool warning = m_kind == Callout::Kind::Warning;
        paintLineIcon(painter, warning ? LineIcon::Warning : LineIcon::Info, QRectF(rect()),
                      Theme::color(warning ? Theme::Role::Warn : Theme::Role::AccentInk), 2.0);
    }

private:
    Callout::Kind m_kind;
};
}

Callout::Callout(Kind kind, const QString &title, const QString &text, QWidget *parent)
    : Panel(kind == Kind::Warning ? Theme::Role::WarnSoft : Theme::Role::AccentSoft,
            kind == Kind::Warning ? Theme::Role::WarnLine : Theme::Role::Line, Theme::Radius, parent),
      m_kind(kind)
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(12, 10, 12, 10);
    row->setSpacing(10);
    auto *icon = new CalloutIcon(kind, this);
    row->addWidget(icon, 0, Qt::AlignTop);

    auto *column = new QVBoxLayout();
    column->setSpacing(2);
    m_title = new QLabel(title, this);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    m_title->setWordWrap(true);
    m_title->setVisible(!title.isEmpty());
    m_text = new QLabel(text, this);
    m_text->setWordWrap(true);
    m_text->setVisible(!text.isEmpty());
    column->addWidget(m_title);
    column->addWidget(m_text);
    row->addLayout(column, 1);
    restyle();
}

QString Callout::title() const
{
    return m_title->text();
}

QString Callout::text() const
{
    return m_text->text();
}

void Callout::changeEvent(QEvent *event)
{
    Panel::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) restyle();
}

void Callout::restyle()
{
    const QString sheet = QString("color: %1; background: transparent;").arg(Theme::css(Theme::Role::Ink));
    m_title->setStyleSheet(sheet);
    m_text->setStyleSheet(sheet);
}

} // namespace UiKit
