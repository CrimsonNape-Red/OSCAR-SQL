/* SleepLib Viatom Loader Implementation
 *
 * Copyright (c) 2020-2026 The OSCAR Team
 * (Initial importer written by dave madden <dhm@mersenne.com>)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

//********************************************************************************************
// Please only INCREMENT the viatom_data_version in viatom_loader.h when making changes
// that change loader behaviour or modify channels in a manner that fixes old data imports.
// Note that changing the data version will require a reimport of existing data for which OSCAR
// does not keep a backup - so it should be avoided if possible.
// i.e. there is no need to change the version when adding support for new devices
//********************************************************************************************

#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <QApplication>
#include <QMessageBox>
#include "viatom_loader.h"
#include "SleepLib/machine.h"
#include "SleepLib/session.h"
#include <memory>
#include <QTimeZone>
#include "mainwindow.h"

extern MainWindow *mainwin;

// TODO: Merge this with PRS1 macros and generalize for all loaders.
#define SESSIONID m_session->session()
#define UNEXPECTED_VALUE(SRC, VALS) { \
    QString message = QString("%1:%2: %3 = %4 != %5").arg(__func__).arg(__LINE__).arg(#SRC).arg(SRC).arg(VALS); \
    qWarning() << SESSIONID << message; \
    s_unexpectedMessages += message; \
    }
#define CHECK_VALUE(SRC, VAL) if ((SRC) != (VAL)) UNEXPECTED_VALUE(SRC, VAL)
#define CHECK_VALUES(SRC, VAL1, VAL2) if ((SRC) != (VAL1) && (SRC) != (VAL2)) UNEXPECTED_VALUE(SRC, #VAL1 " or " #VAL2)
// for more than 2 values, just write the test manually and use UNEXPECTED_VALUE if it fails
static QSet<QString> s_unexpectedMessages;

bool
ViatomLoader::Detect(const QString & path)
{
    // This is only used for CPAP devices, when detecting CPAP cards.
    qDebug() << "ViatomLoader::Detect(" << path << ")";
    return false;
}

int
ViatomLoader::Open(const QStringList & paths)
{
    qDebug() << "ViatomLoader::Open(" << paths.join("; ") << ")";
    m_mach = nullptr;
    int imported = 0;
    int found = 0;
    s_unexpectedMessages.clear();

    int size = paths.size();
    for (int i=0; i < size; i++) {
        if (isAborted()) {
            break;
        }
        // This filename has already been filtered by QFileDialog.
        int ok = OpenFile(paths[i]);
        if (ok > 0) {
            imported++;
        } else if (ok < 0) {
            // Stop on error...
            break;
        }
        found++;
        emit setProgressValue(i+1);
        QCoreApplication::processEvents();
    }

    if (!found) {
        return -1;
    }

    Machine* mach = m_mach;
    if (imported && mach == nullptr) qWarning() << "No device record created?";
    if (mach) {
        qDebug() << "Imported" << imported << "sessions";
        mach->Save();
        p_profile->StoreMachines();
    }
    if (mach && s_unexpectedMessages.count() > 0 && p_profile->session->warnOnUnexpectedData()) {
        // Compare this to the list of messages previously seen for this device
        // and only alert if there are new ones.
        QSet<QString> newMessages = s_unexpectedMessages - mach->previouslySeenUnexpectedData();
        if (newMessages.count() > 0) {
            // TODO: Rework the importer call structure so that this can become an
            // emit statement to the appropriate import job.
            if (mainwin && mainwin->isBatchMode()) {
                qWarning() << "ViatomLoader: Untested data from device:" << newMessages;
            } else {
                QMessageBox::information(QApplication::activeWindow(),
                                         QObject::tr("Untested Data"),
                                         QObject::tr("Your Viatom device generated data that OSCAR has never seen before.") +"\n\n"+
                                         QObject::tr("The imported data may not be entirely accurate, so the developers would like a copy of your Viatom files to make sure OSCAR is handling the data correctly.")
                                         ,QMessageBox::Ok);
            }
            mach->previouslySeenUnexpectedData() += newMessages;
        }
    }

    return found;
}

int ViatomLoader::OpenFile(const QString & filename)
{
    Machine* mach = nullptr;
    bool existing = false;

    Session* sess = ParseFile(filename, &existing);
    if (sess) {
        SaveSessionToDatabase(sess);
        mach = sess->machine();
        m_mach = mach;
        return 1;
    }

    return existing ? 0 : -1; // -1 = error
}



bool IsPOD2Structure(const QString& filePath) {
    //POD2 files consist of data only.
    //There is no metadata in the file.
    //To recognize the POD2 data, we have to look for the proper directory structure
    //The directory name 28 is the model (or some other ID for the Wellue POD2)
    //The data are in the host directory.
    //When copying the data from the Android phone to the PC, this structure must be maintained.
    auto path = QDir::cleanPath(filePath);
    if (!QFileInfo(path).path().endsWith("/28/host")){
        return false;
    }
   return true;
}



Session* ViatomLoader::ParseFilePOD2(const QString & filename, bool *existing)
{
    if (existing) {
        *existing = false;
    }
    qDebug() << "Found Viatom POD 2 data file " << filename;
    QFile file(filename);
    if (!file.open(QFile::ReadOnly)) {
        qDebug() << "Couldn't open Viatom data file" << filename;
        return nullptr;
    }

    MachineInfo info = newInfo();
    //POD2 datafiles don't have a serial number.
    //Import all POD2 data with the same fake "serial number."
    info.serial = "POD2";

    std::unique_ptr<ViatomPOD2File> v = std::unique_ptr<ViatomPOD2File>(new ViatomPOD2File(file));

    if (v->ParseHeader() == false) {
        return nullptr;
    }

    Machine *mach = p_profile->CreateMachine(info);
    if (mach->SessionExists(v->sessionid())) {
        // Skip already imported session
        if (existing) {
            // Inform the caller (if they are interested) that this session was already imported
            *existing = true;
        }
        return nullptr;
    }

    qint64 time_ms = v->timestamp();
    m_session = new Session(mach, v->sessionid());
    m_session->set_first(time_ms);
    bool batterydead = false;
    unsigned char batterymask = 0b11000000;

    QList<ViatomPOD2File::Record> records = v->ReadData();
    m_step = 1000L;
    // Import data
    for (auto & rec : records)
    {
        //I do not know if the POD 2 has the same limits as the other Viatom devices.
        //Lacking better information, I've kept the limit checks from the other Viatom devices.
        //In testing, there were some streaks of 0 values for SPO2 and HR
        //These seem to be invalid measurements.
        // Viatom advertises a range of 30 - 250 bpm.
        if (rec.hr>0 && (rec.hr < 30 || rec.hr > 250)) {
            UNEXPECTED_VALUE(rec.hr, "30-250");
        }
        else{
            if (rec.hr>0){
                AddEvent(OXI_Pulse, time_ms, rec.hr);
            }
        }


        if (rec.spo2 == 0xFF) {
            //From the original Viatom.  May not apply to the Wellue
            // When the readings fall below 61%, Viatom devices record 0xFF for SpO2.
            // The official software discards these readings.
            // TODO: Consider whether to import these as 60% since they reflect hypoxia.
            EndEventList(OXI_SPO2, time_ms);
        } else {
            // Viatom advertises (and graphs) a range of 70% - 99%, but apparently records down to 61%.
            // The official software graphs 61%-70% as 70%.
            // TODO: Consider whether we should import 61%-70% as 70% to match the official reports.
            if (rec.spo2>0 && (rec.spo2 < 61 || rec.spo2 > 100)) {
                UNEXPECTED_VALUE(rec.spo2, "61-100%");
            }
            else
            {
                if (rec.spo2>0)
                {
                    AddEvent(OXI_SPO2, time_ms, rec.spo2);
                }
            }

        }
        float pi = (float)rec.pi / 10;
        if (pi>24)
        {
            //The POD2 manual says the perfusion index ranges from 0 to 20
            //The logged values range from 0 to 200.  2% PI = value of 20
            //The logged values are 10 times the displayed value.
            //2025-08-31: Further experience with the POD2 shows that it will occasionally log
            //perfusion index values above 20%.
            //Changed to throw a warning at 24% instead of 20%

             UNEXPECTED_VALUE(pi, "0-24%");
        }
        else
        {
            AddEvent(OXI_Perf, time_ms, pi);
        }

        time_ms += m_step;
        if ((rec.battery&batterymask) == 0)
        {
            batterydead = true;
        }
    }
    EndEventList(OXI_Pulse, time_ms);
    EndEventList(OXI_SPO2, time_ms);
    EndEventList(OXI_Perf, time_ms);
    m_session->set_last(time_ms);
    if (batterydead)
    {
        QString message = QString(QObject::tr("The imported data for the session starting %1 may be incomplete or incorrect."));
        message=message.arg(QDateTime::fromMSecsSinceEpoch(m_session->first(),QTimeZone::systemTimeZone()).toString());
        if (mainwin && mainwin->isBatchMode()) {
            qWarning().noquote() << "ViatomLoader: Dead battery detected during session starting"
                                 << QDateTime::fromMSecsSinceEpoch(m_session->first(), QTimeZone::systemTimeZone()).toString();
        } else {
            QMessageBox::warning(QApplication::activeWindow(),
                                 QObject::tr("Dead battery"),
                                 QObject::tr("Your Viatom device registered a dead battery during operation.") +"\n\n"+
                                 message,
                                 QMessageBox::Ok);
        }
    }


    return m_session;
}

Session* ViatomLoader::ParseFileViatom(const QString & filename, bool *existing)
{
    if (existing) {
        *existing = false;
    }
    QFile file(filename);
    if (!file.open(QFile::ReadOnly)) {
        qDebug() << "Couldn't open Viatom data file" << filename;
        return nullptr;
    }

    // Read in Viatom database version number
    QByteArray data = file.read(2);
    if (data.size() < 2) {
        qDebug() << filename << "too short for a Viatom data file";
        return nullptr;
    }
    const unsigned char* header = (const unsigned char*) data.constData();
    int sig = header[0] | (header[1] << 8);
    std::unique_ptr<ViatomFile> v = nullptr;
    switch (sig) {
    case 0x0003:
    case 0x0005:
    case 0x0006:
        v = std::unique_ptr<ViatomFile>(new ViatomFile(file));
        break;
    case 0x0301:
        v = std::unique_ptr<O2RingS>(new O2RingS(file));
        break;
    default:
        qDebug() << filename << "Unrecognized DB version number in Viatom data file" << sig;
        return nullptr;
    }

    if (!file.seek(0)) {
        qDebug() << filename << "unable to seek to begining of file";
        return nullptr;
    }

    // Parse header specific to database version number
    if (v->ParseHeader() == false) {
        return nullptr;
    }

    MachineInfo info = newInfo();
    // Check whether the enclosing folder looks like a Viatom serial number, and if so, use it.
    QString foldername = QFileInfo(filename).dir().dirName();
    if (foldername.length() >= 9) {
        bool numeric;
        #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            foldername.rightRef(4).toInt(&numeric);
        #else
            foldername.right(4).toInt(&numeric);
        #endif

        if (numeric) {
            info.serial = foldername;
        }
    }
    Machine *mach = p_profile->CreateMachine(info);

    if (mach->SessionExists(v->sessionid())) {
        // Skip already imported session
        if (existing) {
            // Inform the caller (if they are interested) that this session was already imported
            *existing = true;
        }
        return nullptr;
    }

    qint64 time_ms = v->timestamp();
    m_session = new Session(mach, v->sessionid());
    m_session->set_first(time_ms);

    const ViatomFile::Summary & summary = v->summary();
    if (summary.valid) {
        m_session->settings[Viatom_O2Score] = summary.o2score / 10.0;
        m_session->settings[Viatom_Drops3] = summary.drops3;
        m_session->settings[Viatom_Drops4] = summary.drops4;
        m_session->settings[Viatom_TimeBelow90] = summary.time_below_90;
        m_session->settings[Viatom_AsleepTime] = summary.asleep_time;
        m_session->settings[Viatom_Steps] = summary.steps;
    }

    QList<ViatomFile::Record> records = v->ReadData();
    m_step = v->duration() / records.size() * 1000L;

    // Import data
    for (auto & rec : records) {
        if (rec.oximetry_invalid) {
            EndEventList(OXI_Pulse, time_ms);
            EndEventList(OXI_SPO2, time_ms);
        } else {
            // Viatom advertises a range of 30 - 250 bpm.
            if (rec.hr < 30 || rec.hr > 250) {
                UNEXPECTED_VALUE(rec.hr, "30-250");
            }
            AddEvent(OXI_Pulse, time_ms, rec.hr);

            if (rec.spo2 == 0xFF) {
                // When the readings fall below 61%, Viatom devices record 0xFF for SpO2.
                // The official software discards these readings.
                // TODO: Consider whether to import these as 60% since they reflect hypoxia.
                EndEventList(OXI_SPO2, time_ms);
            } else {
                // Viatom advertises (and graphs) a range of 70% - 99%, but apparently records down to 61%.
                // The official software graphs 61%-70% as 70%.
                // TODO: Consider whether we should import 61%-70% as 70% to match the official reports.
                // Some devices (e.g. OsRing_S) can record 100%, so the upper limit is 100.
                if (rec.spo2 < 61 || rec.spo2 > 100) {
                    UNEXPECTED_VALUE(rec.spo2, "61-100%");
                }
                AddEvent(OXI_SPO2, time_ms, rec.spo2);
            }
        }
        AddEvent(POS_Movement, time_ms, rec.motion);
        time_ms += m_step;
    }
    EndEventList(OXI_Pulse, time_ms);
    EndEventList(OXI_SPO2, time_ms);
    EndEventList(POS_Movement, time_ms);
    m_session->set_last(time_ms);

    return m_session;
}

Session* ViatomLoader::ParseFile(const QString & filename, bool *existing)
{
    if (IsPOD2Structure(filename)){
        return ParseFilePOD2(filename, existing);
    }
    else{
        return ParseFileViatom(filename, existing);
    }
}

void ViatomLoader::SaveSessionToDatabase(Session* sess)
{
    Machine* mach = sess->machine();

    sess->SetChanged(true);
    mach->AddSession(sess);
}

void ViatomLoader::AddEvent(ChannelID channel, qint64 t, EventDataType value)
{
    EventList* C = m_importChannels[channel];
    if (C == nullptr) {
        C = m_session->AddEventList(channel, EVL_Waveform, 1.0, 0.0, 0.0, 0.0, m_step);
        Q_ASSERT(C);  // Once upon a time AddEventList could return nullptr, but not any more.
        m_importChannels[channel] = C;
    }
    // Add the event
    C->AddEvent(t, value);
    m_importLastValue[channel] = value;
}

void ViatomLoader::EndEventList(ChannelID channel, qint64 /*t*/)
{
    EventList* C = m_importChannels[channel];
    if (C != nullptr) {
        // The below would be needed for square charts if the first sample represents
        // the 4 seconds following the starting timestamp:

        // Mark this channel's event list as ended.
        m_importChannels[channel] = nullptr;
    }
}

