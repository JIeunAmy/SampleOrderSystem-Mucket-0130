#pragma once

// data_agent 담당 영역: 시료/주문/생산 진행 상태의 JSON 파일 영속성 계층.
//
// 이 헤더는 Phase 0(골격) 단계 산출물이다. 실제 구현은 Phase 3에서 진행하며,
// 그때 이 파일에 아래 내용을 채워 넣을 예정이다.
//
// TODO(Phase 3):
// - Sample <-> JSON 직렬화/역직렬화
//     저장 필드: 시료 ID, 이름, 평균 생산시간, 수율(정상 생산 수 / 총 생산 수), 현재 재고 수량
// - Order <-> JSON 직렬화/역직렬화
//     저장 필드: 주문번호, 시료 ID, 고객명, 주문 수량, 상태(RESERVED/REJECTED/PRODUCING/CONFIRMED/RELEASE),
//     생성 시각, 갱신 시각
// - 생산 진행 상태(produce_agent 연동) 영속화
//     저장 필드: 주문 ID, 생산 시작 시각(productionStartAt), 예상 완료 시각(productionEndAt)
//     -> 프로그램 재실행 후에도 실제 경과 시간을 복원해 wall-clock 기준 완료 판정이 가능해야 함
// - Model 계층이 사용할 저장소 접근 API 초안
//     예: LoadSamples() / SaveSamples(const 컨테이너&)
//         LoadOrders()  / SaveOrders(const 컨테이너&)
//         SaveOrder(단일 주문)  // 특정 주문 하나만 갱신 저장
//         LoadProductionState() / SaveProductionState(...)
// - JSON 파일 경로 상수 정의 (예: samples.json, orders.json, production_state.json 등)
// - 필요 시 더미 데이터 생성/삽입 유틸리티 (예: SeedDummySamples())
//
// 주의:
// - 상태 전이 규칙, 실 생산량/총 생산 시간 계산, wall-clock 완료 판정 등 도메인 로직은
//   여기서 구현하지 않는다 (각각 model_agent, produce_agent 책임).
// - 콘솔 입출력/메뉴 처리는 여기서 다루지 않는다 (view_agent/controller_agent 책임).
// - JSON 파싱/직렬화 라이브러리 도입 여부는 오케스트레이터와 상의 후 결정한다.
