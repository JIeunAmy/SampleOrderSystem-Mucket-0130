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
//   bool hasActiveJob = productionLine_.HasJobForSample(order.SampleId());
//   int availableStock = hasActiveJob
//       ? 0
//       : sample.Stock() - SumConfirmedQuantity(CollectAllOrders(), order.SampleId());
//   int shortage = order.Quantity() - availableStock;
//   order.Approve(availableStock);
//   if (order.Status() == OrderStatus::PRODUCING) { productionLine_.Enqueue(...); }
//
// 이 테스트 파일은 Controller를 거치지 않고, 위 계산식을 Model/Monitoring/ProductionLine 레벨에서
// 직접 재현하여 계약을 검증한다(Controller 구현은 다음 단계에서 controller_agent가 담당).
//
// ---------------------------------------------------------------------------------------------
// 2026-07-15 요구사항 변경(Red, `.claude/skills/production/SKILL.md` 참고): ProductionLine이 "전역
// 단일 생산 슬롯" 구조로 재설계되어 `CurrentJob()`/`PendingQueue()`가 더 이상 sampleId 인자를 받지
// 않는다. 승인 로직에서 "이 시료가 이미 생산 중인지"를 판정할 때는 `CurrentJob().has_value()`(현재
// job만 확인)로는 부족하다 — 전역 단일 큐 구조에서는 이 시료의 job이 "대기열에만" 있을 수도 있기
// 때문이다. 따라서 전역 큐 전체(현재 job + 대기열)에서 해당 sampleId를 조회하는 새 API
// `bool HasJobForSample(const std::string& sampleId) const`를 사용해야 한다.
// ---------------------------------------------------------------------------------------------

#include <chrono>

#include "catch_amalgamated.hpp"
#include "model/Order.h"
#include "model/Sample.h"
#include "model/MonitoringService.h"
#include "produce/ProductionLine.h"
#include "data/Repository.h"

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
    // 전역 단일 슬롯 구조에서도 A는 여전히 CurrentJob이므로 HasJobForSample이 true여야 한다.
    bool hasActiveJob = productionLine.HasJobForSample("S-001");
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