QStringList ViatomLoader::getNameFilter()
{
    // Sometimes the files have a SleepU_ or O2Ring_ prefix.
    // Sometimes they have punctuation in the timestamp.
    // Note that ":" is not allowed on macOS, so Mac users will need to rename their files in order to select and import them.
    // Corrected filename filter for POD2 import. JE
    // Example filename: 1781126514733.dat
    // That's a 13 digit Unix timestamp in milliseconds followed by .dat

    return QStringList({"*20[0-5][0-9][01][0-9][0-3][0-9][012][0-9][0-5][0-9][0-5][0-9]*",
                        "*20[0-5][0-9]-[01][0-9]-[0-3][0-9] [012][0-9]:[0-5][0-9]:[0-5][0-9]*",
                        "[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9].dat"
    });
}

ChannelID Viatom_O2Score = 0, Viatom_Drops3 = 0, Viatom_Drops4 = 0, Viatom_TimeBelow90 = 0,
          Viatom_AsleepTime = 0, Viatom_Steps = 0;

void ViatomLoader::initChannels()
{
    using namespace schema;

    channel.add(GRP_OXI, new Channel(Viatom_O2Score = 0xe5a0, SETTING, MT_OXIMETER, SESSION,
        "Viatom_O2Score", QObject::tr("O2 Score"),
        QObject::tr("Oxygen score calculated by the oximeter (0-10)"),
        QObject::tr("O2 Score"),
        "", DEFAULT, Qt::green));

    channel.add(GRP_OXI, new Channel(Viatom_Drops3 = 0xe5a1, SETTING, MT_OXIMETER, SESSION,
        "Viatom_Drops3", QObject::tr("3% SpO2 drops"),
        QObject::tr("Number of SpO2 drops of 3% or more counted by the oximeter"),
        QObject::tr("3% drops"),
        "", INTEGER, Qt::green));

    channel.add(GRP_OXI, new Channel(Viatom_Drops4 = 0xe5a2, SETTING, MT_OXIMETER, SESSION,
        "Viatom_Drops4", QObject::tr("4% SpO2 drops"),
        QObject::tr("Number of SpO2 drops of 4% or more counted by the oximeter"),
        QObject::tr("4% drops"),
        "", INTEGER, Qt::green));

    channel.add(GRP_OXI, new Channel(Viatom_TimeBelow90 = 0xe5a3, SETTING, MT_OXIMETER, SESSION,
        "Viatom_TimeBelow90", QObject::tr("Time below 90% SpO2"),
        QObject::tr("Time with SpO2 below 90% according to the oximeter"),
        QObject::tr("Time < 90%"),
        STR_UNIT_Seconds, INTEGER, Qt::green));

    channel.add(GRP_OXI, new Channel(Viatom_AsleepTime = 0xe5a4, SETTING, MT_OXIMETER, SESSION,
        "Viatom_AsleepTime", QObject::tr("Asleep time"),
        QObject::tr("Time asleep according to the oximeter"),
        QObject::tr("Asleep"),
        STR_UNIT_Seconds, INTEGER, Qt::green));

    channel.add(GRP_OXI, new Channel(Viatom_Steps = 0xe5a5, SETTING, MT_OXIMETER, SESSION,
        "Viatom_Steps", QObject::tr("Steps"),
        QObject::tr("Steps counted by the oximeter during the recording"),
        QObject::tr("Steps"),
        "", INTEGER, Qt::green));
}

