#pragma once

// TODO(Phase 5, controller_agent): MenuController 실제 구현 예정.
//
// 이 헤더는 아직 골격(Phase 0) 단계이며, 아래 내용은 이후 단계에서 채워 넣을 설계 개요다.
// 실제 메뉴 분기 로직/Model·Produce·Data API 호출 코드는 이번 범위에 포함되지 않는다.
//
// 1) 메인 메뉴 분기 (6개)
//    - 시료 관리: 시료 등록 / 전체 목록 조회(재고 포함) / 이름 등 속성으로 검색
//      -> View로부터 입력을 받아 model_agent의 Sample API(등록/조회/검색) 호출
//    - 시료 주문: 고객명·시료 ID·수량 입력받아 RESERVED 주문 생성
//      -> 입력 검증(존재하는 시료 ID인지 등) 후 model_agent의 Order 생성 API 호출
//    - 주문 승인/거절: RESERVED 목록을 View에 표시 후 특정 주문을 승인/거절
//      -> 승인 시: model_agent에 재고 확인을 위임하고, 결과에 따라
//         CONFIRMED로 전환하거나 PRODUCING으로 전환 + produce_agent의 생산 큐 등록 API 호출
//      -> 거절 시: model_agent의 주문 상태를 REJECTED로 전환하는 API 호출
//    - 모니터링: 상태별 주문 수 집계(REJECTED 제외), 시료별 재고 현황(여유/부족/고갈) 조회
//      -> model_agent 조회 API 호출 결과를 View에 전달
//    - 생산 라인: 현재 생산 중인 시료 정보와 FIFO 대기 큐 조회
//      -> produce_agent의 ProductionLine 조회 API 호출 결과를 View에 전달
//    - 출고 처리: CONFIRMED 주문 중 하나를 선택해 RELEASE로 전환
//      -> model_agent의 주문 상태 전환 API 호출
//
// 2) Model / Produce / Data API 호출 흐름
//    - Controller는 View의 입력(시료 ID, 고객명, 수량, 주문 번호 등)을 받아 검증한 뒤
//      model_agent / produce_agent가 제공하는 API만 호출하고, 실 생산량·총 생산 시간·재고
//      증감 등의 계산 로직은 각 계층에 위임한다.
//    - data_agent가 제공하는 Repository API를 통한 영속화 호출도 model_agent/produce_agent를
//      경유하며, Controller가 직접 파일 I/O를 수행하지 않는다.
//    - 각 분기의 처리 결과(성공/실패, 갱신된 데이터)는 View에 전달하여 화면을 갱신시킨다.
//
// class MenuController { ... }; // Phase 5에서 구현
