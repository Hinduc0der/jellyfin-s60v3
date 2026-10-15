#include "jfmusic.h"

#include "jfapi.h"
#include "slova.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <phonon/audiooutput.h>
#include <phonon/mediaobject.h>
#include <phonon/mediasource.h>
#include <phonon/path.h>

// Остаток от деления -- ТОЛЬКО через 64 бита.
//
// Обычный `%` над int компилятор превращает в вызов __aeabi_idivmod, а его
// на E90 нет: приложение не запускается вовсе, «функция не поддерживается».
// На этом уже потеряна одна установка (ЗАМЕТКИ, про asClock). 64-битный
// остаток идёт через другую служебную функцию, которая на телефоне есть.
static int ostatok(int a, int b)
{
    if (b <= 0)
        return 0;
    return int(qint64(a) % qint64(b));
}

// Громкость: одиннадцать готовых чисел вместо счёта.
//
// Phonon ждёт долю от нуля до единицы. Посчитать её из процентов -- значит
// разделить, а деление на этом телефоне идёт через служебные функции, часть
// которых отсутствует (на целочисленной __aeabi_idivmod приложение однажды
// просто не запустилось). Таблица не считает ничего.
//
// Числа НЕ равномерные, и это важно. Прямая шкала 0.1, 0.2, ... давала на
// телефоне тишину в двух нижних ступенях: владелец сказал, что и в динамике,
// и в наушниках до 20% не слышно ничего, то есть на деле громкость начинала
// работать с третьей ступени. Слух к тому же устроен не прямо: разница между
// 0.1 и 0.2 слышна куда сильнее, чем между 0.9 и 1.0.
//
// Поэтому первая ступень поднята до слышимой, а дальше шаг растёт плавно.
// Ноль остаётся настоящим нулём -- это тишина по требованию.
static const qreal GROMKOST[11] = {
    qreal(0.00), qreal(0.20), qreal(0.28), qreal(0.36), qreal(0.45),
    qreal(0.54), qreal(0.63), qreal(0.72), qreal(0.81), qreal(0.90),
    qreal(1.00)
};

bool JfMusic::_busy = false;

// Сторож на время вызова медиа-части.
//
// Ставит флаг на входе и снимает на любом выходе. Нужен потому, что
// внутри вызовов Phonon крутится обычный цикл событий, и всё, что в это
// время рисует, может войти внутрь самого себя -- а на этом телефоне
// это зависание.
class Zanyat
{
public:
    Zanyat() { JfMusic::setBusy(true); }
    ~Zanyat() { JfMusic::setBusy(false); }
};

JfMusic::JfMusic(JfApi *api, const QString &cacheDir, qint64 predel,
                 QObject *parent)
    : QObject(parent), _api(api), _dir(cacheDir), _predel(predel), _net(0),
      _reply(0), _file(0), _fetchNow(false), _got(0), _media(0), _out(0),
      _watch(0), _place(0), _current(-1), _shuffle(false), _repeat(false),
      _paused(false), _expect(false), _wasPlaying(false), _bitrate(128),
      _vol(4), _pos(0), _total(0), _coverReply(0)
{
    if (_predel < 4 * 1024 * 1024)
        _predel = 4 * 1024 * 1024;   // меньше двух песен держать бессмысленно
    _net = new QNetworkAccessManager(this);
    QDir().mkpath(_dir);
    qsrand(uint(QDateTime::currentDateTime().toTime_t()));
    // Огрызки прошлого запуска -- первым делом. Недокачанный файл не играет,
    // пользы от него нет никакой, а место он занимает настоящее.
    sweepPart();

    // Сторож. Сигнал finished() у Phonon на Symbian приходит не всегда -- а
    // если он не придёт, следующая песня не заиграет, и всё это снова
    // превратится в «ткни ещё раз». Поэтому раз в две секунды смотрим сами:
    // играли и вдруг остановились -- значит трек кончился.
    _watch = new QTimer(this);
    _watch->setInterval(2000);
    connect(_watch, SIGNAL(timeout()), this, SLOT(watchdog()));
}

