/* Profile Selector Implementation
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2018 Mark Watkins
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#define TEST_MACROS_ENABLEDoff
#include "test_macros.h"

#include <QGuiApplication>
#include <QHeaderView>
#include <QMessageBox>
#include <QProgressDialog>
#include <QFileInfo>
#include <QDir>

#include "profileselector.h"
#include "ui_profileselector.h"
#include "profileselectorstyle.h"
#include "uikit/tagdelegate.h"
#include "uikit/theme.h"
#include "version.h"


#include "SleepLib/profiles.h"
#include "daily.h"
//#include "overview.h"
//#include "statistics.h"
#include "mainwindow.h"
#include "newprofile.h"
#include "cprogressbar.h"
//#include "version.h"

// Database includes
#include "database/profile_repository.h"
#include "database/machine_repository.h"
#include "database/user_info_repository.h"
#include "database/app_preferences_repository.h"
#include "database/database_manager.h"

static const QString kSortPrefCategory = QStringLiteral("ProfileSelector");
static const QString kSortPrefColumnKey = QStringLiteral("sortColumn");
static const QString kSortPrefOrderKey  = QStringLiteral("sortOrder");
static const QString kSortPrefHistoryKey = QStringLiteral("sortHistory");

extern MainWindow * mainwin;

MySortFilterProxyModel2::MySortFilterProxyModel2(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    dateColumn = 4;
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

bool MySortFilterProxyModel2::filterAcceptsRow(int sourceRow,
        const QModelIndex &sourceParent) const
{
    QModelIndex index0 = sourceModel()->index(sourceRow, 0, sourceParent);
    QModelIndex index1 = sourceModel()->index(sourceRow, 1, sourceParent);
    QModelIndex index2 = sourceModel()->index(sourceRow, 2, sourceParent);
    QModelIndex index5 = sourceModel()->index(sourceRow, 5, sourceParent);
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    return (sourceModel()->data(index0).toString().contains(filterRegExp())
        || sourceModel()->data(index1).toString().contains(filterRegExp())
        || sourceModel()->data(index2).toString().contains(filterRegExp())
        || sourceModel()->data(index5).toString().contains(filterRegExp())
        );
#else
    QRegularExpression filterExp = filterRegularExpression();
    return (sourceModel()->data(index0).toString().contains(filterExp)
            || sourceModel()->data(index1).toString().contains(filterExp)
            || sourceModel()->data(index2).toString().contains(filterExp)
            || sourceModel()->data(index5).toString().contains(filterExp)
           );
#endif
}


ProfileSelector::ProfileSelector(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ProfileSelector)
{
    ui->setupUi(this);

    // Colours come from the theme, which reads the application palette, so the page follows
    // the system's light or dark setting like the Welcome page and the Oximetry Wizard.
    setAutoFillBackground(true);
    applyTheme();
    ui->resetFilterButton->setLineIcon(UiKit::LineIcon::Clear);
    ui->resetFilterButton->setAccessibleName(ui->resetFilterButton->toolTip());
    ui->verticalLayout_2->setContentsMargins(Theme::PagePadding, Theme::PagePadding,
                                             Theme::PagePadding, Theme::PagePadding);
    ui->horizontalLayout->setSpacing(Theme::SectionGap);
    ui->verticalLayout_1->setSpacing(Theme::TileGap);

    // Tile-style buttons: a bold name and a line saying what the button does
    using UiKit::CommandButton;
    using UiKit::LineIcon;
    ui->buttonOpenProfile->setup(LineIcon::FolderOpen, tr("Open Profile"),
                                 tr("Load the selected profile's data"), CommandButton::Kind::Primary);
    ui->buttonEditProfile->setup(LineIcon::Edit, tr("Edit Profile"),
                                 tr("Change personal, device and doctor details"));
    ui->buttonNewProfile->setup(LineIcon::UserAdd, tr("New Profile"),
                                tr("Set up another person or device"));
    ui->buttonDestroyProfile->setup(LineIcon::Delete, tr("Destroy Profile"),
                                    tr("Permanently delete the profile and its backups"),
                                    CommandButton::Kind::Danger);

    // The table takes all the extra width; the button column stays at the buttons' width
    int column = 0;
    for (CommandButton *b : { ui->buttonOpenProfile, ui->buttonEditProfile,
                              ui->buttonNewProfile, ui->buttonDestroyProfile }) {
        column = qMax(column, b->sizeHint().width());
    }
    ui->frame_2->setMinimumWidth(0);
    ui->frame_2->setFixedWidth(column);
    ui->horizontalLayout->setStretch(0, 1);
    ui->horizontalLayout->setStretch(1, 0);

    // Text sizes are multiples of the application font, as on the Welcome page
    ui->OSCAR_Label->setFont(Theme::font(1.6, QFont::DemiBold));
    ui->versionLabel->setFont(Theme::font(1.0));
    ui->versionLabel->setText(tr("Version %1").arg(getVersion().displayString()));
    ui->profileInfoGroupBox->setFont(Theme::font(1.15, QFont::DemiBold));
    ui->profileInfoLabel->setFont(Theme::font(1.0));
    ui->diskSpaceInfo->setFont(Theme::font(1.0));

    // Compact rows (users may have dozens of profiles); the open profile gets an "Open" tag
    ui->profileView->setShowGrid(false);
    ui->profileView->setItemDelegateForColumn(0, new UiKit::TagDelegate(ui->profileView));
    QHeaderView *rows = ui->profileView->verticalHeader();
    rows->setSectionResizeMode(QHeaderView::Fixed);
    rows->setDefaultSectionSize(qRound(QFontMetrics(ui->profileView->font()).height() * 1.55));

    model = nullptr;
    proxy = nullptr;

    showDiskUsage = false;  // in case I want to preference it later
    on_diskSpaceInfo_linkActivated(showDiskUsage ? "show" : "hide");

    ui->diskSpaceInfo->setVisible(false);

    QItemSelectionModel * sm = ui->profileView->selectionModel();
    if (sm)
        connect(sm, SIGNAL(currentRowChanged(QModelIndex,QModelIndex)), this, SLOT(on_selectionChanged(QModelIndex,QModelIndex)));
    ui->buttonEditProfile->setEnabled(false);
    ui->buttonOpenProfile->setEnabled(false);
    ui->buttonDestroyProfile->setEnabled(false);

    connect(ui->profileView->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, &ProfileSelector::onSortIndicatorChanged);
}

void ProfileSelector::applyTheme()
{
    // The stylesheet bakes colours in, so it is rebuilt only when the palette really changed;
    // that also stops the setPalette() below from re-entering through changeEvent().
    const QPalette app = QGuiApplication::palette();
    const QString key = app.color(QPalette::Base).name() + app.color(QPalette::Window).name()
                        + app.color(QPalette::Text).name() + app.color(QPalette::Highlight).name();
    if (key == m_themeKey) return;
    m_themeKey = key;

    QPalette p = palette();
    p.setColor(QPalette::Window, Theme::color(Theme::Role::Page));
    setPalette(p);
    setStyleSheet(ProfileSelectorStyle::styleSheet(app));
}

void ProfileSelector::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // The system switched between light and dark (or another palette was applied)
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ThemeChange) {
        applyTheme();
    }
}

void ProfileSelector::onSortIndicatorChanged(int column, Qt::SortOrder order)
{
    if (restoringSort) return;

    // A new sort on a column supersedes any earlier sort on that same column.
    for (int i = sortHistory.size() - 1; i >= 0; --i) {
        if (sortHistory[i].first == column) sortHistory.removeAt(i);
    }
    if (column >= 0) sortHistory.append(qMakePair(column, order));

    if (!DatabaseManager::instance().isOpen()) return;
    QStringList parts;
    for (const auto &key : sortHistory) {
        parts << QString("%1:%2").arg(key.first).arg(static_cast<int>(key.second));
    }
    AppPreferencesRepository repo;
    repo.save(kSortPrefCategory, kSortPrefHistoryKey, parts.join(';'));
}

void ProfileSelector::loadSortHistory()
{
    if (!DatabaseManager::instance().isOpen()) return;

    QString history;
    int col = -1;
    Qt::SortOrder order = Qt::AscendingOrder;
    AppPreferencesRepository repo;
    const QList<AppPrefData> rows = repo.loadByCategory(kSortPrefCategory);
    for (const AppPrefData& row : rows) {
        if (row.key == kSortPrefHistoryKey)     history = row.value;
        else if (row.key == kSortPrefColumnKey) col     = row.value.toInt();
        else if (row.key == kSortPrefOrderKey)  order   = static_cast<Qt::SortOrder>(row.value.toInt());
    }

    sortHistory.clear();
    if (!history.isEmpty()) {
        const QStringList parts = history.split(';', Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QStringList kv = part.split(':');
            if (kv.size() != 2) continue;
            bool okCol = false, okOrder = false;
            const int c = kv[0].toInt(&okCol);
            const int o = kv[1].toInt(&okOrder);
            if (!okCol || !okOrder || c < 0 || (o != Qt::AscendingOrder && o != Qt::DescendingOrder)) continue;
            sortHistory.append(qMakePair(c, static_cast<Qt::SortOrder>(o)));
        }
    } else if (col >= 0) {
        // Preferences written before the full history was kept.
        sortHistory.append(qMakePair(col, order));
    }
}

void ProfileSelector::applySortHistory()
{
    restoringSort = true;
    ui->profileView->setSortingEnabled(true);
    // Column -1 returns the proxy to source-model order, so the replay does not inherit
    // whatever order the header's default indicator (Profile, descending) produced.
    ui->profileView->sortByColumn(-1, Qt::AscendingOrder);
    bool sorted = false;
    for (const auto &key : sortHistory) {
        if (key.first >= model->columnCount()) continue;
        ui->profileView->sortByColumn(key.first, key.second);
        sorted = true;
    }
    if (!sorted) ui->profileView->sortByColumn(0, Qt::AscendingOrder);
    restoringSort = false;
}

ProfileSelector::~ProfileSelector()
{
    QItemSelectionModel * sm = ui->profileView->selectionModel();
    disconnect(sm, SIGNAL(currentRowChanged(QModelIndex,QModelIndex)), this, SLOT(on_selectionChanged(QModelIndex,QModelIndex)));
    delete ui;
}

void ProfileSelector::updateProfileList()
{
    QItemSelectionModel * sm = ui->profileView->selectionModel();
    if (sm) disconnect(sm, SIGNAL(currentRowChanged(QModelIndex,QModelIndex)), this, SLOT(on_selectionChanged(QModelIndex,QModelIndex)));

    QString name;
    int w=0;
    if (proxy) delete proxy;
    if (model) delete model;

    const int columns = 6;
    model = new QStandardItemModel(0, columns, this);
    model->setHeaderData(0, Qt::Horizontal, tr("Profile"));
    model->setHeaderData(1, Qt::Horizontal, tr("Ventilator Brand"));
    model->setHeaderData(2, Qt::Horizontal, tr("Ventilator Model"));
    model->setHeaderData(3, Qt::Horizontal, tr("Other Data"));
    model->setHeaderData(4, Qt::Horizontal, tr("Last Imported"));
    model->setHeaderData(5, Qt::Horizontal, tr("Name"));

    int row = 0;

    QFontMetrics fm(ui->profileView->font());

    ui->profileView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->profileView->setFocusPolicy(Qt::NoFocus);
    ui->profileView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->profileView->setSelectionMode(QAbstractItemView::SingleSelection);

    // Try database first for fast profile list display
    ProfileRepository profileRepo;
    MachineRepository machineRepo;
    UserInfoRepository userRepo;
    
    QList<ProfileData> dbProfiles = profileRepo.findAll();
    
    if (!dbProfiles.isEmpty()) {
        qDebug() << "ProfileSelector: Loading profile list from database (" << dbProfiles.size() << "profiles)";
        
        for (const ProfileData& profileData : dbProfiles) {
            name = profileData.username;
            
            model->insertRows(row, 1, QModelIndex());
            model->setData(model->index(row, 0, QModelIndex()), name);
            model->setData(model->index(row, 0, QModelIndex()), name, Qt::UserRole+2);
            
            // Get user info from database
            UserInfoData userInfo = userRepo.findByProfile(profileData.id);
            QString usersname;
            if (!userInfo.lastName.isEmpty()) {
                usersname = formattedName(userInfo.lastName, userInfo.firstName);
            }
            model->setData(model->index(row, 5, QModelIndex()), usersname);
            model->setData(model->index(row, 5, QModelIndex()), nameSortKey(userInfo.lastName, userInfo.firstName), MySortFilterProxyModel2::NameSortRole);
            model->setData(model->index(row, 3, QModelIndex()), tr("Id: ") + QString::number(userInfo.id));

            // Get most recent CPAP machine from database
            QList<MachineData> allMachines = machineRepo.findByProfile(profileData.id);
            
            // Filter for CPAP machines and find most recent
            MachineData* mostRecent = nullptr;
            QDateTime latestImport;
            
            for (MachineData& mach : allMachines) {
                if (mach.machineType == MT_CPAP) {
                    QDateTime imported = QDateTime::fromString(mach.lastImported, Qt::ISODate);
                    if (!mostRecent || imported > latestImport) {
                        mostRecent = &mach;
                        latestImport = imported;
                    }
                }
            }
            
            if (mostRecent) {
                model->setData(model->index(row, 1, QModelIndex()), mostRecent->brand);
                model->setData(model->index(row, 2, QModelIndex()), mostRecent->model);
                model->setData(model->index(row, 4, QModelIndex()), 
                               latestImport.toString(QLocale::system().dateFormat(QLocale::ShortFormat)));
            }
            // The open profile is marked by updateProfileHighlight(), called below.

            QRect rect = fm.boundingRect(name);
            if (rect.width() > w) w = rect.width();
            
            row++;
        }
    }

    /***
      else {
        qDebug() << "ProfileSelector: Database empty, falling back to Profiles::profiles map";
        
        // Fall back to old method if database is empty
        QMap<QString, Profile *>::iterator pi;
        for (pi = Profiles::profiles.begin(); pi != Profiles::profiles.end(); pi++) {
            Profile *prof = pi.value();
            name = pi.key();

            Machine * mach = prof->GetMachine(MT_CPAP);  // only interested in last cpap machine...
            if (!mach) {
                qDebug() << "Couldn't find device info for" << name;
            }

            model->insertRows(row, 1, QModelIndex());
            QString usersname;
            if (!prof->user->lastName().isEmpty()) {
                usersname = formattedName(prof->user->lastName(), prof->user->firstName());
            }

            model->setData(model->index(row, 0, QModelIndex()), name);
            model->setData(model->index(row, 0, QModelIndex()), name, Qt::UserRole+2);
            model->setData(model->index(row, 5, QModelIndex()), usersname);
            model->setData(model->index(row, 5, QModelIndex()), nameSortKey(prof->user->lastName(), prof->user->firstName()), MySortFilterProxyModel2::NameSortRole);
            if (mach) {
                model->setData(model->index(row, 1, QModelIndex()), mach->brand());
                model->setData(model->index(row, 2, QModelIndex()), mach->model());
                model->setData(model->index(row, 4, QModelIndex()), mach->lastImported().toString(QLocale::system().dateFormat(QLocale::ShortFormat)));
            }
            QBrush bg = QColor(Qt::black);
            QFont font = QApplication::font();
            if (prof == p_profile) {
                bg = QBrush(openProfileHighlightColor);
                font.setBold(true);
            }
            for (int i=0; i<columns; i++) {
                model->setData(model->index(row, i, QModelIndex()), bg, Qt::ForegroundRole);
            }

            QRect rect = fm.boundingRect(name);
            if (rect.width() > w) w = rect.width();

            row++;
        }
    }
    w+=20;
***/

    // Rebuilding the model always replaces the selection model, so any previously
    // selected profile's info is no longer valid. Reset the panel and buttons to
    // their no-selection defaults; on_selectionChanged() repopulates them if/when
    // a row is (re)selected, and the currently-open profile's info is restored below.
    ui->profileInfoLabel->setText(row == 0 ? tr("You must create a profile")
                                            : tr("Please select or create a profile..."));
    ui->profileInfoGroupBox->setTitle(tr("Profile: None"));
    ui->diskSpaceInfo->setVisible(false);
    ui->buttonOpenProfile->setEnabled(false);
    ui->buttonEditProfile->setEnabled(false);
    ui->buttonDestroyProfile->setEnabled(false);

    proxy = new MySortFilterProxyModel2(this);
    proxy->setSourceModel(model);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);

    ui->profileView->setModel(proxy);

    // Only load from preferences once; afterwards sortHistory tracks the user's clicks.
    if (sortHistory.isEmpty()) loadSortHistory();
    applySortHistory();

    QHeaderView *headerView = ui->profileView->horizontalHeader();
    headerView->setStretchLastSection(true);
    headerView->setSectionResizeMode(QHeaderView::Stretch);

    sm = ui->profileView->selectionModel();
    connect(sm, SIGNAL(currentRowChanged(QModelIndex,QModelIndex)), this, SLOT(on_selectionChanged(QModelIndex,QModelIndex)));

    // The rebuild above left the panel/buttons at their no-selection defaults.
    // If a profile is currently open, restore its info instead of leaving it blank.
    if (p_profile) {
        updateProfileHighlight(p_profile->user->userName());
    }
}

