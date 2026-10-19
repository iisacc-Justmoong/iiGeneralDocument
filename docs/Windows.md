# Windows 빌드와 실행

현재 검증 구성은 C++23, Qt 6.8.3 MSVC 2022 키트, MSVC x64 Ninja 및 같은 컴파일러의 vcpkg 의존성을 사용한다. 생성물은 `build/`에 둔다. 시스템 qpdf 패키지가 zlib와 JPEG를 일반 라이브러리 이름으로 내보내는 경우, qpdf의 실제 라이브러리 디렉터리를 사용하여 링크한다.

`ctest --test-dir build --output-on-failure`에서 문서 편집, XML/HTML CRUD, PDF 및 Word 파일의 저장·재열기를 실제로 실행한다. 테스트가 사용하는 원본과 출력 파일은 저장소의 `build/` 안에 둔다.

최신 구성·실행 결과와 복구 조치는 [Windows 빌드 및 실행](WindowsBuildAndRun.md)에 기록한다.
