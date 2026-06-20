#include "ui/WarRoomMainWindow.h"
#include "ui/LinkCreationManager.h"
#include <QtWidgets/QApplication>
#include <QDebug>

int main(int argc, char* argv[])
{
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);

    WarRoomMainWindow window;
    window.show();

    qDebug() << "[DESTDBG] main: entering app.exec()";
    int ret = app.exec();
    qDebug() << "[DESTDBG] main: app.exec() returned" << ret
             << "| &window =" << static_cast<void*>(&window);
    // window 在此处离开作用域 -> 析构链开始
    return ret;
}
