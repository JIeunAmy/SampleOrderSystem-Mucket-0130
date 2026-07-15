---
name: Sample
description: 반도체 시료 생산주문관리 시스템에서 시료(Sample) 관리 기능(등록/목록 조회/검색, 재고 필드)을 구현하거나 수정할 때 사용한다. Sample 도메인의 필드 정의, 검증 규칙, MVC 계층별 책임 분리 방법을 안내한다.
---

# 시료(Sample) 관리 기능 구현 가이드

이 스킬은 "반도체 시료 생산주문관리 시스템"에서 **시료 등록 / 시료 조회 / 시료 검색** 기능을 구현할 때 따라야 할
규칙을 정리한다. 작업 전 저장소 루트의 `CLAUDE.md`를 먼저 확인해 전체 MVC 구조와 상태 흐름을 파악한다.

## Sample 도메인 필드

| 필드 | 설명 |
|---|---|
| 시료 ID | 고유 식별자 (예: `S-001`) |
| 이름 | 시료명 |
| 평균 생산시간 | 1개 생산에 걸리는 평균 시간 (분 단위, 소수점 허용) |
| 수율 | 정상 생산 시료 수 / 총 생산 시료 수 (0~1 사이 값) |
| 재고 수량 | 현재 보유 중인 시료 수량 (등록 시 기본값 0, 초기 재고 값을 지정하면 해당 값으로 시작 — 아래 "초기 재고 지정" 참고. 생산 완료 시 증가, 출고 시 감소). 생산 완료 시 증가하는 수량은 실 생산량(생산된 양, `ceil(부족분/수율)`)과 정확히 동일하며, 부족분(shortage) 자체가 아니다. |

등록된 시료만 주문 가능하므로, Order를 생성하기 전에 시료 ID가 존재하는지 반드시 검증한다.

### 초기 재고 지정 (default parameter)

`Sample` 생성자는 재고 초기값을 마지막 매개변수로 받는다. 반드시 **기본값 0**을 갖도록 구현하여,
값을 넘기지 않으면 기존과 동일하게 재고 0에서 시작하고, 값을 넘기면 해당 값으로 시작하도록 한다.

```cpp
Sample(std::string id, std::string name, double avgProductionTime, double yieldRate, int initialStock = 0);
```

- `initialStock` 생략 시 → 재고 0에서 시작 (기존 호출부와 하위 호환)
- `initialStock`에 양수/0을 명시 → 해당 값으로 시작
- `initialStock`이 음수면 `std::invalid_argument`를 던진다 (재고는 음수가 될 수 없다는 불변식과 일관성 유지)
- `avgProductionTime`은 `double`(분 단위, 소수점 허용)이다. 총 생산 시간(`평균 생산시간 * 실 생산량`) 계산 시
  소수점 값을 그대로 반영하며, `int`로 절삭하지 않는다.

### 생산 완료 시 재고 증가량 = 실 생산량

생산 완료 반영 API(`Order::CompleteProduction(Sample&, int producedQuantity)`)는 `produce_agent`가 계산한
**실 생산량**(`actualQuantity = ceil(shortage / yieldRate)`)을 그대로 인자로 받아 Sample 재고에 더한다.
재고 증가량은 항상 부족분(shortage)이 아니라 실 생산량과 정확히 동일해야 한다. 수율이 1 미만이면
실 생산량이 부족분보다 커질 수 있으므로 이 둘을 혼동하지 않도록 주의한다.

### 영속화(Repository) 사이클에서의 재고 정확성

`ProductionLine::ProcessCompletions`(wall-clock 기준 완료 판정)로 재고가 증가한 뒤에도, `data::SaveSamples`/
`SaveProductionState`로 저장하고 다시 로드했을 때 증가된 재고 수량이 그대로 유지되어야 한다. 즉 "생산 완료 →
재고 증가 → 저장 → 재로드" 전 과정에서 값 손실/불일치가 없어야 하며, 이는 `RepositoryTests.cpp`/
`ProductionLineTests.cpp`의 통합 테스트로 검증된다.

