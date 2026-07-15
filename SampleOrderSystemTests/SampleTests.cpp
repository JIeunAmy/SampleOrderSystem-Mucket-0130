// Phase 2 Red 단계: model_agent가 아직 구현하지 않은 Sample/SampleRepository 인터페이스를
// TDD_Agent가 먼저 설계하고, 이를 검증하는 실패하는 테스트를 작성한다.
//
// 설계한 인터페이스 (model/Sample.h, model_agent가 구현해야 함):
//
//   class Sample {
//   public:
//       // initialStock: 최초 등록 시 재고를 지정할 수 있는 선택적 매개변수. 값을 넘기지 않으면
//       // 기존과 동일하게 0에서 시작한다(하위 호환). initialStock < 0 이면 std::invalid_argument.
//       Sample(std::string id, std::string name, int avgProductionTime, double yieldRate,
//              int initialStock = 0);
//       const std::string& Id() const;
//       const std::string& Name() const;
//       int AvgProductionTime() const;
//       double YieldRate() const;
//       int Stock() const;               // 등록 시 initialStock(기본값 0)에서 시작
//       void IncreaseStock(int amount);   // amount < 0 이면 std::invalid_argument
//       void DecreaseStock(int amount);   // amount < 0 이거나 결과가 음수가 되면 std::invalid_argument
//   };
//   생성자는 yieldRate 가 [0, 1] 범위를 벗어나면 std::invalid_argument 를 던진다.
//   생성자는 initialStock 이 음수이면 std::invalid_argument 를 던진다.
//
//   class SampleRepository {
//   public:
//       void Register(const Sample& sample); // 중복 id면 std::invalid_argument
//       bool Contains(const std::string& id) const;
//       const Sample& Find(const std::string& id) const; // 없으면 std::out_of_range
//   };
//
// 위 시그니처는 model_agent와 조율 대상이며, 구현 시 변경이 필요하면 이 테스트를 함께 갱신한다.

#include "catch_amalgamated.hpp"
#include "model/Sample.h"

TEST_CASE("Sample 생성 시 수율은 0~1 범위여야 한다", "[Sample][yield]")
{
    SECTION("경계값 0은 허용된다")
    {
        REQUIRE_NOTHROW(Sample("S-001", "TestSample", 10, 0.0));
    }

    SECTION("경계값 1은 허용된다")
    {
        REQUIRE_NOTHROW(Sample("S-002", "TestSample", 10, 1.0));
    }

    SECTION("0보다 작은 수율(-0.1)은 거부된다")
    {
        REQUIRE_THROWS_AS(Sample("S-003", "TestSample", 10, -0.1), std::invalid_argument);
    }

    SECTION("1보다 큰 수율(1.1)은 거부된다")
    {
        REQUIRE_THROWS_AS(Sample("S-004", "TestSample", 10, 1.1), std::invalid_argument);
    }
}

TEST_CASE("Sample 등록 시 재고는 0에서 시작한다", "[Sample][stock]")
{
    Sample sample("S-010", "TestSample", 10, 0.9);
    REQUIRE(sample.Stock() == 0);
}

TEST_CASE("Sample 재고 증감 시 음수를 방지한다", "[Sample][stock]")
{
    Sample sample("S-020", "TestSample", 10, 0.9);

    SECTION("정상적인 재고 증가는 허용된다")
    {
        sample.IncreaseStock(5);
        REQUIRE(sample.Stock() == 5);
    }

    SECTION("음수만큼 증가시키는 것은 거부된다")
    {
        REQUIRE_THROWS_AS(sample.IncreaseStock(-1), std::invalid_argument);
    }

    SECTION("재고보다 많은 양을 감소시키면 거부된다")
    {
        sample.IncreaseStock(3);
        REQUIRE_THROWS_AS(sample.DecreaseStock(4), std::invalid_argument);
    }

    SECTION("재고를 정확히 0까지 감소시키는 것은 허용된다(경계값)")
    {
        sample.IncreaseStock(3);
        REQUIRE_NOTHROW(sample.DecreaseStock(3));
        REQUIRE(sample.Stock() == 0);
    }

    SECTION("음수만큼 감소시키는 것은 거부된다")
    {
        sample.IncreaseStock(3);
        REQUIRE_THROWS_AS(sample.DecreaseStock(-1), std::invalid_argument);
    }
}

