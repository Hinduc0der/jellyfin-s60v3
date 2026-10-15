#include "jfwindow.h"

#include "probadefaults.h"
#include "slova.h"
#include "metka.h"

#include <QNetworkConfiguration>
#include <QNetworkConfigurationManager>
#include <QNetworkSession>
#include "jfapi.h"
#include "jfmusic.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFileInfoList>
#include <QInputDialog>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QSettings>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

// Журнал рядом с настройками. На телефоне это единственный способ узнать,
// что произошло: ни консоли, ни отладчика нет, а установка каждой версии
// стоит владельцу подключения телефона и копирования на карту.
// Папка приложения на телефоне.
//
// Называлась по имени исполняемого файла -- JfE90. К первой публичной
// сборке и файл, и папка называются Jellyfin, а старая папка однократно
// переносится: в ней лежат адрес сервера и ключ доступа, и потерять их --
// это заставить человека заново входить по коду.
//
// Проверка сделана один раз за запуск: folder() зовётся на КАЖДУЮ строку
// журнала, а обращение к файловой системе на этом телефоне стоит дорого.
// Чем кончился перенос. Прямо здесь в журнал не напишешь: журнал сам зовёт
// folder(), вышло бы кольцо. Поэтому ответ откладывается до begin().
static QString perenosOtchet;

// Перенос настроек из папки прошлой версии.
//
// Отдельной функцией, а не двумя строками внутри folder(), ровно по одной
// причине: так её проходит и НАСТОЛЬНАЯ сборка. Эта ветка срабатывает только
// у того, кто ставил 0.1.x, то есть на телефоне её не видел никто ни разу --
// а молча не сработавший перенос означает вход по коду заново, с
// компьютером в руках.
static void perenesti(const QString &staroe, const QString &novoe)
{
    if (QFile::exists(novoe)) {
        perenosOtchet = QString::fromUtf8(S_PERENOS_NE_NUZHEN);
        return;
    }
    if (!QFile::exists(staroe)) {
        perenosOtchet = QString::fromUtf8(S_PERENOS_NECHEGO);
        return;
    }
    perenosOtchet = QString::fromUtf8(QFile::copy(staroe, novoe)
                                          ? S_PERENOS_GOTOV
                                          : S_PERENOS_NE_VYSHEL) + staroe;
}

static QString folder()
{
#ifdef Q_OS_SYMBIAN
    const QString teper = QString("C:/Data/Jellyfin");
    const QString staroe = QString("C:/Data/JfE90/settings.ini");
#else
    const QString teper = QDir::tempPath();
    // На ПК старая папка лежит рядом с новой -- так ту же самую ветку можно
    // пройти настольной сборкой, не трогая телефон.
    const QString staroe = QDir::tempPath() + QLatin1String("/JfE90/settings.ini");
#endif

    static bool gotovo = false;
    if (!gotovo) {
        gotovo = true;
        metka("jf-03a-folder.txt");
#ifdef Q_OS_SYMBIAN
        QDir("C:/Data").mkpath("Jellyfin");
#endif
        metka("jf-03b-mkpath.txt");
        // Папка первой версии называлась по имени исполняемого файла, и
        // настройки остались там. Переносим их однократно: в них адрес
        // сервера и ключ доступа, а вход по коду требует компьютера.
        perenesti(staroe, teper + QLatin1String("/settings.ini"));
        // И из прежнего РУССКОГО имени файла, если оно осталось от 0.3.x.
        //
        // Имена файлов и папок на диске должны быть английскими: `muzyka` и
        // `nastroyki` читает человек, открывший телефон файловым менеджером,
        // и выглядит это как чужая небрежность. Для «настроек» и «музыки»
        // английские слова есть -- значит отговорок нет.
        if (perenosOtchet == QString::fromUtf8(S_PERENOS_NECHEGO)) {
            perenesti(teper + QLatin1String("/nastroyki.ini"),
                      teper + QLatin1String("/settings.ini"));
        }
        metka("jf-03c-perenos.txt");
    }
    return teper;
}

static void writeLog(const QString &text)
{
    QFile file(folder() + "/jellyfin.log");
    // Метка UTF-8 в НАЧАЛЕ файла -- три байта EF BB BF.
    //
    // Без неё журнал нельзя прочитать на самом телефоне: просмотрщик Symbian
    // принимает его за однобайтовую кодировку, и весь русский текст
    // превращается в «Ð.Ð3ÑEÐ.ÐuÑ,». Владелец на это и наткнулся, когда
    // захотел посмотреть, отчего замолчала музыка: латинские названия песен
    // читались, а всё наше -- нет.
    //
    // Пишется один раз, в пустой файл. Разбору ничего не мешает: журнал
    // только дописывается и никем обратно не читается.
    // Журнал БЕЗ метки убираем в сторону и начинаем новый.
    //
    // Одной проверки «файл пуст» мало: у всех, кто уже пользуется
    // приложением, журнал существует и метки в нём нет -- значит на телефоне
    // он так и останется нечитаемым. Дописать метку в начало нельзя, её
    // место занято; переписывать файл целиком на 332 МГц незачем. Поэтому
    // старый журнал один раз переименовывается, а новый начинается с метки.
    //
    // Проверка делается ОДИН раз за запуск: writeLog зовётся на каждую
    // строку, и лазить в файловую систему каждый раз на этом телефоне дорого.
    static bool provereno = false;
    if (!provereno) {
        provereno = true;
        if (file.exists() && file.size() >= 3) {
            QFile proba(file.fileName());
            if (proba.open(QIODevice::ReadOnly)) {
                const QByteArray nachalo = proba.read(3);
                proba.close();
                const char metka[3] = { char(0xEF), char(0xBB), char(0xBF) };
                if (nachalo != QByteArray(metka, 3)) {
                    const QString staryy = file.fileName() + QLatin1String(".bez-metki");
                    QFile::remove(staryy);
                    QFile::rename(file.fileName(), staryy);
                }
            }
        }
    }

    const bool pustoy = !file.exists() || file.size() == 0;
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (pustoy) {
            // Байтами, а не строкой с экранированием: так не зависит ни от
            // кодировки исходника, ни от знака char.
            const char nachalo[3] = { char(0xEF), char(0xBB), char(0xBF) };
            file.write(nachalo, 3);
        }
        QTextStream stream(&file);
        stream.setCodec("UTF-8");
        stream << QDateTime::currentDateTime().toString("hh:mm:ss")
               << "  " << text << endl;
    }
}

// Время в виде 12:34. БЕЗ деления целых, нарочно: компилятор превращает
// деление в вызов __aeabi_idiv, которого на E90 нет, и приложение не
// запускается вовсе -- «функция не поддерживается». На это уже потрачена
// одна установка, подробности в ЗАМЕТКАХ.
static QString asClock(qint64 milliseconds)
{
    if (milliseconds < 0)
        milliseconds = 0;
    return QTime(0, 0).addMSecs(int(milliseconds)).toString(
        milliseconds >= 3600000 ? QString("h:mm:ss") : QString("m:ss"));
}

// Есть ли карта памяти и можно ли на неё писать.
//
// Зачем это вообще. Во внутренней памяти E90 свободно несколько десятков
// мегабайт -- на всё: сообщения, снимки, установленные программы. 0.3.0
// держала там до 40 МБ музыки и забила память телефона целиком. На карте
// места гигабайты, и музыке самое место там; в телефоне она остаётся только
// когда карты нет -- и тогда её держим впритык.
//
// Проверяем записью, а не наличием буквы: слот без карты всё равно
// откликается, а «только для чтения» карта бывает.
static bool estKarta()
{
#ifdef Q_OS_SYMBIAN
    static int otvet = -1;
    if (otvet >= 0)
        return otvet == 1;
    otvet = 0;
    if (QDir(QString("E:/")).exists()) {
        QDir(QString("E:/")).mkpath(QString("Data/Jellyfin/music"));
        QFile proba(QString("E:/Data/Jellyfin/music/proba.tmp"));
        if (proba.open(QIODevice::WriteOnly)) {
            proba.write("1");
            proba.close();
            proba.remove();
            otvet = 1;
        }
    }
    return otvet == 1;
#else
    return false;
#endif
}

// Папка под музыку и предел её объёма.
static QString muzPapka()
{
    if (estKarta())
        return QString("E:/Data/Jellyfin/music");
    return folder() + QLatin1String("/music");
}

static qint64 muzPredel()
{
    // На карте -- альбом с запасом. В телефоне -- ровно текущая песня и
    // следующая: при 128 кбит/с песня весит около 3 МБ.
    return estKarta() ? qint64(60) * 1024 * 1024 : qint64(7) * 1024 * 1024;
}

// Полоска из десяти клеток: [====------].
//
// БЕЗ ДЕЛЕНИЯ, нарочно. Деление целых компилятор превращает в вызов
// __aeabi_idiv, которого на E90 нет, -- приложение тогда не запускается
// вовсе, и одна установка на этом уже потеряна (ЗАМЕТКИ, про asClock).
// Сравнение с умножением обходится машинными действиями: умножение у
// процессора своё, а деления у него нет.
// `dlina` -- сколько знаков в самой полоске. Переменная нарочно: ширина
// строки в точках зависит от шрифта, а шрифт на телефоне свой и заранее
// неизвестен. Поэтому полоску подбирают по месту, меряя получившуюся
// строку, а не подгоняют число на глаз по снимку с чужого экрана.
static QString polosa(qint64 est, qint64 vsego, int dlina)
{
    if (vsego <= 0)
        return QString();
    if (est < 0)
        est = 0;
    if (est > vsego)
        est = vsego;
    if (vsego > 100000000)   // двое суток звучания: явно не песня
        return QString();
    // В миллисекундах десятикратная длина песни укладывается в обычное целое
    // с огромным запасом: даже час -- это 36 миллионов.
    const int a = int(est);
    const int b = int(vsego);
    QString line = QLatin1String("[");
    for (int i = 1; i <= dlina; ++i)
        line += QLatin1Char(a * dlina >= b * i ? '=' : '-');
    return line + QLatin1String("]");
}

// Сколько записи уже просмотрено. Единица времени у Jellyfin -- 100
// наносекунд, поэтому делим на 10000... но делить нельзя (см. выше), а
// 64-битное деление разрешено: оно идёт через другую служебную функцию,
// которая на телефоне есть и работает с самого начала.
static qint64 watchedMs(const QVariantMap &item)
{
    const QVariantMap seen = item.value("UserData").toMap();
    const qint64 ticks = seen.value("PlaybackPositionTicks").toLongLong();
    return ticks / Q_INT64_C(10000);
}

