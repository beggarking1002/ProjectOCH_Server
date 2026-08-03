# Packet Protocol and Generation

Updated: 2026-07-20.

## Source and Generated Files

Edit protobuf source files only in:

```text
Common\protoc-21.12-win64\bin\Enum.proto
Common\protoc-21.12-win64\bin\Struct.proto
Common\protoc-21.12-win64\bin\Protocol.proto
```

`GenPackets.bat`, which is also a GameServer pre-build step, generates C++ protobuf code, Unity C# protobuf code, and packet-routing helpers. Do not hand-edit generated files in `GameServer` or the Unity generated-packet folder.

## Packet Wire Format

Every packet is:

```text
uint16 size
uint16 id
protobuf payload
```

`PacketSession::OnRecv()` reads one complete header/payload unit and forwards it to the generated packet dispatcher.

## Packet IDs

Packet IDs are assigned in `Protocol.proto` declaration order. Existing messages must retain their position; append new messages to preserve deployed IDs.

| ID | Message |
| --- | --- |
| 1000-1011 | Login, enter/leave game, spawn/despawn, field move, chat |
| 1012 | `C_ENTER_BATTLE` |
| 1013 | `S_ENTER_BATTLE` |
| 1014 | `C_BATTLE_MOVE` |
| 1015 | `S_BATTLE_MOVE` |
| 1016 | `C_BATTLE_SKILL` |
| 1017 | `S_BATTLE_SKILL` |
| 1018 | `C_BATTLE_END_TURN` |
| 1019 | `S_BATTLE_END_TURN` |
| 1020 | `C_BATTLE_INVITE` |
| 1021 | `S_BATTLE_INVITE_REQUEST` |
| 1022 | `S_BATTLE_INVITE_RECEIVED` |
| 1023 | `C_BATTLE_INVITE_RESPONSE` |
| 1024 | `S_BATTLE_INVITE_RESULT` |
| 1025 | `S_BATTLE_PAWN_DEAD` |
| 1026 | `S_BATTLE_RESULT` |
| 1027 | `C_BATTLE_RESULT_ACK` |
| 1028 | `S_BATTLE_RESULT_ACK` |

## Battle Contract

All battle positions in protocol messages are pure axial coordinates, using `AxialCoord(q, r)`. Unity Point Top / Odd-R cells must be converted only at the Unity map boundary; see [[06_Battle_System_v0_1]].

`S_ENTER_BATTLE` provides complete initial state, including pawn information and all `BattleTileInfo` entries. Action responses are authoritative and include the state necessary for incremental synchronization:

- `pawn_deltas`: action-use and movement flags, HP, armor/barrier values, facing, resources, statuses, auras, and death state.
- `tile_deltas`: changed `base_tile_type` and/or `overlay_type`.
- action logs and current/next turn identifiers.

The client must process every delta in a response, including deltas for pawns other than the caster or directly selected target.

## Generation Workflow

1. Change `.proto` source in `Common\protoc-21.12-win64\bin`.
2. Build `GameServer`, which runs the pre-build generation step.
3. Implement or update the corresponding `Handle_C_*` path in `GameServer`.
4. Confirm generated Unity C# files are copied to `C:\ProjectOCH\Client\Assets\Scripts\Packet\Generated`.
5. Update Unity packet routing and gameplay state handling.
6. Build and test both server and client against the generated contract.

## Verification

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Server.sln /t:GameServer /p:Configuration=Debug /p:Platform=x64 /m:1
```

Generated C++ output is build output. The protobuf source files remain the source of truth.
