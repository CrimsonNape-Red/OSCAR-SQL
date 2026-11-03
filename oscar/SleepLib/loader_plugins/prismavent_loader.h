/* SleepLib Löwenstein prisma VENT Loader Header
 *
 * Imports SD cards written by Löwenstein (formerly Weinmann) prisma VENT home
 * ventilators. Each therapy day is exported as a zip archive holding one WMEDF
 * signal file and one respiratory-event file per session, plus a parameter log
 * from which the settings in force at each session start are recovered.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PRISMAVENT_LOADER_H
#define PRISMAVENT_LOADER_H

#include <QDate>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>

#include "SleepLib/machine_loader.h"

class QDir;

#ifdef UNITTEST_MODE
#include <sstream>  // include first so the #define below can't leak into libstdc++
#define private public
#define protected public
#endif

//********************************************************************************************
/// IMPORTANT!!!
//********************************************************************************************
// Please INCREMENT the following value when making changes to this loaders implementation
// BEFORE making a release
const int prismavent_data_version = 1;
//
//********************************************************************************************

//! Stored in the database as the device's loader name; must never change.
const QString prismavent_class_name = "PrismaVent";

/*! \name prisma VENT settings channels
    Session settings taken from the active therapy program in parameter.xml.
    See Notes/loaders/PRISMA_VENT_CARD_ANALYSIS.md for how each was identified.
    @{ */
extern ChannelID PrismaVent_Mode;            //!< Raw TherapyMode value; only S/T, autoS/T and MPVp are named.
extern ChannelID PrismaVent_Program;         //!< Active therapy program, 1-3.
extern ChannelID PrismaVent_BackupRate;      //!< Backup breath rate, breaths/min.
extern ChannelID PrismaVent_Ti;              //!< Inspiration time of a timed breath, s.
extern ChannelID PrismaVent_TiMin;           //!< Minimum inspiration time, s.
extern ChannelID PrismaVent_TiMax;           //!< Maximum inspiration time, s.
extern ChannelID PrismaVent_TriggerType;     //!< 0 = manual, 1 = automatic trigger.
extern ChannelID PrismaVent_TriggerInsp;     //!< Inspiratory trigger sensitivity level; manual trigger only.
extern ChannelID PrismaVent_TriggerExp;      //!< Expiratory trigger, % of peak flow; manual trigger only.
extern ChannelID PrismaVent_InspRamp;        //!< Inspiratory pressure rise level.
extern ChannelID PrismaVent_ExpRamp;         //!< Expiratory pressure fall level.
/*! @} */

/*! \name prisma VENT event channels
    Respiratory event ids the Prisma loader has no channel for.
    @{ */
extern ChannelID PrismaVent_ePC;             //!< 2-minute epoch with premature cycling (id 6).
extern ChannelID PrismaVent_Disconnect;      //!< Disconnection (id 171).
extern ChannelID PrismaVent_SEL;             //!< Sudden decline of leakage (id 330).
/*! @} */

namespace PrismaVentParsing {

/*! \brief Parses a log timestamp of the form "+HHHH:MM:SS.mmm".
    \param text Timestamp as written by the device. Hours may exceed 23: a
                session that runs past midnight keeps counting from the log's date.
    \param msOut Receives milliseconds since midnight of the log date.
    \return false if \a text is not in the expected form. */
bool parseLogTime(const QString & text, qint64 & msOut);

/*! \class ParameterLog
    \brief The parameter history of one day, read from parameter.xml.

    The device writes a full snapshot of every parameter at each power-on and a
    single entry for each later change. Therapy parameters are written once per
    program (attribute program="0".."2"); device-wide ones carry no program.
    Parameter ids are resolved through the day's parametersmap.xml so that a
    firmware that renumbers them still reads correctly. */
class ParameterLog
{
public:
    /*! \brief Loads the day's parameter log.
        \param parameterXml Contents of parameter.xml.
        \param mapXml Contents of parametersmap.xml.
        \return false if either file is unreadable or the log has no date. */
    bool load(const QByteArray & parameterXml, const QByteArray & mapXml);

    /*! \brief Looks up a device-wide parameter as it stood at a given moment.
        \param name Parameter name as in parametersmap.xml, e.g. "ActiveProgram".
        \param atMs Milliseconds since the epoch.
        \param ok Set to true if a value at or before \a atMs exists.
        \return The value, or 0 if none. */
    int globalValue(const QString & name, qint64 atMs, bool * ok = nullptr) const;

    /*! \brief Looks up a therapy parameter of one program at a given moment.
        \param name Parameter name, e.g. "IPAP".
        \param program Program index as written by the device, 0-2.
        \param atMs Milliseconds since the epoch.
        \param ok Set to true if a value at or before \a atMs exists.
        \return The value, or 0 if none. */
    int programValue(const QString & name, int program, qint64 atMs, bool * ok = nullptr) const;

protected:
    //! (time in ms since the epoch, value) pairs, sorted by time.
    typedef QList<QPair<qint64, int>> Timeline;

