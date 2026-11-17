/* Tall capture
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tallcapture.h"

#include <cmath>
#include <cstring>

namespace TallCapture {

int graphAreaHeight(const QVector<float> &heights, float gap, int needed)
{
    // The same stacking as gGraphView::renderGraphs(): each graph starts on a whole pixel
    float py = 0;
    for (float h : heights) {
        if (py >= needed) break;                                // starts below the line: not drawn
        if (py + h >= needed) return int(std::ceil(py + h));    // crosses it: drawn whole, the last one
        py = std::ceil(py + h + gap);
    }
    return needed;
}

int cappedPageHeight(int wanted, int onScreen, qreal devicePixelRatio)
{
    const int cap = int(MaxDevicePixels / qMax(devicePixelRatio, qreal(0.1)));
    return qMax(onScreen, qMin(wanted, cap));
}

QImage insertRows(const QImage &image, int y, int rows)
{
    if (rows <= 0 || image.isNull()) return image;
    y = qBound(1, y, image.height());

    QImage out(image.width(), image.height() + rows, image.format());
    if (out.isNull()) return image;     // too large to allocate: leave the image as it was
    out.setDevicePixelRatio(image.devicePixelRatio());
    if (image.colorCount() > 0) out.setColorTable(image.colorTable());

    // Both images have the same width and format, so a row is the same number of bytes in each
    const size_t rowBytes = size_t(qMin(image.bytesPerLine(), out.bytesPerLine()));
    for (int row = 0; row < out.height(); ++row) {
        const int source = row < y ? row : (row < y + rows ? y - 1 : row - rows);
        std::memcpy(out.scanLine(row), image.constScanLine(source), rowBytes);
    }
    return out;
}

} // namespace TallCapture
