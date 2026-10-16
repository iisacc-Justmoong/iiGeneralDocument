<a id="hierarchical-xml-crud-support"></a>

# 계층적 XML CRUD 지원

<a id="scope"></a>

## 범위

`XmlTreeDocument` 는 `iiXml 0.1.0` 위에 구축된 소스 보존 계층적 XML 모델이며, `the private standard C++ tag parser` 는 요소 범위, 계층 구조, 및 타입화된 속성 필드를 제공합니다. 공개 어댑터는 이러한 구현 타입을 안정된 `XmlNodeId`, `XmlNode`, 및 `XmlAttribute` 값으로 변환하고 서브트리 CRUD 에 적합한 엄격한 단일 루트 경계를 추가합니다.

이 모듈은 PDF 페이지 모델, HTML 블록 모델, 워드 흐름 모델 및 애플리케이션 지속성 레이어와 독립적입니다. 메모리 내 CRUD를 수행합니다. 호출자는 파일 또는 데이터베이스 로딩을 소유하고 `XmlTreeDocument::xml()`를 유지합니다.

<a id="public-model"></a>

## 공개 모델

`XmlTreeDocument::fromXml()`는 제공된 바이트를 보존하고 사전 순서로 노드를 노출합니다. 각 `XmlNode`에는 다음이 포함됩니다.

- 문서 전체 런타임 `XmlNodeId`;
- 선택적 상위 ID, 순서가 지정된 직계 하위 ID 및 계층 구조 깊이
- 해당 요소 이름, 정확한 시작 및 일치하는 종료 태그 뷰, 내부 XML, 완전한 원시 XML, 그리고 자동 종료 상태;
- 전체 문서 원시 및 값 바이트 범위;
- 이름, 선택적 값, 추론된 값 유형 및 iiXml 선언형 플래그가 포함된 정렬된 속성.

XML 선언, doctype, 주석, 처리 명령, 루트 마크업, 뒤따르는 XML 공백과 UTF-8 BOM는 바이트 그대로 보존한다. 어댑터 경계에서 iiXml의 `<!Doctype root>` 표기와 표준 `<!DOCTYPE root>` 표기를 모두 받는다. 로드한 문서에는 정확히 하나의 루트가 있어야 한다. ID는 하나의 문서 인스턴스 안에만 존재하고 직렬화하지 않는다. 다시 로드하면 새로운 런타임 식별자 집합을 할당한다.

`openingTag()`에는 속성과 최종 `>`가 포함됩니다. `closingTag()`는 일치하는 `</name>` 소스 뷰이며 자체 폐쇄 노드의 경우 비어 있습니다. 이러한 보기는 iiXml의 일치된 시작/끝 범위에서 직접 제공됩니다. 원본 소스를 정규화하거나 다시 직렬화하지 않습니다.

<a id="crud-contract"></a>

## CRUD 계약

```cpp
#include <iiGeneralDocument/iiGeneralDocument.h>

using namespace ii::document;

auto document = XmlTreeDocument::fromXml(
    "<catalog><item>Existing</item></catalog>");
XmlTreeEditor editor(document);

const XmlNodeId catalogId = *document.rootId();
const XmlNodeId created = editor.create(
    "<item enabled=true>Created</item>", catalogId);
const XmlNode* read = editor.read(created);
editor.update(created, "<entry>Updated</entry>");
editor.remove(created);

const std::string& result = document.xml();
```

`create(fragment, parentId)` 는 바깥쪽 ASCII 공백을 잘라내고 정확히 하나의 맨 요소 루트를 요구하며, 부모의 마지막 자식으로 전체 서브트리를 추가합니다. 부모가 자기 닫힘인 경우 ID 를 유지하면서 짝 태그로 확장됩니다. `create(fragment)` 는 문서가 빈 편집기 상태일 때만 허용되며, 유일한 루트를 생성하고 두 번째 문서 루트를 거부합니다.

`read(id)`는 현재 불변 노드 뷰에 대한 포인터를 반환하거나 알 수 없는 ID에 대해 `nullptr`를 반환합니다. 성공적인 변형은 뷰 컬렉션을 다시 빌드하므로 포인터가 `create`, `update` 또는 `remove` 전체에서 유지되어서는 안 됩니다.

