# GameServer Flow

## 시작점

`GameServer\GameServer.cpp`의 `main()`:

1. `ServerPacketHandler::Init()`
2. `ServerService` 생성
   - address: `127.0.0.1:7777`
   - core: `make_shared<IocpCore>()`
   - session factory: `make_shared<GameSession>()`
   - max session: `100`
3. `service->Start()`
4. worker thread 5개 실행
5. `GRoom->DoAsync(&Room::UpdateTick)`
6. main thread는 sleep loop

worker loop인 `DoWorkerJob()`는 반복적으로 다음 일을 한다.

- `service->GetIocpCore()->Dispatch(10)`
- `ThreadManager::DistributeReservedJobs()`
- `ThreadManager::DoGlobalQueueWork()`

## 접속과 세션 관리

`GameSession`은 `PacketSession`을 상속한다.

- `OnConnected()`
  - `GSessionManager.Add(...)`
- `OnDisconnected()`
  - `GSessionManager.Remove(...)`
- `OnRecvPacket()`
  - `ServerPacketHandler::HandlePacket(session, buffer, len)`

`GameSessionManager`는 접속 중인 `GameSession`들을 set으로 관리하고 broadcast를 제공한다.

## 패킷 핸들러

`ServerPacketHandler::Init()`는 `GPacketHandler[UINT16_MAX]`를 모두 `Handle_INVALID`로 초기화한 뒤, C_ 패킷 id에 handler lambda를 넣는다.

현재 server가 받는 패킷:

- `C_LOGIN`
- `C_ENTER_GAME`
- `C_LEAVE_GAME`
- `C_MOVE`
- `C_CHAT`

현재 server가 보내는 패킷:

- `S_LOGIN`
- `S_ENTER_GAME`
- `S_LEAVE_GAME`
- `S_SPAWN`
- `S_DESPAWN`
- `S_MOVE`
- `S_CHAT`

## 로그인

`Handle_C_LOGIN()`:

1. TODO 주석상 DB에서 account/user 정보를 읽을 예정.
2. 현재는 임시로 player 3개를 만들고 random position을 넣는다.
3. `S_LOGIN.success = true`.
4. `SEND_PACKET(loginPkt)`.

## 게임 입장

`Handle_C_ENTER_GAME()`:

1. `ObjectUtils::CreatePlayer(gameSession)`로 `Player` 생성.
2. `GRoom->DoAsync(&Room::HandleEnterPlayer, player)`로 룸 job queue에 입장 처리 요청.

`Room::EnterRoom()`:

1. `_objects`에 object 등록.
2. random position 부여.
3. 신입 player에게 `S_ENTER_GAME` 전송.
4. 다른 player들에게 `S_SPAWN` 전송.
5. 기존 player 목록을 신입 player에게 `S_SPAWN`으로 전송.

## 게임 퇴장

`Handle_C_LEAVE_GAME()`:

1. session에서 player를 얻는다.
2. player가 속한 room을 얻는다.
3. 현재 코드는 `room->HandleLeavePlayer(player)`를 직접 호출한다.

주의: `C_ENTER_GAME`, `C_MOVE`는 `DoAsync()`를 쓰지만 leave는 직접 호출한다. Room state 일관성을 위해 나중에 `DoAsync()` 대상인지 검토할 필요가 있다.

`Room::LeaveRoom()`:

1. `_objects`에서 제거.
2. 퇴장 player에게 `S_LEAVE_GAME` 전송.
3. 주변 player와 본인에게 `S_DESPAWN` 전송.

## 이동

`Handle_C_MOVE()`:

1. session에서 player를 얻는다.
2. player room을 얻는다.
3. `room->DoAsync(&Room::HandleMove, pkt)`로 이동 처리 요청.

`Room::HandleMove()`:

1. `pkt.info().object_id()`로 object를 찾는다.
2. player의 `posInfo`를 packet info로 갱신한다.
3. `S_MOVE`를 만들어 room 전체에 broadcast한다.

## 오브젝트 모델

현재 계층:

```text
Object
└─ Creature
   ├─ Player
   └─ Monster
```

`Object`는 protobuf `ObjectInfo`와 `PosInfo`를 들고 있고, room weak reference를 가진다.

`Player`는 `GameSession` weak reference를 가진다. session 쪽에는 `atomic<shared_ptr<Player>> player`가 있다.

## 현재 미구현/TODO 성격

- 로그인 DB 조회.
- 채팅 처리.
- disconnect 시 room에서 player 제거 여부 확인 필요.
- `Service::CloseService()` 구현.
- IOCP error logging.

