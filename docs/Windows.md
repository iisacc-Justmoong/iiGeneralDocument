# Windows 빌드와 실행

Windows 구성은 C++23, MSVC x64, Windows SDK, CMake와 Ninja 및 같은 컴파일러의 vcpkg 의존성을 사용한다. Qt 키트는 필요하지 않다. iiFileProvider Native와 iiXmlMarkup을 먼저 준비한다. 생성물은 `build/`에 둔다. 시스템 qpdf 패키지가 zlib와 JPEG를 일반 라이브러리 이름으로 내보내는 경우, qpdf의 실제 라이브러리 디렉터리를 사용하여 링크한다.

`ctest --test-dir build --output-on-failure`에서 문서 편집, XML/HTML CRUD, PDF 및 Word 파일의 저장·재열기를 실제로 실행한다. 고정 입력은 `resources/`에 두며 빌드 결과는 `build/`에 둔다. 실행 범위는 실제 검증한 플랫폼별로 기록한다.

최신 구성·실행 결과와 복구 조치는 [Windows 빌드 및 실행](WindowsBuildAndRun.md)에 기록한다.
