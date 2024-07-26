@echo off

:: Répertoires source et destination
set SRC=proxmark3\client\*
set DEST=..\..\proxspace\pm3\proxmark3\client

:: Copier uniquement les fichiers modifiés
cp -R "%SRC%" "%DEST%"
:: Fin