void ProfileSelector::updateProfileHighlight(QString name)
{

    // Clear the previous mark: plain font and no tag. Text colour comes from the stylesheet.
    QFont font = ui->profileView->font();
    font.setBold(false);
    for (int row=0;row < model->rowCount(); row++) {
        for (int i=0; i<model->columnCount(); i++) {
            QModelIndex idx = model->index(row, i, QModelIndex());
            model->setData(idx, QVariant(), Qt::ForegroundRole);
            model->setData(idx, font, Qt::FontRole);
        }
        model->setData(model->index(row, 0, QModelIndex()), QVariant(), UiKit::TagDelegate::TagRole);
    }

    // Mark the open profile: bold, with an "Open" tag after its name
    font.setBold(true);

    for (int row=0;row < proxy->rowCount(); row++) {
        QModelIndex idx = proxy->index(row, 0, QModelIndex());

        if (proxy->data(idx).toString().compare(name)==0) {
            on_selectionChanged(idx, QModelIndex());

            proxy->setData(idx, tr("Open"), UiKit::TagDelegate::TagRole);
            for (int i=0; i<proxy->columnCount(); i++) {
                idx = proxy->index(row, i, QModelIndex());
                proxy->setData(idx, font, Qt::FontRole);
            }
            break;
        }
    }
}

