/* SleepLib ChoiceMMed MD300W1 Oximeter Loader Header
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (C) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef MD300W1LOADER_H
#define MD300W1LOADER_H

#include "SleepLib/serialoximeter.h"

const QString md300w1_class_name = "MD300W1";
const int md300w1_data_version = 1;


/*! \class MD300W1Loader
    \brief Importer for ChoiceMMed MD300W1 data format..
    */
class MD300W1Loader : public SerialOximeter
{
Q_OBJECT
  public:
    MD300W1Loader();
    virtual ~MD300W1Loader();

    virtual bool Detect(const QString &path);
    virtual int Open(const QString & path);

    static void Register();

    virtual int Version() { return md300w1_data_version; }
    virtual const QString &loaderName() { return md300w1_class_name; }


    virtual MachineInfo newInfo() {
        return MachineInfo(MT_OXIMETER, 0, md300w1_class_name, QObject::tr("ChoiceMMed"), QObject::tr("MD300W1 Oximeter"), QString(), QString(), QObject::tr("MD300"), QDateTime::currentDateTime(), md300w1_data_version);
    }


    virtual void process();

    virtual bool isStartTimeValid() { return true; }

protected:
    bool readDATFile(const QString & path);

  private:
    QDateTime oxitime;
};

#endif // MD300W1LOADER_H
