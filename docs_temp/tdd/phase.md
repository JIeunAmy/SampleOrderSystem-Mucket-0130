# TDD_Agent 작업 Phase

## Phase 1 — TDD 대상 식별 (각 계층 phase 문서 작성 직후, 실제 구현 착수 전)

`docs_temp/model/phase.md`, `docs_temp/produce/phase.md`, `docs_temp/data/phase.md`,
`docs_temp/controller/phase.md`, `docs_temp/view/phase.md`를 검토해 TDD 대상 클래스를 아래와 같이 식별했다.

### TDD 필요 (우선순위 높음) — 입력에 따라 결과가 갈리는 순수 로직

| 클래스/모듈 | 계층 | 테스트해야 할 핵심 시나리오 |
|---|---|---|
| `Sample` | model | 수율 0~1 범위 검증(경계값 0, 1, 범위 밖 -0.1/1.1 거부), 등록 시 재고 0 시작, 재고 증감 시 음수 방지 |
| `Order` | model | 상태 전이 규칙 전부(RESERVED→CONFIRMED/PRODUCING, RESERVED→REJECTED, PRODUCING→CONFIRMED, CONFIRMED→RELEASE), 허용되지 않는 전이 시도(REJECTED에서 재승인 등) 거부 |
| `ProductionLine`(FIFO 스케줄러) | produce | 실 생산량 `ceil(부족분/수율)` 경계값(나누어떨어짐/안 떨어짐), 총 생산 시간 계산, FIFO 순서 보장(먼저 들어온 주문 먼저 생산) |
| 생산 실시간(wall-clock) 판정 로직 | produce | 생산 시작 후 즉시 조회 시 미완료, 총 생산 시간 경과 후 조회 시 완료 처리, **앱 재시작 후에도 실제 경과 시간 기준으로 완료 판정**(시작 시각만 저장해두고 재시작 시점의 현재 시각과 비교하는 케이스) |
| `Repository` 직렬화/역직렬화 | data | 정상 JSON 왕복(round-trip) 일치, 파일 손상/누락 시 fallback(빈 상태 시작), 필드 누락 시 처리 |
| 재고 상태 판정(여유/부족/고갈) | model | 재고=0→고갈, 0<재고<수요→부족, 재고>=수요 경계값→여유, 수요 0일 때 처리 |

### TDD 낮음/불필요

| 클래스/모듈 | 계층 | 사유 |
|---|---|---|
| `MenuController` 분기 흐름 | controller | 단순 위임/분기이며 Model/Produce API가 이미 TDD로 검증됨. 통합 테스트나 수동 확인으로 충분 |
| `ConsoleView` | view | 콘솔 출력 문자열은 눈으로 확인하는 것이 더 빠르고 회귀 위험이 낮음 |
| 시료 주문 입력 검증(수량 > 0 등) | controller | 단순 값 검증이나, 로직이 복잡해지면(예: 다중 조건) 재평가 |

## 작업 순서
1. 테스트 프레임워크 선정 (Catch2 또는 Google Test) — orchestrator와 협의 후 `.vcxproj`/테스트 전용 프로젝트에 통합
2. 위 "TDD 필요" 표의 클래스 순서대로 실패하는 테스트 작성 → 각 구현 에이전트(model_agent/produce_agent/data_agent)가 통과시키는 구현 진행
3. phase 문서가 갱신되어 새 클래스/계산식이 추가되면 이 표도 함께 갱신

## 산출물
- 테스트 프로젝트/파일 (예: `SampleOrderSystemTests/`)
- 위 표를 근거로 한 클래스별 테스트 케이스 목록
