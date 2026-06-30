# ProjectOCH Server - Codex Start Here

> 새 Codex 채팅에서 이 vault를 참고할 때 가장 먼저 읽을 문서.

## 한 줄 요약

ProjectOCH 서버는 Windows C++ 기반의 Visual Studio 솔루션이다. `ServerCore`가 IOCP 네트워크, 세션, 스레드, 잡 큐를 제공하고, `GameServer`가 패킷 처리, 룸 상태, 플레이어 오브젝트, 필드 이동 검증을 담당한다. Unity 클라이언트와는 protobuf 패킷으로 통신한다.

## 실제 소스 위치

- Obsidian vault: `C:\ProjectOCH\Server\ProjectOCH_Server`
- 실제 솔루션 루트: `C:\ProjectOCH\Server`
- 솔루션 파일: `C:\ProjectOCH\Server\Server.sln`
- Unity 클라이언트: `C:\ProjectOCH\Client`

## 먼저 볼 파일

- `C:\ProjectOCH\Server\GameServer\GameServer.cpp`
  - 서버 시작점. `Field_001.walkmap.json`을 로드하고 `127.0.0.1:7777`에서 `ServerService`를 연다.
- `C:\ProjectOCH\Server\GameServer\ServerPacketHandler.cpp`
  - `C_LOGIN`, `C_ENTER_GAME`, `C_LEAVE_GAME`, `C_MOVE` 처리 진입점.
- `C:\ProjectOCH\Server\GameServer\Room.cpp`
  - 입장, 퇴장, 스폰/디스폰, 이동 검증, 브로드캐스트 로직.
- `C:\ProjectOCH\Server\GameServer\FieldWalkMapData.cpp`
  - Unity에서 export한 `Field_001.walkmap.json` 로드 및 Hexagon Grid 기반 walkable 검사.
- `C:\ProjectOCH\Server\Common\protoc-21.12-win64\bin\Protocol.proto`
  - 현재 프로토콜 원본.
- `C:\ProjectOCH\Server\Common\protoc-21.12-win64\bin\Struct.proto`
  - `Vec2Fixed`, `ObjectInfo` 정의.

## 프로젝트별 역할

- `ServerCore`
  - 정적 라이브러리.
  - IOCP, socket helper, session, listener, send/recv buffer, thread manager, job queue, timer queue 담당.
- `GameServer`
  - 실행 서버.
  - `GameSession`, `ServerPacketHandler`, `Room`, `Object/Player/Monster`, `FieldWalkMapData` 담당.
- `DummyClient`
  - C++ 테스트 클라이언트.
  - 서버와 같은 protobuf 생성물을 사용한다.
- `Common\protoc-21.12-win64\bin`
  - proto 원본, `protoc.exe`, `GenPackets.bat`, `GenPackets.exe` 위치.
- `Tools\PacketGenerator`
  - Python/Jinja2 기반 C++ handler 및 Unity C# `PacketManager.cs` 생성기.
- `Data\Maps`
  - 서버가 로드하는 필드 맵 데이터. 현재 `Field_001.walkmap.json` 사용.

## 현재 핵심 프로토콜

- 좌표계는 필드 기준 `Vec2Fixed`다.
  - `sint32 x`
  - `sint32 y`
  - fixed-point scale은 현재 `100`.
- `ObjectInfo`
  - `object_id`
  - `object_type`
  - `creature_type`
  - `position: Vec2Fixed`
- 이동
  - `C_MOVE.target`: 클라이언트가 원하는 fixed-point world 좌표.
  - `S_MOVE.object_id/start/target/duration_ms`: 서버가 확정한 이동 결과.

## 주요 실행 흐름

1. `GameServer.cpp`에서 `GFieldWalkMapData.LoadFromFile("C:\\ProjectOCH\\Server\\Data\\Maps\\Field_001.walkmap.json")`.
2. `ServerPacketHandler::Init()`으로 packet id routing 초기화.
3. `ServerService` 생성: 주소 `127.0.0.1:7777`, `GameSession` factory, max session `100`.
4. worker thread가 `IocpCore::Dispatch(10)`, 예약 job 분배, global queue 작업을 반복한다.
5. `PacketSession::OnRecv()`가 `[size:uint16][id:uint16][payload]` 단위로 패킷을 조립한다.
6. `GameSession::OnRecvPacket()`이 `ServerPacketHandler::HandlePacket()`으로 넘긴다.
7. `C_ENTER_GAME`, `C_LEAVE_GAME`, `C_MOVE`는 `Room` job queue에 `DoAsync(...)`로 밀어 넣는다.
8. `Room`은 플레이어 상태를 직렬로 갱신하고 필요한 `S_*` 패킷을 전송한다.

## 중요한 주의사항

- proto 원본은 `Common\protoc-21.12-win64\bin`을 기준으로 본다.
- `GenPackets.bat`가 생성물을 `GameServer`, `DummyClient`, Unity `Client\Assets\Scripts\Packet\Generated`로 복사한다.
- 패킷 ID는 `Protocol.proto`의 `message` 선언 순서대로 1000부터 부여된다. 메시지 순서를 바꾸면 ID가 바뀐다.
- `Field_001.walkmap.json`의 `walkable_ranges`가 비어 있으면 서버 시작 시 `Empty walkable_ranges` 오류가 난다. Unity exporter에서 `Ground_Tilemap`이 제대로 잡혔는지 확인해야 한다.
- 현재 `Field_001`은 Unity Hexagon Grid다. 서버도 rectangle 공식이 아니라 row stride `0.75`, odd row x offset을 반영한다.

## 이어서 읽을 문서

- [[01_Project_Map]]
- [[02_ServerCore_Architecture]]
- [[03_GameServer_Flow]]
- [[04_Packet_Protocol_Generation]]
- [[05_Working_Notes]]
