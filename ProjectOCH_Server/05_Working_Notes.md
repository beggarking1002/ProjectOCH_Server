# Working Notes

## 빌드 관련

- 솔루션: `C:\ProjectOCH\Server\Server.sln`
- IDE: Visual Studio 2022 계열.
- 주요 configuration: Debug/Release, x64/Win32.
- `ServerCore`는 static library로 빌드된다.
- `GameServer`, `DummyClient`는 `Binaries\$(Configuration)\`로 출력된다.
- `ServerCore` 산출물은 `Libraries\Libs\ServerCore\$(Configuration)\`로 출력된다.

자주 쓰는 검증:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Server.sln /t:GameServer /p:Configuration=Debug /p:Platform=x64 /m:1
```

`GameServer` pre-build는 generated C#을 `C:\ProjectOCH\Client\Assets\Scripts\Packet\Generated`에 복사한다.

## 링크/Include

`GameServer`와 `DummyClient`는 다음 include/lib 경로를 사용한다.

- Include
  - `$(SolutionDir)ServerCore\`
  - `$(SolutionDir)Libraries\Include\`
- Library
  - `$(SolutionDir)Libraries\Libs\`

`Libraries\Libs` 아래에는 protobuf 및 ServerCore lib가 있다.

## 현재 구현 요약

- 연결만으로 player를 만들지 않는다.
- `C_LOGIN`은 현재 `S_LOGIN.success=true` 검증 응답만 보낸다.
- `C_ENTER_GAME`에서 player를 생성하고 room 입장을 요청한다.
- `Room` 상태 변경은 `DoAsync` 기반으로 직렬화한다.
  - enter
  - leave
  - disconnect leave
  - move
- `ObjectInfo`는 `unique_ptr`로 소유한다.
- 위치는 `Vec2Fixed position`만 사용한다.
- 필드 이동 검증은 `Field_001.walkmap.json` 기반이다.
- `Field_001`은 Unity Hexagon Grid이므로 서버 변환도 Hex row stride와 row parity offset을 반영한다.
- `C_ENTER_BATTLE` / `C_BATTLE_MOVE`는 `BattleRoom`의 in-memory battle state로 처리한다.
  - `C_ENTER_BATTLE`은 테스트 전투 `Battle_Test_001`과 allied/enemy pawn 목록을 내려준다.
  - `C_BATTLE_MOVE`는 owner, turn, hex radius 6, move_range, occupied 검사를 하고 `BattleMoveResult`로 응답한다.
  - `C_BATTLE_SKILL`은 caster owner/turn, target pawn, skill_slot 1~5, range를 검사하고 damage/target_hp를 응답한다.
  - `C_BATTLE_END_TURN`은 owner/turn 검증 후 `S_BATTLE_END_TURN.next_turn_pawn_id`로 다음 턴을 내려준다.
  - `ServerPacketHandler.cpp`는 패킷을 받은 뒤 `GBattleRoom->DoAsync(...)`로 위임한다.

## 자주 보는 흐름

### 서버 실행

```text
GameServer.cpp
-> GFieldWalkMapData.LoadFromFile(...)
-> ServerPacketHandler::Init
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
Handle_C_ENTER_GAME / Handle_C_LEAVE_GAME / Handle_C_MOVE
-> room->DoAsync(...)
-> JobQueue::Push
-> JobQueue::Execute
-> Room::Handle*
```

### 전투 패킷

```text
Handle_C_ENTER_BATTLE
-> GBattleRoom->DoAsync
-> BattleRoom::HandleEnterBattle
-> create/reuse in-memory BattleState
-> S_ENTER_BATTLE

Handle_C_BATTLE_MOVE
-> GBattleRoom->DoAsync
-> BattleRoom::HandleBattleMove
-> validate battle/pawn/owner/turn/range/occupied
-> update pawn axial
-> S_BATTLE_MOVE

Handle_C_BATTLE_SKILL
-> GBattleRoom->DoAsync
-> BattleRoom::HandleBattleSkill
-> validate battle/caster/owner/turn/target/skill/range
-> apply damage
-> S_BATTLE_SKILL

