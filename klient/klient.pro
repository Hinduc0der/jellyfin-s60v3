# Клиент Jellyfin для Nokia E90.
#
# Что это и чем отличается от пробы в ../videoproba. Та проба отвечала на
# вопросы: рисует ли системная медиа-часть в наше окно, работает ли перемотка,
# принимает ли RealPlayer наш поток. Ответы получены и записаны в ЗАМЕТКИ.md.
# Здесь из них собирается то, чем можно пользоваться.
#
# Устройство продукта, коротко:
#   * видео из библиотеки -- поток 800x352 отдаётся системному плееру по адресу
#     rtsp://<сервер>/jf/<запись>/at<секунда>; место выбирает ЭТО приложение,
#     у RealPlayer для этого ничего нет;
#   * файлы с карты -- свой плеер внутри приложения, с паузой и перемоткой;
#   * вход -- QuickConnect: на телефоне ничего не набирается, кроме адреса
#     сервера один раз.

# Внутреннее имя файла. Пользователю оно НЕ ВИДНО: в списке установленных
# программ стоит имя ПАКЕТА (header ниже), в меню -- подпись из ресурса
# (DEPLOYMENT.display_name). 0.1.10 это доказывает: файл JfE90, пакет
# «Jellyfin for S60», в меню «Jellyfin».
#
# Менять его можно, но незачем, а цена есть: от TARGET зависят имена всех
# четырёх устанавливаемых файлов, и обновление поверх установленного
# прежнего имени оставляет на телефоне мусор.
#
# В 0.2.0 оно было переименовано в Jellyfin, и та сборка не запускалась --
# но причина оказалась совсем другая (нулевой _api в конструкторе окна,
# см. ЗАМЕТКИ, раздел 0.2.3). Переименование было оправдано сравнением
# пакетов побайтно и к незапуску отношения не имело.
TARGET = JfE90
VERSION = 0.3.22
PKG_VERSION = $$replace(VERSION, "\.", ",")

# Версия -- ВНУТРЬ кода, а не только в имя файла.
#
# Без этого узнать, какая сборка стоит на телефоне, нельзя ничем: в
# меню видно имя пакета, а не версию, и присланный журнал не говорит,
# чем он снят. Владелец спросил об этом прямо, разбирая беду, которую
# могло чинить или не чинить обновление.
# БЕЗ кавычек: экранирование, которое переживает mingw, не переживает
# препроцессор симбиановского инструментария -- сборка падала на
# «cpp.exe returned non-zero exit status». Поэтому сюда уходит голое
# число, а в строку его превращает сам код (JF_STR в slova.h).
DEFINES += JF_VERSION_RAW=$$VERSION

# phonon — свой звуковой плеер.
#
# Разведка 07.10.2026 («Проба 3» в ЗАМЕТКАХ): по ссылке http:// Phonon
# не играет никогда, зато готовый локальный файл играет хорошо, а
# видеочасти Phonon в сборке Qt на этом телефоне нет вовсе:
# приложение с Phonon::VideoWidget не запускается. Нам нужен только
# звук, поэтому берём phonon и не трогаем ничего из его видеочасти.
QT += core gui network phonon

# Разбор JSON: в Qt 4.7 его нет вовсе. Взят qt-json -- тот же, что в
# Kutegram, то есть проверенный на этом телефоне.
SOURCES += src/main.cpp src/jfapi.cpp src/jfwindow.cpp src/jfmusic.cpp \
           src/json.cpp
HEADERS += src/jfapi.h src/jfwindow.h src/jfmusic.h src/json.h src/metka.h \
           src/probadefaults.h \
           src/slova.h

symbian {
    # apgrfx/apmime -- запуск системного плеера.
    LIBS += -lapgrfx -lapmime

    # Значок и имя в меню телефона.
    #
    # До этого ни того, ни другого не было: в меню стояло «JfE90» с пустым
    # квадратом. Для своей пробы сойдёт, для продукта нет -- человек ищет
    # приложение глазами, а не по имени исполняемого файла.
    #
    # Значок -- SVG Tiny 1.1: инструментарий Symbian собирает .mif именно из
    # него, и всё, чего в этом профиле нет (прозрачность, заливки-переходы,
    # текст), либо выбрасывается, либо роняет сборку.
    ICON = src/icon.svg
    DEPLOYMENT.display_name = Jellyfin

    TARGET.UID3 = 0xE0713D56
    TARGET.CAPABILITY += NetworkServices ReadUserData WriteUserData UserEnvironment

    supported_platforms = \
            "[0x102032BE],0,0,0,{\"S60ProductID\"}" \ # Symbian 9.2
            "[0x102752AE],0,0,0,{\"S60ProductID\"}" \ # Symbian 9.3
            "[0x1028315F],0,0,0,{\"S60ProductID\"}" \ # Symbian^1
            "[0x20022E6D],0,0,0,{\"S60ProductID\"}" \ # Symbian^3
            "[0x2003A678],0,0,0,{\"S60ProductID\"}"   # Symbian Belle

    default_deployment.pkg_prerules -= pkg_platform_dependencies
    supported_platforms_deployment.pkg_prerules += supported_platforms
    DEPLOYMENT += supported_platforms_deployment

    vendor_info = " " "; Localised Vendor name" "%{\"Jellyfin\"}" " " "; Unique Vendor name" ":\"Jellyfin\"" " "
    package.pkg_prerules += vendor_info
    header = "$${LITERAL_HASH}{\"Jellyfin\"},(0xE0713D56),$$PKG_VERSION,TYPE=SA,RU"
    package.pkg_prerules += header
    DEPLOYMENT += package
    DEPLOYMENT.installer_header = "$${LITERAL_HASH}{\"Jellyfin Installer\"},(0xE5E0AFB5),$$PKG_VERSION"
}
