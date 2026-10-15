@echo off
REM ===============================================================
REM Jellyfin для кнопочных Symbian: сборка .sis (GCCE, abld, Qt 4.7.3).
REM
REM Инструментарий 2011 года не переваривает кириллицу в путях, поэтому
REM исходники зеркалятся в %DP_WORK% (по умолчанию C:\jvbuild).
REM
REM Использование:
REM   build-symbian.bat           русская сборка    -> jellyfin-RU.sis
REM   build-symbian.bat en        английская        -> jellyfin-EN.sis
REM   build-symbian.bat clean     стереть рабочее дерево
REM
REM Внутри пакета языки не отличаются ничем, кроме текстов: тот же UID3,
REM та же версия, то же имя. Значит одна сборка ставится поверх другой
REM как обновление, а рядом друг с другом они не встанут.
REM ===============================================================
setlocal

set SDKPREFIX=C:\QtSDK\Symbian\SDKs\Symbian1Qt473
set SYMBIANDIR=C:\QtSDK\Symbian
set EPOCROOT=\QtSDK\Symbian\SDKs\Symbian1Qt473\
set QTDIR=%SDKPREFIX%
set GCCPATH=%SYMBIANDIR%\tools\gcce4
set NoDefaultCurrentDirectoryInExePath=
set PATH=%SDKPREFIX%\epoc32\gcc\bin;%SDKPREFIX%\epoc32\tools;%GCCPATH%\bin;%SYMBIANDIR%\tools\perl\bin;%SYMBIANDIR%\tools\sbs\bin;%SDKPREFIX%\bin;%GCCPATH%\arm-none-symbianelf\bin;%SDKPREFIX%\mkspecs\default;%QTDIR%;C:\Windows\System32;%PATH%;.

if "%DP_ROOT%"==""  set DP_ROOT=%~dp0..\..
if "%DP_WORK%"==""  set DP_WORK=C:\jvbuild

if "%1"=="clean" (
    md "%TEMP%\dpempty" 2>nul
    if exist "%DP_WORK%" robocopy "%TEMP%\dpempty" "%DP_WORK%" /MIR /NJH /NJS >nul
    if exist "%SDKPREFIX%\EPOC32\BUILD\jvbuild" robocopy "%TEMP%\dpempty" "%SDKPREFIX%\EPOC32\BUILD\jvbuild" /MIR /NJH /NJS >nul
    echo Рабочее дерево стёрто: %DP_WORK% ^(и EPOC32 BUILD^)
    goto :end
)

REM Язык выбирается здесь и попадает в сборку одним определением.
set JV_LANG=RU
set JV_EXTRA=
if /i "%1"=="en" (
    set JV_LANG=EN
    set JV_EXTRA=DEFINES+=JF_EN
)

REM Сборка-щуп: каждый шаг запуска оставляет пустой файл в C:\Data.
REM Нужна, когда приложение не запускается и журнала поэтому нет вовсе:
REM по последнему появившемуся файлу видно, на какой строке всё кончилось.
REM Список отметок -- в ЗАМЕТКАХ, раздел 0.2.2. В обычную сборку они не
REM попадают: отметки сорили бы файлами у всех.
if /i "%2"=="metki" set JV_EXTRA=%JV_EXTRA% DEFINES+=JF_METKI

REM При смене настроек объектные файлы от прошлой сборки негодны, а
REM собирается смесь: код с прежними текстами уже скомпилирован, а
REM библиотеки уже не подключены -- получаются ошибки связывания. Поэтому
REM при других настройках, чем в прошлый раз, рабочее дерево стирается.
set JV_MARK=%DP_WORK%\last-extra.txt
set JV_WAS=
if exist "%JV_MARK%" set /p JV_WAS=<"%JV_MARK%"
if not "%JV_WAS%"=="%JV_EXTRA%" (
    echo === настройки сменились, стираю рабочее дерево ===
    md "%TEMP%\dpempty" 2>nul
    if exist "%DP_WORK%" robocopy "%TEMP%\dpempty" "%DP_WORK%" /MIR /NJH /NJS >nul
    if exist "%SDKPREFIX%\EPOC32\BUILD\jvbuild" robocopy "%TEMP%\dpempty" "%SDKPREFIX%\EPOC32\BUILD\jvbuild" /MIR /NJH /NJS >nul
)
md "%DP_WORK%" 2>nul
echo %JV_EXTRA%>"%JV_MARK%"

