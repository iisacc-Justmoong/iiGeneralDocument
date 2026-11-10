# Dynamic linking

The document library defaults to shared linking. Set
`IIGENERALDOCUMENT_BUILD_SHARED=OFF` only for an explicit static build. Shared
iOS builds use frameworks and WebAssembly builds use side modules; the private
third-party archive dependencies remain implementation details.
