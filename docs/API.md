# API

<a id="hierarchical-xml-documents"></a>

## 계층적 XML 문서

`XmlTreeDocument::fromXml()` 는  `iiXml::Parser::TagDocument` 를 공중 헤더에  iiXml 타입을 노출하지 않은 엄격한 단일 루트 계층 구조로 변환합니다 각  `XmlNode` 는  런타임 -안정적인  ID , 선택적 부모  ID , 순차 직접 자식 ID, 계층 구조 깊이, 이름, 내부/원시  XML , 정확한 열기 및 닫기 태그 뷰, 속성, 자기 닫기 상태, 및 전체 문서 바이트 범위를 제공합니다 `openingTag()` 는 속성과 최종  `>` 를 포함하며;  `closingTag()` 는 자기 닫기 노드의 경우 비어 있습니다 `XmlAttribute` 는  iiXml 의 문자, 정수, 실수, 및 부울 타입 추론을 유지합니다

`XmlTreeEditor`는 하위 트리 수준 CRUD를 제공합니다.

```cpp
auto document = XmlTreeDocument::fromXml(
    "<catalog><item>Existing</item></catalog>");
XmlTreeEditor editor(document);

const XmlNodeId catalogId = *document.rootId();
const XmlNodeId created = editor.create(
    "<item enabled=true>Created</item>", catalogId);
const XmlNode* node = editor.read(created);
editor.update(created, "<entry>Updated</entry>");
editor.remove(created);
```

Create 는 정확히 하나만 있는 맨홀 요소 단편을 받아들이고 이를 마지막 자식으로 추가합니다 비어 있는 편집자 상태는 하나의 새로운 루트를 허용하며; 비어 있지 않은 문서는 두 번째 루트를 거부합니다 자기 닫힘 부모 아래 생성하면 이를 짝 태그로 확장합니다. 업데이트는 선택된 루트 ID 를 유지하면서 하위 요소를 대체하며, 제거는 선택된 전체 서브트리를 삭제합니다. 모든 소스 변경 변형은 원자적 커밋 전에 임시 상태에 파싱되고 동일성 조정됩니다. `XML_TREE_CRUD.md` 를 참조하여 프롤로그 처리, 포인터 수명, 동일성, 및 문법 경계를 확인하세요.

<a id="html-block-documents"></a>

## HTML 블록 문서

`HtmlBlockDocument::fromHtml()` 는 `iiHtmlBlock` 를 사용하여 모든 블록 요소를 인식하면서 완전한 소스 문자열을 보존합니다. `rootIds()` 는 소스 순서로 최상위 블록 루트를 노출합니다. 모든 `HtmlBlock` 는 안정적인 ID, 선택적 부모 ID, 순차 직접 자식 ID, 깊이, 정확한 열림 및 닫힘 태그 뷰, 자기 닫힘 상태, 태그, 값, 원본 HTML, 소스 범위, 및 디스플레이 오버라이드 메타데이터를 불변 뷰로 노출합니다.

부모 관계는 iiHtmlBlock / iiXml 가 제공하는 일치하는 열림/닫힘 범위에서 결정됩니다: 블록을 완전히 포함하는 가장 가까운 블록 범위가 그 부모입니다. 서로를 완전히 포함하지 않는 교차 닫힘 오버레이 범위는 가짜 트리 관계를 받는 대신 독립적인 루트로 남습니다.

`HtmlBlockEditor`는 블록 수준 CRUD를 제공합니다.

```cpp
auto document = HtmlBlockDocument::fromHtml(
    "<main><p>Existing</p></main>");
HtmlBlockEditor editor(document);

const HtmlBlockId mainId = document.blocks().front().id();
const HtmlBlockId created = editor.create("<p>Created</p>", mainId);
const HtmlBlock* block = editor.read(created);
editor.update(created, "<article>Updated</article>");
editor.remove(created);
```

생성은 문서 수준에서 추가하거나 제공된 부모의 마지막 자식으로 추가합니다. 생성 및 업데이트는 정확히 하나의 직렬화된 블록 루트를 받습니다. 업데이트는 대상 ID 를 유지하며, 제거는 선택된 블록과 그 중첩된 동일성을 삭제합니다. 모든 성공적인 변형은 임시 상태를 다시 파싱하고 `revision()` 를 한 번 증가시키며, 모든 실패는 완전히 롤백됩니다. `HTML_BLOCK_CRUD.md` 를 참조하여 동일성, 포인터 수명, iiXml 중첩, 및 허용된 문법 경계를 확인하세요.