`update(id, fragment)` 는 선택된 서브트리 중 정확히 하나를 대체합니다. 대체 루트는 이름이 변경되더라도 선택된 ID 를 유지합니다. 기존 조상과 관련 없는 노드는 ID 를 유지하고, 오래된 자손은 사라지며 대체 자손은 새 ID 를 받습니다. 동일한 정규화된 대체는 모든 ID 를 보존하고 `revision()` 를 증가시키지 않는 무작위 작업입니다.

`remove(id)` 는 선택된 원시 범위와 모든 자손 식별자를 삭제합니다. 알 수 없는 ID 는 `false` 를 반환합니다. 루트를 제거하면 프롤로그와 트레일링 미스셀러니티스 XML 바이트를 보존하고 의도적으로 편집 가능한 빈 상태를 남깁니다. 이후 `create(fragment)` 로 새 루트를 생성할 수 있습니다.

<a id="hierarchy-and-atomicity"></a>

## 계층 구조와 원자성

iiXml 는 독립적으로 닫힌 오버레이 범위를 의도적으로 지원합니다. 일반적인 문서 XML 트리는 그렇지 않습니다: 자매는 순서대로 정렬되고 겹치지 않으며, 모든 자식 범위는 부모의 값 범위 안에 있어야 하고, 문서는 비미스셀러니티스 콘텐츠가 없는 하나의 루트를 가져야 합니다. `<root><a><b></a></b></root>` 와 같은 크로스 닫힌 입력은 거부됩니다.

각 소스 변경 작업은 임시 소스를 생성하고 파싱하며, 정확한 범위로 맵의 안정적 식별자를 매핑하고, 부모/자식 식별자 참조를 검증한 후 완전한 후보가 유효할 때만 커밋합니다. 파싱 오류, 범위 오버플로우, 누락된 ID, 유효하지 않은 조각, 중복 식별자, 계층 구조 위반은 소스, 노드, 다음 ID 상태, 및 리버전을 변경하지 않습니다. 성공적인 소스 변형은 `revision()` 를 정확히 한 번 증가시킵니다. 삭제된 ID 는 문서 인스턴스 내에서 다시 사용되지 않습니다.

<a id="syntax-boundary"></a>

## 구문 경계

요소 인식, 자기 닫는 태그, 주석, CDATA, 처리 지시문, 태그 이름, 완화된 속성 값, 선언된 타입, 및 값 타입 추론은 iiXml 0.1.0를 따릅니다. 이 어댑터는 계층 구조와 프롤로그 배치 유효성을 검사하지만 W3C XML 스키마, 네임스페이스, DTD 또는 엔티티 해결 엔진이 아닙니다. 엔티티를 확장하거나 루트에 대한 도CTYPE 유효성을 검사하지 않습니다. 호의적이지 않거나 표준 민감도 XML 를 허용하는 애플리케이션은 도메인 객체를 값에서 생성하기 전에 스키마, 네임스페이스, 엔티티 및 리소스 접근 유효성을 수행해야 합니다.

<a id="verification"></a>

## 검증

`iiGeneralDocument.XmlTreeCrud` 는 프롤로그 보존, 표준 및 iiXml 도CTYPE 표기법, Unicode 범위, 계층 구조 탐색 및 깊이, 열기/닫기 태그 경계, 타입화된 속성, 루트 및 서브트리 생성, 자기 닫힘 부모 확장, 이동된 범위 내의 안정적인 ID, 서브트리 교체, 재귀적 삭제, 루트 재창조, 무효 소스 거부, 무작위 업데이트, 롤백 및 재사용되지 않은 ID 를 포함합니다. `iiGeneralDocument.ProjectContract` 는 iiXml 구현 타입이 공개 헤더를 통해 누출되는 것을 방지합니다. `iiGeneralDocument.InstallConsumer` 는 설치된 CMake 패키지의 CRUD 표면을 컴파일하고 실행합니다.


## Native dependency migration (0.4)

The complete module now uses C++23 standard types and open-source backends.
Qt and the installed iiXml/iiHtmlBlock packages are not runtime or public-package
dependencies. Author/file APIs use `iiFileProvider::Native`; Word XML uses pugixml
and persistence uses nlohmann/json. The markup algorithms retain the first-party
range/typed-attribute semantics described above. See [migration details](QT_REMOVAL.md)
for byte-string API changes, ABI compatibility, prior-file verification and the
Thinking Space schema-2 byte-diff encoding.
