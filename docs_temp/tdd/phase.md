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
| `ProductionLine`(FIFO 스케줄러) | produce | 실 생산량 `ceil(부족분/수율)` 경계값(나누어떨어짐/안 떨어짐), 총 생산 시간 계산, FIFO 순서 보장(먼저 들어온 주문 먼저 생산), **생산 큐 등록 API 분기**(동일 시료가 이미 생산 중이면 대기열에 추가만 되고, 아니면 즉시 생산 시작), 한 번에 하나의 시료만 생산되는지(다른 시료 주문이 동시에 등록돼도 대기) |
| 생산 실시간(wall-clock) 판정 로직 | produce | 생산 시작 직후 조회 시 미완료, 총 생산 시간 정확히 경과 시 완료(경계값), 총 생산 시간 절반만 경과 시 미완료 및 남은 시간 계산, **앱 재시작 후에도 실제 경과 시간 기준으로 완료 판정**(시작/예상 완료 시각만 저장해두고 재시작 시점의 현재 시각과 비교), 재시작 시 아직 안 지났으면 남은 시간 반영하여 대기 상태 유지 |
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
