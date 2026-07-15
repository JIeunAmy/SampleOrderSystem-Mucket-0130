// Phase 3 Red 단계: data_agent가 아직 구현하지 않은 Repository(JSON 영속성 계층)
// 인터페이스를 TDD_Agent가 먼저 설계하고, 이를 검증하는 실패하는 테스트를 작성한다.
//
// 설계한 인터페이스 (data/Repository.h, data_agent가 구현해야 함):
//
//   namespace data {
//       constexpr const char* kSamplesFilePath = "samples.json";
//       constexpr const char* kOrdersFilePath = "orders.json";
//       constexpr const char* kProductionStateFilePath = "production_state.json";
//
//       void SaveSamples(const std::vector<Sample>& samples,
//                         const std::string& filePath = kSamplesFilePath);
//       std::vector<Sample> LoadSamples(const std::string& filePath = kSamplesFilePath);
//           // 파일이 없거나 JSON 파싱에 실패하면 예외를 던지지 않고 빈 vector를 반환한다.
//
//       void SaveOrders(const std::vector<Order>& orders,
//                        const std::string& filePath = kOrdersFilePath);
//       std::vector<Order> LoadOrders(const std::string& filePath = kOrdersFilePath);
//           // 파일이 없거나 JSON 파싱에 실패하면 예외를 던지지 않고 빈 vector를 반환한다.
//       void SaveOrder(const Order& order, const std::string& filePath = kOrdersFilePath);
//           // filePath에 저장된 기존 주문 목록을 로드한 뒤, order.Id()와 동일한 주문이 있으면
//           // 그 주문만 교체(update)하고, 없으면 목록 끝에 추가(insert)한 뒤 다시 저장한다.
//           // 그 외 기존 주문들의 필드는 훼손되지 않는다.
//
//       struct ProductionState {
//           std::string orderId;
//           std::string productionStartAt;  // ISO 8601 문자열, 예: "2026-07-15T09:00:00"
//           std::string productionEndAt;    // ISO 8601 문자열
//           int actualQuantity = 0;
//           int shortage = 0; // 2026-07-15 추가(Phase 12, Red): job 등록 시점의 부족분. 저장/로드
//                              // 시 actualQuantity와 동일한 패턴으로 정확히 보존되어야 한다 — 이 값이
//                              // 유실되면 ProductionLine::RestoreState가 shortage를 알 수 없어
//                              // SumReservedStockForSample의 재고 클레임 계산이 왜곡된다(버그).
//       };
//       void SaveProductionState(const std::vector<ProductionState>& states,
//                                 const std::string& filePath = kProductionStateFilePath);
//       std::vector<ProductionState> LoadProductionState(
//           const std::string& filePath = kProductionStateFilePath);
//           // 파일이 없거나 JSON 파싱에 실패하면 예외를 던지지 않고 빈 vector를 반환한다.
//   }
//
// Order 상태(enum class OrderStatus)는 JSON에서 문자열("RESERVED"/"REJECTED"/"PRODUCING"/
// "CONFIRMED"/"RELEASE")로 직렬화/역직렬화한다. Order는 orderId, customerName, sampleId,
// quantity, status 필드를 그대로 저장한다(docs_temp/data/phase.md의 sampleName은 예시일 뿐이며
// 실제 model_agent 구현인 Order.h의 sampleId/customerName 필드를 우선한다).
//
// JSON 파싱/직렬화 방식은 자유(직접 만든 경량 파서든 nlohmann/json vendor 추가든)이나,
// 이 테스트는 위 공개 API(SaveSamples/LoadSamples/SaveOrders/LoadOrders/SaveOrder/
// SaveProductionState/LoadProductionState)만 검증하므로 내부 구현 방식에 의존하지 않는다.
//
// 위 시그니처는 data_agent와 조율 대상이며, 구현 시 변경이 필요하면 이 테스트를 함께 갱신한다.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "data/Repository.h"
#include "model/Order.h"
#include "model/Sample.h"

namespace
{
    // 테스트 종료 시 임시 파일을 자동 삭제하기 위한 헬퍼(RAII).
    struct TempFileGuard
    {
        std::string path;
        explicit TempFileGuard(std::string p) : path(std::move(p)) {}
        ~TempFileGuard() { std::remove(path.c_str()); }
    };

    void WriteRawFile(const std::string& path, const std::string& content)
    {
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        ofs << content;
    }
}

