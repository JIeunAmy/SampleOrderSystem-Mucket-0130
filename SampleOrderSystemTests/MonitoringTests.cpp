// Phase 2 Red 단계: model_agent가 아직 구현하지 않은 모니터링 집계 API(재고 상태 판정 +
// 상태별 주문 수 집계)를 TDD_Agent가 먼저 설계하고, 이를 검증하는 실패하는 테스트를 작성한다.
//
// 설계한 인터페이스 (model/MonitoringService.h, model_agent가 구현해야 함):
//
//   enum class StockStatus { SURPLUS, SHORTAGE, DEPLETED }; // 여유 / 부족 / 고갈
//
//   // 재고 상태 판정: stock(재고 수량), demand(해당 시료에 대한 미출고 주문 수요 합계)를 받아 3단계로 분류
//   // 2026-07-15 정책 변경(Red): 재고 == 수요 경계값을 SURPLUS에서 DEPLETED로 이동시킴
//   //  (현재 확정된 수요를 모두 충족하고 나면 재고가 정확히 0이 되어 "곧 고갈될" 상태이므로).
//   //  - 고갈(DEPLETED): stock == 0 이거나 stock == demand (demand != 0 포함, 즉 stock <= demand 이면서 stock > 0인 경계 포함)
//   //  - 부족(SHORTAGE): 0 < stock < demand
//   //  - 여유(SURPLUS):  stock > demand (즉 재고가 수요를 "초과"하는 경우만; demand == 0이고 stock > 0인 경우도 포함)
//   StockStatus JudgeStockStatus(int stock, int demand);
//
//   // 미출고 주문 수요 합계: 특정 sampleId에 대해 확정된 수요만 합산한다. 2026-07-15 변경(Red):
//   // 사용자가 "모니터링 재고 상태 판정에 쓰이는 수요는 CONFIRMED/PRODUCING만 고려해야 하며, 아직
//   // 승인되지 않은 RESERVED는 확정된 수요가 아니므로 제외해야 한다"는 요구사항을 추가함에 따라
//   // PRODUCING/CONFIRMED 상태의 주문 수량(Quantity())만 합산하도록 변경한다.
//   // (RELEASE는 이미 출고되어 더 이상 재고 수요가 아니므로 제외, REJECTED는 애초에 유효한 주문이 아니므로
//   //  제외, RESERVED는 아직 승인되지 않아 확정된 수요가 아니므로 제외)
//   int SumUndeliveredDemand(const std::vector<Order>& orders, const std::string& sampleId);
//
//   // 상태별 주문 수 집계: REJECTED는 집계에서 제외한다.
//   struct OrderStatusCounts
//   {
//       int reserved = 0;
//       int confirmed = 0;
//       int producing = 0;
//       int release = 0;
//   };
//   OrderStatusCounts CountOrdersByStatus(const std::vector<Order>& orders);
//
// 2026-07-15 변경(Red, PLAN.md 옵션 B): 주문 승인 시점의 가용 재고(availableStock) 계산을 위해
// 아래 헬퍼를 새로 추가한다. 특정 sampleId에 대해 CONFIRMED(아직 미출고) 상태 주문들의 수량 합계만
// 구하는 순수 함수이며, Controller(MenuController::HandleApprovalOrRejection)가
// `sample.Stock() - SumConfirmedQuantity(allOrders, sampleId)`를 계산해 Order::Approve(int)에
// 넘길 값을 만드는 데 사용한다.
//
//   int SumConfirmedQuantity(const std::vector<Order>& orders, const std::string& sampleId);
//
// 위 시그니처는 model_agent와 조율 대상이며, 구현 시 변경이 필요하면 이 테스트를 함께 갱신한다.
// task 예시에서 조정한 부분:
//  - "수요 합계" 계산 헬퍼(SumUndeliveredDemand)를 별도 함수로 분리해, JudgeStockStatus는 순수하게
//    (stock, demand) 두 정수만 받는 단순 분류 함수로 유지했다. 이렇게 하면 JudgeStockStatus 자체의
//    경계값 테스트가 Order/vector와 무관하게 독립적으로 가능해진다.

#include <vector>
#include <string>

#include "catch_amalgamated.hpp"
#include "model/Order.h"
#include "model/Sample.h"
#include "model/MonitoringService.h"

TEST_CASE("JudgeStockStatus: 재고가 0이면 항상 고갈(DEPLETED)이다", "[Monitoring][StockStatus][depleted]")
{
    SECTION("수요가 0보다 클 때")
    {
        REQUIRE(JudgeStockStatus(0, 10) == StockStatus::DEPLETED);
    }

    SECTION("수요가 0일 때도 재고가 0이면 고갈")
    {
        REQUIRE(JudgeStockStatus(0, 0) == StockStatus::DEPLETED);
    }
}

