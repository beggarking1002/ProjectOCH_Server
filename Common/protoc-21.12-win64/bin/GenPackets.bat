pushd %~dp0

protoc.exe -I=./ --cpp_out=./ ./Enum.proto
IF ERRORLEVEL 1 GOTO FAIL
protoc.exe -I=./ --cpp_out=./ ./Struct.proto
IF ERRORLEVEL 1 GOTO FAIL
protoc.exe -I=./ --cpp_out=./ ./Protocol.proto
IF ERRORLEVEL 1 GOTO FAIL

protoc.exe -I=./ --csharp_out=. ./Enum.proto
IF ERRORLEVEL 1 GOTO FAIL
protoc.exe -I=./ --csharp_out=. ./Struct.proto
IF ERRORLEVEL 1 GOTO FAIL
protoc.exe -I=./ --csharp_out=. ./Protocol.proto
IF ERRORLEVEL 1 GOTO FAIL

GenPackets.exe --path=./Protocol.proto --output=ClientPacketHandler --recv=S_ --send=C_
IF ERRORLEVEL 1 GOTO FAIL
GenPackets.exe --path=./Protocol.proto --output=ServerPacketHandler --recv=C_ --send=S_
IF ERRORLEVEL 1 GOTO FAIL
GenPackets.exe --path=./Protocol.proto --output=PacketManager --recv=S_ --send=C_
IF ERRORLEVEL 1 GOTO FAIL

IF NOT EXIST ClientPacketHandler.h GOTO MISSING_CLIENT_HANDLER
IF NOT EXIST ServerPacketHandler.h GOTO MISSING_SERVER_HANDLER
IF NOT EXIST PacketManager.cs GOTO MISSING_PACKET_MANAGER

XCOPY /Y Enum.pb.h "../../../GameServer"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Enum.pb.cc "../../../GameServer"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Struct.pb.h "../../../GameServer"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Struct.pb.cc "../../../GameServer"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Protocol.pb.h "../../../GameServer"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Protocol.pb.cc "../../../GameServer"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y ServerPacketHandler.h "../../../GameServer"
IF ERRORLEVEL 1 GOTO FAIL

XCOPY /Y Enum.pb.h "../../../DummyClient"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Enum.pb.cc "../../../DummyClient"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Struct.pb.h "../../../DummyClient"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Struct.pb.cc "../../../DummyClient"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Protocol.pb.h "../../../DummyClient"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Protocol.pb.cc "../../../DummyClient"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y ClientPacketHandler.h "../../../DummyClient"
IF ERRORLEVEL 1 GOTO FAIL

XCOPY /Y Enum.cs "../../../../Client/Assets/Scripts/Packet/Generated"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Struct.cs "../../../../Client/Assets/Scripts/Packet/Generated"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y Protocol.cs "../../../../Client/Assets/Scripts/Packet/Generated"
IF ERRORLEVEL 1 GOTO FAIL
XCOPY /Y PacketManager.cs "../../../../Client/Assets/Scripts/Packet/Generated"
IF ERRORLEVEL 1 GOTO FAIL

GOTO CLEANUP

:MISSING_CLIENT_HANDLER
ECHO [ERROR] ClientPacketHandler.h was not generated.
GOTO FAIL

:MISSING_SERVER_HANDLER
ECHO [ERROR] ServerPacketHandler.h was not generated.
GOTO FAIL

:MISSING_PACKET_MANAGER
ECHO [ERROR] PacketManager.cs was not generated.
ECHO [HINT] Rebuild GenPackets.exe from Tools\PacketGenerator\PacketGenerator.py and copy it here.
GOTO FAIL

:FAIL
ECHO [ERROR] GenPackets.bat failed.
GOTO CLEANUP_FAIL

:CLEANUP
DEL /Q /F *.pb.h
DEL /Q /F *.pb.cc
DEL /Q /F *.h
DEL /Q /F *.cs
popd
PAUSE
EXIT /B 0

:CLEANUP_FAIL
DEL /Q /F *.pb.h
DEL /Q /F *.pb.cc
DEL /Q /F *.h
DEL /Q /F *.cs
popd
PAUSE
EXIT /B 1
