/* CSV oximetry import
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <algorithm>

#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>
#include <QTimeZone>

#include "csvoximetry.h"
#include "SleepLib/calcs.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"
#include "SleepLib/session.h"

namespace CsvOximetry {

const char *const LOADER_NAME = "CSVOximetry";

static QString tr(const char *text)
{
    return QCoreApplication::translate("CsvOximetry", text);
}

static const int MAX_PROBLEMS = 10;

//! The field separator: whichever of tab, semicolon and comma the line has most of.
static QChar separatorOf(const QString &line)
{
    const int tabs = int(line.count(QLatin1Char('\t')));
    const int semis = int(line.count(QLatin1Char(';')));
    const int commas = int(line.count(QLatin1Char(',')));
    if (tabs > 0 && tabs >= semis && tabs >= commas) {
        return QLatin1Char('\t');
    }
    if (semis > commas) {
        return QLatin1Char(';');
    }
    return QLatin1Char(',');
}

//! The fields of a line. A field in double quotes may hold the separator (O2 Insight
//! Pro's "10:59:17PM Jul 15, 2026"); "" inside quotes is a quote.
static QStringList splitFields(const QString &line, QChar sep)
{
    QStringList fields;
    QString field;
    bool quoted = false;
    for (int i = 0; i < line.size(); i++) {
        const QChar c = line[i];
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line[i + 1] == QLatin1Char('"')) {
                    field += c;
                    i++;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
        } else if (c == sep) {
            fields << field.trimmed();
            field.clear();
        } else {
            field += c;
        }
    }
    fields << field.trimmed();
    return fields;
}

//! Other layouts seen in exports, read with English month names: O2 Insight Pro writes
//! "10:59:17PM Jul 15, 2026".
static QDateTime parseOtherDateTime(const QString &text)
{
    static const char *const FORMATS[] = {
        "hh:mm:ssAP MMM d, yyyy", "hh:mm:ss AP MMM d, yyyy", "hh:mm:ssAP MMM d yyyy",
        "MMM d, yyyy hh:mm:ssAP", "MMM d, yyyy hh:mm:ss AP", "MMM d, yyyy HH:mm:ss",
    };
    const QString t = text.simplified();
    for (const char *f : FORMATS) {
        const QDateTime dt = QLocale::c().toDateTime(t, QString::fromLatin1(f));
        if (dt.isValid()) {
            return dt;
        }
    }
    return QDateTime();
}

//! yyyy-MM-dd HH:mm[:ss[.zzz]], "T" or space between, optional offset; local time.
static QDateTime parseDateTime(QString text)
{
    text = text.trimmed();
    static const QRegularExpression re(QStringLiteral(
        "^(\\d{4})-(\\d{1,2})-(\\d{1,2})[T ](\\d{1,2}):(\\d{2})(?::(\\d{2})(?:[.,](\\d{1,3})\\d*)?)?"
        "\\s*(Z|[+-]\\d{2}:?\\d{2})?$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return parseOtherDateTime(text);
    }
    const QDate date(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    QString ms = m.captured(7);
    while (!ms.isEmpty() && ms.size() < 3) {
        ms += QLatin1Char('0');
    }
    const QTime time(m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt(), ms.toInt());
    if (!date.isValid() || !time.isValid()) {
        return QDateTime();
    }
    const QString zone = m.captured(8);
    if (zone.isEmpty()) {
        return QDateTime(date, time);   // local time, as oximeters record it
    }
    int offset = 0;
    if (zone.compare(QLatin1String("Z"), Qt::CaseInsensitive) != 0) {
        QString digits = zone.mid(1);
        digits.remove(QLatin1Char(':'));
        offset = (digits.left(2).toInt() * 60 + digits.mid(2, 2).toInt()) * 60;
        if (zone.startsWith(QLatin1Char('-'))) {
            offset = -offset;
        }
    }
    return QDateTime(date, time, QTimeZone(offset)).toLocalTime();
}

//! A reading, or 0 for none (empty, not a number, or out of range).
static int reading(const QString &field, int low, int high)
{
    bool ok = false;
    const double v = QString(field).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    if (!ok || v < low || v > high) {
        return 0;
    }
    return int(v + 0.5);
}

Result parse(const QString &text, int gapSeconds)
{
    Result result;
    QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r\\n|\\n|\\r")));

    // The first line with content decides the separator and whether there is a header.
    int first = 0;
    while (first < lines.size() && lines[first].trimmed().isEmpty()) {
        first++;
    }
    if (first >= lines.size()) {
        result.error = tr("The file is empty.");
        return result;
    }
    if (lines[first].startsWith(QChar(0xFEFF))) {
        lines[first].remove(0, 1);
    }
    const QChar sep = separatorOf(lines[first]);
    int colDate = -1, colTime = -1, colDateTime = 0, colSpo2 = 1, colPulse = 2;
    const QStringList head = splitFields(lines[first], sep);
    if (!head.isEmpty() && !parseDateTime(head.first()).isValid()) {
        // A header: find the columns by name.
        colDateTime = colSpo2 = colPulse = -1;
        for (int i = 0; i < head.size(); i++) {
            const QString h = head[i].toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
            if (colSpo2 < 0 && (h.contains(QLatin1String("spo2")) || h.contains(QLatin1String("sat"))
                                || h.contains(QLatin1String("oxygen")) || h == QLatin1String("o2"))) {
                colSpo2 = i;
            } else if (colPulse < 0 && (h.contains(QLatin1String("pulse")) || h.contains(QLatin1String("heart"))
                                        || h == QLatin1String("pr") || h == QLatin1String("hr")
                                        || h.startsWith(QLatin1String("pr(")) || h.startsWith(QLatin1String("hr("))
                                        || h.contains(QLatin1String("bpm")))) {
                colPulse = i;
            } else if (h.contains(QLatin1String("date")) && h.contains(QLatin1String("time"))) {
                colDateTime = i;
            } else if (h.contains(QLatin1String("date")) && colDate < 0) {
                colDate = i;
            } else if (h.contains(QLatin1String("time")) && colTime < 0) {
                colTime = i;
            }
        }
        if (colDateTime < 0 && colDate >= 0 && colTime < 0) {
            colDateTime = colDate;   // one "Date" column holding date and time
            colDate = -1;
        }
        if (colDateTime < 0 && colDate < 0 && colTime >= 0) {
            colDateTime = colTime;   // one "Time" column holding date and time
            colTime = -1;
        }
        const bool haveTime = colDateTime >= 0 || (colDate >= 0 && colTime >= 0);
        if (!haveTime || colSpo2 < 0 || colPulse < 0) {
            result.error = tr("The header doesn't name the columns OSCAR needs: date and time, SpO2 and pulse.");
            return result;
        }
        first++;
    }

    QVector<Sample> samples;
    for (int n = first; n < lines.size(); n++) {
        if (lines[n].trimmed().isEmpty()) {
            continue;
        }
        result.rows++;
        const QStringList f = splitFields(lines[n], sep);
        auto field = [&f](int i) { return (i >= 0 && i < f.size()) ? f[i] : QString(); };
        const QString when = colDateTime >= 0 ? field(colDateTime)
                                              : field(colDate) + QLatin1Char(' ') + field(colTime);
        Sample s;
        s.time = parseDateTime(when);
        if (!s.time.isValid()) {
            result.badRows++;
            if (result.problems.size() < MAX_PROBLEMS) {
                result.problems << tr("line %1: date and time not understood (\"%2\")").arg(n + 1).arg(when);
            }
            continue;
        }
        s.spo2 = reading(field(colSpo2), 50, 100);
        s.pulse = reading(field(colPulse), 20, 250);   // 255 is a common "no reading"
        samples.append(s);
    }

    if (samples.isEmpty()) {
        result.error = result.rows == 0 ? tr("The file has no readings.")
                                        : tr("None of the rows could be read.");
        return result;
    }
    if (result.badRows * 2 > result.rows) {
        result.error = tr("Most rows could not be read; is this the right kind of file?");
        return result;
    }

    // Time order; a repeated time keeps its last row.
    std::stable_sort(samples.begin(), samples.end(),
                     [](const Sample &a, const Sample &b) { return a.time < b.time; });
    QVector<Sample> unique;
    for (const Sample &s : samples) {
        if (!unique.isEmpty() && unique.last().time == s.time) {
            unique.last() = s;
        } else {
            unique.append(s);
        }
    }

    // Split where the readings pause for longer than the gap limit.
    Recording rec;
    for (const Sample &s : unique) {
        if (!rec.samples.isEmpty() && rec.samples.last().time.secsTo(s.time) > gapSeconds) {
            result.recordings << rec;
            rec = Recording();
        }
        rec.samples.append(s);
    }
    result.recordings << rec;

    // A recording without a single reading is of no use.
    for (int i = result.recordings.size() - 1; i >= 0; i--) {
        const auto &smp = result.recordings[i].samples;
        const bool any = std::any_of(smp.begin(), smp.end(), [](const Sample &s) { return s.spo2 > 0 || s.pulse > 0; });
        if (!any) {
            result.recordings.removeAt(i);
        }
    }
    if (result.recordings.isEmpty()) {
        result.error = tr("The file has no SpO2 or pulse readings.");
    }
    return result;
}

Result read(const QString &path, int gapSeconds)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Result r;
        r.error = tr("Could not open the file: %1").arg(file.errorString());
        return r;
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    return parse(in.readAll(), gapSeconds);
}

Machine *machine(const QString &rawName)
{
    const QString name = rawName.trimmed();
    for (Machine *m : p_profile->GetMachines(MT_OXIMETER)) {
        if (m->loaderName() == QLatin1String(LOADER_NAME) && m->serial() == name) {
            return m;
        }
    }
    // A fresh ID: Profile::CreateMachine() would otherwise reuse the ID of any earlier
    // CSV machine's data folder, merging two named oximeters into one.
    MachineID id = 0;
    bool taken = true;
    while (taken) {
        id = QRandomGenerator::global()->generate();
        taken = (id == 0);
        for (Machine *m : p_profile->GetMachines()) {
            taken = taken || m->id() == id;
        }
    }
    MachineInfo info(MT_OXIMETER, 0, QString::fromLatin1(LOADER_NAME), tr("Oximeter"),
                     name.isEmpty() ? tr("CSV import") : name, QString(), name, tr("CSV"),
                     QDateTime::currentDateTime(), 1);
    return p_profile->CreateMachine(info, id);
}

//! Adds a channel's readings as events at their own times, ending a section at a gap.
static void addChannel(Session *session, ChannelID code, const QVector<Sample> &samples,
                       int Sample::*value)
{
    EventList *el = nullptr;
    int last = 0;
    qint64 lastTime = 0;
    for (const Sample &s : samples) {
        const qint64 t = s.time.toMSecsSinceEpoch();
        const int v = s.*value;
        if (v > 0) {
            if (!el) {
                el = session->AddEventList(code, EVL_Event);
            }
            if (v != last) {
                if (last > 0) {
                    el->AddEvent(t, last);
                }
                el->AddEvent(t, v);
            }
            lastTime = t;
        } else if (last > 0) {
            el->AddEvent(t, last);   // end the section where the readings stop
            session->setLast(code, t);
        }
        last = v;
    }
    if (el && last > 0) {
        el->AddEvent(lastTime, last);
        session->setLast(code, lastTime);
    }
}

Session *buildSession(Machine *mach, const Recording &rec)
{
    if (!mach || rec.samples.isEmpty()) {
        return nullptr;
    }
    const SessionID sid = SessionID(rec.start().toUTC().toSecsSinceEpoch());
    auto *session = new Session(mach, sid);
    session->really_set_first(rec.start().toMSecsSinceEpoch());

    addChannel(session, OXI_SPO2, rec.samples, &Sample::spo2);
    addChannel(session, OXI_Pulse, rec.samples, &Sample::pulse);

    calcSPO2Drop(session);
    calcPulseChange(session);
    for (ChannelID code : { OXI_Pulse, OXI_SPO2, OXI_PulseChange, OXI_SPO2Drop }) {
        session->first(code);
        session->last(code);
        session->count(code);
    }
    for (ChannelID code : { OXI_PulseChange, OXI_SPO2Drop }) {
        session->cph(code);
        session->sph(code);
    }
    for (ChannelID code : { OXI_Pulse, OXI_SPO2 }) {
        session->Min(code);
        session->Max(code);
        session->avg(code);
        session->wavg(code);
    }
    session->really_set_last(rec.end().toMSecsSinceEpoch());
    session->SetChanged(true);
    session->setOpened(true);
    return session;
}

} // namespace CsvOximetry