### 미등록 시료는 생산 불가

`SampleRepository`에 등록되지 않은 시료 ID로는 주문 자체를 생성할 수 없다. Controller의 `HandleSampleOrder`가
`samples_.Contains(sampleId)`를 확인해 미등록 시료 주문을 거부하며, `ProductionLine::Enqueue`도 시료 ID가 아닌
저장소에서 조회된 `Sample` 객체 자체를 인자로 받는 구조이므로, 애초에 미등록 시료는 생산 큐에 들어갈 수 없다.
따라서 "등록된 시료만 주문 가능" 원칙은 주문 생성 단계와 생산 큐 진입 단계 양쪽에서 이중으로 보장된다.

## 계층별 책임 분리

- **Model** (`model_agent`가 담당): Sample 클래스/구조체 정의, 시료 저장소(등록/조회/검색 API), 재고 증감 메서드.
  수율은 0~1 범위를 벗어나지 않도록 Model 내부에서 검증한다. `Order::CompleteProduction`은 실 생산량만큼만
  재고를 증가시킨다.
- **View** (`view_agent`가 담당): 시료 등록 입력 프롬프트, 시료 목록/검색 결과를 표 형태로 출력.
- **Controller** (`controller_agent`가 담당): 시료 관리 하위 메뉴(등록/목록/검색) 분기, View에서 받은 입력을
  Model의 등록·조회·검색 API로 연결. 시료 주문 생성 시 `samples_.Contains(sampleId)`로 미등록 시료를 걸러낸다.
- **Produce** (`produce_agent`가 담당): FIFO 큐 스케줄링, 실 생산량/총 생산 시간 계산, wall-clock 기준 완료
  판정과 함께 실 생산량만큼 Sample 재고를 반영(부족분이 아님)한다.
- **Data** (`data_agent`가 담당): 시료/주문/생산 상태를 JSON으로 영속화하며, 생산 완료로 증가한 재고 수량이
  저장 → 재로드 사이클을 거쳐도 정확히 유지되도록 보장한다.

## 기능별 요구사항

- **시료 등록**: 시료 ID, 이름, 평균 생산시간, 수율을 입력받아 신규 시료를 추가한다. 이미 존재하는 시료 ID는 거부한다. 초기 재고는 선택 입력으로 처리하고(View에서 미입력 시 Model의 기본값 0이 적용되도록 전달), 값이 입력되면 그 값을 그대로 Model 생성자의 `initialStock` 인자로 전달한다.
- **시료 조회**: 등록된 모든 시료 목록을 현재 재고 수량과 함께 표시한다.
- **시료 검색**: 이름 등 속성으로 특정 시료를 찾아 표시한다 (부분 일치 검색 허용).

## 작업 시 체크리스트

1. `CLAUDE.md`의 주문 상태 흐름·계산식과 모순되지 않는지 확인한다.
2. 새 소스 파일 추가 시 `SampleOrderSystem/SampleOrderSystem.vcxproj`에 `ClCompile`/`ClInclude` 항목을 등록한다.
3. Sample 관련 변경이 Order/ProductionLine 계산식(실 생산량, 재고 기준 승인 분기)에 영향을 주는지 함께 점검한다.
4. `avgProductionTime`이 분 단위 `double`(소수점 허용)로 일관되게 처리되는지 확인한다.
5. 생산 완료 시 Sample 재고에 더해지는 값이 부족분이 아니라 실 생산량(`producedQuantity`)과 정확히 같은지 확인한다.
6. 생산 완료로 늘어난 재고가 Repository 저장/재로드 사이클을 거쳐도 그대로 유지되는지(통합 테스트 포함) 확인한다.
7. 미등록 시료 ID로는 주문 생성 및 생산 큐 진입이 모두 불가능한지(Controller 검증 + `ProductionLine::Enqueue`의 Sample 객체 요구 구조) 확인한다.