TEST_CASE("Sample 목록을 저장 후 다시 로드하면 필드가 원본과 동일하다", "[Repository][Sample][roundtrip]")
{
    TempFileGuard guard("test_samples_roundtrip.json");

    std::vector<Sample> samples;
    samples.emplace_back("S-001", "실리콘 웨이퍼-8인치", 30, 0.95, 20);
    samples.emplace_back("S-002", "테스트 시료", 15, 0.5, 0);

    data::SaveSamples(samples, guard.path);
    std::vector<Sample> loaded = data::LoadSamples(guard.path);

    REQUIRE(loaded.size() == 2);

    REQUIRE(loaded[0].Id() == "S-001");
    REQUIRE(loaded[0].Name() == "실리콘 웨이퍼-8인치");
    REQUIRE(loaded[0].AvgProductionTime() == 30);
    REQUIRE(loaded[0].YieldRate() == Catch::Approx(0.95));
    REQUIRE(loaded[0].Stock() == 20);

    REQUIRE(loaded[1].Id() == "S-002");
    REQUIRE(loaded[1].Name() == "테스트 시료");
    REQUIRE(loaded[1].AvgProductionTime() == 15);
    REQUIRE(loaded[1].YieldRate() == Catch::Approx(0.5));
    REQUIRE(loaded[1].Stock() == 0);
}

TEST_CASE("Sample의 avgProductionTime이 소수점 값이어도 저장 후 로드 시 정확히 동일한 값으로 복원된다",
          "[Repository][Sample][roundtrip][double]")
{
    // 2026-07-15 요구사항 변경(Red): avgProductionTime은 분 단위 소수점을 허용해야 한다.
    // 현재 Sample.h는 int avgProductionTime이라 15.75를 넘기면 15로 잘려 저장/로드된다.
    TempFileGuard guard("test_samples_roundtrip_double.json");

    std::vector<Sample> samples;
    samples.emplace_back("S-900", "소수점시료", 15.75, 0.8, 5);

    data::SaveSamples(samples, guard.path);
    std::vector<Sample> loaded = data::LoadSamples(guard.path);

    REQUIRE(loaded.size() == 1);
    REQUIRE(loaded[0].Id() == "S-900");
    REQUIRE(loaded[0].AvgProductionTime() == Catch::Approx(15.75));
    REQUIRE(loaded[0].Stock() == 5);
}

TEST_CASE("Order 목록을 저장 후 다시 로드하면 상태를 포함해 필드가 원본과 동일하다", "[Repository][Order][roundtrip]")
{
    TempFileGuard guard("test_orders_roundtrip.json");

    Sample sample("S-001", "TestSample", 10, 0.9, 100);

    Order reserved("O-100", "Alice", "S-001", 10);

    Order confirmed("O-101", "Bob", "S-001", 5);
    confirmed.Approve(sample.Stock()); // 재고 충분 -> CONFIRMED

    Order rejected("O-102", "Carol", "S-001", 3);
    rejected.Reject();

    std::vector<Order> orders{ reserved, confirmed, rejected };

    data::SaveOrders(orders, guard.path);
    std::vector<Order> loaded = data::LoadOrders(guard.path);

    REQUIRE(loaded.size() == 3);

    REQUIRE(loaded[0].Id() == "O-100");
    REQUIRE(loaded[0].CustomerName() == "Alice");
    REQUIRE(loaded[0].SampleId() == "S-001");
    REQUIRE(loaded[0].Quantity() == 10);
    REQUIRE(loaded[0].Status() == OrderStatus::RESERVED);

    REQUIRE(loaded[1].Id() == "O-101");
    REQUIRE(loaded[1].CustomerName() == "Bob");
    REQUIRE(loaded[1].SampleId() == "S-001");
    REQUIRE(loaded[1].Quantity() == 5);
    REQUIRE(loaded[1].Status() == OrderStatus::CONFIRMED);

    REQUIRE(loaded[2].Id() == "O-102");
    REQUIRE(loaded[2].CustomerName() == "Carol");
    REQUIRE(loaded[2].SampleId() == "S-001");
    REQUIRE(loaded[2].Quantity() == 3);
    REQUIRE(loaded[2].Status() == OrderStatus::REJECTED);
}