JfMusic::~JfMusic()
{
    abortFetch();
}

QString JfMusic::pathFor(const QString &id) const
{
    // Расширение обязательно: Phonon на Symbian определяет тип файла по нему
    // и без расширения отвечает «media type could not be determined».
    return _dir + QLatin1String("/") + id + QLatin1String(".mp3");
}

QString JfMusic::urlFor(const QString &id) const
{
    // Транскодирует сам Jellyfin, и это важно: в библиотеке лежат и flac, и
    // m4a, которых телефон не играет. Просим mp3 -- если исходник уже mp3 и
    // не толще заказанного, сервер отдаёт его как есть, не тратя ничего.
    //
    // Про качество сказано двумя именами нарочно: у Jellyfin на этом пути
    // оно называется audioBitRate, а на соседнем (universal) --
    // maxStreamingBitrate. Лишнее имя сервер просто не замечает, а вот
    // угадать неверно -- это молча получить поток в 320 кбит/с и втрое
    // большее ожидание первой песни.
    return QString::fromLatin1(
               "%1/Audio/%2/stream.mp3?audioCodec=mp3"
               "&audioBitRate=%3&maxStreamingBitrate=%3&api_key=%4")
        .arg(_api->server()).arg(id)
        .arg(_bitrate * 1000).arg(_api->token());
}

QString JfMusic::currentName() const
{
    if (_current < 0 || _current >= _queue.size())
        return QString();
    return _queue.at(_current).toMap().value("Name").toString();
}

QString JfMusic::stateText() const
{
    if (_current < 0)
        return QString();
    if (!_fetching.isEmpty() && _fetchNow)
        return QString::fromUtf8(S_MUZ_KACHAYU) + currentName();
    if (_paused)
        return QString::fromUtf8(S_MUZ_PAUZA) + currentName();
    return QString::fromUtf8(S_MUZ_IGRAET) + currentName();
}

// --- очередь ---------------------------------------------------------------

void JfMusic::play(const QVariantList &tracks, int from)
{
    _queue = tracks;
    rebuildOrder(from);
    startTrack(from);
}

int JfMusic::appendToQueue(const QVariantList &tracks)
{
    if (_queue.isEmpty() || _queue.size() >= queueCap())
        return 0;

    int added = 0;
    for (int i = 0; i < tracks.size(); ++i) {
        const QVariantMap one = tracks.at(i).toMap();
        if (one.value("Type").toString() != QLatin1String("Audio"))
            continue;
        const QString id = one.value("Id").toString();
        if (id.isEmpty())
            continue;

        // Повторы отсеиваем: при случайном порядке сервер честно может
        // прислать то, что уже стоит в очереди, и песня сыграла бы дважды
        // подряд.
        bool byla = false;
        for (int j = 0; j < _queue.size() && !byla; ++j) {
            if (_queue.at(j).toMap().value("Id").toString() == id)
                byla = true;
        }
        if (byla)
            continue;

        _queue.append(one);
        // Место в порядке воспроизведения -- в конец ХВОСТА, то есть после
        // того, что ещё не сыграло. При перемешивании ставим на случайное
        // место в этом же хвосте: дописывать всё время в конец значило бы,
        // что новые песни всегда последние.
        const int index = _queue.size() - 1;
        if (_shuffle && _place + 1 < _order.size()) {
            const int kuda = _place + 1
                + ostatok(qrand(), _order.size() - _place - 1);
            _order.insert(kuda, index);
        } else {
            _order.append(index);
        }
        ++added;
        if (_queue.size() >= queueCap())
            break;
    }

    if (added > 0)
        emit changed();
    return added;
}

