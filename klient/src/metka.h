#ifndef METKA_H
#define METKA_H

// Отметки запуска -- только для сборки-щупа.
//
// На телефоне нет ни консоли, ни отладчика, а журнал приложения лежит в его
// папке, которая создаётся уже внутри запуска: если приложение умирает
// раньше, журнала нет вовсе, и видно только «ничего не произошло». Поэтому
// каждый шаг запуска оставляет отдельный пустой файл прямо в C:\Data --
// по тому, какой файл последний, видно, на чём всё кончилось.
//
// В обычной сборке отметок нет: они включаются определением JF_METKI
// (`build-symbian.bat ru metki`). Иначе каждый запуск сорил бы
// тринадцатью файлами в C:\Data у всех.
//
// Писать нарочно через QFile: open/write/close уже есть в импорте рабочей
// сборки, значит отметки не добавляют образу ни одной новой зависимости и
// не могут сами стать причиной незапуска. stdio (fopen) добавил бы импорт
// из Open C, которого на этом телефоне может не быть.

#include <QFile>
#include <QIODevice>

static inline void metka(const char *imya)
{
#if defined(Q_OS_SYMBIAN) && defined(JF_METKI)
    QFile file(QString::fromLatin1("C:/Data/") + QString::fromLatin1(imya));
    if (file.open(QIODevice::WriteOnly)) {
        file.write("x", 1);
        file.close();
    }
#else
    (void)imya;
#endif
}

#endif // METKA_H
