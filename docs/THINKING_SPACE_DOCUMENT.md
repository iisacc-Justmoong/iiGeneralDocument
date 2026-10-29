<a id="thinking-space-document"></a>

# Thinking Space 문서

<a id="implemented-scope"></a>

## 구현 범위

Thinking Space 문서는 `.tsdoc` 확장을 사용합니다. 현재 모델은 헤더 메타데이터, 하나의 HTML 블록 본문, 및 버전 객체 저장소를 소유합니다. 버전 0.2 은 스키마-1 JSON 파일 직렬화 를 `toFileBytes` / `fromFileBytes` 를 통해 추가하며, iiFileProvider 저작권 및 완전한 유지/가르기 이력을 포함합니다. 정확한 컨테이너 계약은 [파일 저작권](FILE_AUTHORSHIP.md) 을 참조하십시오.

문서 조립은 4 부분으로 구성됩니다.

- `ThinkingSpaceDocumentHeader`는 문서 내용과 독립적으로 메타데이터를 소유합니다. 메타데이터는 현재 정렬된 문자열 키‐값 맵이며, 아직 필요한 키나 값 스키마가 고정되지 않았습니다.
- `ThinkingSpaceDocumentBody`는 하나의 `HtmlBlockDocument`를 소유하고 있습니다. 그 소스는 여전히 본문 기준 원본이며, 모든 블록 인식 맞춤 태그는 독립적으로 주소 지정 가능한 `HtmlBlock`로 노출됩니다.
- `ThinkingSpaceDocument`는 헤더 하나와 본문 하나를 구성하고 파일 확장자를 `.tsdoc`로 선언합니다.
- `ThinkingSpaceDocumentVersionHistory`는 콘텐츠 주소가 지정된 스냅샷, 가역적인 diff 객체 및 연대순 버전 커밋을 보유하고 있습니다.

```cpp
ThinkingSpaceDocument document;
document.header.metadata["title"] = "Thinking Space";
document.body.htmlBlocks = HtmlBlockDocument::fromHtml(
    "<ts-paragraph style=\"display: block\">Text</ts-paragraph>");
const ThinkingSpaceDocumentVersion version = document.recordVersion(
    "Initial draft", "2026-09-01T00:00:00.000Z");
```

본체는 두 번째 HTML 범위 모델을 도입하는 대신 기존 `HtmlBlockDocument` 모델과 그 `iiHtmlBlock` 어댑터를 의도적으로 재사용합니다. 커스텀 태그는 명시적인 블록 표시 오버라이드와 같은 해당 모델의 블록 분류 계약을 충족해야 합니다. 스키마-1 JSON 컨테이너는 `header`, `bodyHtml`, `history` 를 분리하며; 파일 읽기는 저자성과 역사 무결성을 검증합니다.

<a id="git-object-principle"></a>

## Git 객체 원리

기록은 패치를 정식 버전으로 취급하지 않고 Git의 논리적 저장소 모델을 따릅니다.

1. 헤더 메타데이터와 정확한 본문 HTML는 독립적인 `blob` 개체가 됩니다.
2. 스냅샷 `tree` 는 해당 2 블롭 ID 를 가리킵니다.
3. `commit` 버전은 트리, 상위 커밋, companion diff, 타임스탬프 및 레이블을 가리킵니다.
4. 각 ID 는 SHA-256 보다 `type + " " + byte-size + NUL + payload` 를 초과하므로, 동일한 논리적 객체 콘텐츠는 동일한 ID 를 생성하며, 변경 가능한 시퀀스 번호 없이 유지된 객체들을 검증할 수 있습니다.

Git 자체는 스냅샷을 정식으로 취급하며 트리 간 차이를 계산합니다. `.tsdoc` 는 명시적으로 차이 객체를 요구하므로 이 모델도 각 버전 옆에 하나씩 콘텐츠 주소를 가진 `ThinkingSpaceDocumentDiff` 를 저장합니다. 그것은 베이스/타겟 스냅샷 ID 와 정확한 헤더/본문 바이트 델타를 기록합니다. 델타는 공통 접두어와 접미어 및 제거/삽입된 중간 바이트를 유지하며; `apply()` 는 잘못된 베이스를 거부하고 올바른 베이스에 대한 정확한 타겟을 재구성합니다. 이는 프레젠테이션 지향의 다 블록 차이보다 더 거칠 수 있지만 손실 없고 가역적입니다.

