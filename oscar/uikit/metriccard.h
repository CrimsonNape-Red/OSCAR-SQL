/* Metric card component
 *
 * A compact card showing one number: title, value with unit, a line of detail, and two
 * comparisons with averages marked by trend arrows. Used for the Welcome page's nightly
 * metrics.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_METRICCARD_H
#define UIKIT_METRICCARD_H

#include <QFrame>

#include "comparison.h"

class QLabel;
class QVBoxLayout;

namespace UiKit {

class CompareBox;

/*! \class MetricCard
    \brief Card with a value and two average comparisons; display only */
class MetricCard : public QFrame
{
    Q_OBJECT

public:
    //! \brief Card background and border
    enum class Tint { None, Good, Bad, Breathing, Mask, Usage };

    explicit MetricCard(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    //! \brief Large value with a smaller unit after it; \a unit may be empty
    void setValue(const QString &value, const QString &unit);
    void setSubtitle(const QString &subtitle);

    /*! \brief Sets the two comparison boxes
        \param label7, label30 Box captions, e.g. "7-day avg"
        \param avg7, avg30 The averages as displayed
        \param c7, c30 The comparisons, which set the direction and colour of the arrows */
    void setComparisons(const QString &label7, const QString &avg7, const Comparison &c7,
                        const QString &label30, const QString &avg30, const Comparison &c30);
    //! \brief Shows or hides the comparison boxes
    void setShowComparisons(bool show);

    //! \brief Places \a widget under the subtitle (e.g. an EventBar); the card takes ownership
    void setExtraWidget(QWidget *widget);

    void setTint(Tint tint);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *m_title;
    QLabel *m_value;
    QLabel *m_sub;
    QVBoxLayout *m_extra;
    QWidget *m_compareRow;
    CompareBox *m_box7;
    CompareBox *m_box30;
    Tint m_tint = Tint::None;
};

} // namespace UiKit

#endif // UIKIT_METRICCARD_H
