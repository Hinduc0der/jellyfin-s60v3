#include <QApplication>
#include <QTextCodec>

#include "jfwindow.h"
#include "metka.h"

int main(int argc, char *argv[])
{
    metka("jf-00-do-app.txt");
    QApplication app(argc, argv);
    metka("jf-01-app.txt");

    // Qt 4 читает литералы в коде как Latin-1. Без этих двух строк весь
    // русский текст в приложении превращается в кракозябры -- проверено на
    // Kutegram, там это записано отдельным пунктом.
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));

    metka("jf-02-kodeki.txt");
    JfWindow window;
    metka("jf-06-okno-gotovo.txt");
#ifdef Q_OS_SYMBIAN
    window.showMaximized();
    metka("jf-07-pokazano.txt");
#else
    window.resize(240, 320);
    window.show();
#endif
    return app.exec();
}