Profile *ProfileSelector::SelectProfile(QString profname)
{
    auto pit = Profiles::profiles.find(profname);
    if (pit == Profiles::profiles.end()) return nullptr;

    Profile * prof = pit.value();

    if (prof != p_profile) {
        updateProfileHighlight(profname);
    }

    return prof;
}

void ProfileSelector::on_profileView_doubleClicked(const QModelIndex &index)
{
    QModelIndex idx = proxy->index(index.row(), 0, QModelIndex());
    QString profname = proxy->data(idx, Qt::UserRole+2).toString();

    mainwin->OpenProfile(profname);
}

void ProfileSelector::on_profileFilter_textChanged(const QString &arg1)
{
    #if QT_VERSION < QT_VERSION_CHECK(6,0,0)
        QRegExp regExp("*"+arg1+"*", Qt::CaseInsensitive, QRegExp::Wildcard);
        proxy->setFilterRegExp(regExp);
    #else
        QRegularExpression regExp = QRegularExpression::fromWildcard("*" + arg1 + "*", Qt::CaseInsensitive);
        proxy->setFilterRegularExpression(regExp);
    #endif
}

// Clear filter list
void ProfileSelector::on_resetFilterButton_clicked()
{
    ui->profileFilter->clear();
}

QString ProfileSelector::selectedProfileName() const
{
    const QModelIndex idx = ui->profileView->currentIndex();
    if (!idx.isValid()) return QString();
    return proxy->data(proxy->index(idx.row(), 0, QModelIndex()), Qt::UserRole + 2).toString();
}

