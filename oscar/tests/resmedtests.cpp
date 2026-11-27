/* ResMed Unit Tests
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "resmedtests.h"
#include "sessiontests.h"
#include "../SleepLib/machine_loader.h"
#include <QTemporaryDir>
#include <QRandomGenerator>
#include <tuple>
#if QT_VERSION >= QT_VERSION_CHECK(5,15,0)
    #define QTHEX     Qt::hex
    #define QTDEC     Qt::dec
    #define QT_SKIP_EMPTY_PARTS Qt::SkipEmptyParts
#else
    #define QTHEX     hex
    #define QTDEC     dec
    #define QT_SKIP_EMPTY_PARTS QString::SkipEmptyParts
#endif


#define TESTDATA_PATH "./testdata/"

static ResmedLoader* s_loader = nullptr;
static void iterateTestCards(const QString & root, void (*action)(const QString &));
static QString resmedOutputPath(const QString & inpath, int session, const QString & suffix);

void ResmedTests::initTestCase(void)
{
    p_profile = new Profile(TESTDATA_PATH "profile/", false);
    p_profile->session->setBackupCardData(false);

    p_pref = new Preferences("Preferences");
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);

    schema::init();
    ResmedLoader::Register();
    s_loader = dynamic_cast<ResmedLoader*>(lookupLoader(resmed_class_name));
}

void ResmedTests::cleanupTestCase(void)
{
    delete AppSetting;
    delete p_profile;
    p_profile = nullptr;
    delete p_pref;
}


// ====================================================================================================

static QString s_currentPath;

static void emitSessionYaml(ResmedLoader* /*loader*/, Session* session)
{
    // Emit the parsed session data to compare against our regression benchmarks
    QString outpath = resmedOutputPath(s_currentPath, session->session(), "-session.yml");
    SessionToYaml(outpath, session, true);
}

static void parseAndEmitSessionYaml(const QString & path)
{
    qDebug() << path;

    // TODO: Refactor Resmed so that passing callbacks and using static globals isn't
    // necessary for testing. Both are used for now in order to introduce the minimal
    // set of changes into the Resmed loader needed for testing.
    s_currentPath = path;
    s_loader->OpenWithCallback(path, emitSessionYaml);
}

void ResmedTests::testSessionsToYaml()
{
    iterateTestCards(TESTDATA_PATH "resmed/input/", parseAndEmitSessionYaml);
}

// Defined in resmed_loader.cpp; not declared in a header.
EDFduration getEDFDuration(const QString & filename);

// GitLab #350: the backup's gzip helpers used gzopen(path.toLatin1()), which fails
// when the data folder contains non-Latin-1 characters. Round-trip a file through
// compressFile / uncompressFile / getEDFDuration inside such a folder.
void ResmedTests::testGzipNonAsciiPath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QString dir = tmp.path() + QStringLiteral(u"/\u6587\u4ef6");   // "文件"
    QVERIFY(QDir().mkpath(dir));

    // Minimal EDF header: start date/time at 0xa8, 10 records of 60 s each,
    // followed by binary data with high bytes and too random to compress.
    QByteArray edf(256, ' ');
    edf.replace(0xa8, 16, "19.09.2610.56.07");
    edf.replace(0xec, 8, "10      ");
    edf.replace(0xf4, 8, "60      ");
    QRandomGenerator rng(350);
    for (int i = 0; i < 65536; i++) {
        edf.append(char(rng.bounded(256)));
    }

    QString plain = dir + "/20260919_105607_BRP.edf";
    QFile f(plain);
    QVERIFY(f.open(QFile::WriteOnly));
    f.write(edf);
    f.close();

    QVERIFY(compressFile(plain));
    QVERIFY(QFile::exists(plain + ".gz"));

    QString restored = dir + "/restored/20260919_105607_BRP.edf";
    QVERIFY(QDir().mkpath(dir + "/restored"));
    QVERIFY(uncompressFile(plain + ".gz", restored));
    QFile r(restored);
    QVERIFY(r.open(QFile::ReadOnly));
    QCOMPARE(r.readAll(), edf);
    r.close();

    EDFduration plainDur = getEDFDuration(plain);
    EDFduration gzDur = getEDFDuration(plain + ".gz");
    QVERIFY(gzDur.start != 0);
    QCOMPARE(gzDur.start, plainDur.start);
    QCOMPARE(gzDur.end, plainDur.end);
    QCOMPARE(gzDur.end - gzDur.start, quint32(600));
}


