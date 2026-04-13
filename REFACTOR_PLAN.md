# REFACTOR PLAN

## 1) 현재 아키텍처 요약

### 플러그인/모듈 구조
- 플러그인은 `CadImporter`(Runtime)와 `CadImporterEditor`(Editor) 2개 모듈로 구성되어 있으며, 런타임은 로봇 액터/소켓 연동, 에디터는 워크플로우 UI/JSON 파싱/블루프린트 생성/레벨 교체를 담당한다.
- `CadImporter` 모듈은 `Core`, `CoreUObject`, `Engine`에 의존하고, 소켓 통신을 위해 `TcpSocketPlugin`을 private dependency로 사용한다.
- `CadImporterEditor` 모듈은 `CadImporter`를 포함해 `UnrealEd`, `AssetRegistry`, `Slate`, `ToolMenus` 등 에디터 의존성이 폭넓게 연결되어 있다.

### 주요 진입점
- Runtime 진입점: `FCadImporterModule` (`StartupModule`, `ShutdownModule`).
- Editor 진입점: `FCadImporterEditorModule`.
  - 스타일 등록/해제
  - 워크플로우 탭/메뉴 등록
  - 드라이런 폴더 정리 티커 등록
  - `FCadImportService`를 생성해 `SCadWorkflowWizard`에 주입

### 레이어(현재 상태)
- **UI 레이어**: `Private/UI/*` (`WorkflowWizard.cpp`, `WorkflowWizardJointEditor.cpp` 중심)
- **오케스트레이션 레이어**: `ImportService.cpp`
- **도메인 모델/DTO**: `Public/WorkflowTypes.h`, `Public/ImportModelTypes.h`
- **JSON I/O**: `ChildDocParser.cpp`, `ChildDocExporter.cpp`, `MasterDocExporter.cpp`
- **빌드/임포트 실행**: `Private/Workflow/*`, `Private/Import/*`
- **씬 치환/에디터 계층 조작**: `LevelReplacer.cpp`, `Editor/ActorHierarchyUtils.cpp`
- **Runtime 제어**: `CadRobotActor.cpp`, `CadMasterActor.cpp`

## 2) 문제 지점 및 코드 스멜

### 심각도 상 (즉시 관리 필요)
1. **초대형 UI 파일로 인한 책임 혼재**
   - `WorkflowWizard.cpp`(약 2,100+ 라인), `WorkflowWizardJointEditor.cpp`(약 1,500+ 라인)에서
     상태 관리, 입력 검증, 드라이런 파일 처리, 조인트 애니메이션, 빌드 트리거까지 결합되어 있음.
   - 테스트가 어려우며 회귀 위험이 큼.

2. **오케스트레이션 계층의 과도한 결합**
   - `ImportService.cpp`가 경로 해석, 컨텐츠 루트 준비, 마스터/차일드 재귀 빌드, 에러 다이얼로그 표시,
     블루프린트 태그 조작까지 모두 수행.
   - 순수 로직과 엔진 부수효과(AssetRegistry 스캔, 다이얼로그, 패키지 dirty)가 섞여 있어 단위 테스트가 어려움.

3. **레벨 치환 로직의 복잡도/영향 반경 과대**
   - `LevelReplacer.cpp`는 대규모 파일이며 액터 파괴/교체/계층 탐색/중첩 마스터 처리까지 담당.
   - 실패 시 월드 상태 손상 리스크가 있어 보호 장치(사전 시뮬레이션/검증 리포트)가 더 필요.

### 심각도 중
4. **문자열 기반 식별자와 경로 처리 분산**
   - ActorPath/JsonFileName/ContentRootPath를 여러 계층에서 개별 정규화/조합.
   - 동일 개념의 경로 정규화 규칙이 분산되어 버그 잠재.

5. **공통 규칙의 중복 구현 가능성**
   - 노드 타입/차일드 타입 변환, JSON 필드 검증, 이름 정규화가 다수 파일에 산재.

6. **성능 측정 인프라 부재**
   - 대규모 계층, 다수 child JSON, 에셋 스캔 등 비용이 큰 작업이 있으나 정량 벤치 체계가 없음.

### 심각도 하
7. **Build.cs 템플릿 잔재 코멘트/정리 필요**
   - 동작에는 영향 없으나 유지보수 가독성 저하.

## 3) 목표 아키텍처

### 핵심 원칙
- 기존 동작/포맷/CLI(API) 유지.
- “UI 이벤트 핸들러”와 “도메인 유스케이스”를 분리.
- 파일시스템/에셋레지스트리/다이얼로그 같은 부수효과를 인터페이스 뒤로 격리.
- 대형 파일을 기능 단위로 수평 분해하되, 공개 타입은 최소 변경.

### 제안 구조
- `Workflow/Application` (신규): 워크플로우 유스케이스(선택 검증, flatten 계산, build orchestration)
- `Workflow/Domain` (신규): 순수 데이터 변환/검증/매핑
- `Workflow/Infrastructure` (신규): UE 종속 I/O (AssetRegistry, 파일, 에디터 트랜잭션)
- `UI`는 상태 바인딩 + 사용자 상호작용만 담당

## 4) 점진적 배치 계획 (Incremental Batches)

### Batch 0 — 안전망 확보 (선행)
- 목표: 리팩터 전에 현재 동작을 고정하는 characterization baseline 구축
- 작업:
  - JSON 파서/익스포터 round-trip 샘플 픽스처 작성
  - 경로 해석(`WorkflowBuildInputResolver`) 케이스 테이블화
  - 문자열 enum 변환(`CadImportStringUtils`) 회귀 테스트 작성