`ThinkingSpaceDocumentDiff::apply(baseSnapshot)`는 논리적 헤더와 본문 델타를 모두 적용하고, Blob/트리 ID를 다시 작성하며, 계산된 트리가 `targetSnapshotObjectId`와 일치하는 경우에만 대상을 반환합니다. 기본 빈 스냅샷은 첫 번째 버전의 유효한 기반입니다.

SHA-256 프레임링과 부모/트리 의미론은 Git 스타일이지만 이 객체들은 Git 저장소와 바이트 호환이 아닙니다. 헤더 정규화는 미래 물리적 `.tsdoc` 인코딩이 아닌 내부 해시 표현입니다. 객체 ID 는 무결성과 중복 제거 식별을 제공하며 인증이나 디지털 서명이 아닙니다.

<a id="recording-and-retention"></a>

## 기록 및 보관

`recordVersion()`는 명시적인 커밋 경계입니다. `header.metadata` 또는 `body.htmlBlocks`를 편집해도 아무런 알림 없이는 버전을 생성하지 않습니다. 호출자는 결정론적 가져오기/테스트를 위해 ISO-8601 UTC 타임스탬프를 제공할 수 있습니다. 생략된 타임스탬프는 현재 UTC 시간에서 생성됩니다.

버전은 가장 오래된 것부터 최신 버전으로 정렬되며 `head()`는 최신 버전을 반환합니다. 기록에는 최대 `maximumVersionCount == 100` 커밋이 유지됩니다. 오버플로 시:

- 가장 오래된 커밋을 먼저 제거합니다.
- 보유된 커밋 참조가 없는 스냅샷 및 차이점 개체를 제거합니다.
- `prunedVersionCount()`를 증가시킵니다.
- 첫 번째 보류된 커밋의 누락된 부모 ID를 `shallowBoundaryParentObjectId()`에서 보존합니다.

얕은 경계는 Git 의 얕은 역사 원칙을 반영합니다: 살아남은 커밋 ID 는 오래된 조상이 폐기되었기 때문에 단순히 다시 쓰여지지 않습니다. `verifyIntegrity()` 는 단일 선포된 경계를 수용하며 모든 남은 부모 링크, 객체 해시, 타겟 스냅샷, 재구성 가능한 차이를 확인합니다.

<a id="deferred-contracts"></a>

## 이연 계약

이 개체 모델은 `.tsdoc` 파일이 이미 열리거나 저장될 수 있음을 의미하지 않습니다. 다음 결정은 현재 마일스톤에 포함되지 않습니다.

- 물리적 엔벨로프 및 헤더/본문 인코딩
- 필수 메타데이터 키, 입력된 값 및 형식 버전 관리
- 사용자 정의 태그 어휘 및 중첩 규칙;
- 리더, 라이터, 디스크 수준 검증, 원자 파일 커밋 및 호환성 동작;
- 분기, 병합, 저작자, 서명 및 원격 동기화.

`iiGeneralDocument.ThinkingSpaceDocumentModel`는 집계 표면, `.tsdoc` 확장, 독립적인 메타데이터/본문 구성, 결정적 콘텐츠 ID, 상위 체인, 가역적 델타, 무결성 검사, 얕은 경계, 가장 오래된 것부터 잘라내기 및 정확한 100버전 보존 제한을 확인합니다.


## Native dependency migration (0.4)

The complete module now uses C++23 standard types and open-source backends.
The full document package requires the Qt-free `iiXmlMarkup` component; the standalone PDF component does not. Neither Qt nor the legacy Qt-based iiXml/iiHtmlBlock packages are required. Author/file APIs use `iiFileProvider::Native`; Word XML uses pugixml
and persistence uses nlohmann/json. The markup algorithms retain the first-party
range/typed-attribute semantics described above. See [migration details](QT_REMOVAL.md)
for byte-string API changes, ABI compatibility, prior-file verification and the
Thinking Space schema-2 byte-diff encoding.
