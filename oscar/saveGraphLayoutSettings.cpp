/* user graph settings Implementation
 *
 * Copyright (c) 2022-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#define TEST_MACROS_ENABLEDoff
#include <test_macros.h>

#include <QMessageBox>
#include <QAbstractButton>
#include <QWindow>
#include <QApplication>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QPixmap>
#include <QSize>
#include <QChar>
#include <QDateTime>
#include <QRegularExpression>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QPainter>
#include <QEvent>
#include <QPalette>
#include <QStyle>
#include "SleepLib/profiles.h"
#include "uikit/theme.h"
#include "saveGraphLayoutSettings.h"
#include "database/database_manager.h"
#include "database/graph_layouts_repository.h"

#define USE_FRAMELESS_WINDOW_off
#define USE_PROFILE_SPECIFIC_FOLDERoff      // off implies saved layouts worked for all profiles.
extern bool openOk;

SaveGraphLayoutSettings::SaveGraphLayoutSettings(QString title,QWidget* parent) : parent(parent),title(title)
{
    if (!DatabaseManager::instance().isOpen()) return;

    createMenu();

    menu.dialog->connect(menuAddFullBtn, SIGNAL(clicked()), this, SLOT (addFull_feature()  ));
    menu.dialog->connect(menuAddBtn,     SIGNAL(clicked()), this, SLOT (add_feature()  ));
    menu.dialog->connect(menuRestoreBtn, SIGNAL(clicked()), this, SLOT (restore_feature()  ));
    menu.dialog->connect(menuUpdateBtn,  SIGNAL(clicked()), this, SLOT (update_feature()  ));
    menu.dialog->connect(menuRenameBtn,  SIGNAL(clicked()), this, SLOT (rename_feature()  ));
    menu.dialog->connect(menuDeleteBtn,  SIGNAL(clicked()), this, SLOT (delete_feature()  ));
    menu.dialog->connect(menuHelpBtn,    SIGNAL(clicked()), this, SLOT (help_feature()  ));
    menu.dialog->connect(menuList,       SIGNAL(itemChanged(QListWidgetItem*)), this, SLOT(itemChanged(QListWidgetItem*)   ));
    menu.dialog->connect(menuList,       SIGNAL(itemSelectionChanged()), this, SLOT(itemSelectionChanged()   ));

    singleLineRe = new QRegularExpression( QString("^\\s*([^\\r\\n]{0,%1})").arg(1+maxDescriptionLen) );

    // The dialogs are not modal, so the user can leave the page or close the menu while they are open.
    // Closing the menu closes its help; leaving the page closes them all (see eventFilter()).
    connect(menu.dialog.data(), &QDialog::finished, this, [this]() { closeHelp(); closeHint(); });
    if (parent) parent->installEventFilter(this);
}

SaveGraphLayoutSettings::~SaveGraphLayoutSettings()
{
    if (menu.dialog) {
    menu.dialog->disconnect(menuAddFullBtn, SIGNAL(clicked()), this, SLOT (addFull_feature()  ));
    menu.dialog->disconnect(menuAddBtn,     SIGNAL(clicked()), this, SLOT (add_feature()  ));
    menu.dialog->disconnect(menuRestoreBtn, SIGNAL(clicked()), this, SLOT (restore_feature()  ));
    menu.dialog->disconnect(menuUpdateBtn,  SIGNAL(clicked()), this, SLOT (update_feature()  ));
    menu.dialog->disconnect(menuRenameBtn,  SIGNAL(clicked()), this, SLOT (rename_feature()  ));
    menu.dialog->disconnect(menuDeleteBtn,  SIGNAL(clicked()), this, SLOT (delete_feature()  ));
    menu.dialog->disconnect(menuHelpBtn,    SIGNAL(clicked()), this, SLOT (help_feature()  ));
    menu.dialog->disconnect(menuList,       SIGNAL(itemChanged(QListWidgetItem*)), this, SLOT(itemChanged(QListWidgetItem*)   ));
    menu.dialog->disconnect(menuList,       SIGNAL(itemSelectionChanged()), this, SLOT(itemSelectionChanged()   ));
    }
    helpDestructor();

    delete singleLineRe;

    // The dialogs are children of the main window, which outlives this object (a new one is made
    // each time a profile is opened), so they are deleted here.
    delete help.dialog.data();
    delete hint.dialog.data();
    delete menu.dialog.data();
}

/*! \brief Closes the dialogs when the page they belong to is hidden
 *
 *  The dialogs act on that page's graphs, so they must not stay open over another page.
 */
