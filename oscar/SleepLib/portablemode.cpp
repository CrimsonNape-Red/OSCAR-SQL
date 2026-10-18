/* Portable Mode - run OSCAR from a removable drive, with settings in an INI file
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "portablemode.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace PortableMode
{

const QString MarkerFileName    = QStringLiteral("portable.ini");
const QString CommandLineOption = QStringLiteral("--portable");
const QString DefaultDataFolder = QStringLiteral("../OSCAR Portable Data");

static const QString SettingsFolderName = QStringLiteral("Settings");
static const QString DataFolderKey      = QStringLiteral("DataFolder");

static bool    s_enabled = false;
static bool    s_byOption = false;
static bool    s_firstUse = false;
static bool    s_defaultDataRoot = true;   // the data folder is where DefaultDataFolder puts it
static QString s_root;
static QString s_dataRoot;
static QString s_setupError;

/*!
 * \brief Folder of the running executable, worked out from argv[0].
 *
 * QCoreApplication::applicationDirPath() is not available yet, because QSettings has
 * to be redirected before QApplication is constructed (the graphics engine is read from
 * settings first). Launched from Explorer or a shortcut, argv[0] is the full path. A bare
 * name typed at a prompt was found in the current folder (which Windows searches first)
 * or on PATH.
 */
static QString executableFolder(const char *argv0)
{
    const QString arg0 = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv0));
    if (arg0.contains(QLatin1Char('/')))
        return QDir::cleanPath(QFileInfo(arg0).absolutePath());

#ifdef Q_OS_WIN
    if (QFileInfo::exists(arg0) || QFileInfo::exists(arg0 + QStringLiteral(".exe")))
        return QDir::cleanPath(QDir::currentPath());
#endif
    const QString found = QStandardPaths::findExecutable(arg0);
    if (!found.isEmpty())
        return QDir::cleanPath(QFileInfo(found).absolutePath());
    return QDir::cleanPath(QDir::currentPath());
}

void initialize(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (!qstricmp(argv[i], CommandLineOption.toLatin1().constData())) {
            s_byOption = true;
            break;
        }
    }

    s_root = executableFolder(argc > 0 ? argv[0] : "");
    const QString markerPath = s_root + QLatin1Char('/') + MarkerFileName;
    const bool haveMarker = QFileInfo::exists(markerPath);
    s_enabled = haveMarker || s_byOption;
    if (!s_enabled)
        return;
    if (haveMarker)
        s_byOption = false;     // the marker alone keeps a restarted OSCAR portable

    QString setting;
    QFile marker(markerPath);
    if (haveMarker && marker.open(QIODevice::ReadOnly))
        setting = dataFolderSetting(marker.readAll());
    s_dataRoot = resolveDataRoot(setting, s_root);
    // The shipped marker spells the default out, so compare where it leads.
    s_defaultDataRoot = (s_dataRoot == resolveDataRoot(QString(), s_root));
    s_firstUse = !QFileInfo::exists(s_dataRoot);

    // Redirect even if the folder turns out to be read-only: main() then reports the
    // problem and exits, and nothing is written to this computer's settings meanwhile.
    // Both scopes point at the portable folder so that QSettings' fall-back reads of
    // system-wide settings do not pick anything up from this computer either.
    const QString settingsDir = settingsPath();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDir);

    // QFileInfo::isWritable() does not see Windows access rules, so try writing a file.
    QFile probe(settingsDir + QStringLiteral("/write-test.tmp"));
    if (!QDir().mkpath(settingsDir) || !probe.open(QIODevice::WriteOnly))
        s_setupError = QDir::toNativeSeparators(settingsDir);
    else
        probe.remove();
}

bool isEnabled()
{
    return s_enabled;
}

bool enabledByOption()
{
    return s_enabled && s_byOption;
}

QString rootPath()
{
    return s_root;
}

QString dataRootPath()
{
    return s_dataRoot;
}

QString settingsPath()
{
    return s_dataRoot + QLatin1Char('/') + SettingsFolderName;
}

bool firstUse()
{
    return s_enabled && s_firstUse;
}

