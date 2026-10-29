<a id="html-block-crud-support"></a>

# HTML 블록 CRUD 지원

<a id="scope"></a>

## 범위

`HtmlBlockDocument` 는 `iiHtmlBlock 0.1.0` 위에 구축된 편집 가능한 HTML / iiXml 소스 모델입니다. 원래 소스 바이트 문자열은 문서의 기준 원본 입니다. `iiXml::Markup 블록 추적기` 은 블록 요소와 그 소스 범위를 인식하며, 공개 iiGeneralDocument API 는 이러한 의존성 타입을 안정된 `HtmlBlockId` 객체로 변환합니다.

이 모듈은 의도적으로 PDF 페이지 모델, Word 흐름 모델 및 애플리케이션 지속성 레이어와 분리되어 있습니다. 메모리 내 블록 CRUD를 수행합니다. 파일이나 데이터베이스가 관련되어 있는 경우 호출자는 `HtmlBlockDocument::html()`를 로드하고 유지하는 책임을 집니다.

<a id="public-model"></a>

## 공개 모델

`HtmlBlockDocument::fromHtml()`는 제공된 소스를 바이트 단위로 보존하고 `HtmlBlock` 뷰의 순서가 지정된 읽기 전용 컬렉션을 노출합니다. 각 블록에는 다음이 포함됩니다.

- 문서 전체 `HtmlBlockId`;
- 선택적 상위 ID, 순서가 지정된 직계 하위 ID 및 계층 구조 깊이
- 정확한 시작 태그와 일치하는 종료 태그는 iiHtmlBlock / iiXml에 의해 보고된 값 경계에서 구분됩니다;
- 태그 이름, 내부 값 및 완전한 원시 HTML;
- 전체 문서 원시 및 값 바이트 범위;
- `iiHtmlBlock`에서 인식되는 모든 `display` 재정의.

`HtmlBlockDocument::rootIds()`는 모든 최상위 블록 ID를 소스 순서대로 반환합니다. `openingTag()`에는 속성과 최종 `>`가 포함되어 있습니다. `closingTag()`에는 일치하는 `</name>` 마크업이 포함되어 있거나 `isSelfClosing()`가 true인 경우 비어 있습니다.

빈 문서와 XML, DOCTYPE, 또는 ASCII 공백만 포함하는 소스는 유효하며 0 블록을 포함합니다. 유효하지 않은 마크업은 `DocumentError` 를 던지고 부분적으로 초기화된 문서를 생성하지 않습니다. ID 는 하나의 `HtmlBlockDocument` 인스턴스 내에서만 안정적입니다. 소스에 추가되거나 영속화되지 않으며 HTML 를 다시 로드하면 새로운 런타임 정체성 집합이 생성됩니다.

<a id="crud-contract"></a>

## CRUD 계약

```cpp
#include <iiGeneralDocument/iiGeneralDocument.h>

using namespace ii::document;

auto document = HtmlBlockDocument::fromHtml(
    "<main><p>Existing</p></main>");
HtmlBlockEditor editor(document);

const HtmlBlockId mainId = document.blocks().front().id();
const HtmlBlockId created = editor.create("<p>Created</p>", mainId);
const HtmlBlock* read = editor.read(created);
editor.update(created, "<article>Updated</article>");
editor.remove(created);

const std::string& result = document.html();
```

`create(fragment)` 는 문서에 최상위 블록 하나를 추가합니다. `create(fragment, parentId)` 는 이를 부모의 마지막 자식으로 추가합니다. 프래그먼트 주변의 앞뒤 ASCII 공백은 버려집니다. 프래그먼트 자체는 `source-range validation and exact original fragment serialization` 를 통해 정확히 하나의 최상위 블록으로 직렬화되어야 하며, 인라인 전용, 잘못된 형식, 또는 여러 루트 프래그먼트는 거부됩니다.

`read(id)`는 현재 불변 블록 뷰에 대한 포인터를 반환하거나 알 수 없는 ID에 대해 `nullptr`를 반환합니다. 성공적인 변형은 뷰 컬렉션을 다시 작성하므로 이전에 반환된 포인터가 `create`, `update` 또는 `remove`에서 유지되어서는 안 됩니다.

`update(id, fragment)` 는 선택된 원시 범위를 정확히 대체합니다. 태그가 변경되더라도 선택된 블록은 ID 를 유지합니다. 기존 조상과 관련 없는 블록도 ID 를 유지하며, 대체 후손은 새로운 ID 를 받습니다. 동일한 직렬화된 블록 HTML 로 업데이트하는 것은 모든 후손 ID 를 보존하고 리비전을 증가시키지 않는 무작위 작업입니다.