bool SaveGraphLayoutSettings::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parent && event->type() == QEvent::Hide) {
        closeMenu();
    }
    return QWidget::eventFilter(watched, event);
}

QPushButton*  SaveGraphLayoutSettings::menuBtn(QString name, QIcon* icon, QString style,QSizePolicy::Policy hPolicy,QString tooltip) {
    return newBtnRtn(menuLayoutButtons, name, icon, style, hPolicy,tooltip) ;
}

//QPushButton*  SaveGraphLayoutSettings::helpBtn(QString name, QIcon* icon, QString style,QSizePolicy::Policy hPolicy,QString tooltip) {
    //return newBtnRtn(menuLayoutButtons, name, icon, style, hPolicy,tooltip) ;
//}

QPushButton*  SaveGraphLayoutSettings::newBtnRtn(QHBoxLayout* layout,QString name, QIcon* icon, QString style,QSizePolicy::Policy hPolicy,QString tooltip) {
    QPushButton* button = new QPushButton(name, menu.dialog);
    if (icon) button->setIcon(*icon);       // nullptr: a button with text only
    button->setStyleSheet(style);
    button->setSizePolicy(hPolicy,QSizePolicy::Fixed);
    button->setToolTip(tooltip);
    button->setToolTipDuration(AppSetting->tooltipTimeout());
    // Button text uses the dialog's (application) font, not the graph title font.
    layout->addWidget(button);
    return button;
}

void SaveGraphLayoutSettings::createStyleSheets() {
    styleOn=        calculateButtonStyle(true,false);
    styleOff=       calculateButtonStyle(false,false);
    styleExit=      calculateButtonStyle(true,true);
    styleDialog= QString ( R"( QDialog { border: 3px solid black; } )") ;
}


void SaveGraphLayoutSettings::createMenu() {
    if(menu.dialog) return;
    initDialog(menu,tr("Manage Save Layout Settings") );

    createStyleSheets();

    QString styleDialog( R"( QDialog { border: 3px solid black; } )") ;
    menu.dialog->setStyleSheet(styleDialog);

    menuLayoutButtons = new QHBoxLayout();

    menuAddFullBtn = menuBtn(tr("Add"    ),m_icon_add     , styleOff ,QSizePolicy::Minimum, tr("Add Feature inhibited. The maximum number of Items has been exceeded.") );
    menuAddBtn     = menuBtn(tr("Add"    ),m_icon_add     , styleOn  ,QSizePolicy::Minimum, tr("creates new copy of current settings."));
    menuRestoreBtn = menuBtn(tr("Restore"),m_icon_restore , styleOn  ,QSizePolicy::Minimum, tr("Restores saved settings from selection."));
    menuRenameBtn  = menuBtn(tr("Rename" ),m_icon_rename  , styleOn  ,QSizePolicy::Minimum, tr("Renames the selection. Must edit existing name then press enter."));
    menuUpdateBtn  = menuBtn(tr("Update" ),m_icon_update  , styleOn  ,QSizePolicy::Minimum, tr("Updates the selection with current settings."));
    menuDeleteBtn  = menuBtn(tr("Delete" ),m_icon_delete  , styleOn  ,QSizePolicy::Minimum, tr("Deletes the selection."));
    // A plain "?" in the button's font, not an icon: it stays crisp and follows light and dark.
    menuHelpBtn    = menuBtn("?"          ,nullptr        , styleOn  ,QSizePolicy::Fixed  , tr("Expanded Help menu."));

    menuList = new QListWidget(menu.dialog);
    menuList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    menuList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    menuList->setFont(menu.font);


    menu.layout->addLayout(menuLayoutButtons);
    menu.layout->addWidget(menuList, 1);

    //menuList->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Preferred);
    menuList->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
};

void HelpData::setDialog(QWidget* parent,QString windowsTitle) {
    if (dialog) return;
    #ifdef USE_FRAMELESS_WINDOW
        dialog= new QDialog(parent, Qt::FramelessWindowHint);
        title.setText(QString("<h4>%1</h4>").arg(windowsTitle) );
        headerLayout = new QHBoxLayout();
    #else
        // A child of the main window, so it stays in front of it and can't be lost behind it.
        // Not modal: the rest of OSCAR stays usable while it is open.
        dialog = new QDialog(parent ? parent->window() : nullptr, Qt::WindowTitleHint | Qt::WindowCloseButtonHint|Qt::WindowSystemMenuHint);
        dialog->setWindowTitle(windowsTitle) ;
        dialog->setStyleSheet(QString ( "QDialog { border: 3px solid black; }"));
    #endif
    font = QApplication::font();
    fontBold = font;
    fontBold.setWeight(QFont::Bold);
    layout = new QVBoxLayout(dialog);
}