void JfMusic::rebuildOrder(int keepIndex)
{
    _order.clear();
    for (int i = 0; i < _queue.size(); ++i)
        _order.append(i);

    if (_shuffle && _queue.size() > 1) {
        // Тасование Фишера -- Йетса. Остаток берём через 64 бита (см. выше).
        for (int i = _order.size() - 1; i > 0; --i) {
            const int j = ostatok(qrand(), i + 1);
            const int t = _order[i];
            _order[i] = _order[j];
            _order[j] = t;
        }

        // Нажатая песня -- В НАЧАЛО порядка. Это не украшение, а исправление
        // настоящего обрыва.
        //
        // Раньше она оставалась там, куда её закинуло тасование, и всё, что
        // встало ПЕРЕД ней, не играло никогда: очередь кончалась, не дойдя
        // до начала списка. В журнале владельца это видно дважды: из 14
        // песен сыграло 6, из 16 -- ровно одна. Случайность места и решала,
        // сколько достанется: в среднем пропадала половина папки.
        //
        // Текущую песню это не обрывает -- порядок меняется, а
        // воспроизведение здесь не трогается вовсе.
        for (int i = 0; i < _order.size(); ++i) {
            if (_order.at(i) == keepIndex) {
                _order.removeAt(i);
                break;
            }
        }
        _order.prepend(keepIndex);
    }

    // Где сейчас стоит играющая песня. При перемешивании она первая, при
    // обычном порядке -- на своём месте в списке: выключить перемешивание
    // посреди альбома и значит продолжить альбом подряд, с этого места.
    _place = 0;
    for (int i = 0; i < _order.size(); ++i) {
        if (_order.at(i) == keepIndex) {
            _place = i;
            break;
        }
    }
}

int JfMusic::nextIndex(bool forward) const
{
    if (_order.isEmpty())
        return -1;
    int place = _place + (forward ? 1 : -1);
    if (place >= _order.size())
        place = _repeat ? 0 : -1;
    if (place < 0 && forward)
        return -1;
    if (place < 0)
        place = 0;
    return place;
}

void JfMusic::startTrack(int index)
{
    if (index < 0 || index >= _queue.size())
        return;
    _current = index;
    _paused = false;
    _wasPlaying = false;
    _pos = 0;

    wantCover(_queue.at(index).toMap());

    const QString id = _queue.at(index).toMap().value("Id").toString();
    // Подрезка ПЕРЕД загрузкой, а не после. В 0.3.0 она звалась только по
    // окончании закачки, и песни копились до предела, а предел стоял 40 МБ --
    // во внутренней памяти телефона это вся свободная память. Теперь каждая
    // новая песня сначала освобождает место под себя.
    _keepId = id;
    prune();
    const QString path = pathFor(id);
    if (QFile::exists(path)) {
        playFile(path);
        return;
    }
    emit says(QString::fromUtf8(S_MUZ_KACHAYU) + currentName());
    emit changed();
    fetch(id, true);
}

void JfMusic::jumpTo(int index)
{
    if (index < 0 || index >= _queue.size())
        return;
    for (int i = 0; i < _order.size(); ++i) {
        if (_order.at(i) == index) {
            _place = i;
            break;
        }
    }
    startTrack(index);
}

void JfMusic::next()
{
    const int place = nextIndex(true);
    if (place < 0) {
        emit says(QString::fromUtf8(S_MUZ_KONEC));
        stop();
        return;
    }
    _place = place;
    startTrack(_order.at(_place));
}

void JfMusic::previous()
{
    const int place = nextIndex(false);
    if (place < 0)
        return;
    _place = place;
    startTrack(_order.at(_place));
}