- 변경 후보 파일:
  - `Source/CadImporterEditor/Private/Workflow/WorkflowBuildInputResolver.cpp`
  - `Source/CadImporterEditor/Private/CadImportStringUtils.cpp`
  - `Source/CadImporterEditor/Private/ChildDocParser.cpp`
  - `Source/CadImporterEditor/Private/ChildDocExporter.cpp`
  - (테스트 인프라 신규) `Source/CadImporterEditor/Private/Tests/*` 또는 프로젝트 규약 위치

### Batch 1 — ImportService 분해 (고위험도 대비 효과 큼)
- 목표: `ImportService.cpp`를 순수 계산과 UE 부수효과로 분리
- 작업:
  - 경로/문서 해석 로직을 `Workflow/Application` 유틸로 추출
  - 다이얼로그 표시를 인터페이스(`ICadUserNotifier`)로 캡슐화
  - 블루프린트 태그 조작 및 컨텐츠 루트 준비를 작은 서비스로 분리
- 변경 후보 파일:
  - `Source/CadImporterEditor/Private/ImportService.cpp`
  - `Source/CadImporterEditor/Private/ImportService.h`
  - `Source/CadImporterEditor/Private/Workflow/*` (신규 보조 클래스 포함)

### Batch 2 — Workflow UI 상태/로직 분리
- 목표: `WorkflowWizard*.cpp`의 거대 상태 머신을 기능 단위 presenter/controller로 분리
- 작업:
  - Step별 핸들러(`SelectionStep`, `FlattenStep`, `JointStep`, `BuildStep`) 도입
  - 드라이런 폴더 정리/프리뷰 상태를 독립 서비스로 이동
  - UI 위젯은 ViewModel 접근만 수행
- 변경 후보 파일:
  - `Source/CadImporterEditor/Private/UI/WorkflowWizard.cpp`
  - `Source/CadImporterEditor/Private/UI/WorkflowWizardJointEditor.cpp`
  - `Source/CadImporterEditor/Private/UI/WorkflowWizardSharedUtils.cpp`
  - (신규) `Source/CadImporterEditor/Private/UI/Steps/*`

### Batch 3 — LevelReplacer 안정화
- 목표: 레벨 교체 전 검증 단계와 롤백 단서 강화
- 작업:
  - “적용 전 계획(Plan)” 생성 함수 분리
  - 삭제/교체 후보의 사전 검증 리포트화
  - 실패 시 복구 경로를 명시적으로 기록
- 변경 후보 파일:
  - `Source/CadImporterEditor/Private/LevelReplacer.cpp`
  - `Source/CadImporterEditor/Public/LevelReplacer.h`

### Batch 4 — Runtime 제어 최소 개선
- 목표: 동작 유지하며 `CadRobotActor`의 통신/제어 루프 가시성 개선
- 작업:
  - 소켓 수신 버퍼 처리와 command 적용 경계를 명확화
  - Tick당 수행량 계측 포인트 추가(로깅/스코프 타이머)
- 변경 후보 파일:
  - `Source/CadImporter/Private/CadRobotActor.cpp`
  - `Source/CadImporter/Public/CadRobotActor.h`

## 5) 동작 동일성(Behavior Parity) 검증 계획

- JSON 계약 테스트
  - master/child JSON 파싱 성공/실패 케이스 고정
  - exporter 결과를 canonical JSON 비교(필드 누락/명칭/타입)
- 워크플로우 시나리오 테스트
  - 단일 child, 다중 child, nested master, flatten 적용/미적용
- 블루프린트/레벨 변경 결과 검증
  - 생성 자산 경로, 태그(Background), 계층 관계 비교
- 수동 스모크 체크
  - 에디터 탭 열기 → 워크스페이스 지정 → JSON 생성 → Build → Revert

## 6) 성능 검증 계획

### 기존 인프라
- 저장소 내 전용 benchmark 러너는 현재 확인되지 않음.

### 최소 재현 측정안
1. 입력 데이터셋 3종 고정 (small/medium/large)
2. 아래 구간 wall-clock 측정(동일 머신/동일 프로젝트 상태)
   - master/child JSON 파싱 시간
   - child blueprint 생성 시간
   - level replacement 시간
3. UE 로그 기반 측정 포인트 삽입 (`FPlatformTime::Seconds` diff)
4. 리팩터 전/후 5회 반복, 중앙값 비교

### 최적화 적용 원칙
- 중앙값 기준 10% 이상 개선 또는 메모리 피크 완화가 확인된 경우만 성능 개선 채택
- 개선 근거가 없으면 구조 개선만 반영

## 7) 롤백 전략

- 배치 단위로 커밋/태깅하여 역추적 가능하게 유지
- 기능 플래그(가능하면 에디터 설정)로 신규 경로를 선택 실행
- 각 배치에서 공개 타입/JSON 스키마 변경 금지 원칙 적용
- 문제 발생 시 배치 단위 revert + characterization 테스트로 회귀 범위 확인

## 8) 오픈 이슈 / 가정

- UE 자동화 테스트 프레임워크를 이 저장소 단독으로 실행 가능한지(상위 프로젝트 의존 여부) 확인 필요
- 실제 운영에서 가장 큰 입력 규모(자식 수/중첩 깊이/메시 수) 데이터 필요
- `TcpSocketPlugin` 연동 경계의 실패/재시도 정책 요구사항 명확화 필요
