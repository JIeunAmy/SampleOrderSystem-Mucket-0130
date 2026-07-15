// Phase 4 Red 단계: produce_agent가 아직 구현하지 않은 ProductionLine(FIFO 생산 큐 +
// wall-clock 실시간 완료 판정)의 공개 API를 TDD_Agent가 먼저 설계하고, 이를 검증하는
// 실패하는 테스트를 작성한다.
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
//   2026-07-15 요구사항 변경(Red): Sample::AvgProductionTime()이 double을 반환하도록 바뀌므로
//   (SampleTests.cpp 참고) ProductionJob::totalMinutes도 기존 int에서 double로 변경해야 한다.
//   int로 유지하면 소수점 avgProductionTime * actualQuantity 결과가 대입 시 잘려나가(예: 2.5*4=10.0은
//   보존되지만 0.5*1=0.5는 0으로 잘림) wall-clock 완료 판정이 부정확해진다. startedAt/expectedEndAt
//   계산 시 std::chrono::minutes(int) 대신 소수점 분을 초/밀리초 단위로 환산하는 방식
//   (예: std::chrono::duration_cast<std::chrono::system_clock::duration>(
//        std::chrono::duration<double, std::ratio<60>>(totalMinutes)))으로 바꿔야 한다.
//
//   class ProductionLine
//   {
//   public:
//       using NowProvider = std::function<std::chrono::system_clock::time_point()>;
//
//       explicit ProductionLine(NowProvider nowProvider =
//                                    [] { return std::chrono::system_clock::now(); });
//
//       // 주문을 생산 큐에 등록한다.
//       // - shortage는 "이 주문이 PRODUCING으로 판정된 시점의 부족분"을 호출자(controller_agent)가
//       //   전달한다. Order::Approve(sample)가 이미 그 시점의 sample.Stock()과 quantity_를 비교해
//       //   PRODUCING 여부를 정했으므로, 그 시점의 부족분(quantity - stockAtApproval)을 그대로
//       //   재사용하는 것이 안전하다. ProductionLine이 Enqueue 시점에 SampleRepository를 다시 조회해
//       //   "현재" 재고로 부족분을 재계산하면, 그 사이 다른 주문의 생산 완료로 재고가 이미 변해
//       //   있을 수 있어 실제 필요한 실 생산량과 어긋날 위험이 있다. 따라서 shortage는 인자로
//       //   명시적으로 전달받는다 (재계산하지 않음). yieldRate/avgProductionTime은 sample에서 읽는다.
//       // - 동일 sampleId의 생산이 이미 진행 중이면(CurrentJob(sampleId)가 존재하면) 대기열
//       //   (FIFO)에 추가만 되고 즉시 시작하지 않는다.
//       // - 동일 sampleId의 생산이 진행 중이 아니면 즉시 시작한다(startedAt = nowProvider() 호출 시점).
//       // - 서로 다른 sampleId는 각각 독립적으로 "한 번에 하나씩" 생산되므로 동시에 즉시 시작될 수 있다.
//       void Enqueue(const std::string& orderId, const std::string& sampleId, int shortage,
//                    const Sample& sample);
//
//       // 현재 시각(nowProvider()) 기준으로 진행 중인 job이 완료되었는지 판정한다.
//       // 완료된 job은 orders.Find(orderId).CompleteProduction(sample, job.actualQuantity)를
//       // 호출해 주문 상태를 PRODUCING -> CONFIRMED로 전환하고 Sample 재고를 증가시킨 뒤,
//       // 같은 sampleId의 대기열에 다음 job이 있으면 지금 시각을 startedAt으로 즉시 시작시킨다.
//       // 완료 처리된 orderId 목록을 반환한다(완료된 게 없으면 빈 벡터).
//       //
//       // Sample에 대한 가변 접근이 필요하므로, SampleRepository에 OrderRepository::Find와 대칭인
//       // 비-const 오버로드 `Sample& Find(const std::string&)`가 필요하다 (model/Sample.h는 이미
//       // Green이라 TDD_Agent가 직접 수정하지 않지만, 기존 const Find를 깨지 않는 추가 오버로드이므로
//       // model_agent가 produce_agent Green 단계 진입 전에 반영해야 한다 — docs_temp/tdd/phase.md에 명시).
//       std::vector<std::string> ProcessCompletions(OrderRepository& orders, SampleRepository& samples);
//
//       // 현재 생산 중인 job 조회 (없으면 nullopt)
//       std::optional<ProductionJob> CurrentJob(const std::string& sampleId) const;
//       // FIFO 대기열 조회 (현재 진행 중인 job은 포함하지 않음, 먼저 등록된 순서)
//       std::vector<ProductionJob> PendingQueue(const std::string& sampleId) const;
//
//       // 영속화: 현재 진행 중(current) + 대기(pending) job을 모두 data::ProductionState로 변환한다.
//       // data::ProductionState는 orderId/productionStartAt/productionEndAt/actualQuantity만
//       // 가지므로(data/Repository.h는 이미 Green이라 필드를 추가하지 않음), sampleId/shortage는
//       // 저장하지 않는다. 대신 RestoreState에서 OrderRepository를 통해 orderId -> sampleId를 다시
//       // 얻는다(주문은 이미 JSON에 영속화되어 있으므로 sampleId는 orders.json에서 복구 가능,
//       // shortage는 완료 판정에 불필요하므로 복원 시 0으로 둔다 - actualQuantity만 있으면 재고 반영에
//       // 충분하다).
//       std::vector<data::ProductionState> ExportState() const;
//
//       // states의 각 orderId로 orders에서 sampleId를 조회해 sample별 current/pending 대기열을
//       // 재구성한다. vector 내 순서를 FIFO 순서로 간주한다(같은 sampleId 중 먼저 나온 항목이 current).
//       void RestoreState(const std::vector<data::ProductionState>& states, OrderRepository& orders);
//
//       // ISO 8601 <-> time_point 변환 (data::ProductionState 왕복에 사용, 순수 함수라 독립적으로도 테스트)
//       static std::string FormatTimePoint(std::chrono::system_clock::time_point tp);
//       static std::chrono::system_clock::time_point ParseTimePoint(const std::string& iso8601);
//
//       // 실 생산량 = ceil(shortage / yieldRate) (순수 함수, 경계값 테스트 대상)
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

    auto job = line.CurrentJob("S-001");
    REQUIRE(job.has_value());
    REQUIRE(job->actualQuantity == 20);   // ceil(10 / 0.5)
    REQUIRE(job->totalMinutes == 100);    // 5 * 20
    REQUIRE(job->startedAt == BaseTime());
    REQUIRE(job->expectedEndAt == BaseTime() + std::chrono::minutes(100));
}

