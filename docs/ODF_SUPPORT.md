# OpenDocument 텍스트 지원 계약

## 형식과 버전

`WordDocumentReader`와 `WordDocumentWriter`는 확장자에 따라 OpenDocument
텍스트 코덱을 선택한다.

| 형식 | 읽기 | 쓰기 | 저장 구조 |
| --- | --- | --- | --- |
| `.odt` | 지원 | 지원 | ODF ZIP 패키지 |
| `.fodt` | 지원 | 지원 | 단일 XML 문서 |

리더는 ODF 1.0부터 1.4 텍스트 문서의 공통 의미 부분을 읽고, 그보다 새롭거나 알 수
없는 버전은 명시적 warning과 함께 best-effort로 처리한다. 네임스페이스와 문서 종류를
확인한 뒤 지원하는 요소만 `WordDocument`로 변환한다. 라이터는 널리
호환되는 ODF 1.3 문서를 생성한다. ODT와 FODT는 모두 Qt XML 기반의 네이티브
코덱을 사용하므로 읽기와 쓰기에 LibreOffice 설치가 필요하지 않다.

구현 기준은 OASIS의 최신 공식 명세인 [OpenDocument 1.4 Part 2:
Packages](https://docs.oasis-open.org/office/OpenDocument/v1.4/OpenDocument-v1.4-part2-packages.html)와
[OpenDocument 1.4 Part 3: OpenDocument
Schema](https://docs.oasis-open.org/office/OpenDocument/v1.4/OpenDocument-v1.4-part3-schema.html)이다.
1.3 출력은 이 명세의 하위 버전 호환 구조만 사용한다.

## 공유 flow 모델과 CRUD

ODT와 FODT는 DOCX와 동일한 `WordDocument` flow 모델을 사용한다. 별도의 ODF
전용 공개 객체나 외부 라이브러리 타입은 노출하지 않는다. 따라서 한 형식에서 읽은
문서를 수정한 뒤 다른 형식으로 쓸 수 있다.

- Create: `appendParagraph()`와 `appendTable()`로 블록을 추가하거나 mutable
  `blocks()` 컨테이너에 원하는 위치로 삽입한다.
- Read: const `blocks()`, `metadata()`, `section()`과 각 paragraph, run, table,
  row, cell 컨테이너를 탐색한다.
- Update: mutable `blocks()`, `metadata()`, `section()`을 통해 텍스트, 속성,
  표 구조와 페이지 값을 직접 변경한다.
- Delete: mutable `blocks()` 및 하위 `std::vector`에서 선택한 객체를 지운다.

객체 순서가 문서 순서이다. `WordDocument`는 안정 ID를 부여하지 않으므로 vector를
삽입하거나 삭제한 뒤 기존 iterator, reference, pointer를 재사용해서는 안 된다.
수정 결과는 ODT 또는 FODT로 쓴 뒤 다시 열어 검증할 수 있다.

```cpp
#include <iiGeneralDocument/iiGeneralDocument.h>

using namespace ii::document;

auto read = WordDocumentReader{}.read("source.odt");
if (read.hasErrors()) {
    return 1;
}

auto& paragraph = std::get<WordParagraph>(read.document.blocks().front());
paragraph.runs.front().text = "수정된 제목";

WordParagraph created;
created.runs.push_back({"새 문단", {.bold = true}});
read.document.appendParagraph(std::move(created));

return WordDocumentWriter{}.write(read.document, "revised.fodt").hasErrors();
```

## 지원하는 의미

### 문단과 런

`text:p`와 `text:h`는 순서가 보존된 `WordParagraph`가 된다. paragraph style
이름은 `styleId`에 유지하며 제목의 outline level은 가능한 경우 `Heading1`부터
`Heading9`까지의 스타일로 대응한다. `fo:text-align`은 automatic, left,
center, right, justified 정렬로 변환한다.

문단 본문과 `text:span`은 독립적으로 수정 가능한 `WordRun`이 된다. 탭과 줄바꿈은
각각 `\t`, `\n`으로 표현한다. ODF의 일반 XML 공백은 선행·후행에서 제거하고
연속 구간은 한 칸으로 축약하며, `text:s`의 명시적 반복 공백과 NBSP 같은 비축약
Unicode 공백은 그대로 보존한다. 다시 쓸 때 필요한 공백은 `text:s`로 표현한다.

### 문자 서식

다음 `WordRunProperties`를 ODF text style과 상호 변환한다.

- bold, italic, underline
- Latin font family와 East Asian font family
- point 단위 font size
- 여섯 자리 RGB color

named style, automatic style, parent style의 지원 속성은 읽을 때 계산된 run 속성으로
평탄화한다. paragraph와 text style은 `(family, name)`으로 구분하므로 같은 이름을
사용해도 충돌하지 않는다. ODT의 `content.xml` style은 자기 파트의 font-face 별칭을
우선하고 `styles.xml` 별칭을 fallback으로 사용한다. 이 방식은 화면에 보이는 지원
서식을 편집 가능하게 만들지만 원본의 전체 style 계층이나 테마를 보존한다는 뜻은
아니다.

### 목록

`text:list`의 목록 인스턴스는 `numberingId`, 중첩 깊이는 `numberingLevel` 0~8로
변환한다. style 이름이 같더라도 독립된 top-level 목록은 별도 ID를 받고, nested list와
`text:continue-list`로 연결된 목록은 같은 ID를 사용한다. 라이터는 인접하면서 ID가
같은 문단을 하나의 실제 다단계 list tree로 묶는다. 한 `text:list-item` 안의 첫 문단은
일반 번호 문단이고 이후 문단은 `numberingContinuation=true`로 표시하여 다시 쓸 때
같은 item 안에 유지한다. `text:continue-numbering`은 직전 top-level 목록과 style이
같을 때만 연결한다. `text:list`에 style 이름이 없으면 첫 list-item 문단 style에서
상속된 `style:list-style-name`을 유효 목록 style로 사용한다. 명시된
빈 `style:list-style-name`은 parent style의 목록 style 상속을 취소한다. 명시된
`text:continue-list` 대상을 찾지 못하면 다른 목록으로 fallback하지 않는다. bullet
모양, 임의 번호 형식, 시작 번호, 비인접 목록의 복잡한 계속 규칙은 모델에 없으므로
정확히 보존되지 않는다.

DOCX에는 한 번호 항목에 여러 paragraph를 묶는 같은 계약이 없으므로 변환할 때 첫
문단만 번호를 쓰고 continuation 문단의 `w:numPr`는 생략한다. DOCX를 다시 읽으면 이
ODF 전용 item 결합 정보는 복원되지 않는다.

### 표

`table:table`, `table:table-row`, `table:table-cell`은 각각 `WordTable`,
`WordTableRow`, `WordTableCell`로 변환한다. 행과 셀의 순서, 각 셀 안의 여러 문단,
텍스트와 지원 run 서식을 읽고 쓴다. 빈 셀도 유효한 셀로 유지한다.

### 메타데이터

ODF 메타데이터와 `WordDocument::metadata()` 키의 대응은 다음과 같다.

| WordDocument 키 | ODF 요소 |
| --- | --- |
| `Title` | `dc:title` |
| `Author` | `meta:initial-creator` |
| `LastModifiedBy` | `dc:creator` |
| `Subject` | `dc:subject` |
| `Description` | `dc:description` |
| `Keywords` | `meta:keyword` |
| `Created` | `meta:creation-date` |
| `Modified` | `dc:date` |

`meta:initial-creator`가 없는 입력에서는 `dc:creator`를 Author의 호환 fallback으로
사용할 수 있다. ODF가 허용하는 여러 keyword나 사용자 정의 metadata는 현재 하나의
문자열 map 계약을 넘어서 보존하지 않는다.

### 페이지

본문에서 처음 실제 적용된 paragraph 또는 table style의 상속된
`style:master-page-name`이 참조하는 page layout을 선택한다. 폭, 높이와 개별 margin
또는 `fo:margin` shorthand를 `WordSectionProperties`의 twips 값으로 변환한다. 적용된
master-page 연결이 없으면 첫 master page, 이어서 첫 유효 layout을 fallback으로
사용한다. ODF 규칙에 따라 table cell 내부 문단의 master-page-name은 페이지 전환
후보에서 제외한다. 라이터는 한 개의 page layout과 master page를 생성한다. 문서마다
하나의 section만 표현하므로 여러 master page나 section별 페이지 설정은 선택한 한
layout으로 평탄화된다.

## ODT 패키지 구조

ODT 출력은 `application/vnd.oasis.opendocument.text` MIME type의 ODF ZIP
패키지이다. 다음 계약을 지킨다.

- `mimetype`가 첫 ZIP entry이다.
- `mimetype`는 압축하지 않은 STORE 방식이며 local header extra field가 없다.
- `mimetype` 내용에는 MIME type만 있고 줄바꿈이 없다.
- `META-INF/manifest.xml`이 패키지 entry와 media type을 선언한다.
- `content.xml`은 본문과 automatic style을 담는다.
- `styles.xml`은 named style과 page/master style을 담는다.
- `meta.xml`은 문서 metadata를 담는다.

libzip은 패키지 운송에만 사용하며 공개 헤더나 공개 ABI에 나타나지 않는다. 라이터는
같은 디렉터리의 임시 패키지를 닫은 뒤 네이티브 리더로 다시 열 수 있어야 목적지를
원자적으로 교체한다. 기존 파일을 덮어쓸 때 POSIX mode를 보존하고, 신규 파일은 같은
디렉터리에서 `0666`으로 생성한 probe의 mode를 적용해 프로세스 umask를 존중한다.

## FODT 구조

FODT는 ZIP이 아닌 UTF-8 XML 파일이다. 루트는 `office:document`이고
`office:mimetype="application/vnd.oasis.opendocument.text"`와 ODF version을
선언한다. metadata, font face, named style, automatic style, master style와 본문을
같은 문서 안에 기록한다. ODT와 FODT의 저장 envelope만 다르며 `WordDocument`로
변환되는 의미 범위는 같다.

## 손실 가능성과 명시적 제한

현재 flow 모델이 표현하지 않는 다음 기능은 무손실 왕복 대상이 아니다.

- 이미지, 도형, 차트, 수식, embedded object와 media
- header, footer, footnote, endnote, annotation과 field
- tracked change의 변경 이력과 작성자 정보
- hyperlink 대상 관계와 bookmark 의미
- nested table, 병합 cell, 반복 cell의 원래 압축 표현
- 열 너비, border, 배경, 조건부 style과 전체 style/theme 정의
- 여러 page layout, master page와 section별 설정
- macro, script, RDF, digital signature와 구현체 전용 package entry

읽을 수 있는 표시 텍스트는 가능한 범위에서 평탄화한다. 생략되거나 단순화되는 기능은
diagnostic warning으로 알린다. 지원하지 않는 객체가 포함된 문서를 다시 쓰면 해당
객체와 원래 package entry가 사라질 수 있으며 digital signature도 유지되지 않는다.

## 보안과 자원 제한

- 암호화되거나 password로 보호된 ODT package는 지원하지 않으며 오류로 종료한다.
- 필수 MIME type, manifest, XML root 또는 본문이 없거나 malformed이면 오류로
  종료한다.
- 각 ODT XML part와 FODT XML 전체는 기본 64 MiB
  `maximumXmlPartBytes` 제한을 적용한다.
- ODT ZIP은 최대 10,000개 entry, entry/manifest 경로는 최대 4,096 UTF-8 byte로
  제한한다. manifest 선언과 실제 package entry는 양방향으로 일치해야 한다.
- ZIP entry는 메모리에 풀기 전에 선언된 비압축 크기를 검사한다.
- 각 ZIP part는 선언된 길이 뒤 EOF까지 읽어 CRC와 실제 길이를 검증한다. central
  directory 순서와 별개로 첫 물리 local header가 표준 `mimetype` entry인지 확인한다.
- 고정된 package part만 읽고 archive 내용을 파일시스템 경로로 추출하지 않는다.
- 펼쳐진 모델은 최대 1,000,000개 객체, 의미 중첩은 최대 128단계이며, 펼쳐진 텍스트는
  설정된 XML byte budget을 넘을 수 없다. row/cell 반복의 곱도 같은 누적 budget에
  포함하고 제한 도달 즉시 파싱을 중단한다.
- XML DTD, 외부 entity와 외부 resource 로드는 문서 내용으로 해석하지 않는다.
- ODT와 FODT 라이터는 완성된 임시 파일을 재개방해 의미 파싱까지 성공한 뒤에만 기존
  목적지를 원자적으로 교체한다.

신뢰할 수 있는 대형 문서에만 `WordReadOptions` 또는 `WordWriteOptions`의
`maximumXmlPartBytes`를 늘려야 한다. 쓰기 옵션은 목적지 교체 전 재개방 검증에도
동일하게 적용된다. 크기 제한을 늘리는 것은 지원 의미나 암호화 지원을 확장하지 않는다.

## LibreOffice 상호운용 검증

네이티브 round-trip 테스트는 ODT와 FODT를 각각 생성하고 다시 열어 Unicode,
문단/run 경계, 지원 문자 서식, 목록 level, 표, metadata와 CRUD 수정 결과를
검증한다. ODT 테스트는 ZIP local header를 직접 읽어 첫 `mimetype` entry의 순서,
STORE 방식, extra field 부재와 정확한 MIME type도 확인한다.

별도 style compatibility 테스트는 family별 동명 style, parent 상속, 파트별 font-face
별칭 우선순위와 master-page layout 선택을 검증한다. robustness 테스트는 CRC 오류,
물리/central entry 순서, ZIP 암호화 표시, package/manifest 폭증, 반복 곱셈, 누적 공백,
과도한 중첩, 신규 파일의 umask 기반 mode와 덮어쓰기 권한 보존을 고정한다.

LibreOffice `soffice`를 찾을 수 있는 환경에서는 별도 상호운용 테스트를 수행한다.
iiGeneralDocument가 생성한 ODT/FODT를 LibreOffice로 열고 다시 저장한 결과를
네이티브 리더로 확인하며, LibreOffice가 생성한 ODT/FODT도 읽고 수정한 뒤 재개방한다.
이 검증은 실제 제품 호환성을 위한 선택적 테스트 backend이며 런타임 읽기·쓰기
의존성은 아니다. `soffice`가 없는 환경에서도 네이티브 ODF 테스트는 항상 실행한다.
