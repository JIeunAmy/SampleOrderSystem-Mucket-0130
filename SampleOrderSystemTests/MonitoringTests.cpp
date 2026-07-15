// Phase 2 Red 단계: model_agent가 아직 구현하지 않은 모니터링 집계 API(재고 상태 판정 +
// 상태별 주문 수 집계)를 TDD_Agent가 먼저 설계하고, 이를 검증하는 실패하는 테스트를 작성한다.
//
// 설계한 인터페이스 (model/MonitoringService.h, model_agent가 구현해야 함):
//
//   enum class StockStatus { SURPLUS, SHORTAGE, DEPLETED }; // 여유 / 부족 / 고갈
//
//   // 재고 상태 판정: stock(재고 수량), demand(해당 시료에 대한 미출고 주문 수요 합계)를 받아 3단계로 분류
//   //  - 고갈(DEPLETED): stock == 0 (demand 값과 무관하게 항상 고갈)
//   //  - 부족(SHORTAGE): 0 < stock < demand
//   //  - 여유(SURPLUS):  stock >= demand (demand == 0인 경우도 포함 — 수요가 없으면 항상 여유로 처리)
//   StockStatus JudgeStockStatus(int stock, int demand);
//
//   // 미출고 주문 수요 합계: 특정 sampleId에 대해 REJECTED/RELEASE를 제외한 상태(RESERVED/PRODUCING/
//   // CONFIRMED)의 주문 수량(Quantity())을 모두 합산한다.
//   // (RELEASE는 이미 출고되어 더 이상 재고 수요가 아니므로 제외, REJECTED는 애초에 유효한 주문이 아니므로 제외)
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

TEST_CASE("JudgeStockStatus: 0 < 재고 < 수요이면 부족(SHORTAGE)이다", "[Monitoring][StockStatus][shortage]")
{
    REQUIRE(JudgeStockStatus(5, 10) == StockStatus::SHORTAGE);
    REQUIRE(JudgeStockStatus(1, 2) == StockStatus::SHORTAGE);
}

TEST_CASE("JudgeStockStatus: 재고 >= 수요이면 여유(SURPLUS)이다(경계값 포함)", "[Monitoring][StockStatus][surplus]")
{
    SECTION("재고가 수요보다 많음")
    {
        REQUIRE(JudgeStockStatus(20, 10) == StockStatus::SURPLUS);
    }

    SECTION("재고와 수요가 정확히 같음(경계값)")
    {
        REQUIRE(JudgeStockStatus(10, 10) == StockStatus::SURPLUS);
    }

    SECTION("수요가 0이고 재고가 0보다 크면 항상 여유")
    {
        REQUIRE(JudgeStockStatus(5, 0) == StockStatus::SURPLUS);
    }
}

TEST_CASE("SumUndeliveredDemand: 특정 시료에 대한 미출고 주문 수량만 합산한다", "[Monitoring][demand]")
{
    Sample sample("S-001", "TestSample", 10, 0.9);
    sample.IncreaseStock(100);

    Sample other("S-002", "OtherSample", 5, 0.8);
    other.IncreaseStock(100);

    std::vector<Order> orders;

    // S-001에 대한 RESERVED 주문 (수요에 포함되어야 함)
    orders.emplace_back("O-001", "Alice", "S-001", 3);

    // S-001에 대한 PRODUCING 주문 (수요에 포함되어야 함)
    Order producingOrder("O-002", "Bob", "S-001", 4);
    producingOrder.Approve(sample); // 재고 100 >= 4 이므로 실제로는 CONFIRMED가 되지만,
                                     // 아래 별도 케이스에서 재고 0 상태로 PRODUCING을 유도한다.
    orders.push_back(producingOrder);

    // 재고 부족 상황을 만들어 PRODUCING 상태의 주문을 별도로 준비
    Sample emptySample("S-003", "EmptySample", 10, 0.9);
    Order producingViaShortage("O-003", "Carol", "S-001", 7);
    producingViaShortage.Approve(emptySample); // emptySample.Stock() == 0 < 7 -> PRODUCING
    orders.push_back(producingViaShortage);

    // S-001에 대한 REJECTED 주문 (수요에서 제외되어야 함)
    Order rejectedOrder("O-004", "Dave", "S-001", 100);
    rejectedOrder.Reject();
    orders.push_back(rejectedOrder);

    // S-001에 대한 RELEASE 주문 (수요에서 제외되어야 함)
    Order releasedOrder("O-005", "Eve", "S-001", 50);
    releasedOrder.Approve(sample); // 재고 충분 -> CONFIRMED
    releasedOrder.Release();
    orders.push_back(releasedOrder);

    // 다른 시료(S-002)에 대한 주문 (S-001 수요 합산에서 제외되어야 함)
    Order otherSampleOrder("O-006", "Frank", "S-002", 999);
    orders.push_back(otherSampleOrder);

    // S-001 미출고 수요 = O-001(3, RESERVED) + O-002(4, CONFIRMED) + O-003(7, PRODUCING) = 14
    REQUIRE(SumUndeliveredDemand(orders, "S-001") == 14);
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
    confirmedOrder.Approve(sample);
    orders.push_back(confirmedOrder);

    // PRODUCING 1건 (재고 부족 -> Approve 시 PRODUCING)
    Order producingOrder("O-004", "Dave", "S-002", 1);
    producingOrder.Approve(emptySample);
    orders.push_back(producingOrder);

    // RELEASE 1건
    Order releasedOrder("O-005", "Eve", "S-001", 1);
    releasedOrder.Approve(sample);
    releasedOrder.Release();
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
