@echo off
cd /d %~dp0
set headerfile=VersionHash.h
for /f "usebackq tokens=*" %%i in (`git rev-parse --short HEAD`) do set hash=%%i
if "%hash%"=="" (
	if exist %headerfile% del %headerfile%
	exit /b 0
)
find "%hash%" %headerfile% >nul
if %errorlevel% neq 0 (
	echo #define TVTVIDEODEC_VERSION_HASH "%hash%">%headerfile%
)
exit /b 0
