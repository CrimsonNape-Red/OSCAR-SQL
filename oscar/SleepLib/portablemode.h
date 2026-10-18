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
 *  - every default-constructed QSettings reads and writes an INI file in the Settings
 *    folder next to the executable instead of the registry (or the platform's native
 *    settings store);
 *  - the default data folder is next to the executable instead of in Documents;
 *  - folder paths kept in settings are stored relative to the executable's folder when
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

    /*!
     * \brief Decide whether OSCAR runs in portable mode and, if so, redirect QSettings.
     *
     * Called from main() before QApplication and before any QSettings object exists,
     * so it works from argv: argv[0] locates the executable's folder. It also checks
     * that the Settings folder can be written; see setupError().
     */
    void initialize(int argc, char *argv[]);

    //! \brief True when OSCAR is running in portable mode.
    bool isEnabled();

    //! \brief True when portable mode was requested by --portable rather than the marker.
    bool enabledByOption();

    //! \brief Folder holding the OSCAR executable (and the marker), without a trailing '/'.
    QString rootPath();

    //! \brief Folder holding the portable settings INI file.
    QString settingsPath();

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
     * In portable mode, a path on the same drive as rootPath() becomes relative to it.
     * Otherwise, and whenever portable mode is off, the path is returned unchanged.
     */
    QString toStoredPath(const QString &path);

    /*!
     * \brief Convert a path read from settings back to an absolute path.
     *
     * In portable mode a relative path is resolved against rootPath(). Absolute paths,
     * and every path when portable mode is off, are returned unchanged.
     */
    QString fromStoredPath(const QString &stored);

    //! \brief toStoredPath() against an explicit root, independent of the current mode.
    QString encodePath(const QString &path, const QString &root);

    //! \brief fromStoredPath() against an explicit root, independent of the current mode.
    QString decodePath(const QString &stored, const QString &root);
}

#endif // PORTABLEMODE_H
