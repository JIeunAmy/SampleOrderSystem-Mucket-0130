# TDD_Agent 작업 Phase

## Phase 1 — TDD 대상 식별 (각 계층 phase 문서 작성 직후, 실제 구현 착수 전)

`docs_temp/model/phase.md`, `docs_temp/produce/phase.md`, `docs_temp/data/phase.md`,
`docs_temp/controller/phase.md`, `docs_temp/view/phase.md`와 Phase 0에서 만들어진 스텁 헤더
(`SampleOrderSystem/SampleOrderSystem/model/Sample.h`, `model/Order.h`, `data/Repository.h`,
`produce/ProductionLine.h`)를 대조해 TDD 대상 클래스를 아래와 같이 식별했다.

> **반드시 지킬 것**: 아래 "TDD 필요" 표에 있는 클래스는 실제 구현(model_agent/produce_agent/data_agent)
> 착수 "전"에 TDD_Agent가 실패하는 테스트(Red)를 먼저 작성한다. 구현 에이전트는 그 테스트를 통과시키는
> 방향으로만 작업하고(Green), 테스트를 지우거나 조건을 완화해 통과시키지 않는다. 구현 에이전트가 API
> 시그니처 변경이 필요하다고 판단하면, 먼저 TDD_Agent와 시그니처를 재조율한 뒤 테스트를 갱신하고 나서
> 구현을 진행한다. **"이 클래스는 Red-Green 사이클로 진행되니 구현 전에 반드시 TDD_Agent에게 실패하는
> 테스트 작성을 먼저 요청하라"** — model_agent(Phase 2)/data_agent(Phase 3)/produce_agent(Phase 4)는
> 각자 담당 Phase에 진입하기 전 이 문서를 다시 확인한다.

### TDD 필요 (우선순위 높음) — 입력에 따라 결과가 갈리는 순수 로직

