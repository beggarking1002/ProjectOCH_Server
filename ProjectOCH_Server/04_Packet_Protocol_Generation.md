# Packet Protocol Generation

## 현재 기준 proto 위치

가장 신뢰할 원본은 다음 경로다.

```text
C:\ProjectOCH\Server\Common\protoc-21.12-win64\bin
├─ Enum.proto
├─ Struct.proto
└─ Protocol.proto
```

`GenPackets.bat`가 이 위치에서 `protoc.exe`와 `GenPackets.exe`를 실행한다. 생성 결과는 `GameServer`, `DummyClient`, Unity client로 복사된다.

## 현재 proto 요약

`Enum.proto`:

- `ObjectType`
  - `OBJECT_TYPE_NONE`
  - `OBJECT_TYPE_CREATURE`
  - `OBJECT_TYPE_PROJECTILE`
  - `OBJECT_TYPE_ENV`
- `CreatureType`
  - `CREATURE_TYPE_NONE`
  - `CREATURE_TYPE_PLAYER`
  - `CREATURE_TYPE_MONSTER`
  - `CREATURE_TYPE_NPC`
- `PawnClass`
  - `PAWN_CLASS_SUEN_AXE_SWORD`
  - `PAWN_CLASS_SUEN_PARVIS`
  - `PAWN_CLASS_BEIGE_FIRE`
  - `PAWN_CLASS_BEIGE_ICE`
  - `PAWN_CLASS_ZILLIAN_LONGBOW`
  - `PAWN_CLASS_ZILLIAN_MACE`
  - `PAWN_CLASS_ALEN_SPEAR`
  - `PAWN_CLASS_ALEN_SWORD_SHIELD`
  - `PAWN_CLASS_SERA_NECROMANCER`
  - `PAWN_CLASS_SERA_WARLOCK`
- `BattleMoveResult`
  - `BATTLE_MOVE_RESULT_OK`
  - `BATTLE_MOVE_RESULT_NOT_YOUR_TURN`
  - `BATTLE_MOVE_RESULT_NOT_OWNER`
  - `BATTLE_MOVE_RESULT_NOT_WALKABLE`
  - `BATTLE_MOVE_RESULT_OUT_OF_RANGE`
  - `BATTLE_MOVE_RESULT_OCCUPIED`
  - `BATTLE_MOVE_RESULT_INVALID_BATTLE`
  - `BATTLE_MOVE_RESULT_INVALID_PAWN`

`Struct.proto`:

```proto
message Vec2Fixed
{
    sint32 x = 1;
    sint32 y = 2;
}

message ObjectInfo
{
    uint64 object_id = 1;
    ObjectType object_type = 2;
    CreatureType creature_type = 3;
    Vec2Fixed position = 4;
}

message AxialCoord
{
    sint32 q = 1;
    sint32 r = 2;
}

message BattlePawnInfo
{
    uint64 pawn_id = 1;
    uint64 owner_id = 2;
    PawnClass pawn_class = 3;
    AxialCoord axial = 4;
    int32 hp = 5;
    int32 max_hp = 6;
    int32 move_range = 7;
}
```

`Protocol.proto`:

- `C_LOGIN` / `S_LOGIN`
- `C_ENTER_GAME` / `S_ENTER_GAME`
- `C_LEAVE_GAME` / `S_LEAVE_GAME`
- `S_SPAWN`
- `S_DESPAWN`
- `C_MOVE` / `S_MOVE`
- `C_CHAT` / `S_CHAT`
- `C_ENTER_BATTLE` / `S_ENTER_BATTLE`
- `C_BATTLE_MOVE` / `S_BATTLE_MOVE`

이동 패킷:

```proto
message C_MOVE
{
    Vec2Fixed target = 1;
}

message S_MOVE
{
    uint64 object_id = 1;
    Vec2Fixed start = 2;
    Vec2Fixed target = 3;
    uint32 duration_ms = 4;
}
```

## wire format

프로젝트 패킷은 protobuf payload 앞에 4-byte header를 붙인다.

```text
uint16 size
uint16 id
protobuf payload
```

`PacketSession::OnRecv()`가 이 헤더를 기준으로 패킷을 잘라 `OnRecvPacket()`에 넘긴다.

## 패킷 ID 정책

`Tools\PacketGenerator\ProtoParser.py`는 `message` 선언을 위에서 아래로 읽는다.

