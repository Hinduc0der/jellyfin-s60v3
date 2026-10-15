#ifndef JFAPI_H
#define JFAPI_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

// Разговор с Jellyfin.
//
// Всё общение -- обычный HTTP с JSON. Токен уходит заголовком X-Emby-Token,
// а при входе -- заголовком X-Emby-Authorization, в котором сервер хочет
// видеть имя клиента и опознаватель устройства. БЕЗ этого заголовка
// QuickConnect отвечает «400 Bad Request» -- проверено на 10.10.7, и по
// одному коду ответа догадаться об этом было бы невозможно.
class JfApi : public QObject
{
    Q_OBJECT

public:
    explicit JfApi(QObject *parent = 0);

    void setServer(const QString &address);
    QString server() const { return _server; }
    void setAuth(const QString &token, const QString &userId);
    QString token() const { return _token; }
    QString userId() const { return _userId; }
    bool signedIn() const { return !_token.isEmpty(); }

    // Опознаватель устройства. Сервер по нему помнит, что это тот же телефон,
    // и показывает его в своих настройках -- так вход можно отозвать.
    QString deviceId() const { return _deviceId; }

    // --- вход без набора пароля ---
    // Телефон показывает шестизначный код, владелец вводит его в Jellyfin на
    // большом экране. Пароль на этой клавиатуре не набирается никогда.
    void quickStart();
    void quickCheck();

    // --- библиотека ---
    void loadViews();                                   // ключ "views"
    // Детей спрашиваем ПО-РАЗНОМУ, глядя на то, что открыли.
    //
    // Простой запрос детей раздела «Сериалы» отдаёт 393 серии вперемешку, без
    // всякого деления на сериалы и сезоны: Jellyfin понимает ParentId у
    // раздела по-своему. Поэтому у раздела спрашиваем ровно то, что нужно
    // (сериалы, фильмы или альбомы) и с обходом вложенности, а у сериала,
    // сезона и альбома -- обычных прямых детей.
    // Страницами, по 50 записей.
    //
    // Так надо по двум причинам. Ответ на 206 сериалов -- 111 КБ, и это
    // килобайты по воздуху плюс разбор JSON на 332 МГц при 32 МБ памяти.
    // И пользоваться списком из 206 строк на кнопочном телефоне невозможно:
    // до середины ехать сто нажатий.
    // key -- под каким именем придёт ответ. Нужен, чтобы доливать ОЧЕРЕДЬ
    // музыки, не трогая то, что человек видит на экране.
    //
    // random -- просить вперемешку у СЕРВЕРА. Это не прихоть: в папке может
    // лежать тысяча песен, а приезжает их по пятьдесят. Перемешать можно
    // только загруженное, и «вперемешку» по первым пятидесяти из тысячи --
    // обман. Jellyfin умеет отдать случайные сам, и тогда каждая новая
    // страница приходит из всей папки целиком.
    void loadChildren(const QVariantMap &item, int from = 0,
                      const QString &key = QString(), bool random = false);
    void loadResume();                                  // ключ "resume"
    // Перечитать ОДНУ запись: ключ "one".
    //
    // Нужно после возврата из системного плеера. Место просмотра
    // отмечает компаньон, пока человек смотрит, а у нас в списке лежит
    // то, что приехало до запуска. Без перечитывания «откуда смотреть»
    // предлагает старую минуту, и выглядит это как «позиция не
    // сохранилась», хотя на сервере она сохранена.
    void loadOne(const QString &itemId);

    // Картинка нужного размера -- Jellyfin уменьшает её САМ, на сервере.
    // Телефону нельзя отдавать постер в 1000 точек: на разбор такой картинки
    // уходит и память, и время.
    QString imageUrl(const QString &itemId, int width) const;

    // Адрес потока для системного плеера. Его отдаёт НЕ Jellyfin, а наш
    // сервер потока (в продукте -- плагин), поэтому адрес свой.
    void setStreamHost(const QString &host) { _streamHost = host; }
    QString streamHost() const { return _streamHost; }

    // Порт потока. Ноль означает обычный 554, и тогда в адресе он не
    // пишется вовсе -- плеер подставит его сам.
    //
    // Зачем настройка. На домашних роутерах 554 нередко занят встроенным
    // помощником RTSP: он перехватывает этот порт, чтобы «правильно»
    // разбирать переговоры, и проброс до нашей машины просто не доходит.
    // Проверено на Redmi AX6: правило на 554 структурно не отличается от
    // рабочих, а снаружи порт ведёт себя как непроброшенный. Лечится
    // пробросом на другой внешний порт -- но тогда его надо где-то назвать.
    void setStreamPort(int port) { _streamPort = port; }
    int streamPort() const { return _streamPort; }

    // Экран, под который просить картинку.
    //
    // Называет его приложение, а не человек: свой экран оно знает, а человек
    // про размеры картинки знать не обязан. Для сервера это значит, что один
    // и тот же компаньон обслуживает и E90 с его 800x352, и обычный телефон
    // с 240x320, ничего не настраивая руками.
    //
    // Ноль -- не называть вовсе: тогда сервер возьмёт своё умолчание. Так
    // ведут себя старые сборки, и ломать их не нужно.
    void setScreen(int width, int height) { _screenW = width; _screenH = height; }
    int screenWidth() const { return _screenW; }
    QString streamUrl(const QString &itemId, qint64 positionMs) const;

    // Отметки места здесь нет: её ставит компаньон, а не мы. Почему --
    // в jfapi.cpp, там же, где она раньше лежала.

signals:
    void says(const QString &text);
    void failed(const QString &why);
    void quickCodeReady(const QString &code);
    void signedIn();
    void itemsReady(const QString &key, const QVariantList &items, int total);

private slots:
    void onReply(QNetworkReply *reply);

private:
    QNetworkReply *get(const QString &path, const QString &key);
    QNetworkReply *post(const QString &path, const QByteArray &body,
                        const QString &key);
    void setupRequest(class QNetworkRequest &request) const;
    void watch(QNetworkReply *reply);   // срок ожидания для запроса

    QNetworkAccessManager *_net;
    QString _server;
    QString _streamHost;
    int _streamPort;
    int _screenW;
    int _screenH;
    QString _token;
    QString _userId;
    QString _deviceId;
    QString _secret;      // для QuickConnect
};

#endif // JFAPI_H
