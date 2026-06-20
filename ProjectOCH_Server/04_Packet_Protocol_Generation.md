# Packet Protocol Generation

## 현재 기준 proto 위치

가장 신뢰할 원본은 다음 경로로 보인다.

```text
C:\ProjectOCH\Server\Common\protoc-21.12-win64\bin
├─ Enum.proto
├─ Struct.proto
└─ Protocol.proto
```

이유:

- `GenPackets.bat`가 이 위치에서 `protoc.exe`를 실행한다.
- 이 위치의 `Protocol.proto`에는 `S_SPAWN`, `C_MOVE`, `S_MOVE` 등이 들어 있다.
- `GameServer\Protocol.pb.h`의 내용도 이 proto와 맞는다.

반대로 `GameServer\Protocol.proto`, `GameServer\Struct.proto`는 현재 생성물과 맞지 않는 오래된 복사본으로 보인다.

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

따라서 proto 메시지 순서를 바꾸면 기존 ID가 바뀐다. 라이브 프로토콜을 유지해야 한다면 메시지는 뒤에 추가하는 방식이 안전하다.

## 생성 스크립트

`Common\protoc-21.12-win64\bin\GenPackets.bat`가 하는 일:

1. C++ protobuf 생성
   - `protoc.exe -I=./ --cpp_out=./ ./Enum.proto`
   - `protoc.exe -I=./ --cpp_out=./ ./Struct.proto`
   - `protoc.exe -I=./ --cpp_out=./ ./Protocol.proto`
2. C# protobuf 생성
   - `--csharp_out=.`
3. packet handler 생성
   - `ClientPacketHandler`: client가 받는 `S_`, 보내는 `C_`
   - `ServerPacketHandler`: server가 받는 `C_`, 보내는 `S_`
   - `PacketManager.cs`: C# client용으로 보임
4. 생성물을 복사
   - `GameServer`
   - `DummyClient`
   - `..\..\..\..\Client\Assets\Scripts\Packet\Generated`
5. bin 폴더 안의 임시 생성물 삭제

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
- `Templates\PacketManager.cs`
  - C# packet manager template.

## 새 패킷 추가 절차

1. `Common\protoc-21.12-win64\bin\Protocol.proto`에 message를 추가한다.
2. 가능하면 기존 메시지 순서는 유지하고 맨 뒤에 추가한다.
3. `GenPackets.bat`를 실행한다.
4. `GameServer`에 새 `Handle_C_*` 함수 구현을 추가한다.
5. `DummyClient` 또는 Unity client 쪽 수신 handler를 갱신한다.
6. Visual Studio에서 `Server.sln`을 빌드한다.

## 현재 불일치 메모

- `GameServer\Protocol.proto`에는 `S_SPAWN`, `S_DESPAWN`, `C_MOVE`, `S_MOVE`가 없지만 `GameServer\Protocol.pb.h`에는 존재한다.
- `GameServer\Struct.proto`에는 `Player`만 있으나, 생성된 코드와 `Common` 원본은 `ObjectInfo`, `PosInfo`를 사용한다.
- 작업자가 proto를 수정할 때는 `GameServer` 폴더의 proto를 기준으로 삼지 말고 `Common` 쪽 원본을 먼저 확인해야 한다.

