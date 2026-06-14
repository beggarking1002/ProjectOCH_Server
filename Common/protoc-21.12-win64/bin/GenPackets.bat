pushd %~dp0

protoc.exe -I=./ --cpp_out=./ ./Enum.proto
protoc.exe -I=./ --cpp_out=./ ./Struct.proto
protoc.exe -I=./ --cpp_out=./ ./Protocol.proto

protoc.exe -I=./ --csharp_out=. ./Enum.proto
protoc.exe -I=./ --csharp_out=. ./Struct.proto
protoc.exe -I=./ --csharp_out=. ./Protocol.proto


GenPackets.exe --path=./Protocol.proto --output=ClientPacketHandler --recv=S_ --send=C_
GenPackets.exe --path=./Protocol.proto --output=ServerPacketHandler --recv=C_ --send=S_
GenPackets.exe --path=./Protocol.proto --output=PacketManager --recv=S_ --send=C_

IF ERRORLEVEL 1 PAUSE

XCOPY /Y Enum.pb.h "../../../GameServer"
XCOPY /Y Enum.pb.cc "../../../GameServer"
XCOPY /Y Struct.pb.h "../../../GameServer"
XCOPY /Y Struct.pb.cc "../../../GameServer"
XCOPY /Y Protocol.pb.h "../../../GameServer"
XCOPY /Y Protocol.pb.cc "../../../GameServer"
XCOPY /Y ServerPacketHandler.h "../../../GameServer"

XCOPY /Y Enum.pb.h "../../../DummyClient"
XCOPY /Y Enum.pb.cc "../../../DummyClient"
XCOPY /Y Struct.pb.h "../../../DummyClient"
XCOPY /Y Struct.pb.cc "../../../DummyClient"
XCOPY /Y Protocol.pb.h "../../../DummyClient"
XCOPY /Y Protocol.pb.cc "../../../DummyClient"
XCOPY /Y ClientPacketHandler.h "../../../DummyClient"

XCOPY /Y Enum.cs "../../../../Client/Assets/Scripts/Packet/Generated"
XCOPY /Y Struct.cs "../../../../Client/Assets/Scripts/Packet/Generated"
XCOPY /Y Protocol.cs "../../../../Client/Assets/Scripts/Packet/Generated"
XCOPY /Y PacketManager.cs "../../../../Client/Assets/Scripts/Packet/Generated"

DEL /Q /F *.pb.h
DEL /Q /F *.pb.cc
DEL /Q /F *.h
DEL /Q /F *.cs

PAUSE