//! \brief Shows the dialog without blocking, in front of the main window and with the keyboard focus
void HelpData::showDialog() {
    if (!dialog) return;
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void SaveGraphLayoutSettings::initDialog(
    HelpData & aHelp
    , QString title
    ) {
    if(aHelp.dialog) return;
    aHelp.setDialog(parent,title);
    #ifdef USE_FRAMELESS_WINDOW
        aHelp.headerLayout->addWidget(&aHelp.title);
        aHelp.exitBtn = newBtnRtn(aHelp.headerLayout, "", m_icon_exit, styleExit , QSizePolicy::Fixed, tr("Exits the dialog menu.")) ;
        aHelp.layout->addLayout(aHelp.headerLayout);
    #endif
    // YXXXXXXXXXXXXXXXXXXX
    // dialog->setFont(font) ;
    aHelp.dialog->setSizeGripEnabled(true);       // allows lower right hand corner to resize dialog
    aHelp.dialog->setStyleSheet(QString ( "QDialog { border: 3px solid black; }"));
}

void SaveGraphLayoutSettings::createHelp() {
    createHelp(help, tr("Help Menu - Manage Layout Settings"), helpInfo());
    if (help.exitBtn) help.dialog->connect(help.exitBtn,    SIGNAL(clicked()), this, SLOT (help_exit_feature()  ));
};

void SaveGraphLayoutSettings::createHelp(HelpData & ahelp , QString title , QString text) {
    if(ahelp.dialog) return;
    initDialog(ahelp,title );

    ahelp.label = new QLabel(parent);
    ahelp.label->setFont(ahelp.font);
    ahelp.label->setText(text) ;
    ahelp.layout->addWidget(ahelp.label, 1);

}

void SaveGraphLayoutSettings::helpDestructor() {
    closeHelp();
    closeHint();
    closeMenu();
    if (hint.exitBtn) hint.dialog->disconnect(hint.exitBtn,    SIGNAL(clicked()), this, SLOT ( closeHint()  ));
    if (help.exitBtn) help.dialog->disconnect(help.exitBtn,    SIGNAL(clicked()), this, SLOT ( closeHelp()  ));
    if (menu.exitBtn) menu.dialog->disconnect(menu.exitBtn,    SIGNAL(clicked()), this, SLOT ( help_exit_feature()  ));
}

#define TABLES   QString(R"( <table width="100%"> )")
#define TABLEE   QString(R"( </table> )")
#define CENTERS  QString(R"( <div style="text-align: center;"><p> )")
#define CENTERE  QString(R"( </p></div> )")
#define BREAK    QString(R"( <br> )")
#define PARAS    QString(R"( <p> )")
#define PARAE    QString(R"( </p> )")
#define ROWS     QString(R"( <tr> )")
#define ROWE     QString(R"( </tr> )")
#define COLS     QString(R"( <td> )")
#define COLE     QString(R"( </td> )")
#define CSEP     QString(R"( </th><td>  )")

QString SaveGraphLayoutSettings::hintInfo() {

QStringList strList;

    strList
    <<  CENTERS <<  "<b>"
    <<  tr("Basic Hints")
    <<  "</b>" <<  CENTERE
    <<  TABLES
    <<  ROWS  << COLS << "<b>" << QString(tr("Key Sequence")) << "</b>" << CSEP << "<b>" <<  QString(tr("Description")) << "</b>" << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("MouseWheel")) << CSEP << QString(tr("Scrolls unpinned Graphs")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Ctrl + MouseWheel")) << CSEP << QString(tr("Zooms Time Selection")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("LeftMouse dragDrop")) << CSEP << QString(tr("Defines Time Selection")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("RightMouse dragDrop")) << CSEP << QString(tr("Moves Time Selection")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Ctrl + (right/left)MouseClick")) << CSEP << QString(tr("Zooms Time Selection")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr(" (right/left)MouseClick")) << CSEP << QString(tr("Moves Time Selection")) << COLE << ROWE
    <<  BREAK
    <<  ROWS  << COLS << QString(tr("(right/left) Arrow (Ctrl => faster)")) << CSEP << QString(tr("Moves Time Selection")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Up/Down Arrow")) << CSEP << QString(tr("Scrolls graphs")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Up/Down Arrow+Focus")) << CSEP << QString(tr("Zooms graphs")) << COLE << ROWE
    <<  TABLEE
    <<  CENTERS <<  "<b>"
    <<  tr("Graph Layout Hints")
    <<  "</b>" <<  CENTERE
    <<  TABLES
    <<  ROWS  << COLS << QString(tr("Double Click Graph Title")) << CSEP << QString(tr("Toggles Pinning")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Daily:Double Click Y-axis label")) << CSEP << QString(tr("Toggle Time Selection Auto Zoom")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("DragDrop Graph Title")) << CSEP << QString(tr("Reorders Graph layout")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("DragDrop graph’s bottom line")) << CSEP << QString(tr("Changes Size of Graphs")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Layout Button (next to Graph Button)")) << CSEP << QString(tr("Save / Restore Graph Layouts")) << COLE << ROWE
    <<  TABLEE
    <<  CENTERS <<  "<b>"
    <<  tr("Daily Graph Hints")
    <<  "</b>" <<  CENTERE
    <<  TABLES
    <<  ROWS  << COLS << QString(tr("Click on date")) << CSEP << QString(tr("Toggle Calendar on/off")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Detailed: Click on colored event")) << CSEP << QString(tr("Jump to event tab with event opened")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Detailed: Click on a session (at bottom)")) << CSEP << QString(tr("Toggle session disable / enable session")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Event: Click on an event")) << CSEP << QString(tr("Time Selection 3 min before event 20 sec after")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Bookmark")) << CSEP << QString(tr("Save current Time Selection")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Search Tab")) << CSEP << QString(tr("Search data base")) << COLE << ROWE
    <<  TABLEE
    <<  CENTERS <<  "<b>"
    <<  tr("Miscellaneous Hints")
    <<  "</b>" <<  CENTERE
    <<  TABLES
    <<  ROWS  << COLS << QString(tr("OverView: Shift Click on a date")) << CSEP << QString(tr("Jumps to date in the Daily Tab")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Daily: Event (bottom left corner) ")) << CSEP << QString(tr("Select Events to view")) << COLE << ROWE
    <<  ROWS  << COLS << QString(tr("Graph / Chart (bottom right corner)")) << CSEP << QString(tr("Selects graphs to view")) << COLE << ROWE
    <<  TABLEE
        ;
return strList.join("\n");
}

QString SaveGraphLayoutSettings::helpInfo() {
QStringList strList;
    strList<<QString("<p>")     // no fixed colour: the text follows the light or dark palette
<<QString(tr("This feature manages the saving and restoring of Layout Settings."))
    <<QString("<br>")
<<QString(tr("Layout Settings control the layout of a graph or chart."))
    <<QString("<br>")
<<QString(tr("Different Layouts Settings can be saved and later restored."))
    <<QString("<br> </p> <table width=\"100%\"> <tr><td><b>")
<<QString(tr("Button"))
    <<QString("</b></td> <td><b>")
<<QString(tr("Description"))
    <<QString("</b></td></tr> <tr><td valign=\"top\">")
<<QString(tr("Add"))
    <<QString("</td> <td>")
<<QString(tr("Creates a copy of the current Layout Settings."))
    <<QString("<br>")
<<QString(tr("The default description is the current date."))
    <<QString("<br>")
<<QString(tr("The description may be changed."))
    <<QString("<br>")
<<QString(tr("The Add button will be grayed out when maximum number is reached."))
    <<QString("<br>")
    <<QString("</td></tr> <tr><td><i><u>")
<<QString(tr("Other Buttons"))
    <<QString("</u> </i></td>  <td>")
<<QString(tr("Grayed out when there are no selections"))
    <<QString("</td></tr> <tr><td>")
<<QString(tr("Restore"))
    <<QString("</td> <td>")
<<QString(tr("Loads the Layout Settings from the selection. Stays Open"))
    <<QString("</td></tr> <tr><td>")
<<QString(tr("Rename"))
    <<QString("</td><td>")
<<QString(tr("Modify the description of the selection. Same as a double click."))
    <<QString("</td></tr> <tr><td valign=\"top\">")
<<QString(tr("Update"))
    <<QString("</td><td>")
<<QString(tr("Saves the current Layout Settings to the selection."))
    <<QString("<br>")
<<QString(tr("Prompts for confirmation."))
    <<QString("</td></tr> <tr><td valign=\"top\">")
<<QString(tr("Delete"))
    <<QString("</td> <td>")
<<QString(tr("Deletes the selection."))
    <<QString("<br>")
<<QString(tr("Prompts for confirmation."))
    <<QString("</td></tr> <tr><td><i><u>")
<<QString(tr("Control"))
    <<QString("</u> </i></td> <td></td></tr> <tr><td>")
<<QString(tr("Exit"))
    <<QString("</td> <td>")
<<QString(tr("(Red circle with a white \"X\".) Returns to OSCAR menu."))
    <<QString("</td></tr> <tr><td>")
<<QString(tr("Return"))
    <<QString("</td> <td>")
<<QString(tr("Next to Exit icon. Only in Help Menu. Returns to Layout menu."))
    <<QString("</td></tr> <tr><td>")
<<QString(tr("Escape Key"))
    <<QString("</td> <td>")
<<QString(tr("Exit the Help or Layout menu."))
    <<QString("</td></tr> </table> <p><b>")
<<QString(tr("Layout Settings"))
    <<QString("</b></p> <table width=\"100%\"> <tr> <td>")
<<QString(tr("* Name"))
    <<QString("</td> <td>")
<<QString(tr("* Pinning"))
    <<QString("</td> <td>")
<<QString(tr("* Plots Enabled"))
    <<QString("</td> <td>")
<<QString(tr("* Height"))
    <<QString("</td> </tr> <tr> <td>")
<<QString(tr("* Order"))
    <<QString("</td> <td>")
<<QString(tr("* Event Flags"))
    <<QString("</td> <td>")
<<QString(tr("* Dotted Lines"))
    <<QString("</td> <td>")
<<QString(tr("* Height Options"))
    <<QString("</td> </tr> </table> <p><b>")
<<QString(tr("General Information"))
    <<QString("</b></p> <ul style=margin-left=\"20\"; > <li>")
<<QString(tr("Maximum description size = 80 characters.	"))
    <<QString("</li> <li>")
<<QString(tr("Maximum Saved Layout Settings = 30.	"))
    <<QString("</li> <li>")
<<QString(tr("Saved Layout Settings can be accessed by all profiles."))
    <<QString("<li>")
<<QString(tr("Layout Settings only control the layout of a graph or chart."))
    <<QString("<br>")
<<QString(tr("They do not contain any other data."))
    <<QString("<br>")
<<QString(tr("They do not control if a graph is displayed or not."))
    <<QString("</li> <li>")
<<QString(tr("Layout Settings for daily and overview are managed independently."))
    <<QString("</li> </ul>");
return strList.join("\n");
};

bool SaveGraphLayoutSettings::verifyItem(QListWidgetItem* item,QString text,QIcon* icon) {
    //if (verifyhelp() ) return false;
    if (item) return true;
    initminMenuListSize();
    confirmAction( text ,"" , icon);
    return false;
}

/*! \brief Shows a standard message box and returns true when the user answers with \a success
 *
 *  \a top is the main text (a layout's description or a message) and \a bottom the question, if
 *  any. Both are shown as plain text, as a description is typed by the user. The box has the
 *  application's normal colours; \a icon is the action's icon, drawn to suit light or dark.
 */
bool SaveGraphLayoutSettings::confirmAction(QString top ,QString bottom,QIcon* icon,QMessageBox::StandardButtons flags , QMessageBox::StandardButton adefault, QMessageBox::StandardButton success) {
    QMessageBox msgBox(menu.dialog);
    msgBox.setWindowTitle(menu.dialog->windowTitle());
    msgBox.setTextFormat(Qt::PlainText);
    msgBox.setText(top);
    msgBox.setInformativeText(bottom);
    if (icon!=nullptr) {
        const int size = msgBox.style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, &msgBox);
        msgBox.setIconPixmap(buttonIcon(icon).pixmap(QSize(size,size)));
    }
    msgBox.setStandardButtons(flags);
    msgBox.setDefaultButton(adefault);
    return msgBox.exec()==success;
}