`remove(id)` 는 해당 백엔드가 나머지를 표현할 수 있을 때 `iiHtmlBlock::DeleteBlock` 결과를 사용합니다. 마지막 블록을 제거하는 것도 지원되며, XML 선언이나 DOCTYPE 만 남는 경우에도 해당됩니다. 부모를 제거하면 모든 후손 블록 정체성이 제거됩니다. 알 수 없는 ID 는 변형 없이 `false` 를 반환합니다.

<a id="atomicity-and-identity"></a>

## 원자성과 정체성

각 소스 변경 편집은 임시 소스와 블록 상태로 준비되어 재해석되고, 안정적 ID 가 할당되며, 루트와 부모/자식 관계로 다시 구축된 후에만 커밋됩니다. 성공적인 소스 변형은 `revision()` 를 정확히 한 번 증가시킵니다. 동일한 업데이트와 누락된 ID 제거는 무작위 작업입니다. 파싱 실패, 누락된 ID, 유효하지 않은 조각, 범위 오버플로우, 동일성 조정 실패, 및 계층 구조 재구성 실패는 소스, 블록, 관계, ID, 다음 ID 시퀀스, 및 리버전을 변경하지 않습니다.

iiXml는 엄격한 트리를 형성하는 대신 서로 겹칠 수 있는 독립적으로 닫힌 범위를 허용한다. 다른 겹치는 블록을 가르는 작업은 아무런 알림 없이 해당 블록을 삭제하거나 손상시키는 대신 거부한다. 완전히 중첩된 자손은 선택한 부모 CRUD 단위에 계속 포함된다. 부모는 자식의 대응 닫기 태그를 자신의 대응 닫기 태그가 완전히 감싸는 가장 가까운 열기 태그 범위이다. 서로를 포함하지 않는 교차 범위는 별도의 루트이며 잘못된 부모 ID를 받지 않는다.

<a id="syntax-boundary"></a>

## 구문 경계

블록 분류는 `iiHtmlBlock::DivideBlock` 를 따르며, 표준 블록 태그와 명시적 `display: block` , `flex` , `grid` , `table` , 또는 관련 블록 표시 값을 포함합니다. 이것은 iiHtmlBlock / iiXml 범위 문법이며 브라우저 HTML5 오류 복구 파서가 아닙니다. 전체 네임스페이스 의미론, 엔티티 해석, 및 의미론 DOM 노드는 CDATA 와 주석에 대한 의존성의 현재 표준 범위를 벗어납니다. 명시적인 자기 닫기 마크업은 짝을 이루는 열기/닫기 태그와 구별되지만, 브라우저 스타일의 암시적인 닫기와 빈 요소 복구 수행은 이루어지지 않습니다. 임의의 웹 HTML 를 허용하는 호출자는 이 모델을 생성하기 전에 HTML5 파서로 이를 정규화해야 합니다.

<a id="verification"></a>

## 검증

`iiGeneralDocument.HtmlBlockCrud` 는 순차적인 루트, 부모/자식 탐색, 깊이, 열기/닫기 태그 경계, 자기 닫힘 블록, 중첩 읽기, Unicode 범위, 사용자 지정 표시 블록, 최상위 및 자식 생성, 이동된 범위 전반에 걸친 안정적인 ID, 업데이트 후 계층 구조 재구성, 재귀적 삭제, 최종 루트 삭제, 접두어 보존, 유효하지 않은 조각 롤백, 알 수 없는 ID, 빈 문서, 그리고 중첩된 iiXml 범위를 포함합니다. `iiGeneralDocument.InstallConsumer` 는 설치된 CMake 패키지에서 동일한 API 를 컴파일하고 실행합니다.


## Native dependency migration (0.4)

The complete module now uses C++23 standard types and open-source backends.
The full document package requires the Qt-free `iiXmlMarkup` package; the PDF-only component does not. Qt and the legacy iiXml/iiHtmlBlock packages are not required. Author/file APIs use `iiFileProvider::Native`; Word XML uses pugixml
and persistence uses nlohmann/json. The markup algorithms retain the first-party
range/typed-attribute semantics described above. See [migration details](QT_REMOVAL.md)
for byte-string API changes, ABI compatibility, prior-file verification and the
Thinking Space schema-2 byte-diff encoding.
