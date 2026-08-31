#include "Xml/XmlTreeEditor.h"

#include "Core/Diagnostic.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ii::document {
namespace {

bool isAsciiSpace(char value) noexcept
{
    return value == ' ' || value == '\n' || value == '\r' || value == '\t';
}

std::string_view trimAscii(std::string_view value) noexcept
{
    while (!value.empty() && isAsciiSpace(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && isAsciiSpace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

std::size_t checkedAdd(std::size_t left, std::size_t right)
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw DocumentError("XML node source range overflow");
    }
    return left + right;
}

std::size_t shiftedAfterEdit(
    std::size_t position,
    std::size_t editBegin,
    std::size_t editEnd,
    std::size_t replacementSize)
{
    return checkedAdd(checkedAdd(editBegin, replacementSize), position - editEnd);
}

std::size_t expandedAncestorEnd(
    std::size_t oldEnd,
    std::size_t removedSize,
    std::size_t replacementSize)
{
    return checkedAdd(oldEnd - removedSize, replacementSize);
}

std::uint64_t takeNextId(std::uint64_t& nextId)
{
    if (nextId == 0 || nextId == std::numeric_limits<std::uint64_t>::max()) {
        throw DocumentError("XML node id space is exhausted");
    }
    return nextId++;
}

std::size_t findNodeByRange(
    const std::vector<XmlNode>& nodes,
    const std::vector<bool>& assigned,
    std::size_t begin,
    std::size_t end,
    const std::string* name)
{
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (assigned[index]) {
            continue;
        }
        const auto& node = nodes[index];
        if (node.rawBegin() == begin
            && node.rawEnd() == end
            && (name == nullptr || node.name() == *name)) {
            return index;
        }
    }
    return nodes.size();
}

bool rangesOverlap(
    std::size_t firstBegin,
    std::size_t firstEnd,
    std::size_t secondBegin,
    std::size_t secondEnd) noexcept
{
    return firstBegin < secondEnd && secondBegin < firstEnd;
}

} // namespace

XmlTreeEditor::XmlTreeEditor(XmlTreeDocument& document) noexcept
    : document_(document)
{
}

XmlNodeId XmlTreeEditor::create(
    std::string nodeXml,
    std::optional<XmlNodeId> parent)
{
    std::string serialized = serializeSingleNode(nodeXml);
    if (document_.nextId_ == 0
        || document_.nextId_ == std::numeric_limits<std::uint64_t>::max()) {
        throw DocumentError("XML node id space is exhausted");
    }
    const XmlNodeId createdId{document_.nextId_};

    if (!parent.has_value()) {
        if (document_.rootId_.has_value()) {
            throw DocumentError("XML document already has a root element");
        }
        const std::size_t insertion = document_.xml_.size();
        const std::size_t createdEnd = checkedAdd(insertion, serialized.size());
        replaceRange(
            insertion,
            insertion,
            std::move(serialized),
            {IdentityBinding{createdId, insertion, createdEnd, true}});
        return createdId;
    }

    const XmlNode* parentNode = read(*parent);
    if (parentNode == nullptr) {
        throw DocumentError("XML node create parent was not found");
    }

    if (!parentNode->isSelfClosing()) {
        const std::size_t insertion = parentNode->valueEnd();
        const std::size_t createdEnd = checkedAdd(insertion, serialized.size());
        replaceRange(
            insertion,
            insertion,
            std::move(serialized),
            {IdentityBinding{createdId, insertion, createdEnd, true}});
        return createdId;
    }

    const std::size_t begin = parentNode->rawBegin();
    const std::size_t end = parentNode->rawEnd();
    const XmlNodeId parentId = parentNode->id();
    const std::string parentName = parentNode->name();
    const std::string& parentRaw = parentNode->rawXml();
    if (parentRaw.size() < 3 || !std::string_view(parentRaw).ends_with("/>")) {
        throw DocumentError("XML self-closing parent has unexpected raw markup");
    }

    std::string opening = parentRaw.substr(0, parentRaw.size() - 2);
    opening.push_back('>');
    const std::size_t childBegin = checkedAdd(begin, opening.size());
    const std::size_t childEnd = checkedAdd(childBegin, serialized.size());
    std::string replacement = opening;
    replacement += serialized;
    replacement += "</" + parentName + ">";
    const std::size_t parentEnd = checkedAdd(begin, replacement.size());

    replaceRange(
        begin,
        end,
        std::move(replacement),
        {
            IdentityBinding{parentId, begin, parentEnd, false},
            IdentityBinding{createdId, childBegin, childEnd, true},
        });
    return createdId;
}