TEST_CASE("PLAN.md 예시 B 확장(전역 단일 슬롯 핵심 케이스): 다른 시료가 현재 생산 중이라 이 시료의 job이 "
          "대기열에만 있어도 HasJobForSample은 true이며 availableStock은 여전히 0으로 취급된다",
          "[Approval][PLAN][exampleB][GlobalSlot]")
{
    // 시료 X가 먼저 생산 중이고, 시료 S-001의 job은 전역 대기열에만 들어가 있는 상황을 재현한다.
    Sample sampleX("S-X", "다른시료", 10, 1.0);
    Order orderX("O-X", "Xavier", "S-X", 5);
    orderX.Approve(0); // stock 0 < 5 -> PRODUCING
    ProductionLine productionLine;
    productionLine.Enqueue(orderX.Id(), orderX.SampleId(), 5, sampleX); // 즉시 시작(CurrentJob)

    Sample sample("S-001", "TestSample", 10, 1.0);
    sample.IncreaseStock(10);

    Order orderA("O-301", "Alice", "S-001", 50);
    int availableStockForA = sample.Stock(); // 아직 S-001에 대한 job은 없음
    int shortageForA = orderA.Quantity() - availableStockForA;
    orderA.Approve(availableStockForA);
    REQUIRE(orderA.Status() == OrderStatus::PRODUCING);

    // S-X가 CurrentJob이므로 S-001의 job(orderA)은 전역 대기열에만 들어간다.
    productionLine.Enqueue(orderA.Id(), orderA.SampleId(), shortageForA, sample);
    REQUIRE(productionLine.CurrentJob().value().sampleId == "S-X");

    // S-001은 지금 당장 CurrentJob이 아니지만(대기열에만 존재), HasJobForSample은 true여야 한다.
    bool hasActiveJob = productionLine.HasJobForSample("S-001");
    REQUIRE(hasActiveJob);

    Order orderB("O-302", "Bob", "S-001", 3);
    int availableStockForB = hasActiveJob ? 0 : sample.Stock();
    int shortageForB = orderB.Quantity() - availableStockForB;
    orderB.Approve(availableStockForB);

    REQUIRE(availableStockForB == 0);
    REQUIRE(orderB.Status() == OrderStatus::PRODUCING);
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

// ---------------------------------------------------------------------------------------------
// 2026-07-15 버그 수정(Red): 사용자가 보고한 실제 버그 시나리오 재현.
//
// 근본 원인: `HasJobForSample`은 "이 시료에 대해 생산 큐(현재 job + 대기열)에 job이 하나라도 있으면
// 무조건 가용재고를 0으로 취급"한다. 그러나 shortage(부족분)가 quantity(주문수량)와 같은 job(전량을
// 스스로 생산, 재고에 대한 클레임이 0)이 큐에 있다고 해서 그 시료의 물리적 재고가 전부 "이미 그
// job의 계획에 포함"되는 것은 아니다 — 클레임이 0이면 물리 재고를 전혀 필요로 하지 않기 때문이다.
//
// 새 계산식(다음 단계 controller_agent가 구현):
//   availableStock = sample.Stock()
//                     - SumConfirmedQuantity(allOrders, sampleId)
//                     - productionLine_.SumReservedStockForSample(sampleId, orders_);
// (더 이상 HasJobForSample로 무조건 0 취급하지 않는다.)
// ---------------------------------------------------------------------------------------------

TEST_CASE("사용자 보고 버그: 재고를 전혀 요구하지 않는(shortage==quantity) job들이 큐에 있어도, "
          "완료된 선행 job의 실 생산량만큼 늘어난 재고는 새 주문이 정상적으로 평가받을 수 있어야 한다",
          "[Approval][BUG][ReservedStock]")
{
    // Sample(시료1) 재고 0, yieldRate=0.5 (shortage=3 -> actualQuantity=ceil(3/0.5)=6, 사용자 시나리오와 일치)
    Sample sample("S-1", "시료1", /*avgProductionTime=*/1, /*yieldRate=*/0.5);
    OrderRepository orders;
    ProductionLine productionLine;

    // 주문 A(수량 3) 승인: 이 시점 availableStock = 0 - 0(confirmed) - 0(reserved, 큐 비어있음) = 0
    Order orderA("O-A", "Alice", "S-1", 3);
    int availableStockForA = sample.Stock()
        - SumConfirmedQuantity(std::vector<Order>{}, "S-1")
        - productionLine.SumReservedStockForSample("S-1", orders);
    REQUIRE(availableStockForA == 0);
    int shortageForA = orderA.Quantity() - availableStockForA;
    orderA.Approve(availableStockForA);
    REQUIRE(orderA.Status() == OrderStatus::PRODUCING);
    REQUIRE(shortageForA == 3);
    orders.Register(orderA);
    productionLine.Enqueue(orderA.Id(), orderA.SampleId(), shortageForA, sample); // 즉시 시작(currentJob), claim = 3-3 = 0

    // 주문 B(수량 3, 같은 시료) 승인: A의 클레임이 0이므로 availableStock은 여전히 0이어야 한다
    // (기존 버그 코드라면 HasJobForSample("S-1")이 true이므로 이 부분은 결과가 같지만, 이것이 바로
    //  다음 단계에서 새 계산식이 기존 옵션 B의 보호를 그대로 유지해야 함을 보여준다).
    std::vector<Order> allOrdersAfterA{orderA};
    int availableStockForB = sample.Stock()
        - SumConfirmedQuantity(allOrdersAfterA, "S-1")
        - productionLine.SumReservedStockForSample("S-1", orders);
    REQUIRE(availableStockForB == 0);
    Order orderB("O-B", "Bob", "S-1", 3);
    int shortageForB = orderB.Quantity() - availableStockForB;
    orderB.Approve(availableStockForB);
    REQUIRE(orderB.Status() == OrderStatus::PRODUCING);
    REQUIRE(shortageForB == 3);
    orders.Register(orderB);
    productionLine.Enqueue(orderB.Id(), orderB.SampleId(), shortageForB, sample); // A가 진행 중이므로 대기열행, claim = 3-3 = 0

    // A가 wall-clock 기준으로 완료된다: 재고가 actualQuantity(6)만큼 증가하고 A는 CONFIRMED로 전환된다.
    // (ProductionLine 기본 nowProvider는 실제 system_clock을 쓰므로, A의 총 생산시간이 0에 가깝도록
    //  avgProductionTime=1분·actualQuantity=6 -> 6분이 걸린다. 테스트에서는 FakeClock을 쓰는 별도
    //  ProductionLine 인스턴스로 이 단계를 재현한다.)
    SampleRepository samples;
    samples.Register(sample);

    auto baseTime = std::chrono::system_clock::from_time_t(1784000000);
    long long completedMinutes = 0;
    ProductionLine::NowProvider nowProvider = [&baseTime, &completedMinutes]
    {
        return baseTime + std::chrono::minutes(completedMinutes);
    };
    ProductionLine timedLine(nowProvider);

    // A와 B를 동일한 시나리오로 다시 등록한다(FakeClock 기반 인스턴스에서 완료 판정을 검증하기 위함).
    timedLine.Enqueue(orderA.Id(), orderA.SampleId(), shortageForA, sample); // 즉시 시작, total = 1*6 = 6분
    timedLine.Enqueue(orderB.Id(), orderB.SampleId(), shortageForB, sample); // 대기열행, total = 1*6 = 6분

    completedMinutes = 6; // A의 총 생산 시간(6분) 정확히 경과
    std::vector<std::string> completed = timedLine.ProcessCompletions(orders, samples);
    REQUIRE(completed.size() == 1);
    REQUIRE(completed[0] == "O-A");
    REQUIRE(orders.Find("O-A").Status() == OrderStatus::CONFIRMED);
    REQUIRE(samples.Find("S-1").Stock() == 6); // A의 실 생산량만큼 재고 증가

    // B는 이제 currentJob으로 승격되어 있어야 한다(진행 중).
    auto currentAfterA = timedLine.CurrentJob();
    REQUIRE(currentAfterA.has_value());
    REQUIRE(currentAfterA->orderId == "O-B");

    // 핵심 검증: 이제 주문 C(수량 3, 같은 시료)를 승인 처리한다.
    // 새 계산식으로 계산한 availableStock은 (재고 6) - (CONFIRMED 합계: A의 3) - (큐의 재고 클레임:
    // B는 shortage==quantity이므로 클레임 0) = 3 이다. B의 클레임이 0이므로 재고를 전혀 갉아먹지
    // 않고, availableStock(3) >= orderC.Quantity()(3)이므로 C는 CONFIRMED로 판정되어야 한다.
    // (버그가 있는 기존 코드라면 HasJobForSample("S-1")이 true를 반환해 강제로 PRODUCING이 되어버릴
    //  것이다 — 이 부분이 Red로 실패해야 한다.)
    std::vector<Order> allOrdersAfterCompletion{orders.Find("O-A"), orders.Find("O-B")};
    const Sample& sampleAfterCompletion = samples.Find("S-1");

    int confirmedQtyForC = SumConfirmedQuantity(allOrdersAfterCompletion, "S-1");
    REQUIRE(confirmedQtyForC == 3); // A만 CONFIRMED

    int reservedClaimForC = timedLine.SumReservedStockForSample("S-1", orders);
    REQUIRE(reservedClaimForC == 0); // B의 클레임은 0 (shortage==quantity)

    int availableStockForC = sampleAfterCompletion.Stock() - confirmedQtyForC - reservedClaimForC;
    REQUIRE(availableStockForC == 3); // 6 - 3 - 0

    Order orderC("O-C", "Carol", "S-1", 3);
    orderC.Approve(availableStockForC);

    REQUIRE(orderC.Status() == OrderStatus::CONFIRMED);
}

TEST_CASE("회귀: CONFIRMED 주문도 없고 활성 job도 없으면 availableStock은 sample.Stock()과 같다", "[Approval][regression]")
{
    Sample sample("S-001", "TestSample", 10, 0.9);
    sample.IncreaseStock(50);

    ProductionLine productionLine;
    std::vector<Order> allOrders; // CONFIRMED 주문 없음

    bool hasActiveJob = productionLine.HasJobForSample("S-001");
    REQUIRE_FALSE(hasActiveJob);

    int confirmedQty = SumConfirmedQuantity(allOrders, "S-001");
    REQUIRE(confirmedQty == 0);

    int availableStock = hasActiveJob ? 0 : sample.Stock() - confirmedQty;
    REQUIRE(availableStock == sample.Stock());

    Order order("O-304", "Dave", "S-001", 20);
    order.Approve(availableStock);
    REQUIRE(order.Status() == OrderStatus::CONFIRMED);
}

// ---------------------------------------------------------------------------------------------
// 2026-07-15 버그 수정(Red, Phase 12): 사용자가 실제로 겪은 "재시작(RestoreState)을 거치면 승인
// 판정이 왜곡되는" 시나리오를 재현한다.
//
// 근본 원인: `data::ProductionState`에 `shortage` 필드가 없어서 `ProductionLine::ExportState()`가
// job.shortage를 내보내지 못하고, `ProductionLine::RestoreState()`가 복원되는 모든 job의
// shortage를 무조건 0으로 설정해버린다(`job.shortage = 0; // 완료 판정에 불필요...`라는 주석의 전제가
// Phase 11에서 `SumReservedStockForSample`이 도입되면서 더 이상 성립하지 않게 됨). 그 결과 재시작
// 이후에는 shortage==quantity(전량 자체 생산, 재고 클레임 0이어야 하는) job도
// "quantity - 0 = quantity"만큼 재고 클레임을 갖는 것으로 잘못 계산되어, 새 주문의 availableStock이
// 부당하게 깎여서 CONFIRMED가 되어야 할 주문이 PRODUCING으로 잘못 판정된다.
//
// 이 테스트는 Phase 11 테스트("사용자 보고 버그: ...")와 동일한 시나리오에 "저장(ExportState) ->
// 새 인스턴스로 복원(RestoreState)"을 반드시 끼워 넣어 이 회귀를 재현한다. 재시작 없이 같은 인스턴스로만
// 진행하면(Phase 11 테스트처럼) shortage 값이 메모리에 그대로 남아있어 버그가 드러나지 않는다.
// ---------------------------------------------------------------------------------------------

TEST_CASE("사용자 보고 버그(재시작 포함): 재시작(ExportState/RestoreState)을 거쳐도 shortage 정보가 "
          "유실되지 않아 재고 클레임 계산이 정확하게 유지되고, 신규 주문이 정상적으로 CONFIRMED 판정된다",
          "[Approval][BUG][ReservedStock][RestoreState][Phase12]")
{
    // Sample(시료1) 재고 0, yieldRate=0.5 (shortage=3 -> actualQuantity=ceil(3/0.5)=6)
    Sample sample("S-1", "시료1", /*avgProductionTime=*/1, /*yieldRate=*/0.5);
    OrderRepository orders;
    // 하드코딩된 절대 epoch 대신, 테스트 실행 시점을 기준(baseTime)으로 삼아 상대 시각을 사용한다.
    // beforeRestart도 이 baseTime을 기준으로 동작하도록 고정 NowProvider를 사용해서, 재시작 전/후
    // 시나리오가 실행 시점의 실제 시스템 시각과 무관하게 항상 동일하게 재현되도록 한다.
    auto baseTime = std::chrono::system_clock::now();
    ProductionLine::NowProvider beforeRestartNowProvider = [&baseTime]
    {
        return baseTime;
    };
    ProductionLine beforeRestart(beforeRestartNowProvider);

    // 주문 A(수량 3) 승인: availableStock = 0 - 0(confirmed) - 0(reserved, 큐 비어있음) = 0
    Order orderA("O-A", "Alice", "S-1", 3);
    int availableStockForA = sample.Stock()
        - SumConfirmedQuantity(std::vector<Order>{}, "S-1")
        - beforeRestart.SumReservedStockForSample("S-1", orders);
    REQUIRE(availableStockForA == 0);
    int shortageForA = orderA.Quantity() - availableStockForA;
    orderA.Approve(availableStockForA);
    REQUIRE(orderA.Status() == OrderStatus::PRODUCING);
    REQUIRE(shortageForA == 3);
    orders.Register(orderA);
    beforeRestart.Enqueue(orderA.Id(), orderA.SampleId(), shortageForA, sample); // 즉시 시작(currentJob), claim = 3-3 = 0

    // 주문 B(수량 3, 같은 시료) 승인: A가 활성 상태이므로 대기열에 들어간다. A의 클레임이 0이므로
    // availableStock은 여전히 0이어야 한다(shortage_B = 3 = quantity, claim = 0).
    std::vector<Order> allOrdersAfterA{orderA};
    int availableStockForB = sample.Stock()
        - SumConfirmedQuantity(allOrdersAfterA, "S-1")
        - beforeRestart.SumReservedStockForSample("S-1", orders);
    REQUIRE(availableStockForB == 0);
    Order orderB("O-B", "Bob", "S-1", 3);
    int shortageForB = orderB.Quantity() - availableStockForB;
    orderB.Approve(availableStockForB);
    REQUIRE(orderB.Status() == OrderStatus::PRODUCING);
    REQUIRE(shortageForB == 3);
    orders.Register(orderB);
    beforeRestart.Enqueue(orderB.Id(), orderB.SampleId(), shortageForB, sample); // A가 진행 중이므로 대기열행, claim = 3-3 = 0

    // ---- 여기서 "재시작"을 시뮬레이션한다: ExportState()로 상태를 저장한 뒤, 완전히 새로운
    //      ProductionLine 인스턴스에 RestoreState()로 복원한다. ----
    auto exportedStates = beforeRestart.ExportState();
    REQUIRE(exportedStates.size() == 2);

    long long elapsedMinutes = 0;
    ProductionLine::NowProvider nowProvider = [&baseTime, &elapsedMinutes]
    {
        return baseTime + std::chrono::minutes(elapsedMinutes);
    };
    ProductionLine afterRestart(nowProvider);
    afterRestart.RestoreState(exportedStates, orders);

    SampleRepository samples;
    samples.Register(sample);

    // A의 완료 시각 이후로 가짜 시계를 이동시켜 ProcessCompletions를 호출한다.
    // (A: shortage=3, yieldRate=0.5 -> actualQuantity=6, avgProductionTime=1 -> total=6분)
    elapsedMinutes = 6;
    auto completed = afterRestart.ProcessCompletions(orders, samples);
    REQUIRE(completed.size() == 1);
    REQUIRE(completed[0] == "O-A");
    REQUIRE(orders.Find("O-A").Status() == OrderStatus::CONFIRMED);
    REQUIRE(samples.Find("S-1").Stock() == 6); // A의 실 생산량만큼 재고 증가

    // B는 이제 currentJob으로 승격되어 있어야 한다.
    auto currentAfterA = afterRestart.CurrentJob();
    REQUIRE(currentAfterA.has_value());
    REQUIRE(currentAfterA->orderId == "O-B");

    // 핵심 검증: 재시작을 거쳤음에도 B의 shortage(3, quantity와 동일하므로 클레임 0)가 유실되지 않아,
    // 주문 C(수량 3, 같은 시료)를 승인할 때 availableStock이 정확히 계산되어야 한다.
    //
    // 버그가 있다면(RestoreState가 shortage를 0으로 리셋) B의 클레임이 3-0=3으로 잘못 계산되어
    // availableStock = 6 - 3(A CONFIRMED) - 3(B의 잘못된 클레임) = 0이 되어버리고, C가 부당하게
    // PRODUCING으로 판정된다. 올바른 계산이라면 B의 클레임은 여전히 0이어야 하므로
    // availableStock = 6 - 3 - 0 = 3이 되어 C(수량 3)는 CONFIRMED로 판정되어야 한다.
    std::vector<Order> allOrdersAfterCompletion{orders.Find("O-A"), orders.Find("O-B")};
    const Sample& sampleAfterCompletion = samples.Find("S-1");

    int confirmedQtyForC = SumConfirmedQuantity(allOrdersAfterCompletion, "S-1");
    REQUIRE(confirmedQtyForC == 3); // A만 CONFIRMED

    int reservedClaimForC = afterRestart.SumReservedStockForSample("S-1", orders);
    REQUIRE(reservedClaimForC == 0); // 재시작 이후에도 B의 클레임은 여전히 0이어야 한다 (핵심 회귀 검증)

    int availableStockForC = sampleAfterCompletion.Stock() - confirmedQtyForC - reservedClaimForC;
    REQUIRE(availableStockForC == 3); // 6 - 3 - 0
    REQUIRE(availableStockForC >= 3);

    Order orderC("O-C", "Carol", "S-1", 3);
    orderC.Approve(availableStockForC);

    REQUIRE(orderC.Status() == OrderStatus::CONFIRMED); // 재시작을 거쳤음에도 C는 정상적으로 CONFIRMED여야 한다
}