void ProfileSelector::on_buttonOpenProfile_clicked()
{
    if (ui->profileView->currentIndex().isValid()) {
        QString name = proxy->data(proxy->index(ui->profileView->currentIndex().row(), 0, QModelIndex()), Qt::UserRole+2).toString();
        mainwin->OpenProfile(name);
    }
}

void ProfileSelector::on_buttonEditProfile_clicked()
{
    if (ui->profileView->currentIndex().isValid()) {
        QString name = proxy->data(proxy->index(ui->profileView->currentIndex().row(), 0, QModelIndex()), Qt::UserRole+2).toString();
        qDebug() << "Editing" << name;

        Profile * prof = Profiles::profiles[name];
        //SelectProfile(name); // may not be necessary...

        NewProfile *newprof = new NewProfile(this,&name);
        newprof->edit(name);
        newprof->setWindowModality(Qt::ApplicationModal);
        newprof->setModal(true);
        if (newprof->exec() != NewProfile::Rejected) {
            QString usersname;
            if (!prof->user->lastName().isEmpty()) {
                usersname = formattedName(prof->user->lastName(), prof->user->firstName());
            }

            proxy->setData(proxy->index(ui->profileView->currentIndex().row(), 5, QModelIndex()), usersname);
            proxy->setData(proxy->index(ui->profileView->currentIndex().row(), 5, QModelIndex()), nameSortKey(prof->user->lastName(), prof->user->firstName()), MySortFilterProxyModel2::NameSortRole);
            if (prof == p_profile) updateProfileHighlight(name);
        }

        delete newprof;
    } else {
        QMessageBox::information(this, STR_MessageBox_Information, tr("Select a profile first"), QMessageBox::Ok);
    }
}

