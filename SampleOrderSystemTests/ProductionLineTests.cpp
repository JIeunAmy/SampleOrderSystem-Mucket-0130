// Phase 4 Red 단계 (원본) → 2026-07-15 전역 단일 생산 슬롯 재설계(Red 2단계):
// produce_agent가 아직 재구현하지 않은 ProductionLine(전역 단일 FIFO 생산 큐 +
// wall-clock 실시간 완료 판정)의 공개 API를 TDD_Agent가 먼저 설계하고, 이를 검증하는
// 실패하는 테스트를 작성한다.
//
// ---------------------------------------------------------------------------------------------
// 2026-07-15 요구사항 변경(Red, `.claude/skills/production/SKILL.md` 참고): "생산 라인은 제품(시료)에
// 상관없이 전체 시스템에서 단 하나의 활성 생산 job만 가질 수 있다." 기존 구현은
// `std::unordered_map<std::string, std::deque<ProductionJob>> queues_`로 시료별 독립 큐를 유지해
// 서로 다른 시료가 동시에 생산될 수 있는 버그가 있었다. 이를 "전역 단일 현재 job(currentJob_) +
// 전역 대기열(pendingQueue_)" 구조로 재설계한다.
// ---------------------------------------------------------------------------------------------
//
// 설계한 인터페이스 (produce/ProductionLine.h, produce_agent가 구현해야 함):
//
//   struct ProductionJob
//   {
//       std::string orderId;
//       std::string sampleId;
//       int shortage = 0;        // 이 job 등록 시점의 부족분 (Enqueue 호출자가 전달)
//       int actualQuantity = 0;  // ceil(shortage / sample.YieldRate())
//       double totalMinutes = 0; // sample.AvgProductionTime() * actualQuantity (소수점 분 단위 허용)
//       std::chrono::system_clock::time_point startedAt;
//       std::chrono::system_clock::time_point expectedEndAt; // startedAt + totalMinutes(분, 소수점 포함)
//   };
//
//   class ProductionLine
//   {
//   public:
//       using NowProvider = std::function<std::chrono::system_clock::time_point()>;
//
//       explicit ProductionLine(NowProvider nowProvider =
//                                    [] { return std::chrono::system_clock::now(); });
//
//       // 주문을 전역 생산 큐에 등록한다.
//       // - shortage는 "이 주문이 PRODUCING으로 판정된 시점의 부족분"을 호출자(controller_agent)가
//       //   전달한다(재계산하지 않음). yieldRate/avgProductionTime은 sample에서 읽는다.
//       // - 현재 진행 중인 job이 없으면(CurrentJob()이 nullopt) sampleId와 무관하게 즉시 시작한다.
//       // - 현재 진행 중인 job이 있으면, 새 job의 sampleId가 무엇이든 상관없이(같은 시료든 다른
//       //   시료든) 전역 대기열(pendingQueue_) 맨 뒤에 추가되고 즉시 시작하지 않는다.
//       // - 서로 다른 sampleId라도 "전역 단일 슬롯" 원칙에 따라 동시에 시작될 수 없다(핵심 변경점).
//       void Enqueue(const std::string& orderId, const std::string& sampleId, int shortage,
//                    const Sample& sample);
//
//       // 현재 시각(nowProvider()) 기준으로 진행 중인 job이 완료되었는지 판정한다.
//       // 완료된 job은 orders.Find(orderId).CompleteProduction(sample, job.actualQuantity)를
//       // 호출해 주문 상태를 PRODUCING -> CONFIRMED로 전환하고 Sample 재고를 증가시킨 뒤,
//       // 전역 대기열에 다음 job이 있으면(sampleId 무관) 지금 시각을 startedAt으로 즉시 시작시킨다.
//       // 완료 처리된 orderId 목록을 반환한다(완료된 게 없으면 빈 벡터, 연쇄적으로 여러 개가
//       // 한 번에 완료될 수도 있다).
//       std::vector<std::string> ProcessCompletions(OrderRepository& orders, SampleRepository& samples);
//
//       // 전역적으로 현재 진행 중인 job 조회 (없으면 nullopt) — sampleId 매개변수 없음.
//       std::optional<ProductionJob> CurrentJob() const;
//       // 전역 대기열 조회 (현재 진행 중인 job은 포함하지 않음, 여러 시료가 섞여 있을 수 있음,
//       // 먼저 등록된 순서) — sampleId 매개변수 없음.
//       std::vector<ProductionJob> PendingQueue() const;
//
//       // 특정 sampleId의 job이 전역 큐(현재 job + 대기열) 어딘가에 이미 존재하는지 확인한다.
//       // Controller가 승인 시 "이 시료가 이미 생산 대기/진행 중인지" 판단할 때 사용한다
//       // (현재 job만 확인하고 대기열을 빠뜨리면 안 된다 — SKILL.md "승인 로직과의 연계" 참고).
//       bool HasJobForSample(const std::string& sampleId) const;
//
//       // 영속화: 현재 진행 중(current) + 대기(pending) job을 모두 data::ProductionState로 변환한다.
//       // 리스트의 순서 자체가 FIFO 순서를 나타낸다(첫 번째 항목이 현재 job).
//       // 2026-07-15 변경(Phase 12, Red): data::ProductionState에 shortage 필드가 추가되었으므로
//       // ExportState는 이제 job.shortage도 함께 내보내야 한다(state.shortage = job.shortage).
//       std::vector<data::ProductionState> ExportState() const;
//
//       // states의 각 orderId로 orders에서 sampleId를 조회해 전역 current/pending 대기열을
//       // 재구성한다(첫 번째 항목이 current, 이후 항목이 pendingQueue_ 순서).
//       // 2026-07-15 변경(Phase 12, Red): job.shortage를 더 이상 0으로 하드코딩하지 않고
//       // state.shortage로부터 정확히 복원해야 한다 — 그렇지 않으면 SumReservedStockForSample의
//       // 재고 클레임 계산이 재시작 이후 왜곡된다(버그).
//       void RestoreState(const std::vector<data::ProductionState>& states, OrderRepository& orders);
//
//       // ISO 8601 <-> time_point 변환 (변경 없음)
//       static std::string FormatTimePoint(std::chrono::system_clock::time_point tp);
//       static std::chrono::system_clock::time_point ParseTimePoint(const std::string& iso8601);
//
//       // 실 생산량 = ceil(shortage / yieldRate) (순수 함수, 변경 없음)
//       static int ComputeActualQuantity(int shortage, double yieldRate);
//   };
//
// 위 시그니처는 produce_agent와 조율 대상이며, 구현 시 변경이 필요하면 이 테스트를 함께 갱신한다.

#include <chrono>
#include <cstdio>