| 클래스/모듈 | 계층 | 테스트해야 할 핵심 시나리오 |
|---|---|---|
| `Sample` | model | 수율 0~1 범위 검증(경계값 0, 1, 범위 밖 -0.1/1.1 거부), 등록 시 재고 0 시작, 재고 증감 시 음수 방지, **중복 시료 ID 등록 거부**(model_agent phase.md 검증 항목 6), **초기 재고 지정 생성자**(요구사항 변경, 2026-07-15): `Sample(id, name, avgProductionTime, yieldRate, int initialStock = 0)` — 5번째 인자를 생략하면 기존과 동일하게 재고 0에서 시작(하위 호환), 양수를 넘기면 그 값으로 시작, 0을 명시적으로 넘겨도 0에서 시작, 음수를 넘기면 std::invalid_argument. Red 테스트: `SampleOrderSystemTests/SampleTests.cpp`의 "Sample 생성 시 초기 재고를 지정할 수 있다" 테스트 케이스(2026-07-15 Red 확인 — 기존 생성자가 5개 인자를 받지 않아 C2661/C2440 컴파일 실패). model_agent가 이 시그니처로 구현하면 Green 전환. |
| `Order` | model | 상태 전이 규칙 전부(RESERVED→CONFIRMED/PRODUCING, RESERVED→REJECTED, PRODUCING→CONFIRMED, CONFIRMED→RELEASE), 허용되지 않는 전이 시도(REJECTED에서 재승인/재거절, CONFIRMED에서 재승인, RESERVED/CONFIRMED에서 생산완료 반영 시도, RESERVED에서 출고 시도, RELEASE에서 재출고) 거부, **생산 완료 반영 API 통합 시나리오**(PRODUCING 주문에 대해 완료 반영 API 호출 시 주문 상태가 CONFIRMED로 바뀌는 동시에 해당 Sample 재고가 실 생산량만큼 증가하는지 한 번에 검증). 확정 시그니처: `Order(orderId, customerName, sampleId, quantity)`, `Approve(const Sample&)`(재고>=수량이면 CONFIRMED, 아니면 PRODUCING; RESERVED 아니면 std::logic_error), `Reject()`(RESERVED 아니면 std::logic_error), `CompleteProduction(Sample&, int producedQuantity)`(PRODUCING 아니면 std::logic_error; produce_agent가 계산한 실 생산량을 그대로 Sample 재고에 반영 — quantity_가 아닌 producedQuantity를 받도록 조정한 이유는 실 생산량 `ceil(부족분/수율)`이 주문 수량과 다를 수 있기 때문), **`Release(Sample& sample)`**(2026-07-15 변경, Phase 6: 기존 무인자 `Release()`에서 변경 — CONFIRMED 아니면 std::logic_error(재고 불변), CONFIRMED이면 `sample.DecreaseStock(quantity_)`를 먼저 시도해 성공한 뒤에만 상태를 RELEASE로 전환하며, 재고 부족 시 `Sample::DecreaseStock`의 `std::invalid_argument`가 그대로 전파되고 상태는 CONFIRMED로 유지됨 — "모니터링 재고는 출고 전 값" 요구사항에 따라 출고 시 실제 창고 재고 차감을 반영). Red 테스트: `SampleOrderSystemTests/OrderTests.cpp` (2026-07-15 Phase 6 Red 확인 — `model/Order.h`가 아직 무인자 `Release()`라 C2660 컴파일 오류 9건) |
| `ProductionLine`(FIFO 스케줄러) + 생산 실시간(wall-clock) 판정 로직 | produce | 실 생산량 `ceil(부족분/수율)` 경계값(나누어떨어짐/안 떨어짐), 총 생산 시간 계산, FIFO 순서 보장(먼저 들어온 주문 먼저 생산), **생산 큐 등록 API 분기**(동일 시료가 이미 생산 중이면 대기열에 추가만 되고, 아니면 즉시 생산 시작), 한 번에 하나의 시료만 생산되는지(다른 시료 주문이 동시에 등록돼도 즉시 시작), 생산 시작 직후 조회 시 미완료, 총 생산 시간 정확히 경과 시 완료(경계값, Order CONFIRMED 전환 + Sample 재고 반영 동시 검증), 총 생산 시간 절반만 경과 시 미완료 유지, 완료 시 FIFO 대기열의 다음 job이 즉시 시작되는지, **앱 재시작 후에도 실제 경과 시간 기준으로 완료 판정**(ExportState로 저장한 시작/예상 완료 시각을 새 인스턴스에 RestoreState로 복원한 뒤, 이미 경과 시각이 지났으면 즉시 완료 처리, 아직 안 지났으면 대기 상태 유지), ISO 8601 `FormatTimePoint`/`ParseTimePoint` 왕복 일치. Red 테스트: `SampleOrderSystemTests/ProductionLineTests.cpp` (2026-07-15 Red 확인 — `produce/ProductionLine.h`가 Phase 0 빈 스텁이라 `ProductionLine`/`ProductionJob`/`ComputeActualQuantity` 등 미선언으로 91개 컴파일 오류(C2653/C3861/C2065/C2146 등, `-t:SampleOrderSystemTests` 빌드 100개 초과로 조기 중단)).<br><br>**확정 API 설계** (`produce/ProductionLine.h`, produce_agent 구현 대상, 전역 네임스페이스 — `Sample`/`Order`와 동일한 관례):<br>`struct ProductionJob { std::string orderId; std::string sampleId; int shortage=0; int actualQuantity=0; int totalMinutes=0; std::chrono::system_clock::time_point startedAt; std::chrono::system_clock::time_point expectedEndAt; };`<br>`class ProductionLine { public: using NowProvider = std::function<std::chrono::system_clock::time_point()>; explicit ProductionLine(NowProvider nowProvider = []{ return std::chrono::system_clock::now(); }); void Enqueue(const std::string& orderId, const std::string& sampleId, int shortage, const Sample& sample); std::vector<std::string> ProcessCompletions(OrderRepository& orders, SampleRepository& samples); std::optional<ProductionJob> CurrentJob(const std::string& sampleId) const; std::vector<ProductionJob> PendingQueue(const std::string& sampleId) const; std::vector<data::ProductionState> ExportState() const; void RestoreState(const std::vector<data::ProductionState>& states, OrderRepository& orders); static std::string FormatTimePoint(std::chrono::system_clock::time_point tp); static std::chrono::system_clock::time_point ParseTimePoint(const std::string& iso8601); static int ComputeActualQuantity(int shortage, double yieldRate); };`<br><br>**설계 결정 및 이유**:<br>1. **shortage 전달 방식**: `Enqueue`는 shortage(부족분)를 호출자(controller_agent)로부터 인자로 전달받고, ProductionLine이 SampleRepository를 다시 조회해 "현재" 재고로 재계산하지 않는다. 이유: `Order::Approve(sample)`이 이미 승인 시점의 `sample.Stock()`과 `quantity_`를 비교해 PRODUCING 여부를 판정했으므로, 그 시점의 부족분(`quantity - stockAtApproval`)이 실제로 생산해야 할 양을 결정하는 근거다. Enqueue 시점에 재고를 다시 읽으면 그 사이 다른 주문의 생산 완료로 재고가 이미 증가해있을 수 있어(동일 시료에 대해 여러 주문이 큐에 쌓인 경우) 실제 필요량과 어긋날 위험이 있다. 따라서 승인 시점에 이미 확정된 shortage 값을 그대로 전달받는 방식으로 설계했다.<br>2. **시간 주입 방식**: 생성자가 `NowProvider`(`std::function<time_point()>`)를 받고 기본값은 `std::chrono::system_clock::now()`. 테스트에서는 가변 상태를 갖는 `FakeClock` 콜러블을 참조 캡처하는 람다(`[&clock]{ return clock(); }`)를 넘겨 시간을 임의로 전진시키며(`AdvanceMinutes`) wall-clock 판정을 검증한다. `ProcessCompletions`가 호출될 때마다 `nowProvider()`를 다시 호출하므로 같은 인스턴스에서 시간 경과를 시뮬레이션할 수 있다.<br>3. **RestoreState와 `data::ProductionState`의 관계**: `data::ProductionState`(이미 Green, 수정 불가)는 `orderId`/`productionStartAt`/`productionEndAt`/`actualQuantity`만 가지고 `sampleId`/`shortage` 필드가 없다. `ExportState`는 이 스키마 그대로 내보내고, `RestoreState(states, orders)`는 각 `orderId`를 `OrderRepository`에서 조회해 `sampleId`를 복원한다(주문 자체는 이미 orders.json에 영속화되어 있으므로 sampleId 조회가 항상 가능하다는 전제). `shortage`는 완료 판정/재고 반영에 불필요하므로(이미 계산된 `actualQuantity`만 있으면 충분) 복원 시 0으로 둔다.<br>4. **SampleRepository 의존성(모델 계층에 필요한 추가 오버로드)**: `ProcessCompletions`은 완료된 job에 대해 `orders.Find(orderId).CompleteProduction(sample, actualQuantity)`를 호출해야 하므로 **가변** `Sample&`이 필요하다. 그런데 현재 `model/Sample.h`의 `SampleRepository`는 `const Sample& Find(const std::string&) const` 오버로드만 제공한다(`OrderRepository`는 const/non-const 두 오버로드를 모두 제공하는 것과 비대칭). 이 문서 작성 시점 기준으로 `model/Sample.h`는 이미 Green이라 TDD_Agent가 직접 수정하지 않았지만, **produce_agent Green 전환 전에 model_agent가 `Sample& Find(const std::string&)` 비-const 오버로드를 추가해야 한다** (기존 const Find/기존 테스트를 깨지 않는 순수 추가이므로 회귀 위험 없음). orchestrator는 Phase 4 착수 시 이 선행 작업을 model_agent에게 먼저 배정해야 한다.<br>5. FIFO 대기열은 시료(sampleId)별로 독립적으로 관리하며, 완료된 job이 있으면 같은 시료의 대기열에서 다음 job을 지금 시각(`nowProvider()`)에 즉시 시작시킨다. |
| `Repository` 직렬화/역직렬화 | data | 정상 JSON 왕복(round-trip) 일치(Sample/Order/생산 진행 상태 각각), 파일 손상/누락 시 fallback(빈 상태 시작), 필드 누락 시 처리, **`SaveOrder` 단일 주문 갱신 시 다른 주문 데이터가 훼손되지 않는지**, 생산 진행 상태(`productionStartAt`/`productionEndAt`) 왕복 후 ISO 8601 파싱 결과가 원본과 일치하는지. 확정 API(`data/Repository.h`, `namespace data`, data_agent 구현 대상, 2026-07-15 Red 확인 — `data/Repository.h`가 Phase 0 빈 스텁이라 `data` 네임스페이스/함수 미선언으로 C2653/C3861/C2923 등 72개 컴파일 오류): <br>`constexpr const char* kSamplesFilePath = "samples.json";` / `kOrdersFilePath = "orders.json";` / `kProductionStateFilePath = "production_state.json";` <br>`void SaveSamples(const std::vector<Sample>&, const std::string& filePath = kSamplesFilePath);` <br>`std::vector<Sample> LoadSamples(const std::string& filePath = kSamplesFilePath);` — 파일 없음/JSON 파싱 실패 시 예외를 던지지 않고 빈 vector 반환 <br>`void SaveOrders(const std::vector<Order>&, const std::string& filePath = kOrdersFilePath);` <br>`std::vector<Order> LoadOrders(const std::string& filePath = kOrdersFilePath);` — 파일 없음/손상 시 빈 vector 반환 <br>`void SaveOrder(const Order&, const std::string& filePath = kOrdersFilePath);` — filePath의 기존 주문 목록을 로드 후 동일 `Id()`가 있으면 해당 주문만 교체(update), 없으면 추가(insert)한 뒤 재저장. 다른 주문 필드는 훼손하지 않음 <br>`struct ProductionState { std::string orderId; std::string productionStartAt; std::string productionEndAt; int actualQuantity = 0; };` <br>`void SaveProductionState(const std::vector<ProductionState>&, const std::string& filePath = kProductionStateFilePath);` <br>`std::vector<ProductionState> LoadProductionState(const std::string& filePath = kProductionStateFilePath);` — 파일 없음/손상 시 빈 vector 반환. <br>Order는 `orderId_`/`customerName_`/`sampleId_`/`quantity_`/`status_` 필드를 그대로 저장하며(`docs_temp/data/phase.md`의 `sampleName`은 예시일 뿐이고 실제 model_agent 구현인 `sampleId`/`customerName`을 우선), `OrderStatus`는 JSON에서 문자열("RESERVED"/"REJECTED"/"PRODUCING"/"CONFIRMED"/"RELEASE")로 직렬화한다. JSON 파싱/직렬화 내부 구현 방식(직접 만든 경량 파서 vs nlohmann/json 등 vendor 헤더온리 라이브러리)은 data_agent가 선택하되, 위 공개 API 시그니처만 준수하면 된다 — TDD_Agent 제안: 이 프로젝트 규모(단순 구조체 목록, 중첩 없는 평면적 스키마)에서는 직접 만든 경량 JSON 파서/라이터로 충분하나, 향후 스키마가 복잡해질 가능성을 고려하면 nlohmann/json(헤더 온리) vendor 추가도 안전한 대안이다. Red 테스트: `SampleOrderSystemTests/RepositoryTests.cpp` |
| 재고 상태 판정(여유/부족/고갈) 및 모니터링 집계 (`MonitoringService`) | model | 재고=0→고갈(수요와 무관), 0<재고<수요→부족, 재고>=수요 경계값→여유(재고==수요 포함), 수요=0이고 재고>0→여유, 특정 시료의 미출고 주문 수요 합계(REJECTED/RELEASE 제외한 RESERVED+PRODUCING+CONFIRMED 수량 합산, 다른 시료 주문은 제외, 해당 없으면 0), 상태별 주문 수 집계 시 REJECTED 제외 및 RESERVED/CONFIRMED/PRODUCING/RELEASE 정확히 카운트(빈 목록이면 전부 0). Red 테스트: `SampleOrderSystemTests/MonitoringTests.cpp` (Phase 2 완료, 2026-07-15 Red 확인 — `model/MonitoringService.h` 부재로 C1083 컴파일 실패). 확정 시그니처(model_agent 구현 대상, `model/MonitoringService.h`): `enum class StockStatus { SURPLUS, SHORTAGE, DEPLETED }`; `StockStatus JudgeStockStatus(int stock, int demand)`(순수 함수, stock==0이면 항상 DEPLETED, 0<stock<demand면 SHORTAGE, stock>=demand면 SURPLUS — demand==0 포함); `int SumUndeliveredDemand(const std::vector<Order>& orders, const std::string& sampleId)`(**2026-07-15 변경, Phase 6**: PRODUCING/CONFIRMED 상태이면서 sampleId 일치하는 주문의 Quantity() 합산 — RESERVED는 아직 승인되지 않아 확정된 수요가 아니므로 REJECTED/RELEASE와 함께 제외. Red 테스트: `SampleOrderSystemTests/MonitoringTests.cpp`, 2026-07-15 Phase 6 Red 확인 대상); `struct OrderStatusCounts { int reserved=0; int confirmed=0; int producing=0; int release=0; }`; `OrderStatusCounts CountOrdersByStatus(const std::vector<Order>& orders)`(REJECTED 제외 나머지 4개 상태 카운트). task 예시 대비 조정: JudgeStockStatus는 (stock, demand) 두 정수만 받는 순수 분류 함수로 유지하고, "미출고 수요 합계 계산"은 별도 헬퍼(`SumUndeliveredDemand`)로 분리 — JudgeStockStatus 자체의 경계값 테스트를 Order/vector 구성과 무관하게 독립적으로 검증하기 위함. |