TEST_CASE("동일 시료가 이미 생산 중이면 새 주문은 대기열에만 추가된다", "[ProductionLine][Enqueue][FIFO]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    Sample sample("S-001", "테스트시료", 5, 0.5);

    line.Enqueue("O-1", "S-001", 10, sample);
    clock.AdvanceMinutes(1); // 두 번째 등록 시각은 다르지만 즉시 시작되면 안 된다
    line.Enqueue("O-2", "S-001", 4, sample);

    auto current = line.CurrentJob("S-001");
    REQUIRE(current.has_value());
    REQUIRE(current->orderId == "O-1"); // 먼저 등록된 O-1이 계속 진행 중이어야 함

    auto pending = line.PendingQueue("S-001");
    REQUIRE(pending.size() == 1);
    REQUIRE(pending[0].orderId == "O-2");
}

TEST_CASE("서로 다른 시료는 동시에 각각 즉시 생산이 시작될 수 있다", "[ProductionLine][Enqueue]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    Sample sampleA("S-001", "시료A", 5, 0.5);
    Sample sampleB("S-002", "시료B", 3, 1.0);

    line.Enqueue("O-1", "S-001", 10, sampleA);
    line.Enqueue("O-2", "S-002", 6, sampleB);

    auto jobA = line.CurrentJob("S-001");
    auto jobB = line.CurrentJob("S-002");
    REQUIRE(jobA.has_value());
    REQUIRE(jobB.has_value());
    REQUIRE(jobA->orderId == "O-1");
    REQUIRE(jobB->orderId == "O-2");
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
    order1.Approve(sample); // stock(0) < quantity(20) -> PRODUCING
    order2.Approve(sample); // stock(0) < quantity(8)  -> PRODUCING
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
    auto currentAfter = line.CurrentJob("S-001");
    REQUIRE(currentAfter.has_value());
    REQUIRE(currentAfter->orderId == "O-2");
    REQUIRE(currentAfter->startedAt == BaseTime() + std::chrono::minutes(200));
    REQUIRE(line.PendingQueue("S-001").empty());
}

TEST_CASE("생산 시작 직후에는 완료로 판정되지 않는다", "[ProductionLine][wall-clock]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    OrderRepository orders;
    SampleRepository samples;

    Order order("O-1", "Alice", "S-001", 20);
    Sample sample("S-001", "테스트시료", 5, 0.5);
    order.Approve(sample);
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
    order.Approve(sample);
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
    order.Approve(sample);
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
    order.Approve(sample);
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
    order.Approve(sample);
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

    auto current = after.CurrentJob("S-001");
    REQUIRE(current.has_value());
    REQUIRE(current->orderId == "O-1");
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
// ---------------------------------------------------------------------------------------------

TEST_CASE("avgProductionTime이 소수점 값이면 총 생산 시간도 정확한 소수점 값으로 계산된다",
          "[ProductionLine][Enqueue][double]")
{
    FakeClock clock(BaseTime());
    ProductionLine line([&clock] { return clock(); });

    Sample sample("S-700", "소수점시료", /*avgProductionTime=*/2.5, /*yieldRate=*/1.0);

    line.Enqueue("O-700", "S-700", /*shortage=*/4, sample); // actualQuantity = ceil(4/1.0) = 4

    auto job = line.CurrentJob("S-700");
    REQUIRE(job.has_value());
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
    order.Approve(sample); // stock 0 < 1 -> PRODUCING
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
    order.Approve(sample); // stock 0 < 10 -> PRODUCING
    orders.Register(order);
    samples.Register(sample);

    line.Enqueue("O-600", "S-600", /*shortage=*/10, sample);

    auto job = line.CurrentJob("S-600");
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
    order.Approve(sample); // stock(0) < 20 -> PRODUCING
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
