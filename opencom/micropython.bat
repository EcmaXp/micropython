@echo off
pushd %~dp0
pushd %~dp0\..\windows
set MICROPYTHON_LIB=%CD%\micropython.dll
popd
set MICROPYTHON_OS_SEP=\
java -jar micropython.jar %~dp0\main.py %*
popd
