# produce_agent 작업 Phase

## Phase 4 — 생산 라인 실시간 처리 (model_agent의 Sample/Order API, data_agent의 저장소 API 확정 후 시작)

0. **생산 큐 등록 API** (PDF 16페이지: 승인 시 재고 부족이면 "생산 라인에 자동으로 등록")
   - controller_agent가 승인 처리 중 Model 판정이 `PRODUCING`일 때 호출할 "주문을 생산 큐에 등록" API 제공
1. FIFO 생산 대기열 구조 구현 (주문이 들어온 시료만, 한 번에 하나씩 생산)
2. 실 생산량 = `ceil(부족분 / 수율)`, 총 생산 시간 = `avgProductionTime * 실 생산량` 계산
3. **실시간(wall-clock) 진행 로직**
   - 생산 시작 시 현재 시각(시스템 시각)을 기록
   - 예상 완료 시각 = 시작 시각 + 총 생산 시간
   - 시작/예상 완료 시각을 data_agent의 저장소 API를 통해 영속화 (프로그램 종료 후에도 유지)
4. **앱 재시작 시 복구 로직**
   - data_agent로부터 진행 중이던 생산 항목(시작/예상 완료 시각)을 로드
   - 현재 시각과 비교해 이미 완료 시각이 지났다면 즉시 완료 처리(생산 완료 API 호출)
   - 아직 진행 중이면 남은 시간을 계산해 대기열 상태에 반영
5. 생산 완료 시 model_agent의 API를 호출해 주문 상태 `PRODUCING → CONFIRMED` 전환 및 Sample 재고 반영

## 데이터 스키마 확장 (data_agent와 조율)
생산 진행 상태를 영속화하기 위해 order 또는 별도 production 데이터에 아래 필드 추가 필요:
- `productionStartAt`: 생산 시작 시각 (ISO 8601 등)
- `productionEndAt`: 예상 완료 시각
- (선택) `actualQuantity`: 실 생산량

## 산출물
- `produce/ProductionLine.h` (+ .cpp) — Phase 0에서 확정된 위치
- controller_agent가 "생산 라인 현황 조회" 메뉴에서 호출할 수 있는 API 시그니처 확정