Handle_C_BATTLE_END_TURN
-> GBattleRoom->DoAsync
-> BattleRoom::HandleBattleEndTurn
-> validate battle/pawn/owner/turn
-> advance turn
-> S_BATTLE_END_TURN
```

## FieldWalkMapData 체크리스트

서버 시작 시 다음 파일을 읽는다.

```text
C:\ProjectOCH\Server\Data\Maps\Field_001.walkmap.json
```

정상 로그 예:

```text
[FieldWalkMapData] Loaded Field_001 rows=41 fixed_point_scale=100 cell_size=(0.95, 1)
```

`Empty walkable_ranges`가 나오면 서버 파서 문제가 아니라 JSON 안의 `walkable_ranges`가 실제로 비어 있을 가능성이 높다.

확인할 것:

- Unity exporter에서 `Field_001` prefab이 선택되었는가.
- `Ground_Tilemap`이 실제 타일이 있는 tilemap인가.
- `Copy To Server`로 `C:\ProjectOCH\Server\Data\Maps`에 제대로 덮어썼는가.
- JSON 안에 `{ "y": ..., "x_min": ..., "x_max": ... }` 항목이 있는가.

## 현재 프로토콜 체크리스트

- `Struct.proto`
  - `Vec2Fixed`
  - `ObjectInfo.position`
- `Protocol.proto`
  - `S_ENTER_GAME.player`
  - `S_SPAWN.players`
  - `S_DESPAWN.object_ids`
  - `C_MOVE.target`
  - `S_MOVE.object_id/start/target/duration_ms`
  - `C_ENTER_BATTLE`
  - `S_ENTER_BATTLE.battle_id/map_id/allied_pawns/enemy_pawns/current_turn_pawn_id`
  - `C_BATTLE_MOVE.battle_id/pawn_id/target`
  - `S_BATTLE_MOVE.success/battle_id/pawn_id/start/target/next_turn_pawn_id/result/reason`
  - `C_BATTLE_SKILL.battle_id/caster_pawn_id/skill_slot/target_pawn_id/target_axial`
  - `S_BATTLE_SKILL.success/battle_id/caster_pawn_id/skill_slot/target_pawn_id/target_axial/damage/target_hp/next_turn_pawn_id/reason`
  - `C_BATTLE_END_TURN.battle_id/pawn_id`
  - `S_BATTLE_END_TURN.success/battle_id/pawn_id/next_turn_pawn_id/reason`

`S_LOGIN.players` 같은 로그인 단계 플레이어 목록은 현재 사용하지 않는다. 필드 입장 후 오브젝트 동기화는 `S_ENTER_GAME`과 `S_SPAWN`이 담당한다.

## 잠재 리스크/확인 포인트

- `FieldWalkMapData` JSON 파서는 현재 프로젝트 스키마 전용의 작은 regex 기반 파서다. JSON 구조가 크게 바뀌면 수정이 필요하다.
- Hex `FixedToCell`은 주변 후보 cell center 중 가장 가까운 cell을 고르는 방식이다. Unity `Grid.WorldToCell`과 경계 케이스가 완전히 같은지 실제 클릭 로그로 검증하면 좋다.
- 현재 이동 검증은 target cell walkable 여부만 본다. 경로 중간 장애물, 최대 이동 거리, 속도 검증은 아직 없다.
- 여러 map/room을 지원하려면 `GFieldWalkMapData`, `GRoom` 전역 구조를 map id/room id 기반으로 확장해야 한다.
- 현재 전투 state는 서버 프로세스 메모리에만 있고 `BattleRoom` 하나가 owner별 battle을 관리한다. 실제 기능으로 키우려면 여러 battle room 관리, disconnect/종료/AI/관전/재입장 정책을 정해야 한다.
- 현재 battle skill은 임시 룰이다. slot 1~5를 모두 유효하게 처리하지만 직선/시야/광역/쿨다운/자원 검증은 아직 없다.
- `IocpCore::Dispatch()`의 error branch에서 `iocpEvent` null 가능성.
- `Session::RegisterSend()`에서 `_sendQueue`를 비우는 부분은 lock 주석이 남아 있어 동시성 검토가 필요하다.
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
