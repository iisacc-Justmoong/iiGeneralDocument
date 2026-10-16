<a id="word-support-contract"></a>

# 워드 지원 계약

<a id="formats-and-backends"></a>

## 형식 및 백엔드

대상 또는 소스 확장에 의한 `WordDocumentReader` 및 `WordDocumentWriter` 디스패치:

|형식|읽기|쓰기|백엔드|
| --- | --- | --- | --- |
| `.docx` |예|예|네이티브 OOXML/OPC 구현(pugixml XML 및 비공개 사용) 링크된 libzip|
| `.doc` |예|예|고립된 LibreOffice 를 네이티브 DOCX 코덱으로 변환하거나 그로부터 변환하는 것|
| `.odt` |예|예|네이티브 OpenDocument ZIP pugixml XML 및 비공개를 사용하여 구현 libzip|
| `.fodt` |예|예|pugixml XML를 사용한 네이티브 플랫 OpenDocument XML 구현|

DOCX 는 ZIP 패키지로, 고정된 경로로 가정되는 대신 OPC 패키지 관계를 통해 주요 문서 부분이 발견됩니다. 레거시 DOC 는 Word 97-2003 바이너리 컴파운드 파일 형식입니다. 라이브러리는 해당 바이너리 사양을 재구현한다고 주장하지 않으며, `.doc` 를 위해 로컬에 설치된 `soffice` / `libreoffice` 실행 가능 파일을 요구합니다. `PATH` 에 있지 않을 때 `libreOfficeExecutable` 를 `WordReadOptions` 또는 `WordWriteOptions` 에 설정합니다.

ODT 와 FODT 는 LibreOffice 런타임 없이 동일한 편집 가능한 흐름 모델을 사용합니다. 그들의 상세한 ODF 버전, 패키지, 의미 매핑, 보안, 및 손실 계약은 `ODF_SUPPORT.md` 에 있습니다. 두 작성자는 원자적 교체 전에 같은 디렉토리의 임시 결과를 검증하며, 문서를 덮어쓸 때 기존 POSIX 모드를 유지합니다. 새로운 네이티브 문서는 같은 디렉토리의 POSIX `0666` 생성 탐지기에 의해 생성된 모드를 사용하므로, 읽거나 변경하지 않고 프로세스 umask 가 존중됩니다.

<a id="editable-word-model"></a>

## 편집 가능한 Word 모델

`WordDocument`는 흐름 지향적이며 페이지 좌표 PDF 모델과 독립적입니다. 순서가 지정된 본문 블록을 유지하고 다음을 노출합니다.

- 핵심 제목, 저자, 주제, 설명, 키워드, 제작자 및 수정 메타데이터;
- 스타일 식별자, 정렬, 번호 식별/레벨 및 ODF 동일 목록 항목 연속 플래그가 포함된 단락;
- 탭, 줄 바꿈, 굵게, 이탤릭, 밑줄, 라틴 및 동아시아 글꼴 패밀리, 포인트 크기, 그리고 RGB 색상을 포함한 독립적으로 편집 가능한 텍스트가 제공됩니다;
- 정렬된 행, 셀 및 셀 단락이 있는 테이블;
- Word 트윕의 페이지 너비, 높이 및 여백.

새로운 DOCX 파일에는 명시적인 Normal, Title, Subtitle, 및 Heading 1-3 스타일, 정확한 섹션 기하학, 실제 번호 정의, 및 고정된 DXA 테이블 기하학이 포함되어 있습니다. 작성자는 ZIP 를 libzip 를 통해 원자적으로 커밋한 후 성공을 보고하기 전에 결과를 다시 엽니다. 레거시 DOC 출력은 성공이 보고되기 전에 DOCX 로 다시 변환되어 다시 파싱됩니다.

<a id="explicit-limits"></a>

## 명시적 제한

