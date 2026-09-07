# Project OCH Server

Unity 클라이언트와 C++20 IOCP 서버로 구현한 **서버 권위형 온라인 전술 RPG 프로토타입**입니다. 필드 탐색, 마을 경제, 퀘스트, 생존 자원과 두 플레이어가 Pawn 4개씩 조작하는 4대4 육각 타일 전투를 하나의 영속 계정 흐름으로 연결했습니다.

- Client repository: [ProjectOCH_Client](https://github.com/beggarking1002/ProjectOCH_Client)

## 주요 기능

- 필드 이동, 마을 상호작용과 전투 판정을 서버에서 검증
- 전투 초대, 캐릭터별 클래스 선택과 플레이어당 Pawn 4개의 PvP
- 육각 좌표 기반 이동, ZOC, 반격, 밀치기와 상태 효과
- CSV/JSON 기반 스킬, 아이템, 마을과 퀘스트 데이터
- Google OAuth 2.0 로그인과 MySQL 계정 영속화
- 플레이어별 상점 재고, 인벤토리, 퀘스트, 생존 자원과 전적 저장

## 구조

```text
Unity Client
    ↓ TCP / Protocol Buffers
IOCP · GameSession · PacketHandler
    ↓
Field Room / BattleRoom (각각의 JobQueue)
    ↓
CSV·JSON 정적 데이터 + MySQL 계정 데이터
```

`Room`은 필드 상태를, `BattleRoom`은 전투 상태를 각각의 `JobQueue`에서 순차 처리합니다. 클라이언트는 행동 의도만 전송하고, 서버가 유효성 검사와 결과 판정을 수행한 뒤 최종 상태와 `BattleActionLog`를 양쪽 클라이언트에 전달합니다.

## 기술 스택

- Windows, C++20, IOCP, TCP
- Protocol Buffers
- MySQL 8, SQL migrations
- CSV, JSON
- Visual Studio 2022

## 로컬 실행

1. MySQL 8에 로컬 데이터베이스와 서버 계정을 생성합니다.
2. `Data/Database.json.example`을 `Data/Database.json`으로 복사해 접속 정보를 입력합니다.
3. `Data/GoogleAuth.json.example`을 `Data/GoogleAuth.json`으로 복사해 Google Desktop OAuth의 `client_id`와 `client_secret`을 입력합니다.
4. `Server.sln`을 Visual Studio 2022에서 열고 `GameServer`를 시작 프로젝트로 지정해 x64로 빌드·실행합니다.
5. [클라이언트 저장소](https://github.com/beggarking1002/ProjectOCH_Client)의 Unity 프로젝트를 실행합니다.

DB 생성, 마이그레이션과 개발용 로그인 방법은 [Database/README.md](Database/README.md)를 참고하세요. 클라이언트에는 같은 Google Desktop OAuth 앱의 `client_id`를 사용해야 합니다. 실제 인증 정보가 들어가는 `Data/Database.json`과 `Data/GoogleAuth.json`은 Git에서 제외됩니다.

서버는 기본적으로 `127.0.0.1:7777`에서 실행됩니다. 서버와 클라이언트의 Protocol Buffers 정의 및 생성 코드는 서로 일치해야 합니다.

## 주요 디렉터리

```text
GameServer/   게임 규칙, 필드, 전투, 경제와 DB 연동
ServerCore/   IOCP, Session, JobQueue와 네트워크 기반 코드
Common/       Protocol Buffers 정의와 생성 코드
Data/         게임 정적 데이터와 맵 JSON
Database/     MySQL 마이그레이션 및 설정 문서
Tools/        패킷 코드 생성 도구
```

## 현재 범위

서로 다른 Google 계정의 로컬 클라이언트 2개로 로그인, 필드 동기화, PvP와 전투 종료 후 필드 복귀를 수동 검증했습니다. 전투 도중 재접속 복구, 외부 네트워크 환경과 다수 동시 접속 부하 테스트는 후속 범위입니다.