TEST_CASE("Sample 생성 시 초기 재고를 지정할 수 있다", "[Sample][stock][initialStock]")
{
    SECTION("initialStock 인자를 넘기지 않으면 기존과 동일하게 재고 0에서 시작한다(회귀 확인)")
    {
        Sample sample("S-040", "TestSample", 10, 0.9);
        REQUIRE(sample.Stock() == 0);
    }

    SECTION("initialStock에 양수 값을 넘기면 그 값으로 재고가 시작한다")
    {
        Sample sample("S-041", "TestSample", 10, 0.9, 10);
        REQUIRE(sample.Stock() == 10);
    }

    SECTION("initialStock에 0을 명시적으로 넘겨도 재고는 0에서 시작한다")
    {
        Sample sample("S-042", "TestSample", 10, 0.9, 0);
        REQUIRE(sample.Stock() == 0);
    }

    SECTION("initialStock에 음수를 넘기면 std::invalid_argument를 던진다")
    {
        REQUIRE_THROWS_AS(Sample("S-043", "TestSample", 10, 0.9, -5), std::invalid_argument);
    }

    SECTION("initialStock에 1처럼 작은 양수 값을 넘겨도 정확히 그 값으로 시작한다")
    {
        Sample sample("S-044", "TestSample", 10, 0.9, 1);
        REQUIRE(sample.Stock() == 1);
    }

    SECTION("initialStock에 100처럼 큰 양수 값을 넘겨도 정확히 그 값으로 시작한다")
    {
        Sample sample("S-045", "TestSample", 10, 0.9, 100);
        REQUIRE(sample.Stock() == 100);
    }

    SECTION("initialStock을 0이 아닌 값으로 지정한 뒤 IncreaseStock을 호출하면 initialStock 기준으로 정확히 누적된다")
    {
        Sample sample("S-046", "TestSample", 10, 0.9, 20);
        sample.IncreaseStock(5);
        REQUIRE(sample.Stock() == 25);
    }

    SECTION("initialStock을 0이 아닌 값으로 지정한 뒤 초기 재고보다 많이 DecreaseStock하면 거부된다")
    {
        Sample sample("S-047", "TestSample", 10, 0.9, 20);
        REQUIRE_THROWS_AS(sample.DecreaseStock(21), std::invalid_argument);
        REQUIRE(sample.Stock() == 20);
    }

    SECTION("initialStock을 0이 아닌 값으로 지정한 뒤 그 범위 내에서 DecreaseStock하면 정상적으로 감소한다")
    {
        Sample sample("S-048", "TestSample", 10, 0.9, 20);
        REQUIRE_NOTHROW(sample.DecreaseStock(15));
        REQUIRE(sample.Stock() == 5);
    }
}

TEST_CASE("SampleRepository는 initialStock이 0이 아닌 값으로 생성된 Sample도 정확히 등록/조회한다",
          "[SampleRepository][initialStock]")
{
    SampleRepository repo;
    Sample sample("S-050", "TestSample", 10, 0.9, 20);
    repo.Register(sample);

    SECTION("Find로 얻은 객체의 Stock()이 initialStock 값을 그대로 유지한다")
    {
        const Sample& found = repo.Find("S-050");
        REQUIRE(found.Stock() == 20);
    }

    SECTION("Contains는 등록된 id에 대해 true를 반환한다")
    {
        REQUIRE(repo.Contains("S-050"));
    }
}

TEST_CASE("SampleRepository는 중복된 시료 ID 등록을 거부한다", "[SampleRepository][duplicate]")
{
    SampleRepository repo;
    Sample sample("S-030", "TestSample", 10, 0.9);
    repo.Register(sample);

    REQUIRE(repo.Contains("S-030"));

    Sample duplicate("S-030", "AnotherName", 20, 0.5);
    REQUIRE_THROWS_AS(repo.Register(duplicate), std::invalid_argument);
}