void JfMusic::toggle()
{
    if (!_media || _current < 0)
        return;
    if (_paused) {
        _paused = false;
        const QString path = pathFor(
            _queue.at(_current).toMap().value("Id").toString());

        // После остановки продолжать нечего -- источник закрыт. Открываем
        // файл заново, песня начинается сначала.
        if (QFile::exists(path)) {
            playFile(path);
            emit says(stateText());
            emit changed();
            return;
        }
        // Файла нет -- значит его не докачали. Начинаем песню как обычно,
        // с загрузкой.
        startTrack(_current);
        return;
    } else {
        _paused = true;
        _expect = false;
        if (_watch)
            _watch->stop();   // на паузе сторожу делать нечего

        // СТОП, а не пауза.
        //
        // `pause()` вешает этот телефон наглухо -- проверено трижды,
        // в том числе на сборке без бегущей строки и с отключённым на
        // время вызова рисованием. Значит виноват сам вызов.
        // `stop()` же делается на каждой смене песни и ни разу не
        // подвёл.
        emit says(QString::fromUtf8(S_MUZ_STAVLYU_PAUZU));
        {
            Zanyat zanyat;
            _media->stop();
        }
        emit says(QString::fromUtf8(S_MUZ_PAUZA_VSTALA));
        _pos = 0;
        _wasPlaying = false;
    }
    emit says(stateText());
    emit changed();
}

// Уступаем звук.
//
// У телефона одно звуковое устройство: системный плеер, запустившись, забирает
// его себе, а наш Phonon получает ошибку и замолкает. Владелец это и увидел:
// открыл поверх музыки серию, вернулся -- «could not play: Playback complete.
// Unknown error (-13)». Поэтому не ждём, пока нас выгонят, а уходим сами:
// тогда и ошибки нет, и место в песне известно, и продолжить можно оттуда же.
bool JfMusic::pauseForOther()
{
    if (!_media || _current < 0 || _paused)
        return false;
    _pos = 0;
    _paused = true;
    _expect = false;      // чтобы сторож не счёл это концом песни
    if (_watch)
        _watch->stop();
    emit says(QString::fromUtf8(S_MUZ_STAVLYU_PAUZU));
    {
        Zanyat zanyat;
        _media->stop();
    }
    emit says(QString::fromUtf8(S_MUZ_PAUZA_VSTALA));
    _wasPlaying = false;
    emit changed();
    return true;
}

void JfMusic::stop()
{
    _expect = false;
    _paused = false;
    _wasPlaying = false;
    if (_watch)
        _watch->stop();
    if (_media) {
        Zanyat zanyat;
        _media->stop();
    }
    abortFetch();
    _current = -1;
    _queue.clear();
    _order.clear();
    _cover.clear();
    emit changed();
}

void JfMusic::setShuffle(bool on)
{
    if (_shuffle == on)
        return;
    _shuffle = on;
    rebuildOrder(_current);
    emit changed();
}

void JfMusic::setRepeat(bool on)
{
    _repeat = on;
    emit changed();
}

// --- Phonon ----------------------------------------------------------------

// Создаём по первому нажатию на песню, а не в конструкторе. Если с Phonon на
// этом телефоне что-то окажется не так, приложение всё равно запустится и
// покажет библиотеку, а беда будет видна в журнале -- вместо молчаливой
// смерти на старте, которая уже однажды стоила трёх установок.
void JfMusic::ensurePhonon()
{
    if (_media)
        return;
    _media = new Phonon::MediaObject(this);
    _out = new Phonon::AudioOutput(Phonon::MusicCategory, this);
    Phonon::createPath(_media, _out);
    connect(_media, SIGNAL(finished()), this, SLOT(onPhononFinished()));
    connect(_media, SIGNAL(stateChanged(Phonon::State, Phonon::State)),
            this, SLOT(onPhononState()));
    // Раз в секунду -- для полоски времени на экране плеера.
    _media->setTickInterval(1000);
    connect(_media, SIGNAL(tick(qint64)), this, SLOT(onTick()));
    connect(_media, SIGNAL(totalTimeChanged(qint64)),
            this, SLOT(onTotalTime(qint64)));
    applyVolume();
    emit says(QString::fromUtf8(S_MUZ_PLEER_GOTOV));
}