const XmlNode* XmlTreeEditor::read(XmlNodeId id) const noexcept
{
    return document_.find(id);
}

void XmlTreeEditor::update(XmlNodeId id, std::string nodeXml)
{
    const XmlNode* target = read(id);
    if (target == nullptr) {
        throw DocumentError("XML node update target was not found");
    }
    std::string serialized = serializeSingleNode(nodeXml);
    if (serialized == target->rawXml()) {
        return;
    }
    const std::size_t begin = target->rawBegin();
    const std::size_t end = target->rawEnd();
    const std::size_t replacementEnd = checkedAdd(begin, serialized.size());
    replaceRange(
        begin,
        end,
        std::move(serialized),
        {IdentityBinding{id, begin, replacementEnd, false}});
}

bool XmlTreeEditor::remove(XmlNodeId id)
{
    const XmlNode* target = read(id);
    if (target == nullptr) {
        return false;
    }
    const std::size_t begin = target->rawBegin();
    const std::size_t end = target->rawEnd();
    replaceRange(begin, end, {}, {});
    return true;
}

std::string XmlTreeEditor::serializeSingleNode(std::string_view nodeXml)
{
    const std::string_view trimmed = trimAscii(nodeXml);
    if (trimmed.empty()) {
        throw DocumentError("XML node fragment is empty");
    }

    const std::vector<XmlNode> nodes = XmlTreeDocument::parseNodes(trimmed, false);
    if (nodes.empty()
        || nodes.front().rawBegin() != 0
        || nodes.front().rawEnd() != trimmed.size()) {
        throw DocumentError("XML node fragment must contain exactly one bare root element");
    }
    return nodes.front().rawXml();
}

