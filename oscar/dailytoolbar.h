/* Daily top bar
 *
 * The bar above the Modern Daily page's graphs. At the left, one line naming the device, its
 * mode and its pressures, so a screenshot shows them whichever tab the left panel has open; at
 * the right, the event and graph drop-downs and the Layout and Help buttons that sit in the bar
 * under the graphs on the Classic page.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DAILYTOOLBAR_H
#define DAILYTOOLBAR_H

#include <QWidget>

class QComboBox;
class QHBoxLayout;
class QToolButton;

/*! \class DailyToolbar
    \brief The Modern Daily page's top bar: device line, Flags and Graphs drop-downs, Layout, Help */
class DailyToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit DailyToolbar(QWidget *parent = nullptr);

    /*! \brief Shows \a name in bold, then \a settings after a middle dot

        The line takes the bar's spare width and is cut short with an ellipsis at the right
        when it doesn't fit; its tooltip holds the whole text. An empty \a name shows nothing. */
    void setDeviceLine(const QString &name, const QString &settings);

    //! \brief The device line's text as shown when there is room for all of it (for checks)
    QString deviceLine() const;

    /*! \brief Moves the page's \a events and \a graphs drop-downs into the bar, at the right

        They keep their contents, delegates and connections; the page goes on setting their
        text. */
    void adoptCombos(QComboBox *events, QComboBox *graphs);

signals:
    void layoutClicked();   //!< The Layout button was clicked
    void helpClicked();     //!< The Help button was clicked

protected:
    void changeEvent(QEvent *event) override;

private:
    class DeviceLine;
    //! \brief Colours the buttons from the theme
    void restyle();

    DeviceLine *m_device;
    QHBoxLayout *m_row;
    QToolButton *m_layout;
    QToolButton *m_help;
};

#endif // DAILYTOOLBAR_H
