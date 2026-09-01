#include "Core/Diagnostic.h"
#include "TestSupport.h"
#include "Xml/XmlTreeDocument.h"
#include "Xml/XmlTreeEditor.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

using namespace ii::document;

namespace {

const XmlNode& findByName(const XmlTreeDocument& document, const std::string& name)
{
    for (const auto& node : document.nodes()) {
        if (node.name() == name) {
            return node;
        }
    }
    throw DocumentError("XML tree test node was not found");
}

const XmlNode& findByInnerXml(const XmlTreeDocument& document, const std::string& innerXml)
{
    for (const auto& node : document.nodes()) {
        if (node.innerXml() == innerXml) {
            return node;
        }
    }
    throw DocumentError("XML tree test value was not found");
}

const XmlAttribute& findAttribute(const XmlNode& node, const std::string& name)
{
    for (const auto& attribute : node.attributes()) {
        if (attribute.name() == name) {
            return attribute;
        }
    }
    throw DocumentError("XML tree test attribute was not found");
}

template<typename Function>
void expectDocumentError(Function&& function, const char* message)
{
    try {
        function();
    } catch (const DocumentError&) {
        return;
    }
    expect(false, message);
}

void parsesHierarchyAndTypedAttributes()
{
    const std::string source =
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE catalog>\n"
        "<catalog id=\"root\"><group order=1 enabled=true ratio=0.5>"
        "<item lang=\"ko\">하나</item><item>Two</item>"
        "</group></catalog>\n";
    const auto document = XmlTreeDocument::fromXml(source);

    expect(document.xml() == source, "XML source is preserved byte-for-byte on read");
    expect(document.nodes().size() == 4, "iiXml hierarchy exposes every element node");
    expect(document.rootId().has_value(), "parsed XML has one addressable root");
    expect(document.revision() == 0, "loading XML does not count as a mutation");

    const XmlNode* root = document.find(*document.rootId());
    expect(root != nullptr && root->name() == "catalog", "root id resolves to catalog");
    expect(!root->parentId().has_value(), "root has no parent id");
    expect(root->depth() == 0, "XML root has depth zero");
    expect(root->childIds().size() == 1, "root exposes its direct child id");
    expect(root->openingTag() == "<catalog id=\"root\">",
           "XML root exposes its complete opening tag");
    expect(root->closingTag() == "</catalog>",
           "XML root exposes its matching closing tag");
    expect(findAttribute(*root, "id").value() == "root", "quoted string attribute is readable");
    expect(findAttribute(*root, "id").valueType() == XmlAttributeValueType::string,
           "quoted attribute is typed as string by iiXml");

    const XmlNode* group = document.find(root->childIds().front());
    expect(group != nullptr && group->name() == "group", "child id resolves to group");
    expect(group->parentId() == root->id(), "child exposes its parent identity");
    expect(group->depth() == 1, "XML child exposes its hierarchy depth");
    expect(group->childIds().size() == 2, "group exposes two direct item children");
    expect(findAttribute(*group, "order").valueType() == XmlAttributeValueType::integer,
           "integer attribute type is retained");
    expect(findAttribute(*group, "enabled").valueType() == XmlAttributeValueType::boolean,
           "boolean attribute type is retained");
    expect(findAttribute(*group, "ratio").valueType() == XmlAttributeValueType::real,
           "real attribute type is retained");

    std::unordered_set<std::uint64_t> ids;
    for (const auto& node : document.nodes()) {
        expect(node.id().value != 0, "every XML node receives a non-zero id");
        ids.insert(node.id().value);
        expect(node.rawBegin() < node.rawEnd(), "node raw range is non-empty");
        expect(source.substr(node.rawBegin(), node.rawEnd() - node.rawBegin()) == node.rawXml(),
               "node raw XML matches its source range");
    }
    expect(ids.size() == document.nodes().size(), "XML node ids are document-wide unique");
}

void rejectsNonHierarchicalDocuments()
{
    expectDocumentError(
        [] { static_cast<void>(XmlTreeDocument::fromXml("<a></a><b></b>")); },
        "XML document rejects multiple roots");
    expectDocumentError(
        [] { static_cast<void>(XmlTreeDocument::fromXml("<root><a><b></a></b></root>")); },
        "XML document rejects iiXml cross-close overlays");
    expectDocumentError(
        [] { static_cast<void>(XmlTreeDocument::fromXml("before<root></root>")); },
        "XML document rejects text before the root");
    expectDocumentError(
        [] { static_cast<void>(XmlTreeDocument::fromXml("<root></root>after")); },
        "XML document rejects text after the root");
    expectDocumentError(
        [] {
            static_cast<void>(XmlTreeDocument::fromXml(
                "<root></root><?xml version=\"1.0\"?>"));
        },
        "XML declaration is rejected outside the prolog");
    expectDocumentError(
        [] { static_cast<void>(XmlTreeDocument::fromXml("<?xml version=\"1.0\"?>\n")); },
        "XML document requires one root");
    expectDocumentError(
        [] { static_cast<void>(XmlTreeDocument::fromXml("<root>broken")); },
        "XML document rejects malformed markup");
}

void preservesBomCommentsAndProcessingInstructions()
{
    const std::string source =
        "\xef\xbb\xbf"
        "<?xml-stylesheet type=\"text/xsl\"?>\n"
        "<!--before--><root><![CDATA[<raw>]]><?inside value?></root>"
        "<!--after-->\n";
    const auto document = XmlTreeDocument::fromXml(source);

    expect(document.xml() == source, "BOM and XML miscellaneous markup are preserved exactly");
    expect(document.nodes().size() == 1, "comments, CDATA, and processing instructions are not nodes");
    expect(document.nodes().front().innerXml() == "<![CDATA[<raw>]]><?inside value?>",
           "CDATA and processing instructions remain in root inner XML");
}

void createsRootAndNestedSubtrees()
{
    XmlTreeDocument document;
    XmlTreeEditor editor(document);

    const XmlNodeId rootId = editor.create("  <catalog></catalog>  ");
    expect(document.xml() == "<catalog></catalog>", "create initializes an empty XML document");
    expect(document.rootId() == rootId, "create returns the new root identity");
    expect(editor.read(rootId) != nullptr, "created root is immediately readable");

    const XmlNodeId groupId = editor.create(
        "<group><item>One</item><item>Two</item></group>", rootId);
    expect(document.xml() ==
               "<catalog><group><item>One</item><item>Two</item></group></catalog>",
           "child creation appends a complete subtree before the parent close tag");
    const XmlNode* root = editor.read(rootId);
    const XmlNode* group = editor.read(groupId);
    expect(root != nullptr && root->childIds() == std::vector<XmlNodeId>{groupId},
           "parent exposes the newly created direct child");
    expect(group != nullptr && group->parentId() == rootId && group->childIds().size() == 2,
           "created subtree has parent and child identities");
    expect(document.revision() == 2, "each successful create increments revision once");

    expectDocumentError(
        [&] { static_cast<void>(editor.create("<second></second>")); },
        "create rejects a second document root");
}

void expandsSelfClosingParentWhenCreatingAChild()
{
    auto document = XmlTreeDocument::fromXml("<root><leaf code=1 /></root>");
    XmlTreeEditor editor(document);
    const XmlNodeId rootId = *document.rootId();
    const XmlNodeId leafId = findByName(document, "leaf").id();
    const XmlNode* leaf = editor.read(leafId);
    expect(leaf != nullptr && leaf->isSelfClosing(),
           "XML self-closing node is identified from its tag boundary");
    expect(leaf->openingTag() == "<leaf code=1 />" && leaf->closingTag().empty(),
           "XML self-closing node exposes only its opening tag");
    expect(leaf->depth() == 1, "XML self-closing node participates in hierarchy depth");

    const XmlNodeId childId = editor.create("<child>value</child>", leafId);

    expect(document.xml() == "<root><leaf code=1 ><child>value</child></leaf></root>",
           "creating below a self-closing node expands it into a paired tag");
    expect(editor.read(rootId) != nullptr && editor.read(leafId) != nullptr,
           "self-closing expansion preserves ancestor and parent ids");
    expect(editor.read(childId) != nullptr && editor.read(childId)->parentId() == leafId,
           "created child receives an independent identity");
    expect(editor.read(leafId)->openingTag() == "<leaf code=1 >"
               && editor.read(leafId)->closingTag() == "</leaf>"
               && !editor.read(leafId)->isSelfClosing(),
           "expanded parent exposes separate opening and closing tags");
    expect(editor.read(childId)->depth() == 2,
           "created descendant receives its parsed hierarchy depth");
}

void updatesSubtreeAndPreservesUnaffectedIds()
{
    auto document = XmlTreeDocument::fromXml(
        "<catalog><group><item>Old</item></group><keep>Same</keep></catalog>");
    XmlTreeEditor editor(document);
    const XmlNodeId rootId = *document.rootId();
    const XmlNodeId groupId = findByName(document, "group").id();
    const XmlNodeId oldId = findByInnerXml(document, "Old").id();
    const XmlNodeId keepId = findByInnerXml(document, "Same").id();

    editor.update(groupId, "<section><item>New</item><extra flag=true /></section>");

    expect(document.xml() ==
               "<catalog><section><item>New</item><extra flag=true /></section>"
               "<keep>Same</keep></catalog>",
           "update replaces exactly one XML subtree");
    expect(editor.read(groupId) != nullptr && editor.read(groupId)->name() == "section",
           "updated subtree root retains its identity after a name change");
    expect(editor.read(rootId) != nullptr && editor.read(keepId) != nullptr,
           "ancestor and unrelated shifted node ids survive update");
    expect(editor.read(oldId) == nullptr, "replaced descendants are removed from the identity map");
    expect(findByInnerXml(document, "New").id() != oldId,
           "new descendants receive new identities");
    expect(document.revision() == 1, "successful update increments revision exactly once");
}

void identicalUpdateIsANoOp()
{
    auto document = XmlTreeDocument::fromXml(
        "<root><section><item>Same</item></section></root>");
    XmlTreeEditor editor(document);
    const XmlNodeId sectionId = findByName(document, "section").id();
    std::vector<XmlNodeId> ids;
    for (const auto& node : document.nodes()) {
        ids.push_back(node.id());
    }

    editor.update(sectionId, "  <section><item>Same</item></section>  ");

    std::vector<XmlNodeId> resultingIds;
    for (const auto& node : document.nodes()) {
        resultingIds.push_back(node.id());
    }
    expect(document.revision() == 0, "identical XML update does not create a revision");
    expect(resultingIds == ids, "identical XML update preserves descendant identities");
}

void removesSubtreesAndPreservesTheProlog()
{
    const std::string prefix =
        "<?xml version=\"1.0\"?>\n"
        "<!Doctype root>\n";
    auto document = XmlTreeDocument::fromXml(
        prefix + "<root><branch><item>Delete</item></branch><keep>Stay</keep></root>\n");
    XmlTreeEditor editor(document);
    const XmlNodeId rootId = *document.rootId();
    const XmlNodeId branchId = findByName(document, "branch").id();
    const XmlNodeId itemId = findByInnerXml(document, "Delete").id();
    const XmlNodeId keepId = findByInnerXml(document, "Stay").id();

    expect(editor.remove(branchId), "remove deletes an existing XML subtree");
    expect(document.xml() == prefix + "<root><keep>Stay</keep></root>\n",
           "remove erases only the selected raw range");
    expect(editor.read(branchId) == nullptr && editor.read(itemId) == nullptr,
           "removing a parent deletes all descendant identities");
    expect(editor.read(rootId) != nullptr && editor.read(keepId) != nullptr,
           "ancestor and unrelated identities survive remove");
    expect(!editor.remove(XmlNodeId{9999}), "remove reports a missing id without mutation");

    expect(editor.remove(rootId), "root removal is supported");
    expect(document.xml() == prefix + "\n", "root removal preserves prolog and trailing whitespace");
    expect(!document.rootId().has_value() && document.nodes().empty(),
           "root removal leaves a valid editable empty state");
    expect(document.revision() == 2, "only successful removes increment revision");

    const XmlNodeId replacementRoot = editor.create("<replacement></replacement>");
    expect(document.rootId() == replacementRoot, "a root can be recreated after removal");
    expect(document.xml() == prefix + "\n<replacement></replacement>",
           "recreated root is appended without altering the preserved prolog");
}

void neverReusesDeletedIds()
{
    auto document = XmlTreeDocument::fromXml("<root><item>Old</item></root>");
    XmlTreeEditor editor(document);
    const XmlNodeId rootId = *document.rootId();
    const XmlNodeId oldId = findByInnerXml(document, "Old").id();

    expect(editor.remove(oldId), "old node is removed before identity reuse check");
    const XmlNodeId newId = editor.create("<item>New</item>", rootId);

    expect(newId != oldId, "deleted XML node ids are never reused");
    expect(editor.read(oldId) == nullptr && editor.read(newId) != nullptr,
           "new XML identity cannot alias a deleted node");
}

void rejectsInvalidMutationsAtomically()
{
    auto document = XmlTreeDocument::fromXml("<root><item>Safe</item></root>");
    XmlTreeEditor editor(document);
    const XmlNodeId rootId = *document.rootId();
    const XmlNodeId itemId = findByInnerXml(document, "Safe").id();
    const std::string originalXml = document.xml();
    const std::size_t originalCount = document.nodes().size();

    expectDocumentError(
        [&] { static_cast<void>(editor.create("plain text", rootId)); },
        "create rejects text without an element root");
    expectDocumentError(
        [&] { static_cast<void>(editor.create("<a></a><b></b>", rootId)); },
        "create rejects multiple fragment roots");
    expectDocumentError(
        [&] { static_cast<void>(editor.create("<child></child>", XmlNodeId{9999})); },
        "create rejects a missing parent id");
    expectDocumentError(
        [&] { editor.update(itemId, "<item>broken"); },
        "update rejects malformed XML");
    expectDocumentError(
        [&] { editor.update(XmlNodeId{9999}, "<missing></missing>"); },
        "update rejects a missing target id");

    expect(document.xml() == originalXml, "failed XML CRUD leaves source unchanged");
    expect(document.nodes().size() == originalCount, "failed XML CRUD leaves node state unchanged");
    expect(document.revision() == 0, "failed XML CRUD leaves revision unchanged");
    expect(editor.read(rootId) != nullptr && editor.read(itemId) != nullptr,
           "failed XML CRUD leaves existing identities readable");
}

} // namespace

int main()
{
    parsesHierarchyAndTypedAttributes();
    rejectsNonHierarchicalDocuments();
    preservesBomCommentsAndProcessingInstructions();
    createsRootAndNestedSubtrees();
    expandsSelfClosingParentWhenCreatingAChild();
    updatesSubtreeAndPreservesUnaffectedIds();
    identicalUpdateIsANoOp();
    removesSubtreesAndPreservesTheProlog();
    neverReusesDeletedIds();
    rejectsInvalidMutationsAtomically();
}
