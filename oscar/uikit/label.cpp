/* Themed labels
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "label.h"

#include <QLabel>

namespace UiKit {

QLabel *makeLabel(const QString &text, const QFont &font, Theme::Role colour, QWidget *parent, bool wrap)
{
    auto *label = new QLabel(text, parent);
    label->setFont(font);
    label->setWordWrap(wrap);
    label->setTextFormat(Qt::PlainText);
    setLabelColour(label, colour);
    return label;
}

void setLabelColour(QLabel *label, Theme::Role colour)
{
    setLabelColour(label, Theme::color(colour));
}

void setLabelColour(QLabel *label, const QColor &colour)
{
    // A stylesheet rather than the palette: native styles may ignore palette text colours
    QString c = colour.alpha() == 255
        ? colour.name()
        : QString("rgba(%1, %2, %3, %4)").arg(colour.red()).arg(colour.green()).arg(colour.blue()).arg(colour.alpha());
    // Keep the label's own font: a new stylesheet can make the label re-resolve its font
    QFont font = label->font();
    label->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg(c));
    label->setFont(font);
}

QLabel *makeChip(const QString &text, Theme::Role foreground, Theme::Role background, QWidget *parent)
{
    auto *chip = new QLabel(text, parent);
    chip->setTextFormat(Qt::PlainText);
    QFont font = Theme::font(0.96);
    chip->setStyleSheet(QString("QLabel { color: %1; background: %2; border-radius: 11px; padding: 3px 10px; }")
                            .arg(Theme::css(foreground), Theme::css(background)));
    chip->setFont(font);
    chip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return chip;
}

QLabel *makeSwatch(Theme::Role colour, QWidget *parent)
{
    auto *swatch = new QLabel(parent);
    swatch->setFixedSize(9, 9);
    swatch->setStyleSheet(QString("QLabel { background: %1; border-radius: 3px; }").arg(Theme::css(colour)));
    return swatch;
}

} // namespace UiKit
