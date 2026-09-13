#pragma once
#include <QApplication>
namespace nle::desktop {
inline void apply_style(QApplication &app) {
    app.setStyle("Fusion");
    app.setStyleSheet(
        "QWidget{background:#1b2431;color:#d9e3ee;font-size:13px;} "
        "QToolBar{padding:8px;spacing:8px;border-bottom:1px solid #344256;} "
        "QPushButton{background:#29394c;border:1px solid #40546b;border-radius:4px;padding:7px "
        "12px;} QPushButton:hover{background:#385269;} QPushButton:disabled{color:#718093;} "
        "QLineEdit,QListWidget,QComboBox{background:#121b27;border:1px solid #34465d;padding:6px;} "
        "QListWidget::item{padding:10px;border-bottom:1px solid #233145;} "
        "QListWidget::item:selected{background:#245463;} "
        "QSlider::groove:horizontal{background:#34465d;height:4px;} "
        "QSlider::handle:horizontal{background:#69cfc6;width:12px;margin:-5px "
        "0;border-radius:5px;}");
}
} // namespace nle::desktop
