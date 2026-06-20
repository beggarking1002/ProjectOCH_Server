# Working Notes

## 빌드 관련

- 솔루션: `C:\ProjectOCH\Server\Server.sln`
- IDE: Visual Studio 2022 계열로 보임.
- 주요 configuration: Debug/Release, x64/Win32.
- `ServerCore`는 static library로 빌드된다.
- `GameServer`, `DummyClient`는 `Binaries\$(Configuration)\`로 출력된다.
- `ServerCore` 산출물은 `Libraries\Libs\ServerCore\$(Configuration)\`로 출력된다.

## 링크/Include

`GameServer`와 `DummyClient`는 다음 include/lib 경로를 사용한다.

- Include
  - `$(SolutionDir)ServerCore\`
  - `$(SolutionDir)Libraries\Include\`
- Library
  - `$(SolutionDir)Libraries\Libs\`

`Libraries\Libs` 아래에는 protobuf 및 ServerCore lib가 있는 것으로 보인다.

## 코딩 스타일

- C++17 전후 Visual Studio 스타일.
- `shared_ptr` 기반 lifetime 관리.
- 전역 ref type alias는 `Types.h` 쪽을 확인.
- `USE_LOCK`, `READ_LOCK`, `WRITE_LOCK`, `ASSERT_CRASH`, `OUT` 같은 매크로는 `CoreMacro.h` 계열을 확인.
- 컨텐츠 로직은 room job queue로 직렬화하는 방향이다.

## 자주 보는 흐름

### 서버 실행

```text
GameServer.cpp
-> ServerService::Start
-> Listener::StartAccept
-> worker thread DoWorkerJob
-> IocpCore::Dispatch
```

### 패킷 수신

```text
Session::ProcessRecv
-> PacketSession::OnRecv
-> GameSession::OnRecvPacket
-> ServerPacketHandler::HandlePacket
-> Handle_C_*
```

### 룸 작업

```text
Handle_C_ENTER_GAME / Handle_C_MOVE
-> GRoom->DoAsync(...)
-> JobQueue::Push
-> JobQueue::Execute
-> Room::Handle*
```

## 잠재 리스크/확인 포인트

- `Handle_C_LEAVE_GAME()`는 `Room` job queue를 통하지 않고 `room->HandleLeavePlayer(player)`를 직접 호출한다.
- disconnect 시 `GameSessionManager`에서는 제거되지만 room에서 player 제거가 자동으로 되는지 확인해야 한다.
- `IocpCore::Dispatch()`의 error branch에서 `iocpEvent` null 가능성.
- `Session::RegisterSend()`에서 `_sendQueue`를 비우는 부분은 lock 주석이 남아 있어 동시성 검토가 필요하다.
- `Protocol.proto` 원본 위치가 중복되어 있다. `Common` 쪽을 우선한다.
- `PacketGenerator`의 proto parser는 단순 문자열 기반이라 proto formatting 변화에 약하다.

## Git 상태

현재 Codex sandbox 계정에서는 `git status` 실행 시 다음 문제가 날 수 있다.

```text
fatal: detected dubious ownership in repository at 'C:/ProjectOCH/Server'
```

해결하려면 사용자 승인 후 다음 설정이 필요할 수 있다.

```powershell
git config --global --add safe.directory C:/ProjectOCH/Server
```

## 새 Codex 채팅용 프롬프트 예시

```text
C:\ProjectOCH\Server\ProjectOCH_Server\00_Codex_Start_Here.md 를 먼저 읽고,
ProjectOCH 서버 구조를 파악한 뒤 작업해줘.
```

## 문서 갱신 규칙

- 큰 구조 변경이 있으면 `01_Project_Map.md`를 갱신한다.
- IOCP, 세션, 잡 큐 변경은 `02_ServerCore_Architecture.md`를 갱신한다.
- 패킷 처리나 룸 로직 변경은 `03_GameServer_Flow.md`를 갱신한다.
- proto/패킷 생성 방식 변경은 `04_Packet_Protocol_Generation.md`를 갱신한다.
- 작업 중 발견한 위험이나 TODO는 이 파일에 남긴다.