#include "catch_amalgamated.hpp"
#include "produce/ProductionLine.h"
#include "model/Order.h"
#include "model/Sample.h"
#include "data/Repository.h"

namespace
{
    // 테스트에서 wall-clock을 제어하기 위한 가짜 시계.
    // ProductionLine의 NowProvider(std::function<time_point()>)에 참조로 캡처해 넘긴다.
    class FakeClock
    {
    public:
        explicit FakeClock(std::chrono::system_clock::time_point start) : now_(start) {}

        std::chrono::system_clock::time_point operator()() const { return now_; }

        void AdvanceMinutes(long long minutes) { now_ += std::chrono::minutes(minutes); }
        void Set(std::chrono::system_clock::time_point tp) { now_ = tp; }

    private:
        std::chrono::system_clock::time_point now_;
    };

    std::chrono::system_clock::time_point BaseTime()
    {
        // 2026-07-15 09:00:00 UTC 근방의 임의의 고정 시각.
        return std::chrono::system_clock::from_time_t(1784000000);
    }

    // 영속화 통합 테스트에서 사용하는 임시 파일 자동 삭제 RAII 헬퍼(RepositoryTests.cpp와 동일한 패턴).
    struct TempFileGuard
    {
        std::string path;
        explicit TempFileGuard(std::string p) : path(std::move(p)) {}
        ~TempFileGuard() { std::remove(path.c_str()); }
    };
}

TEST_CASE("실 생산량은 부족분을 수율로 나눈 뒤 올림한다 (나누어떨어지는 경우)", "[ProductionLine][ComputeActualQuantity]")
{
    REQUIRE(ProductionLine::ComputeActualQuantity(10, 0.5) == 20);
}

TEST_CASE("실 생산량은 부족분을 수율로 나눈 뒤 올림한다 (나누어떨어지지 않는 경우)", "[ProductionLine][ComputeActualQuantity]")
{
    // 10 / 0.3 = 33.333... -> ceil = 34
    REQUIRE(ProductionLine::ComputeActualQuantity(10, 0.3) == 34);
}

TEST_CASE("Enqueue 시 총 생산 시간은 평균 생산시간 * 실 생산량이다", "[ProductionLine][Enqueue]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    Sample sample("S-001", "테스트시료", /*avgProductionTime=*/5, /*yieldRate=*/0.5);

    line.Enqueue("O-1", "S-001", /*shortage=*/10, sample);

    auto job = line.CurrentJob();
    REQUIRE(job.has_value());
    REQUIRE(job->orderId == "O-1");
    REQUIRE(job->actualQuantity == 20);   // ceil(10 / 0.5)
    REQUIRE(job->totalMinutes == 100);    // 5 * 20
    REQUIRE(job->startedAt == BaseTime());
    REQUIRE(job->expectedEndAt == BaseTime() + std::chrono::minutes(100));
}

TEST_CASE("동일 시료가 이미 생산 중이면 새 주문은 전역 대기열에만 추가된다", "[ProductionLine][Enqueue][FIFO]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    Sample sample("S-001", "테스트시료", 5, 0.5);

    line.Enqueue("O-1", "S-001", 10, sample);
    clock.AdvanceMinutes(1); // 두 번째 등록 시각은 다르지만 즉시 시작되면 안 된다
    line.Enqueue("O-2", "S-001", 4, sample);

    auto current = line.CurrentJob();
    REQUIRE(current.has_value());
    REQUIRE(current->orderId == "O-1"); // 먼저 등록된 O-1이 계속 진행 중이어야 함

    auto pending = line.PendingQueue();
    REQUIRE(pending.size() == 1);
    REQUIRE(pending[0].orderId == "O-2");
}

// ---------------------------------------------------------------------------------------------
// 2026-07-15 변경(Red): 기존 "서로 다른 시료는 동시에 각각 즉시 생산이 시작될 수 있다" 테스트는
// 잘못된 설계(시료별 독립 큐)를 전제로 했다. 전역 단일 생산 슬롯 원칙에 따라 정반대 기대값으로 교체한다.
// ---------------------------------------------------------------------------------------------

TEST_CASE("전역 단일 생산 슬롯: 서로 다른 시료의 주문이라도 현재 job이 있으면 즉시 시작되지 않고 대기열에 추가된다",
          "[ProductionLine][GlobalSlot][Enqueue]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    Sample sampleA("S-A", "시료A", 5, 0.5);  // shortage=10 -> actualQty=20, total=100분
    Sample sampleB("S-B", "시료B", 3, 1.0);  // shortage=6  -> actualQty=6,  total=18분

    line.Enqueue("O-A", "S-A", 10, sampleA); // 즉시 시작 (startedAt = BaseTime)

    auto current = line.CurrentJob();
    REQUIRE(current.has_value());
    REQUIRE(current->orderId == "O-A");

    line.Enqueue("O-B", "S-B", 6, sampleB); // 다른 시료지만 A가 진행 중이므로 대기해야 함

    // A는 여전히 CurrentJob이어야 하고, B는 즉시 시작되지 않고 대기열에 들어가야 한다.
    auto currentAfterB = line.CurrentJob();
    REQUIRE(currentAfterB.has_value());
    REQUIRE(currentAfterB->orderId == "O-A");

    auto pending = line.PendingQueue();
    REQUIRE(pending.size() == 1);
    REQUIRE(pending[0].orderId == "O-B");
    REQUIRE(pending[0].sampleId == "S-B");

    // A가 wall-clock 기준으로 완료되면, 대기열에 있던 B가 즉시 시작되어야 한다.
    OrderRepository orders;
    SampleRepository samples;
    Order orderA("O-A", "Alice", "S-A", 10);
    Order orderB("O-B", "Bob", "S-B", 6);
    orderA.Approve(0); // stock 0 < 10 -> PRODUCING
    orderB.Approve(0); // stock 0 < 6  -> PRODUCING
    orders.Register(orderA);
    orders.Register(orderB);
    samples.Register(sampleA);
    samples.Register(sampleB);

    clock.Set(BaseTime() + std::chrono::minutes(100)); // A의 총 생산 시간(100분) 정확히 경과
    auto completed = line.ProcessCompletions(orders, samples);

    REQUIRE(completed.size() == 1);
    REQUIRE(completed[0] == "O-A");
    REQUIRE(orders.Find("O-A").Status() == OrderStatus::CONFIRMED);

    auto currentAfterCompletion = line.CurrentJob();
    REQUIRE(currentAfterCompletion.has_value());
    REQUIRE(currentAfterCompletion->orderId == "O-B"); // 다른 시료라도 대기열에서 즉시 시작
    REQUIRE(currentAfterCompletion->startedAt == BaseTime() + std::chrono::minutes(100));
    REQUIRE(line.PendingQueue().empty());
}

