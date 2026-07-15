# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 프로젝트 개요

"S-Semi"라는 가상의 반도체 회사를 위한 **반도체 시료(Sample) 생산주문관리 콘솔 애플리케이션**이다.
고객이 시료를 요청하면 주문 담당자가 주문서를 작성하고, 생산 담당자가 재고를 확인하여 승인/거절하며,
재고가 부족하면 생산 라인에서 자동으로 생산 후 고객에게 출고하는 흐름을 다룬다.
상세 기능 명세는 `docs/[CRA_AI] Day3_개인과제_반도체시료관리.pdf`를 참고할 것 (이 저장소의 유일한 요구사항 원본 문서).

이 시스템은 **Model / View / Controller(MVC)** 구조로 구현한다.
- Model: 시료(Sample), 주문(Order), 생산 라인(ProductionLine)과 같은 도메인 데이터 및 상태 전이 규칙
- View: 콘솔 입출력(메뉴 표시, 사용자 입력 프롬프트, 결과 출력)
- Controller: 사용자 입력을 받아 Model을 조작하고 View 갱신을 지시하는 흐름 제어

Model/View/Controller는 서로 독립된 패키지(디렉터리)로 분리하고, View가 Model을 직접 조작하거나
Model이 View를 직접 호출하지 않도록 역할을 엄격히 나눈다.

## 빌드 및 실행

Visual Studio C++ 콘솔 프로젝트이다 (CMake 아님).

- 솔루션 파일: `SampleOrderSystem.slnx`
- 프로젝트 파일: `SampleOrderSystem/SampleOrderSystem.vcxproj`
- 언어 표준: C++20 (`LanguageStandard = stdcpp20`)
- 플랫폼 툴셋: v145, 대상 플랫폼: x86 / x64, 구성: Debug / Release

명령줄 빌드 (Developer PowerShell / VS Build Tools 환경에서):

```
msbuild SampleOrderSystem.slnx /p:Configuration=Debug /p:Platform=x64
```

빌드 결과 실행 파일은 `x64\Debug\SampleOrderSystem.exe` (또는 선택한 Platform/Configuration 경로)에 생성된다.

현재 저장소에는 소스 코드(.cpp/.h)나 테스트 프레임워크가 아직 존재하지 않는다.
새 소스 파일을 추가할 때는 `SampleOrderSystem.vcxproj`의 `<ItemGroup>`에 `ClCompile`/`ClInclude` 항목을 함께 추가해야
Visual Studio/MSBuild가 인식한다.

## 도메인 모델 및 상태 흐름

### 시료 (Sample)
등록된 시료만 주문 가능. 속성: 시료 ID, 이름, 평균 생산시간, 수율(정상 생산 시료 수 / 총 생산 시료 수).

### 주문 (Order) 상태
| 상태 | 의미 |
|---|---|
| RESERVED | 주문 접수(예약) |
| REJECTED | 주문 거절 (정상 흐름 외 상태, 모니터링 집계에서 제외) |
| PRODUCING | 승인 완료, 재고 부족으로 생산 라인에서 생산 중 |
| CONFIRMED | 승인 완료, 재고 충분하여 출고 대기 중 |
| RELEASE | 출고 완료 |

상태 전이: `RESERVED → (승인)` → 재고 충분 시 `CONFIRMED`, 재고 부족 시 `PRODUCING` / `RESERVED → (거절)` → `REJECTED` /
`PRODUCING → (생산 완료)` → `CONFIRMED` / `CONFIRMED → (출고 처리)` → `RELEASE`.

### 생산 라인 (Production Line)
- 하나의 생산 라인은 시료를 한 번에 하나씩만 생산하며, 주문이 들어온 시료만 생산한다.
- 대기열은 FIFO(선입선출) 큐로 스케줄링한다.
- 실 생산량 = `ceil(부족분 / 수율)`
- 총 생산 시간 = `평균 생산시간 * 실 생산량`
- 생산 완료 시 해당 주문 상태를 `PRODUCING → CONFIRMED`로 전환한다.

## 메인 메뉴 구성

콘솔 메뉴 기반으로 동작하며, 각 메뉴는 아래 기능에 대응한다.

| 메뉴 | 기능 |
|---|---|
| 시료 관리 | 시료 등록 / 전체 목록 조회(재고 포함) / 이름 등 속성으로 검색 |
| 시료 주문 | 고객명·시료 ID·수량을 입력받아 `RESERVED` 주문 생성 |
| 주문 승인/거절 | `RESERVED` 목록 표시 후 특정 주문을 승인(재고에 따라 `CONFIRMED`/`PRODUCING` 분기) 또는 거절(`REJECTED`) |
| 모니터링 | 상태별 주문 수 집계(REJECTED 제외), 시료별 재고 현황(여유/부족/고갈) 확인 |
| 생산 라인 | 현재 생산 중인 시료 정보와 FIFO 대기 큐 확인 |
| 출고 처리 | `CONFIRMED` 주문 중 하나를 선택해 `RELEASE`로 전환 |

화면 레이아웃 자체는 자유롭게 구성 가능하나(PDF는 예시 UI일 뿐), 위 상태 전이 규칙과 계산식(실 생산량, 총 생산 시간)은
기능 명세의 필수 요구사항이므로 반드시 준수해야 한다.

## 서브에이전트 구성 (`.claude/agents/`)

MVC 계층별 작업을 분리해서 위임할 수 있도록 아래 서브에이전트를 두고 있다.

| 에이전트 | 역할 |
|---|---|
| `orchestrator` | 요청을 Model/View/Controller 작업으로 분해해 각 에이전트에 위임하고, 의존 순서(Model 인터페이스 → View/Controller)를 관리하며, 계층 간 인터페이스 불일치를 검증하고 빌드 결과를 확인한다. 직접 소스 코드를 작성하지 않는다. |
| `model_agent` | Sample/Order/ProductionLine 도메인 클래스, 상태 전이 로직, 실 생산량·총 생산 시간 계산식, 재고/주문 저장소를 구현한다. 콘솔 출력이나 입력 처리, 메뉴 분기에는 관여하지 않는다. |
| `view_agent` | 콘솔 메뉴 출력, 입력 프롬프트, Controller가 전달한 데이터의 화면 렌더링(목록/표/상태 뱃지 표시)을 담당한다. 도메인 로직이나 메뉴 분기 흐름 제어에는 관여하지 않는다. |
| `controller_agent` | 메뉴 선택에 따른 분기 처리, View 입력 검증 후 Model API 호출, Model 결과를 View에 전달하는 흐름 제어를 담당한다. 계산 로직이나 출력 문자열을 직접 구현하지 않는다. |

시료(Sample) 관리 기능(등록/조회/검색) 구현 시에는 `.claude/skills/Sample/SKILL.md` 스킬도 함께 참고한다.