/*! \brief Returns \a icon for a button: as it is in light mode, redrawn in the button text colour in dark mode
 *
 *  The icons are black glyphs on a transparent background, which don't show on a dark button.
 *  Also used for the icon in the message boxes.
 */
QIcon SaveGraphLayoutSettings::buttonIcon(const QIcon* icon) const {
    const QPalette palette = QApplication::palette();
    if (!Theme::isDark(palette)) return *icon;
    QPixmap pixmap = icon->pixmap(icon->availableSizes().value(0, QSize(64,64)));
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), palette.color(QPalette::Active, QPalette::ButtonText));
    painter.end();
    return QIcon(pixmap);
}

/*! \brief Gives the menu's buttons the colours and icons of the colour scheme in effect
 *
 *  Called each time the menu opens, as the menu is built once and the scheme may have changed
 *  since. manageButtonApperance() then sets the style of the buttons that depend on the selection.
 */
void SaveGraphLayoutSettings::applyButtonTheme() {
    createStyleSheets();
    menuAddFullBtn->setIcon(buttonIcon(m_icon_add));
    menuAddBtn->setIcon(buttonIcon(m_icon_add));
    menuRestoreBtn->setIcon(buttonIcon(m_icon_restore));
    menuRenameBtn->setIcon(buttonIcon(m_icon_rename));
    menuUpdateBtn->setIcon(buttonIcon(m_icon_update));
    menuDeleteBtn->setIcon(buttonIcon(m_icon_delete));
    menuAddFullBtn->setStyleSheet(styleOff);
    menuAddBtn->setStyleSheet(styleOn);
    menuHelpBtn->setStyleSheet(styleOn);
}

