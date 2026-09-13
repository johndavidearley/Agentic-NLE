#pragma once
#include <QApplication>
#include <QFileInfo>
#include <QFontDatabase>
inline void prepare_test_font(QApplication &app) {
    for (const auto &path : {qEnvironmentVariable("WINDIR") + "/Fonts/segoeui.ttf",
                             QString("/System/Library/Fonts/Supplemental/Arial.ttf"),
                             QString("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")}) {
        if (!QFileInfo::exists(path))
            continue;
        const auto id = QFontDatabase::addApplicationFont(path);
        if (id >= 0) {
            app.setFont(QFont(QFontDatabase::applicationFontFamilies(id).front(), 10));
            return;
        }
    }
}
