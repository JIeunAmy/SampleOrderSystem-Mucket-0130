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
| `Order` | model | 상태 전이 규칙 전부(RESERVED→CONFIRMED/PRODUCING, RESERVED→REJECTED, PRODUCING→CONFIRMED, CONFIRMED→RELEASE), 허용되지 않는 전이 시도(REJECTED에서 재승인/재거절, CONFIRMED에서 재승인, RESERVED/CONFIRMED에서 생산완료 반영 시도, RESERVED에서 출고 시도, RELEASE에서 재출고) 거부, **생산 완료 반영 API 통합 시나리오**(PRODUCING 주문에 대해 완료 반영 API 호출 시 주문 상태가 CONFIRMED로 바뀌는 동시에 해당 Sample 재고가 실 생산량만큼 증가하는지 한 번에 검증). 확정 시그니처: `Order(orderId, customerName, sampleId, quantity)`, `Approve(const Sample&)`(재고>=수량이면 CONFIRMED, 아니면 PRODUCING; RESERVED 아니면 std::logic_error), `Reject()`(RESERVED 아니면 std::logic_error), `CompleteProduction(Sample&, int producedQuantity)`(PRODUCING 아니면 std::logic_error; produce_agent가 계산한 실 생산량을 그대로 Sample 재고에 반영 — quantity_가 아닌 producedQuantity를 받도록 조정한 이유는 실 생산량 `ceil(부족분/수율)`이 주문 수량과 다를 수 있기 때문), `Release()`(CONFIRMED 아니면 std::logic_error). Red 테스트: `SampleOrderSystemTests/OrderTests.cpp` |
| `ProductionLine`(FIFO 스케줄러) + 생산 실시간(wall-clock) 판정 로직 | produce | 실 생산량 `ceil(부족분/수율)` 경계값(나누어떨어짐/안 떨어짐), 총 생산 시간 계산, FIFO 순서 보장(먼저 들어온 주문 먼저 생산), **생산 큐 등록 API 분기**(동일 시료가 이미 생산 중이면 대기열에 추가만 되고, 아니면 즉시 생산 시작), 한 번에 하나의 시료만 생산되는지(다른 시료 주문이 동시에 등록돼도 즉시 시작), 생산 시작 직후 조회 시 미완료, 총 생산 시간 정확히 경과 시 완료(경계값, Order CONFIRMED 전환 + Sample 재고 반영 동시 검증), 총 생산 시간 절반만 경과 시 미완료 유지, 완료 시 FIFO 대기열의 다음 job이 즉시 시작되는지, **앱 재시작 후에도 실제 경과 시간 기준으로 완료 판정**(ExportState로 저장한 시작/예상 완료 시각을 새 인스턴스에 RestoreState로 복원한 뒤, 이미 경과 시각이 지났으면 즉시 완료 처리, 아직 안 지났으면 대기 상태 유지), ISO 8601 `FormatTimePoint`/`ParseTimePoint` 왕복 일치. Red 테스트: `SampleOrderSystemTests/ProductionLineTests.cpp` (2026-07-15 Red 확인 — `produce/ProductionLine.h`가 Phase 0 빈 스텁이라 `ProductionLine`/`ProductionJob`/`ComputeActualQuantity` 등 미선언으로 91개 컴파일 오류(C2653/C3861/C2065/C2146 등, `-t:SampleOrderSystemTests` 빌드 100개 초과로 조기 중단)).<br><br>**확정 API 설계** (`produce/ProductionLine.h`, produce_agent 구현 대상, 전역 네임스페이스 — `Sample`/`Order`와 동일한 관례):<br>`struct ProductionJob { std::string orderId; std::string sampleId; int shortage=0; int actualQuantity=0; int totalMinutes=0; std::chrono::system_clock::time_point startedAt; std::chrono::system_clock::time_point expectedEndAt; };`<br>`class ProductionLine { public: using NowProvider = std::function<std::chrono::system_clock::time_point()>; explicit ProductionLine(NowProvider nowProvider = []{ return std::chrono::system_clock::now(); }); void Enqueue(const std::string& orderId, const std::string& sampleId, int shortage, const Sample& sample); std::vector<std::string> ProcessCompletions(OrderRepository& orders, SampleRepository& samples); std::optional<ProductionJob> CurrentJob(const std::string& sampleId) const; std::vector<ProductionJob> PendingQueue(const std::string& sampleId) const; std::vector<data::ProductionState> ExportState() const; void RestoreState(const std::vector<data::ProductionState>& states, OrderRepository& orders); static std::string FormatTimePoint(std::chrono::system_clock::time_point tp); static std::chrono::system_clock::time_point ParseTimePoint(const std::string& iso8601); static int ComputeActualQuantity(int shortage, double yieldRate); };`<br><br>**설계 결정 및 이유**:<br>1. **shortage 전달 방식**: `Enqueue`는 shortage(부족분)를 호출자(controller_agent)로부터 인자로 전달받고, ProductionLine이 SampleRepository를 다시 조회해 "현재" 재고로 재계산하지 않는다. 이유: `Order::Approve(sample)`이 이미 승인 시점의 `sample.Stock()`과 `quantity_`를 비교해 PRODUCING 여부를 판정했으므로, 그 시점의 부족분(`quantity - stockAtApproval`)이 실제로 생산해야 할 양을 결정하는 근거다. Enqueue 시점에 재고를 다시 읽으면 그 사이 다른 주문의 생산 완료로 재고가 이미 증가해있을 수 있어(동일 시료에 대해 여러 주문이 큐에 쌓인 경우) 실제 필요량과 어긋날 위험이 있다. 따라서 승인 시점에 이미 확정된 shortage 값을 그대로 전달받는 방식으로 설계했다.<br>2. **시간 주입 방식**: 생성자가 `NowProvider`(`std::function<time_point()>`)를 받고 기본값은 `std::chrono::system_clock::now()`. 테스트에서는 가변 상태를 갖는 `FakeClock` 콜러블을 참조 캡처하는 람다(`[&clock]{ return clock(); }`)를 넘겨 시간을 임의로 전진시키며(`AdvanceMinutes`) wall-clock 판정을 검증한다. `ProcessCompletions`가 호출될 때마다 `nowProvider()`를 다시 호출하므로 같은 인스턴스에서 시간 경과를 시뮬레이션할 수 있다.<br>3. **RestoreState와 `data::ProductionState`의 관계**: `data::ProductionState`(이미 Green, 수정 불가)는 `orderId`/`productionStartAt`/`productionEndAt`/`actualQuantity`만 가지고 `sampleId`/`shortage` 필드가 없다. `ExportState`는 이 스키마 그대로 내보내고, `RestoreState(states, orders)`는 각 `orderId`를 `OrderRepository`에서 조회해 `sampleId`를 복원한다(주문 자체는 이미 orders.json에 영속화되어 있으므로 sampleId 조회가 항상 가능하다는 전제). `shortage`는 완료 판정/재고 반영에 불필요하므로(이미 계산된 `actualQuantity`만 있으면 충분) 복원 시 0으로 둔다.<br>4. **SampleRepository 의존성(모델 계층에 필요한 추가 오버로드)**: `ProcessCompletions`은 완료된 job에 대해 `orders.Find(orderId).CompleteProduction(sample, actualQuantity)`를 호출해야 하므로 **가변** `Sample&`이 필요하다. 그런데 현재 `model/Sample.h`의 `SampleRepository`는 `const Sample& Find(const std::string&) const` 오버로드만 제공한다(`OrderRepository`는 const/non-const 두 오버로드를 모두 제공하는 것과 비대칭). 이 문서 작성 시점 기준으로 `model/Sample.h`는 이미 Green이라 TDD_Agent가 직접 수정하지 않았지만, **produce_agent Green 전환 전에 model_agent가 `Sample& Find(const std::string&)` 비-const 오버로드를 추가해야 한다** (기존 const Find/기존 테스트를 깨지 않는 순수 추가이므로 회귀 위험 없음). orchestrator는 Phase 4 착수 시 이 선행 작업을 model_agent에게 먼저 배정해야 한다.<br>5. FIFO 대기열은 시료(sampleId)별로 독립적으로 관리하며, 완료된 job이 있으면 같은 시료의 대기열에서 다음 job을 지금 시각(`nowProvider()`)에 즉시 시작시킨다. |
| `Repository` 직렬화/역직렬화 | data | 정상 JSON 왕복(round-trip) 일치(Sample/Order/생산 진행 상태 각각), 파일 손상/누락 시 fallback(빈 상태 시작), 필드 누락 시 처리, **`SaveOrder` 단일 주문 갱신 시 다른 주문 데이터가 훼손되지 않는지**, 생산 진행 상태(`productionStartAt`/`productionEndAt`) 왕복 후 ISO 8601 파싱 결과가 원본과 일치하는지. 확정 API(`data/Repository.h`, `namespace data`, data_agent 구현 대상, 2026-07-15 Red 확인 — `data/Repository.h`가 Phase 0 빈 스텁이라 `data` 네임스페이스/함수 미선언으로 C2653/C3861/C2923 등 72개 컴파일 오류): <br>`constexpr const char* kSamplesFilePath = "samples.json";` / `kOrdersFilePath = "orders.json";` / `kProductionStateFilePath = "production_state.json";` <br>`void SaveSamples(const std::vector<Sample>&, const std::string& filePath = kSamplesFilePath);` <br>`std::vector<Sample> LoadSamples(const std::string& filePath = kSamplesFilePath);` — 파일 없음/JSON 파싱 실패 시 예외를 던지지 않고 빈 vector 반환 <br>`void SaveOrders(const std::vector<Order>&, const std::string& filePath = kOrdersFilePath);` <br>`std::vector<Order> LoadOrders(const std::string& filePath = kOrdersFilePath);` — 파일 없음/손상 시 빈 vector 반환 <br>`void SaveOrder(const Order&, const std::string& filePath = kOrdersFilePath);` — filePath의 기존 주문 목록을 로드 후 동일 `Id()`가 있으면 해당 주문만 교체(update), 없으면 추가(insert)한 뒤 재저장. 다른 주문 필드는 훼손하지 않음 <br>`struct ProductionState { std::string orderId; std::string productionStartAt; std::string productionEndAt; int actualQuantity = 0; };` <br>`void SaveProductionState(const std::vector<ProductionState>&, const std::string& filePath = kProductionStateFilePath);` <br>`std::vector<ProductionState> LoadProductionState(const std::string& filePath = kProductionStateFilePath);` — 파일 없음/손상 시 빈 vector 반환. <br>Order는 `orderId_`/`customerName_`/`sampleId_`/`quantity_`/`status_` 필드를 그대로 저장하며(`docs_temp/data/phase.md`의 `sampleName`은 예시일 뿐이고 실제 model_agent 구현인 `sampleId`/`customerName`을 우선), `OrderStatus`는 JSON에서 문자열("RESERVED"/"REJECTED"/"PRODUCING"/"CONFIRMED"/"RELEASE")로 직렬화한다. JSON 파싱/직렬화 내부 구현 방식(직접 만든 경량 파서 vs nlohmann/json 등 vendor 헤더온리 라이브러리)은 data_agent가 선택하되, 위 공개 API 시그니처만 준수하면 된다 — TDD_Agent 제안: 이 프로젝트 규모(단순 구조체 목록, 중첩 없는 평면적 스키마)에서는 직접 만든 경량 JSON 파서/라이터로 충분하나, 향후 스키마가 복잡해질 가능성을 고려하면 nlohmann/json(헤더 온리) vendor 추가도 안전한 대안이다. Red 테스트: `SampleOrderSystemTests/RepositoryTests.cpp` |
| 재고 상태 판정(여유/부족/고갈) 및 모니터링 집계 (`MonitoringService`) | model | 재고=0→고갈(수요와 무관), 0<재고<수요→부족, 재고>=수요 경계값→여유(재고==수요 포함), 수요=0이고 재고>0→여유, 특정 시료의 미출고 주문 수요 합계(REJECTED/RELEASE 제외한 RESERVED+PRODUCING+CONFIRMED 수량 합산, 다른 시료 주문은 제외, 해당 없으면 0), 상태별 주문 수 집계 시 REJECTED 제외 및 RESERVED/CONFIRMED/PRODUCING/RELEASE 정확히 카운트(빈 목록이면 전부 0). Red 테스트: `SampleOrderSystemTests/MonitoringTests.cpp` (Phase 2 완료, 2026-07-15 Red 확인 — `model/MonitoringService.h` 부재로 C1083 컴파일 실패). 확정 시그니처(model_agent 구현 대상, `model/MonitoringService.h`): `enum class StockStatus { SURPLUS, SHORTAGE, DEPLETED }`; `StockStatus JudgeStockStatus(int stock, int demand)`(순수 함수, stock==0이면 항상 DEPLETED, 0<stock<demand면 SHORTAGE, stock>=demand면 SURPLUS — demand==0 포함); `int SumUndeliveredDemand(const std::vector<Order>& orders, const std::string& sampleId)`(RESERVED/PRODUCING/CONFIRMED 상태이면서 sampleId 일치하는 주문의 Quantity() 합산, REJECTED/RELEASE 및 다른 sampleId 제외); `struct OrderStatusCounts { int reserved=0; int confirmed=0; int producing=0; int release=0; }`; `OrderStatusCounts CountOrdersByStatus(const std::vector<Order>& orders)`(REJECTED 제외 나머지 4개 상태 카운트). task 예시 대비 조정: JudgeStockStatus는 (stock, demand) 두 정수만 받는 순수 분류 함수로 유지하고, "미출고 수요 합계 계산"은 별도 헬퍼(`SumUndeliveredDemand`)로 분리 — JudgeStockStatus 자체의 경계값 테스트를 Order/vector 구성과 무관하게 독립적으로 검증하기 위함. |

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
