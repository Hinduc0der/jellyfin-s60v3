#include "jfapi.h"
#include "json.h"
#include "slova.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

// Свой журнал, тот же файл, что у окна. Разговор с сервером надо видеть
// целиком: на телефоне «ничего не произошло» и «ответ не разобрался»
// выглядят одинаково -- пустым экраном.
static void apiLog(const QString &text)
{
#ifdef Q_OS_SYMBIAN
    QDir("C:/Data").mkpath("Jellyfin");
    QFile file("C:/Data/Jellyfin/jellyfin.log");
#else
    QFile file(QDir::tempPath() + "/jellyfin.log");
#endif
    // Метка UTF-8 в пустой файл -- чтобы журнал читался на самом телефоне;
    // подробности в jfwindow.cpp, у такого же места.
    const bool pustoy = !file.exists() || file.size() == 0;
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (pustoy) {
                // Байтами, а не строкой с экранированием: так не
                // зависит ни от кодировки исходника, ни от знака char.
                const char metka[3] = { char(0xEF), char(0xBB), char(0xBF) };
                file.write(metka, 3);
            }
        QTextStream stream(&file);
        stream.setCodec("UTF-8");
        stream << QDateTime::currentDateTime().toString("hh:mm:ss")
               << QString::fromUtf8(S_SET_LOG) << text << endl;
    }
}

// Имя клиента и устройства сервер показывает в своих настройках, в списке
// устройств. Пусть там будет понятно, что это за телефон.
static const char *kClient = "Jellyfin for S60";
static const char *kDevice = "Symbian phone";

JfApi::JfApi(QObject *parent)
    : QObject(parent), _net(new QNetworkAccessManager(this))
{
    connect(_net, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(onReply(QNetworkReply*)));
    // Опознаватель устройства должен быть постоянным, иначе сервер при каждом
    // входе считает телефон новым устройством. Держится в настройках, а здесь
    // только значение по умолчанию.
    _deviceId = QString::fromLatin1("s60-jellyfin");
    _streamPort = 0;
    _screenW = 0;
    _screenH = 0;
}

void JfApi::setServer(const QString &address)
{
    _server = address;
    while (_server.endsWith(QChar('/')))
        _server.chop(1);
    if (!_server.isEmpty() && !_server.contains(QString::fromLatin1("://")))
        _server = QString::fromLatin1("http://") + _server;
    if (_streamHost.isEmpty())
        _streamHost = QUrl(_server).host();
}

void JfApi::setAuth(const QString &token, const QString &userId)
{
    _token = token;
    _userId = userId;
}

void JfApi::setupRequest(QNetworkRequest &request) const
{
    // Этот заголовок обязателен даже там, где токена ещё нет: без него
    // QuickConnect отвечает 400.
    const QString who = QString::fromLatin1(
        "MediaBrowser Client=\"%1\", Device=\"%2\", DeviceId=\"%3\", Version=\"%4\"")
        .arg(QString::fromLatin1(kClient))
        .arg(QString::fromLatin1(kDevice))
        .arg(_deviceId)
        .arg(QString::fromLatin1("0.1.7"));
    request.setRawHeader("X-Emby-Authorization", who.toLatin1());
    request.setRawHeader("Accept", "application/json");
    if (!_token.isEmpty())
        request.setRawHeader("X-Emby-Token", _token.toLatin1());
}

// Сторож на запрос. Qt 4.7 срока ожидания не имеет вовсе, а на телефоне
// запрос может не уйти совсем (нет канала, нет маршрута до сервера) --
// и тогда на экране навсегда остаётся последняя надпись. abort() приводит
// к обычному ответу с ошибкой, то есть человек видит причину.
//
// Связь с удалённым объектом безопасна: singleShot с получателем
// отвязывается сам, когда ответ уничтожается.
void JfApi::watch(QNetworkReply *reply)
{
    QTimer::singleShot(25000, reply, SLOT(abort()));
}

QNetworkReply *JfApi::get(const QString &path, const QString &key)
{
    QNetworkRequest request(QUrl(_server + path));
    setupRequest(request);
    QNetworkReply *reply = _net->get(request);
    reply->setProperty("kljuch", key);
    watch(reply);
    apiLog(QString::fromUtf8(S_SPRASHIVAYU).arg(path).arg(key));
    return reply;
}