- 이미지, 도면, 내장 객체, 헤더, 푸터, 주석, 주석, 끝주석, 필드, 콘텐츠 컨트롤 및 중첩된 표는 버전 0.1에서 1 급 모델 항목이 아닙니다.
- 하이퍼링크 표시 텍스트는 읽히지만, 하이퍼링크 관계는 평탄화되어 경고로 보고됩니다.
- 삽입된 추적 텍스트는 현재 콘텐츠로 읽히게 됩니다. 추적된 삭제는 제외되며 경고로 보고됩니다.
- 지원되지 않는 본문 블록과 비텍스트 실행 콘텐츠는 조용한 전체 충실도 주장보다 진단을 생성합니다. 해당 항목들을 포함하는 소스를 다시 작성하면 해당 항목을 생략할 수 있습니다.
- 비밀번호로 보호되거나 암호화된 패키지는 지원되지 않습니다.
- 각 XML 부품은 기본적으로 64 MiB로 제한됩니다. `WordReadOptions`는 읽는 동안 값을 적용합니다. 아카이브 커밋 전에, `WordWriteOptions`는 제한을 초과한 모든 생성된 DOCX `.xml` 및 `.rels` 부품을 거부하고, 동일한 제한을 필수 재개 검증에 적용합니다. 신뢰할 수 있는 문서에 대해서만 `maximumXmlPartBytes`를 재정의하십시오.
- LibreOffice 변환 충실도는 설치된 LibreOffice 빌드와 해당 Microsoft Word 필터에 따라 달라집니다. 변환기 누락, 시간 초과, 변환 오류 또는 읽을 수 없는 변환 출력 안전하게 거부한다.

<a id="example"></a>

## 예

```cpp
#include <iiGeneralDocument/iiGeneralDocument.h>

using namespace ii::document;

WordDocument document;
document.metadata()["Title"] = "Word example";

WordParagraph paragraph;
paragraph.properties.styleId = "Heading1";
paragraph.runs.push_back({"Editable heading", {.bold = true}});
document.appendParagraph(std::move(paragraph));

auto written = WordDocumentWriter{}.write(document, "example.docx");
if (written.hasErrors()) {
    return 1;
}

auto read = WordDocumentReader{}.read("example.docx");
std::get<WordParagraph>(read.document.blocks().front()).runs.front().text =
    "Revised heading";
return WordDocumentWriter{}.write(read.document, "revised.docx").hasErrors();
```

<a id="verification"></a>

## 검증

`iiGeneralDocument.WordModel` 는 편집 가능한 객체 계약을 수정합니다. `iiGeneralDocument.WordRoundTrip` 는 DOCX 에 포함된 Unicode 텍스트, 서식화된 런, 탭, 줄바꿈, 메타데이터, 및 테이블을 작성, 다시 엽니다, 편집하며, 다시 씁니다. `iiGeneralDocument.OdfRoundTrip`, `.OdfStyleCompatibility`, 및 `.OdfRobustness` 는 네이티브 ODT / FODT CRUD, 외부 스타일/레이아웃 매핑, 패키지 무결성, 리소스 제한, 및 원자적 덮어쓰기 행동을 포함합니다. `iiGeneralDocument.LegacyDocRoundTrip` 는 OLE 컴파운드 파일 서명을 확인하고 `soffice` 가 존재할 때 실제 LibreOffice DOC 쓰기/읽기 주기를 수행합니다. 릴리스 확인은 추가로 생성된 DOCX 를 페이지 PNG 로 렌더링하고 모든 페이지를 시각적으로 검사합니다.


## Native dependency migration (0.4)

The complete module now uses C++23 standard types and open-source backends.
Qt and the installed iiXml/iiHtmlBlock packages are not runtime or public-package
dependencies. Author/file APIs use `iiFileProvider::Native`; Word XML uses pugixml
and persistence uses nlohmann/json. The markup algorithms retain the first-party
range/typed-attribute semantics described above. See [migration details](QT_REMOVAL.md)
for byte-string API changes, ABI compatibility, prior-file verification and the
Thinking Space schema-2 byte-diff encoding.
