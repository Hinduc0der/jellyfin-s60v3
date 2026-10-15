@echo off
REM Собрать обе сборки подряд: сначала русскую, потом английскую.
REM
REM Между ними рабочее дерево стирается само (см. build-symbian.bat):
REM тексты зашиты в код определением, и смешивать объектные файлы нельзя.
setlocal
call "%~dp0build-symbian.bat"
if errorlevel 1 (
    echo РУССКАЯ СБОРКА НЕ УДАЛАСЬ
    exit /b 1
)
call "%~dp0build-symbian.bat" en
if errorlevel 1 (
    echo АНГЛИЙСКАЯ СБОРКА НЕ УДАЛАСЬ
    exit /b 1
)
echo.
echo Обе сборки готовы:
dir /b "%~dp0jellyfin-*.sis"
endlocal