void ProfileSelector::on_buttonNewProfile_clicked()
{
    if (p_profile)
        mainwin->CloseProfile();

    NewProfile *newprof = new NewProfile(this);
    newprof->skipWelcomeScreen();
    newprof->setWindowModality(Qt::ApplicationModal);
    newprof->setModal(true);
    if (newprof->exec() == NewProfile::Accepted) {
        p_profile = Profiles::Get(AppSetting->profileName());
        if (p_profile != nullptr) {
            QString name = p_profile->user->userName();
            p_profile = nullptr;
            mainwin->OpenProfile(name);
            // Mark this profile as locally created.
            if (p_profile) {
                p_profile->user->setSource(QStringLiteral("Local"));
                p_profile->Save();
            }
        } else {
            qWarning() << AppSetting->profileName() << "yielded a null profile";
            p_profile=nullptr;
        }
        updateProfileList();
    }
    delete newprof;
}

void ProfileSelector::on_buttonDestroyProfile_clicked()
{
    if (ui->profileView->currentIndex().isValid()) {
        QString name = proxy->data(proxy->index(ui->profileView->currentIndex().row(), 0, QModelIndex()), Qt::UserRole+2).toString();
        // profiles.value() avoids inserting a null entry when the profile has a missing directory
        Profile * profile = Profiles::profiles.value(name, nullptr);

        // For profiles whose directory is missing (e.g. a cancelled import that left a DB
        // row behind), profile is null. Resolve the path from the database instead.
        QString path;
        if (profile) {
            path = profile->Get(PrefMacro(STR_GEN_DataFolder));
        } else {
            ProfileRepository pathRepo;
            ProfileData pd = pathRepo.findByUsername(name);
            if (pd.id > 0) {
                path = ProfileRepository::resolvePath(pd.dataFolder, GetAppData() + "/Profiles");
            }
        }

        // Refuse to act when the resolved path is the Profiles directory itself rather
        // than a profile inside it, which happens when a row carries no username (e.g. a
        // cancelled import). Compare cleaned paths so a trailing slash on either side
        // cannot slip past the guard and offer the whole directory for deletion.
        if (QDir::cleanPath(path) == QDir::cleanPath(GetAppData() + "/Profiles")) {
            QMessageBox::warning(this, STR_MessageBox_Error, tr("The selected profile does not appear to contain any data and cannot be removed by OSCAR"), QMessageBox::Ok);
            return;
        }

        QDialog confirmdlg;
        QVBoxLayout layout(&confirmdlg);

        // 493 - 497 modified to allow for translation - CN
        QString warning = STR_MessageBox_Warning;
        QString line1 = tr("You are about to destroy profile '<b>%1</b>'.").arg(name);
        QString line2 = tr("Think carefully, as this will irretrievably delete the profile along with all <b>backup data</b> stored under<br/>%1.").arg(path);
        QString line3 = tr("Enter the word <b>DELETE</b> below (exactly as shown) to confirm.");
        QLabel message(QString("<b>%1:</b> %2<br/><br/>%3<br/><br/>%4").arg(warning, line1, line2, line3), &confirmdlg);

        layout.insertWidget(0,&message,1);
        QLineEdit lineedit(&confirmdlg);
        layout.insertWidget(1, &lineedit, 1);
        QHBoxLayout layout2;
        layout.insertLayout(2,&layout2,1);
        QPushButton cancel(QString(tr("Cancel")), &confirmdlg);
        QPushButton accept(QString(tr("Delete Profile")), &confirmdlg);
        layout2.addWidget(&cancel);
        layout2.addStretch(1);
        layout2.addWidget(&accept);
        confirmdlg.connect(&cancel, SIGNAL(clicked()), &confirmdlg, SLOT(reject()));
        confirmdlg.connect(&accept, SIGNAL(clicked()), &confirmdlg, SLOT(accept()));
        confirmdlg.connect(&lineedit, SIGNAL(returnPressed()), &confirmdlg, SLOT(accept()));

        if (confirmdlg.exec() != QDialog::Accepted)
            return;

        if (lineedit.text().compare(tr("DELETE"))!=0) {
            QMessageBox::information(NULL, tr("Sorry"), tr("You need to enter DELETE in capital letters."), QMessageBox::Ok);
            return;
        }
        
        qDebug() << "Deleting Profile" << name;
        if (profile == p_profile) {
            // Shut down if active
            mainwin->CloseProfile();
        }
        Profiles::profiles.remove(name);

        // Create progress dialog for database deletion (Phase 1.5)
        // Using QProgressDialog directly for percentage-based progress
        QProgressDialog *dbProgress = new QProgressDialog(
            tr("Preparing database for deletion..."), 
            QString(), 0, 100, this,
            Qt::WindowSystemMenuHint | Qt::WindowTitleHint);
        dbProgress->setWindowModality(Qt::WindowModal);
        dbProgress->setMinimumWidth(400);
        dbProgress->setValue(0);
        dbProgress->setWindowTitle(tr("Deleting profile ") + name);
        dbProgress->show();
        QCoreApplication::processEvents();
        
        // Delete from database with progress reporting (Phase 1.5)
        ProfileRepository profileRepo;
        ProfileData profileData = profileRepo.findByUsername(name);
        bool dbDeleteSuccess = false;
        
        if (profileData.id > 0) {
            // Use Phase 1.5 optimized deletion with progress callbacks
            dbDeleteSuccess = profileRepo.removeWithProgress(profileData.id, 
                [dbProgress](int percent, const QString& message) {
                    // Update progress dialog with database deletion progress
                    dbProgress->setValue(percent);
                    dbProgress->setLabelText(message);
                    QCoreApplication::processEvents();  // Keep UI responsive
                }
            );
            
            if (dbDeleteSuccess) {
                qDebug() << "Deleted profile from database:" << name << "id:" << profileData.id;
            } else {
                qWarning() << "Failed to delete profile from database:" << name;
                QMessageBox::warning(this, STR_MessageBox_Error,
                                   tr("Failed to delete profile from database. The operation has been rolled back."),
                                   QMessageBox::Ok);
                updateProfileList();
                dbProgress->close();
                delete dbProgress;
                return;
            }
//        } else {
//            delete dbProgress;
        }

        // Now delete files from disk
        if (!path.isEmpty()) {
            qDebug() << "Counting items in" << path;

            int itemsProcessed = 0;
            dbProgress->setLabelText(tr("Deleting backup files..."));
            QCoreApplication::processEvents();  // Keep UI responsive
            bool deleteSuccess = removeDirWithProgress(path, dbProgress, 75, 100);
            
//            delete fileProgress;
            
            if (!deleteSuccess) {
                QMessageBox::information(this, STR_MessageBox_Error,
                                         tr("There was an error deleting the profile directory, you need to manually remove it.")+QString("\n\n%1").arg(path),
                                         QMessageBox::Ok);
            } else {
                qDebug() << "Deleted" << itemsProcessed << "items from" << path;
                dbProgress->setValue(100);
                QCoreApplication::processEvents();
                QMessageBox::information(this, STR_MessageBox_Information, tr("Profile '%1' was successfully deleted").arg(name),QMessageBox::Ok);
            }
        } else {
            // No files to delete
            QMessageBox::information(this, STR_MessageBox_Information, tr("Profile '%1' was successfully deleted").arg(name),QMessageBox::Ok);
        }

        dbProgress->close();
        delete dbProgress;

        updateProfileList();

    }
}

