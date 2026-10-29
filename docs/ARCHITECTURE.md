<a id="architecture"></a>

# 건축

<a id="dependency-direction"></a>

## 의존성 방향

종속성 그래프는 의도적으로 단방향입니다.

```text
Core <- Model <- Editing
  ^       ^         ^
  |       |         |
  +-------+---- IO interfaces
                 ^
                 |
             Pdf adapters

Model <- Validation

Flow text model <- Word reader/writer <- DOCX/ODT/FODT codecs <- libzip + pugixml XML
                              ^
                              |
                      legacy DOC converter <- isolated LibreOffice process

XML tree document <- XML tree editor <- iiXml::Markup source-range parser

HTML block document <- HTML block editor <- private standard C++ block ranges

Thinking Space Document -> header metadata + HTML block body + version object store
                                                        |
                                                        v
                                  SHA-256 blobs/tree/commit + reversible diff
```

`Core` 는 진단, PDF 스칼라/컨테이너 값, 및 콘텐츠 지시를 소유합니다. `Model` 는 문서와 독립적으로 주소 지정 가능한 요소를 소유합니다. `Editing` 는 사용 사례 작업을 수행합니다. `IO` 는 추상 PDF -모델 리더 및 작성자 계약을 포함합니다. `Pdf` 는 QPDF 를 포함하는 유일한 레이어입니다. `Validation` 는 코덱 책임이 없습니다. `Xml` 는 엄격한 소스 보존 트리 모델과 원자적 CRUD 조정기를 소유하며, 오직 그 구현 파일만이 iiXml 노드, 범위, 선언, 및 속성 유형을 번역합니다. `Html` 는 독립적인 소스 보존 블록 모델과 원자적 CRUD 조정기를 소유하며, 오직 그 구현 파일만이 iiHtmlBlock 범위 유형을 번역합니다. HTML 재건 경계는 정렬된 루트, 가장 가까운 완전히 포함하는 부모, 직접 자식, 및 일치한 여는/닫는 범위에서 파생된 깊이를 도출하기 전에 최종 안정 ID 를 할당하므로 관계 메타데이터가 소스와 원자적으로 커밋됩니다. `ThinkingSpace` 는 기존 HTML 모델에 별도의 헤더 메타데이터와 콘텐츠 주소 기반 버전 저장소를 함께 구성합니다. 그의 히스토리 레이어는 헤더/바디 블롭을 스냅샷 트리로 해시하고, 불변 버전 커밋을 부모 ID 를 통해 연결하며, 각 커밋에 대해 가역적인 동반 디프를 유지하고, 살아남은 객체 ID 를 변경하지 않으면서 유지된 참조를 가지릅니다. `Word` 는 별도의 흐름 지향 모델과 공개 포맷 디스패처를 소유합니다. 단 `Word/Private` 만 libzip 를 포함하거나 LibreOffice 를 시작하며, 이러한 구현 유형은 설치되지 않았습니다.

패키지 경계에서 `iiGeneralDocument`는 `iiFileProvider::Native`와 `iiGeneralDocument::Pdf`에 공개적으로 연결한다. 작성자 정보와 파일 처리는 표준 C++ Native 구성요소가 소유한다. XML·HTML 범위 알고리즘은 Qt 없는 정적 `iiXml::Markup` 구성요소가 소유한다. 문서 모델의 공개 타입에는 그 구현 타입이 노출되지 않는다. QPDF, libzip, pugixml, nlohmann/json은 비공개 오픈소스 구현이며 공개 타입에는 노출되지 않는다. LibreOffice는 레거시 `.doc`에만 사용하는 선택적인 외부 프로세스이다. 전체 타깃과 PDF 구성요소 모두 Qt에 의존하지 않는다.

비공개 ODF 코덱은 ODT ZIP 전송과 공유 OpenDocument XML 의미론을 분리하며, FODT 는 ZIP 를 우회하고 동일한 스타일, 메타데이터, 바디, 및 페이지 매핑을 사용합니다. 두 어댑터는 모두 `WordDocument` 에 내향적으로 의존하지만, 모델은 ODF, OOXML, libzip, 또는 LibreOffice 의존성이 없습니다. 스타일 식별자는 ODF 패밀리로 범위가 지정되며, 폰트 별명은 대체 경로 이전에 패키지 부분 내에서 해결되고, 목록 인스턴스 식별자는 목록 프레젠테이션 스타일에서 독립적입니다.

