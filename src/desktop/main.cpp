#include "desktop/style.hpp"
#include "desktop/window.hpp"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Agentic NLE");
    app.setStyle("Fusion");
    nle::desktop::apply_style(app);
    QString ffprobe = "ffprobe", project;
    const auto args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--ffprobe" && i + 1 < args.size())
            ffprobe = args[++i];
        else
            project = args[i];
    }
    auto worker = QDir(QCoreApplication::applicationDirPath()).filePath("nle-preview-worker");
#ifdef _WIN32
    worker += ".exe";
#endif
    nle::desktop::Window window(
        worker, ffprobe, true,
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery");
    window.show();
    if (!project.isEmpty())
        try {
            window.openProject(project);
        } catch (const std::exception &error) {
            QMessageBox::critical(&window, "Cannot open project", QString::fromUtf8(error.what()));
        }
    if (project.isEmpty())
        (void)window.restoreDrafts();
    return app.exec();
}