QString ProfileSelector::formattedName(const QString &lastName, const QString &firstName)
{
    // Translators: name display order. English default is "Last, First".
    // Override to "%2 %1" for locales that prefer "First Last" (e.g. French).
    return tr("%1, %2").arg(lastName, firstName);
}

QString ProfileSelector::nameSortKey(const QString &lastName, const QString &firstName)
{
    if (firstName.isEmpty())
        return lastName;
    return lastName + ", " + firstName;
}

QString ProfileSelector::formatSize(qint64 size)
{
    QStringList units = { tr("Bytes"), tr("KB"), tr("MB"), tr("GB"), tr("TB"), tr("PB") };
    int i;
    double outputSize = size;
    for (i=0; i<units.size()-1; i++) {
        if (outputSize < 1024) break;
        outputSize = outputSize/1024;
    }
    return QString("%0 %1").arg(outputSize, 0, 'f', 2).arg(units[i]);
}

qint64 getDatabaseTotalSize() {
    QString dbPath = GetAppData() + "/oscar.db";
    qint64 totalSize = 0;

    // Main database file
    QFileInfo dbFile(dbPath);
    if (dbFile.exists()) {
        totalSize += dbFile.size();
    }

    // WAL file
    QFileInfo walFile(dbPath + "-wal");
    if (walFile.exists()) {
        totalSize += walFile.size();
    }

    // Shared memory file
    QFileInfo shmFile(dbPath + "-shm");
    if (shmFile.exists()) {
        totalSize += shmFile.size();
    }

    return totalSize;
}
QString ProfileSelector::getProfileDiskInfo(Profile *profile)
{
    QString html;
    if (profile) {
//        qint64 sizeSummaries = profile->diskSpaceSummaries(); // Remove for OSCAR 2.0
        qint64 sizeDatabase = getDatabaseTotalSize();
        qint64 sizeBackups = profile->diskSpaceBackups();

        html += "<table>"
//                "<tr><td align=right>"+tr("Summaries:")+"</td><td>"+formatSize(sizeSummaries)+"</td></tr>"
//                "<tr><td align=right>"+tr("Events:")+"</td><td>"+formatSize(sizeEvents)+"</td></tr>"
                "<tr><td align=right>"+tr("Backups:")+"</td><td>"+formatSize(sizeBackups)+"</td></tr>"
                "<tr><td align=right>"+tr("Database (all profiles):")+"</td><td>"+formatSize(sizeDatabase)+"</td></tr>"
                "</table>";
    }
    return html;

}

