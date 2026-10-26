// anyfileqt/app/icons.h
//
// The command bar and menu icons, drawn with QPainter rather than loaded.
//
// WHY DRAWN: there is no icon set that is present on all three platforms.
// QIcon::fromTheme("edit-cut") works on a Linux desktop and returns nothing on
// macOS and Windows, and QStyle's SP_TrashIcon renders as an almost blank
// sheet on macOS. Bundled SVGs would need the Qt SVG icon-engine plugin
// deployed beside the app on every platform -- one more thing for the bundle
// scripts to forget.
//
// Each icon is a few lines on a 24x24 grid, stroked in the palette's text
// colour AT PAINT TIME, so they follow light and dark mode and grey out
// correctly on a disabled action. Crisp at any size and any device pixel
// ratio, because nothing is scaled from a bitmap.
#pragma once

#include <QIcon>

namespace anyfile {

enum class Glyph {
    NewFolder,
    Cut,
    Copy,
    Paste,
    Rename,
    Delete,
    Details,
    Icons,
    Hidden,
    Dual,
    Properties,
    Terminal,
};

QIcon glyphIcon(Glyph g);

}  // namespace anyfile
