# model_agent 작업 Phase

## Phase 2 — 도메인 모델 구현 (Phase 0 골격 확정 후 시작)

1. **Sample**
   - 필드: 시료 ID, 이름, 평균 생산시간, 수율, 재고 수량
   - 등록 시 재고 0으로 시작
   - 수율은 0~1 범위 검증 (Model 내부에서 강제)

2. **Order**
   - 상태 enum: `RESERVED / REJECTED / PRODUCING / CONFIRMED / RELEASE`
   - 상태 전이 메서드
     - 승인 → 재고 충분 시 `CONFIRMED`, 부족 시 `PRODUCING`
     - 거절 → `REJECTED`
     - 생산 완료 → `PRODUCING → CONFIRMED`
     - 출고 처리 → `CONFIRMED → RELEASE`

3. **생산 완료 반영 API** (ProductionLine의 FIFO 큐/실시간 스케줄링 자체는 `produce_agent` 책임)
   - produce_agent가 호출할 "생산 완료 처리" API 제공: 주문 상태 `PRODUCING → CONFIRMED` 전환 + Sample 재고 증가
   - 실 생산량/총 생산 시간 계산식(`ceil(부족분/수율)`, `평균생산시간 * 실생산량`) 자체는 produce_agent가 계산하므로
     model_agent는 이 계산 결과를 받아 반영하는 API만 제공하면 된다

4. **저장소 API**
   - Sample/Order에 대한 순수 인메모리 등록/조회/검색 API
   - data_agent가 이 API 위에 영속성 계층을, produce_agent가 생산 완료 반영 API를 각각 얹을 수 있도록
     구조체와 API를 먼저 확정

5. **검증**
   - 상태 전이 규칙 위반 방지 (예: REJECTED 상태 주문 재승인 불가)
   - 시료 등록 시 중복 시료 ID 거부

## 산출물
- `model/Sample.h`, `model/Order.h` (+ 대응 .cpp)
- data_agent, controller_agent, produce_agent가 참조할 수 있는 API 시그니처 확정 후 orchestrator에 공유