TEST_CASE("FIFO: 서로 다른 시료가 섞여 등록돼도 도착 순서(A, B, A, C)를 그대로 보존해 하나씩 처리한다",
          "[ProductionLine][FIFO][GlobalSlot]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    OrderRepository orders;
    SampleRepository samples;

    // 모든 시료가 shortage=1, yieldRate=1.0 -> actualQuantity=1, avgProductionTime=1 -> totalMinutes=1
    // 이 되도록 구성해 매 1분마다 정확히 하나씩 완료되도록 만든다.
    Sample sampleA("S-A", "시료A", 1, 1.0);
    Sample sampleB("S-B", "시료B", 1, 1.0);
    Sample sampleC("S-C", "시료C", 1, 1.0);
    samples.Register(sampleA);
    samples.Register(sampleB);
    samples.Register(sampleC);

    Order orderA1("O-A1", "Alice", "S-A", 1);
    Order orderB1("O-B1", "Bob", "S-B", 1);
    Order orderA2("O-A2", "Alice2", "S-A", 1);
    Order orderC1("O-C1", "Carol", "S-C", 1);
    orderA1.Approve(0);
    orderB1.Approve(0);
    orderA2.Approve(0);
    orderC1.Approve(0);
    orders.Register(orderA1);
    orders.Register(orderB1);
    orders.Register(orderA2);
    orders.Register(orderC1);

    // 등록 순서: A, B, A, C (같은 시료 A의 job이 두 번 등장해도 순서를 앞당기지 않는다)
    line.Enqueue("O-A1", "S-A", 1, sampleA);
    line.Enqueue("O-B1", "S-B", 1, sampleB);
    line.Enqueue("O-A2", "S-A", 1, sampleA);
    line.Enqueue("O-C1", "S-C", 1, sampleC);

    auto current = line.CurrentJob();
    REQUIRE(current.has_value());
    REQUIRE(current->orderId == "O-A1");

    auto pending = line.PendingQueue();
    REQUIRE(pending.size() == 3);
    REQUIRE(pending[0].orderId == "O-B1");
    REQUIRE(pending[1].orderId == "O-A2");
    REQUIRE(pending[2].orderId == "O-C1");

    // 1분씩 경과시키며 정확히 A1 -> B1 -> A2 -> C1 순서로 완료되는지 검증한다.
    clock.AdvanceMinutes(1);
    auto completed1 = line.ProcessCompletions(orders, samples);
    REQUIRE(completed1.size() == 1);
    REQUIRE(completed1[0] == "O-A1");
    REQUIRE(line.CurrentJob().value().orderId == "O-B1");

    clock.AdvanceMinutes(1);
    auto completed2 = line.ProcessCompletions(orders, samples);
    REQUIRE(completed2.size() == 1);
    REQUIRE(completed2[0] == "O-B1");
    REQUIRE(line.CurrentJob().value().orderId == "O-A2");

    clock.AdvanceMinutes(1);
    auto completed3 = line.ProcessCompletions(orders, samples);
    REQUIRE(completed3.size() == 1);
    REQUIRE(completed3[0] == "O-A2");
    REQUIRE(line.CurrentJob().value().orderId == "O-C1");

    clock.AdvanceMinutes(1);
    auto completed4 = line.ProcessCompletions(orders, samples);
    REQUIRE(completed4.size() == 1);
    REQUIRE(completed4[0] == "O-C1");
    REQUIRE_FALSE(line.CurrentJob().has_value());
    REQUIRE(line.PendingQueue().empty());
}

TEST_CASE("HasJobForSample: 현재 진행 중인 시료는 true, 등록된 적 없는 시료는 false", "[ProductionLine][HasJobForSample]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    Sample sample("S-001", "테스트시료", 5, 0.5);
    line.Enqueue("O-1", "S-001", 10, sample);

    REQUIRE(line.HasJobForSample("S-001"));
    REQUIRE_FALSE(line.HasJobForSample("S-999"));
}

TEST_CASE("HasJobForSample: 대기열에만 있고 지금 당장 CurrentJob은 아닌 시료도 true를 반환한다 (핵심 케이스)",
          "[ProductionLine][HasJobForSample]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    Sample sampleA("S-A", "시료A", 5, 0.5);
    Sample sampleB("S-B", "시료B", 3, 1.0);

    line.Enqueue("O-A", "S-A", 10, sampleA); // 즉시 시작 -> CurrentJob
    line.Enqueue("O-B", "S-B", 6, sampleB);  // 대기열에만 존재, CurrentJob은 아님

    REQUIRE(line.HasJobForSample("S-A")); // 현재 진행 중
    REQUIRE(line.HasJobForSample("S-B")); // 대기 중이지만 존재는 함 (핵심)
    REQUIRE_FALSE(line.HasJobForSample("S-Z")); // 전혀 등록된 적 없는 시료
}

TEST_CASE("FIFO: 먼저 등록된 주문이 완료되면 다음 대기 주문이 즉시 시작된다", "[ProductionLine][FIFO][ProcessCompletions]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    OrderRepository orders;
    SampleRepository samples;

    Order order1("O-1", "Alice", "S-001", 20);
    Order order2("O-2", "Bob", "S-001", 8);
    Sample sample("S-001", "테스트시료", 5, 0.5); // stock 0
    order1.Approve(sample.Stock()); // stock(0) < quantity(20) -> PRODUCING
    order2.Approve(sample.Stock()); // stock(0) < quantity(8)  -> PRODUCING
    orders.Register(order1);
    orders.Register(order2);
    samples.Register(sample);

    line.Enqueue("O-1", "S-001", 20, sample); // shortage=20, actualQty=ceil(20/0.5)=40, total=200분
    line.Enqueue("O-2", "S-001", 8, sample);  // 대기

    // O-1의 총 생산시간(200분)이 정확히 경과한 시점
    clock.AdvanceMinutes(200);
    auto completed = line.ProcessCompletions(orders, samples);

    REQUIRE(completed.size() == 1);
    REQUIRE(completed[0] == "O-1");
    REQUIRE(orders.Find("O-1").Status() == OrderStatus::CONFIRMED);
    REQUIRE(samples.Find("S-001").Stock() == 40);

    // O-2가 다음으로 즉시 시작되어야 함 (현재 시각 = 시작 시각)
    auto currentAfter = line.CurrentJob();
    REQUIRE(currentAfter.has_value());
    REQUIRE(currentAfter->orderId == "O-2");
    REQUIRE(currentAfter->startedAt == BaseTime() + std::chrono::minutes(200));
    REQUIRE(line.PendingQueue().empty());
}

