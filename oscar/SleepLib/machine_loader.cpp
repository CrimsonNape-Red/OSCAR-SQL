/* SleepLib Device Loader Class Implementation
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#define TEST_MACROS_ENABLEDoff
#include <test_macros.h>

#include <QApplication>
#include <QFile>
#include <QDir>
#include <QSet>
#include <QThreadPool>

#include "session.h"
#include "machine_loader.h"
#include "profiles.h"  // For p_profile global
#include "performance_timer.h"

// GLOBALS:
bool genpixmapinit = false;
QList<MachineLoader *> m_loaders;

QPixmap * MachineLoader::genericCPAPPixmap;

MachineLoader::MachineLoader() :QObject(nullptr)
{
#ifndef UNITTEST_MODE  // no QPixmap without a QGuiApplication
    if (!genpixmapinit) {
        genericCPAPPixmap = new QPixmap(genericPixmapPath);
        genpixmapinit = true;
    }
#endif
    m_abort = false;
    m_type = MT_UNKNOWN;
    m_status = NEUTRAL;
    m_ctx = nullptr;
    m_dailySummariesCalculated = false;
}

MachineLoader::~MachineLoader()
{
}

void MachineLoader::addSession(Session * sess)
{
    sessionMutex.lock();
    new_sessions[sess->session()] = sess;
    sessionMutex.unlock();
}

void MachineLoader::finishAddingSessions()
{
    PERF_TIMER_SCOPE("MachineLoader::finishAddingSessions");
    
    // Track session count for the report
    int sessionCount = new_sessions.size();
    
    // CRITICAL: Ensure machine is saved to database BEFORE adding sessions
    // Sessions need the machine's database_id for foreign key relationship
    if (!new_sessions.empty()) {
        Machine * mach = new_sessions.begin().value()->machine();
        if (mach && mach->getDatabaseId() == 0) {
            qDebug() << "MachineLoader::finishAddingSessions: Saving machine to database before adding sessions";
            if (!mach->SaveToDatabase()) {
                qWarning() << "MachineLoader::finishAddingSessions: Failed to save machine to database - sessions will not be saved to database";
            } else {
                qDebug() << "MachineLoader::finishAddingSessions: Machine saved to database with ID" << mach->getDatabaseId();
            }
        }
    }
    
    // Using a map specifically so they are inserted in order.
    QSet<Machine *> touched;
    for (auto it=new_sessions.begin(), end=new_sessions.end(); it != end; ++it) {
        Session * sess = it.value();
        Machine * mach = sess->machine();
        mach->AddSession(sess);
        touched.insert(mach);
    }

    // Rows written before the device's reported-channel set was complete carry NULL in
    // columns the device does report. AddSession() above has settled the set, so put
    // them right before the daily rollup below reads it (GitLab #313).
    for (Machine * mach : touched) {
        mach->repairStoredSummaries();
    }

    // Calculate daily summaries for all sessions that have been added to machines
    // This runs during import, not on every profile load
    // Note: Some loaders add sessions directly to machines (not via new_sessions map),
    // so we always call calculateDailySummaries() regardless of new_sessions state
    if (p_profile) {
        p_profile->calculateDailySummaries();
        m_dailySummariesCalculated = true;
        qDebug() << "MachineLoader::finishAddingSessions: Calculated daily summaries for imported data";
    }

    new_sessions.clear();
    
    // Report performance metrics when import completes
    qDebug() << "========================================";
    qDebug() << "SD Card Import Completed -" << sessionCount << "sessions imported";
    qDebug() << "========================================";
    PERF_TIMER_REPORT();
    qDebug() << "========================================";
}

QPixmap & MachineLoader::getPixmap(QString series)
{
    QHash<QString, QPixmap>::iterator it = m_pixmaps.find(series);
    if (it != m_pixmaps.end()) {
        return it.value();
    }
    return *genericCPAPPixmap;
}

QString MachineLoader::getPixmapPath(QString series)
{
    QHash<QString, QString>::iterator it = m_pixmap_paths.find(series);
    if (it != m_pixmap_paths.end()) {
        return it.value();
    }
    return genericPixmapPath;
}

void MachineLoader::queTask(ImportTask * task)
{
    m_MLtasklist.push_back(task);
}

void MachineLoader::runTasks(bool threaded)
{

    m_totalMLtasks = m_MLtasklist.size();
    qDebug() << "MachineLoader::runTasks MLtasklist size is" << m_totalMLtasks;
    if (m_totalMLtasks == 0) 
        return;
    emit setProgressMax(m_totalMLtasks);
    m_currentMLtask=0;

    threaded=AppSetting->multithreading();
    qDebug() << "MachineLoader::runTasks" << (threaded ? "is" : "is not") << "multithreading";

    if ( ! threaded) {
        while (!m_MLtasklist.isEmpty() && !m_abort) {
            qDebug() << "MachineLoader:runTasks running task" << m_currentMLtask+1;
            ImportTask * task = m_MLtasklist.takeFirst();
            task->run();

            // update progress bar
            m_currentMLtask++;
            emit setProgressValue(m_currentMLtask);
            QApplication::processEvents();

            delete task;
        }
    } else {
        ImportTask * task = m_MLtasklist[0];

        QThreadPool * threadpool = QThreadPool::globalInstance();

        while (!m_abort) {

            if (threadpool->tryStart(task)) {
                m_MLtasklist.pop_front();

                if (!m_MLtasklist.isEmpty()) {
                    // next task to be run
                    task = m_MLtasklist[0];

                    // update progress bar
                    emit setProgressValue(++m_currentMLtask);
                    QApplication::processEvents();
                } else {
                    // job list finished
                    break;
                }
            }
        }
        QThreadPool::globalInstance()->waitForDone(-1);
    }
    if (m_abort) {
        // delete remaining tasks and clear task list
        for (auto & task : m_MLtasklist) {
            delete task;
        }
        m_MLtasklist.clear();
    }
}


QList<MachineLoader *> GetLoaders(MachineType mt)
{
    QList<MachineLoader *> list;
    for (int i=0; i < m_loaders.size(); ++i) {
        if (mt == MT_UNKNOWN) {
            list.push_back(m_loaders.at(i));
        } else {
            MachineType mtype = m_loaders.at(i)->type();
            if (mtype == mt) {
                list.push_back(m_loaders.at(i));
            }
        }
    }
    return list;
}

MachineLoader * lookupLoader(Machine * m)
{
    for (int i=0; i < m_loaders.size(); ++i) {
        MachineLoader * loader = m_loaders.at(i);
        if (loader->loaderName() == m->loaderName())
            return loader;
    }
    return nullptr;
}

MachineLoader * lookupLoader(QString loaderName)
{
    // Keys are lower-case; values must match a registered loaderName() exactly.
    static const QHash<QString, QString> aliases = {
        {"philips", "PRS1"},
        {"respironics", "PRS1"},
        {"devilbiss", "Intellipap"},
        {"bmc-g3x", "BMCG3X"},
        {"bmc_g3x", "BMCG3X"},
        {"icon", "FPIcon"},
        {"f&p-icon", "FPIcon"},
        {"f&p-sleepstyle", "SleepStyle"},
        {"apple-health", "AppleHealth"}
    };

    QString target = loaderName.trimmed();
    QString alias = aliases.value(target.toLower());
    if (!alias.isEmpty()) {
        target = alias;
    }

    for (int i=0; i < m_loaders.size(); ++i) {
        MachineLoader * loader = m_loaders.at(i);
        if (loader->loaderName().compare(target, Qt::CaseInsensitive) == 0)
            return loader;
        // Also support names without "Loader" suffix (e.g. Apex -> ApexLoader, Sefam -> SefamLoader, Yuwell -> YuwellLoader)
        if (loader->loaderName().endsWith("Loader", Qt::CaseInsensitive) &&
            loader->loaderName().left(loader->loaderName().length() - 6).compare(target, Qt::CaseInsensitive) == 0) {
            return loader;
        }
    }
    return nullptr;
}

void RegisterLoader(MachineLoader *loader)
{
    loader->initChannels();
    m_loaders.push_back(loader);
}

void DestroyLoaders()
{
    for (auto & loader : m_loaders) {
        delete(loader);
    }

    m_loaders.clear();
}

/*
QList<ChannelID> CPAPLoader::eventFlags(Day * day)
{
    Machine * mach = day->machine(MT_CPAP);

    QList<ChannelID> list;

    if (mach->loader() != this) {
        qDebug() << "Trying to ask" << loaderName() << "for" << mach->loaderName() << "data";
        return list;
    }

    for (int i = 0; i < ahiChannels.size(); i++)
        list.push_back(ahiChannels.at(i));

    return list;
}
*/

