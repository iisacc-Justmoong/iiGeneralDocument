<a id="pdf-support-contract"></a>

# PDF 지원 계약

<a id="first-class-editable-objects"></a>

## 최고 수준의 편집 가능한 객체

|PDF 문제|읽기||쓰기|
| --- | --- | --- | --- |
|페이지 미디어·자르기 경계, 회전 및 UserUnit|예|예|예|
|텍스트 개체 및 문자열 피연산자|예|예|예|
|벡터 경로 및 페인트 연산자|예|예|예|
|이미지 XObject 및 인코딩된 바이트|예|예|예|
|재귀 양식 XObjects|예|예, 공유 리소스 의미 체계|예|
|인라인 이미지|예|명령어 수준|예|
|음영|예|명령어 수준|예|
|표시된 내용|예|명령어 수준|예|
|그래픽 상태 및 색상 연산자|예|명령어 수준|예|
|알 수 없는/미래 연산자|보존됨|명령어 수준|예|
|주석|예|예|예|
|기존 터미널 AcroForm 필드|예|예|예|
|문서 정보 사전|공통 키|예|예|

<a id="preserved-but-not-first-class-in-01"></a>

## 0.1에서는 보존되었으나 일류는 아님

개요, 목적지, 선택적 콘텐츠 그룹, 구조 트리, 페이지 레이블, JavaScript, 그리고 임의의 카탈로그 확장자는 로드된 문서가 다시 작성될 때 QPDF 소스 객체 그래프에 남아 있습니다. 아직 전용 도메인 객체가 없습니다. 페이지 콘텐츠 스트림을 교체하면 서명, 태그된 PDF 연결, 또는 프로덕트별 가정에도 여전히 영향을 미칠 수 있으므로 제품 워크플로우는 대표 파일을 테스트해야 합니다.

<a id="explicit-limits"></a>

## 명시적 제한

- 텍스트 피연산자는 글꼴로 인코딩된 바이트입니다. 임의의 내장 글꼴에 대한 범용 Unicode 추출 및 자동 재인코딩은 주장되지 않습니다.
- 새 문서에서는 Helvetica/WinAnsi 대체 경로를 사용합니다. 호출자 제공 글꼴을 포함하고 하위 설정하는 것은 구현되지 않습니다.
- 기존 페이지 벡터를 직접 재배열하는 쓰기 경로는 허용되지 않습니다. `DocumentEditor::extractPages/mergePages`는 순서·반복을 포함한 새 문서 객체와 소스 그래프를 구성하며 저장이 가능합니다.
- 기존 AcroForm 필드를 업데이트할 수 있습니다. 새로운 필드/위젯/모양 계층 구조를 구성하는 것은 구현되지 않습니다.
- 다시 작성한 후에는 디지털 서명이 보존되지 않습니다. 작성자는 `allowInvalidatingDigitalSignatures`가 true가 아닌 한 서명된 입력을 거부합니다.
- 암호화 설정은 재현되지 않습니다. 작성자는 `allowRemovingEncryption`가 true가 아닌 이상 암호화된 입력을 거부합니다.
- QPDF 복구 경고가 표시됩니다. 성공적인 복구는 손상된 소스가 제작자의 원래 의도와 일치한다는 증거가 아닙니다.

<a id="verification"></a>

## 검증

작가는 변형 전에 모델을 검증하고 QPDF 를 통해 작성하며, 목적지를 다시 엽니다. 모든 페이지 콘텐츠 스트림을 파싱하고 페이지 수를 확인한 후 경고 메시지를 반환합니다. 릴리스 검증은 추가로 Poppler 를 사용하여 대표 PDF 를 렌더링하고 텍스트, 경로, 이미지, 주석, 클리핑, 및 페이지 기하학을 시각적으로 확인합니다.

## 0.3 Acrobat operation absorption

[The detailed object and dependency contract](ACROBAT_ABSORPTION.md) describes
`DocumentEditor::extractPages/mergePages/rotatePages/addTextNote/setTextField/setMetadataBatch`,
`Document::removeMetadata/attachments/hasXfa`, `Page::cropBox/userUnit`,
`Annotation::author`, and the standard byte reader/writer entry points.
The same seven format operations are available in the standalone Qt-free
`iiGeneralDocument::Pdf` component. Attachment metadata now has readonly objects;
attachment payload mutation remains outside this contract. New manipulation
methods refuse signed, encrypted and XFA inputs and validate full batches before
committing changes. Text values are logical Unicode values; Unicode widget
appearance rendering/font embedding is not claimed.