void ProfileSelector::on_diskSpaceInfo_linkActivated(const QString &link)
{
    QString html;

    if (link == "show") {
        QString name = selectedProfileName();
        auto it = Profiles::profiles.find(name);
        Profile *selected = (it != Profiles::profiles.end()) ? it.value() : nullptr;
        html += "<a href='hide'>"+tr("Hide disk usage information")+"</a>"+getProfileDiskInfo(selected);
        showDiskUsage = true;
    } else {
        html += "<a href='show'>"+tr("Show disk usage information")+"</a>";
        showDiskUsage = false;
    }
    ui->diskSpaceInfo->setText(html);
}

void ProfileSelector::on_selectionChanged(const QModelIndex &index, const QModelIndex &)
{
    bool enabled = false;
    if (index.isValid()) {
        QString name = proxy->data(proxy->index(index.row(), 0, QModelIndex()), Qt::UserRole+2).toString();
        auto prof = Profiles::profiles.find(name);
        if (prof != Profiles::profiles.end()) {
            enabled = true;
            QString html = QString();
            Profile * profile = prof.value();

            if (!profile->user->lastName().isEmpty() && !profile->user->firstName().isEmpty()) {
                html += tr("Name: ") + formattedName(profile->user->lastName(), profile->user->firstName()) + "<br/>";
            }
            if (!profile->user->phone().isEmpty()) {
                html += tr("Phone: %1").arg(profile->user->phone())+"<br/>";
            }
            if (!profile->user->email().isEmpty()) {
                html += tr("Email: <a href='mailto:%1'>%1</a>").arg(profile->user->email())+"<br/>";
            }
            if (!profile->user->address().isEmpty()) {
                html += "<br/>"+tr("Address:")+"<br/>"+profile->user->address().trimmed().replace("\n","<br/>")+"<br/>";
            }
            if (html.isEmpty()) {
                html += tr("No profile information given")+"<br/>";
            }
            ui->diskSpaceInfo->setVisible(true);
            ui->profileInfoGroupBox->setTitle(tr("Profile: %1").arg(name));
            ui->profileInfoLabel->setText(html);

            if (showDiskUsage) { // ugly, but uh....
                ui->diskSpaceInfo->setText("<a href='hide'>"+tr("Hide disk usage information")+"</a>"+getProfileDiskInfo(prof.value()));
            }
        } else {
           ui->diskSpaceInfo->setText(tr("Something went wrong"));
        }
    }
    ui->buttonOpenProfile->setEnabled(enabled);
    ui->buttonEditProfile->setEnabled(enabled);
    // Destroy also works for a row whose profile folder is missing (it removes the
    // database entry), so it only needs a selected row.
    ui->buttonDestroyProfile->setEnabled(index.isValid());
}