// Строка списка: что это и сколько просмотрено.
//
// `withSeries` -- подписывать ли серию названием сериала. В списке серий
// одного сезона оно не нужно и вредно: на внешнем экране 240 точек, и
// строка вида «Название сериала - Релиз-группа [MP4] | 3x01 Серия» показывает
// человеку ровно то, что он и так знает, а номер серии с названием уезжают
// за край. Нужно оно только там, где серии разных сериалов вперемешку, то
// есть в «продолжить просмотр».
static QString titleOf(const QVariantMap &item, bool withSeries)
{
    const QString kind = item.value("Type").toString();
    QString line = item.value("Name").toString();

    if (kind == QLatin1String("Episode")) {
        const int season = item.value("ParentIndexNumber").toInt();
        const int number = item.value("IndexNumber").toInt();
        if (number) {
            line = QString("%1x%2  %3").arg(season)
                       .arg(number, 2, 10, QChar(0x30)).arg(line);
        }
        const QString series = item.value("SeriesName").toString();
        if (withSeries && !series.isEmpty())
            line = series + QString::fromUtf8(" | ") + line;
    } else if (kind == QLatin1String("Audio")) {
        // Номер дорожки и длительность. На экране в 240 точек это всё, что
        // о песне нужно: имя исполнителя в альбоме одно на всех, а время
        // -- единственное, по чему отличают песню от получасового концерта.
        const int number = item.value("IndexNumber").toInt();
        if (number)
            line = QString("%1. %2")
                       .arg(number, 2, 10, QChar(0x30)).arg(line);
        const qint64 secs = item.value("RunTimeTicks").toLongLong()
                            / Q_INT64_C(10000000);
        if (secs > 0)
            line += QString::fromUtf8("   ") + asClock(secs * Q_INT64_C(1000));
        return line;   // «просмотрено» к песне не дописываем
    } else if (kind == QLatin1String("Series")
               || kind == QLatin1String("Movie")) {
        const int year = item.value("ProductionYear").toInt();
        if (year)
            line += QString("  (%1)").arg(year);
    }

    const qint64 watched = watchedMs(item);
    if (watched > 10000)
        line += QString::fromUtf8("   [") + asClock(watched) + QString("]");
    else if (item.value("UserData").toMap().value("Played").toBool())
        line += QString::fromUtf8(S_PROSMOTRENO);
    return line;
}