/*! \brief Returns the style sheet of a button that looks available (\a on) or greyed out
 *
 *  Light mode keeps its fixed colours. Dark mode takes them from the application palette:
 *  the button colours when on, the window colour and disabled text when greyed out.
 */
QString SaveGraphLayoutSettings::calculateButtonStyle(bool on,bool exitBtn){
        QString colors = on ? "color: black;background-color:white;"
                            : "color: darkgray;background-color:#e8e8e8 ;";
        const QPalette palette = QApplication::palette();
        if (Theme::isDark(palette)) {
            const QColor text = on ? palette.color(QPalette::Active, QPalette::ButtonText)
                                   : palette.color(QPalette::Disabled, QPalette::ButtonText);
            const QColor background = palette.color(QPalette::Active, on ? QPalette::Button : QPalette::Window);
            colors = QString("color: %1;background-color:%2;").arg(text.name(), background.name());
        }
        QString btnStyle=QString("QPushButton{%1%2}").arg(colors
            ).arg(!exitBtn ?
            ""
            :
            "background: transparent;"
            "border-radius: 8px;"
            "border: 2px solid transparent;"
            "max-width:1em;"
            "border:none;"
            );
        // No QToolTip rule: the buttons use the tooltip colors set for the whole application (ToolTipColors).
        return btnStyle;
}

