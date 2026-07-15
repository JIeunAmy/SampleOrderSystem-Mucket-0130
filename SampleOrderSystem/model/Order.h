#pragma once

// TODO(Phase 2): Order 클래스 - 주문번호, 시료 ID, 고객명, 주문 수량, 상태
// 참고: CLAUDE.md "도메인 모델 및 상태 흐름 > 주문(Order) 상태"
// - 상태: RESERVED / REJECTED / PRODUCING / CONFIRMED / RELEASE
// - 상태 전이 메서드:
//   RESERVED --(승인, 재고 충분)--> CONFIRMED
//   RESERVED --(승인, 재고 부족)--> PRODUCING
//   RESERVED --(거절)--> REJECTED
//   PRODUCING --(생산 완료)--> CONFIRMED  (생산 완료 반영 API에서 호출, 실제 판정은 produce_agent 책임)
//   CONFIRMED --(출고 처리)--> RELEASE
// - 이 클래스는 상태 값을 보유하고 전이 규칙을 검증하는 역할만 하며,
//   FIFO 큐 스케줄링/생산 시간 계산/실시간 완료 판정 로직은 포함하지 않는다

class Order
{
    // TODO(Phase 2): 필드/메서드 구현 예정
};
