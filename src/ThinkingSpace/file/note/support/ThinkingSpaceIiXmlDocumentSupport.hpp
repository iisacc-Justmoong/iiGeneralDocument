#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>

#include <iiXml.h>

#include <string_view>
#include <vector>

namespace ThinkingSpace::IiXmlDocumentSupport
{
    QString stringFromUtf8View(std::string_view view);
    QString decodeXmlEntities(QString text);
    QString normalizeTextValue(QString text);
    QString stripXmlPreamble(QString source);

    bool tagNameEquals(const iiXml::Parser::TagNode& node, const QString& tagName);
    bool fieldNameEquals(
        const iiXml::Parser::TagDocument& document,
        const iiXml::Parser::TagField& field,
        const QString& attributeName);

    const iiXml::Parser::TagNode* findFirstDescendant(
        const std::vector<iiXml::Parser::TagNode>& nodes,
        const QString& tagName);
    void collectDescendants(
        const std::vector<iiXml::Parser::TagNode>& nodes,
        const QString& tagName,
        std::vector<const iiXml::Parser::TagNode*>* outNodes);

    QString nodeText(
        const iiXml::Parser::TagDocument& document,
        const iiXml::Parser::TagNode* node);
    QString attributeValue(
        const iiXml::Parser::TagDocument& document,
        const iiXml::Parser::TagNode* node,
        const QString& attributeName);
    QString attributeValue(
        const iiXml::Parser::TagDocument& document,
        const iiXml::Parser::TagNode* node,
        const QStringList& attributeNames);

    iiXml::Parser::TagDocumentResult parseDocument(const QString& sourceText);
} // namespace ThinkingSpace::IiXmlDocumentSupport

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
