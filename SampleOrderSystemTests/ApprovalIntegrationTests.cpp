// Phase (Red 단계, PLAN.md 옵션 B): 주문 승인 시 가용 재고(availableStock) 계산 버그 수정 계획을
// 검증하는 통합 테스트. TDD_Agent가 model_agent/controller_agent의 구현 착수 전에 먼저 작성한다.
//
// 확정된 설계 (PLAN.md 3.1/3.2 옵션 B):
//   availableStock = sample.Stock() - SumConfirmedQuantity(allOrders, sampleId)
//   단, 해당 sampleId에 대해 ProductionLine::CurrentJob(sampleId)가 값을 가지면(활성 PRODUCING job
//   존재) availableStock은 무조건 0으로 취급한다.
//   승인 판정: availableStock >= order.Quantity() 이면 CONFIRMED, 아니면 PRODUCING.
//   shortage(PRODUCING일 때) = order.Quantity() - availableStock.
//
// Controller(MenuController::HandleApprovalOrRejection)가 실제로 수행해야 할 계산 순서:
//   const Sample& sample = samples_.Find(order.SampleId());
//   bool hasActiveJob = productionLine_.CurrentJob(order.SampleId()).has_value();
//   int availableStock = hasActiveJob
//       ? 0
//       : sample.Stock() - SumConfirmedQuantity(CollectAllOrders(), order.SampleId());
//   int shortage = order.Quantity() - availableStock;
//   order.Approve(availableStock);
//   if (order.Status() == OrderStatus::PRODUCING) { productionLine_.Enqueue(...); }
//
// 이 테스트 파일은 Controller를 거치지 않고, 위 계산식을 Model/Monitoring/ProductionLine 레벨에서
// 직접 재현하여 계약을 검증한다(Controller 구현은 다음 단계에서 controller_agent가 담당).

#include "catch_amalgamated.hpp"
#include "model/Order.h"
#include "model/Sample.h"
#include "model/MonitoringService.h"
#include "produce/ProductionLine.h"

TEST_CASE("PLAN.md 예시 A: 이미 CONFIRMED된 다른 주문의 수량을 제외한 가용 재고로 승인 판정한다", "[Approval][PLAN][exampleA]")
{
    // Sample 재고=100
    Sample sample("S-001", "TestSample", 10, 0.9);
    sample.IncreaseStock(100);

    // 주문 A(수량 80)를 CONFIRMED로 만든다 (availableStock=100 충분 -> CONFIRMED)
    Order orderA("O-201", "Alice", "S-001", 80);
    orderA.Approve(sample.Stock());
    REQUIRE(orderA.Status() == OrderStatus::CONFIRMED);

    std::vector<Order> allOrders{orderA};

    // 주문 B(수량 30) 승인 시점의 availableStock = 100 - SumConfirmedQuantity([A], "S-001") = 100 - 80 = 20
    int confirmedQty = SumConfirmedQuantity(allOrders, "S-001");
    REQUIRE(confirmedQty == 80);

    int availableStock = sample.Stock() - confirmedQty;
    REQUIRE(availableStock == 20);

    Order orderB("O-202", "Bob", "S-001", 30);
    int shortage = orderB.Quantity() - availableStock;
    orderB.Approve(availableStock);

    REQUIRE(orderB.Status() == OrderStatus::PRODUCING);
    REQUIRE(shortage == 10);
}

TEST_CASE("PLAN.md 예시 B: 같은 시료에 활성 PRODUCING job이 있으면 남은 물리적 재고와 무관하게 무조건 PRODUCING이다", "[Approval][PLAN][exampleB]")
{
    // Sample 재고=10, 수율=1.0(단순화)
    Sample sample("S-001", "TestSample", 10, 1.0);
    sample.IncreaseStock(10);

    ProductionLine productionLine;

    // 주문 A(수량 50) 승인: availableStock=10 < 50 -> PRODUCING, shortage=40
    Order orderA("O-301", "Alice", "S-001", 50);
    int availableStockForA = sample.Stock(); // CONFIRMED 주문 없음, 활성 job도 아직 없음
    int shortageForA = orderA.Quantity() - availableStockForA;
    orderA.Approve(availableStockForA);
    REQUIRE(orderA.Status() == OrderStatus::PRODUCING);
    REQUIRE(shortageForA == 40);

    productionLine.Enqueue(orderA.Id(), orderA.SampleId(), shortageForA, sample);

    // A가 아직 생산 중인 상태에서 주문 B(수량 3) 승인 시도
    bool hasActiveJob = productionLine.CurrentJob("S-001").has_value();
    REQUIRE(hasActiveJob);

    Order orderB("O-302", "Bob", "S-001", 3);
    int availableStockForB = hasActiveJob ? 0 : sample.Stock();
    int shortageForB = orderB.Quantity() - availableStockForB;
    orderB.Approve(availableStockForB);

    REQUIRE(availableStockForB == 0);
    REQUIRE(orderB.Status() == OrderStatus::PRODUCING);
    REQUIRE(shortageForB == orderB.Quantity());
    REQUIRE(shortageForB == 3);
}

TEST_CASE("availableStock=0으로 Approve를 호출하면 주문 수량과 무관하게 항상 PRODUCING이고 shortage는 수량 전체와 같다", "[Approval][contract]")
{
    Order order("O-303", "Carol", "S-001", 7);

    int availableStock = 0;
    int shortage = order.Quantity() - availableStock;
    order.Approve(availableStock);

    REQUIRE(order.Status() == OrderStatus::PRODUCING);
    REQUIRE(shortage == 7);
}

TEST_CASE("회귀: CONFIRMED 주문도 없고 활성 job도 없으면 availableStock은 sample.Stock()과 같다", "[Approval][regression]")
{
    Sample sample("S-001", "TestSample", 10, 0.9);
    sample.IncreaseStock(50);

    ProductionLine productionLine;
    std::vector<Order> allOrders; // CONFIRMED 주문 없음

    bool hasActiveJob = productionLine.CurrentJob("S-001").has_value();
    REQUIRE_FALSE(hasActiveJob);

    int confirmedQty = SumConfirmedQuantity(allOrders, "S-001");
    REQUIRE(confirmedQty == 0);

    int availableStock = hasActiveJob ? 0 : sample.Stock() - confirmedQty;
    REQUIRE(availableStock == sample.Stock());

    Order order("O-304", "Dave", "S-001", 20);
    order.Approve(availableStock);
    REQUIRE(order.Status() == OrderStatus::CONFIRMED);
}
