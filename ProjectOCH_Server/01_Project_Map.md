# Project Map

## 루트

```text
C:\ProjectOCH\Server
├─ Server.sln
├─ ServerCore
├─ GameServer
├─ DummyClient
├─ Common
├─ Tools
├─ Libraries
├─ Binaries
├─ Data
└─ ProjectOCH_Server
```

## Server.sln

Visual Studio 2022 계열 솔루션이다.

- `GameServer\GameServer.vcxproj`
- `DummyClient\DummyClient.vcxproj`
- `ServerCore\ServerCore.vcxproj`
- `Tools\PacketGenerator\PacketGenerator.pyproj`

## ServerCore

정적 라이브러리 프로젝트다.

중요 파일:

- `IocpCore.h/.cpp`: IOCP handle 생성, socket handle 등록, completion dispatch.
- `IocpEvent.h/.cpp`: Accept, Connect, Disconnect, Recv, Send event 타입.
- `Listener.h/.cpp`: 서버 listen socket, `AcceptEx`, accept 완료 처리.
- `Session.h/.cpp`: socket lifecycle, overlapped recv/send, disconnect 처리.
- `RecvBuffer.h/.cpp`: 수신 버퍼 read/write cursor 관리.
- `SendBuffer.h/.cpp`: 송신 버퍼와 write size 관리.
- `Service.h/.cpp`: ClientService/ServerService, 세션 생성/등록/해제/브로드캐스트.
- `ThreadManager.h/.cpp`: worker thread launch/join, TLS 초기화, global queue/timer 분배.
- `Job.h`, `JobQueue.h/.cpp`, `GlobalQueue.h/.cpp`, `JobTimer.h/.cpp`: 룸 단위 직렬 실행을 위한 job 시스템.
- `SocketUtils.h/.cpp`: Winsock 확장 함수와 socket option helper.
- `CoreGlobal.h/.cpp`, `CoreTLS.h/.cpp`: 전역 singleton/TLS 상태.

## GameServer

실제 서버 실행 프로젝트다.

중요 파일:

- `GameServer.cpp`: main entry. walkmap 로드, server service 시작, worker thread 실행.
- `GameSession.h/.cpp`: `PacketSession` 상속, 접속/해제/패킷 수신 hook.
- `GameSessionManager.h/.cpp`: 접속 중인 `GameSession` 관리와 broadcast.
- `ServerPacketHandler.h/.cpp`: 패킷 ID routing, protobuf parse, C_ 패킷 처리.
- `Room.h/.cpp`: 룸 상태, 플레이어 입장/퇴장/이동 검증, `S_ENTER_GAME`, `S_SPAWN`, `S_DESPAWN`, `S_MOVE` 송신.
- `FieldWalkMapData.h/.cpp`: `Field_001.walkmap.json` 로드, Hexagon Grid 좌표 변환, walkable 검사.
- `Object.h/.cpp`, `Creature.h/.cpp`, `Player.h/.cpp`, `Monster.h/.cpp`: 게임 오브젝트 계층.
- `ObjectUtils.h/.cpp`: 플레이어 생성과 object id 부여.
- `Utils.h/.cpp`: 랜덤 유틸.
- `*.pb.h/.cc`: protobuf 생성물.
- `Enum.proto`, `Struct.proto`, `Protocol.proto`: 빌드 시 `Common` 원본에서 복사되는 proto 파일.

## DummyClient

C++ 테스트 클라이언트 프로젝트다.

중요 파일:

- `DummyClient.cpp`: 클라이언트 실행 진입점.
- `ClientPacketHandler.h/.cpp`: 서버에서 오는 S_ 패킷 처리.
- `*.pb.h/.cc`: protobuf 생성물.

## Common

`Common\protoc-21.12-win64\bin` 안에 proto 원본과 생성 스크립트가 있다.

- `Enum.proto`
- `Struct.proto`
- `Protocol.proto`
- `GenPackets.bat`
- `GenPackets.exe`
- `protoc.exe`

`GenPackets.bat`는 C++/C# protobuf, C++ handler, Unity `PacketManager.cs`를 생성한 뒤 각 프로젝트로 복사한다.

## Tools

`Tools\PacketGenerator`는 packet handler 생성기다.

- `PacketGenerator.py`: cli entry.
- `ProtoParser.py`: `message` 선언을 읽고 `C_`, `S_` prefix로 recv/send 패킷을 나눈다.
- `Templates\PacketHandler.h`: C++ handler header template.
- `Templates\PacketManager.cs`: C# client packet manager template.
- `MakeExe.bat`: `py -3 -m PyInstaller` 또는 `python -m PyInstaller`로 `GenPackets.exe`를 만들고 `Common` 쪽으로 복사한다.

## Data

```text
Data\Maps\Field_001.walkmap.json
```

Unity Editor Tool이 export한 field walk map이다.

- `fixed_point_scale`: 현재 100.
- `cell_size`: Unity Grid cell size.
- `origin_world`: tilemap origin.
- `walkable_ranges`: row별 inclusive `x_min..x_max` range.

서버는 이 파일을 시작 시 읽고 `C_MOVE.target` 검증에 사용한다.

## Libraries

- `Libraries\Include`: protobuf header.
- `Libraries\Libs\Protobuf`: protobuf lib.
- `Libraries\Libs\ServerCore`: `ServerCore` 빌드 산출물.

## 출력물

- `Binaries\Debug`, `Binaries\Release`: `GameServer`, `DummyClient` 실행 파일 출력 위치.
- 각 프로젝트의 `x64\Debug` 폴더는 중간 빌드 산출물이다.