TEST_CASE("존재하지 않는 파일을 LoadSamples/LoadOrders 하면 빈 벡터를 반환한다", "[Repository][fallback]")
{
    SECTION("Samples")
    {
        std::vector<Sample> loaded = data::LoadSamples("no_such_samples_file_xyz.json");
        REQUIRE(loaded.empty());
    }

    SECTION("Orders")
    {
        std::vector<Order> loaded = data::LoadOrders("no_such_orders_file_xyz.json");
        REQUIRE(loaded.empty());
    }

    SECTION("ProductionState")
    {
        std::vector<data::ProductionState> loaded = data::LoadProductionState("no_such_production_state_file_xyz.json");
        REQUIRE(loaded.empty());
    }
}

TEST_CASE("손상된 JSON 파일을 Load하면 예외 없이 빈 벡터를 반환한다", "[Repository][fallback][corrupted]")
{
    SECTION("Samples")
    {
        TempFileGuard guard("test_samples_corrupted.json");
        WriteRawFile(guard.path, "{ this is not valid JSON [[[");

        std::vector<Sample> loaded;
        REQUIRE_NOTHROW(loaded = data::LoadSamples(guard.path));
        REQUIRE(loaded.empty());
    }

    SECTION("Orders")
    {
        TempFileGuard guard("test_orders_corrupted.json");
        WriteRawFile(guard.path, "not even close to json ---");

        std::vector<Order> loaded;
        REQUIRE_NOTHROW(loaded = data::LoadOrders(guard.path));
        REQUIRE(loaded.empty());
    }

    SECTION("ProductionState")
    {
        TempFileGuard guard("test_production_state_corrupted.json");
        WriteRawFile(guard.path, "{{{ broken");

        std::vector<data::ProductionState> loaded;
        REQUIRE_NOTHROW(loaded = data::LoadProductionState(guard.path));
        REQUIRE(loaded.empty());
    }
}

TEST_CASE("SaveOrder로 기존 주문 중 하나만 갱신해도 다른 주문은 그대로 유지된다", "[Repository][Order][SaveOrder]")
{
    TempFileGuard guard("test_orders_saveorder.json");

    Sample sample("S-001", "TestSample", 10, 0.9, 100);

    Order orderA("O-200", "Alice", "S-001", 10);
    Order orderB("O-201", "Bob", "S-001", 5);
    Order orderC("O-202", "Carol", "S-001", 3);

    std::vector<Order> orders{ orderA, orderB, orderC };
    data::SaveOrders(orders, guard.path);

    // orderB만 승인 상태로 전이시킨 뒤 단일 갱신 저장
    orderB.Approve(sample.Stock()); // CONFIRMED
    data::SaveOrder(orderB, guard.path);

    std::vector<Order> loaded = data::LoadOrders(guard.path);
    REQUIRE(loaded.size() == 3);

    // orderA, orderC는 훼손되지 않고 RESERVED 그대로 유지
    bool foundA = false, foundB = false, foundC = false;
    for (const auto& o : loaded)
    {
        if (o.Id() == "O-200")
        {
            foundA = true;
            REQUIRE(o.CustomerName() == "Alice");
            REQUIRE(o.Quantity() == 10);
            REQUIRE(o.Status() == OrderStatus::RESERVED);
        }
        else if (o.Id() == "O-201")
        {
            foundB = true;
            REQUIRE(o.CustomerName() == "Bob");
            REQUIRE(o.Quantity() == 5);
            REQUIRE(o.Status() == OrderStatus::CONFIRMED);
        }
        else if (o.Id() == "O-202")
        {
            foundC = true;
            REQUIRE(o.CustomerName() == "Carol");
            REQUIRE(o.Quantity() == 3);
            REQUIRE(o.Status() == OrderStatus::RESERVED);
        }
    }
    REQUIRE(foundA);
    REQUIRE(foundB);
    REQUIRE(foundC);
}

TEST_CASE("SaveOrder로 신규 주문을 저장하면 기존 파일에 추가된다", "[Repository][Order][SaveOrder]")
{
    TempFileGuard guard("test_orders_saveorder_insert.json");

    std::vector<Order> orders{ Order("O-300", "Alice", "S-001", 10) };
    data::SaveOrders(orders, guard.path);

    Order newOrder("O-301", "Dave", "S-002", 7);
    data::SaveOrder(newOrder, guard.path);

    std::vector<Order> loaded = data::LoadOrders(guard.path);
    REQUIRE(loaded.size() == 2);

    bool found301 = false;
    for (const auto& o : loaded)
    {
        if (o.Id() == "O-301")
        {
            found301 = true;
            REQUIRE(o.CustomerName() == "Dave");
            REQUIRE(o.SampleId() == "S-002");
            REQUIRE(o.Quantity() == 7);
            REQUIRE(o.Status() == OrderStatus::RESERVED);
        }
    }
    REQUIRE(found301);
}

