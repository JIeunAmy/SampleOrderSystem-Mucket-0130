# data_agent 작업 Phase

## Phase 2 — JSON 영속성 계층 (model_agent의 구조체 정의 확정 후 시작)

1. Sample/Order 목록을 JSON으로 직렬화/역직렬화하는 함수 구현
   - Model이 정의한 구조체를 그대로 사용, 도메인 로직(상태 전이 규칙, 계산식)은 재구현하지 않음
2. 저장 파일 경로/스키마 정의 (예: `samples.json`, `orders.json`)

### JSON 스키마 예시

**order data (`orders.json`)**
```json
{
  "orderId": "ORD-0001",
  "sampleName": "실리콘 웨이퍼-8인치",
  "quantity": 50,
  "status": "RESERVED"
}
```
- `orderId`: 주문 고유 ID
- `sampleName`: 주문한 시료 이름 (Sample 조회 시 참조용)
- `quantity`: 주문 수량
- `status`: `RESERVED / REJECTED / PRODUCING / CONFIRMED / RELEASE` 중 하나

**sample data (`samples.json`)**
```json
{
  "sampleId": "S-001",
  "name": "실리콘 웨이퍼-8인치",
  "avgProductionTime": 30,
  "yieldRate": 0.95,
  "stock": 20
}
```
- `sampleId`: 시료 고유 ID
- `name`: 시료명
- `avgProductionTime`: 평균 생산시간 (분 단위)
- `yieldRate`: 수율 (0~1 사이 값)
- `stock`: 현재 재고 수량

**production 진행 상태 (`produce_agent`가 사용, 프로그램 종료 후에도 실시간 경과를 복원하기 위한 필드)**
```json
{
  "orderId": "ORD-0001",
  "productionStartAt": "2026-07-15T09:00:00",
  "productionEndAt": "2026-07-15T10:30:00",
  "actualQuantity": 3
}
```
- `productionStartAt` / `productionEndAt`: 생산 시작/예상 완료 시각 (실제 현실 시간 기준, ISO 8601)
- 앱 재시작 시 이 값을 로드해 현재 시각과 비교, 이미 지났다면 즉시 생산 완료 처리하는 것은 `produce_agent`의 책임

3. 앱 시작 시 로드 → Model 저장소에 주입하는 함수 제공
   - 저장 "호출 시점"은 controller_agent가 결정하고, data_agent는 저장/로드 함수(API)만 제공
4. 파일 손상/누락 시 fallback 처리 (빈 상태로 시작)

## 산출물
- `data/Repository.h` (+ .cpp): 저장/로드 API
- controller_agent가 호출할 수 있는 명확한 함수 시그니처 (예: `SaveSamples`, `LoadSamples`, `SaveOrders`, `LoadOrders`)