- 시작 ID: `1000`
- recv prefix: 실행 인자에 따라 `C_` 또는 `S_`
- send prefix: 실행 인자에 따라 `S_` 또는 `C_`
- `message` 선언 순서대로 ID 증가

따라서 proto 메시지 순서를 바꾸면 기존 ID가 바뀐다. 라이브 프로토콜을 유지해야 한다면 기존 메시지는 건드리지 말고 뒤에 추가하는 방식이 안전하다.

현재 순서 기준 주요 ID:

- `C_LOGIN = 1000`
- `S_LOGIN = 1001`
- `C_ENTER_GAME = 1002`
- `S_ENTER_GAME = 1003`
- `C_LEAVE_GAME = 1004`
- `S_LEAVE_GAME = 1005`
- `S_SPAWN = 1006`
- `S_DESPAWN = 1007`
- `C_MOVE = 1008`
- `S_MOVE = 1009`
- `C_CHAT = 1010`
- `S_CHAT = 1011`
- `C_ENTER_BATTLE = 1012`
- `S_ENTER_BATTLE = 1013`
- `C_BATTLE_MOVE = 1014`
- `S_BATTLE_MOVE = 1015`

## 생성 스크립트

`Common\protoc-21.12-win64\bin\GenPackets.bat`가 하는 일:

1. C++ protobuf 생성
   - `Enum.pb.h/.cc`
   - `Struct.pb.h/.cc`
   - `Protocol.pb.h/.cc`
2. C# protobuf 생성
   - `Enum.cs`
   - `Struct.cs`
   - `Protocol.cs`
3. packet helper 생성
   - `ClientPacketHandler.h`: client가 받는 `S_`, 보내는 `C_`
   - `ServerPacketHandler.h`: server가 받는 `C_`, 보내는 `S_`
   - `PacketManager.cs`: Unity client용 packet routing
4. 생성물을 복사
   - `GameServer`
   - `DummyClient`
   - `C:\ProjectOCH\Client\Assets\Scripts\Packet\Generated`
5. 임시 생성물을 삭제

현재 `GenPackets.bat`는 각 단계마다 `ERRORLEVEL`을 검사하고, `ClientPacketHandler.h`, `ServerPacketHandler.h`, `PacketManager.cs` 생성 누락을 감지한다.

## PacketGenerator

경로:

```text
C:\ProjectOCH\Server\Tools\PacketGenerator
```

구성:

- `PacketGenerator.py`
  - argparse로 proto path/output/recv/send prefix를 받는다.
  - Jinja2 template으로 `.h` 또는 `.cs` 생성.
- `ProtoParser.py`
  - 단순 line parser. `message`로 시작하는 줄만 인식한다.
  - nested message나 특수 formatting에는 취약할 수 있다.
- `Templates\PacketHandler.h`
  - C++ handler header template.
  - 현재 Unreal 관련 include/분기는 제거되어 일반 C++ `make_shared` 기반이다.
- `Templates\PacketManager.cs`
  - Unity C# packet manager template.
- `MakeExe.bat`
  - `py -3 -m PyInstaller` 또는 `python -m PyInstaller`로 `GenPackets.exe`를 재생성한다.
  - 생성된 exe를 `Common\protoc-21.12-win64\bin`으로 복사한다.

## 새 패킷 추가 절차

1. `Common\protoc-21.12-win64\bin\Protocol.proto`에 message를 추가한다.
2. 가능하면 기존 메시지 순서는 유지하고 맨 뒤에 추가한다.
3. `GenPackets.bat`를 실행하거나 `GameServer` 빌드의 pre-build로 생성한다.
4. `GameServer`에 새 `Handle_C_*` 함수 구현을 추가한다.
5. Unity client 쪽 `PacketHandler`, `NetworkService`, gameplay 반영 코드를 갱신한다.
6. 필요하면 `DummyClient\ClientPacketHandler.cpp`도 갱신한다.
7. `Server.sln /t:GameServer` 빌드로 생성/컴파일을 검증한다.

## 주의사항

- `GameServer` 폴더의 proto 파일은 빌드 결과로 복사되는 파일이다. 원본 수정은 `Common` 쪽에서 한다.
- Unity client generated C#은 서버 루트 밖(`C:\ProjectOCH\Client`)으로 복사된다. 자동 빌드/검증 환경에서는 권한 이슈가 날 수 있다.
- `GenPackets.bat`에는 `PAUSE`가 남아 있다. Visual Studio pre-build에서는 로그에 보이지만 현재 빌드는 통과한다.