### TDD 낮음/불필요

| 클래스/모듈 | 계층 | 사유 |
|---|---|---|
| `MenuController` 분기 흐름 | controller | 단순 위임/분기이며 Model/Produce API가 이미 TDD로 검증됨. 통합 테스트나 수동 확인으로 충분 |
| `ConsoleView` | view | 콘솔 출력 문자열은 눈으로 확인하는 것이 더 빠르고 회귀 위험이 낮음 |
| 시료 주문 입력 검증(수량 > 0 등) | controller | 단순 값 검증이나, 로직이 복잡해지면(예: 다중 조건) 재평가 |

> 위 표는 phase 문서가 갱신되어 새 클래스/계산식/검증 규칙이 추가될 때마다 함께 갱신한다.
> 표에 없는 로직을 구현 에이전트가 임의로 "TDD 불필요"로 간주하고 테스트 없이 구현하지 않는다 — 애매하면
> TDD_Agent에게 먼저 판단을 요청한다.

## 테스트 프레임워크 통합 계획

- **선정 프레임워크**: Catch2 (v3.5.4), 헤더 온리 방식(단일 헤더 앰알거메이션: `catch_amalgamated.hpp` +
  `catch_amalgamated.cpp`). vcpkg/NuGet 등 외부 패키지 매니저 의존 없이 저장소에 vendor로 포함해
  오프라인 빌드가 가능하도록 한다.
- **배치 위치**: `SampleOrderSystemTests/vendor/catch2/catch_amalgamated.hpp`,
  `SampleOrderSystemTests/vendor/catch2/catch_amalgamated.cpp` — 이번 Phase 1에서 실제로 다운로드해
  배치 완료(네트워크 접근 가능했음).
- **테스트 전용 프로젝트**: `SampleOrderSystemTests/` 디렉터리에 별도 콘솔 프로젝트
  `SampleOrderSystemTests.vcxproj`를 신설한다(이번 Phase 1에서는 아직 생성하지 않음 — Phase 2 진입
  시 orchestrator가 model_agent와 함께 처리).
  - 테스트 실행 파일의 `main`은 Catch2 기본 러너를 사용(별도 main 작성 불필요, `catch_amalgamated.cpp`가
    기본 main을 제공하거나 `CATCH_CONFIG_MAIN` 매크로 사용).
  - 테스트 대상 소스(`model/Sample.cpp`, `model/Order.cpp`, `data/Repository.cpp`,
    `produce/ProductionLine.cpp`)를 어떻게 참조할지는 두 가지 방식 중 orchestrator가 결정:
    1. `SampleOrderSystem` 본체 프로젝트를 정적 라이브러리로도 빌드할 수 있게 구성하고 테스트
       프로젝트가 링크
    2. 테스트 프로젝트의 `.vcxproj`에 필요한 `.cpp` 파일들을 직접 `ClCompile` 항목으로 추가(간단하지만
       중복 컴파일 발생) — 본 프로젝트가 아직 실행 파일(exe) 단일 구성이므로 초기에는 이 방식이 더 간단할
       수 있음
  - 솔루션(`SampleOrderSystem.slnx`)에 프로젝트 참조를 추가하는 작업은 orchestrator 담당.
- **새 테스트 파일 추가 시 규칙**: 테스트 파일(`*.test.cpp`)을 추가하면 반드시
  `SampleOrderSystemTests.vcxproj`의 `<ItemGroup>` `ClCompile` 항목에도 등록한다(본 문서 상단
  CLAUDE.md 지침과 동일한 원칙).
- **현재 범위**: 이번 Phase 1에서는 Sample/Order/Repository/ProductionLine이 아직 구현되지 않았으므로
  (model_agent가 Phase 2에서 착수 예정) 실제 테스트 케이스 코드는 작성하지 않는다. Phase 2 진입
  직전(model_agent 착수 전) TDD_Agent가 `Sample.test.cpp`/`Order.test.cpp`를 먼저 작성하고, Phase 3/4
  진입 직전 각각 `Repository.test.cpp`/`ProductionLine.test.cpp`를 작성한다.

## 작업 순서
1. ~~테스트 프레임워크 선정(Catch2 또는 Google Test) — orchestrator와 협의 후 `.vcxproj`/테스트 전용
   프로젝트에 통합~~ → **완료**: Catch2 v3.5.4 헤더 온리 방식으로 선정, vendor 파일 배치 완료(위
   "테스트 프레임워크 통합 계획" 참고). `.vcxproj` 신설 자체는 Phase 2 진입 시 orchestrator가 진행.
2. 위 "TDD 필요" 표의 클래스 순서대로 실패하는 테스트 작성 → 각 구현 에이전트(model_agent/produce_agent/
   data_agent)가 통과시키는 구현 진행
   - 순서: `Sample`/`Order`(model_agent, Phase 2) → `Repository`(data_agent, Phase 3) →
     `ProductionLine`/wall-clock 판정(produce_agent, Phase 4)
3. phase 문서가 갱신되어 새 클래스/계산식이 추가되면 이 표도 함께 갱신

> 각 구현 에이전트는 담당 Phase 착수 전 이 문서의 "TDD 필요" 표를 다시 확인하고, 해당 클래스의 테스트가
> 아직 작성되지 않았다면 TDD_Agent에게 먼저 실패하는 테스트 작성을 요청한 뒤 구현에 들어간다.

## 산출물
- 테스트 프로젝트/파일 (예: `SampleOrderSystemTests/`, vendor Catch2 배치 완료)
- 위 표를 근거로 한 클래스별 테스트 케이스 목록

## Phase 3 진행 상황 (2026-07-15)

