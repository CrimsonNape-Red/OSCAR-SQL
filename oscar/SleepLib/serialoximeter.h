/* SleepLib DeviceLoader Base Class Header
 *
 * Copyright (C) 2011-2018 Mark Watkins 
 * Copyright (c) 2019-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SERIALOXIMETER_H
#define SERIALOXIMETER_H

#include <QDateTime>
#include <QMap>
#include <QVector>

#include "SleepLib/machine_loader.h"



struct OxiRecord
{
    quint8 pulse;
    quint8 spo2;
    quint16 perf;

    OxiRecord():pulse(0), spo2(0),perf(0) {}
    OxiRecord(quint8 p, quint8 s): pulse(p), spo2(s) {}
    OxiRecord(quint8 p, quint8 s, quint16 pi): pulse(p), spo2(s), perf(pi) {} // with perfusion index
    OxiRecord(const OxiRecord & copy) { pulse = copy.pulse; spo2 = copy.spo2; perf = copy.perf; }
    //OxiRecord' is deprecated because it has a user-provided copy constructor
    OxiRecord& operator=(const OxiRecord& other) {
        if (this != &other) {  // Self-assignment check
            pulse = other.pulse;
            spo2 = other.spo2;
            perf = other.perf;
        }
        return *this;
    }
};

/*! \brief Base class of the oximeter file readers the Oximetry Wizard uses (CMS50,
 *  ChoiceMMed). A reader fills oxisessions from a data file; the wizard then lines
 *  the recording up with CPAP data and saves it.
 *
 *  The name is historical: these readers used to download over the CMS50's serial
 *  cable as well, which no current CMS50 uses, so that was removed.
 */
class SerialOximeter : public MachineLoader
{
Q_OBJECT
public:
    SerialOximeter() : MachineLoader() {
        oxirec = nullptr;
        have_perfindex = false;
    }
    virtual ~SerialOximeter() {}

    virtual bool Detect(const QString &path)=0;
    virtual int Open(const QString & path)=0;

    static void Register() {}

    virtual int Version()=0;
    virtual const QString &loaderName()=0;

    virtual QString getModel() { return QString(); }
    virtual QString getVendor()  { return QString(); }

    virtual MachineInfo newInfo() {
        return MachineInfo(MT_OXIMETER, 0, "", QString(), QString(), QString(), QString(), "Generic", QDateTime::currentDateTime(), 0);
    }

    bool havePerfIndex() { return have_perfindex; }

    virtual void process() {}

    // available sessions
    QMap<QDateTime, QVector<OxiRecord> *> oxisessions;

    // current session
    QVector<OxiRecord> * oxirec;

    QDateTime startTime() { return m_startTime; }
    void setStartTime(QDateTime datetime) { m_startTime = datetime; }
    virtual bool isStartTimeValid() { return true; }
    virtual void setDuration(int) { }

    virtual qint64 importResolution() { return 1000; }

    void trashRecords();

protected:
    QDateTime m_startTime;
    bool have_perfindex;
};

#endif // SERIALOXIMETER_H