// 2026-07-15 정책 변경(Red): 재고와 수요가 정확히 같으면 현재 확정된 수요를 모두 충족시키고 나면
// 재고가 정확히 0이 되어 "곧 고갈될" 상태이므로, 더 이상 여유(SURPLUS)가 아니라 고갈(DEPLETED)로 판정한다.
TEST_CASE("JudgeStockStatus: 재고와 수요가 정확히 같으면 고갈(DEPLETED)이다(경계값 변경)", "[Monitoring][StockStatus][depleted][boundary]")
{
    SECTION("재고와 수요가 정확히 같음(0이 아닌 경계값)")
    {
        REQUIRE(JudgeStockStatus(10, 10) == StockStatus::DEPLETED);
    }

    SECTION("재고와 수요가 모두 0이어도 고갈(기존 규칙과 동일하게 유지)")
    {
        REQUIRE(JudgeStockStatus(0, 0) == StockStatus::DEPLETED);
    }
}

TEST_CASE("JudgeStockStatus: 0 < 재고 < 수요이면 부족(SHORTAGE)이다", "[Monitoring][StockStatus][shortage]")
{
    REQUIRE(JudgeStockStatus(5, 10) == StockStatus::SHORTAGE);
    REQUIRE(JudgeStockStatus(1, 2) == StockStatus::SHORTAGE);
}

// 2026-07-15 정책 변경(Red): 재고 == 수요는 더 이상 여유가 아니므로, 여유(SURPLUS)는
// 재고가 수요를 "초과"하는 경우에만 성립한다.
TEST_CASE("JudgeStockStatus: 재고 > 수요(초과)이면 여유(SURPLUS)이다", "[Monitoring][StockStatus][surplus]")
{
    SECTION("재고가 수요보다 많음(초과)")
    {
        REQUIRE(JudgeStockStatus(20, 10) == StockStatus::SURPLUS);
    }

    SECTION("재고가 수요보다 근소하게 많음(경계값 바로 위)")
    {
        REQUIRE(JudgeStockStatus(11, 10) == StockStatus::SURPLUS);
    }

    SECTION("재고가 10, 수요가 7이면 여유(추가 시나리오)")
    {
        REQUIRE(JudgeStockStatus(10, 7) == StockStatus::SURPLUS);
    }

    SECTION("수요가 0이고 재고가 0보다 크면 항상 여유")
    {
        REQUIRE(JudgeStockStatus(5, 0) == StockStatus::SURPLUS);
    }
}

TEST_CASE("SumUndeliveredDemand: 특정 시료에 대해 CONFIRMED/PRODUCING 주문 수량만 합산하고 RESERVED는 제외한다", "[Monitoring][demand]")
{
    Sample sample("S-001", "TestSample", 10, 0.9);
    sample.IncreaseStock(100);

    Sample other("S-002", "OtherSample", 5, 0.8);
    other.IncreaseStock(100);

    std::vector<Order> orders;

    // S-001에 대한 RESERVED 주문 (2026-07-15 변경: 아직 승인되지 않아 확정된 수요가 아니므로 수요에서 제외되어야 함)
    orders.emplace_back("O-001", "Alice", "S-001", 3);

    // S-001에 대한 CONFIRMED 주문 (수요에 포함되어야 함)
    Order confirmedOrder("O-002", "Bob", "S-001", 4);
    confirmedOrder.Approve(sample.Stock()); // 재고 100 >= 4 이므로 CONFIRMED
    orders.push_back(confirmedOrder);

    // 재고 부족 상황을 만들어 PRODUCING 상태의 주문을 별도로 준비 (수요에 포함되어야 함)
    Sample emptySample("S-003", "EmptySample", 10, 0.9);
    Order producingViaShortage("O-003", "Carol", "S-001", 7);
    producingViaShortage.Approve(emptySample.Stock()); // emptySample.Stock() == 0 < 7 -> PRODUCING
    orders.push_back(producingViaShortage);

    // S-001에 대한 REJECTED 주문 (수요에서 제외되어야 함)
    Order rejectedOrder("O-004", "Dave", "S-001", 100);
    rejectedOrder.Reject();
    orders.push_back(rejectedOrder);

    // S-001에 대한 RELEASE 주문 (수요에서 제외되어야 함)
    Order releasedOrder("O-005", "Eve", "S-001", 50);
    releasedOrder.Approve(sample.Stock()); // 재고 충분 -> CONFIRMED
    releasedOrder.Release(sample);
    orders.push_back(releasedOrder);

    // 다른 시료(S-002)에 대한 주문 (S-001 수요 합산에서 제외되어야 함)
    Order otherSampleOrder("O-006", "Frank", "S-002", 999);
    orders.push_back(otherSampleOrder);

    // S-001 미출고 수요(RESERVED 제외) = O-002(4, CONFIRMED) + O-003(7, PRODUCING) = 11
    // (O-001의 RESERVED 수량 3은 더 이상 포함되지 않는다)
    REQUIRE(SumUndeliveredDemand(orders, "S-001") == 11);
}

TEST_CASE("SumUndeliveredDemand: RESERVED 상태 주문만 있으면 수요는 0이다", "[Monitoring][demand][reserved-excluded]")
{
    std::vector<Order> orders;
    orders.emplace_back("O-020", "Alice", "S-001", 5);
    orders.emplace_back("O-021", "Bob", "S-001", 8);

    REQUIRE(SumUndeliveredDemand(orders, "S-001") == 0);
}