    static int valueAt(const Timeline & timeline, qint64 atMs, bool * ok);

    QHash<QString, int> m_ids;                 //!< Parameter name -> id from parametersmap.xml.
    QHash<int, Timeline> m_global;             //!< Device-wide parameters by id.
    QHash<int, Timeline> m_program[3];         //!< Therapy parameters by program, then id.
};

/*! \struct RespEvent
    \brief One respiratory event from an event_NNNN.xml file. */
struct RespEvent
{
    int id = 0;            //!< Event type as written by the device.
    qint64 beginMs = 0;    //!< Start, ms from the start of the session.
    qint64 endMs = -1;     //!< End, ms from the start of the session; -1 if never closed.
    int strength = 0;      //!< Strength attribute; its meaning is unknown for most types.
};

/*! \brief Parses an event_NNNN.xml file into begin/end pairs.

    Each event is written as a "begin" and an "end" element with the same id and
    a time in seconds from the session start. Pairs are matched per id in file
    order; a begin left open at the end of the file is returned with endMs = -1,
    and an end with no begin is taken to have started with the session. */
QList<RespEvent> parseRespEvents(const QByteArray & xml);

} // namespace PrismaVentParsing

//********************************************************************************************

/*! \class PrismaVentLoader
    \brief Löwenstein prisma VENT loader module.

    The card layout is WM<article number>/SN<serial>/, with one zip per therapy
    day named <counter>_<yyyy-mm-dd>.zip. A card can hold folders for more than
    one device; the one with the most recent day is imported. */
class PrismaVentLoader : public CPAPLoader
{
    Q_OBJECT
    static bool initialized;

public:
    PrismaVentLoader();
    virtual ~PrismaVentLoader();

    //! \brief Registers this loader with the list of loaders.
    static void Register();

    //! \brief True if \a path is, or contains, a prisma VENT device folder.
    virtual bool Detect(const QString & path) override;

    //! \brief Imports the sessions from the most recent device folder under \a path.
    virtual int Open(const QString & path) override;

    //! \brief Reads the device identity from device.xml.
    virtual MachineInfo PeekInfo(const QString & path) override;

    virtual int Version() override { return prismavent_data_version; }
    virtual const QString & loaderName() override { return prismavent_class_name; }

    virtual MachineInfo newInfo() override {
        return MachineInfo(MT_CPAP, 0, prismavent_class_name, QObject::tr("Löwenstein"),
                           QObject::tr("prisma VENT"), QString(), QString(), QString(),
                           QDateTime::currentDateTime(), prismavent_data_version);
    }

    virtual ChannelID CPAPModeChannel() override { return PrismaVent_Mode; }

    //! \brief Called at application init to set up the prisma VENT channels.
    virtual void initChannels() override;

protected:
    /*! \brief Finds the device folders at, below, or two levels below \a path.

        A device folder holds device.xml (root element Device_settings) next to
        prismaVENT.sdpvdat or configuration.json. Grandchildren are only searched
        under WM* folders, which keeps Detect() cheap on a card root holding
        another brand's deep folder tree. */
    QStringList findDeviceDirs(const QString & path);

    /*! \brief Picks the device folder to import from \a path.
        \return The folder whose newest day archive is the most recent, or the
                first device folder if none holds an archive; empty if none. */
    QString selectDeviceDir(const QString & path);

    //! \brief Reads device.xml in \a deviceDir into a MachineInfo.
    MachineInfo readDeviceInfo(const QString & deviceDir);

    /*! \brief Copies \a deviceDir into the machine's backup folder.

        The WM.../SN... layout is reproduced, with the WM level named from the
        device's WM_number, so that Rebuild, which opens the backup folder itself,
        finds it again. The device's diagnostic logs folder is not copied. Files
        already backed up with the same size are skipped; a day archive that
        gains a session is rewritten with a new size. */
    void backupDeviceDir(const QString & deviceDir, const MachineInfo & info);

    /*! \brief Imports every new session from one day archive.
        \return The number of sessions added. */
    int importDayArchive(const QString & zipPath);

    /*! \brief Builds one session from its signal and event files.
        \return The session, or nullptr if the signal file is unreadable or the
                session should be skipped. Ownership passes to the caller. */
    Session * buildSession(const QByteArray & signalData, const QByteArray & eventData,
                           const PrismaVentParsing::ParameterLog * params);

    //! \brief Fills the session's settings from the program active at \a startMs,
    //! the session start in ms since the epoch.
    void applySettings(Session * session, const PrismaVentParsing::ParameterLog & params, qint64 startMs);

    //! \brief Event ids already reported as unexpected during this import.
    QSet<int> m_reportedEventIds;
    //! \brief Therapy modes already reported as unexpected during this import.
    QSet<int> m_reportedModes;
};

#endif // PRISMAVENT_LOADER_H
