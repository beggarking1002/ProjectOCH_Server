# ProjectOCH Server - Start Here

Updated: 2026-08-17.

## Workspace

- Server root: `C:\ProjectOCH\Server`
- Solution: `Server.sln`
- Obsidian vault: `ProjectOCH_Server`
- Unity client root: `C:\ProjectOCH\Client`
- Source protobuf files: `Common\protoc-21.12-win64\bin`

## Current Snapshot

The project is a Windows C++20 IOCP game server. `ServerCore` owns networking and job queues. `GameServer` owns Google-authenticated accounts, MySQL persistence, field rooms, server-authoritative movement and villages, player-scoped economy and quests, PvP battle rooms, battle validation, and battle data loading. The executable starts on `127.0.0.1:7777` and runs five worker threads.

The current playable loop covers login, persistent expedition state, path-based field travel, village shops, food and trade inventory, generated quests, class selection, and server-authoritative tactical PvP. Battle entry applies satiety/fame penalties, and PvP victory atomically transfers the loser's trade goods to the winner.

Read these first:

1. [[15_Current_Status_2026-08-17]]
2. [[12_Project_Structure_2026-07-29]]
3. [[10_Battle_Architecture_Rules]]
4. [[06_Battle_System_v0_1]]
5. [[08_Battle_Data_Tables]]
6. [[07_Battle_Invite_Flow]]
7. [[13_Alen_Shield_Implementation]]
8. [[14_Zillian_Mace_Implementation]]
9. [[04_Packet_Protocol_Generation]]

## Build Verification

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Server.sln /t:GameServer /p:Configuration=Debug /p:Platform=x64 /m:1
```

The GameServer pre-build step regenerates protobuf and packet helper output, including the Unity generated C# packet files.

## Source of Truth

- Protocol: `Common\protoc-21.12-win64\bin\Enum.proto`, `Struct.proto`, `Protocol.proto`
- Runtime battle logic: `GameServer\BattleRoom.*`, `BattleEffectExecutor.*`, `BattlePawn.h`
- Field movement and villages: `GameServer\Room.*`, `FieldWalkMapData.*`, `Data\Maps\Field_001.walkmap.json`
- Accounts and persistence: `GameServer\ServerPacketHandler.cpp`, `GameSessionManager.*`, `DatabaseManager.*`, `Database\Migrations\*.sql`
- Economy and quests: `GameServer\EconomyService.*`, `QuestService.*`, `Data\EconomyConfig.csv`, `Data\Quest*.csv`
- Data loader: `GameServer\BattleTemplateManager.*`
- Runtime data: `Data\ClassKey.csv`, `PawnTemplate.csv`, `BattleSkill.csv`, `BattleSkillEffect.csv`, `BattleSkillEffectParam.csv`, `BattleZoc.csv`, `BattleConfig.csv`, `Maps\BattleField_001.walkmap.json`

## Documentation Rule

When behavior changes, update [[15_Current_Status_2026-08-17]] or create the next dated status note. Update [[06_Battle_System_v0_1]] and [[08_Battle_Data_Tables]] for reusable battle rules and data schema changes. Apply [[10_Battle_Architecture_Rules]] before placing new logic. Update [[04_Packet_Protocol_Generation]] when protobuf contracts or generated packet flow changes. Never place OAuth secrets or database passwords in the vault.
