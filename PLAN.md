# PLAN: 주문 승인 시 가용 재고(availableStock) 계산 버그 수정 계획

> **구현 완료 (2026-07-15)**: 아래 계획은 옵션 B로 채택되어 실제로 구현되었다.
> `Order::Approve(int availableStock)`, `MonitoringService::SumConfirmedQuantity`,
> `MenuController::HandleApprovalOrRejection`의 availableStock 계산 로직이 모두 반영되어
> 전체 테스트(270 assertions/61 test cases)를 통과하며, 실제 실행으로 PLAN.md의 예시 A 시나리오
> (재고 100, CONFIRMED 주문 80 존재 시 신규 주문 30이 정확히 PRODUCING으로 판정됨)가 정확히
> 재현됨을 확인했다.

## 1. 배경

주문 승인(`RESERVED → CONFIRMED`/`PRODUCING`) 시점에 재고 판정 로직이 `Sample::Stock()`(물리적 재고
원본값)만 바라보고 있어, 아래 두 가지 상황을 고려하지 않는 문제가 있다.

1. **이미 CONFIRMED(출고 대기, 아직 미출고)된 다른 주문들의 수량을 제외하지 않는다.**
   `Order::Release(Sample&)`가 실제로 재고를 차감하는 시점은 "출고 처리" 시점이지 `CONFIRMED` 전환
   시점이 아니다(`SKILL.md` "출고(Release) 시 재고 차감" 섹션 참고). 즉 `CONFIRMED` 상태인 주문은
   이미 그 수량만큼 재고를 "예약"해둔 것과 마찬가지인데, 새로운 주문을 승인할 때 이 예약분을 빼지
   않고 원본 `Stock()`을 그대로 비교하면, 여러 주문이 같은 재고를 중복으로 확보하게 된다.

   **예시 A**: Sample 재고=100. 주문 A(수량 80)가 이미 `CONFIRMED` 상태로 출고 대기 중(아직 Release
   안 됨, 재고는 여전히 100). 이때 새 주문 B(수량 30)가 `RESERVED`로 들어와서 승인 처리하면, 현재
   로직은 `sample.Stock()=100 >= 30`이므로 B도 `CONFIRMED`로 판정한다. 하지만 실제 가용 재고는
   100-80=20뿐이므로 B는 사실 `PRODUCING`(부족분 10을 생산)이 되어야 정상이다. 이 버그를 방치하면
   나중에 A와 B를 모두 출고(Release)하려 할 때 재고가 부족해서(A 출고 후 재고 20, B의 30을
   출고하려 하면 `DecreaseStock`이 예외를 던짐) 실패하게 된다.

2. **이미 해당 시료에 대해 `PRODUCING` 상태(생산 라인에 활성 job이 있는 상태)라면, 현재 남아있는
   재고는 이미 그 생산 중인 주문에 귀속된 것으로 간주해야 한다.** 어떤 주문이 `PRODUCING`으로
   판정됐다는 것은, 그 시점에 "가용 재고"가 그 주문의 수량보다 부족했다는 뜻이고, 그 주문의
   부족분(shortage) 계산은 "가용 재고 전부를 그 주문이 쓴다"는 전제로 이루어진다(즉 가용 재고를
   다 써도 부족한 만큼만 생산). 따라서 그 주문이 아직 생산 중(`PRODUCING`, 아직 `CONFIRMED`로
   전환 안 됨)인 동안에는, 같은 시료에 대한 다른 어떤 신규 주문도 현재 가용 재고를 나눠 가질 수
   없다 — 그 재고는 이미 앞서 생산 중인 주문의 계획에 전부 포함되어 있기 때문이다. 따라서 이 기간
   동안 도착하는 새 주문은 가용 재고 계산과 무관하게 **무조건 `PRODUCING`으로 판정되어야 하고,
   그 주문의 부족분(shortage)은 가용 재고를 전혀 고려하지 않은 채 주문 수량 전체**여야 한다(FIFO
   큐에서 앞선 job 뒤에 대기).

   **예시 B**: Sample 재고=10, 수율=1.0(단순화). 주문 A(수량 50)가 승인되어 가용재고(10) < 50이므로
   `PRODUCING` 판정, shortage=40, 실 생산량=40이 생산 큐에 등록됨(A는 최종적으로 재고
   10(기존)+40(생산)=50을 확보할 계획). A가 아직 생산 중인 상태에서 주문 B(수량 3)가 `RESERVED`로
   들어와 승인 처리되면, 현재 로직은 `sample.Stock()=10 >= 3`이므로 B를 `CONFIRMED`로 판정해버린다.
   하지만 이 10의 재고는 이미 A의 계획에 전부 포함되어 있으므로(A가 최종적으로 50을 확보하려면 이
   10이 반드시 필요), B가 이 중 3을 가져가면 나중에 A를 출고하려 할 때 재고가 부족해진다(A 완료
   시 재고=10+40=50이 될 것으로 예상했지만, B가 이미 3을 `CONFIRMED`로 가져가 출고까지 마쳤다면
   실제로는 50-3=47만 남아 A의 50 출고가 실패한다). 따라서 B는 마땅히 `PRODUCING`으로 판정되어
   (재고 0 취급, shortage=3 전체) FIFO 큐에서 A 뒤에 대기해야 한다.