TEST_CASE("생산 시작 직후에는 완료로 판정되지 않는다", "[ProductionLine][wall-clock]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    OrderRepository orders;
    SampleRepository samples;

    Order order("O-1", "Alice", "S-001", 20);
    Sample sample("S-001", "테스트시료", 5, 0.5);
    order.Approve(sample.Stock());
    orders.Register(order);
    samples.Register(sample);

    line.Enqueue("O-1", "S-001", 20, sample);

    auto completed = line.ProcessCompletions(orders, samples);

    REQUIRE(completed.empty());
    REQUIRE(orders.Find("O-1").Status() == OrderStatus::PRODUCING);
}

TEST_CASE("총 생산 시간의 절반만 경과하면 여전히 미완료다", "[ProductionLine][wall-clock]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    OrderRepository orders;
    SampleRepository samples;

    Order order("O-1", "Alice", "S-001", 20);
    Sample sample("S-001", "테스트시료", 5, 0.5); // total = 5 * ceil(20/0.5)=5*40=200분
    order.Approve(sample.Stock());
    orders.Register(order);
    samples.Register(sample);

    line.Enqueue("O-1", "S-001", 20, sample);
    clock.AdvanceMinutes(100); // 절반만 경과

    auto completed = line.ProcessCompletions(orders, samples);

    REQUIRE(completed.empty());
    REQUIRE(orders.Find("O-1").Status() == OrderStatus::PRODUCING);
    REQUIRE(samples.Find("S-001").Stock() == 0);
}

TEST_CASE("총 생산 시간이 정확히 경과하면 완료로 판정되어 CONFIRMED와 재고 반영이 이루어진다", "[ProductionLine][wall-clock]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    OrderRepository orders;
    SampleRepository samples;

    Order order("O-1", "Alice", "S-001", 20);
    Sample sample("S-001", "테스트시료", 5, 0.5); // total = 200분, actualQuantity = 40
    order.Approve(sample.Stock());
    orders.Register(order);
    samples.Register(sample);

    line.Enqueue("O-1", "S-001", 20, sample);
    clock.AdvanceMinutes(200);

    auto completed = line.ProcessCompletions(orders, samples);

    REQUIRE(completed.size() == 1);
    REQUIRE(completed[0] == "O-1");
    REQUIRE(orders.Find("O-1").Status() == OrderStatus::CONFIRMED);
    REQUIRE(samples.Find("S-001").Stock() == 40);
}

TEST_CASE("재시작 복구: 이미 완료 시각이 지난 job은 복원 직후 완료 처리된다", "[ProductionLine][RestoreState]")
{
    FakeClock clockBeforeRestart(BaseTime());
    ProductionLine before([&clockBeforeRestart] { return clockBeforeRestart(); });

    OrderRepository orders;
    SampleRepository samples;

    Order order("O-1", "Alice", "S-001", 20);
    Sample sample("S-001", "테스트시료", 5, 0.5); // total = 200분, actualQuantity = 40
    order.Approve(sample.Stock());
    orders.Register(order);
    samples.Register(sample);

    before.Enqueue("O-1", "S-001", 20, sample);
    auto exported = before.ExportState();
    REQUIRE(exported.size() == 1);
    REQUIRE(exported[0].orderId == "O-1");
    REQUIRE(exported[0].actualQuantity == 40);

    // "재시작" 이후 현재 시각은 시작 시각으로부터 총 생산 시간(200분)을 훌쩍 넘겼다.
    FakeClock clockAfterRestart(BaseTime() + std::chrono::minutes(500));
    ProductionLine after([&clockAfterRestart] { return clockAfterRestart(); });
    after.RestoreState(exported, orders);

    auto completed = after.ProcessCompletions(orders, samples);

    REQUIRE(completed.size() == 1);
    REQUIRE(completed[0] == "O-1");
    REQUIRE(orders.Find("O-1").Status() == OrderStatus::CONFIRMED);
    REQUIRE(samples.Find("S-001").Stock() == 40);
}

TEST_CASE("재시작 복구: 아직 완료 시각이 지나지 않은 job은 대기 상태를 유지한다", "[ProductionLine][RestoreState]")
{
    FakeClock clockBeforeRestart(BaseTime());
    ProductionLine before([&clockBeforeRestart] { return clockBeforeRestart(); });

    OrderRepository orders;
    SampleRepository samples;

    Order order("O-1", "Alice", "S-001", 20);
    Sample sample("S-001", "테스트시료", 5, 0.5); // total = 200분
    order.Approve(sample.Stock());
    orders.Register(order);
    samples.Register(sample);

    before.Enqueue("O-1", "S-001", 20, sample);
    auto exported = before.ExportState();

    // "재시작" 이후 현재 시각은 시작 시각으로부터 100분만 경과(총 생산 시간의 절반).
    FakeClock clockAfterRestart(BaseTime() + std::chrono::minutes(100));
    ProductionLine after([&clockAfterRestart] { return clockAfterRestart(); });
    after.RestoreState(exported, orders);

    auto completed = after.ProcessCompletions(orders, samples);

    REQUIRE(completed.empty());
    REQUIRE(orders.Find("O-1").Status() == OrderStatus::PRODUCING);
    REQUIRE(samples.Find("S-001").Stock() == 0);

    auto current = after.CurrentJob();
    REQUIRE(current.has_value());
    REQUIRE(current->orderId == "O-1");
}

TEST_CASE("재시작 복구: 대기열까지 포함해 FIFO 순서가 그대로 보존된다 (전역 단일 큐)",
          "[ProductionLine][RestoreState][FIFO]")
{
    FakeClock clockBeforeRestart(BaseTime());
    ProductionLine before([&clockBeforeRestart] { return clockBeforeRestart(); });

    OrderRepository orders;
    SampleRepository samples;

    Sample sampleA("S-A", "시료A", 5, 0.5);
    Sample sampleB("S-B", "시료B", 3, 1.0);
    Order orderA("O-A", "Alice", "S-A", 10);
    Order orderB("O-B", "Bob", "S-B", 6);
    orderA.Approve(0);
    orderB.Approve(0);
    orders.Register(orderA);
    orders.Register(orderB);
    samples.Register(sampleA);
    samples.Register(sampleB);

    before.Enqueue("O-A", "S-A", 10, sampleA); // current, total=100분
    before.Enqueue("O-B", "S-B", 6, sampleB);  // pending, 다른 시료

    auto exported = before.ExportState();
    REQUIRE(exported.size() == 2);
    REQUIRE(exported[0].orderId == "O-A"); // 첫 번째 항목이 current
    REQUIRE(exported[1].orderId == "O-B");

    FakeClock clockAfterRestart(BaseTime()); // 아직 시간 경과 없음
    ProductionLine after([&clockAfterRestart] { return clockAfterRestart(); });
    after.RestoreState(exported, orders);

    auto current = after.CurrentJob();
    REQUIRE(current.has_value());
    REQUIRE(current->orderId == "O-A");

    auto pending = after.PendingQueue();
    REQUIRE(pending.size() == 1);
    REQUIRE(pending[0].orderId == "O-B");
}

