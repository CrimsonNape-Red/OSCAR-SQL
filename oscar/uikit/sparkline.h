/* Sparkline component
 *
 * A small line chart of recent nightly values with a dashed reference line (the average)
 * and a dot on the latest value.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_SPARKLINE_H
#define UIKIT_SPARKLINE_H

#include <QColor>
#include <QPointF>
#include <QSizeF>
#include <QVector>
#include <QWidget>

namespace UiKit {

/*! \class Sparkline
    \brief Draws a series to scale: area and line in the accent colour, a dashed reference
           line, and a dot on the last value. NaN values are gaps in the line. */
class Sparkline : public QWidget
{
    Q_OBJECT

public:
    explicit Sparkline(QWidget *parent = nullptr);

    /*! \brief Sets what to draw
        \param values Oldest first; NaN where there is no data
        \param reference Value of the dashed line, included in the vertical scale
        \param dotColour Colour of the dot on the last value */
    void setData(const QVector<double> &values, double reference, const QColor &dotColour);

    /*! \brief Maps \a values to points in a \a size box with \a pad pixels above and below

        Points are evenly spaced across the full width. The vertical scale spans the smallest
        and largest non-NaN value and \a reference; a flat range is widened by 1 either side.
        NaN values map to points with NaN coordinates. */
    static QVector<QPointF> mapPoints(const QVector<double> &values, double reference,
                                      QSizeF size, double pad);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> m_values;
    double m_reference = 0;
    QColor m_dot;
};

} // namespace UiKit

#endif // UIKIT_SPARKLINE_H
