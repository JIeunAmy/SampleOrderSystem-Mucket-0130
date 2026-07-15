# controller_agent 작업 Phase

## Phase 5 — 메뉴 분기 및 Model/Data/Produce 연동 (Model/Data/Produce API 확정 후 시작)

1. 메인 메뉴 분기 골격 구현 (6개 메뉴)
   - 시료 관리 / 시료 주문 / 주문 승인·거절 / 모니터링 / 생산 라인 / 출고 처리
2. **시료 관리** 서브메뉴: 등록/목록/검색 — Sample 스킬(`.claude/skills/Sample/SKILL.md`)의 계층 분리 규칙 준수
3. **시료 주문**: 고객명·시료 ID·수량 입력 검증 → Model의 Order 생성 API 호출 (`RESERVED` 생성)
4. **승인/거절**
   - RESERVED 목록 조회 → View에 전달
   - 승인 시 Model의 승인 API 호출 (재고 비교/분기는 Model이 결정, Controller는 결과만 View에 전달)
   - 거절 시 Model의 거절 API 호출
5. **모니터링**: Model에서 상태별 집계, 시료별 재고 현황 조회 후 View에 전달
6. **생산 라인**: 현재 생산 중 시료 + FIFO 대기열 조회 후 View에 전달
7. **출고 처리**: CONFIRMED 목록 조회 → 선택 주문 `RELEASE` 전환 API 호출
8. 각 분기에서 상태 변경(등록/전이) 발생 직후 data_agent의 저장 함수 호출 지점 삽입

## 산출물
- `controller/MenuController.h` (+ .cpp)
- View로 넘기는 데이터 형태(DTO/구조체)를 view_agent와 합의