<a id="flow-text-documents"></a>

## 유동 텍스트 문서

`WordDocument` 는 PDF 페이지 모델과 분리된 순차, 흐름 지향 모델입니다. 그 `WordBlock` 값은 `WordParagraph` 또는 `WordTable` 입니다. 문단 줄은 Unicode 텍스트, 탭, 줄 바꿈, 강조, 별도의 라틴/동아시아 글꼴, 크기, 및 색상을 포함하여 독립적으로 편집 가능하게 유지됩니다. 페이지 치수와 여백은 Word twips 를 사용합니다. `WordParagraphProperties::numberingId` 과 `numberingLevel` 는 목록과 그 깊이를 식별하며, `numberingContinuation` 는 동일한 ODF 목록 항목에 속하는 추가 문단을 표시하여 다른 번호 매기기 항목을 생성하지 않습니다.

`WordDocumentReader` 와 `WordDocumentWriter` 는 `.docx`, `.doc`, `.odt`, 또는 `.fodt` 에서 백엔드를 선택합니다. DOCX, ODT, 및 FODT 는 네이티브 패키지/XML 코덱을 사용합니다. DOC 는 LibreOffice 를 격리된 변환 프로세스로 사용하며, 호출자는 일치하는 옵션 타입에서 실행 가능 파일과 시간 제한을 지정할 수 있습니다. 각 백엔드는 안정적인 진단과 `hasErrors()` 를 반환합니다. 리더 및 작성자 옵션은 모두 `maximumXmlPartBytes` 를 노출하며, DOCX 작성자는 생성된 모든 XML/관계 부분과 모든 네이티브 작성자가 사전 커밋 재오프닝 검증을 위해 이를 사용하므로 실패한 검증은 기존 DOCX/ODT/FODT 목적지를 결코 대체하지 않습니다.

```cpp
WordDocument document;
WordParagraph paragraph;
paragraph.runs.push_back({"Hello Word", {.fontFamily = "Arial"}});
document.appendParagraph(std::move(paragraph));

auto result = WordDocumentWriter{}.write(document, "hello.docx");
auto reopened = WordDocumentReader{}.read("hello.docx");
auto odf = WordDocumentWriter{}.write(reopened.document, "hello.odt");
```

지원되는 객체 매트릭스, 충실도 경계, LibreOffice 런타임 계약, 보안 제한 및 검증 게이트는 `WORD_SUPPORT.md`를 참조하세요. ODT/FODT 패키징, 의미 체계 매핑 및 CRUD 제한은 `ODF_SUPPORT.md`를 참조하세요.

<a id="thinking-space-documents"></a>

## Thinking Space 문서

`ThinkingSpaceDocument`는 `.tsdoc` 형식에 대한 메모리 내 객체 경계입니다. `ThinkingSpaceDocumentHeader`는 독립적으로 주소를 지정할 수 있는 사용자 정의 태그 블록을 위해 기존 `HtmlBlockDocument`를 소유하는 `ThinkingSpaceDocumentBody`와 별도로 문자열 키-값 메타데이터를 유지합니다.

`recordVersion(label, createdAtUtc)` 는 현재 헤더와 정확한 본문 HTML 를 `ThinkingSpaceDocumentVersionHistory` 에 포착합니다. 헤더/본문 blob, 스냅샷 트리, 차이, 및 버전 커밋은 Git 스타일의 콘텐츠 주소 기반 SHA-256 ID 를 사용합니다. 각 버전은 부모 커밋, 대상 스냅샷, 및 가역적 차이 객체를 가리킵니다. 동일한 콘텐츠는 동일한 blob/트리 ID 를 생성합니다. `verifyIntegrity()` 는 유지된 객체 그래프와 델타 재구성을 확인합니다.

최신 100 버전을 시간순으로 유지합니다. 오버플로가 오래된 커밋을 먼저 제거하고 참조되지 않은 스냅샷/디프 객체를 가비지 수집합니다. 첫 번째 유지된 커밋은 불변 ID 를 재작성하지 않고 삭제된 부모 ID 를 얕은 기록 경계로 보존합니다. 기록은 명시적이며, 일반 헤더 또는 본문 편집은 커밋이 아닙니다.

`.tsdoc` 판독기, 기록기, 물리적 봉투, 필수 메타데이터 스키마 또는 사용자 정의 태그 어휘가 아직 정의되지 않았습니다. 정확한 구현 및 지연 계약은 `THINKING_SPACE_DOCUMENT.md`를 참조하세요.

