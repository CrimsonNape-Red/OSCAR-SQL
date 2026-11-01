/* SleepLib CMS50X Loader Implementation
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark W
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

//********************************************************************************************
// Please only INCREMENT the cms50_data_version in cms50_loader.h when making changes
// that change loader behaviour or modify channels in a manner that fixes old data imports.
// Note that changing the data version will require a reimport of existing data for which OSCAR
// does not keep a backup - so it should be avoided if possible.
// i.e. there is no need to change the version when adding support for new devices
//********************************************************************************************

#include <QApplication>
#include <QDir>
#include <QString>
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QDebug>
#include <QList>
#include <QMessageBox>


// The qt5.15 obsolescence of hex requires this change.
// this solution to QT's obsolescence is only used in debug statements
#if QT_VERSION >= QT_VERSION_CHECK(5,15,0)
    #define QTHEX     Qt::hex
    #define QTDEC     Qt::dec
#else
    #define QTHEX     hex
    #define QTDEC     dec
#endif


using namespace std;

#include "cms50_loader.h"
#include "SleepLib/machine.h"
#include "SleepLib/session.h"

CMS50Loader::CMS50Loader()
{
    m_type = MT_OXIMETER;
    m_abort = false;
    cms50dplus = false;
    oxirec = nullptr;
}

CMS50Loader::~CMS50Loader()
{
}

bool CMS50Loader::Detect(const QString &path)
{
    if (p_profile->oxi->oximeterType() == 1) {
        return true;
    }

    Q_UNUSED(path);
    return false;
}

int CMS50Loader::Open(const QString & path)
{
    // Reads a data file saved by SpO2Review or similar. (Downloading from the
    // oximeter's serial cable, which no current CMS50 uses, was removed.)
    m_abort = false;
    cms50dplus = false;
    setStatus(NEUTRAL);

    if (oxirec) {
        trashRecords();
    }

    QString ext = path.section(".", -1);	// find the last '.'
    if ((ext.compare("spo2", Qt::CaseInsensitive)==0) || (ext.compare("spo", Qt::CaseInsensitive)==0) || (ext.compare("spor", Qt::CaseInsensitive)==0)) {
        // try to read and process SpoR file..
        return readSpoRFile(path) ? 1 : 0;
    }

    return 0;
}

bool CMS50Loader::readSpoRFile(QString path)
{
    QFile file(path);
    if (!file.exists()) {
    	qWarning() << "Can't find the oximeter file: " << path;
        QMessageBox::warning(nullptr, STR_MessageBox_Error, "<h2>"+tr("Could not find the oximeter file:")+"<br/><br/>"+path+"</h2>");
        return false;
    }

    if (!file.open(QFile::ReadOnly)) {
    	qWarning() << "Can't open the oximeter file: " << path;
        QMessageBox::warning(nullptr, STR_MessageBox_Error, "<h2>"+tr("Could not open the oximeter file:")+"<br/><br/>"+path+"</h2>");
        return false;
    }

    bool spo2header = false;
    QString ext = path.section('.', -1);
    qDebug() << "Oximeter file extention is " << ext;
    if (ext.compare("spo2",Qt::CaseInsensitive) == 0) {
        spo2header = true;
        qDebug() << "Oximeter file looks like an SpO2 type" ;
    }

    QByteArray data;

    qint64 filesize = file.size();
    data = file.readAll();
    QDataStream in(data);
    in.setByteOrder(QDataStream::LittleEndian);
    quint16 pos;
    in >> pos;

    in.skipRawData(pos - 2);

    int bytes_per_record = 2;

    if (!spo2header) {
        // next is 0x0002
        // followed by 16bit duration in seconds

        // Read date and time (it's a 16bit charset)

        char dchr[20];
        int j = 0;
        for (int i = 0; i < 18 * 2; i += 2) {
            dchr[j++] = data.at(8 + i);
        }

        dchr[j] = 0;
        if (dchr[0]) {
            QString dstr(dchr);
            // Ensure date is correct first to ensure DST is handled correctly
            QDate date = QDate::fromString(dstr.left(8),"MM/dd/yy");
            QTime time = QTime::fromString(dstr.right(8),"HH:mm:ss");
            if (date.year() < 2000) {
                date = date.addYears(100);
            }
            m_startTime = QDateTime(date, time);
        } else {
            m_startTime = QDateTime(QDate::currentDate(), QTime(0,0,0));
            cms50dplus = true;
        }
    } else { // it is an spo2header

        quint32 samples = 0; // number of samples

        quint32 year, month, day;
        quint32 hour, minute, second;

        if (data.at(pos) != 1) {
            qWarning() << "oximeter file" << path << "might be odd format";
        }

        // Unknown cruft header...
        in.skipRawData(200);

        in >> year >> month >> day;
        in >> hour >> minute >> second;
        
        if ( year == 0 ) {    // typically from a CMS50D+
            m_startTime = QDateTime(QDate::currentDate(), QTime(hour, minute, second));
            cms50dplus = true;
        }
        else
            m_startTime = QDateTime(QDate(year, month, day), QTime(hour, minute, second));

        // ignoring it for now
        pos += 0x1c + 200;

        in >> samples;

        int remainder = filesize - pos;

        bytes_per_record = remainder / samples;
        qDebug() << samples << "samples of" << bytes_per_record << "bytes each";

        // CMS50I .spo2 data have 4 bytes: a 16bit, followed by spo2 then pulse

    }

    oxirec = new QVector<OxiRecord>;
    oxisessions[m_startTime] = oxirec;

    unsigned char o2, pr;
    quint16 un;

    // Read all Pulse and SPO2 data
    do {
        if (bytes_per_record > 2) {
            in >> un;
        }
        in >> o2;
        in >> pr;

        if ((o2 == 0x7f) && (pr == 0xff)) {
            o2 = pr = 0;
            un = 0;
        }

        if (spo2header) {
            oxirec->append(OxiRecord(pr, o2));
        } else {
            oxirec->append(OxiRecord(o2, pr));
        }
    } while (!in.atEnd());



    // processing gets done later
    return true;
}

void CMS50Loader::process()
{
    // Just clean up any extra crap before oximeterimport parses the oxirecords..
    return;
//    if (!oxirec)
//    if (size<10)

}



static bool cms50_initialized = false;

void CMS50Loader::Register()
{
    if (cms50_initialized) { return; }

    qDebug() << "Registering CMS50Loader";
    RegisterLoader(new CMS50Loader());
    cms50_initialized = true;
}