void SaveGraphLayoutSettings::looksOn(QPushButton* button,bool on){
    //button->setEnabled(on);
    button->setStyleSheet(on?styleOn:styleOff);
}

void SaveGraphLayoutSettings::manageButtonApperance() {

    bool enable = menuList->currentItem();
    looksOn(menuUpdateBtn,enable);
    looksOn(menuRestoreBtn,enable);
    looksOn(menuDeleteBtn,enable);
    looksOn(menuRenameBtn,enable);

    if (nextNumToUse<0) {  // check if at Maximum limit
        // must always hide first
        menuAddBtn->hide();
        menuAddFullBtn->show();
    } else {
        // must always hide first
        menuAddFullBtn->hide();
        menuAddBtn->show();
    }
}


void SaveGraphLayoutSettings::add_feature() {
    if(!graphView) return;
    QString desc = QDateTime::currentDateTime().toString();
    writeSettings(nextNumToUse, desc);
    QListWidgetItem* item = updateFileList(nextNumToUse);
    if (item!=nullptr) {
        menuList->setCurrentItem(item,QItemSelectionModel::ClearAndSelect);
        menuList->editItem(item);
    }
    menuList->sortItems();
    resizeMenu();
}

void SaveGraphLayoutSettings::addFull_feature() {
    verifyItem( nullptr,tr("Maximum number of Items exceeded.") , m_icon_add);
}

void SaveGraphLayoutSettings::update_feature() {
    if(!graphView) return;
    QListWidgetItem* item = menuList->currentItem();
    if (!verifyItem(item, tr("No Item Selected"), m_icon_update)) return;
    if(!confirmAction(item->text(), tr("Ok to Update?"), m_icon_update)) return;
    int slotIndex = item->data(fileNameRole).toInt();
    writeSettings(slotIndex, item->text());
}

void SaveGraphLayoutSettings::restore_feature() {
    if(!graphView) return;
    QListWidgetItem* item = menuList->currentItem();
    if (!verifyItem(item, tr("No Item Selected"), m_icon_restore)) return;
    int slotIndex = item->data(fileNameRole).toInt();
    loadSettings(slotIndex);
    closeMenu();
}