사용자 요약: "새로 들어온 reserved order에 대해 confirmed/producing을 결정하는 것은
1) 해당 sample에 대해 confirmed된 양 제외, 2) producing에 진입한다면 지금 남아있는 양은 현재
producing order에 종속" — 위 배경 설명과 정확히 일치하는 요구사항이다.

## 2. 현재 구현의 한계 (실제 코드 인용)

`SampleOrderSystem/SampleOrderSystem/model/Order.h`의 `Order::Approve`:

```cpp
void Approve(const Sample& sample)
{
    if (status_ != OrderStatus::RESERVED)
    {
        throw std::logic_error("Order can only be approved from RESERVED status");
    }

    status_ = (sample.Stock() >= quantity_) ? OrderStatus::CONFIRMED : OrderStatus::PRODUCING;
}
```

`SampleOrderSystem/SampleOrderSystem/controller/MenuController.h`의 `HandleApprovalOrRejection()`:

```cpp
if (choice == 1)
{
    const Sample& sample = samples_.Find(order.SampleId());
    int shortage = order.Quantity() - sample.Stock();

    order.Approve(sample);
    if (order.Status() == OrderStatus::PRODUCING)
    {
        productionLine_.Enqueue(order.Id(), order.SampleId(), shortage, samples_.Find(order.SampleId()));
        SaveProductionState();
    }

    data::SaveOrder(order);
    view_.ShowApprovalResult(order);
}
```

- `Order::Approve(const Sample&)`는 인자로 받은 `Sample`의 원본 `Stock()`만 보고 `CONFIRMED`/
  `PRODUCING`을 가른다. `CONFIRMED`로 대기 중인 다른 주문의 수량이나, 같은 시료에 대해 이미
  `PRODUCING` 중인 job이 있는지는 전혀 알지 못한다(애초에 `Order`/`Sample` 클래스는
  `OrderRepository`나 `ProductionLine`을 참조하지 않는 구조이므로 알 수 없다).
- `HandleApprovalOrRejection()`의 `shortage` 계산(`order.Quantity() - sample.Stock()`) 역시 동일한
  한계를 갖는다. `sample.Stock()`을 그대로 사용하므로, 이미 다른 주문이 그 재고를 예약해둔 상태
  또는 이미 활성 `PRODUCING` job이 있는 상태를 반영하지 못한다.
- 참고로 `MonitoringService.h`의 `SumUndeliveredDemand`는 이미 "PRODUCING/CONFIRMED 주문 수량
  합계"를 계산하는 헬퍼를 갖고 있으나(모니터링 목적), 이는 재고 상태(SURPLUS/SHORTAGE/DEPLETED)
  판정용이지 승인 시점의 availableStock 계산에는 사용되고 있지 않다.
- `ProductionLine::CurrentJob(sampleId)`는 이미 특정 시료에 대한 활성 job 유무를
  `std::optional<ProductionJob>`으로 조회할 수 있는 API를 제공하므로, "활성 PRODUCING job 존재
  여부" 판정에 재사용 가능하다.

## 3. 수정 방향 (제안)

### 3.1 "가용 재고(availableStock)" 개념 도입

```
availableStock = sample.Stock() - (해당 sampleId에 대해 CONFIRMED 상태이고
                                    아직 RELEASE되지 않은 모든 주문의 수량 합계)
```

- 만약 해당 `sampleId`에 대해 `ProductionLine::CurrentJob(sampleId)`가 값을 갖는다면(활성 job이
  존재), `availableStock`을 0으로 간주한다(또는 그와 동등하게 무조건 `PRODUCING`으로 분기하는
  별도 조건으로 처리해도 무방 — 의미상 동일).