<a id="solid-boundaries"></a>

## SOLID 경계

- SRP: 각 클래스에는 하나의 변경 이유가 있습니다. 예를 들어, `TextElement`는 텍스트 피연산자를 편집하고, `DocumentEditor`는 객체를 찾아 조정하고, `DocumentValidator`는 불변성을 적용하고, `PdfDocumentWriter`는 모델을 PDF 파일에 매핑합니다.
- OCP : 새로운 PDF 페이지 코덱은 `DocumentReader` 와 `DocumentWriter` 를 구현하며, 새로운 의미론적 페이지 타입은 `Element` 에서 파생됩니다. XML 트리 및 HTML 블록 인식은 문서 재건 경계를 유지한 채 남아있고, Word 형식 분배는 그 흐름 모델에 의존하며, 그 패키지/변환 어댑터는 교체 가능하게 유지됩니다.
- LSP: 모든 요소는 순서가 지정된 명령 시퀀스를 유지하며 유형별 가정 없이 `Element`를 통해 사용될 수 있습니다.
- ISP: 읽기와 쓰기는 별도의 인터페이스입니다. PDF만 검사하는 애플리케이션은 돌연변이 출력에 의존하지 않습니다.
- DIP : PDF 편집 및 검증은 모델과 인터페이스에 의존합니다. XML CRUD 는 공개 트리 모델에 의존하는 반면, 그 구현은 iiXml 를 적응시킵니다; HTML CRUD 도 마찬가지로 iiHtmlBlock 를 적응시킵니다. 외부 형식 어댑터만 QPDF, libzip, 또는 LibreOffice 에 의존합니다.

<a id="recognition-strategy"></a>

## 인식 전략

리더는 먼저 모든 콘텐츠 개체를 `PdfInstruction`로 구문 분석합니다. 그런 다음 순서가 지정된 전체 시퀀스를 의미 요소로 분할합니다.

- `BT ... ET`는 `TextElement`가 됩니다.
- 페인트/끝 연산자를 통한 경로 구성은 `PathElement`가 됩니다.
- 이미지 및 Form XObject `Do` 호출은 리소스 인식 요소가 됩니다.
- `BI ... ID ... EI`는 `InlineImageElement`가 됩니다.
- 음영 처리, 표시된 내용 및 그래픽 상태 연산자는 전용 유형을 받습니다.
- 나머지 연산자나 매달린 피연산자는 `UnknownElement`가 됩니다.

구문 분석된 명령어는 의도적으로 삭제되지 않습니다. 작성자는 요소 순서를 다시 하나의 페이지 콘텐츠 스트림으로 직렬화합니다. 이는 바이트 단위 보존이 아닌 의미론적 보존입니다. 공백, 스트림 경계, 압축, 개체 번호 및 상호 참조 레이아웃이 변경될 수 있습니다.

Form XObjects 는 재귀적으로 파싱됩니다. 동일한 간접 형식의 여러 배치는 하나의 `FormContent` 를 공유하며, 이는 PDF 리소스 의미론을 반영합니다. 따라서 중첩된 요소를 편집하면 해당 형식을 참조하는 모든 배치가 업데이트됩니다. 발생 특이한 편집은 먼저 형식 리소스를 복제/분리해야 하며, 공유 리소스를 우연히 변형해서는 안 됩니다.

<a id="failure-boundaries"></a>

## 실패 경계

잘못된 값, 중복 ID, 유효하지 않은 페이지 기하, 유효하지 않은 이미지 페이로드, 및 누락된 형식 리소스는 검증 또는 작성에 실패합니다. 알 수 없는 페이지 연산자는 편집 가능한 명령으로 남아있습니다. 원래 소스 바이트는 로드된 문서에 첨부되어 있어, QPDF 가 1 차 모델 객체가 아닌 카탈로그 구조를 보존할 수 있게 합니다.

HTML 돌연변이는 커밋 전에 완전한 후보 소스 및 블록 컬렉션을 구축합니다. 잘못된 형식 또는 다중 루트 조각, 누락된 ID, ID 조정 실패 및 iiXml 오버레이 범위를 넘는 변경 사항으로 인해 소스, ID, next-ID 상태 및 개정판은 그대로 유지됩니다.

