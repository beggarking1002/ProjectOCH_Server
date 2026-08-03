# Project Structure Audit - 2026-07-29

## 목적

이 문서는 현재 `C:\ProjectOCH\Server` 코드와 솔루션 구성을 기준으로 정리한 프로젝트 진입점 및 책임 경계다. 기능 구현 전 빠르게 관련 모듈을 찾는 용도로 사용한다.

## 솔루션 구성

`Server.sln`은 Visual Studio 2022 / v143 기반의 네 프로젝트를 포함한다.

| 프로젝트 | 유형 | 책임 |
| --- | --- | --- |
| `ServerCore` | C++ 정적 라이브러리 | Windows IOCP, 세션, 송수신 버퍼, job queue, worker thread |
| `GameServer` | C++ 콘솔 실행 파일 | 필드/전투 게임 로직, 패킷 처리, 런타임 데이터 로딩 |
| `DummyClient` | C++ 콘솔 실행 파일 | 서버 연결 및 패킷 흐름 확인용 테스트 클라이언트 |
| `Tools\\PacketGenerator` | Python 도구 | protobuf 메시지에서 C++/Unity 패킷 라우팅 코드 생성 |

`GameServer`와 `DummyClient` 실행 파일은 `Binaries\\Debug` 또는 `Binaries\\Release`에 출력되고, `ServerCore` 라이브러리는 `Libraries\\Libs\\ServerCore`에 출력된다.

## 런타임 흐름

```text
Unity Client / DummyClient
  -> Listener / AcceptEx
  -> Session / PacketSession
  -> GameSession
  -> ServerPacketHandler
  -> Room (field) 또는 BattleRoom (battle)
```

`GameServer.cpp`는 다음 순서로 시작한다.

1. `Data\\Maps\\Field_001.walkmap.json`을 로드한다.
2. `BattleTemplateManager`로 `Data\\*.csv` 전투 데이터를 로드하고 검증한다.
3. 패킷 핸들러를 초기화하고 `127.0.0.1:7777`, 최대 100 세션의 `ServerService`를 시작한다.
4. 5개 worker thread가 IOCP dispatch, 예약 job, global queue를 반복 처리한다.

`Room`과 `BattleRoom`은 모두 `JobQueue`를 상속한다. 같은 룸의 상태 변경은 `DoAsync`를 통해 직렬화되어, 네트워크 worker가 여러 개여도 room state를 동시에 변경하지 않는다.

## 모듈별 책임

### ServerCore

- `IocpCore`, `IocpEvent`, `Listener`, `Session`, `Service`: Windows IOCP socket lifecycle.
- `RecvBuffer`, `SendBuffer`, `BufferReader`, `BufferWriter`: packet buffer 처리.
- `ThreadManager`, `JobQueue`, `GlobalQueue`, `JobTimer`: 순차 실행이 필요한 게임 job과 worker scheduling.

### 필드 게임 로직

- `GameSession`: 연결/해제와 packet receive hook.
- `ServerPacketHandler`: `C_*` protobuf message를 검증 대상 room handler로 전달.
- `Room`: player 입장/퇴장, spawn/despawn, 필드 이동, PvP 초대 상태.
- `FieldWalkMapData`: Unity가 export한 Odd-R field walk map을 읽고 fixed-point 위치를 검증.
- `Object -> Creature -> Player/Monster`: 필드 객체 계층.

### 전투 게임 로직

- `BattleRoom`: battle lifecycle, ownership/current-turn validation, 상태 전이, 결과 packet 조립.
- `BattlePawn` 및 클래스 계층: 특정 pawn class의 고유 행동과 상태.
- `BattleSpatialService`, `BattleMovementService`, `BattleDisplacementService`, `BattleTurnService`, `BattleZocService`: 재사용 가능한 공간/이동/밀치기/턴/ZOC 규칙.
- `BattleSkillExecutionService`, `BattleSkillResolver`, `BattleEffectExecutor`: target area, skill modifier, CSV effect group 실행.
- `BattleTemplateManager`: CSV parsing과 cross-table validation, map tile 변환.

전투 좌표는 프로토콜과 서버 내부에서 axial `(q, r)` 좌표를 사용한다. Unity Point Top / Odd-R 타일 좌표와의 변환은 클라이언트/맵 경계에서만 처리한다.

## 프로토콜과 생성물

수정 가능한 프로토콜 원본은 다음 세 파일뿐이다.

```text
Common\\protoc-21.12-win64\\bin\\Enum.proto
Common\\protoc-21.12-win64\\bin\\Struct.proto
Common\\protoc-21.12-win64\\bin\\Protocol.proto
```

패킷 wire format은 `[uint16 size][uint16 id][protobuf payload]`다. `GameServer`의 pre-build step은 `GenPackets.bat`을 실행해 C++ protobuf, C++ handler, Unity C# packet manager를 생성한다. 따라서 `GameServer`와 `DummyClient`의 `*.pb.*`, generated handler는 직접 수정하지 않는다.

## 런타임 데이터

| 경로 | 용도 |
| --- | --- |
| `Data\\Maps\\Field_001.walkmap.json` | 필드 이동 가능 셀 |
| `Data\\PawnTemplate.csv`, `ClassKey.csv` | pawn class와 기본 스탯 |
| `Data\\BattleSkill*.csv` | skill, variant, effect group, effect parameter |
| `Data\\Maps\\BattleField_001.walkmap.json` | 전투 맵의 유효 타일과 서버 이동·경계 판정 기준 |
| `Data\\BattleZoc.csv`, `BattleConfig.csv` | ZOC와 공통 전투 규칙 |

## 현재 전투 상태

구현된 주요 범위는 PvP 초대/수락, shared battle entry, 턴 queue, 행동 사용/이동/사거리/점유/소유권 검증, armor/barrier/status/aura, battle result acknowledgement와 필드 복귀다. action response는 pawn/tile delta와 action log를 포함하므로 클라이언트는 서버 응답을 최종 상태로 반영해야 한다.

후속 우선순위는 Unity의 axial conversion 실전 검증, prop collision 검증, 남은 클래스의 data-driven skill 작성, target shape 확장, 전투 simulation test다. 자세한 규칙은 [[10_Battle_Architecture_Rules]], 구현 현황은 [[09_Implementation_Status_2026-07-20]]을 따른다.