void XmlTreeEditor::replaceRange(
    std::size_t begin,
    std::size_t end,
    std::string replacement,
    std::vector<IdentityBinding> bindings)
{
    if (begin > end || end > document_.xml_.size()) {
        throw DocumentError("XML node edit range is out of source bounds");
    }
    if (document_.revision_ == std::numeric_limits<std::uint64_t>::max()) {
        throw DocumentError("XML node revision space is exhausted");
    }

    std::string nextXml = document_.xml_;
    nextXml.replace(begin, end - begin, replacement);
    std::vector<XmlNode> nextNodes = XmlTreeDocument::parseNodes(nextXml, true);
    std::vector<bool> assigned(nextNodes.size(), false);

    std::vector<std::optional<XmlNodeId>> temporaryParentIds;
    std::vector<std::vector<XmlNodeId>> temporaryChildIds;
    temporaryParentIds.reserve(nextNodes.size());
    temporaryChildIds.reserve(nextNodes.size());
    std::unordered_map<std::uint64_t, std::size_t> temporaryIndexes;
    for (std::size_t index = 0; index < nextNodes.size(); ++index) {
        temporaryParentIds.push_back(nextNodes[index].parentId());
        temporaryChildIds.push_back(nextNodes[index].childIds());
        temporaryIndexes.emplace(nextNodes[index].id().value, index);
    }

    std::uint64_t nextId = document_.nextId_;
    for (const auto& binding : bindings) {
        const std::size_t index = findNodeByRange(
            nextNodes, assigned, binding.begin, binding.end, nullptr);
        if (index == nextNodes.size()) {
            throw DocumentError("XML identity binding was not found after parsing");
        }
        if (binding.allocate) {
            const std::uint64_t allocated = takeNextId(nextId);
            if (allocated != binding.id.value) {
                throw DocumentError("XML node id allocation state is inconsistent");
            }
        }
        nextNodes[index].id_ = binding.id;
        assigned[index] = true;
    }

    const std::size_t removedSize = end - begin;
    for (const auto& oldNode : document_.nodes_) {
        std::size_t expectedBegin = 0;
        std::size_t expectedEnd = 0;
        bool preserve = false;

        if (oldNode.rawBegin() == begin && oldNode.rawEnd() == end) {
            preserve = false;
        } else if (oldNode.rawEnd() <= begin) {
            expectedBegin = oldNode.rawBegin();
            expectedEnd = oldNode.rawEnd();
            preserve = true;
        } else if (oldNode.rawBegin() >= end) {
            expectedBegin = shiftedAfterEdit(
                oldNode.rawBegin(), begin, end, replacement.size());
            expectedEnd = shiftedAfterEdit(
                oldNode.rawEnd(), begin, end, replacement.size());
            preserve = true;
        } else if (oldNode.rawBegin() < begin && oldNode.rawEnd() > end) {
            expectedBegin = oldNode.rawBegin();
            expectedEnd = expandedAncestorEnd(
                oldNode.rawEnd(), removedSize, replacement.size());
            preserve = true;
        } else if (rangesOverlap(oldNode.rawBegin(), oldNode.rawEnd(), begin, end)) {
            if (!(begin <= oldNode.rawBegin() && oldNode.rawEnd() <= end)) {
                throw DocumentError("XML node edit crosses an overlapping range");
            }
        }

        if (!preserve) {
            continue;
        }

        const std::size_t index = findNodeByRange(
            nextNodes, assigned, expectedBegin, expectedEnd, &oldNode.name_);
        if (index == nextNodes.size()) {
            throw DocumentError("XML node identity could not be preserved after editing");
        }
        nextNodes[index].id_ = oldNode.id();
        assigned[index] = true;
    }

    for (std::size_t index = 0; index < nextNodes.size(); ++index) {
        if (!assigned[index]) {
            nextNodes[index].id_ = XmlNodeId{takeNextId(nextId)};
        }
    }

    for (std::size_t index = 0; index < nextNodes.size(); ++index) {
        if (temporaryParentIds[index].has_value()) {
            const auto parent = temporaryIndexes.find(temporaryParentIds[index]->value);
            if (parent == temporaryIndexes.end()) {
                throw DocumentError("XML parent identity remap failed");
            }
            nextNodes[index].parentId_ = nextNodes[parent->second].id();
        } else {
            nextNodes[index].parentId_ = std::nullopt;
        }

        nextNodes[index].childIds_.clear();
        nextNodes[index].childIds_.reserve(temporaryChildIds[index].size());
        for (const XmlNodeId temporaryChild : temporaryChildIds[index]) {
            const auto child = temporaryIndexes.find(temporaryChild.value);
            if (child == temporaryIndexes.end()) {
                throw DocumentError("XML child identity remap failed");
            }
            nextNodes[index].childIds_.push_back(nextNodes[child->second].id());
        }
    }

    std::unordered_set<std::uint64_t> finalIds;
    for (const auto& node : nextNodes) {
        if (node.id().value == 0 || !finalIds.insert(node.id().value).second) {
            throw DocumentError("XML node identities are not unique after editing");
        }
    }

    document_.xml_ = std::move(nextXml);
    document_.nodes_ = std::move(nextNodes);
    document_.rootId_ = document_.nodes_.empty()
        ? std::nullopt
        : std::optional<XmlNodeId>{document_.nodes_.front().id()};
    document_.nextId_ = nextId;
    ++document_.revision_;
}

} // namespace ii::document