/**
 * @brief Encode a buffer as a complete gzip stream (RFC 1952).
 *
 * Done in memory with zlib's deflate so that file I/O can go through QFile,
 * which handles Unicode paths on every platform. gzopen() takes a narrow
 * char* path and cannot (GitLab #350).
 * @param data Uncompressed bytes.
 * @param out Receives the gzip stream.
 * @return true on success.
 */
static bool gzipEncode(const QByteArray & data, QByteArray & out)
{
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    // windowBits 15 + 16 selects a gzip header and trailer instead of zlib's
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }

    out.resize(deflateBound(&strm, data.size()));
    strm.next_in = (Bytef *)data.constData();
    strm.avail_in = data.size();
    strm.next_out = (Bytef *)out.data();
    strm.avail_out = out.size();

    // deflateBound() guarantees room for a single-pass Z_FINISH
    int ret = deflate(&strm, Z_FINISH);
    out.resize(strm.total_out);
    deflateEnd(&strm);
    return ret == Z_STREAM_END;
}

bool uncompressFile(QString infile, QString outfile)
{
    if (!infile.endsWith(".gz",Qt::CaseInsensitive)) {
        qDebug() << "uncompressFile()" << outfile << "missing .gz extension???";
        return false;
    }

    if (QFile::exists(outfile)) {
        qDebug() << "uncompressFile()" << outfile << "already exists";
        return false;
    }

    // Read through QFile rather than gzopen() so non-ASCII paths work (GitLab #350)
    QFile fi(infile);
    if (!fi.open(QFile::ReadOnly)) {
        qDebug() << "uncompressFile() Couldn't open" << infile;
        return false;
    }
    QByteArray data = gUncompress(fi.readAll());
    fi.close();
    if (data.isEmpty()) {
        qDebug() << "uncompressFile() Couldn't decompress" << infile;
        return false;
    }

    QFile out(outfile);
    if (out.open(QFile::WriteOnly)) {
        out.write(data);
        out.close();
    }

    return true;

}

