# Project OCH Portfolio

Project OCH의 포트폴리오 제출용 문서를 모아 둔 폴더입니다. 문서의 구현 범위는 `GameServer`, `ServerCore`, `Common` 프로토콜과 `Data/*.csv`를 기준으로 작성합니다. 마지막 코드 대조일은 2026-07-29입니다.

## Documents

| File | Audience | Purpose |
| --- | --- | --- |
| `01_Planning_Portfolio_Draft.md` | Game planner | 전투 문제 정의, 핵심 규칙, 캐릭터 사례, 발표 구성 |
| `02_Development_Technical_Portfolio_Draft.md` | Game/server developer | 서버 구조, 데이터 파이프라인, 프로토콜, 구현 범위와 한계 |

## Recommended Submission Package

1. 지원 직군에 맞는 문서 한 개를 PDF로 편집한다.
2. 60~120초 분량의 전투 영상으로 이동, ZOC, 스킬, 반응 로그를 보여 준다.
3. 기술 지원에는 전투 서버 구조 다이어그램과 핵심 코드 링크를 1장 추가한다.
4. 미완성 기능은 완료된 것처럼 표현하지 않고, 다음 검증 항목으로 구분한다.

## Current Evidence

- 마지막 문서화된 `GameServer` Debug build: warning 0, error 0 (2026-07-20 구현 현황 기준)
- 서버 권한 전투, CSV 효과 실행, IOCP/JobQueue, Protocol Buffers 생성 파이프라인
- 8개 Pawn 템플릿, 45개 전투 스킬 정의, 101개 효과 행, 306개 효과 파라미터 행
- `BeigeIce`, `BeigeFire`, `SuenAxe`, `AlenSpear`, `ZillianLongbow` 전용 `BattlePawn` 구현
- PvP 초대/수락, shared battle state, 결과 확인 후 필드 복귀 프로토콜
