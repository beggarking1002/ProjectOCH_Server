# ProjectOCH Server - Codex Start Here

> 새 Codex 채팅에서 이 vault를 참고할 때 가장 먼저 읽을 문서.

## 한 줄 요약

ProjectOCH 서버는 Windows C++ 기반의 Visual Studio 솔루션이다. `ServerCore`가 IOCP 네트워크, 세션, 스레드, 잡 큐를 제공하고, `GameServer`가 패킷 처리와 룸/오브젝트 게임 로직을 얹는다. `DummyClient`는 같은 프로토콜을 쓰는 테스트 클라이언트다.

## 실제 소스 위치

- Obsidian vault: `C:\ProjectOCH\Server\ProjectOCH_Server`
- 실제 솔루션 루트: `C:\ProjectOCH\Server`
- 솔루션 파일: `C:\ProjectOCH\Server\Server.sln`

## 먼저 볼 파일

- `C:\ProjectOCH\Server\GameServer\GameServer.cpp`
  - 서버 시작점. `127.0.0.1:7777`에서 `ServerService`를 열고 워커 스레드 5개를 실행한다.
- `C:\ProjectOCH\Server\ServerCore\Session.h`
  - 세션, 패킷 세션, 패킷 헤더 구조.
- `C:\ProjectOCH\Server\ServerCore\Session.cpp`
  - connect, recv, send, disconnect IOCP 처리 흐름.
- `C:\ProjectOCH\Server\GameServer\ServerPacketHandler.cpp`
  - 클라이언트 패킷별 게임 로직 진입점.
- `C:\ProjectOCH\Server\GameServer\Room.cpp`
  - 입장, 퇴장, 이동, 브로드캐스트 로직.
- `C:\ProjectOCH\Server\Common\protoc-21.12-win64\bin\Protocol.proto`
  - 현재 패킷 원본으로 보이는 proto 파일.

## 프로젝트별 역할

- `ServerCore`
  - 정적 라이브러리.
  - IOCP, socket helper, session, listener, send/recv buffer, thread manager, job queue, timer queue를 담당한다.
- `GameServer`
  - 실행 서버.
  - `ServerCore`를 링크하고 `GameSession`, `ServerPacketHandler`, `Room`, `Object/Player/Monster` 로직을 담당한다.
- `DummyClient`
  - 테스트 클라이언트.
  - 서버와 같은 protobuf 생성물을 사용한다.
- `Common\protoc-21.12-win64\bin`
  - proto 원본, `protoc.exe`, 패킷 핸들러 생성/복사 bat가 있다.
- `Tools\PacketGenerator`
  - Python/Jinja2 기반 패킷 핸들러 생성기.
- `Libraries`
  - protobuf include/lib, ServerCore lib 출력 위치.
- `Binaries`
  - GameServer/DummyClient 실행 파일 출력 위치.

## 주요 실행 흐름

1. `GameServer.cpp`에서 `ServerPacketHandler::Init()` 호출.
2. `ServerService` 생성: 주소 `127.0.0.1:7777`, `IocpCore`, `GameSession` factory, max session `100`.
3. `ServerService::Start()`가 `Listener::StartAccept()`를 호출한다.
4. `Listener`가 accept event를 미리 걸고, 접속 성공 시 `Session::ProcessConnect()`로 연결 세션을 등록한다.
5. 워커 스레드가 반복적으로 `IocpCore::Dispatch(10)`, 예약 Job 분배, GlobalQueue 작업을 처리한다.
6. `PacketSession::OnRecv()`가 `[size:uint16][id:uint16][payload]` 단위로 패킷을 조립한다.
7. `GameSession::OnRecvPacket()`이 `ServerPacketHandler::HandlePacket()`으로 넘긴다.
8. `Handle_C_ENTER_GAME`, `Handle_C_MOVE` 등에서 `GRoom->DoAsync(...)`로 룸 잡 큐에 게임 로직을 밀어 넣는다.

## 매우 중요한 주의사항

- `GameServer\Protocol.proto`, `Struct.proto`는 현재 생성된 `.pb.h` 및 핸들러와 맞지 않는 오래된 파일로 보인다.
- 더 최신 원본은 `Common\protoc-21.12-win64\bin\Protocol.proto`, `Struct.proto`, `Enum.proto`로 보인다.
- `GenPackets.bat`는 proto 생성 후 결과물을 `GameServer`, `DummyClient`, 그리고 외부 `Client\Assets\Scripts\Packet\Generated`로 복사한다.
- 패킷 ID는 `PacketGenerator`가 `message` 선언 순서대로 1000부터 부여한다. proto 메시지 순서를 바꾸면 ID가 바뀐다.
- Git 명령은 현재 환경에서 `dubious ownership`으로 막힐 수 있다. 필요하면 사용자 승인 후 `safe.directory` 설정이 필요하다.

## 이어서 읽을 문서

- [[01_Project_Map]]
- [[02_ServerCore_Architecture]]
- [[03_GameServer_Flow]]
- [[04_Packet_Protocol_Generation]]
- [[05_Working_Notes]]

