/* SleepLib RESMED Loader Header
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (C) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RESMED_LOADER_H
#define RESMED_LOADER_H

#include <QFileInfoList>
#include <QVector>
#include <chrono>
#include "SleepLib/machine.h" // Base class: MachineLoader
#include "SleepLib/machine_loader.h"
#include "SleepLib/profiles.h"
#include "SleepLib/loader_plugins/edfparser.h"
#include "SleepLib/loader_plugins/resmed_EDFinfo.h"

//********************************************************************************************
/// IMPORTANT!!!
//********************************************************************************************
// Please INCREMENT the following value when making changes to this loaders implementation.
//
const int resmed_data_version = 15;
//
//********************************************************************************************


class ResmedLoader;

class ResMedDay {
public:
    ResMedDay( QDate d) : date(d) {}

    QDate date;
    STRRecord str;
    QHash<QString, QString> files;  // key is filename, value is fullpath
};

typedef void (*ResDaySaveCallback)(ResmedLoader* loader, Session* session);

class ResDayTask:public ImportTask
{
public:
    ResDayTask(ResmedLoader * l, Machine * m, ResMedDay * d, ResDaySaveCallback s): reimporting(false), loader(l), mach(m), resday(d), save(s) {}
    virtual ~ResDayTask() {}
    virtual void run();

    bool reimporting;

protected:
    ResmedLoader * loader;
    Machine * mach;
    ResMedDay * resday;
    ResDaySaveCallback save;
};

/*! \class ResmedLoader
    \brief Importer for ResMed S9 Data
    */
class ResmedLoader : public CPAPLoader
{
    Q_OBJECT
    friend class ResmedImport;
    friend class ResmedImportStage2;
  public:
    ResmedLoader();
    virtual ~ResmedLoader();

    //! \brief Register the ResmedLoader with the list of other device loaders
    static void Register();

    //! \brief Detect if the given path contains a valid Folder structure
    virtual bool Detect(const QString & path);

    //! \brief Look up device model information of ResMed file structure stored at path
    virtual MachineInfo PeekInfo(const QString & path);

    virtual void checkSummaryDay( ResMedDay & resday, QDate date, Machine * mach );

    //! \brief Scans for ResMed SD folder structure signature, and loads any new data if found
    virtual int Open(const QString &);

    //! \brief Returns the version number of this ResMed loader
    virtual int Version() { return resmed_data_version; }

    //! \brief Returns the device class name of this loader. ("ResMed")
    virtual const QString &loaderName() { return resmed_class_name; }

    virtual MachineInfo newInfo() {
        return MachineInfo(MT_CPAP, 0, resmed_class_name, QObject::tr("ResMed"), QString(), 
        QString(), QString(), QObject::tr("S9"), QDateTime::currentDateTime(), resmed_data_version);
    }

    virtual void initChannels();

    //! \brief Converts EDFSignal data to time delta packed EventList, and adds to Session
    void ToTimeDelta(Session *sess, ResMedEDFInfo &edf, EDFSignal &es, ChannelID code, long recs,
                     qint64 duration, EventDataType min = 0, EventDataType max = 0, bool square = false);

    //! \brief How long after a session's last waveform sample an EVE event still belongs to it.
    //!
    //! ResMed devices write events to EVE.edf up to a minute after the waveform files stop
    //! (the last breaths before mask-off) and count them in their own indices (#363).
    static constexpr qint64 EVE_GRACE_AFTER_END_MS = 60000;

    //! \brief Latest event time that may be attributed to a session ending at \a sessLastMs.
    //!
    //! Normally \a sessLastMs + EVE_GRACE_AFTER_END_MS, but never reaching another session of
    //! the same day: ends 1 ms before the next session's start, and gives no grace at all
    //! when another session is still running at \a sessLastMs.
    //! \param sessLastMs      End of the session's waveform data (ms since epoch); 0 if none
    //! \param otherSessionsMs (start, end) in ms of the day's other sessions
    //! \return The grace end in ms, or \a sessLastMs / 0 when no grace applies
    static qint64 eventGraceEnd(qint64 sessLastMs, const QList<QPair<qint64, qint64>> & otherSessionsMs);

    //! \brief Parse the EVE Event annotation data, and save to Session * sess
    //! This contains all Hypopnea, Obstructive Apnea, Central and Apnea codes
    //! \param graceEndMs Events after the session's end but no later than this are kept too,
    //!                   and the session's end is moved to them (see eventGraceEnd()).
    //!                   0 keeps only events inside the session.
    bool LoadEVE(Session *sess, const QString & path, qint64 graceEndMs = 0);

    //! \brief Parse the CSL Event annotation data, and save to Session * sess
    //! This contains Cheyne Stokes Respiration flagging on the AirSense 10
    bool LoadCSL(Session *sess, const QString & path);

    //! \brief Parse the BRP High Resolution data, and save to Session * sess
    //! This contains Flow Rate, Mask Pressure, and Resp. Event  data
    bool LoadBRP(Session *sess, const QString & path);