- 승인 판정: `availableStock >= order.Quantity()` 이면 `CONFIRMED`, 아니면 `PRODUCING`.
- `PRODUCING`일 때 `shortage = order.Quantity() - availableStock`
  (활성 job이 있어 `availableStock=0`으로 취급된 경우 `shortage`는 `order.Quantity()` 전체가 됨).

### 3.2 설계 옵션 (계층 분리 원칙 유지)

`Order`/`Sample` 클래스는 `OrderRepository`나 `ProductionLine`을 직접 참조하지 않는 현재 계층
분리 원칙(Model은 상태 전이만 소유, 저장소/큐 조회는 Controller가 담당)을 유지해야 한다. 이를
지키기 위한 두 가지 옵션을 제시한다.

**옵션 A — `Approve` 시그니처를 값 기반으로 변경** (미채택)

Controller(`MenuController::HandleApprovalOrRejection`)가 `orders_.FindByStatus(OrderStatus::CONFIRMED)`
와 `productionLine_.CurrentJob(sampleId)`를 조회해 `availableStock`을 계산한 뒤, `Order::Approve`에
계산된 값을 직접 전달하도록 시그니처를 변경한다. 예:

```cpp
// Order.h
void Approve(int availableStock)
{
    if (status_ != OrderStatus::RESERVED) { ... }
    status_ = (availableStock >= quantity_) ? OrderStatus::CONFIRMED : OrderStatus::PRODUCING;
}
```

또는 재고/예약분/생산중 여부를 그대로 넘기고 Order 내부에서 availableStock을 계산하게 할 수도
있다: `Approve(const Sample& sample, int reservedForOtherOrders, bool sampleCurrentlyProducing)`.
어느 쪽이든 "판정 로직 자체"(CONFIRMED vs PRODUCING을 가르는 비교식)는 여전히 `Order`(Model)가
소유하되, "CONFIRMED 주문 합계 조회"와 "생산 중 여부 조회"라는 저장소/큐 접근은 Controller가
대신 수행해서 순수 값으로 넘겨준다.

**옵션 B — `MonitoringService`에 헬퍼 함수 추가 후 Controller가 조합** (채택 및 구현 완료)

`MonitoringService.h`의 기존 `SumUndeliveredDemand` 패턴을 참고해, "특정 시료의 CONFIRMED 합계"만
구하는 새 순수 함수를 추가한다. 옵션 B는 계산 함수를 `MonitoringService`에 명확히 분리해
재사용성과 테스트 용이성을 높인다는 점에서 옵션 A보다 낫다고 판단되어 최종 채택되었다.

아래는 실제로 구현된 최종 코드다(2026-07-15 기준 실제 소스 인용).

`SampleOrderSystem/SampleOrderSystem/model/MonitoringService.h`:

```cpp
// 확정(CONFIRMED) 수량 합계: 특정 sampleId에 대해 CONFIRMED 상태 주문의
// 수량(Quantity())을 모두 합산한다. (다른 상태 및 다른 sampleId는 제외)
inline int SumConfirmedQuantity(const std::vector<Order>& orders, const std::string& sampleId)
{
    int total = 0;

    for (const auto& order : orders)
    {
        if (order.SampleId() != sampleId)
        {
            continue;
        }

        if (order.Status() == OrderStatus::CONFIRMED)
        {
            total += order.Quantity();
        }
    }

    return total;
}
```

`SampleOrderSystem/SampleOrderSystem/model/Order.h`의 `Order::Approve`(값 기반 시그니처로 변경됨):

```cpp
void Approve(int availableStock)
{
    if (status_ != OrderStatus::RESERVED)
    {
        throw std::logic_error("Order can only be approved from RESERVED status");
    }

    status_ = (availableStock >= quantity_) ? OrderStatus::CONFIRMED : OrderStatus::PRODUCING;
}
```

`SampleOrderSystem/SampleOrderSystem/controller/MenuController.h`의 `HandleApprovalOrRejection()`
승인(choice == 1) 분기(Controller가 `SumConfirmedQuantity`와 `productionLine_.CurrentJob(sampleId)`를
조합해 `availableStock`을 계산한 뒤 `Order::Approve`에 값으로 전달):