TEST_CASE("FormatTimePoint/ParseTimePoint는 ISO 8601 왕복 시 원본과 일치한다", "[ProductionLine][ISO8601]")
{
    auto original = BaseTime() + std::chrono::minutes(123);
    std::string formatted = ProductionLine::FormatTimePoint(original);
    auto parsed = ProductionLine::ParseTimePoint(formatted);

    // 초 단위까지만 보존하면 충분하다 (밀리초 이하는 요구사항 범위 밖).
    auto originalSeconds = std::chrono::time_point_cast<std::chrono::seconds>(original);
    auto parsedSeconds = std::chrono::time_point_cast<std::chrono::seconds>(parsed);
    REQUIRE(originalSeconds == parsedSeconds);
}

// ---------------------------------------------------------------------------------------------
// 2026-07-15 요구사항 추가: avgProductionTime의 소수점 지원(double) + 생산 완료 반영량 검증 +
// 영속화(Repository) 통합 시나리오 + 미등록 시료 생산 불가 설계 확인.
// (전역 단일 슬롯 재설계 이후에도 계산식/영속화 계약 자체는 동일하게 유지된다.)
// ---------------------------------------------------------------------------------------------

TEST_CASE("avgProductionTime이 소수점 값이면 총 생산 시간도 정확한 소수점 값으로 계산된다",
          "[ProductionLine][Enqueue][double]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    Sample sample("S-700", "소수점시료", /*avgProductionTime=*/2.5, /*yieldRate=*/1.0);

    line.Enqueue("O-700", "S-700", /*shortage=*/4, sample); // actualQuantity = ceil(4/1.0) = 4

    auto job = line.CurrentJob();
    REQUIRE(job.has_value());
    REQUIRE(job->orderId == "O-700");
    REQUIRE(job->actualQuantity == 4);
    REQUIRE(job->totalMinutes == Catch::Approx(10.0)); // 2.5 * 4 = 10.0
    REQUIRE(job->expectedEndAt == BaseTime() + std::chrono::seconds(600)); // 10.0분 = 600초
}

TEST_CASE("avgProductionTime이 1분 미만의 소수(0.5분=30초)여도 wall-clock 판정이 초 단위로 정확하다",
          "[ProductionLine][wall-clock][double]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    OrderRepository orders;
    SampleRepository samples;

    Order order("O-800", "Alice", "S-800", 1);
    Sample sample("S-800", "초단위시료", /*avgProductionTime=*/0.5, /*yieldRate=*/1.0);
    order.Approve(sample.Stock()); // stock 0 < 1 -> PRODUCING
    orders.Register(order);
    samples.Register(sample);

    line.Enqueue("O-800", "S-800", /*shortage=*/1, sample); // actualQuantity=1, totalMinutes=0.5

    // 29초만 경과: 아직 미완료여야 한다 (0.5분=30초 미만)
    clock.Set(BaseTime() + std::chrono::seconds(29));
    auto stillProducing = line.ProcessCompletions(orders, samples);
    REQUIRE(stillProducing.empty());
    REQUIRE(orders.Find("O-800").Status() == OrderStatus::PRODUCING);

    // 정확히 30초 경과: 완료 처리되어야 한다
    clock.Set(BaseTime() + std::chrono::seconds(30));
    auto completed = line.ProcessCompletions(orders, samples);
    REQUIRE(completed.size() == 1);
    REQUIRE(orders.Find("O-800").Status() == OrderStatus::CONFIRMED);
    REQUIRE(samples.Find("S-800").Stock() == 1);
}

TEST_CASE("실 생산량이 부족분과 다른 경우(나누어떨어지지 않음)에도 재고 증가량은 정확히 실 생산량과 일치한다",
          "[ProductionLine][ProcessCompletions][actualQuantity]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    OrderRepository orders;
    SampleRepository samples;

    Order order("O-600", "Alice", "S-600", 10);
    Sample sample("S-600", "테스트시료", 1, 0.3); // actualQuantity = ceil(10/0.3) = 34 (shortage와 다름)
    order.Approve(sample.Stock()); // stock 0 < 10 -> PRODUCING
    orders.Register(order);
    samples.Register(sample);

    line.Enqueue("O-600", "S-600", /*shortage=*/10, sample);

    auto job = line.CurrentJob();
    REQUIRE(job.has_value());
    REQUIRE(job->shortage == 10);
    REQUIRE(job->actualQuantity == 34);
    REQUIRE(job->actualQuantity != job->shortage);

    clock.AdvanceMinutes(34); // totalMinutes = 1 * 34 = 34

    auto completed = line.ProcessCompletions(orders, samples);

    REQUIRE(completed.size() == 1);
    // 재고 증가량은 shortage(10)이 아니라 actualQuantity(34)와 정확히 일치해야 한다.
    REQUIRE(samples.Find("S-600").Stock() == 34);
}

TEST_CASE("영속화 통합: 만료된 생산 job을 저장했다가 새 인스턴스로 복원하면 재고 증가가 저장/로드 사이클을 거쳐도 유지된다",
          "[ProductionLine][Repository][integration]")
{
    TempFileGuard samplesGuard("test_integration_samples.json");
    TempFileGuard ordersGuard("test_integration_orders.json");
    TempFileGuard stateGuard("test_integration_production_state.json");

    FakeClock clockBefore(BaseTime());
    ProductionLine before([&clockBefore] { return clockBefore(); });

    OrderRepository orders;
    SampleRepository samples;

    Order order("O-500", "Alice", "S-500", 20);
    Sample sample("S-500", "통합테스트시료", 5, 0.5, /*initialStock=*/0); // total = 5 * ceil(20/0.5) = 5*40 = 200분
    order.Approve(sample.Stock()); // stock(0) < 20 -> PRODUCING
    orders.Register(order);
    samples.Register(sample);

    before.Enqueue("O-500", "S-500", /*shortage=*/20, sample); // actualQuantity = 40

    // 1) 생산 진행 상태 + 시료 + 주문을 각각 JSON 파일로 저장
    data::SaveProductionState(before.ExportState(), stateGuard.path);
    data::SaveSamples(std::vector<Sample>{ samples.Find("S-500") }, samplesGuard.path);
    data::SaveOrders(std::vector<Order>{ orders.Find("O-500") }, ordersGuard.path);

    // 2) "재시작": 완전히 새로운 SampleRepository/OrderRepository/ProductionLine 인스턴스로 다시 로드
    std::vector<Sample> loadedSamples = data::LoadSamples(samplesGuard.path);
    std::vector<Order> loadedOrders = data::LoadOrders(ordersGuard.path);
    std::vector<data::ProductionState> loadedStates = data::LoadProductionState(stateGuard.path);

    REQUIRE(loadedSamples.size() == 1);
    REQUIRE(loadedOrders.size() == 1);
    REQUIRE(loadedStates.size() == 1);

    SampleRepository newSamples;
    for (const auto& s : loadedSamples)
    {
        newSamples.Register(s);
    }
    OrderRepository newOrders;
    for (const auto& o : loadedOrders)
    {
        newOrders.Register(o);
    }

    // 재실행 시각은 생산 시작(BaseTime)으로부터 총 생산 시간(200분)을 훌쩍 넘겼다.
    FakeClock clockAfter(BaseTime() + std::chrono::minutes(500));
    ProductionLine after([&clockAfter] { return clockAfter(); });
    after.RestoreState(loadedStates, newOrders);

    REQUIRE(newSamples.Find("S-500").Stock() == 0); // 완료 처리 이전에는 아직 재고 반영 전

    // 3) 생산 완료 판정 -> Sample 재고가 실 생산량(actualQuantity=40)만큼 증가해야 한다
    auto completed = after.ProcessCompletions(newOrders, newSamples);

    REQUIRE(completed.size() == 1);
    REQUIRE(completed[0] == "O-500");
    REQUIRE(newOrders.Find("O-500").Status() == OrderStatus::CONFIRMED);
    REQUIRE(newSamples.Find("S-500").Stock() == 40);

    // 4) 증가한 재고를 다시 저장 후 로드해도 정확히 유지되는지 확인 (영속화 사이클 재검증)
    data::SaveSamples(std::vector<Sample>{ newSamples.Find("S-500") }, samplesGuard.path);
    std::vector<Sample> reloaded = data::LoadSamples(samplesGuard.path);
    REQUIRE(reloaded.size() == 1);
    REQUIRE(reloaded[0].Stock() == 40);
}

