// Phase 2 Red 단계: model_agent가 아직 구현하지 않은 Order 인터페이스를
// TDD_Agent가 먼저 설계하고, 이를 검증하는 실패하는 테스트를 작성한다.
//
// 설계한 인터페이스 (model/Order.h, model_agent가 구현해야 함):
//
//   enum class OrderStatus { RESERVED, REJECTED, PRODUCING, CONFIRMED, RELEASE };
//
//   class Order {
//   public:
//       Order(std::string orderId, std::string customerName, std::string sampleId, int quantity);
//       // 생성 시 상태는 RESERVED
//
//       const std::string& Id() const;
//       const std::string& CustomerName() const;
//       const std::string& SampleId() const;
//       int Quantity() const;
//       OrderStatus Status() const;
//
//       // 승인 처리: sample의 현재 재고와 주문 수량을 비교해 CONFIRMED 또는 PRODUCING으로 전이
//       // (재고 >= 수량이면 CONFIRMED, 아니면 PRODUCING). RESERVED 상태가 아니면 std::logic_error.
//       void Approve(const Sample& sample);
//
//       void Reject(); // RESERVED가 아니면 std::logic_error
//
//       // 생산 완료 반영: produce_agent가 계산한 "실 생산량"(producedQuantity)만큼 sample 재고를
//       // 증가시키고 주문 상태를 PRODUCING -> CONFIRMED로 전환한다. PRODUCING이 아니면 std::logic_error.
//       // (주문 수량이 아니라 produce_agent가 계산한 실 생산량을 그대로 반영하는 이유: 실 생산량은
//       //  ceil(부족분/수율)로 계산되어 요청 예제의 quantity_와 다를 수 있으므로, model_agent는 계산된
//       //  값을 그대로 반영하는 API만 제공하고 계산 자체는 produce_agent 책임으로 둔다)
//       void CompleteProduction(Sample& sample, int producedQuantity);
//
//       // 출고 처리: sample 재고를 quantity_만큼 차감한 뒤(재고 부족 시 Sample::DecreaseStock이
//       // std::invalid_argument를 던지고, 이 경우 상태는 CONFIRMED로 유지된다) 상태를 RELEASE로 전환한다.
//       // CONFIRMED가 아니면 std::logic_error (재고 차감 시도 이전에 상태부터 검사하며, 재고는 변하지 않는다).
//       void Release(Sample& sample);
//   };
//
// 위 시그니처는 model_agent와 조율 대상이며, 구현 시 변경이 필요하면 이 테스트를 함께 갱신한다.
// (task 지시문의 CompleteProduction(Sample&) 예시에서 producedQuantity 매개변수를 추가하도록 조정함 —
//  이유는 위 주석 참고. quantity_만큼만 증가시키면 실 생산량과 주문 수량이 다른 경우를 반영할 수 없다.)
//
// 2026-07-15 변경(Red): 사용자가 "출고 시 창고 재고를 실제로 차감해야 한다"는 요구사항을 추가함에 따라
// Release()의 시그니처를 Release(Sample&)로 변경한다. CompleteProduction(Sample&, int)과 동일한 패턴으로,
// 재고 차감(DecreaseStock)을 먼저 시도해 성공한 뒤에만 상태를 RELEASE로 전환해야 한다(재고 부족 시 예외가
// 그대로 전파되고 상태는 CONFIRMED로 남아야 함).

#include "catch_amalgamated.hpp"
#include "model/Order.h"
#include "model/Sample.h"

TEST_CASE("Order 생성 시 상태는 RESERVED이다", "[Order][state]")
{
    Order order("O-001", "Alice", "S-001", 10);

    REQUIRE(order.Id() == "O-001");
    REQUIRE(order.CustomerName() == "Alice");
    REQUIRE(order.SampleId() == "S-001");
    REQUIRE(order.Quantity() == 10);
    REQUIRE(order.Status() == OrderStatus::RESERVED);
}

TEST_CASE("RESERVED 주문을 승인하면 재고 비교 결과에 따라 CONFIRMED 또는 PRODUCING으로 전이한다", "[Order][approve]")
{
    SECTION("재고가 주문 수량보다 많으면 CONFIRMED")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        sample.IncreaseStock(20);
        Order order("O-002", "Alice", "S-001", 10);

        order.Approve(sample);

        REQUIRE(order.Status() == OrderStatus::CONFIRMED);
    }

    SECTION("재고가 주문 수량과 정확히 같으면 CONFIRMED(경계값)")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        sample.IncreaseStock(10);
        Order order("O-003", "Alice", "S-001", 10);

        order.Approve(sample);

        REQUIRE(order.Status() == OrderStatus::CONFIRMED);
    }

    SECTION("재고가 주문 수량보다 적으면 PRODUCING")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        sample.IncreaseStock(5);
        Order order("O-004", "Alice", "S-001", 10);

        order.Approve(sample);

        REQUIRE(order.Status() == OrderStatus::PRODUCING);
    }

    SECTION("재고가 0이면(고갈) PRODUCING")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        Order order("O-005", "Alice", "S-001", 10);

        order.Approve(sample);

        REQUIRE(order.Status() == OrderStatus::PRODUCING);
    }
}

TEST_CASE("RESERVED 주문을 거절하면 REJECTED로 전이한다", "[Order][reject]")
{
    Order order("O-006", "Alice", "S-001", 10);

    order.Reject();

    REQUIRE(order.Status() == OrderStatus::REJECTED);
}

