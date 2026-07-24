# ProjectOCH Server - Start Here

Updated: 2026-07-25.

## Workspace

- Server root: `C:\ProjectOCH\Server`
- Solution: `Server.sln`
- Obsidian vault: `ProjectOCH_Server`
- Unity client root: `C:\ProjectOCH\Client`
- Source protobuf files: `Common\protoc-21.12-win64\bin`

## Current Snapshot

The project is a Windows C++ IOCP game server. `ServerCore` owns networking and job queues. `GameServer` owns field rooms, PvP battle rooms, server-authoritative battle validation, and battle data loading.

The battle implementation is currently centered on `BEIGE_ICE`. Its passive, four normal skills, ultimate, and sub action are data-driven through CSV effect groups. Other pawn classes still have legacy fallback skill specs.

Read these first:

1. [[09_Implementation_Status_2026-07-20]]
2. [[06_Battle_System_v0_1]]
3. [[08_Battle_Data_Tables]]
4. [[07_Battle_Invite_Flow]]
5. [[10_Battle_Architecture_Rules]]
6. [[04_Packet_Protocol_Generation]]

## Build Verification

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Server.sln /t:GameServer /p:Configuration=Debug /p:Platform=x64 /m:1
```

The GameServer pre-build step regenerates protobuf and packet helper output, including the Unity generated C# packet files.

## Source of Truth

- Protocol: `Common\protoc-21.12-win64\bin\Enum.proto`, `Struct.proto`, `Protocol.proto`
- Runtime battle logic: `GameServer\BattleRoom.*`, `BattleEffectExecutor.*`, `BattlePawn.h`
- Data loader: `GameServer\BattleTemplateManager.*`
- Runtime data: `Data\ClassKey.csv`, `PawnTemplate.csv`, `BattleSkill.csv`, `BattleSkillEffect.csv`, `BattleSkillEffectParam.csv`, `BattleMapTile.csv`

## Documentation Rule

When battle behavior changes, update [[06_Battle_System_v0_1]], [[08_Battle_Data_Tables]], and the newest implementation-status note. Apply [[10_Battle_Architecture_Rules]] before placing new logic. Update [[04_Packet_Protocol_Generation]] when protobuf contracts or generated packet flow changes.
