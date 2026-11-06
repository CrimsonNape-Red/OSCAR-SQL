/* Image trimming
 *
 * Device pictures are drawn on canvases with wide empty margins, usually square canvases
 * around a wide device. These helpers find the visible part so a picture can be shown
 * without its margins, leaving the image files themselves unchanged.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_IMAGETRIM_H
#define UIKIT_IMAGETRIM_H

#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QString>

namespace UiKit {

/*! \brief Returns the smallest rectangle holding every visible pixel of \a image
    A pixel is background when it is (nearly) transparent or (nearly) white in every
    channel, so pictures with white backgrounds trim the same way as transparent ones.
    Returns an empty rectangle when the image has no visible pixels. */
QRect visibleRect(const QImage &image);

/*! \brief Returns \a content grown on every side by \a margin times its longer side, kept
           inside an image of \a bounds */
QRect paddedRect(const QRect &content, const QSize &bounds, qreal margin);

/*! \brief Returns the picture at \a path with its empty margins removed, keeping a small even
           margin around the visible part
    Results are cached per path. Returns a null pixmap when the file can't be loaded. */
QPixmap trimmedPixmap(const QString &path);

} // namespace UiKit

#endif // UIKIT_IMAGETRIM_H