static bool viatom_initialized = false;

void
ViatomLoader::Register()
{
    if (!viatom_initialized) {
        qDebug("Registering ViatomLoader");
        RegisterLoader(new ViatomLoader());
        viatom_initialized = true;
    }
}


// ===============================================================================================

#undef SESSIONID
#define SESSIONID this->m_sessionid

ViatomPOD2File::ViatomPOD2File(QFile & file) : m_file(file)
{
}

QDateTime ViatomPOD2File::getPOD2FilenameTimestamp()
{
    //Filenames are usually just a Unix epoch timestamp.
    QString date_string = QFileInfo(m_file).baseName();
    qint64 msecs = date_string.toLongLong();

    return QDateTime::fromMSecsSinceEpoch(msecs,QTimeZone::systemTimeZone());
}

bool ViatomPOD2File::ParseHeader()
{    
    QDateTime filename_timestamp = getPOD2FilenameTimestamp();
    m_timestamp = filename_timestamp.toMSecsSinceEpoch();
    m_sessionid = m_timestamp / 1000L;

    qint64 datasize = m_file.size();
    m_record_count = datasize / RECORD_SIZE;
    m_resolution = 1100L;
    m_duration   = m_record_count;

    return true;
}

QList<ViatomPOD2File::Record> ViatomPOD2File::ReadData()
{
    QByteArray data = m_file.readAll();
    QDataStream in(data);
    in.setByteOrder(QDataStream::LittleEndian);

    QList<ViatomPOD2File::Record> records;

    // Read all Pulse, SPO2 and PI data
    do {
        ViatomPOD2File::Record rec;
        in >> rec.spo2 >> rec.hr >> rec.unk1 >> rec.pi >> rec.unk2 >> rec.battery;

        records.append(rec);
    } while (records.size() < m_record_count);


    return records;
}