// ---------------------------------------------------------------------------------------------
// 2026-07-15 버그 수정(Red): Enqueue 시점에 pendingQueue_로 들어가는 job의 startedAt/expectedEndAt이
// 전혀 계산되지 않고 기본값(epoch)으로 남아 있던 문제. "앱이 꺼져 있던 동안 앞선 job이 실제로는 이미
// 끝났어도, ProcessCompletions가 실행되어 그 사실을 '목격'해야만 다음 job이 시작되는" 구조라서, 꺼져
//있던 시간만큼 뒤 job들의 시계가 전혀 흐르지 않았다.
//
// 수정 방향: Enqueue 시점에 즉시 "이전 항목(대기열의 마지막 항목, 없으면 현재 진행 중인 job)의
// expectedEndAt 바로 다음"을 새 job의 startedAt으로 미리 계산해 확정한다.
// ---------------------------------------------------------------------------------------------

TEST_CASE("Enqueue 시점 시각 사전계산: 대기열에 들어가는 job은 시계를 움직이지 않아도 앞선 job의 종료 예정 시각부터 시작하도록 미리 계산된다",
          "[ProductionLine][Enqueue][precompute][BUG]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    Sample sampleA("S-A", "시료A", 5, 0.5);  // shortage=10 -> actualQty=20, total=100분
    Sample sampleB("S-B", "시료B", 3, 1.0);  // shortage=6  -> actualQty=6,  total=18분

    line.Enqueue("O-A", "S-A", 10, sampleA); // 즉시 시작: startedAt=t0, expectedEndAt=t0+100분

    auto current = line.CurrentJob();
    REQUIRE(current.has_value());
    REQUIRE(current->expectedEndAt == BaseTime() + std::chrono::minutes(100));

    // 시계를 전혀 움직이지 않은 채로 B를 등록한다 (버그가 있다면 B.startedAt은 기본값(epoch)이 된다).
    line.Enqueue("O-B", "S-B", 6, sampleB);

    auto pending = line.PendingQueue();
    REQUIRE(pending.size() == 1);
    REQUIRE(pending[0].orderId == "O-B");

    // B는 A가 끝나는 시각(t0+100분)부터 시작하도록 이미 확정되어 있어야 한다.
    REQUIRE(pending[0].startedAt == current->expectedEndAt);
    REQUIRE(pending[0].expectedEndAt == pending[0].startedAt + std::chrono::minutes(18));
}

TEST_CASE("Enqueue 시점 시각 사전계산: 대기열이 2개 이상이면 이전 대기 항목의 종료 예정 시각을 이어받는 체인을 형성한다",
          "[ProductionLine][Enqueue][precompute][BUG]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    Sample sampleA("S-A", "시료A", 5, 0.5);  // shortage=10 -> actualQty=20, total=100분
    Sample sampleB("S-B", "시료B", 3, 1.0);  // shortage=6  -> actualQty=6,  total=18분
    Sample sampleC("S-C", "시료C", 4, 1.0);  // shortage=5  -> actualQty=5,  total=20분

    line.Enqueue("O-A", "S-A", 10, sampleA); // 즉시 시작
    line.Enqueue("O-B", "S-B", 6, sampleB);  // 대기 1번째
    line.Enqueue("O-C", "S-C", 5, sampleC);  // 대기 2번째

    auto pending = line.PendingQueue();
    REQUIRE(pending.size() == 2);
    REQUIRE(pending[0].orderId == "O-B");
    REQUIRE(pending[1].orderId == "O-C");

    // C는 B(대기열의 마지막 항목)가 끝나는 시각부터 시작해야 한다 (체인이 누적됨).
    REQUIRE(pending[1].startedAt == pending[0].expectedEndAt);
    REQUIRE(pending[1].expectedEndAt == pending[1].startedAt + std::chrono::minutes(20));
}