// ====================================================================================================
// GitLab #363: ResMed writes events into EVE.edf up to a minute after the session's waveform
// files stop, and counts them in its own indices. OSCAR dropped them because they fell outside
// the session's [first, last] range.

void ResmedTests::testEveGraceEnd()
{
    const qint64 last = 1000000000LL;
    const qint64 grace = ResmedLoader::EVE_GRACE_AFTER_END_MS;
    using Spans = QList<QPair<qint64, qint64>>;

    // Alone on the day: events up to a minute after the end belong to the session.
    QCOMPARE(ResmedLoader::eventGraceEnd(last, Spans()), last + grace);

    // A session that ended earlier has no effect.
    QCOMPARE(ResmedLoader::eventGraceEnd(last, Spans{ { last - 7200000, last - 3600000 } }), last + grace);

    // The next session starting inside the minute ends the grace just before its start.
    QCOMPARE(ResmedLoader::eventGraceEnd(last, Spans{ { last + 20000, last + 3600000 } }), last + 19999);

    // A session starting after the minute has no effect.
    QCOMPARE(ResmedLoader::eventGraceEnd(last, Spans{ { last + grace + 1, last + 3600000 } }), last + grace);

    // Another session still running at this one's end: no grace at all.
    QCOMPARE(ResmedLoader::eventGraceEnd(last, Spans{ { last - 60000, last + 60000 } }), last);

    // A session without waveform data has no end to extend.
    QCOMPARE(ResmedLoader::eventGraceEnd(0, Spans()), qint64(0));
}

// ====================================================================================================
// GitLab #376: re-importing a day purged every stored session, so a session imported from
// another SD card (cards swapped between the halves of a night) was lost. Only the stored
// sessions that the new files overlap may be replaced.

void ResmedTests::testSessionsOverlappedByFiles()
{
    const qint64 h = 3600000;
    const qint64 t0 = 1000000000000LL;
    using Spans = QList<QPair<qint64, qint64>>;
    using Indices = QList<int>;

    // Card swap: first session stored, the new card holds only a later session -> keep it.
    QCOMPARE(ResmedLoader::sessionsOverlappedByFiles(Spans{ { t0, t0 + 4*h } },
                                                     Spans{ { t0 + 5*h, t0 + 7*h } }), Indices());

    // Same card, session grew since the last import -> replace it.
    QCOMPARE(ResmedLoader::sessionsOverlappedByFiles(Spans{ { t0, t0 + 2*h } },
                                                     Spans{ { t0, t0 + 4*h } }), Indices{ 0 });

    // Two stored sessions, the new files cover only the second.
    QCOMPARE(ResmedLoader::sessionsOverlappedByFiles(Spans{ { t0, t0 + 2*h }, { t0 + 3*h, t0 + 4*h } },
                                                     Spans{ { t0 + 3*h, t0 + 5*h }, { t0 + 6*h, t0 + 7*h } }),
             Indices{ 1 });

    // A file starting exactly where a stored session ends doesn't overlap it.
    QCOMPARE(ResmedLoader::sessionsOverlappedByFiles(Spans{ { t0, t0 + 2*h } },
                                                     Spans{ { t0 + 2*h, t0 + 3*h } }), Indices());

    // No new waveform files -> nothing replaced.
    QCOMPARE(ResmedLoader::sessionsOverlappedByFiles(Spans{ { t0, t0 + 2*h } }, Spans()), Indices());
}

/*! \brief Write a minimal ResMed-style EVE.edf holding the given annotations
 *
 * One "EDF Annotations" signal and one data record, as ResMed writes them. Each entry of
 * \a events is (offset in seconds, duration in seconds, text).
 */
