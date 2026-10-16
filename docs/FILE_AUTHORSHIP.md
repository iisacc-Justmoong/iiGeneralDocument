<a id="file-authorship"></a>

# 파일 저작자

iiGeneralDocument 0.4는 표준 C++23 `iiFileProviderNative 0.5` 구성요소를 사용한다. 그것은 공유 `Authorship` 장부를 저장합니다: 검증된 iisacc 계정/프로필/장치/기여 정보, 기여자 식별자, 첫/최신 변경 시간, 최신 작성자 참조, 및 정확한 서명되지 않은 십진법 버전입니다. 활성 편집 정체성은 런타임 컨텍스트이며 파일에서 결코 복원되지 않습니다. 토큰, 로그인 자격 증명 및 토큰 갱신은 결코 이 장부에 입력되지 않습니다. 메타데이터는 귀속 데이터이며 검증된 소유권이나 로그인이 아닙니다.

<a id="mutation-boundaries"></a>

## 돌연변이 경계

위 `setFileAuthor(iiFileProvider::native::FileAuthor)` 를 `Document`, `WordDocument`, `HtmlBlockDocument`, `XmlTreeDocument` 또는 `ThinkingSpaceDocument` 에 호출합니다. 선택된 프로필은 즉시 기록됩니다. 동일한 프로필을 선택하는 것은 런타임 편집 컨텍스트만 변경합니다. 성공적인 변경은 변형이 반환되기 전에 JSON 덤프를 재생성하며 타이머나 게터 없이 수행됩니다. 저자가 선택되기 전의 변경사항은 명시적으로 귀속되지 않은 상태로 유지됩니다.

- PDF : `DocumentEditor` 작업 및 `Document` 페이지/필드/버전/메타데이터 세터 스탬프 편집. 동일한 텍스트/이미지 교체 및 누락된 제거는 전혀 할 수 없습니다. 캐시된 JSON는 `metadata()["iisacc:authorship"]`에도 존재합니다.
- Word: 단락/표 삽입 및 `setMetadata` 스탬프 직접 중첩 실행/스타일/테이블/섹션 변경을 위해 `WordDocument::edit(callback)`를 사용하십시오. 복사된 초안을 커밋하고 실제 값 변경을 감지하며 false/throw 콜백을 폐기합니다. 이미 스탬프가 찍힌 초안은 두 번 스탬프가 찍혀 있지 않습니다.
- HTML / XML : 성공적인 서브트리 CRUD가 기존 원자 소스/ ID 커밋 경계에 스탬프됩니다. 잘못된 입력과 동일한 소스는 메타데이터를 변경하지 않습니다.
- Thinking Space : `edit(callback)`는 헤더와 본문 변경을 포함하고 헤더의 예약된 메타데이터 키를 즉시 동기화합니다. Body HTML 편집기들은 동일한 원장을 사용합니다. `recordVersion`는 현재 원장을 스냅샷에 캡처합니다.

기존 가변 벡터/맵과 공개 Thinking Space 집계는 소스 호환성과 분리된 구성을 위해 유지됩니다. C++ 은 보유된 원시 참조를 통한 할당을 관찰할 수 없습니다. 위 방법을 통해 라우트 작성을 수행하며, 구식 직접 PDF /Word 변형은 `recordChange()` 로 끝나야 합니다. 애플리케이션 편집에는 `ThinkingSpaceDocument::edit` 를 사용하므로 헤더와 본문이 동기화되어 있습니다. 이 문서 값은 파일 핸들이나 숨겨진 쓰기 소유자가 없습니다. PDF /Word 작성기는 호출될 때 이미 업데이트된 메타데이터를 영속화하며 새 백그라운드 자동 저장 서비스가 아닙니다.

```cpp
word.setFileAuthor(author);
word.edit([](ii::document::WordDocument &draft) {
    std::get<ii::document::WordParagraph>(draft.blocks().at(0)).runs.at(0).text = "Revised";
    return true;
});
// word.authorship().dump()에는 이미 새 리비전과 기여 정보가 포함되어 있다.
auto result = ii::document::WordDocumentWriter{}.write(word, "revised.docx");
```

<a id="embedded-formats"></a>

## 내장된 형식

|형식|내부 표현|
| --- | --- |
| PDF |문서 정보 사전의 Unicode 하위의 `/iisacc:authorship` 문자열|
| DOCX |`docProps/custom.xml`, `iisacc:authorship` 라는 사용자 정의 문자열 속성으로, 패키지 관계와 콘텐츠 타입을 포함합니다.|
| ODT/FODT |문서 메타데이터의 `meta:user-defined` 문자열 속성|
| DOC |기존 LibreOffice 브릿지는 변환을 통해 사용자 정의 속성을 유지합니다|
| HTML/XML |`toFileBytes()` 는 기본64   UTF-8   JSON 주석을 추가하며;  `fromFileBytes()` 는 소스 파싱 전에 해당 엔벨로프를 검증하고 제거합니다|
| `.tsdoc` |스키마 2 JSON 컨테이너(스키마 1 읽기 지원)는 헤더 메타데이터, 정확한 본문 HTML 및 완전한 유지 버전/스냅샷/차이점 저장소를 포함합니다|

