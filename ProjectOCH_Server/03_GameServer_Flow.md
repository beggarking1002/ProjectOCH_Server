# GameServer Flow

## 시작점

`GameServer\GameServer.cpp`의 `main()`:

1. `GFieldWalkMapData.LoadFromFile("C:\\ProjectOCH\\Server\\Data\\Maps\\Field_001.walkmap.json")`
2. `ServerPacketHandler::Init()`
3. `ServerService` 생성
   - address: `127.0.0.1:7777`
   - core: `make_shared<IocpCore>()`
   - session factory: `make_shared<GameSession>()`
   - max session: `100`
4. `service->Start()`
5. worker thread 5개 실행
6. `GRoom->DoAsync(&Room::UpdateTick)`
7. main thread는 sleep loop

worker loop인 `DoWorkerJob()`는 반복적으로 다음 일을 한다.

- `service->GetIocpCore()->Dispatch(10)`
- `ThreadManager::DistributeReservedJobs()`
- `ThreadManager::DoGlobalQueueWork()`

## 접속과 세션 관리

`GameSession`은 `PacketSession`을 상속한다.

- `OnConnected()`
  - `GSessionManager.Add(session)`
  - 이 시점에는 player를 만들거나 room에 넣지 않는다.
- `OnDisconnected()`
  - session에 player가 있으면 player의 room에 `Room::HandleLeavePlayer`를 `DoAsync`로 요청한다.
  - room이 없으면 `session->player`만 비운다.
  - `GSessionManager.Remove(session)`
- `OnRecvPacket()`
  - `ServerPacketHandler::HandlePacket(session, buffer, len)`

## 현재 패킷

server가 받는 패킷:

- `C_LOGIN`
- `C_ENTER_GAME`
- `C_LEAVE_GAME`
- `C_MOVE`
- `C_CHAT`
- `C_ENTER_BATTLE`
- `C_BATTLE_MOVE`
- `C_BATTLE_SKILL`

server가 보내는 패킷:

- `S_LOGIN`
- `S_ENTER_GAME`
- `S_LEAVE_GAME`
- `S_SPAWN`
- `S_DESPAWN`
- `S_MOVE`
- `S_CHAT`
- `S_ENTER_BATTLE`
- `S_BATTLE_MOVE`
- `S_BATTLE_SKILL`

## 로그인

`Handle_C_LOGIN()`:

1. 현재는 DB 조회 없이 `S_LOGIN.success = true`.
2. `SEND_PACKET(loginPkt)`.

로그인은 접속 검증 역할만 한다. player 생성과 room 입장은 `C_ENTER_GAME`에서 한다.

## 게임 입장

`Handle_C_ENTER_GAME()`:

1. session에 player가 없으면 `ObjectUtils::CreatePlayer(gameSession)`로 생성한다.
2. `GRoom->DoAsync(&Room::HandleEnterPlayer, player)`.

`Room::HandleEnterPlayer()`:

1. player/session 관계가 유효한지 확인한다.
2. 이미 room에 있으면 `S_ENTER_GAME`과 기존 플레이어 `S_SPAWN`을 다시 보낸다.
3. room에 없으면 `EnterRoom(player, true)`.

`Room::EnterRoom()`:

1. `_objects`에 object 등록.
2. `FieldWalkMapData::TryGetRandomWalkablePosition()`으로 walkable spawn position 부여.
3. 신입 player에게 `S_ENTER_GAME(success=true, player=ObjectInfo)` 전송.
4. 신입 player에게 기존 player 목록을 `S_SPAWN`으로 전송.
5. 다른 player들에게 신입 object를 `S_SPAWN`으로 전송.

Unity client 쪽 `FieldObjectManager`는 `S_ENTER_GAME.Player`를 내 pawn 생성 기준으로 사용하고, `S_SPAWN`을 다른 player pawn 생성/갱신 기준으로 사용한다.

## 게임 퇴장

`Handle_C_LEAVE_GAME()`:

1. session에서 player를 얻는다.
2. player가 속한 room을 얻는다.
3. `room->DoAsync(&Room::HandleLeavePlayer, gameSession)`.

`GameSession::OnDisconnected()`도 같은 방향으로 room leave를 `DoAsync`로 보낸다.

`Room::LeaveRoom()`:

1. `_objects`에서 object 제거.
2. 퇴장 player에게 `S_LEAVE_GAME` 전송.
3. 주변 player와 본인에게 `S_DESPAWN(object_id)` 전송.
4. session의 player 참조를 정리한다.

## 이동

`C_MOVE.target`은 `Vec2Fixed` fixed-point world 좌표다.

`Handle_C_MOVE()`:

1. session에서 player를 얻는다.
2. player room을 얻는다.
3. `room->DoAsync(&Room::HandleMove, gameSession, pkt)`.

`Room::HandleMove()`:

1. `GetPlayerInRoom(session)`으로 session과 room에 묶인 player인지 재검증한다.
2. `pkt.target` 존재 여부를 확인한다.
3. `FieldWalkMapData::IsWalkableFixed(pkt.target, cellX, cellY)`로 target을 검증한다.
4. walkable이면:
   - 현재 서버 위치를 `start`로 저장.
   - `player->position`을 target으로 갱신.
   - `S_MOVE(object_id, start, target, duration_ms=300)`을 room 전체에 broadcast.