static QByteArray makeEveEdf(const QString & startDateTime,
                             const QList<std::tuple<int, int, QString>> & events)
{
    QByteArray tal("+0\x14\x14\x00", 5);    // record time-keeping annotation
    for (const auto & ev : events) {
        tal += QString("+%1\x15%2\x14%3\x14").arg(std::get<0>(ev)).arg(std::get<1>(ev))
                   .arg(std::get<2>(ev)).toLatin1();
        tal += '\0';
    }
    if (tal.size() % 2) tal += '\0';
    const int samples = tal.size() / 2;

    auto field = [](const QString & s, int width) { return s.leftJustified(width, ' ', true).toLatin1(); };
    QByteArray edf;
    edf += field("0", 8);
    edf += field("X X X X", 80);
    edf += field("Startdate 31-OCT-2026 X X X", 80);
    edf += field(startDateTime, 16);          // dd.MM.yyHH.mm.ss
    edf += field("512", 8);
    edf += field("EDF+C", 44);
    edf += field("1", 8);                     // one data record
    edf += field("0", 8);                     // annotation-only record
    edf += field("1", 4);                     // one signal
    edf += field("EDF Annotations", 16);
    edf += field("", 80);
    edf += field("", 8);
    edf += field("-1", 8);
    edf += field("1", 8);
    edf += field("-32768", 8);
    edf += field("32767", 8);
    edf += field("", 80);
    edf += field(QString::number(samples), 8);
    edf += field("", 32);
    edf += tal;
    return edf;
}

void ResmedTests::testEveEventPastSessionEnd()
{
    // matchSignal() needs the ResMed annotation names, which initChannels() registers
    // (only once: registering the channels twice logs duplicate-channel errors).
    if (!schema::channel.names.contains("RMS9_Mode")) {
        s_loader->initChannels();
    }

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + "/20261031_232742_EVE.edf";

    // The night from #363: waveforms 23:27:47 for 7800 s; the only event, an obstructive
    // apnea, is logged 7822 s after the EVE start, 19 s after the waveforms stop. A second
    // event 120 s after the waveforms stop is beyond the grace and stays excluded.
    const QString startText = "31.10.2623.27.44";
    QFile f(path);
    QVERIFY(f.open(QFile::WriteOnly));
    f.write(makeEveEdf(startText, { { 7822, 11, "Obstructive Apnea" },
                                    { 7923, 0, "Hypopnea" } }));
    f.close();

    const qint64 eveStart = QDateTime(QDate(2026, 10, 31), QTime(23, 27, 44), EDFInfo::localNoDST)
                                .toMSecsSinceEpoch();
    const qint64 first = eveStart + 3000;
    const qint64 last = first + 7800000;
    const qint64 oaTime = eveStart + 7822000;

    CPAP mach(p_profile, 363);

    // Without a grace end (the old behaviour), the event past the end is dropped.
    {
        Session sess(&mach, SessionID(first / 1000));
        sess.really_set_first(first);
        sess.really_set_last(last);
        QVERIFY(s_loader->LoadEVE(&sess, path));
        QCOMPARE(sess.count(CPAP_Obstructive), EventDataType(0));
        QCOMPARE(sess.realLast(), last);
    }

    // With the grace end, the apnea is kept and the session now ends at it; the hypopnea
    // two minutes past the end is still excluded.
    {
        Session sess(&mach, SessionID(first / 1000));
        sess.really_set_first(first);
        sess.really_set_last(last);
        const qint64 graceEnd = ResmedLoader::eventGraceEnd(last, {});
        QVERIFY(s_loader->LoadEVE(&sess, path, graceEnd));
        QCOMPARE(sess.count(CPAP_Obstructive), EventDataType(1));
        QCOMPARE(sess.count(CPAP_Hypopnea), EventDataType(0));
        QCOMPARE(sess.realLast(), oaTime);
    }
}


// ====================================================================================================
// GitLab #425: a session recorded while the SD card was out of the device has a mask-on/off
// pair in STR.edf but no DATALOG files. It was dropped whenever the same day had other
// sessions with files, so the day's usage was under-reported.