JfWindow::JfWindow(QWidget *parent)
    : QWidget(parent), _api(0), _music(0), _status(0), _panel(0), _cover(0),
      _panelRow(0), _list(0), _backAction(0),
      _quickClock(0), _selfTest(false), _selfStep(0), _session(0),
      _started(false), _broken(false), _bigW(0), _bigH(0),
      _lastIndex(0), _savedIndex(-1), _savedCount(-1),
      _queueLoaded(0), _queueTotal(0), _queueFetching(false),
      _queueRandomFlight(false), _queueEager(false), _queueDry(false),
      _playerAction(0), _pausedForVideo(false),
      _bars(0), _panelPos(0), _musicDirty(false),
      _exitArmed(false),
      _exitClock(0), _tickClock(0), _tickRow(-1), _tickPos(0)
{
    metka("jf-03-ctor.txt");
    setWindowTitle(QString::fromUtf8(S_IMYA));
    metka("jf-04-nazvanie.txt");

    _status = new QLabel(QString::fromUtf8(S_ZAPUSKAYUS), this);
    _status->setWordWrap(true);
    // Табло плеера. Живёт над списком и видно только на экране музыки:
    // на остальных экранах оно отнимало бы строки у библиотеки.
    _panel = new QLabel(this);
    // БЕЗ переноса по словам, и это главное в устройстве табло.
    //
    // С переносом длинное название занимало четыре строки, табло росло вниз
    // и выдавливало за край то, что стоит ниже, -- на снимке владельца от
    // 15.10 пропала полоска времени, осталась одна громкость. Теперь у
    // табло ровно три строки, сколько бы ни было букв: что не влезло --
    // едет бегущей строкой.
    _panel->setWordWrap(false);
    // Обложка слева от табло. Пустая -- не показывается вовсе: у альбома её
    // может не быть, и пустой квадрат на экране 240 точек -- потеря места.
    _cover = new QLabel(this);
    _cover->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    _cover->hide();
    _panelRow = new QWidget(this);
    QHBoxLayout *panelLayout = new QHBoxLayout(_panelRow);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(4);
    panelLayout->addWidget(_cover);
    panelLayout->addWidget(_panel, 1);
    _panelRow->hide();

    // Полоски времени и громкости -- под обложкой, во всю ширину экрана.
    // Рядом с обложкой им не хватало места: там около тринадцати знаков, а
    // «0:59 [=====-----] 3:11  40%» втрое длиннее.
    _bars = new QLabel(this);
    _bars->setWordWrap(false);
    _bars->hide();
    _list = new QListWidget(this);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addWidget(_status);
    layout->addWidget(_panelRow);
    layout->addWidget(_bars);
    layout->addWidget(_list, 1);

    // Правая софт-клавиша. По умолчанию Qt вешает на неё «Выход», и человек
    // оказывается заперт: нажатие закрывает приложение вместо возврата на
    // уровень выше. Лечится только действием -- обычные нажатия клавиш до
    // окна не доходят, софт-клавиша идёт не как клавиша. В пробе на этом
    // владелец дважды оказался в западне.
    _backAction = new QAction(QString::fromUtf8(S_NAZAD), this);
    _backAction->setSoftKeyRole(QAction::NegativeSoftKey);
    connect(_backAction, SIGNAL(triggered()), this, SLOT(goBack()));
    addAction(_backAction);
    // И СПИСКУ тоже. На Симбиане софт-клавиши строятся по действиям
    // того, кто В ФОКУСЕ, а фокус держит список. Действие на окне
    // при этом не видно вовсе, и на правой клавише остаётся умолчательный
    // «Выход» -- именно так и получилось в 0.1.2: кнопка закрывала
    // приложение из любого места. В пробе этого не было видно: там
    // действие вешалось, лишь когда список скрыт и фокус был у окна.
    _list->addAction(_backAction);

    // Левая софт-клавиша -- «Сейчас играет», и она появляется только пока
    // музыка идёт.
    //
    // Владелец на 0.3.4 сказал прямо: «если я выйду из плеера назад в список
    // медиа, музыка играть продолжит, но вернуться в плеер уже нельзя». Так и
    // было: строка «Сейчас играет» стоит только в САМОМ корне, а из середины
    // библиотеки до неё надо выйти наверх по всем уровням. Клавиша рядом с
    // «Назад» решает это из любого места.
    _playerAction = new QAction(QString::fromUtf8(S_MUZ_ZAGOLOVOK), this);
    _playerAction->setSoftKeyRole(QAction::PositiveSoftKey);
    _playerAction->setVisible(false);
    connect(_playerAction, SIGNAL(triggered()), this, SLOT(toPlayer()));
    addAction(_playerAction);
    _list->addAction(_playerAction);
    metka("jf-04b-vidzhety.txt");

    // Бегущая строка. Шаг нарочно неспешный: на этом телефоне каждая
    // перерисовка стоит, а читать бегущий текст быстрее всё равно нельзя.
    _tickClock = new QTimer(this);
    _tickClock->setInterval(400);
    connect(_tickClock, SIGNAL(timeout()), this, SLOT(tickerStep()));
    connect(_list, SIGNAL(currentRowChanged(int)), this, SLOT(tickerReset()));

    // Центральная клавиша не доходит до окна: её забирает список.
    _list->installEventFilter(this);
    _list->setFocus();
#ifdef Q_OS_SYMBIAN
    _list->setEditFocus(true);
#endif

    _api = new JfApi(this);
    connect(_api, SIGNAL(itemsReady(QString, QVariantList, int)),
            this, SLOT(onItems(QString, QVariantList, int)));
    connect(_api, SIGNAL(quickCodeReady(QString)),
            this, SLOT(onQuickCode(QString)));
    connect(_api, SIGNAL(signedIn()), this, SLOT(onSignedIn()));
    connect(_api, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
    connect(_api, SIGNAL(says(QString)), this, SLOT(says(QString)));

    // ТОЛЬКО ПОСЛЕ создания _api, и вот почему.
    //
    // rememberScreen() заканчивается вызовом _api->setScreen(). Пока
    // этот вызов стоял в начале конструктора, _api был ещё нулём -- и
    // приложение умирало молча на самом запуске: ставилось, значок в
    // меню, нажатие не делало ничего, своей папки не создавало. Ни одной
    // новой зависимости у образа при этом не появлялось, потому что
    // setScreen -- встроенный метод из jfapi.h; поэтому сверка импорта,
    // прав, заголовка и ресурсов с рабочей 0.1.10 показывала полное
    // совпадение, и причину нашла только сборка-щуп. Подробности в
    // ЗАМЕТКАХ, разделы 0.2.1 и 0.2.3.
    rememberScreen();
    metka("jf-04c-ekran.txt");

    _quickClock = new QTimer(this);
    _quickClock->setInterval(3000);
    connect(_quickClock, SIGNAL(timeout()), this, SLOT(pollQuick()));

    metka("jf-05-do-zhurnala.txt");
    writeLog(QString::fromUtf8(S_ZAPUSK_LOG));
    metka("jf-05b-zhurnal.txt");
    // Настройки читаются не здесь, а по таймеру: в пробе обращение к окну и
    // к сети прямо из конструктора роняло приложение без единого слова.
    QTimer::singleShot(100, this, SLOT(begin()));
}

// Старый кэш из памяти телефона -- вон, один раз при запуске.
//
// 0.3.0 держала музыку в C:/Data/Jellyfin/muzyka с пределом 40 МБ и забила
// внутреннюю память телефона целиком. Теперь музыка уезжает на карту, но сами
// собой те мегабайты с C: не исчезнут: папка останется лежать, и ни одна
// подрезка её больше не увидит -- она подрезает только текущую папку.
// Поэтому убираем её здесь, при первом же запуске новой версии.
static void uberiStaryjKesh()
{
    // Папки с прежними русскими именами -- вон, вместе с содержимым: это
    // кэш, он восстановится сам.
    const char *starye[] = { "/muzyka", 0 };
    for (int i = 0; starye[i]; ++i) {
        QDir staraya(folder() + QLatin1String(starye[i]));
        if (staraya.exists()) {
            const QFileInfoList byli = staraya.entryInfoList(QDir::Files);
            for (int j = 0; j < byli.size(); ++j)
                QFile::remove(byli.at(j).absoluteFilePath());
            QDir().rmdir(staraya.absolutePath());
        }
        QDir naKarte(QString("E:/Data/Jellyfin") + QLatin1String(starye[i]));
        if (naKarte.exists()) {
            const QFileInfoList byli = naKarte.entryInfoList(QDir::Files);
            for (int j = 0; j < byli.size(); ++j)
                QFile::remove(byli.at(j).absoluteFilePath());
            QDir().rmdir(naKarte.absolutePath());
        }
    }

    const QString staraya = folder() + QLatin1String("/music");
    if (staraya == muzPapka())
        return;   // музыка и так лежит там, подрезка справится сама
    QDir dir(staraya);
    if (!dir.exists())
        return;
    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    qint64 ubrano = 0;
    for (int i = 0; i < files.size(); ++i) {
        const qint64 size = files.at(i).size();
        if (QFile::remove(files.at(i).absoluteFilePath()))
            ubrano += size;
    }
    QDir().rmdir(staraya);
    if (ubrano > 0) {
        writeLog(QString::fromUtf8(S_MUZ_KESH_STARYJ)
                     .arg(int(ubrano >> 20)));
    }
}

void JfWindow::begin()
{
    // Чем кончился перенос настроек из папки прошлой версии. Строка одна, но
    // без неё молчаливая неудача переноса неотличима от «настроек не было»,
    // а это разница между «войди заново» и «всё на месте».
    if (!perenosOtchet.isEmpty())
        writeLog(perenosOtchet);
    uberiStaryjKesh();
    openNetwork();
}

// Симбиан сам в сеть не выходит: пока не открыт канал связи
// (QNetworkSession на точке доступа), запрос не уходит и ответа не
// будет НИКОГДА -- без ошибки, без сообщения, навсегда. Именно так
// выглядело зависание на «прошу код для входа» в 0.1.0.
//
// В Kutegram то же самое сделано в main.cpp, и это проверено на этом же
// телефоне. Разница: там никто не ждёт открытия, потому что первый
// запрос случается после действия человека -- а наш уходит сразу при
// запуске, поэтому ждём сигнала и начинаем только после него.
void JfWindow::openNetwork()
{
    QNetworkConfigurationManager manager;
    if (!(manager.capabilities()
          & QNetworkConfigurationManager::NetworkSessionRequired)) {
        note(QString::fromUtf8(S_SET_BEZ_TOCHKI));
        netGo();
        return;
    }

    const QNetworkConfiguration config = manager.defaultConfiguration();
    if (!config.isValid()) {
        note(QString::fromUtf8(S_TOCHKA_NE_VYBRANA));
        netGo();
        return;
    }

    _session = new QNetworkSession(config, this);
    connect(_session, SIGNAL(opened()), this, SLOT(netGo()));
    connect(_session, SIGNAL(error(QNetworkSession::SessionError)),
            this, SLOT(netBroke()));
    note(QString::fromUtf8(S_OTKRYVAYU_KANAL) + config.name());
    _session->open();

    // Если ни одного сигнала так и не пришло -- начинаем всё равно:
    // пусть лучше будет ошибка запроса, чем вечное ожидание.
    QTimer::singleShot(20000, this, SLOT(netGo()));
}

void JfWindow::netBroke()
{
    note(QString::fromUtf8(S_KANAL_NE_OTKRYLSYA)
         + (_session ? _session->errorString() : QString()));
    netGo();
}

// Звать можно сколько угодно раз -- работа начнётся один.
void JfWindow::netGo()
{
    if (_started)
        return;
    _started = true;
    if (_session && _session->isOpen())
        note(QString::fromUtf8(S_KANAL_OTKRYT));
    loadSettings();
}

// Очередь прошлого запуска. Читается вместе с настройками, но плеер по ней
// не поднимается: в корне просто появляется строка «Продолжить слушать».
// Нажмут -- тогда и создадим плеер, как при обычном нажатии на песню.
void JfWindow::loadQueue()
{
    QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
    const QStringList ids = keep.value("music_queue_ids").toStringList();
    const QStringList names = keep.value("music_queue_names").toStringList();
    const QStringList artists = keep.value("music_queue_artists").toStringList();
    if (ids.isEmpty() || ids.size() != names.size())
        return;

    _lastQueue.clear();
    for (int i = 0; i < ids.size(); ++i) {
        QVariantMap one;
        one.insert(QLatin1String("Type"), QLatin1String("Audio"));
        one.insert(QLatin1String("Id"), ids.at(i));
        one.insert(QLatin1String("Name"), names.at(i));
        if (i < artists.size())
            one.insert(QLatin1String("AlbumArtist"), artists.at(i));
        _lastQueue.append(one);
    }

    _lastIndex = keep.value("music_queue_index").toInt();
    if (_lastIndex < 0 || _lastIndex >= _lastQueue.size())
        _lastIndex = 0;
    _savedCount = _lastQueue.size();
    _savedIndex = _lastIndex;
    writeLog(QString::fromUtf8(S_MUZ_OCHERED_BYLA)
                 .arg(_lastQueue.size()).arg(_lastIndex + 1));
}

// Запись очереди. Зовётся на каждое изменение состояния плеера, поэтому
// сначала сверяемся: писать один и тот же список на карту при каждом
// нажатии громкости незачем, это флеш-память телефона.
void JfWindow::saveQueue()
{
    if (!_music)
        return;

    const QVariantList queue = _music->queue();
    const int index = _music->currentIndex();
    if (queue.size() == _savedCount && index == _savedIndex)
        return;

    QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
    if (queue.isEmpty()) {
        // Остановили нарочно -- значит и возвращаться не к чему.
        keep.remove("music_queue_ids");
        keep.remove("music_queue_names");
        keep.remove("music_queue_artists");
        keep.remove("music_queue_index");
        keep.sync();
        _lastQueue.clear();
        _savedCount = 0;
        _savedIndex = -1;
        return;
    }

    // Пишем не всю очередь, а окно от текущей песни.
    //
    // Очередь теперь доливается с сервера и доходит до сотен записей, а
    // настройки лежат в памяти телефона и записываются на каждую смену
    // песни. Полторы сотни названий -- это уже десятки килобайт на каждую
    // запись; продолжать слушать с места, на котором остановились, столько
    // не требует.
    const int skolko = 150;
    int ot = index > 0 ? index : 0;
    if (queue.size() - ot > skolko && ot + skolko <= queue.size()) {
        // начало -- текущая песня, остальное отбрасываем
    } else if (queue.size() > skolko) {
        ot = queue.size() - skolko;
    } else {
        ot = 0;
    }
    const int doKuda = qMin(queue.size(), ot + skolko);

    QStringList ids;
    QStringList names;
    QStringList artists;
    for (int i = ot; i < doKuda; ++i) {
        const QVariantMap one = queue.at(i).toMap();
        ids.append(one.value("Id").toString());
        names.append(one.value("Name").toString());
        QString who = one.value("AlbumArtist").toString();
        if (who.isEmpty())
            who = one.value("Album").toString();
        artists.append(who);
    }

    keep.setValue("music_queue_ids", ids);
    keep.setValue("music_queue_names", names);
    keep.setValue("music_queue_artists", artists);
    keep.setValue("music_queue_index", index - ot > 0 ? index - ot : 0);
    keep.sync();
    _savedCount = queue.size();
    _savedIndex = index;
}

void JfWindow::loadSettings()
{
    loadQueue();
    QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
    const QString server = keep.value("server").toString();
    const QString stream = keep.value("stream").toString();
    const QString token = keep.value("token").toString();
    const QString user = keep.value("user").toString();
    _selfTest = keep.value("proverka").toInt() == 1;
    _api->setStreamPort(keep.value("stream_port").toInt());

    if (server.isEmpty()) {
        askServer();
        return;
    }
    _api->setServer(server);
    if (!stream.isEmpty())
        _api->setStreamHost(stream);
    else
        useProbeStream();
    if (token.isEmpty()) {
        startSignIn();
        return;
    }
    _api->setAuth(token, user);
    note(QString::fromUtf8(S_CHITAYU_BIBLIOTEKU));
    _api->loadViews();
}

void JfWindow::saveSettings()
{
    QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
    keep.setValue("server", _api->server());
    keep.setValue("stream", _api->streamHost());
    keep.setValue("stream_port", _api->streamPort());
    keep.setValue("token", _api->token());
    keep.setValue("user", _api->userId());
    keep.sync();
}

void JfWindow::useProbeStream()
{
    // Пока поток отдаёт rtsp-proba.py на отдельной машине -- берём её
    // адрес из сборки. Пустая строка = сервер один, адрес уже выведен
    // из адреса Jellyfin в JfApi::setServer.
    const QString probe = QString::fromLatin1(STREAM_HOST_PROBA);
    if (probe.isEmpty())
        return;
    _api->setStreamHost(probe);
    note(QString::fromUtf8(S_POTOK_S_OTDELNOY) + probe);
}

void JfWindow::askServer()
{
    // Единственный вопрос за всё время жизни приложения: адрес Jellyfin.
    // Сервер один -- поток потом отдаёт плагин внутри того же Jellyfin,
    // поэтому адрес потока не спрашивается вовсе (см. probadefaults.h).
    // Пароль -- никогда: дальше QuickConnect, где шестизначный код
    // вводится на большом экране.
    bool ok = false;
    const QString given = QInputDialog::getText(
        this, QString::fromUtf8(S_ADRES_JELLYFIN),
        QString::fromUtf8(S_NAPRIMER_ADRES),
        // В пустом поле -- общий пример, а не чей-то настоящий адрес:
        // приложение ставится не только нам, и показывать чужому человеку
        // адрес нашей машины незачем.
        QLineEdit::Normal, _api->server().isEmpty()
                               ? QString::fromUtf8(S_PRIMER_ADRESA)
                               : _api->server(), &ok);
    if (!ok || given.isEmpty()) {
        note(QString::fromUtf8(S_BEZ_ADRESA));
        return;
    }
    _api->setServer(given);
    // Хост потока выводится из нового адреса, а не остаётся от старого:
    // иначе после переезда приложение спрашивало бы библиотеку у одной
    // машины, а видео просило у другой, и объяснить это человеку нечем.
    _api->setStreamHost(QUrl(_api->server()).host());
    useProbeStream();

    saveSettings();
    startSignIn();
}

void JfWindow::startSignIn()
{
    note(QString::fromUtf8(S_PROSHU_KOD));
    _api->quickStart();
}

void JfWindow::onQuickCode(const QString &code)
{
    _list->clear();
    _list->addItem(QString::fromUtf8(S_KOD) + code);
    _list->addItem(QString::fromUtf8(S_KOD1));
    _list->addItem(QString::fromUtf8(S_KOD2));
    _list->addItem(QString::fromUtf8(S_KOD3));
    _list->addItem(QString::fromUtf8(S_KOD4));
    note(QString::fromUtf8(S_ZHDU_KOD));
    writeLog(QString::fromUtf8(S_KOD_LOG) + code);
    _quickClock->start();
}

void JfWindow::pollQuick()
{
    _api->quickCheck();
}

void JfWindow::onSignedIn()
{
    _quickClock->stop();
    saveSettings();
    note(QString::fromUtf8(S_VOSHLI));
    _api->loadViews();
}

// Неудача должна быть поправимой с телефона.
//
// Адрес спрашивается один раз и ложится в настройки, и если он окажется
// неверным, без этого списка его негде поменять: пришлось бы
// искать файл C:/Data/Jellyfin/settings.ini файловым менеджером или сносить
// пакет. Каждая такая переустановка -- это подключение, копирование и ожидание.
void JfWindow::onFailed(const QString &why)
{
    note(QString::fromUtf8(S_NE_VYSHLO) + why);
    _quickClock->stop();
    _broken = true;
    _list->clear();
    _list->addItem(QString::fromUtf8(S_SERVER) + _api->server());
    _list->addItem(QString::fromUtf8(S_SMENIT_ADRES));
    _list->addItem(QString::fromUtf8(S_POPROBOVAT_SNOVA));
    _list->addItem(QString::fromUtf8(S_VOYTI_ZANOVO_KOD));
    _list->setCurrentRow(1);
}

// Разбор нажатия в этом списке починки.
void JfWindow::fixChoice(int row)
{
    if (row == 1) {
        QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
        keep.remove("server");
        keep.remove("stream");
        keep.sync();
        _broken = false;
        askServer();
    } else if (row == 2) {
        _broken = false;
        note(QString::fromUtf8(S_PROBUYU_SNOVA));
        loadSettings();
    } else if (row == 3) {
        QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
        keep.remove("token");
        keep.remove("user");
        keep.sync();
        _broken = false;
        _api->setAuth(QString(), QString());
        startSignIn();
    }
}

void JfWindow::says(const QString &text)
{
    note(text);
}

void JfWindow::note(const QString &text)
{
    writeLog(text);
    _status->setText(text);
}

void JfWindow::onItems(const QString &key, const QVariantList &items,
                       int total)
{
    // В журнал -- что именно пришло. Со стороны телефона «пустой список» и
    // «список не отрисовался» выглядят одинаково, а разбираться потом
    // придётся по журналу: другого способа нет.
    writeLog(QString::fromUtf8(S_PRISHLO)
                 .arg(key).arg(items.size()));
    for (int i = 0; i < items.size() && i < 10; ++i) {
        const QVariantMap one = items.at(i).toMap();
        writeLog(QString::fromUtf8("   %1 [%2] %3")
                     .arg(one.value("Name").toString())
                     .arg(one.value("Type").toString())
                     .arg(one.value("Id").toString()));
    }

    // Перечитанная запись -- подменяем её в списках на месте.
    //
    // Целиком список не перезапрашиваем нарочно: человек после плеера
    // возвращается на ту же строку, и перестроение увело бы выбор и
    // прокрутку. Меняется только то, ради чего спрашивали, -- UserData.
    if (key == QLatin1String("one")) {
        if (items.isEmpty())
            return;
        const QVariantMap fresh = items.at(0).toMap();
        const QString id = fresh.value("Id").toString();
        if (id.isEmpty())
            return;
        bool found = false;
        for (int l = 0; l < _stack.size(); ++l) {
            QVariantList &where = _stack[l].items;
            for (int i = 0; i < where.size(); ++i) {
                QVariantMap one = where.at(i).toMap();
                if (one.value("Id").toString() != id)
                    continue;
                one.insert(QLatin1String("UserData"),
                           fresh.value(QLatin1String("UserData")));
                where[i] = one;
                found = true;
            }
        }
        if (found) {
            writeLog(QString::fromUtf8(S_MESTO_OBNOVLENO)
                         .arg(asClock(watchedMs(fresh))));
            redraw();
        }
        return;
    }

    // Долив очереди музыки. На экран это не попадает вовсе: человек может в
    // это время ходить совсем в другом месте библиотеки.
    if (key == QLatin1String("ochered")) {
        _queueFetching = false;
        if (!_music)
            return;
        const int added = _music->appendToQueue(items);
        // Счётчик страниц двигаем только для обычного порядка: при случайном
        // «сколько уже спросили» ничего не значит, сервер каждый раз отдаёт
        // другие, и сбитый счётчик потом пропустил бы настоящие страницы.
        if (!_queueRandomFlight)
            _queueLoaded += items.size();
        // Пришло, но всё знакомое, -- значит папку мы уже вычерпали.
        if (added == 0)
            _queueDry = true;
        _queueRandomFlight = false;
        if (total > 0)
            _queueTotal = total;
        writeLog(QString::fromUtf8(S_MUZ_DOLIL)
                     .arg(added).arg(_music->queue().size()));
        return;
    }

    if (key == QLatin1String("views")) {
        showRoot();
        // ПЕРВОЙ строкой -- настройки.
        //
        // Сначала они стояли последними, чтобы не мешать тем, кто пришёл
        // смотреть кино. Оказалось наоборот: в корне ещё и «недавнее», список
        // длинный, и до настроек приходится прокручивать его весь. А нужны
        // они как раз тогда, когда ничего не работает -- сменить адрес
        // сервера или порт. Тогда они должны быть там, где их видно сразу.
        QVariantMap settings;
        settings.insert(QLatin1String("Type"), QLatin1String("Nastroyki"));
        settings.insert(QLatin1String("Name"), QString::fromUtf8(S_NASTROYKI));
        _stack.last().items.append(settings);
        for (int i = 0; i < items.size(); ++i)
            _stack.last().items.append(items.at(i));
        // «Продолжить слушать» -- здесь, а не только при смене песни. Корень
        // строится заново на каждый ответ сервера, и очередь прошлого
        // запуска иначе некому было бы в него поставить: плеера-то ещё нет.
        ensureRootMusicRow();
        redraw();
        if (_selfTest) {
            // Раздел с сериалами -- в нём больше всего уровней, значит
            // проверяется самый длинный путь.
            for (int i = 0; i < items.size(); ++i) {
                const QVariantMap one = items.at(i).toMap();
                if (one.value("CollectionType").toString()
                        == QLatin1String("tvshows")) {
                    _selfStep = 1;
                    writeLog(QString::fromUtf8(S_SAMOPROVERKA_RAZDEL)
                             + one.value("Name").toString());
                    _current = one;
                    _api->loadChildren(one);
                    return;
                }
            }
        }
        // «Продолжить просмотр» спрашивается сразу и дописывается НАВЕРХ
        // корневого списка: ради этого приложение и нужно -- включить
        // недосмотренное, не ходя по дереву.
        _api->loadResume();
        return;
    }

    // Следующая страница -- дописывается к тому, что уже показано.
    if (key == QLatin1String("more")) {
        if (_stack.isEmpty())
            return;
        Level &level = _stack.last();
        for (int i = 0; i < items.size(); ++i)
            level.items.append(items.at(i));
        level.total = total;
        level.chosen = _list->currentRow();
        redraw();
        return;
    }

    if (key == QLatin1String("resume")) {
        if (items.isEmpty() || _stack.isEmpty())
            return;
        // Недосмотренное встаёт наверх корня, но НИЖЕ настроек.
        //
        // Сначала оно дописывалось поверх всего, и настройки уезжали вниз:
        // недосмотренных записей бывает двенадцать, а разделов библиотеки
        // два-три -- строка «Настройки» оказывалась тринадцатой из
        // пятнадцати, то есть на глаз в самом конце. Ровно это и было
        // видно в 0.1.10, хотя добавлялась она первой.
        //
        // Поэтому первая строка корня (если это настройки) остаётся на
        // месте, а недосмотренное вставляется сразу за ней.
        const QVariantList bylo = _stack.first().items;
        // Сколько строк наверху корня -- наши собственные: «Сейчас играет»
        // и «Настройки». Недосмотренное встаёт СРАЗУ ЗА НИМИ, иначе
        // настройки уезжают на тринадцатую строку из пятнадцати -- ровно то,
        // на что уже жаловались в 0.1.10.
        int shapka = 0;
        while (shapka < bylo.size()) {
            const QString kind = bylo.at(shapka).toMap()
                                     .value("Type").toString();
            if (kind != QLatin1String("Nastroyki")
                && kind != QLatin1String("Muzyka"))
                break;
            ++shapka;
        }
        QVariantList together;
        for (int i = 0; i < shapka; ++i)
            together.append(bylo.at(i));
        for (int i = 0; i < items.size(); ++i)
            together.append(items.at(i));
        for (int i = shapka; i < bylo.size(); ++i)
            together.append(bylo.at(i));
        _stack.first().items = together;
        if (_stack.size() == 1)
            redraw();
        return;
    }

    if (items.isEmpty()) {
        note(QString::fromUtf8(S_ZDES_PUSTO));
        return;
    }

    if (_selfTest && key == QLatin1String("children") && !items.isEmpty()) {
        const QVariantMap first = items.at(0).toMap();
        const QString kind = first.value("Type").toString();
        writeLog(QString::fromUtf8(S_SAMOPROVERKA_SHAG)
                     .arg(_selfStep).arg(items.size()).arg(kind)
                     .arg(first.value("Name").toString()));
        if (kind == QLatin1String("Series") || kind == QLatin1String("Season")
            || kind == QLatin1String("MusicAlbum")) {
            ++_selfStep;
            _current = first;
            _api->loadChildren(first);
            return;
        }
        // Дошли до записи -- показать, какой адрес уйдёт системному плееру.
        writeLog(QString::fromUtf8(S_SAMOPROVERKA_GOTOVO)
                 + _api->streamUrl(first.value("Id").toString(),
                                   watchedMs(first)));
        _selfTest = false;
    }

    Level level;
    level.title = _current.value("Name").toString();
    level.parent = _current;
    level.kind = key;
    level.items = items;
    level.total = total;
    level.chosen = 0;
    push(level);
}

// Строка экрана музыки: команда или песня очереди.
static QVariantMap musicRow(const QString &id, const QString &name)
{
    QVariantMap one;
    one.insert(QLatin1String("Type"), QLatin1String("Muzyka"));
    one.insert(QLatin1String("Id"), id);
    one.insert(QLatin1String("Name"), name);
    return one;
}

// Качество музыки из настроек -- нужно и до того, как плеер создан.
static int muzKbit()
{
    QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
    const int kbit = keep.value("music_kbit").toInt();
    return kbit > 0 ? kbit : 128;
}

// Громкость из настроек, ступенями 0..10. Тоже нужна до создания плеера.
static int muzGromkost()
{
    QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
    if (!keep.contains("music_volume"))
        return 4;
    const int shag = keep.value("music_volume").toInt();
    return shag < 0 ? 0 : (shag > 10 ? 10 : shag);
}

// Сколько занято музыкой, в мегабайтах. Считается и без плеера: настройки
// открывают как раз тогда, когда ничего не играет.
//
// Сдвиг, а не деление: сдвиг -- машинное действие, а деление на этом
// телефоне идёт через служебную функцию, которой может не оказаться.
static int muzMegabayt()
{
    QDir dir(muzPapka());
    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    qint64 vsego = 0;
    for (int i = 0; i < files.size(); ++i)
        vsego += files.at(i).size();
    return int(vsego >> 20);
}

// Строка настройки: вид, опознаватель и подпись.
static QVariantMap settingRow(const QString &id, const QString &name)
{
    QVariantMap one;
    one.insert(QLatin1String("Type"), QLatin1String("Nastroyka"));
    one.insert(QLatin1String("Id"), id);
    one.insert(QLatin1String("Name"), name);
    return one;
}

QVariantList JfWindow::settingsRows() const
{
    // Значения стоят прямо в строках -- чаще всего настройки открывают
    // именно чтобы посмотреть, куда приложение ходит и каким портом.
    QVariantList rows;
    rows.append(settingRow(
        QLatin1String("server"),
        QString::fromUtf8(S_SERVER) + _api->server()));
    rows.append(settingRow(
        QLatin1String("port"),
        QString::fromUtf8(S_PORT_POTOKA_ROD)
            + QString::number(_api->streamPort() > 0 ? _api->streamPort() : 554)));
    rows.append(settingRow(
        QLatin1String("vhod"),
        QString::fromUtf8(S_VOYTI_ZANOVO)));
    rows.append(settingRow(
        QLatin1String("muzyka"),
        QString::fromUtf8(S_MUZ_KACHESTVO)
            + QString::number(_music ? _music->bitrate() : muzKbit())
            + QString::fromUtf8(S_MUZ_KBIT)));
    rows.append(settingRow(
        QLatin1String("gromkost"),
        QString::fromUtf8(S_MUZ_GROMKOST)
            + QString::number(muzGromkost() * 10) + QLatin1Char('%')));
    rows.append(settingRow(
        QLatin1String("kesh"),
        QString::fromUtf8(S_MUZ_KESH) + QString::number(muzMegabayt())
            + QString::fromUtf8(S_MUZ_KESH_MB)));
    rows.append(settingRow(
        QLatin1String("pleer"),
        QString::fromUtf8(S_PLEER_SPRASHIVAET)));
    // Версия -- последней строкой. Нужна ровно в одном случае: сказать, какая
    // сборка стоит. До этого узнать было нечем -- в меню телефона видно имя
    // пакета, а не номер, и присланный журнал не говорил, чем он снят.
    rows.append(settingRow(
        QLatin1String("versiya"),
        QString::fromUtf8(S_VERSIYA)));
    return rows;
}

void JfWindow::openSettings()
{
    Level level;
    level.title = QString::fromUtf8(S_NASTROYKI);
    level.kind = QLatin1String("nastroyki");
    level.items = settingsRows();
    push(level);
}

void JfWindow::refreshSettings()
{
    // Строки списка рисуются один раз, поэтому после изменения значение в
    // них остаётся прежним, пока не выйдешь и не зайдёшь заново. Человек
    // при этом видит старый порт и считает, что настройка не сохранилась.
    if (_stack.isEmpty() || _stack.last().kind != QLatin1String("nastroyki"))
        return;
    // Место в списке запоминаем: перерисовка ставит строку из `chosen`, и
    // без этого человека выбрасывало бы на первую строку после каждой
    // правки.
    _stack.last().chosen = _list->currentRow();
    _stack.last().items = settingsRows();
    redraw();
}

void JfWindow::doSetting(const QString &what)
{
    if (what == QLatin1String("server")) {
        askServer();
        refreshSettings();
    } else if (what == QLatin1String("port")) {
        bool ok = false;
        const int now = _api->streamPort() > 0 ? _api->streamPort() : 554;
        const int given = QInputDialog::getInt(
            this, QString::fromUtf8(S_PORT_POTOKA),
            QString::fromUtf8(S_OBYCHNO_554), now, 1, 65535, 1, &ok);
        if (ok) {
            _api->setStreamPort(given);
            saveSettings();
            note(QString::fromUtf8(S_PORT_POTOKA_STAL) + QString::number(given));
            refreshSettings();
        }
    } else if (what == QLatin1String("vhod")) {
        QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
        keep.remove("token");
        keep.remove("user");
        keep.sync();
        _api->setAuth(QString(), QString());
        startSignIn();
    } else if (what == QLatin1String("muzyka")) {
        // Сколько кбит/с просить у Jellyfin. Меньше -- быстрее приезжает
        // первая песня, больше -- чище звук в наушниках. Уже скачанное
        // остаётся прежним: перекачивать альбом ради этого незачем.
        bool ok = false;
        const int given = QInputDialog::getInt(
            this, QString::fromUtf8(S_MUZ_SKOLKO_KBIT),
            QString::fromUtf8(S_MUZ_SKOLKO_POYASN),
            _music ? _music->bitrate() : muzKbit(), 48, 320, 16, &ok);
        if (ok) {
            if (_music)
                _music->setBitrate(given);
            QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
            keep.setValue("music_kbit", given);
            keep.sync();
            refreshSettings();
        }
    } else if (what == QLatin1String("gromkost")) {
        // Ступенями 0..10, а не процентами: процент пришлось бы делить на
        // десять, а деления этот телефон не умеет (см. asClock).
        bool ok = false;
        const int given = QInputDialog::getInt(
            this, QString::fromUtf8(S_MUZ_SKOLKO_GROMKO),
            QString::fromUtf8(S_MUZ_SKOLKO_GROMKO_POYASN),
            muzGromkost(), 0, 10, 1, &ok);
        if (ok) {
            QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
            keep.setValue("music_volume", given);
            keep.sync();
            if (_music)
                _music->setVolumeStep(given);
            refreshSettings();
        }
        } else if (what == QLatin1String("kesh")) {
        // Ради этой строки всё и затевалось в 0.3.1: 0.3.0 забила внутреннюю
        // память телефона целиком, и убрать это из приложения было нечем.
        const int mb = muzMegabayt();
        if (mb <= 0) {
            note(QString::fromUtf8(S_MUZ_KESH_PUSTO));
            return;
        }
        if (QMessageBox::question(
                this, QString::fromUtf8(S_MUZ_KESH_VOPROS),
                QString::fromUtf8(S_MUZ_KESH_VOPROS_TEKST)
                    .arg(mb).arg(muzPapka()),
                QMessageBox::Yes | QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
        if (_music) {
            _music->clearCache();
        } else {
            // Плеера ещё нет -- чистим папку сами, не поднимая Phonon.
            QDir dir(muzPapka());
            const QFileInfoList files = dir.entryInfoList(QDir::Files);
            for (int i = 0; i < files.size(); ++i)
                QFile::remove(files.at(i).absoluteFilePath());
        }
        note(QString::fromUtf8(S_MUZ_KESH_UBRAN));
        refreshSettings();
    } else if (what == QLatin1String("pleer")) {
        // Объяснение, а не настройка: этим распоряжаемся не мы.
        //
        // RealPlayer спрашивает «Требуется подключение к серверу?» перед
        // каждым запуском, потому что у него самого не выбрана точка
        // доступа. Из приложения на это не повлиять -- мы отдаём ему адрес,
        // а чем он пойдёт в сеть, решает он. Зато можно сказать, где это
        // выключается: вопрос стоит четыре секунды из пятнадцати, которые
        // человек ждёт картинку.
        QMessageBox::information(
            this, QString::fromUtf8(S_VOPROS_PLEERA),
            QString::fromUtf8(S_VOPROS_PLEERA_TEKST));
    }
}

void JfWindow::showRoot()
{
    _stack.clear();
    Level root;
    root.title = QString::fromUtf8(S_IMYA);
    root.kind = QString::fromLatin1("views");
    root.chosen = 0;
    _stack.append(root);
}

void JfWindow::push(const Level &level)
{
    if (!_stack.isEmpty())
        _stack.last().chosen = _list->currentRow();
    _stack.append(level);
    redraw();
}

void JfWindow::goBack()
{
    if (_stack.size() <= 1) {
        // На самом верху «Назад» -- это выход. Но не с первого нажатия:
        // к библиотеке возвращаются, нажимая «Назад» подряд и не глядя, и
        // одно лишнее нажатие закрывало приложение вместе с музыкой.
        if (!_exitArmed) {
            _exitArmed = true;
            QString text = QString::fromUtf8(S_ESHCHE_RAZ_VYHOD);
            if (_music && _music->active())
                text += QString::fromUtf8(S_ESHCHE_RAZ_MUZYKA);
            note(text);
            if (!_exitClock) {
                _exitClock = new QTimer(this);
                _exitClock->setSingleShot(true);
                _exitClock->setInterval(3000);
                connect(_exitClock, SIGNAL(timeout()), this, SLOT(disarmExit()));
            }
            _exitClock->start();
            return;
        }
        qApp->quit();
        return;
    }
    // Ушли с верхнего уровня -- взвод больше не нужен.
    _exitArmed = false;
    _stack.removeLast();
    redraw();
}

// Строка выбора сменилась (или список перерисован) -- начинаем заново.
void JfWindow::tickerReset()
{
    // Прежней строке возвращаем её полный текст: она уехала на середине, и
    // оставить её так -- значит показывать бессмыслицу вроде «дняя битва».
    if (_tickRow >= 0 && _tickRow < _list->count() && !_tickFull.isEmpty()) {
        QListWidgetItem *was = _list->item(_tickRow);
        if (was && was->text() != _tickFull)
            was->setText(_tickFull);
    }

    _tickClock->stop();
    _tickRow = _list->currentRow();
    _tickPos = 0;
    _tickFull.clear();
    if (_tickRow < 0)
        return;

    QListWidgetItem *now = _list->item(_tickRow);
    if (!now)
        return;
    _tickFull = now->text();

    // На экране плеера таймер нужен ВСЕГДА: этим же таймером едет табло,
    // а там своя причина бежать -- длинное название песни. Без этого
    // условия табло стояло бы неподвижно каждый раз, когда выбранная
    // строка списка короткая.
    const bool pleer = !_stack.isEmpty()
        && _stack.last().kind == QLatin1String("muzyka");

    // Влезает -- и бежать незачем. Ширину берём у видимой части списка, за
    // вычетом полоски прокрутки и отступов.
    const QFontMetrics fm(_list->font());
    if (!pleer && fm.width(_tickFull) <= _list->viewport()->width() - 16)
        return;
    _tickClock->start();
}

void JfWindow::tickerStep()
{
    // Пока плеер внутри своего вызова -- не трогаем список ВООБЩЕ.
    //
    // Внутри вызовов Phonon крутится обычный цикл событий, и этот
    // таймер приходит прямо туда. Перестройка списка в такой момент --
    // вход внутрь самого себя, и телефон встаёт; похоже, именно так
    // бегущая строка и убила паузу в 0.3.9.
    if (JfMusic::busy())
        return;
    if (_tickRow != _list->currentRow() || _tickFull.isEmpty()) {
        tickerReset();
        return;
    }
    QListWidgetItem *item = _list->item(_tickRow);
    if (!item) {
        _tickClock->stop();
        return;
    }
    // Строка списка влезает целиком -- её не трогаем вовсе: таймер мог быть
    // запущен ради табло.
    {
        const QFontMetrics fm(_list->font());
        if (fm.width(_tickFull) <= _list->viewport()->width() - 16)
            return;
    }

    // Заодно -- табло плеера: название и исполнитель там тоже едут, и
    // своего таймера для этого заводить незачем.
    if (!_stack.isEmpty() && _stack.last().kind == QLatin1String("muzyka")) {
        ++_panelPos;
        if (_panelPos > 200)
            _panelPos = 0;
        setPanelText();
    }

    // Текст едет по кругу, с просветом между концом и началом -- иначе
    // непонятно, где строка кончилась.
    const QString krug = _tickFull + QLatin1String("   ");
    ++_tickPos;
    if (_tickPos >= krug.size())
        _tickPos = 0;
    item->setText(krug.mid(_tickPos) + krug.left(_tickPos));
}

void JfWindow::disarmExit()
{
    if (!_exitArmed)
        return;
    _exitArmed = false;
    // Возвращаем на место обычную надпись: иначе человек так и остался бы с
    // предупреждением о выходе, которое уже ничего не значит.
    redraw();
}

void JfWindow::redraw()
{
    if (_stack.isEmpty())
        return;
    const Level &level = _stack.last();
    _list->clear();
    // Названием сериала подписываем только там, где серии вперемешку.
    const bool mixed = (level.kind == QLatin1String("resume"));
    for (int i = 0; i < level.items.size(); ++i)
        _list->addItem(titleOf(level.items.at(i).toMap(), mixed));
    // Последней строкой -- «дальше», если на сервере есть ещё.
    if (level.total > level.items.size()) {
        _list->addItem(QString::fromUtf8(S_ESHCHE)
                           .arg(level.total - level.items.size()));
    }
    if (_list->count())
        _list->setCurrentRow(qBound(0, level.chosen, _list->count() - 1));
    _status->setText(level.title.isEmpty() ? QString::fromUtf8(S_IMYA)
                                           : level.title);

    tickerReset();

    // Табло -- только на экране плеера.
    if (level.kind == QLatin1String("muzyka")) {
        // Табло перестроено -- бегущая строка начинается сначала, иначе
        // новая песня показалась бы с середины названия.
        const QVariantList queue = _music ? _music->queue() : QVariantList();
        const int index = _music ? _music->currentIndex() : -1;
        QVariantMap one;
        if (index >= 0 && index < queue.size())
            one = queue.at(index).toMap();
        const QString name = one.value("Name").toString();
        if (name != _panelName) {
            _panelName = name;
            _panelPos = 0;
        }
        setPanelText();
        showCover();
        _panelRow->show();
        _bars->show();
    } else {
        _panelRow->hide();
        _bars->hide();
    }
}

bool JfWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == _list && event->type() == QEvent::KeyPress) {
        QKeyEvent *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Select || key->key() == Qt::Key_Enter
            || key->key() == Qt::Key_Return) {
            enter();
            return true;
        }
        // Backspace -- клавиша стирания, есть на самой клавиатуре. Key_Back и
        // Key_No -- та же правая софт-клавиша на тех сборках, где она всё-таки
        // приходит нажатием, а не действием. Дешевле принять все три, чем
        // гадать, какая из них на этом аппарате.
        if (key->key() == Qt::Key_Backspace || key->key() == Qt::Key_Back
            || key->key() == Qt::Key_No) {
            goBack();
            return true;
        }
        // Громкость -- звёздочкой и решёткой, и работает это НА ЛЮБОМ
        // экране, пока играет музыка: ходить за регулятором в плеер,
        // когда музыка бьёт по ушам, -- то же самое, что не иметь его.
        // Своя регулировка здесь не удобство: боковых клавиш громкости у
        // E90 нет, а на других аппаратах этого возраста они могут не
        // работать вовсе.
        if (_music && _music->active()) {
            if (key->key() == Qt::Key_Asterisk
                || key->key() == Qt::Key_VolumeUp) {
                _music->louder();
                saveVolume();
                note(QString::fromUtf8(S_MUZ_GROMKOST)
                     + QString::number(_music->volumePercent())
                     + QLatin1Char('%'));
                return true;
            }
            if (key->key() == Qt::Key_NumberSign
                || key->key() == Qt::Key_VolumeDown) {
                _music->quieter();
                saveVolume();
                note(QString::fromUtf8(S_MUZ_GROMKOST)
                     + QString::number(_music->volumePercent())
                     + QLatin1Char('%'));
                return true;
            }
        }
        // Пауза цифрой -- с любого экрана, пока играет музыка. Раньше её
        // можно было поставить ТОЛЬКО строкой в плеере: чтобы остановить
        // музыку, надо было сперва до плеера дойти.
        if (_music && _music->active() && key->key() == Qt::Key_5) {
            _music->toggle();
            return true;
        }
        // И клавишей -- на случай, если софт-клавиша на этом аппарате
        // покажется не подписью, а меню «Функции»: нажатие ноля вернёт к
        // плееру откуда угодно.
        if (_music && _music->active() && key->key() == Qt::Key_0) {
            toPlayer();
            return true;
        }
        // На экране музыки джойстик вправо-влево переключает песни. Строки
        // для этого есть, но клавиша быстрее: именно её ищут рукой, не глядя
        // на экран.
        if (_music && !_stack.isEmpty()
            && _stack.last().kind == QLatin1String("muzyka")) {
            if (key->key() == Qt::Key_Right) {
                _music->next();
                return true;
            }
            if (key->key() == Qt::Key_Left) {
                _music->previous();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void JfWindow::enter()
{
    _exitArmed = false;
    if (_broken) {
        fixChoice(_list->currentRow());
        return;
    }
    if (_stack.isEmpty())
        return;
    const Level &level = _stack.last();
    const int row = _list->currentRow();
    if (row < 0)
        return;
    if (row >= level.items.size()) {
        // Нажали на «...ещё -->».
        note(QString::fromUtf8(S_DOGRUZHAYU));
        _api->loadChildren(level.parent, level.items.size());
        return;
    }
    // Экран музыки разбирает нажатия сам.
    if (level.kind == QLatin1String("muzyka")) {
        musicChoice(row);
        return;
    }
    // Экран очереди: нажали песню -- прыгаем к ней и возвращаемся к плееру,
    // чтобы было видно, что заиграло.
    if (level.kind == QLatin1String("muzochered")) {
        if (_music)
            _music->jumpTo(row);
        goBack();
        return;
    }

    const QVariantMap chosen = level.items.at(row).toMap();
    // Песню нажали в альбоме -- в очередь идёт ВЕСЬ альбом, начиная с неё.
    // Ради этого всё и затевалось: одной песней список воспроизведения
    // смысла не имеет.
    if (chosen.value("Type").toString() == QLatin1String("Audio")) {
        startMusic(level.items, row, level.parent, level.total);
        return;
    }
    openItem(chosen);
}

void JfWindow::openItem(const QVariantMap &item)
{
    const QString kind = item.value("Type").toString();
    const QString id = item.value("Id").toString();
    _current = item;

    if (kind == QLatin1String("Nastroyki")) {
        openSettings();
        return;
    }
    if (kind == QLatin1String("Nastroyka")) {
        doSetting(id);
        return;
    }

    if (kind == QLatin1String("Episode") || kind == QLatin1String("Movie")
        || kind == QLatin1String("Video")) {
        playFrom(item);
        return;
    }

    if (kind == QLatin1String("Muzyka")) {
        if (id == QLatin1String("prodolzhit")) {
            // Вернулись к очереди прошлого запуска -- вот теперь и плеер, и
            // загрузка той песни, на которой остановились.
            startMusic(_lastQueue, _lastIndex);
            return;
        }
        // Строка «Сейчас играет» в корне -- возврат к плееру.
        openMusicPage();
        return;
    }

    if (kind == QLatin1String("Audio")) {
        // Сюда попадают одиночные песни -- например из «продолжить». Очередь
        // из одной дорожки, но играет её тот же свой плеер.
        writeLog(QString::fromUtf8(S_ZVUK_LOG) + id);
        QVariantList odna;
        odna.append(item);
        startMusic(odna, 0);
        return;
    }

    // Всё остальное -- папка: библиотека, сериал, сезон, альбом.
    note(QString::fromUtf8(S_OTKRYVAYU) + item.value("Name").toString());
    _api->loadChildren(item);
}

void JfWindow::rememberScreen()
{
    // Самый большой экран, виденный за время работы.
    //
    // У E90 их два: 240x320 снаружи и 800x352 под крышкой. Приложение
    // запускается на том, который сейчас активен, и если спрашивать размер в
    // момент запуска плеера, то открытая по дороге крышка даст картинку на
    // треть экрана. Поэтому берётся наибольший из виденных: он же и тот, на
    // котором человек будет смотреть.
    const QRect screen = QApplication::desktop()->screenGeometry();
    // Кладём набок. Видео на телефоне всегда показывается в ширину: на
    // обычном S60 экран 240x320 стоймя, а плеер разворачивает картинку и
    // занимает 320x240. Если назвать серверу экран как есть, он закодирует
    // картинку стоймя -- высокую и узкую, -- и плеер будет вписывать её в
    // лежачий экран, оставив от неё четверть.
    const int wide = qMax(screen.width(), screen.height());
    const int tall = qMin(screen.width(), screen.height());
    if (wide * tall > _bigW * _bigH) {
        _bigW = wide;
        _bigH = tall;
    }
    // Проверка на _api здесь не лишняя: функция зовётся и при показе
    // окна, и из конструктора, а порядок в конструкторе уже один раз
    // стоил работоспособности всей сборки.
    if (_api && _bigW > 0 && _bigH > 0)
        _api->setScreen(_bigW, _bigH);
}

// Откуда смотреть. Это и есть вся перемотка, какая здесь возможна.
//
// У RealPlayer на этом телефоне во время показа вверх-вниз не делают
// ничего, а лево-право меняют громкость: ни шкалы, ни перемотки у него
// нет, и это не наша недоделка. Исходников плеера в открытом виде не
// существует, патчить нечего. Значит место выбирается ДО запуска -- здесь,
// в приложении, которое и так знает и запись, и где просмотр прервался.
//
// Три строки, не больше: на кнопочном телефоне каждая строка меню -- это
// нажатие вниз. «Продолжить» стоит первой и выбрана заранее, поэтому
// обычное продолжение просмотра стоит одного нажатия, как и было.
void JfWindow::playFrom(const QVariantMap &item)
{
    // Музыку останавливаем ДО вопроса, а не перед самим запуском плеера.
    //
    // Владелец на 0.3.5: «при попытке вызвать серию спросил "откуда
    // смотреть" и завис на этом». Это окно вопроса -- СВОЙ цикл событий, и
    // внутри него продолжают приходить и таймер-сторож плеера, и готовая
    // закачка следующей песни: смена источника у Phonon изнутри модального
    // окна -- верный способ получить зависание. Пока не доказано, что дело
    // именно в этом, но уступить звук заранее всё равно надо: человек
    // собрался смотреть видео, музыка всё равно смолкнет.
    if (_music && _music->pauseForOther()) {
        _pausedForVideo = true;
        writeLog(QString::fromUtf8(S_MUZ_USTUPIL));
    }

    // Запись в журнал ДО ОКНА, а не после.
    //
    // 0.3.5 зависла ровно здесь («спросил "откуда смотреть" и завис на
    // этом»), и в журнале не осталось ничего: последняя строка была про
    // доехавшую песню. Потому что писать было нечего -- первая запись стояла
    // уже после выбора. Теперь видно, дошли ли мы до вопроса вообще.
    //
    // То же правило спасло бы и перемотку: там `seek` вешал телефон ДО
    // строки «перемотка», и в журнале её нет ни одной, хотя нажатий было
    // несколько.
    writeLog(QString::fromUtf8(S_OTKUDA_LOG)
                 + item.value("Name").toString());

    const QString id = item.value("Id").toString();
    const qint64 watched = watchedMs(item);
    // Длительность -- чтобы не дать выбрать минуту за концом записи.
    // Обычным делением делить нельзя (см. asClock), 64-битным можно.
    const qint64 vsego = item.value("RunTimeTicks").toLongLong()
                         / Q_INT64_C(10000000);
    const int predel = vsego > 60 ? int(vsego / Q_INT64_C(60)) : 600;

    const bool est = watched > 10000;
    QStringList ways;
    if (est)
        ways << QString::fromUtf8(S_PRODOLZHIT_S) + asClock(watched);
    ways << QString::fromUtf8(est ? S_S_NACHALA : S_SMOTRET);
    ways << QString::fromUtf8(S_VYBRAT_MESTO);

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, item.value("Name").toString(),
        QString::fromUtf8(S_OTKUDA_SMOTRET), ways, 0, false, &ok);
    if (!ok) {
        writeLog(QString::fromUtf8(S_OTKUDA_OTMENA));
        return;
    }
    const int row = ways.indexOf(chosen);
    writeLog(QString::fromUtf8(S_OTKUDA_VYBOR) + chosen);

    if (row == ways.size() - 1) {
        // Минуты, а не секунды. Искать в фильме с точностью до секунды на
        // телефонной клавиатуре незачем, а короткое число и набирается
        // быстрее, и целиком видно на экране в 240 точек.
        const int bylo = int(watched / Q_INT64_C(60000));
        writeLog(QString::fromUtf8(S_OTKUDA_MINUTY));
        ok = false;
        const int given = QInputDialog::getInt(
            this, QString::fromUtf8(S_S_KAKOY_MINUTY),
            QString::fromUtf8(S_VSEGO_MINUT).arg(predel),
            qBound(0, bylo, predel), 0, predel, 1, &ok);
        if (!ok)
            return;
        playVideo(id, qint64(given) * Q_INT64_C(60000));
        return;
    }
    playVideo(id, (est && row == 0) ? watched : 0);
}

void JfWindow::playVideo(const QString &itemId, qint64 positionMs)
{
    // Звук уступаем ДО запуска плеера, а не ждём, пока нас выгонят. У
    // телефона одно звуковое устройство, и музыка всё равно смолкнет -- но
    // уйдя сама, она знает своё место в песне и возвращается туда же.
    if (_music && _music->pauseForOther()) {
        _pausedForVideo = true;
        writeLog(QString::fromUtf8(S_MUZ_USTUPIL));
    }

    rememberScreen();
    // Чтобы было что перечитать по возвращении: место просмотра отмечает
    // компаньон, пока человек смотрит, и в нашем списке оно устаревает.
    _watchedId = itemId;
    const QString url = _api->streamUrl(itemId, positionMs);
    note(QString::fromUtf8(S_OTDAYU_PLEERU_S) + asClock(positionMs));
    writeLog(QString::fromUtf8(S_POTOK_LOG) + url);
    QDesktopServices::openUrl(QUrl(url));
}

// --- музыка ----------------------------------------------------------------

// Плеер создаётся по первому нажатию на песню.
//
// Нарочно не в конструкторе. Вместе с ним в сборке появляется phonon.dll, а
// про запуск на этом телефоне мы уже знаем: одна неудачная зависимость или
// один вызов не в том порядке -- и приложение молча не стартует, а причину
// ищут сборкой-щупом (ЗАМЕТКИ, 0.2.2 и 0.2.3). Здесь же, если что-то пойдёт
// не так, беда случится на понятном действии и будет видна в журнале.
JfMusic *JfWindow::music()
{
    if (_music)
        return _music;
    _music = new JfMusic(_api, muzPapka(), muzPredel(), this);
    // Карта может пропасть на ходу: тогда музыка уедет в память телефона, а
    // не перестанет качаться совсем.
    _music->setFallbackDir(folder() + QLatin1String("/music"));
    QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
    _music->setBitrate(muzKbit());
    _music->setShuffle(keep.value("music_shuffle").toInt() == 1);
    _music->setRepeat(keep.value("music_repeat").toInt() == 1);
    // Громкость: четыре ступени из десяти, если ничего не выбрано. Phonon
    // по умолчанию ставит максимум, и на 0.3.0 это оказалось невыносимо
    // громко -- поэтому умолчание задаём сами.
    _music->setVolumeStep(keep.contains("music_volume")
                              ? keep.value("music_volume").toInt() : 4);
    connect(_music, SIGNAL(changed()), this, SLOT(musicChanged()));
    connect(_music, SIGNAL(progress()), this, SLOT(musicProgress()));
    connect(_music, SIGNAL(says(QString)), this, SLOT(says(QString)));
    writeLog(QString::fromUtf8(estKarta() ? S_MUZ_KESH_GDE_KARTA
                                          : S_MUZ_KESH_GDE_TELEFON));
    return _music;
}

void JfWindow::startMusic(const QVariantList &tracks, int from,
                          const QVariantMap &parent, int total)
{
    // В очередь идут только песни: в «продолжить просмотр» лежат вперемешку
    // серии и фильмы, и ставить их в очередь музыки нечего.
    QVariantList queue;
    int start = 0;
    for (int i = 0; i < tracks.size(); ++i) {
        const QVariantMap one = tracks.at(i).toMap();
        if (one.value("Type").toString() != QLatin1String("Audio"))
            continue;
        if (i == from)
            start = queue.size();
        queue.append(one);
    }
    if (queue.isEmpty())
        return;
    writeLog(QString::fromUtf8(S_MUZ_OCHERED_LOG)
                 .arg(queue.size()).arg(start + 1));
    _queueParent = parent;
    _queueLoaded = tracks.size();
    _queueTotal = total;
    _queueFetching = false;
    _queueRandomFlight = false;
    // Вперемешку по большой папке -- случайную страницу просим сразу.
    _queueEager = music()->shuffle() && total > tracks.size();
    _queueDry = false;
    music()->play(queue, start);
    openMusicPage();
}

void JfWindow::openMusicPage()
{
    ensureRootMusicRow();
    if (!_stack.isEmpty() && _stack.last().kind == QLatin1String("muzyka")) {
        refreshMusic();
        return;
    }
    Level level;
    level.title = QString::fromUtf8(S_MUZ_ZAGOLOVOK);
    level.kind = QLatin1String("muzyka");
    level.items = musicRows();
    level.chosen = 0;
    push(level);
    refreshMusic();
}

// Табло плеера: четыре строки над списком.
//
//   Название песни
//   Исполнитель · 3 из 12 · играет
//   1:23 [====------] 3:47
//   Громкость [====------] 40%
//
// Полоски из «=» и «-», а не из красивых знаков заполнения: что именно есть
// в шрифте телефона 2007 года, снаружи не проверить, а пустой квадрат вместо
// полоски хуже, чем простые чёрточки.
// Бегущая строка для ТАБЛО.
//
// Та же мысль, что у бегущей строки списка, но текст здесь не в списке, а в
// надписи, и ширина своя -- узкая колонка рядом с обложкой. Влезает --
// отдаём как есть, иначе крутим по кругу с просветом, чтобы было видно, где
// строка кончилась.
QString JfWindow::runLine(const QString &full, int width) const
{
    if (full.isEmpty())
        return full;
    const QFontMetrics fm(_panel->font());
    if (fm.width(full) <= width)
        return full;
    const QString krug = full + QLatin1String("   ");
    int pos = _panelPos;
    if (pos >= krug.size())
        pos = 0;
    return krug.mid(pos) + krug.left(pos);
}

// Табло: три КОРОТКИЕ строки рядом с обложкой.
//
// Ровно три, всегда: название, исполнитель, место в очереди. Переноса нет,
// длинное едет бегущей строкой. Полоски времени и громкости сюда не ставим
// -- им тут не хватает ширины, они живут отдельной строкой во всю ширину
// (см. musicBars).
QString JfWindow::musicPanel() const
{
    if (!_music || !_music->active())
        return QString();
    const QVariantList queue = _music->queue();
    const int index = _music->currentIndex();
    QVariantMap one;
    if (index >= 0 && index < queue.size())
        one = queue.at(index).toMap();

    // Ширину берём у самой надписи: она зависит от того, показана обложка
    // или нет, а обложки у альбома может и не быть.
    const int width = _panel->width() > 0 ? _panel->width() - 2 : 130;

    QString text = runLine(one.value("Name").toString(), width);

    QString by = one.value("AlbumArtist").toString();
    if (by.isEmpty())
        by = one.value("Album").toString();
    text += QLatin1String("\n") + runLine(by, width);

    // Счётчик -- по ПОРЯДКУ воспроизведения, а не по номеру строки в
    // очереди: при перемешивании номер строки скачет, и это выглядит
    // поломкой -- «8 из 15», нажал «следующая», стало «6 из 15».
    // Место в порядке растёт на каждой песне и говорит, сколько
    // папки осталось.
    text += QLatin1String("\n") + QString::number(_music->place() + 1)
        + QString::fromUtf8(S_MUZ_IZ) + QString::number(queue.size());
    return text;
}

// Полоски: время и громкость, одной строкой во всю ширину экрана.
//
// Длину песни телефон СООБЩАЕТ не сразу. Поэтому порядок такой: есть длина
// -- время с полоской; длины ещё нет, но песня идёт -- просто сколько
// сыграно; нет ничего -- одно слово о состоянии. Громкость рядом в любом
// случае: каждая строка на экране 240x320 на счету, а их всего восемь.
QString JfWindow::musicBars() const
{
    if (!_music || !_music->active())
        return QString();
    const qint64 vsego = _music->duration();
    const qint64 est = _music->position();
    const QString hvost = QLatin1String("  ")
        + QString::number(_music->volumePercent()) + QLatin1Char('%');

    if (vsego <= 0) {
        if (!_music->currentName().isEmpty() && est > 0)
            return asClock(est) + hvost;
        return QString::fromUtf8(_music->paused() ? S_MUZ_STOIT : S_MUZ_IDET)
            + hvost;
    }

    // Подбираем длину полоски так, чтобы строка влезла целиком. Начинаем с
    // десяти знаков и укорачиваем, пока не поместится; совсем не поместилась
    // -- отдаём без полоски, время важнее украшения.
    const int width = _bars->width() > 0 ? _bars->width() - 2 : 230;
    const QFontMetrics fm(_bars->font());
    for (int dlina = 10; dlina >= 4; --dlina) {
        const QString line = asClock(est) + QLatin1Char(' ')
            + polosa(est, vsego, dlina) + QLatin1Char(' ') + asClock(vsego)
            + hvost;
        if (fm.width(line) <= width)
            return line;
    }
    return asClock(est) + QLatin1Char('/') + asClock(vsego) + hvost;
}

// Обновить табло целиком -- и короткие строки, и полоски.
void JfWindow::setPanelText()
{
    _panel->setText(musicPanel());
    _bars->setText(musicBars());
}

// Восемь команд, за ними очередь. Команды строками, а не скрытыми клавишами:
// на кнопочном телефоне невидимое управление не существует -- владелец на
// 0.3.0 сказал про джойстик «как бы мотает, а может переключает треки»,
// потому что догадаться было не о чем. Поэтому у каждой строки в скобках
// написана её клавиша, а что получилось -- сразу видно на табло выше.
QVariantList JfWindow::musicRows() const
{
    QVariantList rows;
    if (!_music)
        return rows;
    rows.append(musicRow(QLatin1String("pauza"),
        QString::fromUtf8(_music->paused() ? S_MUZ_PRODOLZHIT
                                           : S_MUZ_PAUZA_CMD)));
    rows.append(musicRow(QLatin1String("gromche"),
        QString::fromUtf8(S_MUZ_GROMCHE)));
    rows.append(musicRow(QLatin1String("tishe"),
        QString::fromUtf8(S_MUZ_TISHE)));
    rows.append(musicRow(QLatin1String("vpered"),
        QString::fromUtf8(S_MUZ_SLED_KL)));
    rows.append(musicRow(QLatin1String("nazad"),
        QString::fromUtf8(S_MUZ_PRED_KL)));
    rows.append(musicRow(QLatin1String("vperemeshku"),
        QString::fromUtf8(S_MUZ_VPEREMESHKU)
            + QString::fromUtf8(_music->shuffle() ? S_MUZ_VKL : S_MUZ_VYKL)));
    rows.append(musicRow(QLatin1String("povtor"),
        QString::fromUtf8(S_MUZ_POVTOR)
            + QString::fromUtf8(_music->repeat() ? S_MUZ_VKL : S_MUZ_VYKL)));
    rows.append(musicRow(QLatin1String("ochered"),
        QString::fromUtf8(S_MUZ_OCHERED_EKRAN)
            .arg(_music->queue().size())));
    rows.append(musicRow(QLatin1String("stop"),
        QString::fromUtf8(S_MUZ_OSTANOVIT)));
    return rows;
}

// Очередь -- отдельным экраном.
//
// Раньше она лежала прямо под командами, и при семидесяти девяти песнях
// экран плеера превращался в длинный список: до «Остановить» надо было
// прокручивать. Владелец спросил прямо -- «можно ли убрать прокрутку на
// экране плеера?». Теперь команды влезают целиком, а очередь открывается
// отдельно и закрывается нажатием на песню.
QVariantList JfWindow::queueRows() const
{
    QVariantList rows;
    if (!_music)
        return rows;
    const QVariantList queue = _music->queue();
    for (int i = 0; i < queue.size(); ++i) {
        const QVariantMap one = queue.at(i).toMap();
        QString name = titleOf(one, false);
        if (i == _music->currentIndex())
            name = QString::fromLatin1("> ") + name;
        rows.append(musicRow(QString::number(i), name));
    }
    return rows;
}

void JfWindow::openQueuePage()
{
    if (!_music || !_music->active())
        return;
    Level level;
    level.title = QString::fromUtf8(S_MUZ_OCHERED_ZAG);
    level.kind = QLatin1String("muzochered");
    level.items = queueRows();
    // Открываемся на той песне, что играет: искать её в семидесяти девяти
    // строках руками -- то же самое, что не иметь очереди.
    level.chosen = _music->currentIndex();
    push(level);
}

// Сколько строк занимают команды. Очереди под ними больше нет.
static const int MUZ_KOMAND = 9;

void JfWindow::musicChoice(int row)
{
    if (!_music || row < 0 || row >= MUZ_KOMAND)
        return;
    QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
    switch (row) {
    case 0:
        _music->toggle();
        break;
    case 1:
        _music->louder();
        saveVolume();
        break;
    case 2:
        _music->quieter();
        saveVolume();
        break;
    case 3:
        _music->next();
        break;
    case 4:
        _music->previous();
        break;
    case 5:
        _music->setShuffle(!_music->shuffle());
        keep.setValue("music_shuffle", _music->shuffle() ? 1 : 0);
        keep.sync();
        if (_music->shuffle()) {
            // Вперемешку по всей папке, а не по видимой странице.
            _queueEager = true;
            _queueDry = false;
            maybeExtendQueue();
        }
        break;
    case 6:
        _music->setRepeat(!_music->repeat());
        keep.setValue("music_repeat", _music->repeat() ? 1 : 0);
        keep.sync();
        break;
    case 7:
        openQueuePage();
        break;
    case 8:
        _music->stop();
        goBack();
        break;
    }
}

// Громкость запоминаем: выставлять её заново при каждом запуске -- это та же
// беда, что и «невероятно громко», только растянутая на все дни.
void JfWindow::saveVolume()
{
    if (!_music)
        return;
    QSettings keep(folder() + "/settings.ini", QSettings::IniFormat);
    keep.setValue("music_volume", _music->volumeStep());
    keep.sync();
}

void JfWindow::refreshMusic()
{
    if (_stack.isEmpty())
        return;
    // Экран очереди обновляем тоже: песня меняется сама, и пометка «>»
    // должна ехать вместе с ней.
    if (_stack.last().kind == QLatin1String("muzochered")) {
        _stack.last().chosen = _list->currentRow();
        _stack.last().items = queueRows();
        redraw();
        return;
    }
    if (_stack.last().kind != QLatin1String("muzyka"))
        return;
    // Место в списке запоминаем: без этого каждая смена песни выбрасывала бы
    // человека на первую строку -- а песни меняются сами, в том и смысл.
    _stack.last().chosen = _list->currentRow();
    _stack.last().items = musicRows();
    redraw();
    // Строку состояния НЕ трогаем: табло и так показывает и название, и
    // состояние, а дублировать их на экране в 240 точек -- отнимать
    // строку у списка. Сообщения (качаю, ошибка) туда по-прежнему
    // приходят сами, через says().
}

// Долив очереди с сервера.
//
// Папка может быть на тысячу песен, а приезжают они по пятьдесят -- и до
// 0.3.5 очередь была ровно той страницей, которую человек видел на экране:
// песни крутились между первой и пятидесятой. Теперь, подходя к концу
// загруженного, просим следующую страницу и дописываем её в хвост.
//
// Когда включено «вперемешку», просим у сервера СЛУЧАЙНЫЕ: перемешать можно
// только то, что приехало, а вперемешку по первым пятидесяти из тысячи -- это
// обман. Jellyfin умеет отдать случайные сам.
void JfWindow::maybeExtendQueue()
{
    if (!_music || !_music->active() || _queueFetching || _queueDry)
        return;
    if (_queueParent.value("Id").toString().isEmpty())
        return;   // очередь не из папки: одна песня или прошлый запуск
    if (_music->queue().size() >= JfMusic::queueCap())
        return;

    const bool random = _music->shuffle();
    // При случайном порядке «сколько уже спросили» ничего не значит: сервер
    // каждый раз отдаёт другие. При обычном -- кончились, значит кончились.
    if (!random && _queueLoaded >= _queueTotal)
        return;
    // Доливаем заранее: пока доедет страница, песни успеют доиграть.
    // Исключение -- «вперемешку» только что включили: тогда случайную
    // страницу просим не дожидаясь, иначе первые песни были бы выбраны
    // из той же пятидесятки, что на экране.
    // Близость к концу -- тоже по порядку воспроизведения. По номеру
    // строки очереди при перемешивании это была случайность: долив
    // мог сработать на восьмой песне из пятнадцати, а перед
    // последней -- не сработать вовсе.
    if (!_queueEager && _music->place() < _music->orderSize() - 5)
        return;
    _queueEager = false;

    _queueFetching = true;
    _queueRandomFlight = random;
    _api->loadChildren(_queueParent, _queueLoaded,
                       QLatin1String("ochered"), random);
}

// Окно снова стало главным -- значит системный плеер закрыли.
//
// Музыку продолжаем сами: остановили её мы, не человек, и оставить её лежать
// -- значит заставить его идти в плеер и нажимать «Продолжить» за нашу же
// самодеятельность.
void JfWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::ActivationChange)
        return;
    if (!isActiveWindow())
        return;

    if (_pausedForVideo) {
        _pausedForVideo = false;
        if (_music && _music->active() && _music->paused()) {
            writeLog(QString::fromUtf8(S_MUZ_VERNUL));
            _music->toggle();
        }
    }

    // Вернулись из системного плеера -- перечитать запись.
    //
    // Не здесь и не сейчас: мы внутри чужого события, а на этом телефоне
    // работа изнутри чужого цикла событий уже дважды кончалась зависанием.
    // Поэтому только заказываем, а спрашиваем на ближайшем свободном шаге.
    if (!_watchedId.isEmpty())
        QTimer::singleShot(0, this, SLOT(refreshWatched()));
}

// Перечитать запись, которую только что смотрели.
//
// Место просмотра отмечает компаньон, пока идёт поток, -- приложение его не
// ставит и знать о нём не может. В списке лежит то, что приехало ДО запуска
// плеера, и без этого запроса «откуда смотреть» предлагало бы прежнюю
// минуту, а в адрес потока уходила бы она же. Со стороны это выглядит как
// «позиция не сохранилась», хотя на сервере она сохранена.
void JfWindow::refreshWatched()
{
    if (_watchedId.isEmpty())
        return;
    const QString id = _watchedId;
    _watchedId.clear();
    writeLog(QString::fromUtf8(S_PERECHITYVAYU));
    _api->loadOne(id);
}

void JfWindow::toPlayer()
{
    if (_music && _music->active())
        openMusicPage();
}

// Плеер сообщил, что что-то изменилось.
//
// Перерисовку мы здесь НЕ ДЕЛАЕМ, и это не лень. Сигнал приходит изнутри
// вызова Phonon -- например прямо после `pause()`, не выходя из него, -- а на
// этом телефоне любое действие внутри чужого цикла событий уже дважды
// оборачивалось зависанием (следующая песня из `finished()` в 0.3.0, окно
// «откуда смотреть» в 0.3.5). Перестроить весь список, пересчитать ширины и
// перезапустить таймеры прямо там -- работа несоразмерная и ровно того же
// рода. Поэтому заказываем перерисовку на ближайший свободный момент, а
// несколько сигналов подряд сливаются в одну.
void JfWindow::musicChanged()
{
    if (_musicDirty)
        return;
    _musicDirty = true;
    QTimer::singleShot(60, this, SLOT(musicRedraw()));
}

void JfWindow::musicRedraw()
{
    // Плеер ещё внутри вызова -- подождём. Перерисовка оттуда и есть
    // то, от чего мы уходим.
    if (JfMusic::busy()) {
        QTimer::singleShot(60, this, SLOT(musicRedraw()));
        return;
    }
    _musicDirty = false;
    maybeExtendQueue();
    saveQueue();
    ensureRootMusicRow();
    refreshMusic();
}

// Раз в секунду. Трогаем ТОЛЬКО табло: перестроить список из сорока песен
// каждую секунду на 332 МГц -- значит занять телефон рисованием вместо
// музыки, да ещё и уводить выбранную строку из-под руки.
void JfWindow::musicProgress()
{
    if (JfMusic::busy())
        return;
    if (_stack.isEmpty() || _stack.last().kind != QLatin1String("muzyka"))
        return;
    setPanelText();
}

// Обложка на экране плеера.
//
// Картинку уменьшает САМ Jellyfin, до 96 точек -- это 5-10 КБ и мгновенный
// разбор. Если её нет (у альбома может не быть вовсе, или ещё не приехала),
// место под неё не занимаем: на экране 240 точек каждая строка на счету.
void JfWindow::showCover()
{
    const QString path = _music ? _music->coverPath() : QString();
    if (path.isEmpty() || !QFile::exists(path)) {
        _cover->clear();
        _coverShown.clear();
        _cover->hide();
        return;
    }
    // Указатель проверяем ОБЯЗАТЕЛЬНО: QLabel::pixmap() отдаёт НОЛЬ, когда
    // картинки нет, и обращение к нему -- это падение приложения. А ноль
    // здесь бывает законно: clear() картинку убирает, и если потом вернётся
    // та же самая песня, путь совпадёт, а картинки уже не будет.
    const QPixmap *now = _cover->pixmap();
    if (path == _coverShown && now && !now->isNull()) {
        _cover->show();
        return;   // та же самая -- не перечитываем файл
    }

    QPixmap picture;
    if (!picture.load(path)) {
        _cover->clear();
        _coverShown.clear();
        _cover->hide();
        return;
    }
    _cover->setPixmap(picture);
    _coverShown = path;
    _cover->show();
}

// Строка «Сейчас играет» первой в корне. Пока музыка идёт, к плееру нужно
// возвращаться из любого места, а стопка уровней к этому времени уже
// свёрнута -- искать его там негде.
void JfWindow::ensureRootMusicRow()
{
    // Левая софт-клавиша живёт ровно столько, сколько идёт музыка.
    if (_playerAction)
        _playerAction->setVisible(_music && _music->active());

    if (_stack.isEmpty() || _stack.first().kind != QLatin1String("views"))
        return;
    QVariantList &items = _stack.first().items;
    const bool est = !items.isEmpty()
        && items.first().toMap().value("Type").toString()
               == QLatin1String("Muzyka");
    const bool nado = (_music && _music->active()) || !_lastQueue.isEmpty();
    if (nado) {
        QVariantMap row;
        if (_music && _music->active()) {
            row = musicRow(QLatin1String("sejchas"),
                           QString::fromUtf8(S_MUZ_SEYCHAS)
                               + _music->currentName());
        } else {
            // Очередь прошлого запуска. Сама по себе она не играет: телефон
            // музыку фоном не держит, а начинать её без спроса при каждом
            // запуске -- это и сеть, и звук тогда, когда их не просили.
            const QVariantMap one = _lastQueue.at(_lastIndex).toMap();
            row = musicRow(QLatin1String("prodolzhit"),
                           QString::fromUtf8(S_MUZ_PRODOLZHIT_SLUSHAT)
                               + one.value("Name").toString());
        }
        if (est)
            items[0] = row;
        else
            items.prepend(row);
    } else if (est) {
        items.removeFirst();
    } else {
        return;
    }
    if (_stack.size() == 1) {
        _stack.first().chosen = _list->currentRow();
        redraw();
    }
}
