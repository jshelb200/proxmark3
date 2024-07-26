@echo off

:: Répertoires source et destination
set SRC=proxmark3\armsrc\*
set DEST=..\..\proxspace\pm3\proxmark3\armsrc

:: Copier uniquement les fichiers modifiés
cp -R "%SRC%" "%DEST%"
:: Fin