- `SampleOrderSystemTests/RepositoryTests.cpp` 작성 완료, `SampleOrderSystemTests.vcxproj`의 `ClCompile`에 등록 완료.
- 테스트 케이스: Sample round-trip, Order round-trip(상태 enum 포함), 파일 없음 시 fallback(Samples/Orders/ProductionState 각각), 손상된 JSON 파일 fallback(Samples/Orders/ProductionState 각각), `SaveOrder` 단일 갱신 시 타 주문 훼손 없음, `SaveOrder` 신규 주문 삽입, ProductionState round-trip 후 ISO 8601 문자열 정확히 일치.
- Red 확인: `msbuild SampleOrderSystem.slnx -p:Configuration=Debug -p:Platform=x64 -t:SampleOrderSystemTests` 실행 결과 72개 컴파일 오류(`data` 네임스페이스 및 `SaveSamples`/`LoadSamples`/`SaveOrders`/`LoadOrders`/`SaveOrder`/`SaveProductionState`/`LoadProductionState`/`ProductionState` 미선언) 발생 — `data/Repository.h`가 아직 Phase 0 빈 스텁이므로 예상대로 Red.
- 다음 단계: data_agent가 위 확정 API대로 `data/Repository.h`(+ 필요 시 `.cpp`)를 구현해 Green 전환. JSON 처리 방식(경량 자체 파서 vs nlohmann/json vendor)은 data_agent가 선택.

## Phase 5 진행 상황 (2026-07-15) — 사용자 요구사항 4건 추가(Red)

사용자가 다음 4가지 요구사항을 추가했다:
1. `Sample::avgProductionTime`은 분 단위이며 소수점 값을 허용해야 한다(현재 `int` → `double`로 변경 필요).
2. 생산 완료 후 Sample 재고 증가량은 "실 생산량(actualQuantity)"과 정확히 동일해야 한다.
3. Repository(영속화)에서 wall-clock 완료 후 재고 증가가 저장/로드 사이클을 거쳐도 유지되어야 한다.
4. `SampleRepository`에 등록되지 않은 시료는 생산(생산 큐 등록)이 불가능해야 한다.

### 요구사항 1 — avgProductionTime: int → double (Red)

- `model/Sample.h`: 생성자 `Sample(std::string id, std::string name, int avgProductionTime, ...)`와
  `int AvgProductionTime() const`를 각각 `double avgProductionTime`, `double AvgProductionTime() const`로
  변경해야 한다. model_agent 담당.
- `produce/ProductionLine.h`: `ProductionJob::totalMinutes`(현재 `int`)를 `double`로 변경해야 한다.
  `totalMinutes = sample.AvgProductionTime() * actualQuantity`가 소수점 결과를 낼 수 있으므로(예:
  0.5*1=0.5, 2.5*4=10.0) `int`로 두면 대입 시 잘려나간다. 또한 `StartJob`/`ProcessCompletions`에서
  `startedAt + std::chrono::minutes(totalMinutes)`(정수 분 단위)로 `expectedEndAt`을 계산하는 부분도
  분수 분 단위(예: 0.5분=30초)를 표현할 수 있도록 변경해야 한다 — 제안:
  `std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::duration<double, std::ratio<60>>(totalMinutes))`.
  `RestoreState`에서 `totalMinutes`를 `expectedEndAt - startedAt`으로부터 역산하는 부분(현재
  `std::chrono::duration_cast<std::chrono::minutes>(...).count()`, 정수 분 절삭)도 소수점을 보존하도록
  `std::chrono::duration<double, std::ratio<60>>`로 캐스팅해야 한다. produce_agent 담당.
- `data/Repository.h`: `SaveSamples`에서 `s.AvgProductionTime()`을 그대로 출력하는 부분은 변경 불필요(값
  타입만 double이 되면 스트림 출력이 자동으로 소수점을 포함), `LoadSamples`의
  `int avgProductionTime = ... std::stoi(...)`를 `double avgProductionTime = ... std::stod(...)`로
  바꿔야 한다. data_agent 담당.
- `view/ConsoleView.h`: `int ReadAvgProductionTime()`을 `double ReadAvgProductionTime()`으로 변경(입력
  파싱을 `std::stod` 등으로). view_agent 담당.
- `controller/MenuController.h`: `HandleSampleRegistration()`의 `int avgProductionTime = view_.ReadAvgProductionTime();`을
  `double avgProductionTime = ...;`로 변경. controller_agent 담당.
- **Red 테스트 추가**:
  - `SampleOrderSystemTests/SampleTests.cpp`: "Sample은 평균 생산시간에 소수점 값을 허용하고 정확히
    보존한다" (12.5, 0.5, 정수 10과의 하위 호환, 15.75 네 가지 SECTION).
  - `SampleOrderSystemTests/RepositoryTests.cpp`: "Sample의 avgProductionTime이 소수점 값이어도 저장 후
    로드 시 정확히 동일한 값으로 복원된다" (15.75 round-trip).
  - `SampleOrderSystemTests/ProductionLineTests.cpp`: "avgProductionTime이 소수점 값이면 총 생산 시간도
    정확한 소수점 값으로 계산된다"(2.5*4=10.0, `expectedEndAt`이 600초 뒤와 정확히 일치하는지까지 확인)와
    "avgProductionTime이 1분 미만의 소수(0.5분=30초)여도 wall-clock 판정이 초 단위로 정확하다"(29초
    경과 시 미완료, 30초 경과 시 완료).
- **Red 확인 결과** (`msbuild ... -t:SampleOrderSystemTests`, 2026-07-15): 컴파일은 경고(C4244,
  double→int 암묵적 축소 변환) 7건과 함께 **성공**했다(현재 시그니처가 모두 `int`라 `double` 리터럴이
  암묵적으로 축소 변환되어 컴파일 자체는 깨지지 않음). 테스트 실행 결과 새로 추가한 4개 테스트 케이스가
  전부 실패(Red)했다: `sample.AvgProductionTime()`이 12.5 대신 12, 0.5 대신 0, 15.75 대신 15를 반환;
  Repository round-trip도 15.75 대신 15로 복원; `job->totalMinutes`가 10.0 대신 8(=2.5→2로 잘린
  `AvgProductionTime()` * actualQuantity 4의 정수 연산 결과인 것으로 보임); 0.5분(30초) 케이스는 29초
  시점에 이미 완료로 판정되어 `stillProducing.empty()`가 실패(0.5가 0으로 잘려 `totalMinutes=0`이 되어
  즉시 완료 판정된 것). 전체 결과: 52 test cases 중 48 passed / 4 failed, 224 assertions 중 218
  passed / 6 failed — 예상대로 Red.

### 요구사항 2 — 생산 완료 반영량 = 실 생산량(actualQuantity) 검증

- 기존 `ProductionLineTests.cpp`의 wall-clock 완료 테스트들은 모두 `shortage`와 `actualQuantity`가
  우연히 일치하는 경우(예: shortage=20, yieldRate=0.5 → actualQuantity=40, 항상 나누어떨어짐)만
  다루고 있어 "shortage ≠ actualQuantity인 경우에도 재고 증가량이 actualQuantity를 정확히 따르는지"는
  커버리지가 부족했다.
- **추가한 테스트**: "실 생산량이 부족분과 다른 경우(나누어떨어지지 않음)에도 재고 증가량은 정확히 실
  생산량과 일치한다" — shortage=10, yieldRate=0.3 → actualQuantity=ceil(10/0.3)=34(shortage와 다름).
  생산 완료 후 `samples.Find("S-600").Stock() == 34`(10이 아님)를 검증.
- **결과**: 이미 Green(현재 `ProductionLine::ProcessCompletions`가 `order.CompleteProduction(sample,
  current.actualQuantity)`를 호출하므로 shortage가 아닌 actualQuantity가 재고에 반영됨) — 새 테스트는
  이 계약을 명시적으로 고정(regression lock)하는 역할을 한다. 전체 테스트 실행 결과에서도 이 케이스는
  실패하지 않았다(위 48 passed 중 하나).

### 요구사항 3 — Repository 영속화 후 재고 증가 확인(통합 시나리오)