// Громкость. В 0.3.0 её не задавали вовсе -- и напрасно: Phonon по умолчанию
// ставит единицу, то есть громкость на максимум, и владелец сказал про 0.3.0
// ровно это -- «невероятно громко, и я никак не могу это изменить».
//
// Регулятор обязан быть свой, и это не удобство, а необходимость: боковых
// клавиш громкости у E90 нет вовсе, а на других аппаратах этого возраста
// они могут не работать или не доходить до приложения. Значит единственный
// надёжный способ -- менять громкость программно, своими строками и своими
// клавишами.
//
// Это Phonon::AudioOutput, а не системная медиа-часть: тот запрет из ЗАМЕТОК
// (10.10, от SetVolumeL приложение не запускалось) относится к
// CVideoPlayerUtility -- другой библиотеке и другому вызову.
void JfMusic::applyVolume()
{
    if (!_out)
        return;
    if (_vol < 0)
        _vol = 0;
    if (_vol > 10)
        _vol = 10;
    _out->setVolume(GROMKOST[_vol]);
}

void JfMusic::setVolumeStep(int step)
{
    _vol = step < 0 ? 0 : (step > 10 ? 10 : step);
    applyVolume();
}

void JfMusic::louder()
{
    if (_vol >= 10)
        return;
    ++_vol;
    applyVolume();
    emit changed();
}

void JfMusic::quieter()
{
    if (_vol <= 0)
        return;
    --_vol;
    applyVolume();
    emit changed();
}

qint64 JfMusic::position() const
{
    return _media ? _media->currentTime() : 0;
}

qint64 JfMusic::duration() const
{
    return _total;
}

void JfMusic::onTotalTime(qint64 ms)
{
    _total = ms;
    emit progress();
}

void JfMusic::onTick()
{
    emit progress();
}

void JfMusic::playLater()
{
    if (_otlozhen.isEmpty())
        return;
    // Всё ещё заняты -- подождём ещё немного, не выбрасывая песню.
    if (busy()) {
        QTimer::singleShot(150, this, SLOT(playLater()));
        return;
    }
    const QString path = _otlozhen;
    _otlozhen.clear();
    // Пока ждали, музыку могли остановить или переключить.
    if (_paused || _current < 0)
        return;
    playFile(path);
}

// --- обложка ----------------------------------------------------------------

QString JfMusic::coverFileFor(const QString &key) const
{
    return _dir + QLatin1String("/") + key + QLatin1String(".jpg");
}

// Обложку просим ОДНУ НА АЛЬБОМ, а не на песню: у десяти дорожек одного
// альбома она одна и та же, и качать её десять раз -- это десять запросов и
// десять файлов там, где хватает одного.
void JfMusic::wantCover(const QVariantMap &track)
{
    QString key = track.value("AlbumId").toString();
    if (key.isEmpty())
        key = track.value("Id").toString();
    if (key.isEmpty())
        return;

    const QString path = coverFileFor(key);
    if (QFile::exists(path)) {
        _cover = path;
        emit changed();
        return;
    }

    // Обложки прошлой песни на экране быть не должно: лучше пусто, чем чужая.
    _cover.clear();
    emit changed();

    if (_coverReply) {
        // Не успели -- значит песни меняют быстрее, чем приходят картинки.
        _coverReply->disconnect(this);
        _coverReply->abort();
        _coverReply->deleteLater();
        _coverReply = 0;
    }

    _coverKey = key;
    _coverReply = _net->get(QNetworkRequest(QUrl(_api->imageUrl(key, 96))));
    connect(_coverReply, SIGNAL(finished()), this, SLOT(onCoverDone()));
}

void JfMusic::onCoverDone()
{
    if (!_coverReply)
        return;

    const bool ok = (_coverReply->error() == QNetworkReply::NoError);
    const QByteArray data = _coverReply->readAll();
    const QString key = _coverKey;
    _coverReply->deleteLater();
    _coverReply = 0;
    _coverKey.clear();

    // Пустой ответ бывает законно: у альбома может не быть картинки вовсе.
    if (!ok || data.isEmpty())
        return;

    QFile file(coverFileFor(key));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(data);
    file.close();

    // Пока качалась, песню могли сменить -- тогда это уже не её обложка.
    if (_current >= 0 && _current < _queue.size()) {
        const QVariantMap one = _queue.at(_current).toMap();
        QString now = one.value("AlbumId").toString();
        if (now.isEmpty())
            now = one.value("Id").toString();
        if (now != key)
            return;
    }

    _cover = coverFileFor(key);
    emit changed();
}

