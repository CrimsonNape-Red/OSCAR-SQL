/* SleepLib CMS50X Loader Header
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (C) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CMS50F37LOADER_H
#define CMS50F37LOADER_H

#include "SleepLib/serialoximeter.h"

const QString cms50f37_class_name = "CMS50F37";
const int cms50f37_data_version = 0;


/*! \class CMS5037Loader
    \brief Bulk Importer for newer CMS50 oximeters
    */
class CMS50F37Loader : public SerialOximeter
{
Q_OBJECT
  public:
    CMS50F37Loader();
    virtual ~CMS50F37Loader();

    virtual bool Detect(const QString &path);
    virtual int Open(const QString & path);

    static void Register();

    virtual int Version() { return cms50f37_data_version; }
    virtual const QString &loaderName() { return cms50f37_class_name; }

    virtual MachineInfo newInfo() {
        return MachineInfo(MT_OXIMETER, 0, cms50f37_class_name, QObject::tr("Contec"), QObject::tr("CMS50F3.7"), QString(), QString(), QObject::tr("CMS50F"), QDateTime::currentDateTime(), cms50f37_data_version);
    }

    virtual void process();
    virtual bool isStartTimeValid() { return !cms50dplus; }

  protected:
    bool readSpoRFile(const QString & path);

  private:
    bool cms50dplus;
};

#endif // CMS50F37LOADER_H