- **추가한 테스트**(`ProductionLineTests.cpp`, "영속화 통합" 섹션): "영속화 통합: 만료된 생산 job을
  저장했다가 새 인스턴스로 복원하면 재고 증가가 저장/로드 사이클을 거쳐도 유지된다".
  1. Sample(재고 0) + Order(PRODUCING) 생성 후 `ProductionLine::Enqueue`로 생산 큐 등록.
  2. `ExportState()` + `data::SaveProductionState`, `data::SaveSamples`, `data::SaveOrders`로 저장.
  3. 완전히 새로운 `SampleRepository`/`OrderRepository`/`ProductionLine` 인스턴스로 `data::LoadSamples`/
     `data::LoadOrders`/`data::LoadProductionState` 후 `RestoreState`로 복원(재실행 시각은 총 생산
     시간을 훌쩍 넘긴 시점으로 설정).
  4. `ProcessCompletions` 호출 후 `Sample::Stock()`이 `actualQuantity`(40)만큼 증가했는지 확인.
  5. 증가된 재고를 다시 `data::SaveSamples`로 저장 후 `data::LoadSamples`로 재로드해도 40이 정확히
     유지되는지 확인(영속화 사이클 재검증).
- **결과**: 이미 Green — data_agent/produce_agent가 이미 구현한 `SaveSamples`/`LoadSamples`/
  `SaveProductionState`/`LoadProductionState`/`ExportState`/`RestoreState`/`ProcessCompletions`
  조합이 이 통합 시나리오를 정확히 만족한다. 다만 요구사항 1(avgProductionTime → double)이 반영되면
  이 테스트에 사용된 `Sample("S-500", "통합테스트시료", 5, 0.5, 0)`의 `5`가 `double`로 암시적 변환되어
  계속 통과해야 하므로(회귀 없음), 향후 model_agent/produce_agent Green 전환 후에도 재확인이 필요하다.

### 요구사항 4 — 미등록 시료는 생산 불가 (설계 판단, 새 테스트 최소 추가)

- `ProductionLine::Enqueue(const std::string& orderId, const std::string& sampleId, int shortage,
  const Sample& sample)`는 `Sample` 값 자체를 인자로 받으므로, API 차원에서 "호출자가 로컬에서 즉석으로
  만든(=SampleRepository에 등록되지 않은) `Sample` 객체"를 넘기는 것을 막을 방법은 없다(한계).
- 그러나 현재 유일한 실제 호출부(`SampleOrderSystem/controller/MenuController.h:273`)는 항상
  `samples_.Find(order.SampleId())`의 결과를 그대로 넘긴다. `SampleRepository::Find`는 등록되지 않은
  id에 대해 `std::out_of_range`를 던지므로(`model/Sample.h`), 등록되지 않은 시료로는 애초에 `Find`
  단계에서 예외가 발생해 `Enqueue` 호출 자체가 이루어지지 않는다.
- 또한 생산 큐 등록은 항상 `Order`가 `PRODUCING`으로 전환된 뒤에만 일어나며(`HandleApproval` 흐름),
  `Order` 생성/승인 이전 단계인 `MenuController::HandleSampleOrder()`(주문 생성)에서 이미
  `samples_.Contains(sampleId)`를 확인해 등록되지 않은 시료 ID로는 애초에 주문(`RESERVED`) 자체가
  생성되지 않는다(`SampleOrderSystem/controller/MenuController.h:218`). 즉 "등록되지 않은 시료 주문 생성"
  →(1차 방어) 및 "등록되지 않은 시료 Enqueue" →(2차 방어, `Find`의 `out_of_range`)가 이중으로 보장된다.
- **판단**: `ProductionLine::Enqueue`의 시그니처를 `SampleRepository&` + `sampleId`를 받아 내부에서
  존재 확인 후 예외를 던지는 형태로 바꾸는 방안도 검토했으나, (a) 현재 아키텍처에서 유일한 호출 경로가
  이미 이중으로 방어되어 있어 실질적 위험이 낮고, (b) 시그니처 변경 시 `ProductionLine`이
  `SampleRepository`에 대한 의존성을 추가로 갖게 되어 "Sample/Order 클래스 정의에는 관여하지 않는다"는
  produce_agent 책임 경계가 흐려질 수 있으므로, 시그니처는 변경하지 않기로 판단했다.
- 이에 따라 새로운 Red 테스트는 추가하지 않되, 이 안전장치의 근거가 되는 `SampleRepository::Find`의
  예외 동작을 명시적으로 고정하는 테스트를 `SampleTests.cpp`에 추가했다: "SampleRepository는 등록되지
  않은 시료 ID를 Find하면 예외를 던진다"(const/non-const 오버로드 모두 `std::out_of_range` 확인). 이미
  구현되어 있으므로 Green(회귀 방지용 규약 고정)이다.
- **후속 권고**: controller_agent는 `HandleApproval`(생산 큐 등록 지점)에서도 `samples_.Find(...)`
  호출을 `try/catch`로 감싸거나 사전에 `Contains` 체크를 추가해, 이론상 `Order`에 저장된 `sampleId`가
  이후 어떤 경로로든 `SampleRepository`에서 사라진 경우(현재 코드에는 삭제 API가 없어 발생하지 않지만)
  `std::out_of_range`가 처리되지 않은 채 전파되지 않도록 방어적으로 유지할 것을 권고한다(현재 코드는
  이미 등록 시점에 존재를 보장하므로 필수는 아니나, 향후 시료 삭제 기능이 추가될 경우 재검토 필요).

### 반영된 파일

- `SampleOrderSystemTests/SampleTests.cpp`: 인터페이스 주석에 `avgProductionTime: int → double` 변경
  공지 추가, "Sample은 평균 생산시간에 소수점 값을 허용하고 정확히 보존한다"(4 SECTION), "SampleRepository는
  등록되지 않은 시료 ID를 Find하면 예외를 던진다" TEST_CASE 추가.
- `SampleOrderSystemTests/RepositoryTests.cpp`: "Sample의 avgProductionTime이 소수점 값이어도 저장 후
  로드 시 정확히 동일한 값으로 복원된다" TEST_CASE 추가.
- `SampleOrderSystemTests/ProductionLineTests.cpp`: 인터페이스 주석에 `ProductionJob::totalMinutes:
  int → double` 변경 필요성과 `startedAt`/`expectedEndAt` 소수점 분 계산 방식 제안 추가, `TempFileGuard`
  헬퍼 추가, 4개 TEST_CASE 추가("avgProductionTime 소수점→총 생산시간", "0.5분(30초) wall-clock 정밀도",
  "actualQuantity ≠ shortage일 때 재고 증가량", "영속화 통합 시나리오").
- 세 파일 모두 `SampleOrderSystemTests.vcxproj`에 이미 `ClCompile` 항목으로 등록되어 있어(기존 파일
  수정만 했으므로) 프로젝트 파일 변경은 불필요했다.

### 다음 단계(Green 전환 담당)

1. model_agent: `model/Sample.h`의 `avgProductionTime` 관련 타입을 `int` → `double`로 변경.
2. produce_agent: `produce/ProductionLine.h`의 `ProductionJob::totalMinutes`를 `double`로 변경하고,
   `StartJob`/`ProcessCompletions`/`RestoreState`의 시간 계산을 소수점 분(초 단위 정밀도)까지 지원하도록
   수정.
3. data_agent: `data/Repository.h`의 `LoadSamples`에서 `std::stoi` → `std::stod`로 변경.
4. view_agent: `view/ConsoleView.h`의 `ReadAvgProductionTime()` 반환 타입을 `double`로 변경.
5. controller_agent: `controller/MenuController.h`의 `avgProductionTime` 지역 변수 타입을 `double`로
   변경.
