#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QVariantMap>
#include <QString>

namespace ThinkingSpace::NoteBodyResourceTagGenerator
{
    QVariantMap normalizeImportedResourceDescriptor(const QVariantMap& resourceEntry);
    QString buildCanonicalResourceTag(const QVariantMap& resourceEntry);
} // namespace ThinkingSpace::NoteBodyResourceTagGenerator

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