void ResmedTests::testMaskPairsWithoutData()
{
    const qint64 h = 3600000;
    const qint64 t0 = 1000000000000LL;
    using Spans = QList<QPair<qint64, qint64>>;
    using Indices = QList<int>;

    // Three sessions, the card was out for the middle one.
    QCOMPARE(ResmedLoader::maskPairsWithoutData(
                 Spans{ { t0, t0 + 3*h }, { t0 + 4*h, t0 + 5*h }, { t0 + 6*h, t0 + 8*h } },
                 Spans{ { t0, t0 + 3*h }, { t0 + 6*h, t0 + 8*h } }), Indices{ 1 });

    // Every pair has files: nothing to add.
    QCOMPARE(ResmedLoader::maskPairsWithoutData(
                 Spans{ { t0, t0 + 3*h }, { t0 + 4*h, t0 + 5*h } },
                 Spans{ { t0, t0 + 3*h }, { t0 + 4*h, t0 + 5*h } }), Indices());

    // The previous session's files run 20 s into the pair (STR times are whole minutes):
    // the pair is still a session of its own.
    QCOMPARE(ResmedLoader::maskPairsWithoutData(
                 Spans{ { t0, t0 + 3*h }, { t0 + 3*h, t0 + 4*h } },
                 Spans{ { t0, t0 + 3*h + 20000 } }), Indices{ 1 });

    // A session stored from another SD card covers the pair: importing it as a summary
    // would duplicate that session.
    QCOMPARE(ResmedLoader::maskPairsWithoutData(
                 Spans{ { t0, t0 + 3*h }, { t0 + 4*h, t0 + 5*h } },
                 Spans{ { t0 + 30000, t0 + 3*h - 15000 }, { t0 + 4*h, t0 + 5*h } }), Indices());

    // Files that matched no pair but hold the pair's session (they made a group of their own).
    QCOMPARE(ResmedLoader::maskPairsWithoutData(
                 Spans{ { t0, t0 + 3*h } },
                 Spans{ { t0 + 10*60000, t0 + 3*h + 10*60000 } }), Indices());

    // Pairs with a missing time, or that don't end after they start, are never sessions.
    QCOMPARE(ResmedLoader::maskPairsWithoutData(
                 Spans{ { t0, 0 }, { 0, t0 + h }, { t0 + 2*h, t0 + 2*h }, { t0 + 4*h, t0 + 3*h } },
                 Spans()), Indices());

    // Nothing covered: every valid pair is returned.
    QCOMPARE(ResmedLoader::maskPairsWithoutData(
                 Spans{ { t0, t0 + h }, { t0 + 2*h, t0 + 3*h } }, Spans()), (Indices{ 0, 1 }));
}

/*! \brief Write a minimal ResMed-style waveform EDF: one "Crc16" signal, one sample per record
 *
 * Enough for the loader to take the recording's start and length from it. Holds no therapy
 * signal, so the session it produces has times but no waveforms.
 */
static QByteArray makeWaveEdf(const QString & startDateTime, int records, int secondsPerRecord)
{
    auto field = [](const QString & s, int width) { return s.leftJustified(width, ' ', true).toLatin1(); };
    QByteArray edf;
    edf += field("0", 8);
    edf += field("X X X X", 80);
    edf += field("Startdate 31-OCT-2026 X X X", 80);
    edf += field(startDateTime, 16);          // dd.MM.yyHH.mm.ss
    edf += field("512", 8);
    edf += field("", 44);
    edf += field(QString::number(records), 8);
    edf += field(QString::number(secondsPerRecord), 8);
    edf += field("1", 4);                     // one signal
    edf += field("Crc16", 16);
    edf += field("", 80);
    edf += field("", 8);
    edf += field("-32768", 8);
    edf += field("32767", 8);
    edf += field("-32768", 8);
    edf += field("32767", 8);
    edf += field("", 80);
    edf += field("1", 8);                     // one sample per record
    edf += field("", 32);
    edf += QByteArray(records * 2, '\0');
    return edf;
}

static QList<Session *> s_savedSessions;

static void collectSession(ResmedLoader* /*loader*/, Session* session)
{
    s_savedSessions.append(session);
}

//! Deletes the sessions collected by collectSession() when a test ends, pass or fail.
struct SavedSessionsCleanup {
    ~SavedSessionsCleanup() { qDeleteAll(s_savedSessions); s_savedSessions.clear(); }
};

//! Mask-on time (s since epoch) of the first (0) or second (1) session of the card-out day.
static quint32 cardOutMaskOn(int index)
{
    const quint32 first = QDateTime(QDate(2026, 10, 31), QTime(23, 27, 0), EDFInfo::localNoDST)
                              .toSecsSinceEpoch();
    return first + index * 3 * 3600;
}

/*! \brief Run a ResMed day with two STR mask-on/off pairs, of which only the first has a file
 *
 * First pair: 23:27 for two hours, with a waveform file starting at 23:27:42. Second pair:
 * 02:27 for 23 minutes, no file (the SD card was out). The sessions the loader saves are
 * left in s_savedSessions.
 * \param mach             device the sessions belong to; must outlive them
 * \param storedSessionsMs sessions already stored for the day, passed on to the task
 */