6. 위 변경들이 모두 완료된 뒤 `msbuild ... -t:SampleOrderSystemTests`로 전체 테스트가 Green(198개
   assertions 이상, 기존 194 + 신규 4개 이상 실패했던 것 포함 전부 통과)이 되는지 재확인한다.

## Phase 4 진행 상황 (2026-07-15)

- `SampleOrderSystemTests/ProductionLineTests.cpp` 작성 완료, `SampleOrderSystemTests.vcxproj`의 `ClCompile`에 등록 완료.
- 테스트 케이스: `ComputeActualQuantity` 경계값(나누어떨어짐 10/0.5=20, 안 떨어짐 10/0.3=ceil(33.33..)=34), 총 생산 시간 계산(avgProductionTime * actualQuantity) 검증, 동일 시료 FIFO 대기(먼저 등록한 주문이 계속 CurrentJob으로 남고 나중 주문은 PendingQueue에 추가), 서로 다른 시료는 동시에 즉시 시작, FIFO 완료 후 다음 대기 job이 즉시 시작(startedAt=완료 판정 시각), 생산 시작 직후 미완료, 총 생산 시간 절반 경과 시 미완료(재고 불변), 총 생산 시간 정확히 경과 시 완료(Order CONFIRMED 전환 + Sample 재고 반영 동시 확인), 재시작 복구 후 이미 경과 시각이 지났으면 즉시 완료, 아직 안 지났으면 대기 유지, `FormatTimePoint`/`ParseTimePoint` ISO 8601 왕복 일치.
- Red 확인: `msbuild SampleOrderSystem.slnx -p:Configuration=Debug -p:Platform=x64 -t:SampleOrderSystemTests` 실행 결과 91개 컴파일 오류 발생(1000개 초과분은 MSBuild가 100개에서 조기 중단) — `ProductionLine`/`ProductionJob`/`ComputeActualQuantity` 등이 `produce/ProductionLine.h`(Phase 0 빈 스텁)에 선언되어 있지 않아 예상대로 Red.
- **produce_agent Green 전환 전 필수 선행 작업**: `model/Sample.h`의 `SampleRepository`에 비-const `Sample& Find(const std::string&)` 오버로드 추가가 필요하다(현재는 const 오버로드만 존재해 `ProcessCompletions`가 재고를 증가시킬 가변 참조를 얻을 수 없음). `OrderRepository`가 이미 const/non-const 두 오버로드를 제공하는 것과 대칭을 맞추는 순수 추가이므로 기존 테스트에 영향 없음. orchestrator는 이 작업을 model_agent에게 먼저 배정한 뒤 produce_agent의 `ProductionLine.h` 구현을 진행시켜야 한다.
- 다음 단계: (1) model_agent가 `SampleRepository::Find` 비-const 오버로드 추가, (2) produce_agent가 위 확정 API대로 `produce/ProductionLine.h`(+ 필요 시 `.cpp`)를 구현해 Green 전환.

## Phase 6 진행 상황 (2026-07-15) — 사용자 요구사항 2건 추가(Red)

사용자가 다음 2가지 요구사항을 추가했다:
1. 모니터링/재고 확인 시 보여지는 재고(quantity)는 "출고(Release) 전" 값이므로, 실제 출고 처리 시
   Sample 재고가 주문 수량만큼 차감되어야 한다(현재 `Order::Release()`는 상태만 바꾸고 재고를 건드리지
   않음 — 버그성 누락).
2. 모니터링 재고 상태(여유/부족/고갈) 판정에 쓰이는 "수요"는 CONFIRMED/PRODUCING만 고려해야 하며,
   아직 승인되지 않은 RESERVED 주문은 확정된 수요가 아니므로 제외해야 한다(현재
   `SumUndeliveredDemand`는 RESERVED/PRODUCING/CONFIRMED를 모두 합산).

### 요구사항 1 — Order::Release가 Sample 재고를 차감하도록 시그니처 변경 (Red)

- `model/Order.h`: 기존 `void Release()`(무인자, 상태만 CONFIRMED→RELEASE 전환)를
  `void Release(Sample& sample)`로 변경해야 한다. `CompleteProduction(Sample&, int)`과 동일한 순서
  원칙을 따른다 — **재고 차감(`sample.DecreaseStock(quantity_)`)을 먼저 시도해 성공한 뒤에만 상태를
  RELEASE로 전환**한다. 재고가 부족하면 `Sample::DecreaseStock`이 `std::invalid_argument`를 던지고,
  이 예외가 그대로 전파되며 주문 상태는 CONFIRMED로 유지되어야 한다(상태를 먼저 바꾸고 재고 차감을
  시도하면, 재고 부족 시 "출고는 실패했는데 상태만 RELEASE로 바뀌는" 불일치가 생기므로 반드시 차감을
  먼저 시도하는 순서로 구현해야 한다). CONFIRMED가 아닌 상태(RESERVED/PRODUCING/REJECTED/RELEASE)에서
  호출 시 `std::logic_error`를 던지고 재고는 변하지 않아야 한다(상태 검사가 재고 차감보다 먼저 이루어짐).
  model_agent 담당.
- `controller/MenuController.h`: `HandleRelease()`(363번째 줄 근처)의 `order.Release();` 호출을
  `order.Release(sample);`로 변경해야 한다(해당 주문의 `SampleId()`로 `samples_.Find(...)`한 가변
  `Sample&`을 전달). 이번 작업 범위 아님 — controller_agent가 Green 전환 시 함께 수정.
- **Red 테스트 추가/수정** (`SampleOrderSystemTests/OrderTests.cpp`):
  - 인터페이스 주석의 `Release()` 시그니처를 `Release(Sample&)`로 갱신하고 재고 차감/차감 순서 계약을
    명시.
  - "CONFIRMED 주문을 출고 처리하면 RELEASE로 전이하고 Sample 재고를 주문 수량만큼 차감한다": 재고 20,
    주문 수량 10인 CONFIRMED 주문을 `Release(sample)` 호출 후 상태 RELEASE, `sample.Stock() == 10` 확인.
  - "출고 처리 시 재고가 주문 수량보다 부족하면 예외가 전파되고 주문 상태는 CONFIRMED로 유지된다": 승인
    이후 외부 요인으로 재고가 줄어(20→5) 주문 수량(10)보다 부족해진 상황에서 `Release(sample)` 호출 시
    `std::invalid_argument`가 전파되고, 주문 상태는 CONFIRMED 유지, `sample.Stock()`은 차감 없이 5 유지.
  - "허용되지 않는 상태 전이는 거부된다" 섹션의 출고 관련 SECTION들을 `Release(sample)` 형태로 갱신하고,
    각각 재고가 변하지 않는지(`REQUIRE(sample.Stock() == ...)`)와 상태가 유지되는지 함께 확인하도록
    보강. PRODUCING/REJECTED 상태에서의 거부 SECTION을 신규 추가(기존에는 RESERVED/RELEASE 상태에서의
    거부만 있었음).
  - 기존에 무인자로 호출하던 모든 `.Release()` 호출부(`OrderTests.cpp`, `MonitoringTests.cpp`)를
    `.Release(sample)`로 갱신.

### 요구사항 2 — SumUndeliveredDemand에서 RESERVED 제외 (Red)

- `model/MonitoringService.h`: `SumUndeliveredDemand`의 `switch` 문에서 `case OrderStatus::RESERVED:`
  분기를 제거해, PRODUCING/CONFIRMED 상태의 주문 수량만 합산하도록 변경해야 한다. model_agent 담당.
