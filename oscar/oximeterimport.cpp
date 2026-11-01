/* Oximeter Import Wizard Implementation
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#define TEST_MACROS_ENABLEDoff
#include <test_macros.h>

#include <QButtonGroup>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QTimeZone>
#include <QVBoxLayout>

#include "oximeterimport.h"
#include "translation.h"
#include "ui_oximeterimport.h"
#include "SleepLib/calcs.h"
#include "SleepLib/viatomble/viatomble_availability.h"
#include "SleepLib/viatomble/viatomble_usb.h"
#include "mainwindow.h"
#ifdef OSCAR_HAVE_BLUETOOTH
#include "viatomimportpanel.h"
#endif

extern MainWindow * mainwin;

Qt::DayOfWeek firstDayOfWeekFromLocale();
QList<SerialOximeter *> GetOxiLoaders();

// The oximeter types, as stored in the profile (STR_OS_OximeterType). The values never
// change, whatever order the wizard shows the devices in.
enum OximeterTypeId { OXI_CMS50F37 = 0, OXI_CMS50 = 1, OXI_MD300W1 = 2, OXI_VIATOM = 3, OXI_EMAY = 4 };

// The device cards' order on the first page.
static const int DEVICE_ORDER[] = { OXI_VIATOM, OXI_EMAY, OXI_CMS50F37, OXI_CMS50, OXI_MD300W1 };

//! Oximeters whose recordings carry their own start time and are downloaded in the
//! import panel (over Bluetooth, or a Viatom/Wellue cable).
static bool downloadsInPanel(int type)
{
    return type == OXI_VIATOM || type == OXI_EMAY;
}

//! A dark theme: a few colours (badges, notes) need lighter shades on it.
static bool darkTheme(const QWidget *w)
{
    return w->palette().color(QPalette::Base).lightness() < 128;
}

static QString goodColor(const QWidget *w) { return darkTheme(w) ? "#6fcf97" : "#1b6b43"; }
static QString warnColor(const QWidget *w) { return darkTheme(w) ? "#f2b36b" : "#9a4a00"; }

//! A picture scaled for the screen's pixel density.
static QPixmap devicePicture(const QString &path, const QSize &size, const QWidget *w)
{
    const qreal dpr = w->devicePixelRatioF();
    QPixmap pm = QPixmap(path).scaled(size * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

//! A label that passes clicks through to the card or option button it sits on.
static QLabel *passiveLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    label->setWordWrap(true);
    return label;
}

// Cards and options are checkable buttons drawn with the theme's own colours.
static const char *const CARD_STYLE =
    "QPushButton#wizardCard { text-align: left; padding: 0px; border: 1px solid palette(mid);"
    "  border-radius: 10px; background: palette(base); }"
    "QPushButton#wizardCard:hover { border-color: palette(highlight); }"
    "QPushButton#wizardCard:checked { border: 2px solid palette(highlight); }"
    "QPushButton#wizardCard:disabled { background: palette(window); }";

OximeterImport::OximeterImport(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OximeterImport)
{
    ui->setupUi(this);
    setWindowTitle(tr("Oximeter Import Wizard"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(1000, 700);
    setStyleSheet(CARD_STYLE);

    // The step bar above the pages, the footer below them.
    auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    layout->insertWidget(0, buildStepBar());
    layout->addWidget(buildFooter());

    m_devicePage = buildDevicePage();
    m_methodPage = buildMethodPage();
    m_summaryPage = buildSummaryPage();
    ui->stackedWidget->addWidget(m_devicePage);
    ui->stackedWidget->addWidget(m_methodPage);
    ui->stackedWidget->addWidget(m_summaryPage);

    // CPAP-sync page
    Qt::DayOfWeek dow = firstDayOfWeekFromLocale();
    ui->calendarWidget->setFirstDayOfWeek(dow);
    ui->calendarWidget->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    ui->dateTimeEdit->setMinimumHeight(ui->dateTimeEdit->height() + 10);
    ui->syncCPAPGroup->setVisible(false);

    QVBoxLayout * barLayout = new QVBoxLayout;
    barLayout->setContentsMargins(0, 0, 0, 0);
    ui->sessBarFrame->setLayout(barLayout);
    sessbar = new SessionBar(this);
    sessbar->setSelectMode(true);
    sessbar->setMouseTracking(true);
    sessbar->setMinimumHeight(40);
    connect(sessbar, SIGNAL(sessionClicked(Session*)), this, SLOT(onSessionSelected(Session*)));
    barLayout->addWidget(sessbar, 1);

    m_suggestion = new QLabel(ui->syncPage);
    m_suggestion->setWordWrap(true);
    m_suggestion->setVisible(false);
    if (auto *syncLayout = qobject_cast<QVBoxLayout *>(ui->syncPage->layout())) {
        syncLayout->insertWidget(1, m_suggestion);
    }
    ui->tableOxiSessions->setSelectionBehavior(QAbstractItemView::SelectRows);

    // A Viatom/Wellue cable can be plugged in while the first page is showing.
    m_cableTimer.setInterval(2000);
    connect(&m_cableTimer, &QTimer::timeout, this, &OximeterImport::updateCableBadge);

    // The last oximeter chosen; Viatom/Wellue for a new profile.
    int type = p_profile->oxi->oximeterType();
    if (!m_devices->button(type)) {
        type = OXI_VIATOM;
    }
    m_devices->button(type)->setChecked(true);

    updateCableBadge();
    showPage(m_devicePage, DeviceStep);
}

OximeterImport::~OximeterImport()
{
    if (oximodule) {
        oximodule->trashRecords();
    }
    disconnect(sessbar, SIGNAL(sessionClicked(Session*)), this, SLOT(onSessionSelected(Session*)));
    delete ui;
}

void OximeterImport::setViatomFileImporter(std::function<bool(const QString &)> importFile)
{
    m_importViatomFile = std::move(importFile);
}

void OximeterImport::setViatomFilesImporter(std::function<bool()> importFiles)
{
    m_importViatomFiles = std::move(importFiles);
}

void OximeterImport::showViatomImport()
{
    m_devices->button(OXI_VIATOM)->setChecked(true);
    p_profile->oxi->setOximeterType(OXI_VIATOM);
    refreshMethods();
    showPage(m_methodPage, MethodStep);
}

bool OximeterImport::importedAny() const
{
#ifdef OSCAR_HAVE_BLUETOOTH
    if (m_viatomPanel && m_viatomPanel->importedAny()) {
        return true;
    }
#endif
    return m_importedFiles;
}

int OximeterImport::oximeterType() const
{
    return m_devices->checkedId();
}

OximeterImport::Method OximeterImport::method() const
{
    const int id = m_methods->checkedId();
    return id < 0 ? NoMethod : Method(id);
}

QString OximeterImport::deviceName(int type) const
{
    switch (type) {
    case OXI_VIATOM:   return tr("Viatom/Wellue");
    case OXI_EMAY:     return tr("EMAY");
    case OXI_CMS50F37: return tr("Contec CMS50F v3.7+");
    case OXI_CMS50:    return tr("Contec CMS50D+/E/F");
    case OXI_MD300W1:  return tr("ChoiceMMed MD300W1");
    }
    return QString();
}

bool OximeterImport::viatomUsbBuilt() const
{
    // The cable is read in the Viatom import panel, which needs a Bluetooth build.
    return ViatomBle::Usb::supported() && ViatomBle::availability() != ViatomBle::Availability::NotBuilt;
}

// ----------------------------------------------------------------------------------------
// Frame: step bar and footer

QWidget *OximeterImport::buildStepBar()
{
    auto *bar = new QWidget(this);
    bar->setAutoFillBackground(true);
    bar->setBackgroundRole(QPalette::Base);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(28, 12, 28, 12);
    row->setSpacing(26);
    const QStringList titles = { tr("Oximeter"), tr("Method"), tr("Import"), tr("Match to CPAP") };
    for (int i = 0; i < titles.size(); i++) {
        auto *number = new QLabel(QString::number(i + 1), bar);
        number->setFixedSize(24, 24);
        number->setAlignment(Qt::AlignCenter);
        auto *title = new QLabel(titles[i], bar);
        auto *step = new QHBoxLayout;
        step->setSpacing(8);
        step->addWidget(number);
        step->addWidget(title);
        row->addLayout(step);
        m_stepNumbers << number;
        m_stepTitles << title;
    }
    row->addStretch();
    return bar;
}

QWidget *OximeterImport::buildFooter()
{
    auto *footer = new QWidget(this);
    footer->setAutoFillBackground(true);
    footer->setBackgroundRole(QPalette::Base);
    auto *row = new QHBoxLayout(footer);
    row->setContentsMargins(20, 12, 20, 12);

    auto *about = new QLabel(QStringLiteral("<a href=\"about\">%1</a>").arg(tr("About oximetry in OSCAR")), footer);
    connect(about, &QLabel::linkActivated, this, [this]() { showAbout(); });

    m_cancelButton = new QPushButton(tr("Cancel"), footer);
    m_backButton = new QPushButton(tr("Back"), footer);
    m_nextButton = new QPushButton(tr("Next"), footer);
    m_nextButton->setDefault(true);
    for (QPushButton *b : { m_cancelButton, m_backButton, m_nextButton }) {
        b->setAutoDefault(false);
        b->setMinimumSize(96, 34);
    }
    connect(m_cancelButton, &QPushButton::clicked, this, &OximeterImport::reject);
    connect(m_backButton, &QPushButton::clicked, this, &OximeterImport::goBack);
    connect(m_nextButton, &QPushButton::clicked, this, &OximeterImport::goNext);

    row->addWidget(about);
    row->addStretch();
    row->addWidget(m_cancelButton);
    row->addWidget(m_backButton);
    row->addWidget(m_nextButton);
    return footer;
}

void OximeterImport::showPage(QWidget *page, Step step)
{
    ui->stackedWidget->setCurrentWidget(page);
    m_step = step;
    if (step == DeviceStep) {
        m_cableTimer.start();
    } else {
        m_cableTimer.stop();
    }
    updateStepBar();
    updateFooter();
}

void OximeterImport::updateStepBar()
{
    const QString highlight = palette().color(QPalette::Highlight).name();
    const QString onHighlight = palette().color(QPalette::HighlightedText).name();
    const QString muted = palette().color(QPalette::PlaceholderText).name();

    // What was chosen replaces a finished step's title; the last step depends on the device.
    QStringList titles = { tr("Oximeter"), tr("Method"), tr("Import"),
                           downloadsInPanel(oximeterType()) && method() != FilesMethod ? tr("Summary")
                                                                                    : tr("Match to CPAP") };
    if (m_step > DeviceStep) {
        titles[0] = deviceName(oximeterType());
    }
    if (m_step > MethodStep) {
        titles[1] = method() == UsbMethod ? tr("USB cable") : method() == BluetoothMethod ? tr("Bluetooth") : tr("Data files");
    }

    for (int i = 0; i < m_stepNumbers.size(); i++) {
        QLabel *number = m_stepNumbers[i];
        QLabel *title = m_stepTitles[i];
        title->setText(titles[i]);
        QFont f = title->font();
        f.setBold(i == m_step);
        title->setFont(f);
        if (i < m_step) {
            number->setText(QString(QChar(0x2713)));   // check mark
            number->setStyleSheet(QString("border-radius: 12px; background: %1; color: %2;").arg(goodColor(this), darkTheme(this) ? "#10241a" : "#ffffff"));
            title->setStyleSheet(QString());
        } else if (i == m_step) {
            number->setText(QString::number(i + 1));
            number->setStyleSheet(QString("border-radius: 12px; background: %1; color: %2;").arg(highlight, onHighlight));
            title->setStyleSheet(QString("color: %1;").arg(highlight));
        } else {
            number->setText(QString::number(i + 1));
            number->setStyleSheet(QString("border-radius: 12px; border: 1px solid %1; color: %1;").arg(muted));
            title->setStyleSheet(QString("color: %1;").arg(muted));
        }
    }
}

void OximeterImport::updateFooter()
{
    QWidget *page = ui->stackedWidget->currentWidget();
    m_backButton->setVisible(m_step != DeviceStep && page != m_summaryPage);
    m_nextButton->setEnabled(true);
    if (page == ui->syncPage) {
        m_nextButton->setText(tr("Save"));
    } else if (page == m_summaryPage) {
        m_nextButton->setText(tr("Finish"));
    } else if (page == m_methodPage && method() == FilesMethod) {
        m_nextButton->setText(tr("Choose files..."));
    } else if (page == m_methodPage) {
        m_nextButton->setText(tr("Connect"));
        m_nextButton->setEnabled(method() != NoMethod);
    } else {
        m_nextButton->setText(tr("Next"));
    }
    m_cancelButton->setVisible(page != m_summaryPage);
}

void OximeterImport::showAbout()
{
    QMessageBox::information(this, tr("About oximetry in OSCAR"),
        "<p>" + tr("Pulse Oximeters are medical devices used to measure blood oxygen saturation. During extended Apnea events and abnormal breathing patterns, blood oxygen saturation levels can drop significantly, and can indicate issues that need medical attention.") + "</p>"
        "<p>" + tr("OSCAR gives you the ability to track Oximetry data alongside CPAP session data, which can give valuable insight into the effectiveness of CPAP treatment. It will also work standalone with your Pulse Oximeter, allowing you to store, track and review your recorded data.") + "</p>"
        "<p><b>" + tr("Please remember:") + "</b> <i>"
        + tr("If you are trying to sync oximetry and CPAP data, please make sure you imported your CPAP sessions first before proceeding!") + "</i></p>"
        "<p>" + tr("Even for devices with an internal clock, it is still recommended to get into the habit of starting oximeter records at the same time as CPAP sessions, because CPAP internal clocks tend to drift over time, and not all can be reset easily.") + "</p>");
}

// ----------------------------------------------------------------------------------------
// Step 1: the oximeter

QWidget *OximeterImport::buildDevicePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 24, 32, 16);
    layout->setSpacing(16);

    auto *heading = new QLabel(tr("Which oximeter do you have?"), page);
    QFont hf = heading->font();
    hf.setPointSizeF(hf.pointSizeF() * 1.6);
    hf.setBold(true);
    heading->setFont(hf);
    auto *subtitle = new QLabel(tr("Choose your model. OSCAR remembers it for next time."), page);
    layout->addWidget(heading);
    layout->addWidget(subtitle);

    struct Card { int type; QString picture; QString models; QString methods; };
    const QList<Card> cards = {
        { OXI_VIATOM, ":/icons/wellue.png", tr("O2Ring, O2Ring S, SleepU, Checkme O2, Wearable O2"),
          tr("USB cable · Bluetooth · Data files") },
        { OXI_EMAY, QStringLiteral("text:EMAY"), tr("EMO-80 (Bluetooth sleep oximeter)"),
          tr("Bluetooth") },
        { OXI_CMS50F37, ":/icons/cms50f.png", tr("CMS50F v3.7+, CMS50H, CMS50I, CMS50D+ v4.6, Pulox PO-400/500"),
          tr("Data files") },
        { OXI_CMS50, ":/icons/oximeter.png", tr("CMS50D+, CMS50E, CMS50F, Pulox PO-200/300"),
          tr("Data files") },
        { OXI_MD300W1, QString(), tr("Data files (.dat) saved by its own software"), tr("Data files") },
    };

    m_devices = new QButtonGroup(this);
    auto *grid = new QHBoxLayout;
    grid->setSpacing(14);
    for (const Card &c : cards) {
        auto *card = new QPushButton(page);
        card->setObjectName("wizardCard");
        card->setCheckable(true);
        card->setMinimumSize(165, 300);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        card->setAccessibleName(deviceName(c.type));
        auto *v = new QVBoxLayout(card);
        v->setContentsMargins(14, 12, 14, 14);
        v->setSpacing(8);

        auto *badgeRow = new QHBoxLayout;
        badgeRow->addStretch();
        QLabel *badge = passiveLabel(QString(), card);
        badge->setWordWrap(false);
        badgeRow->addWidget(badge);
        v->addLayout(badgeRow);
        if (c.type == OXI_VIATOM) {
            m_cableBadge = badge;
        }

        QLabel *picture = passiveLabel(QString(), card);
        picture->setAlignment(Qt::AlignCenter);
        picture->setMinimumHeight(118);
        if (c.picture.startsWith(QLatin1String("text:"))) {
            // No picture to hand: the brand name, large.
            picture->setText(c.picture.mid(5));
            QFont pf = picture->font();
            pf.setBold(true);
            pf.setPointSizeF(pf.pointSizeF() * 2.6);
            picture->setFont(pf);
        } else {
            picture->setPixmap(c.picture.isEmpty()
                ? style()->standardIcon(QStyle::SP_FileIcon).pixmap(64, 64)
                : devicePicture(c.picture, QSize(140, 112), this));
        }
        v->addWidget(picture);

        QLabel *name = passiveLabel(deviceName(c.type), card);
        QFont nf = name->font();
        nf.setBold(true);
        nf.setPointSizeF(nf.pointSizeF() * 1.15);
        name->setFont(nf);
        v->addWidget(name);
        QLabel *models = passiveLabel(c.models, card);
        models->setForegroundRole(QPalette::PlaceholderText);
        v->addWidget(models);
        v->addStretch();
        QLabel *methods = passiveLabel(c.methods, card);
        QFont mf = methods->font();
        mf.setPointSizeF(mf.pointSizeF() * 0.92);
        methods->setFont(mf);
        v->addWidget(methods);

        m_devices->addButton(card, c.type);
        grid->addWidget(card, 1);
    }
    layout->addLayout(grid);

    // Remembered at once, so the next wizard starts on it.
    connect(m_devices, &QButtonGroup::idClicked, this, [this](int type) {
        p_profile->oxi->setOximeterType(type);
        updateStepBar();
    });

    layout->addWidget(new QLabel(tr("Not sure which one? Plug in your oximeter's cable: a Viatom/Wellue cable is marked when OSCAR finds it."), page));
    layout->addStretch();
    return page;
}

void OximeterImport::updateCableBadge()
{
    if (!m_cableBadge) {
        return;
    }
    const bool found = viatomUsbBuilt() && !ViatomBle::Usb::findPorts().isEmpty();
    m_cableBadge->setText(found ? QString(QChar(0x2713)) + QLatin1Char(' ') + tr("Cable found") : QString());
    m_cableBadge->setStyleSheet(found ? QString("color: %1; font-weight: bold;").arg(goodColor(this)) : QString());
}

// ----------------------------------------------------------------------------------------
// Step 2: how to import

QWidget *OximeterImport::buildMethodPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 24, 32, 16);
    layout->setSpacing(12);

    auto *heading = new QLabel(tr("How do you want to import?"), page);
    QFont hf = heading->font();
    hf.setPointSizeF(hf.pointSizeF() * 1.6);
    hf.setBold(true);
    heading->setFont(hf);
    m_methodSubtitle = new QLabel(page);
    layout->addWidget(heading);
    layout->addWidget(m_methodSubtitle);

    m_methods = new QButtonGroup(this);
    auto option = [this, page, layout](Method id, const QString &title, QLabel **detailOut, QLabel **noteOut) {
        auto *button = new QPushButton(page);
        button->setObjectName("wizardCard");
        button->setCheckable(true);
        button->setAccessibleName(title);
        auto *v = new QVBoxLayout(button);
        v->setContentsMargins(16, 12, 16, 12);
        v->setSpacing(4);
        QLabel *t = passiveLabel(title, button);
        QFont tf = t->font();
        tf.setBold(true);
        tf.setPointSizeF(tf.pointSizeF() * 1.1);
        t->setFont(tf);
        QLabel *detail = passiveLabel(QString(), button);
        QLabel *note = passiveLabel(QString(), button);
        v->addWidget(t);
        v->addWidget(detail);
        v->addWidget(note);
        // A button doesn't size itself to labels laid out on it: room for the title, two
        // lines of detail and a note.
        button->setMinimumHeight(QFontMetrics(tf).lineSpacing() + 3 * button->fontMetrics().lineSpacing() + 32);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_methods->addButton(button, id);
        layout->addWidget(button);
        if (detailOut) *detailOut = detail;
        if (noteOut) *noteOut = note;
        return button;
    };

    QLabel *usbDetail = nullptr;
    m_usbOption = option(UsbMethod, tr("Through the USB cable"), &usbDetail, &m_usbNote);
    usbDetail->setText(tr("Use the cable that came with the oximeter. Close O2 Insight Pro first."));
    m_bluetoothOption = option(BluetoothMethod, tr("Over Bluetooth"), &m_bluetoothDetail, &m_bluetoothNote);
    m_filesOption = option(FilesMethod, tr("From data files"), &m_filesDetail, nullptr);

    m_deviceHelp = new QLabel(page);
    m_deviceHelp->setWordWrap(true);
    m_deviceHelp->setForegroundRole(QPalette::PlaceholderText);
    layout->addSpacing(6);
    layout->addWidget(m_deviceHelp);
    layout->addStretch();

    connect(m_methods, &QButtonGroup::idClicked, this, [this]() {
        updateStepBar();
        updateFooter();
    });
    return page;
}

void OximeterImport::refreshMethods()
{
    const int type = oximeterType();
    const bool viatom = (type == OXI_VIATOM);
    const bool emay = (type == OXI_EMAY);
    m_methodSubtitle->setText(tr("From your %1 oximeter.").arg(deviceName(type)));

    m_usbOption->setVisible(viatom);
    m_bluetoothOption->setVisible(viatom || emay);

    const QString good = QString("color: %1; font-weight: bold;").arg(goodColor(this));
    const QString warn = QString("color: %1; font-weight: bold;").arg(warnColor(this));

    // USB cable
    bool usbOk = viatomUsbBuilt();
    const bool cable = usbOk && !ViatomBle::Usb::findPorts().isEmpty();
    m_usbOption->setEnabled(usbOk);
    m_usbNote->setText(!usbOk ? tr("This version of OSCAR can't read the oximeter's USB cable.")
                       : cable ? tr("Cable found on USB.")
                               : tr("No cable found yet. Plug it in; OSCAR waits for it on the next page."));
    m_usbNote->setStyleSheet(!usbOk ? warn : cable ? good : QString());

    // Bluetooth
    const ViatomBle::Availability ble = ViatomBle::availability();
    const bool bleOk = (ble == ViatomBle::Availability::Available);
    m_bluetoothOption->setEnabled(bleOk);
    m_bluetoothNote->setText(bleOk ? tr("Bluetooth is ready.") : ViatomBle::availabilityReason(ble) + QLatin1Char('.'));
    m_bluetoothNote->setStyleSheet(bleOk ? good : warn);

    // Data files and help for the device
    switch (type) {
    case OXI_VIATOM:
        m_bluetoothDetail->setText(tr("Put the oximeter in standby and close the ViHealth app on your phone. The oximeter "
                                      "switches itself off after about two minutes without a connection."));
        m_filesDetail->setText(tr("Files copied from the oximeter, or exported from the ViHealth app."));
        m_deviceHelp->setText(tr("Recordings downloaded over the cable or Bluetooth are kept in your profile's "
                                 "Oximetry_Data folder, so they can be imported again later. OSCAR can set the "
                                 "oximeter's clock when it connects, which keeps oximetry in step with CPAP data."));
        break;
    case OXI_EMAY:
        m_bluetoothDetail->setText(tr("Switch the oximeter on and close EMAY's app on your phone."));
        m_filesDetail->setText(tr("Recordings OSCAR downloaded earlier (.spo2), in your profile's Oximetry_Data folder."));
        m_deviceHelp->setText(tr("OSCAR can set the oximeter's clock when it connects, and never deletes "
                                 "recordings from it. EMAY support was written from EMAY's app and hasn't been "
                                 "tried on a device yet: please report how it goes."));
        break;
    case OXI_MD300W1:
        m_filesDetail->setText(tr("MedView .dat files saved by the ChoiceMMed software."));
        m_deviceHelp->setText(tr("If you want to match the recording to CPAP data, import your CPAP data first."));
        break;
    default:
        m_filesDetail->setText(tr("Files saved by SpO2Review or a similar program (.spo, .spor, .spo2)."));
        m_deviceHelp->setText(tr("Contec CMS50D+ devices do not have an internal clock, and do not record a starting time. If you do not have a CPAP session to link a recording to, you will have to enter the start time manually after the import process is completed."));
        break;
    }

    // Keep a usable choice: the cable if it is there, else Bluetooth, else files.
    QAbstractButton *checked = m_methods->checkedButton();
    if (!checked || !checked->isVisible() || !checked->isEnabled()) {
        QPushButton *pick = emay ? (bleOk ? m_bluetoothOption : m_filesOption)
                          : !viatom ? m_filesOption
                          : cable ? m_usbOption
                          : bleOk ? m_bluetoothOption
                          : usbOk ? m_usbOption : m_filesOption;
        pick->setChecked(true);
    }
}

// ----------------------------------------------------------------------------------------
// Navigation

void OximeterImport::goNext()
{
    QWidget *page = ui->stackedWidget->currentWidget();
    if (page == m_devicePage) {
        p_profile->oxi->setOximeterType(oximeterType());
        refreshMethods();
        showPage(m_methodPage, MethodStep);
    } else if (page == m_methodPage) {
        startImport();
    } else if (page == m_viatomPage) {
#ifdef OSCAR_HAVE_BLUETOOTH
        if (!m_viatomPanel->stopForClose()) {
            return;
        }
        const int n = m_viatomPanel->importedCount();
        showSummary(n > 0 ? tr("%n recording(s) imported into OSCAR.", "", n)
                          : tr("No recordings were imported."));
#endif
    } else if (page == ui->chooseSessionPage) {
        QTableWidgetItem * item_0 = ui->tableOxiSessions->item(ui->tableOxiSessions->currentRow(), 0);
        if (!item_0) return;
        oximodule->setStartTime(item_0->data(Qt::UserRole).toDateTime());
        showSyncPage();
    } else if (page == ui->syncPage) {
        saveSession();
    } else if (page == m_summaryPage) {
        accept();
    }
}

void OximeterImport::goBack()
{
    QWidget *page = ui->stackedWidget->currentWidget();
    if (page == m_methodPage) {
        showPage(m_devicePage, DeviceStep);
    } else if (page == m_viatomPage) {
#ifdef OSCAR_HAVE_BLUETOOTH
        if (!m_viatomPanel->stopForClose()) {
            return;
        }
#endif
        refreshMethods();
        showPage(m_methodPage, MethodStep);
    } else if (page == ui->chooseSessionPage) {
        refreshMethods();
        showPage(m_methodPage, MethodStep);
    } else if (page == ui->syncPage) {
        if (oximodule && oximodule->oxisessions.size() > 1) {
            showPage(ui->chooseSessionPage, ImportStep);
        } else {
            refreshMethods();
            showPage(m_methodPage, MethodStep);
        }
    }
}

void OximeterImport::reject()
{
#ifdef OSCAR_HAVE_BLUETOOTH
    if (m_viatomPanel && ui->stackedWidget->currentWidget() == m_viatomPage && !m_viatomPanel->stopForClose()) {
        return;
    }
#endif
    qDebug() << "oximod - Cancel button clicked";
    QDialog::reject();
}

// ----------------------------------------------------------------------------------------
// Step 3: import

void OximeterImport::startImport()
{
    const int type = oximeterType();
    if (!downloadsInPanel(type) || (type == OXI_EMAY && method() == FilesMethod)) {
        importOximeterFile();
        return;
    }
    if (method() == FilesMethod) {
        // Chooses and imports the files, with its own progress and messages.
        if (m_importViatomFiles && m_importViatomFiles()) {
            m_importedFiles = true;
            showSummary(tr("The Viatom/Wellue data files were imported into OSCAR."));
        }
        return;
    }
#ifdef OSCAR_HAVE_BLUETOOTH
    if (!m_viatomPanel) {
        // A downloaded Viatom file goes to the Viatom loader, an EMAY recording (.spo2)
        // straight into a session with its own start time.
        auto importFile = [this](const QString &path) {
            if (path.endsWith(QLatin1String(".spo2"), Qt::CaseInsensitive)) {
                return importRecordingFile(path);
            }
            return m_importViatomFile && m_importViatomFile(path);
        };
        m_viatomPanel = new ViatomImportPanel(importFile, this);
        m_viatomPage = new QWidget(this);
        auto *v = new QVBoxLayout(m_viatomPage);
        v->setContentsMargins(32, 20, 32, 12);
        v->addWidget(m_viatomPanel);
        ui->stackedWidget->addWidget(m_viatomPage);
    }
    if (type == OXI_EMAY) {
        m_viatomPanel->setPreferredFamilies({ ViatomBle::DeviceFamily::Emay });
    } else {
        m_viatomPanel->setPreferredFamilies({ ViatomBle::DeviceFamily::Classic, ViatomBle::DeviceFamily::O2RingS });
    }
    showPage(m_viatomPage, ImportStep);
    if (method() == UsbMethod) {
        m_viatomPanel->startWithUsb();
    } else {
        m_viatomPanel->startScan();
    }
#endif
}

void OximeterImport::importOximeterFile()
{
    QString documentsFolder = (*p_profile)[STR_PREF_LastOximetryPath].toString();
    if (documentsFolder.isEmpty())
        documentsFolder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    if (oximeterType() == OXI_EMAY) {
        // Recordings OSCAR downloaded from an EMAY oximeter are kept with the profile.
        const QString saved = QDir(p_profile->Get("{" + STR_GEN_DataFolder + "}")).filePath(QStringLiteral("Oximetry_Data"));
        if (QDir(saved).exists()) {
            documentsFolder = saved;
        }
    }
    const QString filter = oximeterType() == OXI_MD300W1 ? tr("Oximetry Files (*.dat)")
                         : oximeterType() == OXI_EMAY    ? tr("Recordings OSCAR downloaded (*.spo2)")
                                                         : tr("Oximetry Files (*.spo *.spor *.spo2 *.SpO2 *.dat)");
    QString filename = QFileDialog::getOpenFileName(this, tr("Select a valid oximetry data file"), documentsFolder,
                                                    filter, nullptr, nativeDialogOption());
    if (filename.isEmpty())
        return;

    qDebug() << "oximod - Chosen filename is " << filename;

    oximodule = nullptr;
    for (SerialOximeter * loader : GetOxiLoaders()) {
        if (loader && loader->Open(filename)) { // there is a null entry in loaders. so ths check is necessary
            oximodule = loader;
            break;
        }
    }
    if (!oximodule) {
        QMessageBox::warning(this, STR_MessageBox_Warning, tr("No Oximetry module could parse the given file:")+QString("\n\n%1").arg(filename), QMessageBox::Ok);
        return;
    }
    qDebug() << "oximod - Using loader " << oximodule->loaderName();

    (*p_profile)[STR_PREF_LastOximetryPath] = QFileInfo(filename).absolutePath();

    if (oximodule->oxisessions.size() > 1) {
        chooseSession();
    } else {
        showSyncPage();
    }
}

void OximeterImport::chooseSession()
{
    qDebug() << "oximod - Oximeter Choose Session called";
    ui->tableOxiSessions->clearContents();
    ui->tableOxiSessions->setRowCount(oximodule->oxisessions.size());

    int row = 0;
    for (auto it = oximodule->oxisessions.begin(); it != oximodule->oxisessions.end(); ++it) {
        const QDateTime & key = it.key();
        QVector<OxiRecord> * oxirec = it.value();
        auto *item = new QTableWidgetItem(QLocale().toString(key, QLocale::ShortFormat));
        item->setData(Qt::UserRole, key);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        ui->tableOxiSessions->setItem(row, 0, item);

        long int duration = oxirec->size() * oximodule->importResolution() / 1000L;
        item = new QTableWidgetItem(QString::asprintf("%02li:%02li:%02li", duration / 3600, (duration / 60) % 60, duration % 60));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        ui->tableOxiSessions->setItem(row, 1, item);

        item = new QTableWidgetItem(tr("Oximeter Session %1").arg(row + 1));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        ui->tableOxiSessions->setItem(row, 2, item);
        row++;
    }
    ui->tableOxiSessions->selectRow(0);
    showPage(ui->chooseSessionPage, ImportStep);
}

// ----------------------------------------------------------------------------------------
// Step 4: match to CPAP, or a summary

QWidget *OximeterImport::buildSummaryPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 24, 32, 16);
    auto *heading = new QLabel(tr("Import finished"), page);
    QFont hf = heading->font();
    hf.setPointSizeF(hf.pointSizeF() * 1.6);
    hf.setBold(true);
    heading->setFont(hf);
    m_summaryText = new QLabel(page);
    m_summaryText->setWordWrap(true);
    layout->addWidget(heading);
    layout->addWidget(m_summaryText);
    layout->addWidget(new QLabel(tr("Downloaded recordings carry their own start time, so there is nothing to match to CPAP data."), page));
    layout->addStretch();
    return page;
}

void OximeterImport::showSummary(const QString &text)
{
    m_summaryText->setText(text);
    showPage(m_summaryPage, FinishStep);
}

void OximeterImport::showSyncPage()
{
    if (oximodule == nullptr) {
        qCritical() << "oximod - OximeterImport::showSyncPage called when oximodule is null";
        return;
    }
    qDebug() << "oximod Start Time is " << oximodule->startTime().toString("yyyy-MMM-dd HH.mm.ss");

    showPage(ui->syncPage, FinishStep);

    QDate first = p_profile->FirstDay();
    QDate last = p_profile->LastDay();
    QDate oxidate = oximodule->startTime().date();

    if ((oxidate >= first) && (oxidate <= last)) {
        ui->calendarWidget->setMinimumDate(first);
        ui->calendarWidget->setMaximumDate(last);
        ui->calendarWidget->setCurrentPage(oxidate.year(), oxidate.month());
        ui->calendarWidget->setSelectedDate(oxidate);
        on_calendarWidget_clicked(oximodule->startTime().date());
    } else {
        // No CPAP sessions to sync it to.. kill the CPAP sync option
        on_calendarWidget_clicked(last);
        ui->calendarWidget->setCurrentPage(oxidate.year(), oxidate.month());
        ui->calendarWidget->setSelectedDate(oxidate);
    }

    ui->radioSyncOximeter->setChecked(true);
    on_radioSyncOximeter_clicked();

    if (!oximodule->isStartTimeValid()) {
        // Oximeter doesn't provide a clock
        ui->labelSyncOximeter->setText(tr("I need to set the time manually, because my oximeter doesn't have an internal clock."));
    }

    suggestCpapSession();
}

void OximeterImport::suggestCpapSession()
{
    m_suggestion->setVisible(false);
    if (!oximodule || !oximodule->oxisessions.contains(oximodule->startTime())) {
        return;
    }
    const qint64 oxStart = oximodule->startTime().toMSecsSinceEpoch();
    const qint64 oxLength = qint64(oximodule->oxisessions[oximodule->startTime()]->size()) * oximodule->importResolution();
    const qint64 oxEnd = oxStart + oxLength;

    // With a clock: the CPAP session on that night that overlaps the recording most.
    // Without one the date means nothing, so: on the last night with CPAP data, the
    // session closest in length to the recording.
    QList<QDate> days;
    if (oximodule->isStartTimeValid()) {
        const QDate d = oximodule->startTime().date();
        days << d.addDays(-1) << d << d.addDays(1);
    } else {
        days << p_profile->LastGoodDay(MT_CPAP);
    }

    Session *best = nullptr;
    QDate bestDay;
    qint64 bestScore = 0;
    for (const QDate &date : days) {
        Day *day = p_profile->GetGoodDay(date, MT_CPAP);
        if (!day) {
            continue;
        }
        for (Session *s : *day) {
            qint64 score;
            if (oximodule->isStartTimeValid()) {
                score = qMin(oxEnd, s->last()) - qMax(oxStart, s->first());   // overlap
            } else {
                score = oxLength - qAbs(s->length() - oxLength);              // closeness in length
            }
            if (score > bestScore) {
                bestScore = score;
                best = s;
                bestDay = date;
            }
        }
    }
    if (!best) {
        qDebug() << "oximod - no CPAP session to suggest";
        return;
    }

    ui->radioSyncCPAP->setChecked(true);
    ui->syncCPAPGroup->setVisible(true);
    ui->calendarWidget->setSelectedDate(bestDay);   // fills the session bar for that day
    on_calendarWidget_clicked(bestDay);
    for (int i = 0; i < sessbar->count(); i++) {
        if (sessbar->session(i) == best) {
            sessbar->setSelected(i);
            sessbar->update();
            break;
        }
    }
    const QDateTime start = QDateTime::fromMSecsSinceEpoch(best->first(), QTimeZone::systemTimeZone());
    ui->dateTimeEdit->setDateTime(start);

    QString why;
    if (oximodule->isStartTimeValid()) {
        why = tr("it overlaps the recording by %1%.").arg(qRound(100.0 * bestScore / qMax<qint64>(1, oxLength)));
    } else {
        why = tr("its length is closest to the recording's, on the last night with CPAP data.");
    }
    m_suggestion->setText(tr("Suggested: the CPAP session starting %1, because %2 Choose another below if it isn't right.")
                              .arg(QLocale().toString(start, QLocale::ShortFormat), why));
    m_suggestion->setStyleSheet(QString("color: %1; font-weight: bold;").arg(goodColor(this)));
    m_suggestion->setVisible(true);
    qDebug() << "oximod - suggested CPAP session" << best->session() << "score" << bestScore;
}

void OximeterImport::on_calendarWidget_clicked(const QDate &date)
{
	qDebug() << "oximod - Calendar widget clicked " << date.toString("yyyy-MMM-dd");
    if (ui->radioSyncCPAP->isChecked()) {
        qDebug() << "oximod - Syncing to a CPAP session";
        Day * day = p_profile->GetGoodDay(date, MT_CPAP);

        sessbar->clear();
        if (day) {
            QDateTime time=QDateTime::fromMSecsSinceEpoch(day->first(), QTimeZone::systemTimeZone());
            sessbar->clear();
            QList<QColor> colors;
            colors.push_back("#ffffe0");
            colors.push_back("#ffe0ff");
            colors.push_back("#e0ffff");
            QList<Session *>::iterator i;
            int j=0;
            for (i=day->begin(); i != day->end(); ++i) {
                sessbar->add((*i),colors.at(j++ % colors.size()));
            }
            sessbar->setVisible(true);
            ui->sessbarLabel->setText(tr("%1 session(s) on %2, starting at %3").arg(day->size()).arg(time.date().toString(QLocale::system().dateFormat(QLocale::LongFormat))).arg(time.time().toString("hh:mm:ssap")));
            sessbar->setSelected(0);
            ui->dateTimeEdit->setDateTime(time);
        } else {
            ui->sessbarLabel->setText(tr("No CPAP data available on %1").arg(date.toString(QLocale::system().dateFormat(QLocale::LongFormat))));
            qDebug() << "oximod - Using oximeter time " << oximodule->startTime().toString("yyyy-MMM-dd hh:mm:ssap") << "on date " << date.toString(QLocale::system().dateFormat(QLocale::LongFormat));
            ui->dateTimeEdit->setDateTime(QDateTime(date,oximodule->startTime().time()));
        }

        sessbar->update();
    } else if (ui->radioSyncOximeter) {
        qDebug() << "oximod - Using oximeter date and time";
        ui->sessbarLabel->setText(QString("%1").arg(date.toString(QLocale::system().dateFormat(QLocale::LongFormat))));
        ui->dateTimeEdit->setDateTime(QDateTime(date, ui->dateTimeEdit->dateTime().time()));
    }
}

void OximeterImport::on_calendarWidget_selectionChanged()
{
    on_calendarWidget_clicked(ui->calendarWidget->selectedDate());
}

void OximeterImport::onSessionSelected(Session * session)
{
    QDateTime time=QDateTime::fromMSecsSinceEpoch(session->first(), QTimeZone::systemTimeZone());
    qDebug() << "oximod - Selected session starts at " << time.toString("yyyy-MMM-dd hh:mm:ssap");
    ui->dateTimeEdit->setDateTime(time);
}

void OximeterImport::on_sessionBackButton_clicked()
{
	qDebug() << "oximod - Session Back button clicked";
    int idx = (sessbar->selected()-1);
    if (idx >= 0) {
        sessbar->setSelected(idx);
        QDateTime datetime = QDateTime::fromMSecsSinceEpoch(sessbar->session(idx)->first(), QTimeZone::systemTimeZone());
        ui->dateTimeEdit->setDateTime(datetime);
        sessbar->update();
    }
}

void OximeterImport::on_sessionForwardButton_clicked()
{
	qDebug() << "oximod - Session Forward button clicked";
    int idx = (sessbar->selected()+1);
    if (idx < sessbar->count()) {
        sessbar->setSelected(idx);
        QDateTime datetime = QDateTime::fromMSecsSinceEpoch(sessbar->session(idx)->first(), QTimeZone::systemTimeZone());
        ui->dateTimeEdit->setDateTime(datetime);
        sessbar->update();
    }
}

void OximeterImport::on_radioSyncCPAP_clicked()
{
	qDebug() << "oximod - Use CPAP Time button clicked";
    on_calendarWidget_clicked(ui->calendarWidget->selectedDate());

    ui->syncCPAPGroup->setVisible(true);
}

void OximeterImport::on_radioSyncOximeter_clicked()
{
	qDebug() << "oximod - Use OximeterTime button clicked";
    ui->syncCPAPGroup->setVisible(false);
    m_suggestion->setVisible(false);
    if ( oximodule ) {
        qDebug() << "oximod - Oximeter time is " << oximodule->startTime().toString("yyyy-MMM-dd HH:mm:ssap")
                 << (oximodule->isStartTimeValid() ? "" : "(no clock)");
        ui->calendarWidget->setSelectedDate(oximodule->startTime().date());
        ui->dateTimeEdit->setDateTime(oximodule->startTime());
    }
}

void OximeterImport::saveSession()
{
	qDebug() << "oximod - Oximeter Save button clicked";
    if (!oximodule) return;

    QVector<OxiRecord> * oxirec = nullptr;

    if (!oximodule->oxisessions.contains(oximodule->startTime())) {
        QMessageBox::warning(this, STR_MessageBox_Error, tr("Something went wrong getting session data"), QMessageBox::Ok);
        reject();
        return;
    }
    oxirec = oximodule->oxisessions[oximodule->startTime()];
    if (oxirec->size() < 10) {
        qDebug() << "oximod - oxirec size less than 10 - quitting";
        return;
    }

    storeSession(oximodule, oxirec, ui->dateTimeEdit->dateTime());

    mainwin->EnableTabs(true);  // somebody has to do it...

    mainwin->getDaily()->LoadDate(mainwin->getDaily()->getDate());
    mainwin->getOverview()->ReloadGraphs();

    m_importedFiles = true;
    oximodule->trashRecords();
    oximodule = nullptr;
    accept();
}

void OximeterImport::storeSession(SerialOximeter *mod, QVector<OxiRecord> *oxirec, const QDateTime &startTime)
{
    MachineInfo info = mod->newInfo();
    Machine * mach = p_profile->CreateMachine(info);
    SessionID sid = startTime.toUTC().toSecsSinceEpoch();
    quint64 start = quint64(sid) * 1000L;

    Session * session = new Session(mach, sid);
    session->really_set_first(start);

    EventList * ELpulse = nullptr;
    EventList * ELspo2 = nullptr;
    EventList * ELperf = nullptr;

    quint16 lastpulse = 0;
    quint16 lastspo2 = 0;
    quint16 lastperf = 0;
    quint16 lastgoodpulse = 0;
    quint16 lastgoodspo2 = 0;
    quint16 lastgoodperf = 0;

    bool haveperf = mod->havePerfIndex();

    quint64 ti = start;


    qint64 step = mod->importResolution();
    int size = oxirec->size();

    // why was I skipping the first sample? not priming it anymore..
    qDebug() << "oximod = Creating event list for pulse and O2 saturation";
    for (int i=0; i < size; ++i) {
        OxiRecord * rec = &(*oxirec)[i];

        if (rec->pulse > 0) {
            if (lastpulse == 0) {
                ELpulse = session->AddEventList(OXI_Pulse, EVL_Event);
            }
            if (lastpulse != rec->pulse) {
                if (lastpulse > 0) {
                    ELpulse->AddEvent(ti, lastpulse);
                }
                ELpulse->AddEvent(ti, rec->pulse);
            }
            lastgoodpulse = rec->pulse;
        } else {
            // end section properly
            if (lastgoodpulse > 0) {
                ELpulse->AddEvent(ti, lastpulse);
                session->setLast(OXI_Pulse, ti);
                lastgoodpulse = 0;
            }
        }

        lastpulse = rec->pulse;

        if (rec->spo2 > 0) {
            if (lastspo2 == 0) {
                ELspo2 = session->AddEventList(OXI_SPO2, EVL_Event);
            }
            if (lastspo2 != rec->spo2) {
                if (lastspo2 > 0) {
                    ELspo2->AddEvent(ti, lastspo2);
                }
                ELspo2->AddEvent(ti, rec->spo2);
            }
            lastgoodspo2 = rec->spo2;
        } else {
            // end section properly
            if (lastgoodspo2 > 0) {
                ELspo2->AddEvent(ti, lastspo2);
                session->setLast(OXI_SPO2, ti);
                lastgoodspo2 = 0;
            }
        }
        lastspo2 = rec->spo2;

        if (haveperf) {
            // Perfusion Index
            if (rec->perf > 0) {
                if (lastperf == 0) {
                    ELperf = session->AddEventList(OXI_Perf, EVL_Event, 0.01f);
                }
                if (lastperf != rec->perf) {
                    if (lastperf > 0) {
                        ELperf->AddEvent(ti, lastperf);
                    }
                    ELperf->AddEvent(ti, rec->perf);
                }
                lastgoodperf = rec->perf;
            } else {
                // end section properly
                if (lastgoodperf > 0) {
                    ELperf->AddEvent(ti, lastperf);
                    session->setLast(OXI_Perf, ti);
                    lastgoodperf = 0;
                }
            }
            lastperf = rec->perf;
        }

        ti += step;
    }
    ti -= step;
    if (ELpulse && (lastpulse > 0)) {
        ELpulse->AddEvent(ti, lastpulse);
        session->setLast(OXI_Pulse, ti);
    }

    if (ELspo2 && (lastspo2 > 0)) {
        ELspo2->AddEvent(ti, lastspo2);
        session->setLast(OXI_SPO2, ti);
    }

    if (haveperf && ELperf && lastperf > 0) {
        ELperf->AddEvent(ti, lastperf);
        session->setLast(OXI_Perf, ti);
    }

    if (haveperf) {
        session->first(OXI_Perf);
        session->last(OXI_Perf);
        session->count(OXI_Perf);
        session->Min(OXI_Perf);
        session->Max(OXI_Perf);
    }

    calcSPO2Drop(session);
    calcPulseChange(session);

    qDebug() << "oximod - Setting up device and session";

    mach->setModel(mod->getModel());
    mach->setBrand(mod->getVendor());

    session->first(OXI_Pulse);
    session->first(OXI_SPO2);
    session->last(OXI_Pulse);
    session->last(OXI_SPO2);

    session->first(OXI_PulseChange);
    session->first(OXI_SPO2Drop);
    session->last(OXI_PulseChange);
    session->last(OXI_SPO2Drop);

    session->cph(OXI_PulseChange);
    session->sph(OXI_PulseChange);

    session->cph(OXI_SPO2Drop);
    session->sph(OXI_SPO2Drop);

    session->count(OXI_Pulse);
    session->count(OXI_SPO2);
    session->count(OXI_PulseChange);
    session->count(OXI_SPO2Drop);
    session->Min(OXI_Pulse);
    session->Min(OXI_SPO2);
    session->Max(OXI_Pulse);
    session->Max(OXI_SPO2);
    // avg/wavg are needed so daily_summaries.spo2_avg and pulse_avg are correct
    session->avg(OXI_Pulse);
    session->avg(OXI_SPO2);
    session->wavg(OXI_Pulse);
    session->wavg(OXI_SPO2);

    session->really_set_last(ti);
    session->SetChanged(true);

    session->setOpened(true);

    qDebug() << "oximod - Adding session to device";
    mach->AddSession(session);
    qDebug() << "oximod - Saving device";
    mach->Save();
    p_profile->StoreMachines();
    p_profile->calculateDailySummaries();
}

bool OximeterImport::importRecordingFile(const QString &path)
{
    // A recording downloaded with its start time, e.g. from an EMAY EMO-80 (.spo2): read
    // by the oximetry file readers, then stored at that time without the CPAP-sync step.
    SerialOximeter *mod = nullptr;
    for (SerialOximeter *loader : GetOxiLoaders()) {
        if (loader && loader->Open(path)) {
            mod = loader;
            break;
        }
    }
    if (!mod) {
        qWarning().noquote() << "oximod - no reader for" << path;
        return false;
    }
    const QDateTime start = mod->startTime();
    QVector<OxiRecord> *oxirec = mod->oxisessions.value(start, nullptr);
    if (!oxirec || oxirec->size() < 10 || !start.isValid()) {
        qWarning().noquote() << "oximod -" << path << "has no usable recording";
        mod->trashRecords();
        return false;
    }
    Machine *mach = p_profile->CreateMachine(mod->newInfo());
    const SessionID sid = start.toUTC().toSecsSinceEpoch();
    if (mach && mach->SessionExists(sid)) {
        qDebug().noquote() << "oximod -" << path << "is already in OSCAR";
        mod->trashRecords();
        return true;
    }
    storeSession(mod, oxirec, start);
    mod->trashRecords();
    return true;
}
