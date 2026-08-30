#pragma once

#include "Core/Diagnostic.h"
#include "Model/Document.h"
#include "iiGeneralDocument/Export.h"

#include <vector>

namespace ii::document {

class IIGENERALDOCUMENT_EXPORT DocumentValidator {
public:
    [[nodiscard]] std::vector<Diagnostic> validate(const Document& document) const;
    [[nodiscard]] bool hasErrors(const Document& document) const;
};

} // namespace ii::document