- **Red 테스트 추가/수정** (`SampleOrderSystemTests/MonitoringTests.cpp`):
  - 인터페이스 주석 갱신: "PRODUCING/CONFIRMED 상태의 주문 수량만 합산하며 RESERVED는 확정된 수요가
    아니므로 제외한다"는 계약 명시.
  - 기존 "SumUndeliveredDemand: 특정 시료에 대한 미출고 주문 수량만 합산한다" 테스트를 "RESERVED/
    PRODUCING/CONFIRMED만 합산"에서 "CONFIRMED/PRODUCING만 합산하고 RESERVED는 제외"로 기대값을
    수정(RESERVED 3 + CONFIRMED 4 + PRODUCING 7 = 14 → CONFIRMED 4 + PRODUCING 7 = 11, RESERVED
    수량 3은 더 이상 포함되지 않음을 주석으로 명시).
  - 신규 테스트 "SumUndeliveredDemand: RESERVED 상태 주문만 있으면 수요는 0이다" 추가(RESERVED만 있는
    주문 목록에 대해 결과가 0인지 확인하는 경계 케이스).
  - 이 파일 내 `.Release()` 호출(2건, CountOrdersByStatus/기존 SumUndeliveredDemand 테스트의 RELEASE
    시나리오 준비용)도 `.Release(sample)`로 갱신.

### Red 확인 결과 (2026-07-15)

`msbuild SampleOrderSystem.slnx -p:Configuration=Debug -p:Platform=x64 -t:SampleOrderSystemTests` 실행
결과, `MonitoringTests.cpp`/`OrderTests.cpp` 컴파일 단계에서 **9개의 C2660 컴파일 오류**가 발생했다(모두
"`Order::Release`: 함수는 1개의 인수를 사용하지 않습니다" — `model/Order.h`의 `Release()`가 아직 무인자
시그니처이기 때문). 오류 발생 위치: `MonitoringTests.cpp:115, 170`, `OrderTests.cpp:171, 182, 194, 205,
208, 238, 255`. 예상대로 Red — `SumUndeliveredDemand`의 RESERVED 제외 로직은 `model/MonitoringService.h`가
아직 변경되지 않았으므로 컴파일 자체는 통과하나(값 비교 실패로 런타임 Red가 될 예정), 이번 빌드는
Release 시그니처 불일치로 컴파일 단계에서 조기 실패해 `SumUndeliveredDemand` 관련 테스트의 런타임 실행
결과는 이번 실행에서 확인되지 않았다. model_agent가 `Order::Release(Sample&)`와 `SumUndeliveredDemand`의
RESERVED 제외 로직을 함께 Green 전환한 뒤 재빌드하면 모든 신규/수정 테스트가 통과해야 한다.

### 다음 단계(Green 전환 담당)

1. model_agent: `model/Order.h`의 `Release()`를 `Release(Sample& sample)`로 변경(재고 차감 우선, 실패
   시 상태 유지), `model/MonitoringService.h`의 `SumUndeliveredDemand`에서 RESERVED 케이스 제거.
2. controller_agent: `controller/MenuController.h`의 `HandleRelease()`에서 `order.Release();`를
   `order.Release(sample);`로 변경(해당 주문의 시료를 `samples_.Find(order.SampleId())`로 조회해 전달).
3. 위 변경 완료 후 `msbuild ... -t:SampleOrderSystemTests` 재실행으로 전체 테스트가 Green이 되는지
   확인(신규/수정된 Release 및 SumUndeliveredDemand 관련 테스트 케이스 전부 통과 확인 필요).

## Phase 7 진행 상황 (2026-07-15) — JudgeStockStatus 경계값 정책 변경(Red)

사용자가 재고 상태 판정 정책을 변경했다: **재고(stock)와 수요(demand)가 정확히 같을 때는 더 이상
SURPLUS(여유)가 아니라 DEPLETED(고갈)로 판정한다.** 이유: 재고가 수요와 정확히 같으면 현재 확정된 수요를
모두 충족시키고 나면 재고가 정확히 0이 되어 "곧 고갈될" 상태이므로, 여유가 아니라 고갈로 보는 것이 더
실무적으로 타당하다는 판단이다.

- **변경된 규칙** (`model/MonitoringService.h`, `JudgeStockStatus(int stock, int demand)`):
  - 고갈(DEPLETED): `stock == 0` **이거나** `stock == demand`(0이 아닌 경우 포함) — 즉 `stock <= demand`
    이면서 `stock == demand`이거나 `stock == 0`인 경우. 정리하면 `stock == 0 || stock == demand`.
  - 부족(SHORTAGE): `0 < stock < demand` (변경 없음)
  - 여유(SURPLUS): `stock > demand` (재고가 수요를 **초과**하는 경우만; 기존에는 `stock >= demand`였음)
- **현재 구현**(`model/MonitoringService.h:23-36`)은 아직 기존 규칙(`stock >= demand` → SURPLUS)을 그대로
  사용하고 있어, `JudgeStockStatus(10, 10)`이 SURPLUS(0)를 반환한다. 이번 Phase 7에서는 테스트만 먼저
  변경했고(Red), 실제 구현 변경은 model_agent가 Green 단계에서 진행한다.
- **Red 테스트 변경/추가** (`SampleOrderSystemTests/MonitoringTests.cpp`):
  - 기존 "JudgeStockStatus: 재고 >= 수요이면 여유(SURPLUS)이다(경계값 포함)" TEST_CASE의
    "재고와 수요가 정확히 같음(경계값)" SECTION(기대값 SURPLUS)을 별도 TEST_CASE
    "JudgeStockStatus: 재고와 수요가 정확히 같으면 고갈(DEPLETED)이다(경계값 변경)"로 분리하고 기대값을
    DEPLETED로 수정(`JudgeStockStatus(10, 10) == StockStatus::DEPLETED`), `stock=0, demand=0` 케이스도
    함께 포함해 기존 고갈 규칙과의 일관성을 재확인.
  - 기존 SURPLUS TEST_CASE는 "JudgeStockStatus: 재고 > 수요(초과)이면 여유(SURPLUS)이다"로 제목을
    변경하고, 재고가 수요를 실제로 초과하는 시나리오만 남김: `(20,10)`(기존 유지), 신규 추가
    `(11,10)`(경계값 바로 위), 신규 추가 `(10,7)`(초과 폭이 있는 케이스), `(5,0)`(수요 0, 변경 없음).
  - 인터페이스 설계 주석(파일 상단)도 새 규칙에 맞게 갱신.
- **Red 확인 결과** (2026-07-15): `msbuild SampleOrderSystem.slnx -p:Configuration=Debug -p:Platform=x64
  -t:SampleOrderSystemTests` 빌드는 경고/오류 없이 성공(시그니처 변경이 없으므로 컴파일 자체는 항상
  통과). 테스트 실행(`SampleOrderSystemTests.exe "[StockStatus]"`) 결과, "재고와 수요가 정확히 같으면
  고갈(DEPLETED)이다(경계값 변경)" TEST_CASE의 "재고와 수요가 정확히 같음(0이 아닌 경계값)" SECTION이
  예상대로 실패했다: `REQUIRE( JudgeStockStatus(10, 10) == StockStatus::DEPLETED )` → 실제값 SURPLUS(0)
  vs 기대값 DEPLETED(2). 전체 4 test cases 중 3 passed / 1 failed, 10 assertions 중 9 passed / 1
  failed — 예상대로 Red.

### 다음 단계(Green 전환 담당)

1. model_agent: `model/MonitoringService.h`의 `JudgeStockStatus`를 다음과 같이 수정한다.
   ```cpp
   inline StockStatus JudgeStockStatus(int stock, int demand)
   {
       if (stock == 0 || stock == demand)
       {
           return StockStatus::DEPLETED;
       }

       if (stock < demand)
       {
           return StockStatus::SHORTAGE;
       }

       return StockStatus::SURPLUS; // stock > demand
   }
   ```
   (또는 동등한 분기 순서 — 단 "stock == demand → DEPLETED"와 "stock > demand일 때만 SURPLUS" 계약을
   반드시 만족해야 한다.) 주석(라인 19-22)도 새 규칙에 맞게 함께 갱신할 것을 권고한다.
