/* Portable Mode - run OSCAR from a removable drive, with settings in an INI file
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PORTABLEMODE_H
#define PORTABLEMODE_H

#include <QString>
#include <QStringList>

/*!
 * \namespace PortableMode
 * \brief Lets OSCAR run from a folder on a USB stick or SD card without leaving its
 *        settings on the computer it happens to be plugged into.
 *
 * Portable mode is switched on by a marker file (portable.ini) next to the OSCAR
 * executable, or by the --portable command line option. When it is on:
 *  - everything the user keeps (settings and data) goes in a portable data folder, by
 *    default "OSCAR Portable Data" beside the program folder, so that upgrading means
 *    replacing the program folder; the marker's DataFolder line can move it;
 *  - every default-constructed QSettings reads and writes an INI file in the Settings
 *    folder inside the portable data folder instead of the registry (or the platform's
 *    native settings store); the registry key Qt's file dialogs write regardless is
 *    removed again on Windows if OSCAR created it (removeQtRegistryKey());
 *  - the default data folder (OSCAR20_Data) is in the portable data folder instead of
 *    in Documents;
 *  - folder paths kept in settings are stored relative to the portable data folder when
 *    they are on the same drive, so they still resolve when the drive is given a
 *    different letter on another computer.
 *
 * initialize() must run before the first QSettings object is created. See
 * Notes/Developer Notes/PORTABLE_MODE_DESIGN.md.
 */
namespace PortableMode
{
    //! \brief Name of the marker file that switches portable mode on.
    extern const QString MarkerFileName;

    //! \brief Command line option that switches portable mode on without a marker file.
    extern const QString CommandLineOption;

    //! \brief Where the portable data folder is when the marker does not say: beside the
    //!        program folder, relative to it.
    extern const QString DefaultDataFolder;

    /*!
     * \brief Decide whether OSCAR runs in portable mode and, if so, redirect QSettings.
     *
     * Called from main() before QApplication and before any QSettings object exists,
     * so it works from argv: argv[0] locates the executable's folder. It reads the
     * marker's DataFolder line and checks that the Settings folder can be written,
     * creating it if necessary; see setupError().
     */
    void initialize(int argc, char *argv[]);

    //! \brief True when OSCAR is running in portable mode.
    bool isEnabled();

    //! \brief True when portable mode was requested by --portable rather than the marker.
    bool enabledByOption();

    //! \brief Folder holding the OSCAR executable (and the marker), without a trailing '/'.
    QString rootPath();

    //! \brief Portable data folder: holds the Settings folder and the default data folder.
    QString dataRootPath();

    //! \brief Folder holding the portable settings INI file.
    QString settingsPath();

    /*!
     * \brief True when the portable data folder did not exist when OSCAR started: this
     *        copy of OSCAR Portable has not been used before.
     */
    bool firstUse();

    /*!
     * \brief The folder named after the portable ZIP that holds the program folder, when
     *        the portable data folder would be created inside it; otherwise empty.
     *
     * Windows' "Extract All" puts the program folder in a folder named after the ZIP,
     * such as OSCAR-2.1.0-Win64-Portable. Data kept beside the program folder would then
     * sit inside that version-named folder, and a newer version extracted the same way
     * would not find it. Only reported on first use and with the default DataFolder.
     */
    QString zipNamedParentFolder();

    /*!
     * \brief On first use, remove the Settings and portable data folders again if they are
     *        still empty; for when OSCAR exits before using them.
     */
    void removeUnusedFolders();

    /*!
     * \brief On Windows, remove HKEY_CURRENT_USER\Software\QtProject if it did not exist
     *        when portable OSCAR started; does nothing otherwise, or after the first call.
     *
     * Qt's file dialogs save their last folder, history and view mode there with
     * QSettings(QSettings::UserScope, "QtProject"), which always uses the registry, so
     * the INI redirection cannot catch it. A key that was already there may belong to
     * another Qt program and is left alone. Call when OSCAR exits, and before it starts
     * a new copy of itself so that the new copy finds the key gone and removes the one
     * it creates in turn.
     */
    void removeQtRegistryKey();

    //! \brief Reason portable mode cannot work (folder not writable), or empty if it can.
    QString setupError();

    /*!
     * \brief Extra arguments a restarted OSCAR needs to stay in portable mode.
     *
     * Returns --portable when that option is what enabled portable mode; a marker file
     * is found again by the new process, so nothing is needed then.
     */
    QStringList restartArguments();

    /*!
     * \brief Convert a path to the form kept in settings.
     *
     * In portable mode, a path on the same drive as dataRootPath() becomes relative to it.
     * Otherwise, and whenever portable mode is off, the path is returned unchanged.
     */
    QString toStoredPath(const QString &path);

    /*!
     * \brief Convert a path read from settings back to an absolute path.
     *
     * In portable mode a relative path is resolved against dataRootPath(). Absolute paths,
     * and every path when portable mode is off, are returned unchanged.
     */
    QString fromStoredPath(const QString &stored);

    //! \brief toStoredPath() against an explicit root, independent of the current mode.
    QString encodePath(const QString &path, const QString &root);

    //! \brief fromStoredPath() against an explicit root, independent of the current mode.
    QString decodePath(const QString &stored, const QString &root);

    /*!
     * \brief The DataFolder value in the marker file's contents, or empty if there is none.
     *
     * Read as plain text rather than through QSettings, which would treat the backslashes
     * of a Windows path as escape characters. Lines starting with ';' or '#' are comments,
     * section headers are ignored, and surrounding quotes are removed.
     */
    QString dataFolderSetting(const QByteArray &markerContents);

    /*!
     * \brief The portable data folder for a DataFolder value (empty for the default),
     *        relative to the program folder unless absolute.
     *
     * ".." above a drive root stays at the root, as it does in the file system, so a
     * program folder at the top of a drive keeps its data folder inside it.
     */
    QString resolveDataRoot(const QString &setting, const QString &programRoot);

    /*!
     * \brief The parent of \a programRoot if its name looks like a portable ZIP's name
     *        (OSCAR-...-Portable...) and it is not a drive root; otherwise empty.
     */
    QString zipNamedParent(const QString &programRoot);
}

#endif // PORTABLEMODE_H