void SaveGraphLayoutSettings::rename_feature() {
    if(!graphView) return;
    QListWidgetItem *	item=menuList->currentItem();
    if (!verifyItem(item,  tr("No Item Selected") ,  m_icon_rename)) return ;
    menuList->editItem(item);
    // SaveGraphLayoutSettings::itemChanged(QListWidgetItem *item) is called when edit changes the entry.
    // itemChanged will update the description map
}

void SaveGraphLayoutSettings::help_exit_feature() {
    closeMenu();
    closeHelp();
}

void SaveGraphLayoutSettings::help_feature() {
    initminMenuListSize();
    createHelp();
    if(!help.dialog) return;
    if (help.isOpen()) {
        closeHelp();
        return;
    }
    help.showDialog();
}

void SaveGraphLayoutSettings::delete_feature() {
    if(!graphView) return;
    QListWidgetItem* item = menuList->currentItem();
    if (!verifyItem(item, tr("No Item Selected"), m_icon_delete)) return;
    if(!confirmAction(item->text(), tr("Ok To Delete?"), m_icon_delete)) return;

    int slotIndex = item->data(fileNameRole).toInt();
    deleteSettings(slotIndex);
    delete item;
    if (nextNumToUse < 0) {
        nextNumToUse = slotIndex;
    }
    manageButtonApperance();
    resizeMenu();
}

void SaveGraphLayoutSettings::itemChanged(QListWidgetItem *item)
{
    int slotIndex = item->data(fileNameRole).toInt();
    QString desc = item->text();

    // use only the first line in a multiline string. Can be set using cut and paste
    QRegularExpressionMatch match = singleLineRe->match(desc);
    if (match.hasMatch()) {
        desc = match.captured(1).trimmed();
    } else {
        desc = "";
    }
    if (desc.length() > maxDescriptionLen) {
        desc = desc.left(maxDescriptionLen - 3) + "...";
    }
    if (desc.length() > 0) {
        GraphLayoutsRepository repo;
        repo.updateNamedLayoutDescription(title.toLower(), slotIndex, desc);
    } else {
        // Restore previous description from DB
        GraphLayoutData layoutData;
        GraphLayoutsRepository repo;
        if (repo.loadNamedLayout(title.toLower(), slotIndex, layoutData)) {
            desc = layoutData.description;
        }
    }
    item->setText(desc);
    menuList->sortItems();
    menuList->setCurrentItem(item);
    resizeMenu();
}

void SaveGraphLayoutSettings::itemSelectionChanged()
{
    initminMenuListSize();
    manageButtonApperance();
}

void SaveGraphLayoutSettings::initminMenuListSize() {
    if (minMenuDialogSize.width()==0) {
        menuDialogSize     = menu.dialog->size();
        minMenuDialogSize  = menuDialogSize;

        menuListSize       = menuList->size();
        minMenuListSize    = menuListSize;

        dialogListDiff     = menuDialogSize - menuListSize;

        dialogListDiff.setWidth (horizontalWidthAdjustment + dialogListDiff.width());

        resizeMenu();
    }
};

void SaveGraphLayoutSettings::writeSettings(int slotIndex, const QString& description) {
    QByteArray data = graphView->serializeSettings();
    GraphLayoutsRepository repo;
    repo.saveNamedLayout(title.toLower(), slotIndex, description, gVversion, data);
}

void SaveGraphLayoutSettings::loadSettings(int slotIndex) {
    GraphLayoutsRepository repo;
    GraphLayoutData layoutData;
    if (repo.loadNamedLayout(title.toLower(), slotIndex, layoutData)) {
        graphView->deserializeSettings(layoutData.data);
        graphView->updateScale();
    }
}

void SaveGraphLayoutSettings::deleteSettings(int slotIndex) {
    GraphLayoutsRepository repo;
    repo.deleteNamedLayout(title.toLower(), slotIndex);
}



QSize  SaveGraphLayoutSettings::maxSize(const QSize AA , const QSize BB ) {
    return QSize (  qMax(AA.width(),BB.width()) , qMax(AA.height(),BB.height() ) );
}

bool  SaveGraphLayoutSettings::sizeEqual(const QSize AA , const QSize BB ) {
    return  (AA.width()==BB.width()) && ( AA.height()==BB.height()) ;
}