2. 이 변경이 `SumUndeliveredDemand`/`CountOrdersByStatus`나 다른 모듈의 기존 동작에는 영향을 주지 않으므로
   (순수 분류 함수 하나만 변경) 추가 연쇄 수정은 없을 것으로 예상되나, controller_agent/view_agent가
   `JudgeStockStatus`의 반환값을 화면에 "여유/부족/고갈" 문구로 매핑하는 코드가 있다면 그 매핑 로직 자체는
   변경할 필요 없다(enum 값과 문구 매핑은 그대로 유지, 판정 조건만 바뀜).
3. 위 변경 완료 후 `msbuild ... -t:SampleOrderSystemTests` 재실행으로 전체 테스트가 Green이 되는지
   확인한다.

## Phase 8 진행 상황 (2026-07-15) — 주문 승인 시 availableStock 계산 버그 수정 (PLAN.md 옵션 B, Red)

`PLAN.md`("주문 승인 시 가용 재고(availableStock) 계산 버그 수정 계획")에서 채택된 **옵션 B**를
Red-Green 루프로 진행한다. 배경(예시 A/B)은 `PLAN.md` 1절 참고.

### 확정 설계 (옵션 B)

1. **`model/MonitoringService.h`에 신규 순수 함수 추가**:
   ```cpp
   // 특정 sampleId에 대해 CONFIRMED(아직 미출고) 상태 주문들의 수량 합계
   int SumConfirmedQuantity(const std::vector<Order>& orders, const std::string& sampleId);
   ```
   `SumUndeliveredDemand`와 동일한 패턴(순회 + 상태/시료 필터)이며, PRODUCING은 포함하지 않고
   CONFIRMED만 합산한다는 점이 다르다.
2. **`model/Order.h`의 `Approve` 시그니처를 값 기반으로 완전히 교체** (오버로드로 남기지 않음):
   ```cpp
   // 기존: void Approve(const Sample& sample);
   // 변경: void Approve(int availableStock);
   //   availableStock >= quantity_ 이면 CONFIRMED, 아니면 PRODUCING. RESERVED 아니면 std::logic_error(변경 없음).
   ```
   `Order`/`Sample`은 여전히 `OrderRepository`/`ProductionLine`을 참조하지 않는다(계층 분리 유지) —
   availableStock 계산(CONFIRMED 합계 차감, 활성 PRODUCING job 존재 시 0 취급)은 전부 Controller의 책임.
3. **`controller/MenuController.h::HandleApprovalOrRejection()`가 구현해야 할 계산 순서**(다음 단계
   controller_agent 인계용, 이번 Red 단계에서는 수정하지 않음):
   ```cpp
   const Sample& sample = samples_.Find(order.SampleId());
   bool hasActiveJob = productionLine_.CurrentJob(order.SampleId()).has_value();
   int availableStock = hasActiveJob
       ? 0
       : sample.Stock() - SumConfirmedQuantity(CollectAllOrders(), order.SampleId());
   int shortage = order.Quantity() - availableStock;
   order.Approve(availableStock);
   if (order.Status() == OrderStatus::PRODUCING)
   {
       productionLine_.Enqueue(order.Id(), order.SampleId(), shortage, samples_.Find(order.SampleId()));
       SaveProductionState();
   }
   ```

### Red 테스트 변경/추가

- `SampleOrderSystemTests/OrderTests.cpp`: 인터페이스 주석의 `Approve(const Sample&)`를
  `Approve(int availableStock)`로 갱신하고 PLAN.md 옵션 B 계약을 명시. 기존 13곳의
  `order.Approve(sample)` 호출을 전부 `order.Approve(sample.Stock())`로 교체(호출부의 `sample.Stock()`을
  그대로 값으로 넘기면 기존 시나리오 결과는 동일 — 회귀 없음).
- `SampleOrderSystemTests/MonitoringTests.cpp`: 인터페이스 주석에 `SumConfirmedQuantity` 시그니처 추가.
  기존 6곳의 `.Approve(sample)`/`.Approve(emptySample)` 호출을 `.Approve(sample.Stock())`/
  `.Approve(emptySample.Stock())`로 교체. 신규 테스트 2건 추가:
  - "SumConfirmedQuantity: 같은 시료의 CONFIRMED 주문 수량을 모두 합산한다"(수량 80+30=110, RESERVED/
    다른 시료 CONFIRMED는 제외 확인 — PLAN.md 예시 A의 확장 케이스).
  - "SumConfirmedQuantity: 해당 시료의 CONFIRMED 주문이 없으면 0을 반환한다"(RESERVED만 있는 목록).
- `SampleOrderSystemTests/ApprovalIntegrationTests.cpp` (신규 파일, `SampleOrderSystemTests.vcxproj`
  `ClCompile`에 등록 완료): `Sample`/`Order`/`MonitoringService::SumConfirmedQuantity`/`ProductionLine`을
  조합해 Controller가 수행할 계산을 그대로 재현하는 통합 테스트 4건.
  - "PLAN.md 예시 A": 재고 100, CONFIRMED 주문 A(80) 존재 시 신규 주문 B(30)의
    `availableStock = 100 - SumConfirmedQuantity([A]) = 20` → B는 PRODUCING, shortage=10.
  - "PLAN.md 예시 B": `ProductionLine::Enqueue`로 활성 job을 만든 뒤(`CurrentJob(sampleId).has_value()`
    확인), 신규 주문은 남은 물리적 재고(10)와 무관하게 `availableStock=0`으로 취급되어 무조건 PRODUCING,
    shortage=주문 수량 전체(3).
  - "availableStock=0으로 Approve 호출 시 항상 PRODUCING, shortage=수량 전체" 계약 단위 테스트.
  - "회귀: CONFIRMED 주문도 없고 활성 job도 없으면 availableStock = sample.Stock()"(기존 단순 케이스와
    동일하게 동작하는지 확인).

### Red 확인 결과 (2026-07-15)

`msbuild SampleOrderSystem.slnx -p:Configuration=Debug -p:Platform=x64 -t:SampleOrderSystemTests` 실행
결과, 컴파일 단계에서 **다음 두 종류의 오류**가 발생했다(예상대로 Red):
- **C2664** (28개 호출 지점 × 중복 출력 2회 = 56줄): `Order::Approve(const Sample&)`에 `int`를 넘길 수
  없다는 오류. `OrderTests.cpp`/`MonitoringTests.cpp`의 모든 `Approve(sample.Stock())` 호출 지점에서
  발생 — `model/Order.h`가 아직 옛 시그니처(`const Sample&`)이기 때문.
- **C3861** (`SumConfirmedQuantity`: 식별자를 찾을 수 없음, 4곳 × 2회 = 8줄): `MonitoringTests.cpp:262,
  270`, `ApprovalIntegrationTests.cpp:44, 114` — `model/MonitoringService.h`에 아직
  `SumConfirmedQuantity`가 선언되지 않았기 때문.

### 다음 단계(Green 전환 담당)

1. model_agent: `model/MonitoringService.h`에 `SumConfirmedQuantity(const std::vector<Order>&, const
   std::string&)` 추가, `model/Order.h`의 `Approve(const Sample&)`를 `Approve(int availableStock)`로
   교체(오버로드 아님 — 완전 대체). `Sample.h` include는 `CompleteProduction(Sample&, int)`/
   `Release(Sample&)`가 여전히 사용하므로 유지.
2. controller_agent: `controller/MenuController.h::HandleApprovalOrRejection()`을 위 "확정 설계 3."의
   계산 순서대로 수정(`SumConfirmedQuantity` + `productionLine_.CurrentJob(...)` 조합으로
   `availableStock` 계산 후 `order.Approve(availableStock)` 호출, `shortage`도 `availableStock` 기준으로
   재계산).
3. 위 변경 완료 후 `msbuild ... -t:SampleOrderSystemTests` 재실행으로 전체 테스트(기존 회귀 포함)가
   Green이 되는지 확인한다.