```cpp
if (choice == 1)
{
    const Sample& sample = samples_.Find(order.SampleId());

    bool hasActiveJob = productionLine_.CurrentJob(order.SampleId()).has_value();
    int availableStock;
    if (hasActiveJob)
    {
        availableStock = 0;
    }
    else
    {
        std::vector<Order> allOrders = CollectAllOrders();
        int confirmedQty = SumConfirmedQuantity(allOrders, order.SampleId());
        availableStock = sample.Stock() - confirmedQty;
    }
    int shortage = order.Quantity() - availableStock;

    order.Approve(availableStock);
    if (order.Status() == OrderStatus::PRODUCING)
    {
        productionLine_.Enqueue(order.Id(), order.SampleId(), shortage, samples_.Find(order.SampleId()));
        SaveProductionState();
    }

    data::SaveOrder(order);
    view_.ShowApprovalResult(order);
}
```

이 구현은 `hasActiveJob`(같은 시료에 대한 활성 `PRODUCING` job 존재 여부)이 true이면
`availableStock`을 0으로 강제해 무조건 `PRODUCING` 판정 + `shortage = order.Quantity()` 전체가
되도록 하고, 그렇지 않은 경우에만 `sample.Stock() - confirmedQty`로 CONFIRMED 예약분을 제외한
가용 재고를 계산하도록 하여 계획 문서의 "3.1 availableStock 개념"을 그대로 코드로 반영했다.

**공통 원칙**: 두 옵션 중 어느 쪽이든 **계층 분리 원칙(Model은 상태 전이만, 저장소/큐 조회는
Controller가 담당)을 지켜야 한다.** `Order`/`Sample` 클래스가 `OrderRepository`나
`ProductionLine` 헤더를 include하거나 그 인스턴스를 참조하는 일은 없어야 한다.

## 4. 영향 범위

- `SampleOrderSystem/SampleOrderSystem/model/Order.h`
  - `Approve` 시그니처 변경(`const Sample&` → `int availableStock` 또는 유사한 값 기반 인자).
- `SampleOrderSystem/SampleOrderSystem/model/MonitoringService.h`
  - (옵션 B 채택 시) `SumConfirmedQuantity(const std::vector<Order>&, const std::string&)` 신규
    헬퍼 함수 추가.
- `SampleOrderSystem/SampleOrderSystem/controller/MenuController.h`
  - `HandleApprovalOrRejection()`에서 `availableStock` 계산 로직 추가
    (`orders_.FindByStatus(OrderStatus::CONFIRMED)` 순회 또는 `SumConfirmedQuantity` 호출 +
    `productionLine_.CurrentJob(order.SampleId())` 조회), `shortage` 계산식 변경, `order.Approve(...)`
    호출부 변경.
- 테스트
  - `SampleOrderSystemTests/OrderTests.cpp`: `Order::Approve`의 새 시그니처에 맞춘 단위 테스트
    갱신 및 신규 시나리오 추가.
  - 통합 시나리오 테스트(Controller 레벨 또는 별도 통합 테스트 파일) 추가 필요성: 예시 A/B와 같은
    다중 주문 시나리오를 end-to-end로 검증하는 테스트가 없다면 신설을 검토한다.

## 5. TDD 필요 여부

이 변경은 **핵심 도메인 계산/상태 전이 로직**(주문 승인 판정, 부족분 계산)이므로 CLAUDE.md의
TDD_Agent 운영 원칙에 따라 반드시 **TDD Red-Green 루프**로 진행해야 한다. 구현 착수 전에 아래
시나리오에 대해 먼저 실패하는 테스트를 작성한다.

- **경계값 포함 CONFIRMED 제외 검증**: 같은 시료에 대해 이미 `CONFIRMED` 주문(수량 X)이 있는
  상태에서 신규 주문 승인 시, `availableStock = stock - X`를 기준으로 `availableStock >= quantity`
  이면 `CONFIRMED`, `availableStock < quantity`이면 `PRODUCING`으로 정확히 갈리는지(특히
  `availableStock == quantity`인 경계값 케이스 포함).
- **활성 PRODUCING job 존재 시 무조건 PRODUCING 판정**: 같은 시료에 대해
  `ProductionLine::CurrentJob(sampleId)`가 값을 가질 때, 남은 물리적 재고가 신규 주문 수량보다
  많더라도 신규 주문이 무조건 `PRODUCING`으로 판정되는지, 그리고 이때 `shortage`가 정확히
  `order.Quantity()` 전체(가용 재고를 전혀 차감하지 않은 값)와 같은지.
- **다중 CONFIRMED 주문 합산 검증**: 같은 시료에 대해 `CONFIRMED` 주문이 2건 이상 겹쳐 있을 때,
  그 수량 합계가 정확히 `availableStock` 계산에서 제외되는지(예시 A의 확장 케이스).