QNetworkReply *JfApi::post(const QString &path, const QByteArray &body,
                           const QString &key)
{
    QNetworkRequest request(QUrl(_server + path));
    setupRequest(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QString::fromLatin1("application/json"));
    QNetworkReply *reply = _net->post(request, body);
    reply->setProperty("kljuch", key);
    watch(reply);
    apiLog(QString::fromUtf8(S_POSYLAYU)
               .arg(path).arg(key).arg(body.size()));
    return reply;
}

void JfApi::quickStart()
{
    _secret.clear();
    post(QString::fromLatin1("/QuickConnect/Initiate"), QByteArray(),
         QString::fromLatin1("quick-start"));
}

void JfApi::quickCheck()
{
    if (_secret.isEmpty())
        return;
    get(QString::fromLatin1("/QuickConnect/Connect?secret=") + _secret,
        QString::fromLatin1("quick-check"));
}

void JfApi::loadViews()
{
    get(QString::fromLatin1("/UserViews"), QString::fromLatin1("views"));
}

void JfApi::loadChildren(const QVariantMap &item, int from,
                         const QString &key, bool random)
{
    const QString id = item.value("Id").toString();
    const QString collection = item.value("CollectionType").toString();

    QString what;
    if (collection == QLatin1String("tvshows"))
        what = QString::fromLatin1("&IncludeItemTypes=Series&Recursive=true");
    else if (collection == QLatin1String("movies"))
        what = QString::fromLatin1("&IncludeItemTypes=Movie&Recursive=true");
    else if (collection == QLatin1String("music"))
        what = QString::fromLatin1("&IncludeItemTypes=MusicAlbum&Recursive=true");

    // Песни альбома -- по номеру дорожки, а не по названию.
    //
    // По названию они встают как в телефонной книге: «Ария» перед «Зимой»,
    // хотя на диске наоборот. Альбом слушают подряд, в том порядке, в каком
    // его записали, и очередь воспроизведения строится из этого же списка.
    QString poryadok = QString::fromLatin1("SortName");
    if (item.value("Type").toString() == QLatin1String("MusicAlbum"))
        poryadok = QString::fromLatin1("ParentIndexNumber,IndexNumber,SortName");
    if (random)
        poryadok = QString::fromLatin1("Random");

    // Лишние поля не просим: это лишние килобайты по воздуху и лишняя работа
    // разборщику на 332 МГц. UserData (где остановились) приходит и так.
    get(QString::fromLatin1("/Items?ParentId=%1&SortBy=%2%3"
                            "&EnableImages=false&StartIndex=%4&Limit=50")
            .arg(id).arg(poryadok).arg(what).arg(random ? 0 : from),
        key.isEmpty() ? QString::fromLatin1(from ? "more" : "children") : key);
}

void JfApi::loadOne(const QString &itemId)
{
    // Списком из одной записи, а не `/Items/<id>`: ответ тогда той же
    // формы, что у всех прочих списков, и разбирается тем же кодом.
    get(QString::fromLatin1("/Items?Ids=%1&EnableImages=false").arg(itemId),
        QString::fromLatin1("one"));
}

void JfApi::loadResume()
{
    get(QString::fromLatin1("/UserItems/Resume?Limit=12&MediaTypes=Video"
                            "&EnableImages=false"),
        QString::fromLatin1("resume"));
}

QString JfApi::imageUrl(const QString &itemId, int width) const
{
    return QString::fromLatin1("%1/Items/%2/Images/Primary"
                               "?maxWidth=%3&quality=80&format=Jpg")
        .arg(_server).arg(itemId).arg(width);
}