void JfMusic::playFile(const QString &path)
{
    // НИКОГДА не трогаем плеер, пока мы внутри другого его вызова.
    //
    // Так бывает не по нашей воле: внутри вызовов Phonon крутится
    // обычный цикл событий, и туда прилетает «песня докачалась».
    // Раньше мы прямо оттуда меняли источник и запускали звук, то есть
    // входили в медиа-часть из неё же самой -- на этом аппарате это
    // зависание. Теперь просто откладываем на полтораста миллисекунд.
    if (busy()) {
        _otlozhen = path;
        QTimer::singleShot(150, this, SLOT(playLater()));
        return;
    }
    ensurePhonon();
    {
        Zanyat zanyat;
        _media->stop();
    }
    _total = 0;
    emit says(QString::fromUtf8(S_MUZ_OTKRYVAYU));
    {
        Zanyat zanyat;
        _media->setCurrentSource(Phonon::MediaSource(path));
        _media->play();
    }
    emit says(QString::fromUtf8(S_MUZ_OTKRYL));
    // Громкость задаём на каждую песню: на этом телефоне она живёт у плеера,
    // а плеер под каждый файл создаётся заново.
    applyVolume();
    _expect = true;
    _paused = false;
    _watch->start();
    emit says(QString::fromUtf8(S_MUZ_IGRAET) + currentName());
    emit changed();
    prefetchNext();
}

void JfMusic::onPhononState()
{
    if (!_media)
        return;
    const Phonon::State state = _media->state();
    if (state == Phonon::PlayingState)
        _wasPlaying = true;
    if (state == Phonon::ErrorState) {
        _expect = false;
        emit says(QString::fromUtf8(S_MUZ_OSHIBKA) + _media->errorString());

        // Две совершенно разные беды, и лечатся они по-разному.
        //
        // Если песня ТАК И НЕ ЗАИГРАЛА -- файл испорчен или не того вида, и
        // одна такая не должна останавливать всё: идём дальше.
        //
        // Если же она играла и вдруг умерла -- звук у нас забрали (системный
        // плеер, звонок, будильник). Перескакивать на следующую тут нельзя:
        // она умрёт точно так же, и так можно пройти всю очередь за минуту,
        // скачивая каждую песню. Поэтому встаём на паузу и ждём человека.
        if (_wasPlaying) {
            _paused = true;
            emit says(QString::fromUtf8(S_MUZ_ZVUK_ZABRALI));
            emit changed();
            return;
        }
        QTimer::singleShot(500, this, SLOT(onPhononFinished()));
    }
}

void JfMusic::onPhononFinished()
{
    if (!_expect)
        return;
    _expect = false;
    QTimer::singleShot(200, this, SLOT(advance()));
}

void JfMusic::advance()
{
    next();
}

void JfMusic::watchdog()
{
    if (!_media || !_expect || _paused)
        return;
    // Остановились после того, как действительно играли, -- значит трек
    // кончился, а сигнала не было.
    if (_wasPlaying && _media->state() == Phonon::StoppedState)
        onPhononFinished();
}

// --- загрузка --------------------------------------------------------------