ViatomFile::ViatomFile(QFile & file) : m_file(file)
{
}

QDateTime ViatomFile::getFilenameTimestamp()
{
    QString fileName = QFileInfo(m_file).fileName();

    // Extract the 14-digit numeric timestamp (yyyyMMddHHmmss) regardless of
    // any prefix ending in "_" or suffix starting with "." or "_".
    static const QRegularExpression re14("(\\d{14})");
    QRegularExpressionMatch match = re14.match(fileName);
    QString date_string = match.hasMatch() ? match.captured(1) : QString();

    return QDateTime::fromString(date_string, "yyyyMMddHHmmss");
}

bool ViatomFile::ParseHeader()
{
    static const int HEADER_SIZE = 40;
    QByteArray data = m_file.read(HEADER_SIZE);
    if (data.size() < HEADER_SIZE) {
        qDebug() << m_file.fileName() << "too short for a Viatom data file";
        return false;
    }

    const unsigned char* header = (const unsigned char*) data.constData();
    int sig   = header[0] | (header[1] << 8);
    int year  = header[2] | (header[3] << 8);
    int month = header[4];
    int day   = header[5];
    int hour  = header[6];
    int min   = header[7];
    int sec   = header[8];
    
    switch (sig) { //Viatom database version number  - Crimson Nape
    case 0x0003:
    case 0x0005:
        break;
    case 0x0006:
        break;  // 3-byte records (spo2, hr, motion); no oximetry_invalid or vibration fields
    default:
        qDebug() << m_file.fileName() << "Unrecognized DB version number in Viatom data file" << sig;
        return false;
        break;
    }
    m_sig = sig;

    if ((year < 2015 || year > 2059) || (month < 1 || month > 12) || (day < 1 || day > 31) ||
        (hour > 23) || (min > 59) || (sec > 59)) {
        qDebug() << m_file.fileName() << "invalid timestamp in Viatom data file";
        return false;
    }

    // It's unclear what the starting timestamp represents: is it the time at which
    // the device starts measuring data, and the first sample is 4s after that? Or
    // is the starting timestamp the time at which the first 4s average is reported
    // (and the first 4 seconds being average precede the starting timestamp)?
    //
    // If the former, then the chart draws the first sample too early (right at the
    // starting timestamp). Technically these should probably be square charts, but
    // the code currently forces them to be non-square.
    QDateTime data_timestamp = QDateTime(QDate(year, month, day), QTime(hour, min, sec));
    QDateTime filename_timestamp = getFilenameTimestamp();
    if (filename_timestamp.isValid()) {
        if (filename_timestamp != data_timestamp) {
            // TODO: Once there's a better/easier way to adjust session times within OSCAR, we can remove the below.
            qDebug() << m_file.fileName() << "Using filename timestamp" << filename_timestamp.toString("yyyy-MM-dd HH:mm:ss")
                     << "instead of header timestamp" << data_timestamp.toString("yyyy-MM-dd HH:mm:ss");
            data_timestamp = filename_timestamp;
        }
    } else {
        qWarning() << m_file.fileName() << "invalid timestamp in Viatom filename";
    }

    m_timestamp = data_timestamp.toMSecsSinceEpoch();
    m_sessionid = m_timestamp / 1000L;

    int filesize = header[9] | (header[10] << 8) | (header[11] << 16);  // possibly 32-bit
    CHECK_VALUE(header[12], 0);

    m_duration   = header[13] | (header[14] << 8);

    // Summary fields as decoded by the ViHealth SDK (OxyBleFile). Older models leave them all 0.
    m_summary.asleep_time   = header[15] | (header[16] << 8);  // in seconds
    //int avg_spo2 = header[17];
    //int min_spo2 = header[18];
    m_summary.drops3        = header[19];  // number of events
    m_summary.drops4        = header[20];  // number of events
    //int asleep_pct = header[21];  // percentage of the recording spent asleep
    m_summary.time_below_90 = header[22] | (header[23] << 8);  // in seconds
    //int events_under_90pct = header[24];  // number of distinct events
    m_summary.o2score       = header[25];  // tenths of a point
    m_summary.steps         = header[26] | (header[27] << 8) | (header[28] << 16) | ((quint32)header[29] << 24);
    for (int i = 15; i <= 29; i++) {
        if (header[i] != 0) {
            m_summary.valid = true;
            break;
        }
    }
    //CHECK_VALUE(header[30], 0);  // average pulse rate (when nonzero)
    CHECK_VALUE(header[31], 0);
    CHECK_VALUE(header[32], 0);
    CHECK_VALUE(header[33], 0);
    CHECK_VALUE(header[34], 0);
    CHECK_VALUE(header[35], 0);
    CHECK_VALUE(header[36], 0);
    CHECK_VALUE(header[37], 0);
    CHECK_VALUE(header[38], 0);
    CHECK_VALUE(header[39], 0);

    // Calculate timing resolution (in ms) of the data
    qint64 datasize = m_file.size() - HEADER_SIZE;
    int record_size = (m_sig == 0x0006) ? 3 : RECORD_SIZE;  // sig 0x0006 has 3-byte records
    m_record_count = datasize / record_size;
    m_resolution = m_duration / m_record_count * 1000L;
    if (m_resolution == 2000 && m_sig == 3) {
        // Interestingly the file size in the header corresponds the number of
        // distinct samples. These files actually double-report each sample!
        // So this resolution isn't really the real one. The importer should
        // calculate resolution from duration / record count after reading the
        // records, which will be deduplicated.
        CHECK_VALUE(filesize, ((m_file.size() - HEADER_SIZE) / 2) + HEADER_SIZE);
    } else {
        CHECK_VALUE(filesize, m_file.size());
    }
    if (m_sig == 0x0006) {
        CHECK_VALUE(m_resolution, 1000);
    } else {
        CHECK_VALUES(m_resolution, 2000, 4000);
    }
    if (true) {  // TODO: We need CheckMe sample data where this doesn't hold true.
        CHECK_VALUE(datasize % record_size, 0);
        CHECK_VALUE(m_duration % m_record_count, 0);
    }


    return true;
}