<a id="document-model"></a>

## 문서 모델

`Document`는 페이지, 메타데이터 및 터미널 AcroForm 필드를 소유합니다. `Page`는 미디어 상자, 회전, 정렬된 요소 및 주석을 소유합니다. 문서와 페이지는 이동 전용이므로 리소스 ID가 실수로 복제되지 않습니다.

모든 페이지 요소에는 다음이 포함됩니다.

- 안정적인 문서 전체 `ElementId`;
- `ElementKind`;
- 주문된 변경 가능한 `std::vector<PdfInstruction>`;
- 추측 없이 파생될 수 있는 경우 선택적 범위입니다.

`PdfValue`는 null, 부울, 정수, 실수, 이름, 문자열 바이트, 배열, 사전 또는 보존된 원시 구문을 나타냅니다. 배열과 사전은 재귀적입니다. `toPdfSyntax()`는 쓰기 전에 항상 이름과 리터럴 문자열을 이스케이프합니다.

<a id="high-level-editing"></a>

## 높은 수준의 편집

`DocumentEditor`는 고유 ID를 할당하고 일반적인 작업을 제공합니다.

```cpp
Document document;
document.addPage(Page(Rect{0, 0, 612, 792}));

DocumentEditor editor(document);
const auto text = editor.addText(0, "Hello", Point{72, 700}, 18);
const auto border = editor.addRectangle(0, Rect{60, 640, 200, 40});
editor.replaceText(text, 0, "Hello PDF");
editor.remove(border);
```

`find`, `replaceText`, `replaceImage` 및 `remove`는 최상위 페이지 요소뿐만 아니라 재귀 양식 XObject 콘텐츠를 트래버스합니다. 공유 양식은 공유 `FormContent`를 사용하기 때문에 하나의 중첩 요소는 여러 번 그려도 하나의 ID를 갖습니다.

일반적이지 않은 작업의 경우 `Element::instructions()`를 직접 편집하세요. 호출자는 유효한 PDF 연산자 개수 및 리소스 참조에 대한 책임을 집니다. `DocumentValidator`는 구조적 불변성을 검사하지만 완전한 ISO 32000 적합성 검사기는 아닙니다.

<a id="images"></a>

## 이미지

`ImageElement`는 배치 지침을 `ImageInfo`와 분리합니다. 판독기는 차원, 색상 공간, 필터/디코드 매개변수 구문, 인코딩된 스트림 바이트 및 소스 객체 참조를 노출합니다. `replaceImage`는 새 바이트와 전체 이미지 디코딩 계약을 허용합니다.

필터링되지 않은 8비트 `/DeviceRGB` 페이로드는 정확히 `width * height * 3` 바이트를 포함해야 합니다. 인코딩된 데이터는 `/DCTDecode`와 같은 필터를 지정할 수 있습니다. 바이트는 이미 선언된 필터와 일치해야 합니다.

<a id="annotations-and-forms"></a>

## 주석 및 양식

주석은 페이지 페인트 지침 외부에 표시됩니다. 표준 하위 유형, 직사각형, 내용, 플래그 및 전체 PDF 사전을 독립적으로 편집할 수 있습니다. 쓰기는 페이지 주석 배열을 다시 작성하여 사전에 포함된 간접 필드/위젯 관계를 유지합니다.

기존 터미널 AcroForm 필드는 이름, 유형, 값, 플래그, 선택지 및 출처 동일성을 노출합니다. 값이 QPDF 를 지원할 때 텍스트와 선택지 출력이 재생성됩니다. ASCII 가 아닌 값은 정통 필드 값을 업데이트하고 경고가 생성되며, QPDF 는 그 출력을 신뢰할 수 있게 합성할 수 없습니다. 새로운 AcroForm /위젯 계층 구조를 만드는 것은 버전 0.1밖입니다.

텍스트 및 선택 값에는 `FormField::setUtf8Value()`를 사용하십시오. 작성자는 이를 PDF Unicode 문자열로 인코딩합니다. `setValue()`는 버튼 이름 및 기타 하위 수준 PDF 값에 계속 사용할 수 있습니다.

<a id="results-and-diagnostics"></a>

## 결과 및 진단

독자와 작성자는 `ReadResult` 및 `WriteResult`를 반환합니다. 출력을 사용하거나 전달하기 전에 `hasErrors()`를 확인하십시오. 치명적이지 않은 수리, 모양 및 쓰기 후 경고는 안정적인 코드 및 컨텍스트 문자열과 함께 `diagnostics`에 남아 있습니다.