void JfMusic::fetch(const QString &id, bool needNow)
{
    if (_fetching == id) {
        // Уже качаем именно это -- просто поднимаем срочность.
        if (needNow)
            _fetchNow = true;
        return;
    }
    abortFetch();
    // Место под новую песню освобождаем ДО того, как начали писать: иначе
    // закачка упадёт на полпути с «некуда писать», а огрызок останется.
    prune();

    // Качаем в файл с другим именем, а по готовности переименовываем.
    // Недокачанный файл не должен выглядеть готовым: плеер его не сыграет,
    // а в следующий раз мы решим, что он в кэше, и будет тишина.
    _file = new QFile(pathFor(id) + QLatin1String(".chast"));
    if (!_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        // Карта могла пропасть на ходу -- тогда уходим в запасную папку, в
        // память телефона, и пробуем ещё раз. Молча перестать качать музыку
        // было бы похоже на сломанное приложение.
        delete _file;
        _file = 0;

        if (!_fallback.isEmpty() && _fallback != _dir) {
            _dir = _fallback;
            QDir().mkpath(_dir);
            emit says(QString::fromUtf8(S_MUZ_PAPKA_UPALA));
            _file = new QFile(pathFor(id) + QLatin1String(".chast"));
            if (!_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                delete _file;
                _file = 0;
            }
        }

        if (!_file) {
            emit says(QString::fromUtf8(S_MUZ_NET_MESTA));
            return;
        }
    }

    _fetching = id;
    _fetchNow = needNow;
    _got = 0;
    QNetworkRequest request(QUrl(urlFor(id)));
    _reply = _net->get(request);
    connect(_reply, SIGNAL(readyRead()), this, SLOT(onData()));
    connect(_reply, SIGNAL(finished()), this, SLOT(onDownloadDone()));
}

void JfMusic::abortFetch()
{
    if (_reply) {
        _reply->disconnect(this);
        _reply->abort();
        _reply->deleteLater();
        _reply = 0;
    }
    if (_file) {
        const QString name = _file->fileName();
        _file->close();
        delete _file;
        _file = 0;
        QFile::remove(name);
    }
    _fetching.clear();
    _fetchNow = false;
    _got = 0;
}

// Пишем кусками по мере поступления, а не целиком в конце: 3 МБ в памяти
// телефона с 32 МБ -- это не мелочь, и Депеша на этом уже обжигалась.
void JfMusic::onData()
{
    if (!_reply || !_file)
        return;
    const QByteArray part = _reply->readAll();
    _file->write(part);
    _got += part.size();
}

void JfMusic::onDownloadDone()
{
    if (!_reply)
        return;
    const QString id = _fetching;
    const bool needed = _fetchNow;
    const bool ok = (_reply->error() == QNetworkReply::NoError);
    const QString why = _reply->errorString();

    if (_file) {
        _file->write(_reply->readAll());
        _file->close();
    }
    const QString part = _file ? _file->fileName() : QString();
    delete _file;
    _file = 0;
    _reply->deleteLater();
    _reply = 0;
    _fetching.clear();
    _fetchNow = false;

    if (!ok) {
        QFile::remove(part);
        emit says(QString::fromUtf8(S_MUZ_NE_DOEHALO) + why);
        if (needed) {
            // Следующая песня может быть целой -- не стоять же на месте.
            QTimer::singleShot(1500, this, SLOT(onPhononFinished()));
            _expect = true;
        }
        return;
    }

    const QString ready = pathFor(id);
    QFile::remove(ready);
    QFile::rename(part, ready);
    prune();

    if (needed && _current >= 0
        && _queue.at(_current).toMap().value("Id").toString() == id) {
        // На паузе -- НЕ ЗАПУСКАЕМ. Пауза бывает поставлена потому, что
        // звук ушёл видео, а поверх приложения в это время стоит модальное
        // окно («откуда смотреть») со своим циклом событий. Трогать Phonon
        // изнутри чужого цикла -- верный способ получить зависание, а
        // владелец его уже видел. Файл просто лежит готовый: продолжат --
        // заиграет.
        if (_paused) {
            emit says(QString::fromUtf8(S_MUZ_PODGOTOVLEN));
            emit changed();
            return;
        }
        playFile(ready);
        return;
    }
    // Это была упреждающая подкачка -- просто тихо лежит в кэше.
    emit says(QString::fromUtf8(S_MUZ_PODGOTOVLEN));
}

QString JfMusic::idAt(int place) const
{
    if (place < 0 || place >= _order.size())
        return QString();
    const int index = _order.at(place);
    if (index < 0 || index >= _queue.size())
        return QString();
    return _queue.at(index).toMap().value("Id").toString();
}

