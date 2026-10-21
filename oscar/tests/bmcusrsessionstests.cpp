/* BMC USR Session Chain Unit Tests
 *
 * Tests that the legacy BMC loader reads every historic session in a .USR file, including
 * those after a headerless record that breaks the 'next session' pointer chain (#343).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "bmcusrsessionstests.h"

#include <QByteArray>
#include <QDate>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>

#include "SleepLib/loader_plugins/bmcDataParsing.h"

namespace {

//! Offset of the first historic session record in a .USR file
constexpr int kSessionsStart = 0x102340;
//! Length of the smallest record ReadHistoricSession() accepts: header fields up to the
//! 0x45 message list, then its 0xFF terminator (1 type byte + 4 data bytes).
constexpr int kRecordLength = 0x4A;

//! Exposes the protected USR reader for testing.
class TestableBmcData : public BmcData
{
public:
    using BmcData::BmcData;
    using BmcData::ReadAllSessions;
};

//! Encode a date the way BmcEncodedDate::DecodeDate() expects it.
quint16 encodeDate(const QDate &date)
{
    return static_cast<quint16>(((date.year() - 2000) << 9) | (date.month() << 5) | date.day());
}

/**
 * @brief Build a minimal historic session record
 * @param date Session date
 * @param next File offset written as the record's 'next session' pointer
 */
QByteArray makeRecord(const QDate &date, quint32 next)
{
    QByteArray rec(kRecordLength, '\0');
    rec[0] = static_cast<char>(0xE1);
    qToLittleEndian<quint32>(next, rec.data() + 1);
    qToLittleEndian<quint16>(encodeDate(date), rec.data() + 0x07);
    qToLittleEndian<quint16>(360, rec.data() + 0x0f);   // duration, minutes
    rec[0x45] = static_cast<char>(0xFF);                 // empty message list
    return rec;
}

/**
 * @brief Write a .USR file holding an in-progress session and the given historic area
 * @param dir     Directory to write 12345678.USR into
 * @param history Bytes placed at kSessionsStart
 */
bool writeUsr(const QString &dir, const QByteArray &history)
{
    QByteArray usr(kSessionsStart, '\0');
    qToLittleEndian<quint16>(encodeDate(QDate(2026, 10, 7)), usr.data() + 0x431);
    usr[0x441] = static_cast<char>(0xFF);                // in-progress session has no messages
    usr.append(history);

    QFile f(dir + QStringLiteral("/12345678.USR"));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(usr) == usr.size();
}

QList<QDate> sessionDates(const QList<BmcUsrSession> &sessions)
{
    QList<QDate> dates;
    for (const auto &s : sessions)
        dates.append(s.StartTimestamp.date());
    return dates;
}

} // namespace

void BmcUsrSessionsTests::testSessionsAfterHeaderlessRecordsAreRead()
{
    // Mirrors the card in #343: two records, a headerless fragment that the second
    // record's 'next' points at, one record whose 'next' points at another fragment,
    // then the rest of the chain to the end of the file.
    const QByteArray fragmentA(289, '\0');
    QByteArray fragmentB(583, '\0');
    fragmentB[0] = 0x03;

    QByteArray history;
    auto at = [&history]() { return static_cast<quint32>(kSessionsStart + history.size()); };

    history.append(makeRecord(QDate(2025, 12, 16), at() + kRecordLength));
    history.append(makeRecord(QDate(2025, 12, 17), at() + kRecordLength));
    history.append(fragmentA);
    history.append(makeRecord(QDate(2025, 12, 21), at() + kRecordLength));
    history.append(fragmentB);
    history.append(makeRecord(QDate(2025, 12, 23), at() + kRecordLength));
    history.append(makeRecord(QDate(2026, 10, 2), at() + kRecordLength));   // ends at EOF

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeUsr(dir.path(), history));

    TestableBmcData data(dir.path());
    data.ReadAllSessions();

    const QList<QDate> expected {
        QDate(2025, 12, 16), QDate(2025, 12, 17), QDate(2025, 12, 21),
        QDate(2025, 12, 23), QDate(2026, 10, 2),
        QDate(2026, 10, 7)   // in-progress session, appended last
    };
    QCOMPARE(sessionDates(data.AllUsrSessions), expected);
}

void BmcUsrSessionsTests::testLastRecordWithUnsetNextPointer()
{
    QByteArray history;
    auto at = [&history]() { return static_cast<quint32>(kSessionsStart + history.size()); };

    history.append(makeRecord(QDate(2026, 9, 28), at() + kRecordLength));
    history.append(makeRecord(QDate(2026, 9, 29), 0xFFFFFFFF));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeUsr(dir.path(), history));

    TestableBmcData data(dir.path());
    data.ReadAllSessions();

    const QList<QDate> expected { QDate(2026, 9, 28), QDate(2026, 9, 29), QDate(2026, 10, 7) };
    QCOMPARE(sessionDates(data.AllUsrSessions), expected);
}