QList<ViatomFile::Record> ViatomFile::ReadData()
{
    QByteArray data = m_file.readAll();
    QDataStream in(data);
    in.setByteOrder(QDataStream::LittleEndian);

    QList<ViatomFile::Record> records;

    // Read all Pulse, SPO2 and Motion data
    do {
        ViatomFile::Record rec;
        if (m_sig == 0x0006) {
            // sig 0x0006 uses 3-byte records: spo2, hr, motion (no invalid/vibration fields)
            in >> rec.spo2 >> rec.hr >> rec.motion;
            rec.oximetry_invalid = (rec.spo2 == 0xFF || rec.hr == 0xFF) ? 0xFF : 0;
            rec.vibration = 0;
        } else {
            // The pulse rate is really 16 bits, so oximetry_invalid is its high byte: 0xFFFF marks an invalid reading.
            in >> rec.spo2 >> rec.hr >> rec.oximetry_invalid >> rec.motion >> rec.vibration;
            CHECK_VALUES(rec.oximetry_invalid, 0, 0xFF);
            // The last byte is a set of flags: 0x80 = SpO2 alarm, 0x40 = pulse alarm, 0x20 = motion alarm,
            // 0x10 = invalid reading; the ViHealth SDK also reads bits 4-5 as a sleep state.
            // Nothing has been seen in the low nibble.
            if (rec.vibration & 0x0F) {
                UNEXPECTED_VALUE(rec.vibration, "flags in bits 4-7 only");
            }
            // Invalid readings indicate any interruption in the measurements, whether
            // transitory (e.g. due to movement) or when the device is removed at the end of a session.
            if (rec.oximetry_invalid == 0xFF) {
                CHECK_VALUE(rec.spo2, 0xFF);
                CHECK_VALUE(rec.hr, 0xFF);
            }
        }
        records.append(rec);
    } while (records.size() < m_record_count);

    // It turns out 2s files are actually just double-reported samples on older models!
    if (m_resolution == 2000) {
        QList<ViatomFile::Record> dedup;
        bool all_are_duplicated = true;

        if ((records.size() % 2) != 0) {
            // An odd number of samples inherently can't be all duplicates.
            all_are_duplicated = false;
        } else {
            for (int i = 0; i < records.size(); i += 2) {
                auto & a = records.at(i);
                auto & b = records.at(i+1);
                if (a.spo2 != b.spo2
                    || a.hr != b.hr
                    || a.oximetry_invalid != b.oximetry_invalid
                    || a.motion != b.motion
                    || a.vibration != b.vibration) {
                    all_are_duplicated = false;
                    break;
                }
                dedup.append(a);
            }
        }
        if (m_sig == 5) {
            // Confirm that CheckMe O2 Max is a true 2s sample rate.
            CHECK_VALUE(all_are_duplicated, false);
        } else {
            // Confirm that older models are actually a 4s sample rate.
            CHECK_VALUE(m_sig, 3);
            CHECK_VALUE(all_are_duplicated, true);
        }
        if (all_are_duplicated) {
            // Return the deduplicated list.
            records = dedup;
        }
    }
    if (m_sig == 0x0006) {
        CHECK_VALUE(duration() / records.size(), 1);  // 1s resolution
    } else if (m_sig == 5) {
        CHECK_VALUES(duration() / records.size(), 2, 4);  // We've seen 2s and 4s resolution.
    } else {
        CHECK_VALUE(duration() / records.size(), 4);  // We've only seen 4s true resolution so far.
    }

    return records;
}