void JfMusic::prefetchNext()
{
    if (!_fetching.isEmpty())
        return;
    const int place = nextIndex(true);
    if (place < 0)
        return;
    const QString id = idAt(place);
    if (id.isEmpty())
        return;
    // Следующую берегём от подрезки: иначе она же её и удалит, освобождая
    // место под себя.
    _keepId = id;
    if (QFile::exists(pathFor(id)))
        return;
    fetch(id, false);
}

// Недокачанные огрызки. Каждый -- это мегабайты, которые уже никогда не
// сыграют: файл с другим расширением, и опознать его по имени песни нельзя.
// Чистим при запуске и при каждой подрезке.
void JfMusic::sweepPart()
{
    QDir dir(_dir);
    const QFileInfoList huzhe = dir.entryInfoList(
        QStringList() << QLatin1String("*.chast"), QDir::Files);
    for (int i = 0; i < huzhe.size(); ++i) {
        // Тот, что качается прямо сейчас, не трогаем.
        if (_file && huzhe.at(i).absoluteFilePath() == _file->fileName())
            continue;
        QFile::remove(huzhe.at(i).absoluteFilePath());
    }
}

// Подрезка кэша: самые старые уходят первыми, пока не уложимся в предел.
// Текущую песню и следующую не трогаем -- иначе плеер остался бы без файла
// прямо на ходу.
//
// Предел задаёт окно, и он мал нарочно: во внутренней памяти E90 свободно
// десятки мегабайт на всё -- и на сообщения, и на снимки. 0.3.0 с пределом
// 40 МБ забила её целиком, и телефон начал жаловаться на нехватку памяти.
void JfMusic::prune()
{
    sweepPart();

    QDir dir(_dir);
    const QFileInfoList files = dir.entryInfoList(
        QStringList() << QLatin1String("*.mp3"), QDir::Files, QDir::Time);
    qint64 vsego = 0;
    for (int i = 0; i < files.size(); ++i)
        vsego += files.at(i).size();
    if (vsego <= _predel)
        return;

    const QString teper = _current >= 0 && _current < _queue.size()
        ? pathFor(_queue.at(_current).toMap().value("Id").toString())
        : QString();
    const QString potom = _keepId.isEmpty() ? QString() : pathFor(_keepId);

    // entryInfoList с QDir::Time отдаёт новые первыми -- идём с конца.
    for (int i = files.size() - 1; i >= 0 && vsego > _predel; --i) {
        const QString path = files.at(i).absoluteFilePath();
        if (path == teper || path == potom)
            continue;
        const qint64 size = files.at(i).size();
        if (QFile::remove(path))
            vsego -= size;
    }
}

qint64 JfMusic::cacheSize() const
{
    QDir dir(_dir);
    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    qint64 vsego = 0;
    for (int i = 0; i < files.size(); ++i)
        vsego += files.at(i).size();
    return vsego;
}

// Очистка по требованию.
//
// Музыку при этом НЕ ОСТАНАВЛИВАЕМ и файл играющей песни не трогаем. 0.3.5
// делала наоборот -- звала stop() и сносила папку целиком, -- и владелец
// сказал прямо: «если во время прослушивания очистить кэш музыки, то убивает
// и играющую песню». Человек просил освободить место, а не выключить музыку.
//
// Настройки и ключ входа лежат в другой папке, их это не касается.
void JfMusic::clearCache()
{
    const QString teper = (_current >= 0 && _current < _queue.size())
        ? pathFor(_queue.at(_current).toMap().value("Id").toString())
        : QString();
    // Следующую тоже щадим: её уже качают или скачали, и удалить её -- значит
    // устроить тишину между песнями на ровном месте.
    const QString potom = _keepId.isEmpty() ? QString() : pathFor(_keepId);

    QDir dir(_dir);
    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    for (int i = 0; i < files.size(); ++i) {
        const QString path = files.at(i).absoluteFilePath();
        if (path == teper || path == potom || path == _cover)
            continue;
        QFile::remove(path);
    }
}