static void runCardOutDay(Machine & mach, const QList<QPair<qint64, qint64>> & storedSessionsMs)
{
    if (!schema::channel.names.contains("RMS9_Mode")) {
        s_loader->initChannels();
    }
    s_savedSessions.clear();

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString name = "20261031_232742_BRP.edf";
    const QString path = tmp.path() + "/" + name;
    QFile f(path);
    QVERIFY(f.open(QFile::WriteOnly));
    f.write(makeWaveEdf("31.10.2623.27.42", 100, 60));     // 100 minutes
    f.close();

    ResMedDay resday(QDate(2026, 10, 31));
    resday.str.date = resday.date;
    resday.str.maskon = { cardOutMaskOn(0), cardOutMaskOn(1) };
    resday.str.maskoff = { cardOutMaskOn(0) + 2 * 3600, cardOutMaskOn(1) + 23 * 60 };
    resday.str.maskevents = 4;
    resday.str.oai = 2.0;                   // the day's obstructive apnea index
    resday.files[name] = path;

    ResDayTask task(s_loader, &mach, &resday, collectSession);
    task.storedSessionsMs = storedSessionsMs;
    task.run();
}

void ResmedTests::testCardOutSessionImportedAsSummary()
{
    SavedSessionsCleanup cleanup;
    CPAP mach(p_profile, 425);
    runCardOutDay(mach, {});
    if (QTest::currentTestFailed()) return;

    // The session with the file, then the card-out session as a summary-only session.
    QCOMPARE(s_savedSessions.size(), 2);
    Session *detail = s_savedSessions[0];
    QCOMPARE(detail->session(), SessionID(cardOutMaskOn(0)));
    QVERIFY(!detail->summaryOnly());

    Session *summary = s_savedSessions[1];
    QCOMPARE(summary->session(), SessionID(cardOutMaskOn(1)));
    QVERIFY(summary->summaryOnly());
    QCOMPARE(summary->first(), qint64(cardOutMaskOn(1)) * 1000);
    QCOMPARE(summary->last(), qint64(cardOutMaskOn(1) + 23 * 60) * 1000);
    // Events are estimated from the day's index, as on a day with no files at all.
    QVERIFY(qAbs(summary->count(CPAP_Obstructive) - 2.0 * 23.0 / 60.0) < 0.001);
}

// Card swap (#376): the session of the second pair is already stored, imported from another
// card. It must not be imported again as a summary-only session with the same session id.
void ResmedTests::testCardOutSessionNotDuplicated()
{
    SavedSessionsCleanup cleanup;
    CPAP mach(p_profile, 425);
    // Stored from its waveform files: starts a little after mask-on, ends a little before mask-off.
    const qint64 storedFirst = qint64(cardOutMaskOn(1)) * 1000 + 20000;
    const qint64 storedLast = qint64(cardOutMaskOn(1) + 23 * 60) * 1000 - 5000;
    runCardOutDay(mach, { { storedFirst, storedLast } });
    if (QTest::currentTestFailed()) return;

    QCOMPARE(s_savedSessions.size(), 1);
    QCOMPARE(s_savedSessions[0]->session(), SessionID(cardOutMaskOn(0)));
    QVERIFY(!s_savedSessions[0]->summaryOnly());
}


// ====================================================================================================

QString resmedOutputPath(const QString & inpath, int session, const QString & suffix)
{
    // Output to resmed/output/FOLDER/000000(-session.yml, etc.)
    QDir path(inpath);
    QStringList pathlist = QDir::toNativeSeparators(inpath).split(QDir::separator(), QT_SKIP_EMPTY_PARTS);
    QString foldername = pathlist.last();

    QDir outdir(TESTDATA_PATH "resmed/output/" + foldername);
    outdir.mkpath(".");
    
    QString filename = QString("%1%2")
                        .arg(session)
                        .arg(suffix);
    return outdir.path() + QDir::separator() + filename;
}

void iterateTestCards(const QString & root, void (*action)(const QString &))
{
    QDir dir(root);
    dir.setFilter(QDir::NoDotAndDotDot | QDir::Dirs);
    dir.setSorting(QDir::Name);
    QFileInfoList flist = dir.entryInfoList();

    // Look through each folder in the given root
    for (auto & fi : flist) {
        // If it contains a DATALOG folder, it's a ResMed SD card
        QDir datalog(fi.canonicalFilePath() + QDir::separator() + "DATALOG");
        if (datalog.exists()) {
            // Confirm that it contains the file that the ResMed loader expects
            QFileInfo idfile(fi.canonicalFilePath() + QDir::separator() + "Identification.tgt");
            if (idfile.exists()) {
                action(fi.canonicalFilePath());
            }
        }
    }
}
