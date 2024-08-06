@echo off

:: Répertoires source et destination
set SRC=proxmark3\*
set DEST=proxspace\pm3\proxmark3

:: Copier uniquement les fichiers modifiés
cp -R "%SRC%" "%DEST%"
:: Fin
