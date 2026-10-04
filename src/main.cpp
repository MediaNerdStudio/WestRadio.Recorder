#include <QApplication>
#include <QIcon>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <QtGlobal>
#include <QCommandLineParser>
#include "MainWindow.h"

static const char *kStyleSheet = R"(
QMainWindow, QWidget {
    background-color: #15171c;
    color: #cfd3d9;
}
QFrame {
    background-color: #22262d;
    border: 1px solid #2e333b;
}
QLabel {
    background: transparent;
    border: none;
    color: #cfd3d9;
}
QLabel[dim="true"] {
    color: #8a9099;
}
QPushButton {
    background-color: #2a2f37;
    border: 1px solid #3a4049;
    border-radius: 3px;
    padding: 4px 10px;
    color: #cfd3d9;
}
QPushButton:hover {
    background-color: #343a44;
}
QPushButton:pressed {
    background-color: #22262d;
}
QPushButton:disabled {
    color: #5a6069;
    background-color: #22262d;
}
QPushButton:checked {
    background-color: #3d8bff;
    color: #ffffff;
    border-color: #3d8bff;
}
QLineEdit, QSpinBox {
    background-color: #0f1114;
    border: 1px solid #2e333b;
    border-radius: 3px;
    padding: 3px 5px;
    color: #cfd3d9;
    selection-background-color: #3d8bff;
}
QLineEdit:focus, QSpinBox:focus {
    border-color: #3d8bff;
}
QCheckBox, QRadioButton {
    background: transparent;
    color: #cfd3d9;
    spacing: 5px;
}
QCheckBox::indicator, QRadioButton::indicator {
    width: 13px;
    height: 13px;
    border: 1px solid #3a4049;
    background-color: #0f1114;
}
QRadioButton::indicator {
    border-radius: 7px;
}
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background-color: #3d8bff;
    border-color: #3d8bff;
}
QScrollArea {
    background-color: #15171c;
    border: none;
}
QScrollBar:horizontal {
    background: #15171c;
    height: 8px;
}
QScrollBar:vertical {
    background: #15171c;
    width: 8px;
}
QScrollBar::handle:horizontal, QScrollBar::handle:vertical {
    background: #3a4049;
    border-radius: 4px;
    min-height: 20px;
    min-width: 20px;
}
QScrollBar::handle:hover {
    background: #4a515c;
}
QScrollBar::add-line, QScrollBar::sub-line {
    width: 0px;
    height: 0px;
}
QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
}
QMenu {
    background-color: #22262d;
    border: 1px solid #2e333b;
    color: #cfd3d9;
}
QMenu::item:selected {
    background-color: #3d8bff;
    color: #ffffff;
}
QMenuBar {
    background-color: #22262d;
    color: #cfd3d9;
    border-bottom: 1px solid #2e333b;
}
QMenuBar::item {
    background: transparent;
    padding: 4px 10px;
}
QMenuBar::item:selected {
    background-color: #2e333b;
}
QMenuBar::item:pressed {
    background-color: #3d8bff;
    color: #ffffff;
}
QToolTip {
    background-color: #22262d;
    border: 1px solid #3a4049;
    color: #cfd3d9;
}
)";

static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context);
    if (type != QtWarningMsg && type != QtCriticalMsg && type != QtFatalMsg)
        return;

    static const QString logDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(logDir);
    QFile file(logDir + QStringLiteral("/recorder.log"));
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << ' ' << msg << '\n';
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("WestRadio Recorder"));
    app.setOrganizationName(QStringLiteral("WestRadio"));
    app.setWindowIcon(QIcon(QStringLiteral(":/AppIcon.png")));
    app.setStyle(QStringLiteral("Fusion"));
    app.setStyleSheet(QString::fromLatin1(kStyleSheet));

    qInstallMessageHandler(messageHandler);

    QCommandLineParser parser;
    QCommandLineOption configOption(QStringLiteral("config"),
                                    QStringLiteral("Load this .wrrec.json config at startup."),
                                    QStringLiteral("path"));
    parser.addOption(configOption);
    parser.addHelpOption();
    parser.process(app);

    MainWindow window(nullptr, parser.value(configOption));
    window.show();

    return app.exec();
}
