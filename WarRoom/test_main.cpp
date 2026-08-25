#include <QApplication>
#include <QMainWindow>

int main_(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QMainWindow w;
    w.setWindowTitle("Bare Test");
    w.resize(400, 300);
    w.show();
    return app.exec();
}