QString zipNamedParentFolder()
{
    return (firstUse() && s_defaultDataRoot) ? zipNamedParent(s_root) : QString();
}

void removeUnusedFolders()
{
    // rmdir() removes only an empty folder, so nothing OSCAR has saved can go.
    if (firstUse()) {
        QDir().rmdir(settingsPath());
        QDir().rmdir(s_dataRoot);
    }
}

QString setupError()
{
    return s_setupError;
}

QStringList restartArguments()
{
    return enabledByOption() ? QStringList(CommandLineOption) : QStringList();
}

// Relative to the portable data folder rather than the program folder: everything kept
// there moves with it, which on platforms without drive letters is the only case that
// can be stored relative (see encodePath()).
QString toStoredPath(const QString &path)
{
    return s_enabled ? encodePath(path, s_dataRoot) : path;
}

QString fromStoredPath(const QString &stored)
{
    return s_enabled ? decodePath(stored, s_dataRoot) : stored;
}

QString encodePath(const QString &path, const QString &root)
{
    if (path.isEmpty() || root.isEmpty() || QDir::isRelativePath(path))
        return path;

    const QString relative = QDir(root).relativeFilePath(QDir::cleanPath(path));
#ifdef Q_OS_WIN
    // relativeFilePath() gives back an absolute path when the two are on different
    // drives; anything it could make relative is on the portable drive.
    const bool onPortableDrive = QDir::isRelativePath(relative);
#else
    // There are no drive letters, so only a path inside the portable folder is sure to
    // move with it; a mount point can change, and "../.." would follow the wrong one.
    const bool onPortableDrive = QDir::isRelativePath(relative)
                                 && relative != QStringLiteral("..")
                                 && !relative.startsWith(QStringLiteral("../"));
#endif
    return onPortableDrive ? relative : path;
}

QString decodePath(const QString &stored, const QString &root)
{
    if (stored.isEmpty() || root.isEmpty() || !QDir::isRelativePath(stored))
        return stored;
    return QDir::cleanPath(root + QLatin1Char('/') + stored);
}

QString dataFolderSetting(const QByteArray &markerContents)
{
    const QStringList lines = QString::fromUtf8(markerContents).split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('#')))
            continue;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq < 0 || line.left(eq).trimmed().compare(DataFolderKey, Qt::CaseInsensitive) != 0)
            continue;
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2).trimmed();
        return QDir::fromNativeSeparators(value);
    }
    return QString();
}

QString resolveDataRoot(const QString &setting, const QString &programRoot)
{
    const QString folder = setting.isEmpty() ? DefaultDataFolder : QDir::fromNativeSeparators(setting);
    if (!QDir::isRelativePath(folder))
        return QDir::cleanPath(folder);

    // QDir::cleanPath() keeps a ".." it cannot resolve ("E:/../x"), so walk the
    // components here and stop climbing at the root. (Done on the path text: QDir::cdUp()
    // only moves to a folder that exists.)
    QString base = QDir::cleanPath(programRoot);
    QStringList rest;
    for (const QString &part : folder.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part == QStringLiteral("."))
            continue;
        if (part == QStringLiteral("..")) {
            if (!rest.isEmpty())
                rest.removeLast();
            else if (!QDir(base).isRoot())
                base = QFileInfo(base).path();
            continue;
        }
        rest << part;
    }
    return QDir::cleanPath(base + QLatin1Char('/') + rest.join(QLatin1Char('/')));
}

QString zipNamedParent(const QString &programRoot)
{
    const QString program = QDir::cleanPath(programRoot);
    if (program.isEmpty() || QDir(program).isRoot())
        return QString();
    const QString parent = QFileInfo(program).path();
    if (QDir(parent).isRoot())
        return QString();
    const QString name = QFileInfo(parent).fileName();
    if (name.startsWith(QStringLiteral("OSCAR-"), Qt::CaseInsensitive)
        && name.contains(QStringLiteral("-Portable"), Qt::CaseInsensitive))
        return parent;
    return QString();
}

} // namespace PortableMode