[OpenDocument   1.3, 섹션 19.338](https://docs.oasis-open.org/office/OpenDocument/v1.3/os/part3-schema/OpenDocument-v1.3-os-part3-schema.html) 은 `meta:value-type` 가 생략될 때 string 을 기본값으로 정의합니다. 리더는 LibreOffice 형식과 명시적인 string 타입을 허용합니다. 다른 작성된 값 타입, 중복 속성 및 잘못된 형식 장부는 읽기 오류를 발생시킵니다. PDF /Word 작성자는 정제된 장부를 사용하므로 예약된 원시 맵 값을 변경하면 작성된 출력에 임의의 인증 필드를 주입하지 않습니다

마크업 엔벨로프는 파일의 끝에서 `\n<!--iisacc:authorship:v1:BASE64-->` 입니다. 표준 기본64 는 `--` 를 XML 주석에 도입할 수 없습니다. 원시 `html()` 와 `xml()` 는 변경되지 않는 오프셋을 가진 정확한 편집 가능한 본문으로 남아 있습니다. 파일 사용자는 귀속을 유지하려면 `toFileBytes` / `fromFileBytes` 를 사용해야 합니다. 잘못된 형식, 중복 또는 미래 버전 작성된 주석은 안전하게 거부한다 합니다. 외부 편집자는 사용자 정의 속성/주석을 제거할 수 있으므로 이러한 테스트된 경로 밖의 보존은 가정하지 않습니다

`.tsdoc` 파일은 `format: "iiGeneralDocument.ThinkingSpace"` , `schemaVersion: 1` , `header` , `bodyHtml` , 및 `history` 를 가집니다 역사는 버전, 스냅샷, 차이점, 가지치기된 개수 및 얕은 경계를 저장합니다 모든 버전 ID 및 가역적 차이점은 읽을 때 검증되며, 유지/가지치기된 역사는 보존됩니다 컨테이너는 64   MiB 로 제한되며, 최대 100 개의 유지 버전, 스냅샷 및 차이점을 가집니다 각 저자 장부는 iiFileProvider 로 1 MiB 로 제한됩니다. 이전 디스크 컨테이너는 없었으며, 알 수 없는 컨테이너 버전은 암시적으로 마이그레이션되는 대신 거부됩니다.

<a id="dependency-and-validation"></a>

## 종속성 및 유효성 검사

작성자와 파일 처리는 동일한 AGPL-3.0-only의 형제 프로젝트 `iiFileProvider::Native`가 소유한다. JSON은 nlohmann/json, Unicode 정규화는 utf8proc, 시간·문자열·파일 인터페이스는 C++23 표준 타입을 사용한다. 공개 모델의 ABI 버전은 0.4이므로 기존 전체 모듈 소비자는 재빌드한다. 이전 프로필 및 장부의 형식과 내용은 유지되며 Qt 의존성은 없다.

테스트는 즉시 덤프, 무작위/거부된 편집, PDF / DOCX / DOC / ODT / FODT 및 마크업 라운드 트립, LibreOffice 의 기본 문자열 타입, 잘못된 형식/미래 메타데이터 및 완전/가려진 `.tsdoc` 이력을 다룹니다. 독립적으로 설치된 패키지 테스트는 내보낸 CMake 타겟과 헤더를 통해 동일한 계약을 연습합니다.

설치된 타깃은 비시스템 include 디렉터리를 내보내므로 선택한 SDK 접두사가 오래된 전역 `CPATH` 헤더보다 우선한다. 설치된 소비자 회귀 테스트는 충돌하는 레거시 umbrella 헤더를 의도적으로 제공한다. Thinking Space의 직렬화는 JSON을 작성하기 전에 UTF-8이 아닌 문자열을 거부한다.


## Native dependency migration (0.4)

The complete module now uses C++23 standard types and open-source backends.
Qt and the installed iiXml/iiHtmlBlock packages are not runtime or public-package
dependencies. Author/file APIs use `iiFileProvider::Native`; Word XML uses pugixml
and persistence uses nlohmann/json. The markup algorithms retain the first-party
range/typed-attribute semantics described above. See [migration details](QT_REMOVAL.md)
for byte-string API changes, ABI compatibility, prior-file verification and the
Thinking Space schema-2 byte-diff encoding.
