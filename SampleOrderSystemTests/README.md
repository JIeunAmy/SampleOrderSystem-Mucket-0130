# SampleOrderSystemTests (준비 중)

이 디렉터리는 TDD_Agent Phase 1에서 준비한 테스트 전용 프로젝트의 골격이다.
실제 `.vcxproj` 및 테스트 소스 코드(`*.test.cpp`)는 model_agent/produce_agent/data_agent가
각 계층 구현에 착수하기 직전(Phase 2/3/4)에 TDD_Agent가 먼저 작성하고, orchestrator가
빌드 시스템(솔루션) 통합을 진행한다.

## 현재 포함된 것
- `vendor/catch2/catch_amalgamated.hpp`, `vendor/catch2/catch_amalgamated.cpp`
  (Catch2 v3.5.4 단일 헤더 앰알거메이션, https://github.com/catchorg/Catch2 에서 받음)

## 아직 없는 것 (Phase 2 진입 시 orchestrator/TDD_Agent가 추가 예정)
- `SampleOrderSystemTests.vcxproj` (콘솔 앱, Catch2 amalgamated 소스 포함, main은 Catch2 기본 러너 사용)
- `Sample.test.cpp`, `Order.test.cpp` 등 계층별 테스트 소스
- 솔루션(`SampleOrderSystem.slnx`)에 테스트 프로젝트 추가 및 참조 설정
  (테스트 프로젝트는 `SampleOrderSystem.vcxproj`의 model/produce/data 소스를 정적 라이브러리 형태로
  참조하거나, 필요한 .cpp를 직접 포함하는 방식 중 orchestrator가 결정)

자세한 통합 계획은 `docs_temp/tdd/phase.md`의 "테스트 프레임워크 통합 계획" 절 참고.
