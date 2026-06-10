#include "ui/WarRoomMainWindow.h"
#include "ui/LinkCreationManager.h"
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    WarRoomMainWindow window;
    window.show();

    return app.exec();
}