QString JfApi::streamUrl(const QString &itemId, qint64 positionMs) const
{
    // Форма адреса -- участком пути, без единого особого знака. Через собачку
    // (`/jf/<запись>@754`) RealPlayer зависает на «выполняется подключение»:
    // собачка в адресах отделяет имя пользователя от узла, и разборщик 2007
    // года читает адрес по-своему. Проверено на телефоне.
    const qint64 seconds = positionMs / 1000;
    QString host = _streamHost;
    if (_streamPort > 0 && _streamPort != 554)
        host += QString::fromLatin1(":%1").arg(_streamPort);
    QString url = QString::fromLatin1("rtsp://%1/jf/%2")
                      .arg(host).arg(itemId);
    // Пропуск к перекодировщику -- наш же токен Jellyfin.
    //
    // Зачем он в адресе. Перекодировщик на каждый запрос поднимает ffmpeg, а
    // снаружи его порт открыт всем. Без пропуска десяток запросов с улицы
    // кладёт машину, и это не предположение, а обычный способ досадить.
    // Своего пароля заводить не нужно: у нас уже есть токен, выданный
    // Jellyfin при входе по коду, и перекодировщик проверяет его у Jellyfin.
    if (!_token.isEmpty())
        url += QString::fromLatin1("/k%1").arg(_token);
    // Экран. Сервер по нему решает, какого размера картинку кодировать:
    // отдавать на 240x320 ту же, что на E90, -- значит втрое переплачивать
    // килобитами, а на мобильном канале они оплачиваются потерями.
    if (_screenW > 0 && _screenH > 0)
        url += QString::fromLatin1("/s%1x%2").arg(_screenW).arg(_screenH);
    if (seconds > 0)
        url += QString::fromLatin1("/at%1").arg(seconds);
    return url;
}

// Отметки места здесь нет нарочно -- и раньше была, мёртвым кодом.
//
// Отметить место может только тот, кто знает, докуда досмотрели. Мы, отдав
// адрес системному плееру, уходим: ни про паузы, ни про выход из плеера
// приложение не узнаёт вовсе. Поэтому место отмечает компаньон у Jellyfin
// -- он один видит и PAUSE, и TEARDOWN, и знает, сколько секунд записи
// отдал. Приложение это место только ЧИТАЕТ, из UserData.

void JfApi::onReply(QNetworkReply *reply)
{
    const QString key = reply->property("kljuch").toString();
    const QByteArray body = reply->readAll();
    const int code = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    apiLog(QString::fromUtf8(S_OTVET_NA)
               .arg(key).arg(code).arg(body.size()).arg(reply->error()));
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError && code != 200) {
        // Код ответа важнее текста ошибки Qt: по нему сразу видно, дело в
        // сети или в сервере.
        emit failed(QString::fromUtf8(S_SERVER_OTVETIL)
                        .arg(code ? QString::number(code)
                                  : QString::fromUtf8(S_NET_SVYAZI))
                        .arg(reply->errorString()));
        return;
    }

    bool parsed = false;
    const QVariant answer = QtJson::parse(QString::fromUtf8(body), parsed);
    if (!parsed) {
        emit failed(QString::fromUtf8(S_NE_RAZOBRALSYA)
                        .arg(body.size()));
        return;
    }
    const QVariantMap map = answer.toMap();

    if (key == QLatin1String("quick-start")) {
        _secret = map.value(QString::fromLatin1("Secret")).toString();
        emit quickCodeReady(map.value(QString::fromLatin1("Code")).toString());
    } else if (key == QLatin1String("quick-check")) {
        if (map.value(QString::fromLatin1("Authenticated")).toBool()) {
            const QString body2 = QString::fromLatin1(
                "{\"Secret\":\"%1\"}").arg(_secret);
            post(QString::fromLatin1("/Users/AuthenticateWithQuickConnect"),
                 body2.toLatin1(), QString::fromLatin1("quick-done"));
            emit says(QString::fromUtf8(S_KOD_PODTVERZHDEN));
        }
    } else if (key == QLatin1String("quick-done")) {
        _token = map.value(QString::fromLatin1("AccessToken")).toString();
        _userId = map.value(QString::fromLatin1("User")).toMap()
                      .value(QString::fromLatin1("Id")).toString();
        if (_token.isEmpty())
            emit failed(QString::fromUtf8(S_NET_KLYUCHA));
        else
            emit signedIn();
    } else if (key == QLatin1String("position")) {
        // Ответа не ждём, но ошибку покажем.
    } else {
        // Списки записей приходят либо как {Items: [...]}, либо массивом.
        QVariantList items = map.value(QString::fromLatin1("Items")).toList();
        if (items.isEmpty() && answer.type() == QVariant::List)
            items = answer.toList();
        const int total = map.value(QString::fromLatin1(
            "TotalRecordCount")).toInt();
        emit itemsReady(key, items, total ? total : items.size());
    }
}