O2RingS::O2RingS(QFile & file) : ViatomFile(file)
{
}

bool O2RingS::ParseHeader()
{
    // For the O2Ring S, the header (10 bytes) only contains the signature and device model.
    // The other metadata is in a 48-byte trailer after the 3-byte samples (ViHealth SDK OxyIIBleFile):
    //   +0 checksum (4), +4 magic (4), +8 start time (4), +12 record count (4), +16 seconds per sample,
    //   +17 channel type, +18 channel bytes, +32 asleep time (2), +34 avg SpO2, +35 min SpO2,
    //   +36 3% drops, +37 4% drops, +38 % below 90, +39 time below 90 (2), +41 drops below 90,
    //   +42 O2 score, +43 steps (4), +47 avg pulse
    qint64 trailer_loc = m_file.size() - TRAILER_SIZE;
    if (trailer_loc < HEADER_SIZE || !m_file.seek(trailer_loc)) {
        qDebug() << m_file.fileName() << "error locating Viatom trailer";
        return false;
    }
    QByteArray data = m_file.read(TRAILER_SIZE);
    if (data.size() < TRAILER_SIZE) {
        qDebug() << m_file.fileName() << "too short for a Viatom data file";
        return false;
    }
    const unsigned char* trailer = (const unsigned char*) data.constData();
    auto u16 = [trailer](int i) { return (quint32) (trailer[i] | (trailer[i+1] << 8)); };
    auto u32 = [trailer, u16](int i) { return u16(i) | (u16(i+2) << 16); };

    m_sig = 0x0301;
    m_sessionid = 0;  // for UNEXPECTED_VALUE until the timestamp is known

    // max number of records in a O2Ring S file is 36000
    m_record_count = u32(12);
    CHECK_VALUE((qint64) m_record_count * 3, trailer_loc - HEADER_SIZE);
    if (m_record_count <= 0 || (qint64) m_record_count * 3 > trailer_loc - HEADER_SIZE) {
        qDebug() << m_file.fileName() << "invalid record count in Viatom data file" << m_record_count;
        return false;
    }

    m_interval = trailer[16];
    if (m_interval == 0) {
        UNEXPECTED_VALUE(m_interval, "nonzero");
        m_interval = 1;
    }
    m_duration = m_record_count * m_interval;
    m_resolution = m_interval * 1000;

    // The start time is the local wall-clock time stored as seconds since the epoch.
    QDateTime data_timestamp;
    quint32 start = u32(8);
    if (start != 0) {
        QDateTime wallclock = QDateTime::fromSecsSinceEpoch(start, QTimeZone::utc());
        data_timestamp = QDateTime(wallclock.date(), wallclock.time());
    }
    QDateTime filename_timestamp = getFilenameTimestamp();
    if (filename_timestamp.isValid()) {
        if (data_timestamp.isValid() && filename_timestamp != data_timestamp) {
            qDebug() << m_file.fileName() << "Using filename timestamp" << filename_timestamp.toString("yyyy-MM-dd HH:mm:ss")
                     << "instead of trailer timestamp" << data_timestamp.toString("yyyy-MM-dd HH:mm:ss");
        }
        data_timestamp = filename_timestamp;
    } else if (data_timestamp.isValid()) {
        qWarning() << m_file.fileName() << "invalid timestamp in Viatom filename, using trailer timestamp";
    } else {
        qWarning() << m_file.fileName() << "no valid timestamp in Viatom filename or trailer";
        return false;
    }
    m_timestamp = data_timestamp.toMSecsSinceEpoch();
    m_sessionid = m_timestamp / 1000L;

    m_summary.asleep_time   = u16(32);
    m_summary.drops3        = trailer[36];
    m_summary.drops4        = trailer[37];
    m_summary.time_below_90 = u16(39);
    m_summary.o2score       = trailer[42];
    m_summary.steps         = u32(43);
    m_summary.valid = true;

    // advance past the header
    return m_file.seek(HEADER_SIZE);
}

QList<ViatomFile::Record> O2RingS::ReadData()
{
    QList<ViatomFile::Record> records;

    // Read all Pulse, SPO2 and Motion data
    // 0xFF for spo2 or hr indicates an interruption in measurement
    // The third byte packs motion in bits 0-5 (in units of 2) with the pulse reminder (vibration)
    // in bit 6 and the SpO2 reminder in bit 7.
    QDataStream in(m_file.read((qint64) m_record_count * 3));
    do {
        ViatomFile::Record rec;
        unsigned char flags;
        in >> rec.spo2 >> rec.hr >> flags;
        rec.oximetry_invalid = (rec.spo2 == 0xFF || rec.hr == 0xFF) ? 0xFF : 0;
        rec.motion = (flags & 0x3F) * 2;
        rec.vibration = ((flags & 0x80) ? 0x80 : 0) | ((flags & 0x40) ? 0x40 : 0);
        records.append(rec);
    } while (records.size() < m_record_count);

    // Confirm that the sample rate matches the interval in the trailer
    CHECK_VALUE(duration() / records.size(), m_interval);
    return records;
}
