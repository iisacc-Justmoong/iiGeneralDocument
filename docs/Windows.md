# Windows 빌드와 실행

C++23, Qt의 MinGW 13.1 키트, Ninja와 동일한 컴파일러로 빌드한 vcpkg 의존성을 사용한다. 생성물은 `build/`에 둔다. 시스템 qpdf 패키지가 zlib와 JPEG를 일반 라이브러리 이름으로 내보내는 경우, qpdf의 실제 라이브러리 디렉터리를 사용하여 링크한다.

`ctest --test-dir build --output-on-failure`에서 문서 편집, XML/HTML CRUD, PDF 및 Word 파일의 저장·재열기를 실제로 실행한다. 테스트가 사용하는 원본과 출력 파일은 저장소의 `build/` 안에 둔다.
