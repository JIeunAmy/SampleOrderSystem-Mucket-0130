# orchestrator 작업 Phase

## Phase 0 — 프로젝트 골격
- `SampleOrderSystem.vcxproj`에 `model/`, `view/`, `controller/`, `data/`, `produce/` 디렉터리 구조 확정
- 각 계층 간 인터페이스(헤더) 배치 위치 합의
  - `model/Sample.h`, `model/Order.h`
  - `data/Repository.h`
  - `controller/MenuController.h`
  - `view/ConsoleView.h`
  - `produce/ProductionLine.h` (FIFO 큐, 실시간 생산 스케줄링 — `produce_agent` 담당)

## Phase 1.5 — 생산 라인 (Phase 1의 model_agent API 확정 후, produce_agent에게 위임)
- 상세 내용은 `docs_temp/produce_agent.md` 참고
- Model(Sample/Order API), Data(생산 시작/완료 시각 영속화 API) 확정 후 시작

## Phase 5 — 통합 검증 (model/data/produce/controller/view 작업 완료 후)
1. `main.cpp`에서 메뉴 루프 연결, 앱 시작 시 data_agent 로드 호출 배치
2. `.vcxproj`에 신규 소스 파일 전부 등록되었는지 확인
3. `msbuild SampleOrderSystem.slnx /p:Configuration=Debug /p:Platform=x64`로 빌드 검증
4. 계층 간 인터페이스 시그니처 불일치 점검 (Model API ↔ Controller 호출, Controller ↔ View 데이터 형태, produce_agent ↔ Model/Data API)
5. 상태 전이 규칙 및 계산식(실 생산량 `ceil(부족분/수율)`, 총 생산 시간 `평균생산시간 * 실생산량`)이 CLAUDE.md 명세와 일치하는지 최종 확인
