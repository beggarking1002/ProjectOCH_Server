pushd %~dp0

set PYTHON_CMD=

where py >nul 2>nul
IF NOT ERRORLEVEL 1 set PYTHON_CMD=py -3

IF "%PYTHON_CMD%"=="" (
	where python >nul 2>nul
	IF NOT ERRORLEVEL 1 set PYTHON_CMD=python
)

IF "%PYTHON_CMD%"=="" GOTO MISSING_PYTHON

%PYTHON_CMD% -m PyInstaller --onefile PacketGenerator.py
IF ERRORLEVEL 1 GOTO FAIL
MOVE /Y .\dist\PacketGenerator.exe .\GenPackets.exe
IF ERRORLEVEL 1 GOTO FAIL
COPY /Y .\GenPackets.exe ..\..\Common\protoc-21.12-win64\bin\GenPackets.exe
IF ERRORLEVEL 1 GOTO FAIL
@RD /S /Q .\build
@RD /S /Q .\dist
DEL /S /F /Q .\PacketGenerator.spec
popd
PAUSE
EXIT /B 0

:FAIL
ECHO [ERROR] MakeExe.bat failed.
ECHO [HINT] If PyInstaller is missing, run: %PYTHON_CMD% -m pip install pyinstaller
popd
PAUSE
EXIT /B 1

:MISSING_PYTHON
ECHO [ERROR] Python was not found.
ECHO [HINT] Install Python or add it to PATH, then run MakeExe.bat again.
popd
PAUSE
EXIT /B 1