bool compressFile(QString infile, QString outfile)
{
    if (outfile.isEmpty()) {
        outfile = infile + ".gz";
    } else if (!outfile.endsWith(".gz")) {
        outfile += ".gz";
    }
    if (QFile::exists(outfile)) {
        qDebug() << "compressFile()" << outfile << "already exists";
        return false;
    }

    QFile f(infile);

    if (!f.exists(infile)) {
        qDebug() << "compressFile()" << infile << "does not exist";
        return false;
    }

    qint64 size = f.size();

    if (!f.open(QFile::ReadOnly)) {
        qDebug() << "compressFile() Couldn't open" << infile;
        return false;
    }

    QByteArray buf = f.read(size);
    f.close();

    if (buf.size() != size) {
        qDebug() << "compressFile() Couldn't read all of" << infile;
        return false;
    }

    QByteArray gz;
    if (!gzipEncode(buf, gz)) {
        qDebug() << "compressFile() Couldn't compress" << infile;
        return false;
    }

    // Write through QFile rather than gzopen() so non-ASCII paths work (GitLab #350)
    QFile out(outfile);
    if (!out.open(QFile::WriteOnly)) {
        qDebug() << "compressFile() Couldn't open" << outfile << "for writing";
        return false;
    }
    if (out.write(gz) != gz.size()) {
        qDebug() << "compressFile() Couldn't write all of" << outfile;
        out.close();
        out.remove();
        return false;
    }
    out.close();
    return true;
}

int MachineLoader::Open(const QStringList & paths)
{
    int i = 0;
    int size = paths.size();
    for (i=0; i < size; i++) {
        if (isAborted()) {
            break;
        }
        QString filename = paths[i];

        int res = OpenFile(filename);
        if (res < 0) {
            break;
        }
        // A file that imported nothing before the abort (e.g. a dismissed
        // chooser) doesn't count as opened.
        if (res == 0 && isAborted()) {
            break;
        }
        emit setProgressValue(i+1);
        QCoreApplication::processEvents();
    }
    return i;
}