TEST_CASE("SumUndeliveredDemand: 해당 시료에 대한 주문이 없으면 0을 반환한다", "[Monitoring][demand][edge]")
{
    std::vector<Order> orders;
    orders.emplace_back("O-010", "Alice", "S-999", 5);

    REQUIRE(SumUndeliveredDemand(orders, "S-001") == 0);
}

TEST_CASE("CountOrdersByStatus: REJECTED를 제외하고 상태별 주문 수를 집계한다", "[Monitoring][counts]")
{
    Sample sample("S-001", "TestSample", 10, 0.9);
    sample.IncreaseStock(100);

    Sample emptySample("S-002", "EmptySample", 10, 0.9);

    std::vector<Order> orders;

    // RESERVED 2건
    orders.emplace_back("O-001", "Alice", "S-001", 1);
    orders.emplace_back("O-002", "Bob", "S-001", 1);

    // CONFIRMED 1건 (재고 충분 -> Approve 시 바로 CONFIRMED)
    Order confirmedOrder("O-003", "Carol", "S-001", 1);
    confirmedOrder.Approve(sample.Stock());
    orders.push_back(confirmedOrder);

    // PRODUCING 1건 (재고 부족 -> Approve 시 PRODUCING)
    Order producingOrder("O-004", "Dave", "S-002", 1);
    producingOrder.Approve(emptySample.Stock());
    orders.push_back(producingOrder);

    // RELEASE 1건
    Order releasedOrder("O-005", "Eve", "S-001", 1);
    releasedOrder.Approve(sample.Stock());
    releasedOrder.Release(sample);
    orders.push_back(releasedOrder);

    // REJECTED 2건 (집계에서 제외되어야 함)
    Order rejectedOrder1("O-006", "Frank", "S-001", 1);
    rejectedOrder1.Reject();
    orders.push_back(rejectedOrder1);

    Order rejectedOrder2("O-007", "Grace", "S-001", 1);
    rejectedOrder2.Reject();
    orders.push_back(rejectedOrder2);

    OrderStatusCounts counts = CountOrdersByStatus(orders);

    REQUIRE(counts.reserved == 2);
    REQUIRE(counts.confirmed == 1);
    REQUIRE(counts.producing == 1);
    REQUIRE(counts.release == 1);
}

TEST_CASE("CountOrdersByStatus: 빈 목록이면 모든 카운트가 0이다", "[Monitoring][counts][edge]")
{
    std::vector<Order> orders;

    OrderStatusCounts counts = CountOrdersByStatus(orders);

    REQUIRE(counts.reserved == 0);
    REQUIRE(counts.confirmed == 0);
    REQUIRE(counts.producing == 0);
    REQUIRE(counts.release == 0);
}

// 2026-07-15 추가(Red, PLAN.md 옵션 B): SumConfirmedQuantity - 같은 시료에 대해 CONFIRMED 주문이
// 2건 이상 겹쳐 있을 때 그 수량 합계가 정확히 계산되는지 검증한다(PLAN.md 예시 A의 확장 케이스).
TEST_CASE("SumConfirmedQuantity: 같은 시료의 CONFIRMED 주문 수량을 모두 합산한다", "[Monitoring][confirmed][PLAN]")
{
    Sample sample("S-001", "TestSample", 10, 0.9);
    sample.IncreaseStock(200);

    std::vector<Order> orders;

    // S-001에 대한 CONFIRMED 주문 2건 (수량 80, 30 -> 합계 110)
    Order confirmedA("O-101", "Alice", "S-001", 80);
    confirmedA.Approve(sample.Stock()); // 재고 200 >= 80 -> CONFIRMED
    orders.push_back(confirmedA);

    Order confirmedB("O-102", "Bob", "S-001", 30);
    confirmedB.Approve(sample.Stock()); // 재고 200 >= 30 -> CONFIRMED
    orders.push_back(confirmedB);

    // RESERVED 주문은 합산에서 제외되어야 함
    orders.emplace_back("O-103", "Carol", "S-001", 999);

    // 다른 시료의 CONFIRMED 주문은 합산에서 제외되어야 함
    Sample other("S-002", "OtherSample", 5, 0.8);
    other.IncreaseStock(50);
    Order confirmedOther("O-104", "Dave", "S-002", 40);
    confirmedOther.Approve(other.Stock());
    orders.push_back(confirmedOther);

    REQUIRE(SumConfirmedQuantity(orders, "S-001") == 110);
}

TEST_CASE("SumConfirmedQuantity: 해당 시료의 CONFIRMED 주문이 없으면 0을 반환한다", "[Monitoring][confirmed][edge]")
{
    std::vector<Order> orders;
    orders.emplace_back("O-110", "Alice", "S-001", 5); // RESERVED

    REQUIRE(SumConfirmedQuantity(orders, "S-001") == 0);
}
