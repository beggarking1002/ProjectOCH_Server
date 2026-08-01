# ProjectOCH Server - Start Here

Updated: 2026-07-29.

## Workspace

- Server root: `C:\ProjectOCH\Server`
- Solution: `Server.sln`
- Obsidian vault: `ProjectOCH_Server`
- Unity client root: `C:\ProjectOCH\Client`
- Source protobuf files: `Common\protoc-21.12-win64\bin`

## Current Snapshot

The project is a Windows C++20 IOCP game server. `ServerCore` owns networking and job queues. `GameServer` owns field rooms, PvP battle rooms, server-authoritative battle validation, and battle data loading. The executable starts on `127.0.0.1:7777` and runs five worker threads.

The battle implementation is centered on a server-authoritative, axial-coordinate tactical battle model. `BEIGE_ICE` and `BEIGE_FIRE` are fully data/effect-driven; the active PvP roster also includes CSV/effect-pipeline support for `SUEN_AXE`, `ZILLIAN_LONGBOW`, and `ALEN_SPEAR`.

Read these first:

1. [[12_Project_Structure_2026-07-29]]
2. [[09_Implementation_Status_2026-07-20]]
3. [[06_Battle_System_v0_1]]
4. [[08_Battle_Data_Tables]]
5. [[07_Battle_Invite_Flow]]
6. [[10_Battle_Architecture_Rules]]
7. [[11_Zillian_Longbow_Implementation]]
8. [[13_Alen_Shield_Implementation]]
9. [[04_Packet_Protocol_Generation]]

## Build Verification

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Server.sln /t:GameServer /p:Configuration=Debug /p:Platform=x64 /m:1
```

The GameServer pre-build step regenerates protobuf and packet helper output, including the Unity generated C# packet files.

## Source of Truth

- Protocol: `Common\protoc-21.12-win64\bin\Enum.proto`, `Struct.proto`, `Protocol.proto`
- Runtime battle logic: `GameServer\BattleRoom.*`, `BattleEffectExecutor.*`, `BattlePawn.h`
- Data loader: `GameServer\BattleTemplateManager.*`
- Runtime data: `Data\ClassKey.csv`, `PawnTemplate.csv`, `BattleSkill.csv`, `BattleSkillEffect.csv`, `BattleSkillEffectParam.csv`, `BattleZoc.csv`, `BattleConfig.csv`, `Maps\BattleField_001.walkmap.json`

## Documentation Rule

When battle behavior changes, update [[06_Battle_System_v0_1]], [[08_Battle_Data_Tables]], and the newest implementation-status note. Apply [[10_Battle_Architecture_Rules]] before placing new logic. Update [[04_Packet_Protocol_Generation]] when protobuf contracts or generated packet flow changes.