- **회귀 테스트**: 활성 `PRODUCING` job도 없고 `CONFIRMED` 주문도 없는 기존 단순 케이스(현재
  `Order::Approve(const Sample&)` 동작과 동일한 결과가 나와야 하는 케이스)가 새 로직에서도 회귀
  없이 그대로 통과하는지.
- **Reject 및 상태 전이 가드 회귀**: `Approve` 시그니처 변경이 `RESERVED`가 아닌 상태에서 호출 시
  예외를 던지는 기존 가드 로직에 영향을 주지 않는지.

## 6. 요약

이 계획은 모두 구현 완료되었다.

## 7. 구현 완료 요약

이 계획(옵션 B)은 TDD Red-Green 루프로 실제 구현까지 완료되었다. 아래는 최종 반영 내역이다.

### 7.1 변경/신설된 소스 파일

- `SampleOrderSystem/SampleOrderSystem/model/Order.h`
  - `Approve(const Sample&)` → `Approve(int availableStock)`로 시그니처 변경. 판정 로직
    (`availableStock >= quantity_` → `CONFIRMED`, 아니면 `PRODUCING`)은 여전히 `Order`(Model)가
    소유한다.
- `SampleOrderSystem/SampleOrderSystem/model/MonitoringService.h`
  - `SumConfirmedQuantity(const std::vector<Order>&, const std::string&)` 신규 헬퍼 함수 추가
    (특정 sampleId의 CONFIRMED 주문 수량 합계 계산).
- `SampleOrderSystem/SampleOrderSystem/controller/MenuController.h`
  - `HandleApprovalOrRejection()`의 승인(choice == 1) 분기에서
    `productionLine_.CurrentJob(order.SampleId())`로 활성 job 여부를 먼저 확인하고, 활성 job이
    있으면 `availableStock = 0`, 없으면 `sample.Stock() - SumConfirmedQuantity(...)`로
    `availableStock`을 계산한 뒤 `order.Approve(availableStock)`을 호출하도록 갱신.
    `shortage = order.Quantity() - availableStock`도 함께 갱신.

### 7.2 갱신/신설된 테스트 파일

- `SampleOrderSystemTests/OrderTests.cpp`: `Order::Approve`의 새 시그니처(`int availableStock`)에
  맞춘 단위 테스트 갱신 및 경계값(`availableStock == quantity`) 케이스 추가.
- `SampleOrderSystemTests/MonitoringTests.cpp`: `SumConfirmedQuantity`에 대한 신규 단위 테스트
  추가(단일/다중 CONFIRMED 주문 합산, 다른 상태/다른 sampleId 제외 검증 포함).
- `SampleOrderSystemTests/ApprovalIntegrationTests.cpp` (신규): 예시 A(CONFIRMED 예약분 제외)와
  예시 B(활성 PRODUCING job 존재 시 무조건 PRODUCING 판정) 시나리오를 Controller 레벨 end-to-end로
  검증하는 통합 테스트를 신설.
- `SampleOrderSystemTests/ProductionLineTests.cpp`, `SampleOrderSystemTests/OrderRepositoryTests.cpp`,
  `SampleOrderSystemTests/RepositoryTests.cpp`: 기존에 `Order::Approve(const Sample&)`를 호출하던
  테스트 코드들을 새 시그니처(`Order::Approve(int availableStock)`)에 맞춰 호출부를 갱신(회귀 없이
  통과 확인).
- `SampleOrderSystem/SampleOrderSystem/data/Repository.h`의 `ReconstructOrder`: 영속화된 주문을
  복원할 때도 새 `Approve` 시그니처와 일관되게 상태 재구성 로직을 갱신.

### 7.3 최종 검증 결과

- 전체 테스트 스위트 실행 결과: **270 assertions, 61 test cases 모두 통과**.
- 실제 실행(콘솔 애플리케이션)으로 PLAN.md 예시 A 시나리오를 재현: Sample 재고 100, 주문 A(수량
  80)가 이미 `CONFIRMED` 상태로 대기 중인 상태에서 신규 주문 B(수량 30)를 승인하면
  `availableStock = 100 - 80 = 20 < 30`이므로 B가 정확히 `PRODUCING`으로 판정되고, `shortage = 10`이
  생산 큐에 등록됨을 확인했다.
- 회귀 테스트(활성 `PRODUCING` job도 없고 `CONFIRMED` 주문도 없는 기존 단순 케이스)도 기존과 동일한
  결과로 통과함을 확인해, 이번 변경이 기존 정상 흐름에 영향을 주지 않았음을 검증했다.
