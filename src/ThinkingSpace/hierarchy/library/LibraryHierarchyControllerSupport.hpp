#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/hierarchy/ThinkingSpaceHierarchyIoSupport.hpp"

namespace ThinkingSpace::Hierarchy::LibrarySupport
{
    using ThinkingSpace::Hierarchy::IoSupport::normalizePath;
    using ThinkingSpace::Hierarchy::IoSupport::readUtf8File;
    using ThinkingSpace::Hierarchy::IoSupport::resolveContentsDirectories;
} // namespace ThinkingSpace::Hierarchy::LibrarySupport

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
