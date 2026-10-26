/* SleepLib Weinmann SOMNOsoft/Balance Loader Header
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins
 * Copyright (c) 2016-2017 "TBM" (Weinmann support for the SleepyHead fork this is based on)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef WEINMANN_LOADER_H
#define WEINMANN_LOADER_H

#include <QFile>

#include "SleepLib/machine.h" // Base class: MachineLoader
#include "SleepLib/machine_loader.h"
#include "SleepLib/profiles.h"


//********************************************************************************************
/// IMPORTANT!!!
//********************************************************************************************
// Please INCREMENT the following value when making changes to this loaders implementation.
//
const int weinmann_data_version = 3;
//
//********************************************************************************************

/*! \class Weinmann
    \brief Weinmann customized device object
    */
class Weinmann : public CPAP
{
  public:
    Weinmann(Profile *, MachineID id = 0);
    virtual ~Weinmann();
};


struct CompInfo {
    CompInfo()
    {
        session = nullptr;
        flow_start = 0;
        flow_size = 0;
        stat_start = 0;
        stat_size = 0;
        pres_start = 0;
        pres_size = 0;
        amv_start = 0;
        amv_size = 0;
        event_start = 0;
        event_recs = 0;
    }
    CompInfo(Session *sess, QDateTime dt, quint32 fs, quint32 fl, quint32 ss, quint32 sl, quint32 ps, quint32 pl,
             quint32 as, quint32 al, quint32 es, quint32 er)
        : session(sess), time(dt), flow_start(fs), flow_size(fl), stat_start(ss), stat_size(sl),
          pres_start(ps), pres_size(pl), amv_start(as), amv_size(al), event_start(es), event_recs(er)
    {
    }
    Session *session;
    QDateTime time;
    quint32 flow_start;
    quint32 flow_size;
    quint32 stat_start;
    quint32 stat_size;
    quint32 pres_start;
    quint32 pres_size;
    quint32 amv_start;
    quint32 amv_size;
    quint32 event_start;
    quint32 event_recs;
};

const QString weinmann_class_name = STR_MACH_Weinmann;

/*! \class WeinmannLoader
    \brief Loader for the data of Weinmann SOMNObalance / SOMNObalance e devices

    Reads WM_DATA.TDF (and WM_PARA.TPG) from the SD card.
    */
class WeinmannLoader : public CPAPLoader
{
  public:
    WeinmannLoader();
    virtual ~WeinmannLoader();

    //! \brief Detect if the given path contains a valid Folder structure
    virtual bool Detect(const QString &path);

    //! \brief Look up the device model and serial number
    virtual MachineInfo PeekInfo(const QString &path);

    //! \brief Scans path for Weinmann data signature, and Loads any new data
    virtual int Open(const QString &path);

    //! \brief Returns SleepLib database version of this Weinmann loader
    virtual int Version() { return weinmann_data_version; }

    //! \brief Returns the device loader name of this class
    virtual const QString &loaderName() { return weinmann_class_name; }

    int ParseIndex(QFile &wmdata);
    int ParseSettings(QFile &wmdata);

    //! \brief Registers this MachineLoader with the master list, so Weinmann data can load
    static void Register();

    virtual MachineInfo newInfo()
    {
        return MachineInfo(MT_CPAP, 0, weinmann_class_name, QObject::tr("Weinmann"), QString(), QString(), QString(), QObject::tr("SOMNObalance"), QDateTime::currentDateTime(), weinmann_data_version);
    }
    virtual void initChannels();

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Now for some CPAPLoader overrides
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    virtual QString PresReliefLabel();
    virtual ChannelID PresReliefMode();
    virtual ChannelID PresReliefLevel();
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////

  protected:
    QHash<QString, int> index;
    QHash<QString, QString> msettings;
    QHash<QString, double> mhumiuse;
    QList<CompInfo> compinfo;

    //! \brief converts the device id into a string
    QString GetDeviceName(const int id);

    //! \brief writes the settings of the device to the session.
    //! Honors already set pressure values - so call it BEFORE setting session specific values.
    void PopulateSettings(Session *sess);

    //! \brief updates the progress bar and keeps the user interface responsive
    void reportProgress(int value);
};


#endif // WEINMANN_LOADER_H
