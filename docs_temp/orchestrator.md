# orchestrator 작업 Phase

세부 에이전트별 phase 문서(`docs_temp/{model,data,produce,controller,view,tdd}/phase.md`)와
`docs/[CRA_AI] Day3_개인과제_반도체시료관리.pdf`(10~24페이지, 기능 명세 원본)를 대조해 전체 순서를 재구성한다.
각 Phase는 이전 Phase가 노출하는 API가 확정되어야 시작할 수 있다.

## Phase 0 — 프로젝트 골격
- `SampleOrderSystem.vcxproj`에 `model/`, `data/`, `produce/`, `controller/`, `view/` 디렉터리 구조 확정
- 각 계층 간 인터페이스(헤더) 배치 위치 합의
  - `model/Sample.h`, `model/Order.h`
  - `data/Repository.h`
  - `produce/ProductionLine.h`
  - `controller/MenuController.h`
  - `view/ConsoleView.h`

## Phase 1 — TDD 대상 식별 (`TDD_Agent`, 모든 계층 phase 문서 작성 직후·구현 착수 전)
- `docs_temp/{model,data,produce,controller,view}/phase.md`를 읽고 TDD가 필요한 클래스와 테스트 시나리오를 식별
- 상세 내용/결과 표는 `docs_temp/tdd/phase.md` 참고
- 식별 결과를 model_agent/data_agent/produce_agent 등 해당 구현 에이전트에게 공유

## Phase 2 — Model 구현 (`model_agent`, Phase 0 확정 후 시작)
- 상세 내용은 `docs_temp/model/phase.md` 참고
- `Sample`(수율 검증, 재고), `Order`(상태 전이), 생산 완료 반영 API, 인메모리 저장소 API 구현
- **모니터링 집계 API 포함** (PDF 20페이지: 상태별 주문 수 집계 REJECTED 제외 + 시료별 재고 여유/부족/고갈 판정) —
  이 판정 로직은 model_agent가 소유하고 controller_agent는 호출만 한다
- **Red-Green 루프 적용 대상**: `Sample`, `Order`, 재고 상태 판정(여유/부족/고갈) — `docs_temp/tdd/phase.md` 표 기준으로 진행

## Phase 3 — Data 영속성 (`data_agent`, Phase 2의 구조체 정의 확정 후 시작)
- 상세 내용은 `docs_temp/data/phase.md` 참고
- Sample/Order JSON 직렬화·역직렬화, 저장/로드 API, 생산 시각(`productionStartAt`/`productionEndAt`) 영속화 스키마 구현
- **Red-Green 루프 적용 대상**: `Repository` 직렬화/역직렬화

## Phase 4 — Produce 생산 라인 (`produce_agent`, Phase 2의 생산 완료 반영 API + Phase 3의 저장소 API 확정 후 시작)
- 상세 내용은 `docs_temp/produce/phase.md` 참고
- **생산 큐 등록 API 포함** (PDF 16페이지: 승인 시 재고 부족이면 "생산 라인에 자동으로 등록") —
  controller_agent가 승인 처리 중 호출할 "주문을 FIFO 큐에 등록" API를 이 Phase에서 함께 노출한다
- FIFO 대기열, 실 생산량/총 생산 시간 계산, wall-clock 기반 실시간 완료 판정 및 앱 재시작 복구 로직 구현
- **Red-Green 루프 적용 대상**: `ProductionLine`(FIFO 스케줄러), 생산 실시간(wall-clock) 판정 로직

## Phase 5 — Controller 메뉴 분기 (`controller_agent`, Phase 2~4 API 확정 후 시작)
- 상세 내용은 `docs_temp/controller/phase.md` 참고
- 6개 메인 메뉴 분기, Model/Produce API 호출, data_agent 저장 호출 지점 삽입
- **연결 지점 주의**: 승인 처리 시 Model 판정이 `PRODUCING`이면 반드시 Phase 4의 produce_agent 생산 큐 등록 API를
  이어서 호출한다. 이 호출이 빠지면 승인은 되었지만 생산이 시작되지 않는 버그가 되므로, 인터페이스 검증(Phase 7)에서
  반드시 확인한다.
- TDD 우선순위 낮음(단순 분기) — 통합 테스트/수동 확인으로 대체, Red-Green 루프 미적용

## Phase 6 — View 콘솔 입출력 (`view_agent`, Controller가 넘길 데이터 형태 확정 후 시작, Phase 5와 병행 가능)
- 상세 내용은 `docs_temp/view/phase.md` 참고
- 메뉴/입력 프롬프트, 목록·표·상태 뱃지 렌더링
- TDD 대상 아님 — Red-Green 루프 미적용

## Phase 2~4 공통: TDD Red-Green 반복 루프
`docs_temp/tdd/phase.md`의 "TDD 필요" 표에 있는 클래스는 해당 Phase 안에서 클래스 하나씩 아래 사이클로 반복한다.
직접 구현하지 않고 매 사이클마다 해당 서브에이전트에게 위임한다.

1. **Red**: `TDD_Agent`에게 위임해 대상 클래스의 실패하는 테스트를 먼저 작성한다.
2. **Green**: 해당 Phase의 구현 에이전트(`model_agent`/`data_agent`/`produce_agent`)에게 위임해 테스트를
   통과시키는 최소한의 구현을 작성하게 한다.
3. **검증**: 테스트를 다시 실행해 통과하는지 확인한다 (`msbuild` + 테스트 실행). 실패하면 같은 에이전트에게
   재위임하고, 통과할 때까지 2~3단계를 반복한다.
4. **다음 클래스로 이동**: 통과가 확인되면 같은 Phase 또는 다음 Phase의 다음 클래스로 넘어가 1번부터 다시 반복한다.
   - 의존 관계가 있는 클래스(예: `ProductionLine`은 `Sample`/`Order`의 생산 완료 반영 API를 사용)는 의존 대상이
     먼저 Green이 된 뒤에 시작한다 — 즉 Phase 2 → 3 → 4 순서를 그대로 따른다.
5. 표의 모든 클래스가 Green 상태가 되면 Phase 7(통합 검증)로 진행한다.

## Phase 7 — 통합 검증 (`orchestrator`, model/data/produce/controller/view 작업 완료 후)
1. `main.cpp`에서 메뉴 루프 연결, 앱 시작 시 data_agent 로드 호출 배치
2. `.vcxproj`에 신규 소스 파일 전부 등록되었는지 확인
3. `msbuild SampleOrderSystem.slnx /p:Configuration=Debug /p:Platform=x64`로 빌드 검증
4. 계층 간 인터페이스 시그니처 불일치 점검 (Model API ↔ Controller 호출, Controller ↔ View 데이터 형태, produce_agent ↔ Model/Data API)
5. 상태 전이 규칙 및 계산식(실 생산량 `ceil(부족분/수율)`, 총 생산 시간 `평균생산시간 * 실생산량`)이 CLAUDE.md 명세와 일치하는지 최종 확인
6. Phase 1에서 식별된 TDD 대상 클래스가 모두 Green 상태인지 최종 확인
7. **PDF 원본 대조** (`docs/[CRA_AI] Day3_개인과제_반도체시료관리.pdf` 10~24페이지)
   - 승인 시 `PRODUCING` 판정된 주문이 실제로 produce_agent의 생산 큐에 등록되는지(Phase 5 ↔ Phase 4 연결) 직접 실행해 확인
   - 모니터링 화면에 재고 여유/부족/고갈 3단계가 model_agent의 판정 API 값 그대로 표시되는지 확인
   - 그 외 메뉴별 입력값·상태 전이 문구가 PDF 명세와 어긋나지 않는지 재확인
