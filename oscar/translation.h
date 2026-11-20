/* Multilingual Support header
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef TRANSLATION_H
#define TRANSLATION_H

#include <QString>
#include <QFileDialog>
const QString DefaultLanguage = "en_US";
const QString LangSetting = "Settings/Language";

void initTranslations(bool batchMode = false);
QString currentLanguage();
QString lookupLanguageName(QString language);

//! Returns QFileDialog::DontUseNativeDialog when the active OSCAR language differs
//! from the OS locale language, so Qt can translate dialog button labels, or when the
//! OS would draw its dialog in the other light/dark scheme from the one OSCAR is showing
//! (see ColorScheme::nativeDialogsMatch()).
//! Returns an empty Options otherwise -- native dialogs look and perform better.
QFileDialog::Options nativeDialogOption();

#endif // TRANSLATION_H