5. walkable이 아니면:
   - 서버 위치는 갱신하지 않는다.
   - 요청 client에게만 `S_MOVE(object_id, start=current, target=current, duration_ms=0)`을 보내 보정한다.

## 전투 입장과 전투 이동

현재 전투 처리는 `BattleRoom`이 담당한다. `ServerPacketHandler.cpp`는 `C_ENTER_BATTLE`, `C_BATTLE_MOVE`를 받은 뒤 `GBattleRoom->DoAsync(...)`로 작업을 넘긴다. 전투 상태 변경은 `BattleRoom` job queue 안에서 직렬 실행된다.

`Handle_C_ENTER_BATTLE()`:

1. `GBattleRoom->DoAsync(&BattleRoom::HandleEnterBattle, gameSession)`로 위임한다.

`BattleRoom::HandleEnterBattle()`:

1. session에 player가 없으면 `S_ENTER_BATTLE(success=false, reason="player is not in game")`을 보낸다.
2. player가 있으면 player object id를 owner id로 사용한다.
3. owner별 battle이 없으면 `Battle_Test_001` 전투를 새로 만든다.
4. allied pawn 2개, enemy pawn 2개, `current_turn_pawn_id`를 채운 `S_ENTER_BATTLE(success=true)`를 보낸다.

`Handle_C_BATTLE_MOVE()`:

1. `GBattleRoom->DoAsync(&BattleRoom::HandleBattleMove, gameSession, pkt)`로 위임한다.

`BattleRoom::HandleBattleMove()`:

1. player/session, battle id, pawn id, target 존재 여부를 검증한다.
2. 소유자, 현재 턴, hex radius 6 walkable 범위, axial 이동 거리, 점유 여부를 검사한다.
3. 실패하면 `S_BATTLE_MOVE(success=false, result=...)`로 구체적인 실패 enum을 보낸다.
4. 성공하면 pawn axial을 갱신하고 allied pawn 기준으로 다음 턴을 넘긴 뒤 `S_BATTLE_MOVE(success=true, result=OK)`를 보낸다.

`Handle_C_BATTLE_SKILL()`:

1. `GBattleRoom->DoAsync(&BattleRoom::HandleBattleSkill, gameSession, pkt)`로 위임한다.

`BattleRoom::HandleBattleSkill()`:

1. player/session, battle id, caster pawn id, target pawn id를 검증한다.
2. caster 소유자와 현재 턴을 검증한다.
3. `skill_slot`별 damage/range를 얻는다. 현재 slot 0은 damage 25/range 1, slot 1은 damage 35/range 3이다.
4. target pawn 위치와 `target_axial`이 맞는지 확인하고, 사거리 안이면 target hp를 차감한다.
5. 성공하면 allied pawn 기준으로 다음 턴을 넘기고 `S_BATTLE_SKILL(success=true, damage, target_hp, next_turn_pawn_id)`를 보낸다.
6. 실패하면 `S_BATTLE_SKILL(success=false, reason=...)`을 보낸다.

아직 실제 전투 룸, 적 AI, 턴 큐, 전투 종료, persistent battle state는 없다. 클라이언트 전투 UI/연동을 시작하기 위한 서버 응답 골격이다.

## FieldWalkMapData

`FieldWalkMapData`는 Unity exporter가 만든 JSON을 읽는다.

```text
C:\ProjectOCH\Server\Data\Maps\Field_001.walkmap.json
```

주요 데이터:

- `fixed_point_scale`: fixed world 좌표 스케일. 현재 100.
- `cell_size`: Unity Grid cell size. 현재 Field_001은 x 0.95, y 1.0.
- `origin_world`: tilemap origin.
- `walkable_ranges`: row별 inclusive `x_min..x_max`.

현재 `Field_001`은 Unity Hexagon Grid다.

- row stride는 `cellSize.y * 0.75`.
- odd row에는 x offset `0.5 * cellSize.x`가 적용된다.
- 서버 `FixedToCell`은 주변 후보 cell center 중 가장 가까운 cell을 고른다.
- 서버 `CellToFixed`는 Unity `GetCellCenterWorld`와 맞는 cell center를 만든다.

`walkable_ranges`가 비어 있으면 서버 시작 시 `Empty walkable_ranges` 오류가 난다. 이 경우 Unity exporter에서 `Ground_Tilemap`이 잘 잡혔는지 확인해야 한다.

## 오브젝트 모델

현재 계층:

```text
Object
└─ Creature
   ├─ Player
   └─ Monster
```

`Object`는 `unique_ptr<Protocol::ObjectInfo>`를 소유하고, `Protocol::Vec2Fixed* position`은 `ObjectInfo.position`을 가리킨다.

`Player`는 `GameSession` weak reference를 가진다. session 쪽에는 `atomic<shared_ptr<Player>> player`가 있다.

## 현재 미구현/TODO 성격

- 로그인 DB 조회.
- 채팅 처리.
- 여러 room/map 확장.
- 이동 속도, 최대 이동 거리, path 검증.
- IOCP error logging 보강.