TEST_CASE("핵심 버그 시나리오: 앱이 오래 꺼져 있다가 재실행되어도 ProcessCompletions 한 번 호출로 밀린 job들이 모두 연쇄 완료된다",
          "[ProductionLine][ProcessCompletions][precompute][BUG]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    OrderRepository orders;
    SampleRepository samples;

    Sample sampleA("S-A", "시료A", 10, 1.0); // shortage=1 -> actualQty=1, total=10분
    Sample sampleB("S-B", "시료B", 20, 1.0); // shortage=1 -> actualQty=1, total=20분
    samples.Register(sampleA);
    samples.Register(sampleB);

    Order orderA("O-A", "Alice", "S-A", 1);
    Order orderB("O-B", "Bob", "S-B", 1);
    orderA.Approve(0); // stock 0 < 1 -> PRODUCING
    orderB.Approve(0); // stock 0 < 1 -> PRODUCING
    orders.Register(orderA);
    orders.Register(orderB);

    // t0에 A(즉시 시작, 10분)와 B(대기, A 종료 시각부터 시작하도록 사전계산되어 20분간 진행)를 등록한다.
    line.Enqueue("O-A", "S-A", 1, sampleA);
    line.Enqueue("O-B", "S-B", 1, sampleB);

    // B는 t0+10분에 시작해 t0+30분에 끝나도록 이미 확정되어 있어야 한다.
    auto pendingBeforeJump = line.PendingQueue();
    REQUIRE(pendingBeforeJump.size() == 1);
    REQUIRE(pendingBeforeJump[0].startedAt == BaseTime() + std::chrono::minutes(10));
    REQUIRE(pendingBeforeJump[0].expectedEndAt == BaseTime() + std::chrono::minutes(30));

    // 프로그램이 t0+35분까지(즉 A, B 모두 끝난 뒤까지) 한 번도 실행되지 않았다가, 이제 딱 한 번
    // ProcessCompletions가 호출되는 상황을 재현한다.
    clock.Set(BaseTime() + std::chrono::minutes(35));
    auto completed = line.ProcessCompletions(orders, samples);

    REQUIRE(completed.size() == 2);
    REQUIRE(completed[0] == "O-A");
    REQUIRE(completed[1] == "O-B");
    REQUIRE(orders.Find("O-A").Status() == OrderStatus::CONFIRMED);
    REQUIRE(orders.Find("O-B").Status() == OrderStatus::CONFIRMED);
    REQUIRE(samples.Find("S-A").Stock() == 1);
    REQUIRE(samples.Find("S-B").Stock() == 1);
    REQUIRE_FALSE(line.CurrentJob().has_value());
    REQUIRE(line.PendingQueue().empty());
}

// ---------------------------------------------------------------------------------------------
// 2026-07-15 버그 수정(Red, PLAN.md 후속): 승인 시 가용 재고(availableStock) 계산에서
// `ProductionLine::HasJobForSample`이 "이 시료에 대해 큐에 job이 하나라도 있으면 무조건 가용재고를
// 0으로 취급"해버리는 문제. shortage(부족분)가 quantity(주문수량)보다 작은 job(예: quantity=50,
// shortage=40)은 "quantity-shortage"(=10)만큼 기존 물리 재고에 대한 미확정 클레임을 갖지만,
// shortage == quantity인 job(예: quantity=3, shortage=3, 전량 자체 생산)은 재고 클레임이 0이므로 그런
// job이 큐에 있어도 새 주문은 현재 재고를 정상적으로 평가받아야 한다.
//
// 신규 API: 특정 sampleId에 대해 생산 큐(현재 job + 대기열)에 있는 모든 job들의
// "재고 클레임"(orders.Find(job.orderId).Quantity() - job.shortage) 합계를 반환한다.
//   int SumReservedStockForSample(const std::string& sampleId, OrderRepository& orders) const;
// Controller가 다음 단계에서 구현해야 할 계산식(인계용):
//   availableStock = sample.Stock()
//                     - SumConfirmedQuantity(allOrders, sampleId)
//                     - productionLine_.SumReservedStockForSample(sampleId, orders_);
// (더 이상 HasJobForSample로 무조건 0 취급하지 않는다 — quantity-shortage==0인 job은 클레임에
//  기여하지 않으므로 자연스럽게 배제된다.)
// ---------------------------------------------------------------------------------------------

TEST_CASE("SumReservedStockForSample: shortage가 quantity보다 작으면 그 차이(재고 클레임)를 반환한다",
          "[ProductionLine][SumReservedStockForSample]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });
    OrderRepository orders;

    Sample sample("S-X", "시료X", 5, 1.0);

    Order order("O-1", "Alice", "S-X", 50);
    order.Approve(10); // availableStock=10 < 50 -> PRODUCING
    orders.Register(order);

    line.Enqueue("O-1", "S-X", /*shortage=*/40, sample); // claim = 50 - 40 = 10

    REQUIRE(line.SumReservedStockForSample("S-X", orders) == 10);
}

TEST_CASE("SumReservedStockForSample: shortage가 quantity와 같으면(전량 자체 생산) 재고 클레임은 0이다 (핵심 버그 케이스)",
          "[ProductionLine][SumReservedStockForSample][BUG]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });
    OrderRepository orders;

    Sample sample("S-X", "시료X", 5, 1.0);

    Order order("O-1", "Alice", "S-X", 3);
    order.Approve(0); // availableStock=0 -> PRODUCING, shortage=3=quantity
    orders.Register(order);

    line.Enqueue("O-1", "S-X", /*shortage=*/3, sample); // claim = 3 - 3 = 0

    REQUIRE(line.SumReservedStockForSample("S-X", orders) == 0);
}

TEST_CASE("SumReservedStockForSample: 현재 job과 대기열 job의 재고 클레임을 합산한다",
          "[ProductionLine][SumReservedStockForSample]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });
    OrderRepository orders;

    Sample sample("S-X", "시료X", 5, 1.0);

    Order orderCurrent("O-1", "Alice", "S-X", 3);
    orderCurrent.Approve(0); // shortage=3=quantity -> claim 0
    orders.Register(orderCurrent);
    line.Enqueue("O-1", "S-X", 3, sample); // 즉시 시작(currentJob)

    Order orderPending("O-2", "Bob", "S-X", 5);
    orderPending.Approve(3); // availableStock=3 < 5 -> shortage=2, claim = 5-2 = 3
    orders.Register(orderPending);
    line.Enqueue("O-2", "S-X", 2, sample); // currentJob이 이미 있으므로 대기열행

    REQUIRE(line.SumReservedStockForSample("S-X", orders) == 3);
}

TEST_CASE("SumReservedStockForSample: 해당 sampleId의 job이 전혀 없으면 0을 반환한다",
          "[ProductionLine][SumReservedStockForSample]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });
    OrderRepository orders;

    REQUIRE(line.SumReservedStockForSample("S-NONE", orders) == 0);
}

TEST_CASE("SumReservedStockForSample: 다른 시료의 job은 합산에서 제외된다",
          "[ProductionLine][SumReservedStockForSample]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });
    OrderRepository orders;

    Sample sampleY("S-Y", "시료Y", 5, 1.0);
    Order orderY("O-Y", "Carol", "S-Y", 10);
    orderY.Approve(4); // availableStock=4 < 10 -> shortage=6, claim = 10-6 = 4
    orders.Register(orderY);
    line.Enqueue("O-Y", "S-Y", 6, sampleY); // 즉시 시작

    // S-X에 대한 job은 없으므로, S-Y의 클레임(4)이 있어도 S-X 조회 결과는 0이어야 한다.
    REQUIRE(line.SumReservedStockForSample("S-X", orders) == 0);
}

