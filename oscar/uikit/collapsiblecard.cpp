/* Collapsible card
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "collapsiblecard.h"

#include <QAbstractButton>
#include <QKeyEvent>
#include <QPainter>
#include <QVBoxLayout>

#include "lineicons.h"

namespace UiKit {

/*! \class CollapsibleCard::Header
    \brief The card's header: chevron and bold title; shows hover and keyboard focus */
class CollapsibleCard::Header : public QAbstractButton
{
public:
    Header(const QString &title, QWidget *parent) : QAbstractButton(parent)
    {
        setText(title);
        setAccessibleName(title);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_Hover);
    }

    bool open = true;

    QSize sizeHint() const override
    {
        QFont bold = font();
        bold.setBold(true);
        return QSize(QFontMetrics(bold).horizontalAdvance(text()) + 40, QFontMetrics(bold).height() + 14);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(2, 2, -2, -2);
        if (underMouse() || isDown()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Theme::color(Theme::Role::Surface2));
            painter.drawRoundedRect(box, 6, 6);
        }
        if (hasFocus()) {
            painter.setPen(QPen(Theme::color(Theme::Role::Accent), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box, 6, 6);
        }
        const int iconSide = 14;
        const QRectF icon(8, (height() - iconSide) / 2.0, iconSide, iconSide);
        paintLineIcon(painter, open ? LineIcon::ChevronDown : LineIcon::ChevronRight, icon,
                      Theme::color(Theme::Role::Ink2), 2.4);
        QFont bold = font();
        bold.setBold(true);
        painter.setFont(bold);
        painter.setPen(Theme::color(Theme::Role::Ink));
        const QRect textRect(int(icon.right()) + 8, 0, width() - int(icon.right()) - 16, height());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(bold).elidedText(text(), Qt::ElideRight, textRect.width()));
    }

    //! Enter toggles, as Space does
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            click();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }
};

CollapsibleCard::CollapsibleCard(const QString &title, QWidget *parent)
    : Panel(Theme::Role::Surface, Theme::Role::Line, Theme::Radius, parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(6, 4, 10, 6);
    m_layout->setSpacing(2);
    m_header = new Header(title, this);
    m_layout->addWidget(m_header);
    connect(m_header, &QAbstractButton::clicked, this, [this] {
        setOpen(!m_open);
        emit toggled(m_open);
    });
}

void CollapsibleCard::setContent(QWidget *content)
{
    if (m_content) {
        m_layout->removeWidget(m_content);
        delete m_content;
    }
    m_content = content;
    if (m_content) {
        m_content->setParent(this);
        m_layout->addWidget(m_content);
        m_content->setVisible(m_open);
    }
}

void CollapsibleCard::setOpen(bool open)
{
    m_open = open;
    m_header->open = open;
    m_header->update();
    if (m_content) m_content->setVisible(open);
    // Closed, the card shrinks to its header
    m_layout->setContentsMargins(6, 4, 10, open ? 6 : 4);
}

QWidget *CollapsibleCard::header() const
{
    return m_header;
}

void CollapsibleCard::setHeaderToolTip(const QString &tip)
{
    m_header->setToolTip(tip);
}

} // namespace UiKit