echo === зеркалю исходники ===
robocopy "%DP_ROOT%\jellyfin\klient" "%DP_WORK%\klient" /MIR /NFL /NDL /NJH /NJS /NP /XD .git /XF *.sis *.bak-* >nul
if errorlevel 8 exit /b 1

REM Ниже НЕЛЬЗЯ проверять errorlevel после cd: robocopy возвращает 1,
REM когда что-то скопировал (это успех), а cd код возврата не
REM сбрасывает. Из-за этого сборка молча обрывалась после зеркаления
REM ровно тогда, когда исходники изменились -- то есть всегда, когда
REM она и была нужна. Попадание в дерево проверяем по файлу.
cd /d "%DP_WORK%\klient"
if not exist klient.pro (
    echo НЕ ПОПАЛ в рабочее дерево: %DP_WORK%\klient
    exit /b 1
)

echo === qmake (язык %JV_LANG%, %JV_EXTRA%) ===
call qmake -spec symbian-abld CONFIG+=release %JV_EXTRA%
if errorlevel 1 exit /b 1

echo === make release-gcce ===
REM abld -- perl-скрипт, он возвращает 0 даже когда сборка упала, поэтому
REM успех проверяется по журналу, а не по коду возврата.
call make release-gcce > "%DP_WORK%\build.log" 2>&1
type "%DP_WORK%\build.log"
if errorlevel 1 exit /b 1
findstr /i /c:"error:" /c:"Error 1" /c:"Error 2" "%DP_WORK%\build.log" >nul && (
    echo.
    echo СБОРКА НЕ УДАЛАСЬ -- смотри %DP_WORK%\build.log
    exit /b 1
)

REM Имя .sis задаётся TARGET в klient.pro. Читаем, а не угадываем:
REM в 0.2.0 имя было переименовано и обратно, и жёстко прописанное
REM имя тут же разошлось со сборкой.
set DP_TARGET=
for /f "tokens=3" %%t in ('findstr /b /c:"TARGET = " "%DP_ROOT%\jellyfin\klient\klient.pro"') do set DP_TARGET=%%t
if "%DP_TARGET%"=="" (
    echo НЕ НАШЁЛ TARGET в klient.pro
    exit /b 1
)

echo === make sis ^(%DP_TARGET%.sis^) ===
del /q "%DP_WORK%\klient\%DP_TARGET%.sis" 2>nul
call make sis QT_SIS_OPTIONS=-d

REM Значок. mifconv возвращает НОЛЬ, даже когда не разобрал SVG, и тихо
REM пишет пустой .mif: один двойной дефис в XML-комментарии (а в этом
REM проекте он служит тире) обрывает разбор, и вместо 411 байт выходит 69.
REM Увидеть это можно только на телефоне, поэтому проверяем размером.
set DP_MIF=%SDKPREFIX%\epoc32\data\z\resource\apps\%DP_TARGET%.mif
if not exist "%DP_MIF%" ( echo ЗНАЧОК НЕ СОБРАН: %DP_MIF% & exit /b 1 )
for %%F in ("%DP_MIF%") do if %%~zF LSS 200 (
    echo ЗНАЧОК ПУСТОЙ ^(%%~zF байт^): mifconv не разобрал src\icon.svg.
    echo Ищи двойной дефис в комментарии или иное нарушение XML.
    exit /b 1
)

echo === собираю готовое ===
set DP_VER=
for /f "tokens=3" %%v in ('findstr /b /c:"VERSION = " "%DP_ROOT%\jellyfin\klient\klient.pro"') do set DP_VER=%%v
if "%DP_VER%"=="" set DP_VER=unknown
if not exist "%DP_WORK%\klient\%DP_TARGET%.sis" (
    echo УПАКОВКА НЕ УДАЛАСЬ -- %DP_TARGET%.sis не появился
    exit /b 1
)
REM Два имени у одного файла: короткое идёт в Releases на GitHub, с
REM версией -- остаётся здесь в архиве. Пакеты ставятся вручную и копятся,
REM и без версии в имени потом не понять, что именно стоит на телефоне.
copy /y "%DP_WORK%\klient\%DP_TARGET%.sis" "%~dp0jellyfin-%JV_LANG%.sis" >nul
copy /y "%DP_WORK%\klient\%DP_TARGET%.sis" "%~dp0jellyfin-%DP_VER%-%JV_LANG%.sis" >nul
echo Готово: jellyfin-%JV_LANG%.sis (версия %DP_VER%)
:end
endlocal