// ---------------------------------------------------------------------------------------------
// 2026-07-15 버그 수정(Red, Phase 12): ExportState()가 job.shortage를 data::ProductionState로
// 넘기지 않고, RestoreState()가 항상 job.shortage=0으로 복원해버리는 문제. 이 때문에 재시작
// (RestoreState) 이후에는 SumReservedStockForSample의 재고 클레임 계산이 왜곡된다(shortage==quantity
// 인 job도 재시작 후에는 클레임이 quantity 전체인 것처럼 잘못 계산됨). data::ProductionState에
// shortage 필드를 추가(RepositoryTests.cpp)한 뒤, ExportState가 이를 채우고 RestoreState가 이를
// 그대로 복원해야 한다.
// ---------------------------------------------------------------------------------------------

TEST_CASE("ExportState는 job의 shortage를 data::ProductionState.shortage로 정확히 내보낸다 (Phase 12, BUG)",
          "[ProductionLine][ExportState][shortage][BUG]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });
    OrderRepository orders;

    Sample sample("S-X", "시료X", 5, 1.0);

    // shortage(2) != quantity(5) != actualQuantity(2) 인 케이스로 필드 혼동 여부까지 함께 검증한다.
    Order order("O-1", "Alice", "S-X", 5);
    order.Approve(3); // availableStock=3 < 5 -> PRODUCING, shortage=2
    orders.Register(order);

    line.Enqueue("O-1", "S-X", /*shortage=*/2, sample); // actualQuantity = ceil(2/1.0) = 2

    auto exported = line.ExportState();
    REQUIRE(exported.size() == 1);
    REQUIRE(exported[0].orderId == "O-1");
    REQUIRE(exported[0].actualQuantity == 2);
    REQUIRE(exported[0].shortage == 2); // 원본 job.shortage와 정확히 일치해야 한다
}

TEST_CASE("재시작 복구 후에도 SumReservedStockForSample은 복원 전과 정확히 동일한 값을 반환한다 (Phase 12, 핵심 회귀 방지, BUG)",
          "[ProductionLine][RestoreState][shortage][SumReservedStockForSample][BUG]")
{
    FakeClock clock(BaseTime());
    ProductionLine before([&clock] { return clock(); });
    OrderRepository orders;

    Sample sample("S-X", "시료X", 5, 1.0);

    // currentJob: quantity=3, shortage=3(전량 자체 생산) -> claim = 0
    Order orderA("O-A", "Alice", "S-X", 3);
    orderA.Approve(0); // availableStock=0 -> PRODUCING, shortage=3
    orders.Register(orderA);
    before.Enqueue("O-A", "S-X", 3, sample); // 즉시 시작(currentJob)

    // pendingQueue job: quantity=5, shortage=2 -> claim = 3 (재고 일부를 미리 점유)
    Order orderB("O-B", "Bob", "S-X", 5);
    orderB.Approve(3); // availableStock=3 < 5 -> PRODUCING, shortage=2
    orders.Register(orderB);
    before.Enqueue("O-B", "S-X", 2, sample); // currentJob이 있으므로 대기열행

    // 복원 전 기준값(정답)
    int claimBeforeRestart = before.SumReservedStockForSample("S-X", orders);
    REQUIRE(claimBeforeRestart == 3); // 0(A) + 3(B) = 3

    auto exported = before.ExportState();
    REQUIRE(exported.size() == 2);

    // "재시작": 새로운 ProductionLine 인스턴스에 복원한다.
    FakeClock clockAfterRestart(BaseTime());
    ProductionLine after([&clockAfterRestart] { return clockAfterRestart(); });
    after.RestoreState(exported, orders);

    // 핵심 검증: 복원 후에도 재고 클레임 계산이 복원 전과 정확히 동일해야 한다.
    // (버그가 있다면 job.shortage가 모두 0으로 리셋되어 claim = quantity - 0 = quantity가 되어버려서
    //  A는 3, B는 5, 합계 8을 잘못 반환하게 된다.)
    int claimAfterRestart = after.SumReservedStockForSample("S-X", orders);
    REQUIRE(claimAfterRestart == claimBeforeRestart);
    REQUIRE(claimAfterRestart == 3);
}

TEST_CASE("핵심 버그 시나리오 + 재시작 복구: 대기 중이던 job의 사전계산된 시각이 저장/복원을 거쳐도 유지되어 연쇄 완료된다",
          "[ProductionLine][RestoreState][precompute][BUG]")
{
    FakeClock clockBefore(BaseTime());
    ProductionLine before([&clockBefore] { return clockBefore(); });

    OrderRepository orders;
    SampleRepository samples;

    Sample sampleA("S-A", "시료A", 10, 1.0); // shortage=1 -> actualQty=1, total=10분
    Sample sampleB("S-B", "시료B", 20, 1.0); // shortage=1 -> actualQty=1, total=20분
    samples.Register(sampleA);
    samples.Register(sampleB);

    Order orderA("O-A", "Alice", "S-A", 1);
    Order orderB("O-B", "Bob", "S-B", 1);
    orderA.Approve(0);
    orderB.Approve(0);
    orders.Register(orderA);
    orders.Register(orderB);

    before.Enqueue("O-A", "S-A", 1, sampleA); // 즉시 시작, t0~t0+10분
    before.Enqueue("O-B", "S-B", 1, sampleB); // 대기, 사전계산: t0+10분~t0+30분

    auto exported = before.ExportState();
    REQUIRE(exported.size() == 2);
    REQUIRE(exported[0].orderId == "O-A");
    REQUIRE(exported[1].orderId == "O-B");
    // 대기열 항목도 이미 정확한 시각을 가지고 있어야 export/restore 사이클에서 그대로 보존된다.
    REQUIRE(ProductionLine::ParseTimePoint(exported[1].productionStartAt) ==
            BaseTime() + std::chrono::minutes(10));
    REQUIRE(ProductionLine::ParseTimePoint(exported[1].productionEndAt) ==
            BaseTime() + std::chrono::minutes(30));

    // "재시작": 새 인스턴스로 복원 후, 두 job이 모두 끝난 한참 뒤 시각으로 시계를 점프시킨다.
    FakeClock clockAfter(BaseTime() + std::chrono::minutes(35));
    ProductionLine after([&clockAfter] { return clockAfter(); });
    after.RestoreState(exported, orders);

    auto completed = after.ProcessCompletions(orders, samples);

    REQUIRE(completed.size() == 2);
    REQUIRE(completed[0] == "O-A");
    REQUIRE(completed[1] == "O-B");
    REQUIRE(orders.Find("O-A").Status() == OrderStatus::CONFIRMED);
    REQUIRE(orders.Find("O-B").Status() == OrderStatus::CONFIRMED);
    REQUIRE(samples.Find("S-A").Stock() == 1);
    REQUIRE(samples.Find("S-B").Stock() == 1);
    REQUIRE_FALSE(after.CurrentJob().has_value());
    REQUIRE(after.PendingQueue().empty());
}