TEST_CASE("허용되지 않는 상태 전이는 거부된다", "[Order][invalid-transition]")
{
    SECTION("REJECTED 상태에서 재승인 시도는 거부된다")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        sample.IncreaseStock(20);
        Order order("O-007", "Alice", "S-001", 10);
        order.Reject();

        REQUIRE_THROWS_AS(order.Approve(sample), std::logic_error);
    }

    SECTION("REJECTED 상태에서 재거절 시도는 거부된다")
    {
        Order order("O-008", "Alice", "S-001", 10);
        order.Reject();

        REQUIRE_THROWS_AS(order.Reject(), std::logic_error);
    }

    SECTION("이미 CONFIRMED 상태에서 재승인 시도는 거부된다")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        sample.IncreaseStock(20);
        Order order("O-009", "Alice", "S-001", 10);
        order.Approve(sample);

        REQUIRE_THROWS_AS(order.Approve(sample), std::logic_error);
    }

    SECTION("RESERVED 상태에서 생산 완료 반영을 시도하면 거부된다(PRODUCING 아님)")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        Order order("O-010", "Alice", "S-001", 10);

        REQUIRE_THROWS_AS(order.CompleteProduction(sample, 10), std::logic_error);
    }

    SECTION("CONFIRMED 상태에서 생산 완료 반영을 시도하면 거부된다(PRODUCING 아님)")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        sample.IncreaseStock(20);
        Order order("O-011", "Alice", "S-001", 10);
        order.Approve(sample);

        REQUIRE_THROWS_AS(order.CompleteProduction(sample, 10), std::logic_error);
    }

    SECTION("RESERVED 상태에서 출고 처리를 시도하면 거부된다(CONFIRMED 아님)")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        sample.IncreaseStock(20);
        Order order("O-012", "Alice", "S-001", 10);

        REQUIRE_THROWS_AS(order.Release(sample), std::logic_error);
        REQUIRE(sample.Stock() == 20);
        REQUIRE(order.Status() == OrderStatus::RESERVED);
    }

    SECTION("PRODUCING 상태에서 출고 처리를 시도하면 거부된다(CONFIRMED 아님)")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        Order order("O-014", "Alice", "S-001", 10);
        order.Approve(sample); // 재고 0 < 10 -> PRODUCING

        REQUIRE_THROWS_AS(order.Release(sample), std::logic_error);
        REQUIRE(sample.Stock() == 0);
        REQUIRE(order.Status() == OrderStatus::PRODUCING);
    }

    SECTION("REJECTED 상태에서 출고 처리를 시도하면 거부된다(CONFIRMED 아님)")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        sample.IncreaseStock(20);
        Order order("O-015", "Alice", "S-001", 10);
        order.Reject();

        REQUIRE_THROWS_AS(order.Release(sample), std::logic_error);
        REQUIRE(sample.Stock() == 20);
        REQUIRE(order.Status() == OrderStatus::REJECTED);
    }

    SECTION("RELEASE 상태에서 재출고 시도는 거부된다")
    {
        Sample sample("S-001", "TestSample", 10, 0.9);
        sample.IncreaseStock(20);
        Order order("O-013", "Alice", "S-001", 10);
        order.Approve(sample);
        order.Release(sample);
        REQUIRE(sample.Stock() == 10);

        REQUIRE_THROWS_AS(order.Release(sample), std::logic_error);
        REQUIRE(sample.Stock() == 10);
        REQUIRE(order.Status() == OrderStatus::RELEASE);
    }
}

TEST_CASE("생산 완료 반영 API는 PRODUCING 주문을 CONFIRMED로 전환하면서 Sample 재고를 실 생산량만큼 증가시킨다", "[Order][production][integration]")
{
    Sample sample("S-001", "TestSample", 10, 0.9);
    // 재고 0에서 시작 -> 주문 5개 승인 시 재고 부족으로 PRODUCING 전이
    Order order("O-020", "Alice", "S-001", 5);
    order.Approve(sample);
    REQUIRE(order.Status() == OrderStatus::PRODUCING);
    REQUIRE(sample.Stock() == 0);

    // produce_agent가 계산한 실 생산량(예: ceil(5/0.9) = 6)을 그대로 반영한다고 가정
    order.CompleteProduction(sample, 6);

    REQUIRE(order.Status() == OrderStatus::CONFIRMED);
    REQUIRE(sample.Stock() == 6);
}

TEST_CASE("CONFIRMED 주문을 출고 처리하면 RELEASE로 전이하고 Sample 재고를 주문 수량만큼 차감한다", "[Order][release]")
{
    Sample sample("S-001", "TestSample", 10, 0.9);
    sample.IncreaseStock(20);
    Order order("O-030", "Alice", "S-001", 10);
    order.Approve(sample);
    REQUIRE(order.Status() == OrderStatus::CONFIRMED);

    order.Release(sample);

    REQUIRE(order.Status() == OrderStatus::RELEASE);
    REQUIRE(sample.Stock() == 10);
}

TEST_CASE("출고 처리 시 재고가 주문 수량보다 부족하면 예외가 전파되고 주문 상태는 CONFIRMED로 유지된다", "[Order][release][shortage]")
{
    Sample sample("S-001", "TestSample", 10, 0.9);
    sample.IncreaseStock(20);
    Order order("O-031", "Alice", "S-001", 10);
    order.Approve(sample);
    REQUIRE(order.Status() == OrderStatus::CONFIRMED);

    // 승인 이후 재고가 외부 요인으로 줄어들어 출고 시점에는 주문 수량보다 부족한 상황을 재현한다.
    sample.DecreaseStock(15); // 20 -> 5, 남은 재고 5 < 주문 수량 10

    REQUIRE_THROWS_AS(order.Release(sample), std::invalid_argument);
    REQUIRE(order.Status() == OrderStatus::CONFIRMED);
    REQUIRE(sample.Stock() == 5);
}