void SaveGraphLayoutSettings::resizeMenu() {
    if (minMenuDialogSize.width()==0) return;

    QSize newSize = calculateMenuDialogSize();
    newSize.setWidth ( newSize.width());

    menuDialogSize = menu.dialog->size();

    if ( sizeEqual(newSize , menuDialogSize)) {
        // no work to do
        return;
    };

    if ( menuDialogSize.width() < newSize.width() )  {
        menu.dialog->setMinimumWidth(newSize.width());
        menu.dialog->setMaximumWidth(QWIDGETSIZE_MAX);
    } else if ( menuDialogSize.width() > newSize.width() )  {
        menu.dialog->setMaximumWidth(newSize.width());
        menu.dialog->setMinimumWidth(newSize.width());
    }
    if ( menuDialogSize.height() < newSize.height() )  {
        menu.dialog->setMinimumHeight(newSize.height());
        menu.dialog->setMaximumHeight(QWIDGETSIZE_MAX);
    } else if ( menuDialogSize.height() > newSize.height() )  {
        menu.dialog->setMaximumHeight(newSize.height());
        menu.dialog->setMinimumHeight(newSize.height());
    }
    menuDialogSize = newSize;
}

QSize SaveGraphLayoutSettings::calculateMenuDialogSize() {
    if (menuDialogSize.width()==0) return QSize(0,0);
    QListWidgetItem* item;
    widestItem=nullptr;
    QFontMetrics fm = QFontMetrics(menu.font);

    // account for scrollbars.
    QSize returnValue = QSize( 0 , fm.height() );  // add an extra space at the bottom. width didn't work

    // Account for dialog Size
    returnValue += dialogListDiff;
    QSize size;

    for (int index = 0; index < menuList->count(); ++index) {
        item = menuList->item(index);
        if (!item) continue;
        size  = fm.boundingRect(item->text()).size();
        if (returnValue.width() < size.width()) {
            returnValue.setWidth( qMax( returnValue.width(),size.width()));
            widestItem=item;
        }
        returnValue.setHeight( returnValue.height()+size.height());
    }
    returnValue.setWidth( horizontalWidthAdjustment +  returnValue.width() ) ;

    returnValue = maxSize(returnValue, minMenuDialogSize);
    return returnValue;
}

QListWidgetItem* SaveGraphLayoutSettings::updateFileList(int findSlot) {
    QListWidgetItem* ret = nullptr;
    manageButtonApperance();
    menuList->clear();
    nextNumToUse = -1;

    GraphLayoutsRepository repo;
    QList<GraphLayoutData> layouts = repo.loadAllNamedLayouts(title.toLower());

    int count = 0;
    for (const GraphLayoutData& layout : layouts) {
        if (nextNumToUse < 0 && layout.slotIndex != count) {
            nextNumToUse = count;   // first gap found
        }
        count++;

        QListWidgetItem* item = new QListWidgetItem(layout.description);
        item->setData(fileNameRole, layout.slotIndex);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        menuList->addItem(item);
        if (findSlot >= 0 && layout.slotIndex == findSlot) {
            ret = item;
        }
    }

    if (nextNumToUse < 0) {
        nextNumToUse = (count < maxFiles) ? count : -1;
    }

    manageButtonApperance();
    menuList->sortItems();
    return ret;
}


void SaveGraphLayoutSettings::closeHelp() {
    if (help.isOpen()) {
        help.dialog->close();
    }
}

void SaveGraphLayoutSettings::closeHint() {
    if (hint.isOpen()) {
        hint.dialog->close();
    }
}

void SaveGraphLayoutSettings::closeMenu() {
    closeHelp();
    closeHint();
    if (menu.isOpen()) {
        menu.dialog->close();
    }
}

void SaveGraphLayoutSettings::triggerLayout(gGraphView* graphView) {
    if (!menu.dialog) return;
    if (menu.isOpen()) {
        closeMenu();
        return;
    }
    this->graphView=graphView;
    updateFileList();
    applyButtonTheme();
    manageButtonApperance();
    menu.showDialog();      // not modal; closing it closes its help (see the constructor)
}

QSize   textsize(QFont font ,QString text) {
   return QFontMetrics(font).size(0 , text);
}

void SaveGraphLayoutSettings::createHint() {
    createHelp(hint, tr("Graph Short-Cuts Help"), hintInfo());
    if (hint.exitBtn) {
        hint.dialog->connect(hint.exitBtn,    SIGNAL(clicked()), this, SLOT (closeHint()));
    }
};

void SaveGraphLayoutSettings::hintHelp() {
    if (hint.isOpen()) {
        hint.dialog->close();
        return;
    }
    createHint();
    hint.showDialog();
}




//====================================================================================================
//====================================================================================================