TEST_CASE("ProductionState 목록을 저장 후 다시 로드하면 ISO 8601 시각 문자열이 원본과 정확히 일치한다",
          "[Repository][ProductionState][roundtrip]")
{
    TempFileGuard guard("test_production_state_roundtrip.json");

    std::vector<data::ProductionState> states;
    states.push_back({ "O-400", "2026-07-15T09:00:00", "2026-07-15T10:30:00", 3 });
    states.push_back({ "O-401", "2026-07-15T10:31:00", "2026-07-15T12:01:00", 5 });

    data::SaveProductionState(states, guard.path);
    std::vector<data::ProductionState> loaded = data::LoadProductionState(guard.path);

    REQUIRE(loaded.size() == 2);

    REQUIRE(loaded[0].orderId == "O-400");
    REQUIRE(loaded[0].productionStartAt == "2026-07-15T09:00:00");
    REQUIRE(loaded[0].productionEndAt == "2026-07-15T10:30:00");
    REQUIRE(loaded[0].actualQuantity == 3);

    REQUIRE(loaded[1].orderId == "O-401");
    REQUIRE(loaded[1].productionStartAt == "2026-07-15T10:31:00");
    REQUIRE(loaded[1].productionEndAt == "2026-07-15T12:01:00");
    REQUIRE(loaded[1].actualQuantity == 5);
}

// ---------------------------------------------------------------------------------------------
// 2026-07-15 버그 수정(Red, Phase 12): ProductionState에 shortage 필드가 없어
// ProductionLine::RestoreState가 항상 job.shortage=0으로 복원해버리는 문제(주석 근거가 이제 틀림 —
// SumReservedStockForSample이 shortage를 실제로 사용하게 되었기 때문). ProductionState에 새 필드
// `int shortage = 0;`을 추가하고, SaveProductionState/LoadProductionState가 다른 정수 필드
// (actualQuantity)와 동일한 패턴으로 저장/복원해야 한다.
// ---------------------------------------------------------------------------------------------

TEST_CASE("ProductionState의 shortage 필드는 저장 후 다시 로드해도 정확히 유지된다 (Phase 12, shortage 영속화)",
          "[Repository][ProductionState][roundtrip][shortage][BUG]")
{
    TempFileGuard guard("test_production_state_shortage_roundtrip.json");

    std::vector<data::ProductionState> states;
    // shortage != actualQuantity != 0인 케이스를 포함해 필드가 서로 혼동되지 않는지도 함께 검증한다.
    data::ProductionState stateA;
    stateA.orderId = "O-410";
    stateA.productionStartAt = "2026-07-15T09:00:00";
    stateA.productionEndAt = "2026-07-15T10:30:00";
    stateA.actualQuantity = 6;
    stateA.shortage = 3; // shortage < quantity(원 주문 수량은 여기서 알 수 없으나 임의로 3만 부족했다고 가정)
    states.push_back(stateA);

    data::ProductionState stateB;
    stateB.orderId = "O-411";
    stateB.productionStartAt = "2026-07-15T10:31:00";
    stateB.productionEndAt = "2026-07-15T12:01:00";
    stateB.actualQuantity = 6;
    stateB.shortage = 0; // 전량 자체 생산이 아니라 shortage가 0인(재고로 전부 충당) 극단 케이스
    states.push_back(stateB);

    data::SaveProductionState(states, guard.path);
    std::vector<data::ProductionState> loaded = data::LoadProductionState(guard.path);

    REQUIRE(loaded.size() == 2);

    REQUIRE(loaded[0].orderId == "O-410");
    REQUIRE(loaded[0].actualQuantity == 6);
    REQUIRE(loaded[0].shortage == 3);

    REQUIRE(loaded[1].orderId == "O-411");
    REQUIRE(loaded[1].actualQuantity == 6);
    REQUIRE(loaded[1].shortage == 0);
}
