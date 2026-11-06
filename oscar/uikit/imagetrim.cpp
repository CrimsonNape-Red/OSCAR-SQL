/* Image trimming
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "imagetrim.h"

#include <QHash>

namespace UiKit {

namespace {
constexpr int kMinAlpha = 16;      //!< Pixels at or below this alpha are background
constexpr int kMinWhite = 246;     //!< Pixels with every channel at or above this are background
constexpr qreal kTrimMargin = 0.04; //!< Margin kept around the visible part, of its longer side

bool isBackground(QRgb px)
{
    return qAlpha(px) <= kMinAlpha
        || (qRed(px) >= kMinWhite && qGreen(px) >= kMinWhite && qBlue(px) >= kMinWhite);
}
}

QRect visibleRect(const QImage &image)
{
    const QImage img = image.convertToFormat(QImage::Format_ARGB32);
    int left = img.width(), top = img.height(), right = -1, bottom = -1;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (isBackground(line[x])) continue;
            left = qMin(left, x);
            right = qMax(right, x);
            top = qMin(top, y);
            bottom = qMax(bottom, y);
        }
    }
    if (right < 0) return QRect();
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

QRect paddedRect(const QRect &content, const QSize &bounds, qreal margin)
{
    const int pad = qRound(qMax(content.width(), content.height()) * margin);
    return content.adjusted(-pad, -pad, pad, pad) & QRect(QPoint(0, 0), bounds);
}

QPixmap trimmedPixmap(const QString &path)
{
    static QHash<QString, QPixmap> cache;
    auto it = cache.constFind(path);
    if (it != cache.constEnd()) return it.value();

    QImage image(path);
    QPixmap result;
    if (!image.isNull()) {
        QRect content = visibleRect(image);
        result = QPixmap::fromImage(content.isEmpty()
                                        ? image
                                        : image.copy(paddedRect(content, image.size(), kTrimMargin)));
    }
    cache.insert(path, result);
    return result;
}

} // namespace UiKit
