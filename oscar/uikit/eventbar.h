/* Event bar component
 *
 * A proportional bar of event counts with a legend underneath, e.g. obstructive and central
 * apneas and hypopneas for one night.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_EVENTBAR_H
#define UIKIT_EVENTBAR_H

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

namespace UiKit {

/*! \class EventBar
    \brief Draws event counts as one proportional bar plus a "OA 12  CA 5 ..." legend */
class EventBar : public QWidget
{
    Q_OBJECT

public:
    //! \brief One segment of the bar
    struct Part {
        QColor colour;      //!< Segment and legend swatch colour
        int count = 0;      //!< Number of events; sets the segment's share of the bar
        QString label;      //!< Short channel label, e.g. "OA"
    };

    explicit EventBar(QWidget *parent = nullptr);

    //! \brief Replaces the segments; parts with a zero count are not drawn
    void setParts(const QVector<Part> &parts);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    //! \brief Lays out the legend entries for \a width; returns the legend's height
    int legendHeight(int width) const;

    QVector<Part> m_parts;
};

} // namespace UiKit

#endif // UIKIT_EVENTBAR_H
