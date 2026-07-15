// Phase 2 (Model): OrderRepository 테스트
// SampleRepository와 동일한 패턴의 순수 인메모리 등록/조회/검색 API 검증.
// data_agent(영속성)/produce_agent(FIFO 큐 구성)가 이 API 위에 얹을 것이므로,
// 조회로 얻은 참조가 실제 저장된 객체를 가리키는지(상태 변경 반영 여부)도 함께 검증한다.

#include "catch_amalgamated.hpp"
#include "model/Order.h"
#include "model/Sample.h"

TEST_CASE("정상 등록 후 Find로 조회 가능하다", "[OrderRepository][register]")
{
    OrderRepository repo;
    Order order("O-100", "Alice", "S-001", 10);

    repo.Register(order);

    REQUIRE(repo.Contains("O-100"));
    const Order& found = repo.Find("O-100");
    REQUIRE(found.Id() == "O-100");
    REQUIRE(found.CustomerName() == "Alice");
    REQUIRE(found.SampleId() == "S-001");
    REQUIRE(found.Quantity() == 10);
    REQUIRE(found.Status() == OrderStatus::RESERVED);
}

TEST_CASE("중복 orderId 등록 시 예외가 발생한다", "[OrderRepository][register]")
{
    OrderRepository repo;
    Order order1("O-101", "Alice", "S-001", 10);
    Order order2("O-101", "Bob", "S-002", 5);

    repo.Register(order1);

    REQUIRE_THROWS_AS(repo.Register(order2), std::invalid_argument);
}

TEST_CASE("존재하지 않는 orderId를 Find하면 예외가 발생한다", "[OrderRepository][find]")
{
    OrderRepository repo;

    REQUIRE_FALSE(repo.Contains("NO-SUCH-ORDER"));
    REQUIRE_THROWS_AS(repo.Find("NO-SUCH-ORDER"), std::out_of_range);

    const OrderRepository& constRepo = repo;
    REQUIRE_THROWS_AS(constRepo.Find("NO-SUCH-ORDER"), std::out_of_range);
}

TEST_CASE("FindBySampleId로 특정 시료 주문만 필터링된다", "[OrderRepository][search]")
{
    OrderRepository repo;
    repo.Register(Order("O-200", "Alice", "S-001", 10));
    repo.Register(Order("O-201", "Bob", "S-002", 5));
    repo.Register(Order("O-202", "Carol", "S-001", 3));

    auto result = repo.FindBySampleId("S-001");

    REQUIRE(result.size() == 2);
    for (auto* order : result)
    {
        REQUIRE(order->SampleId() == "S-001");
    }
}

TEST_CASE("FindByStatus로 특정 상태 주문만 필터링된다", "[OrderRepository][search]")
{
    OrderRepository repo;
    Sample sample("S-001", "TestSample", 10, 0.9);
    sample.IncreaseStock(20);

    Order reserved("O-300", "Alice", "S-001", 10);
    Order approved("O-301", "Bob", "S-001", 5);
    approved.Approve(sample.Stock()); // 재고 충분 -> CONFIRMED

    repo.Register(reserved);
    repo.Register(approved);

    auto reservedResult = repo.FindByStatus(OrderStatus::RESERVED);
    auto confirmedResult = repo.FindByStatus(OrderStatus::CONFIRMED);

    REQUIRE(reservedResult.size() == 1);
    REQUIRE(reservedResult[0]->Id() == "O-300");
    REQUIRE(confirmedResult.size() == 1);
    REQUIRE(confirmedResult[0]->Id() == "O-301");
}

TEST_CASE("Find로 얻은 참조를 통해 상태를 변경하면 저장소에 실제로 반영된다", "[OrderRepository][mutation]")
{
    OrderRepository repo;
    Sample sample("S-001", "TestSample", 10, 0.9);
    sample.IncreaseStock(20);
    repo.Register(Order("O-400", "Alice", "S-001", 10));

    Order& found = repo.Find("O-400");
    found.Approve(sample.Stock());

    REQUIRE(repo.Find("O-400").Status() == OrderStatus::CONFIRMED);
}

TEST_CASE("All은 전체 주문 목록을 반환한다", "[OrderRepository][search]")
{
    OrderRepository repo;
    repo.Register(Order("O-500", "Alice", "S-001", 10));
    repo.Register(Order("O-501", "Bob", "S-002", 5));

    auto all = repo.All();

    REQUIRE(all.size() == 2);
}