XML 변형은 더 엄격한 계층 불변성을 사용하여 커밋 전 후보 경계를 동일한 것으로 사용한다. 여러 루트, 루트 외부의 내용, 잘못된 프롤로그, 교차 닫힌 iiXml 범위, 유효하지 않은 조각, 및 동일성 조정 실패는 소스, ID, 다음 ID 상태, 및 버전을 그대로 둔다. 루트를 의도적으로 제거하면 편집 가능한 빈 상태로 진입하며, 이후 루트 생성은 완전한 XML 문서를 복원한다.

다시 작성하면 서명이 무효화되므로 서명된 파일은 기본적으로 거부됩니다. 현재 작성자가 암호화되지 않은 결과를 내보내므로 암호화된 입력은 기본적으로 쓰기 시 거부됩니다. 두 동작 모두 재정의하려면 명시적인 작성기 옵션이 필요합니다.

ODT / FODT 는 원자적 목적지 교체 전에 같은 디렉토리의 임시 아티팩트를 작성하고 다시 엽니다. 그들의 독자들은 물리적 ZIP 엔벨로프 위반, CRC /무결성 오류, 암호화된 항목, 매니페스트/패키지 불일치, 및 한계가 설정된 의미론적 확장을 안전하게 거부한다 합니다.

<a id="authorship-boundary-02"></a>

## 저작권 경계(0.2)

각 편집 가능한 모델은 복사 가능한 iiFileProvider `Authorship` 값을 소유합니다. 변형 조정기는 캐시된 덤프를 동기적으로 업데이트하며; 코덱은 오직 정제된 값만 임베드하거나 읽습니다. 어떤 파일 계정이 인증된 현재 편집자로 승진되지 않습니다. PDF /Word 는 유산 가변 집계체를 유지합니다; 검증된 저작은 그들의 조정기 또는 명시적 `recordChange` 를 사용하며, Word/ Thinking Space 는 또한 중첩된 집계 변경에 대한 원자적 초안 콜백을 추가로 제공합니다. 네이티브 `.tsdoc` 코덱은 기존 히스토리 객체 저장소를 보존하고 검증하며 두 번째 히스토리 구현을 도입하지 않습니다.

## 파일 저장 소유권

파일 CRUD는 iiFileProvider 0.5가 소유한다. PDF는 메모리에서 생성·검증한 뒤 provider가 원자적으로 공개한다. DOCX/ODT는 provider가 연 읽기 장치와 소유한 임시 파일을 사용하고, FODT/DOC 변환 결과도 provider를 통해 기록·공개한다. Thinking Space/HTML/XML 바이트 코덱은 File::create/read/update/remove와 조합한다. iiFileProvider에서 이 SDK로의 역참조는 금지한다.

## SDK-owned PDF object codec (0.3)

`iiGeneralDocument::Pdf` owns byte-oriented `ii::document::pdf::Document` objects
and their transactional `Editor`. This lower component depends only on standard
C++23 and private pinned OSS libraries. It never references the aggregate model,
iiFileProvider or an Engineering checkout. The existing aggregate editor adapts
its current modeled state through the standard byte reader/writer for page
assembly, then restores the source/first aggregate's authorship and records one
change. Rotation, metadata, notes and text-field values remain ordinary aggregate
edits. This dependency points from the aggregate to the format codec without a
reverse edge. Physical file CRUD remains owned by iiFileProvider.

The installed package has two exports. `COMPONENTS Pdf` loads only the lower
codec; the default full target also loads the standard-only Native provider
and supplies Word/XML/HTML/Thinking Space models without Qt. See [the complete operation rules](ACROBAT_ABSORPTION.md).


## Native dependency migration (0.4)

The complete module now uses C++23 standard types and open-source backends.
The full document package requires the Qt-free `iiXmlMarkup` package; the PDF-only component does not. Qt and the legacy iiXml/iiHtmlBlock packages are not required. Author/file APIs use `iiFileProvider::Native`; Word XML uses pugixml
and persistence uses nlohmann/json. The markup algorithms retain the first-party
range/typed-attribute semantics described above. See [migration details](QT_REMOVAL.md)
for byte-string API changes, ABI compatibility, prior-file verification and the
Thinking Space schema-2 byte-diff encoding.