    //! \brief Parse the SAD Pulse oximetry attachment data, and save to Session * sess
    //! This contains Pulse Rate and SpO2 Oxygen saturation data
    bool LoadSAD(Session *sess, const QString & path);

    //! \brief Parse the PRD low resolution data, and save to Session * sess
    //! This contains the Pressure, Leak, Respiratory Rate, Minute Ventilation, Tidal Volume, etc..
    bool LoadPLD(Session *sess, const QString & path);

    //! \brief Validate edf.startdate against sess->session() (which is the STR.edf
    //! mask-on time) and repair it if the EDF header ASCII datetime field is
    //! corrupt. Also recomputes edf.enddate from the still-valid record count and
    //! duration fields. Call immediately after edf.Parse().
    //! \return true if startdate was valid or was successfully repaired.
    bool repairEDFStartFromSession(ResMedEDFInfo &edf, Session *sess,
                                   const QString &path);


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Now for some CPAPLoader overrides
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    virtual QString PresReliefLabel() { return QObject::tr("EPR: "); }

    virtual ChannelID PresReliefMode() ;
    virtual ChannelID PresReliefLevel() ;
    virtual ChannelID CPAPModeChannel() ;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////

    //! \brief Number of sessions saved during the current import.
    //! Reset in Open() and incremented by SaveSession() under sessionMutex.
    int sessionCount;
    static void SaveSession(ResmedLoader* loader, Session* session);
    ResDaySaveCallback saveCallback;
    int OpenWithCallback(const QString & dirpath, ResDaySaveCallback s);

    void LogUnexpectedMessage(const QString & message);

protected:
    //! \brief The STR.edf file is a unique edf file with many signals
    bool ProcessSTRfiles(Machine *, QMap<QDate, STRFile> &, QDate);

    //! \brief Scan for new files to import in the given datalog directory
    //! \returns A QFileInfoList of files that are relevant to this import, including non-EDF files to be backed up
    QFileInfoList scanDatalogFiles(const QString & datalogPath, const QDate & firstImport);

    //! \brief Write a backup copy of the DATALOG files to the backup path provided, if backing up is enabled
    //! \returns A QFileInfoList of the files at their now proper location, which shall be the backupPath if backup is enabled
    QFileInfoList backupDatalogFiles(const QFileInfoList & datalogFiles, const QString & datalogPath, const QString & backupPath);

    //! \brief Group EDF files into resdayList by ResMed (noon-split) day, keyed by filename without .gz.
    //! Files without a .edf or .edf.gz extension are skipped
    void populateResmedDayList(const QFileInfoList & files);

//! \brief Write a backup copy to the backup path
    QString Backup(const QString & file, const QString & backup_path);

// The data members
//    QMap<SessionID, QStringList> sessfiles;
//    QMap<quint32, STRRecord> strsess;
//    QMap<QDate, QList<STRRecord *> > strdate;

    QMap<QDate, ResMedDay> resdayList;

    QHash<ChannelID, qint64> channel_efficiency;
    QHash<ChannelID, std::chrono::nanoseconds> channel_time;
    // Cumulative profiling times. Imports are single-threaded, so no locking is needed.
    std::chrono::nanoseconds timeInLoadBRP{};     //!< Time spent in LoadBRP().
    std::chrono::nanoseconds timeInLoadPLD{};     //!< Time spent in LoadPLD().
    std::chrono::nanoseconds timeInLoadEVE{};     //!< Time spent in LoadEVE().
    std::chrono::nanoseconds timeInLoadCSL{};     //!< Time spent in LoadCSL().
    std::chrono::nanoseconds timeInLoadSAD{};     //!< Time spent in LoadSAD().
    std::chrono::nanoseconds timeInEDFOpen{};     //!< Time spent opening EDF files.
    std::chrono::nanoseconds timeInEDFInfo{};     //!< Time spent parsing EDF headers.
    std::chrono::nanoseconds timeInAddWaveform{}; //!< Time spent in AddWaveform() for BRP data.
    std::chrono::nanoseconds timeInTimeDelta{};   //!< Time spent computing time deltas.

    // TODO: This really belongs in a generic location that all loaders can use.
    // But that will require retooling the overall call structure so that there's
    // a top-level import job that's managing a specific import. Right now it's
    // essentially managed by the importCPAP method rather than an object instance
    // with state.
    QMutex m_importMutex;
    QSet<QString> m_unexpectedMessages;

};

namespace resmed {

//! Extract a date from a filename, taking into account Resmed's noon split
QDate sleepDateFromFilename(const QString & filename);

/*!
 * \brief isImportableDatalogFile check if a file is importable. A file is importable
 * if it is readable, non-empty and its name parses to at or after firstImport.
 * The extension is not checked, so that non-EDF files are still backed up
 * \return True if fi passes all checks, false otherwise
 */
bool isImportableDatalogFile(const QFileInfo & fi, const QDate & firstImport);

}

#endif // RESMED_LOADER_H
