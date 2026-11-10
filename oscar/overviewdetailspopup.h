/* Overview details popup
 *
 * A small window inside the Overview page describing one night's bar on one graph: each part of
 * the bar as a shaded swatch beside its value. Several can be open at once for comparing; each
 * stays until closed and can be dragged by its header.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OVERVIEWDETAILSPOPUP_H
#define OVERVIEWDETAILSPOPUP_H

#include <QColor>
#include <QFrame>
#include <QList>

#include "Graphs/gSummaryChart.h"

class QLabel;

/*! \class OverviewDetailsPopup
    \brief One night's bar from one Overview graph, part by part */
class OverviewDetailsPopup : public QFrame
{
    Q_OBJECT
public:
    /*! \param parent The Overview page; the popup stays within it
        \param title Header text: the date and the graph's headline value
        \param subtitle A line under the header; may be empty
        \param headerColor The graph's title strip colour (light-mode; translated for dark mode)
        \param rows The bar's parts, top part first */
    OverviewDetailsPopup(QWidget *parent, const QString &title, const QString &subtitle,
                         const QColor &headerColor, const QList<DetailRow> &rows);

signals:
    //! \brief The popup was closed (close button or Esc); it deletes itself afterwards
    void closed();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    //! \brief Sets the colours from the header colour and the current palette
    void applyStyle();
    //! \brief Moves the popup to \a pos, kept inside its parent
    void moveWithin(QPoint pos);

    QColor m_headerColor;
    QWidget *m_header = nullptr;
    QLabel *m_title = nullptr;
    QString m_styleKey;         //!< Palette colours the style was last built from
    bool m_dragging = false;
    QPoint m_dragOffset;
};

#endif // OVERVIEWDETAILSPOPUP_H
