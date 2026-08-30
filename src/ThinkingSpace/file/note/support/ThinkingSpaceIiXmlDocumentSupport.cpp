#include "ThinkingSpace/file/note/support/ThinkingSpaceIiXmlDocumentSupport.hpp"

namespace ThinkingSpace::IiXmlDocumentSupport
{
    QString stringFromUtf8View(std::string_view view)
    {
        return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
    }

    QString decodeXmlEntities(QString text)
    {
        text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
        text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
        text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
        text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        return text;
    }

    QString normalizeTextValue(QString text)
    {
        return decodeXmlEntities(text.trimmed());
    }

    QString stripXmlPreamble(QString source)
    {
        source = source.trimmed();

        if (source.startsWith(QStringLiteral("<?xml"), Qt::CaseInsensitive))
        {
            const qsizetype declarationEnd = source.indexOf(QStringLiteral("?>"));
            if (declarationEnd >= 0)
            {
                source = source.mid(declarationEnd + 2).trimmed();
            }
        }

        if (source.startsWith(QStringLiteral("<!DOCTYPE"), Qt::CaseInsensitive))
        {
            const qsizetype doctypeEnd = source.indexOf(QLatin1Char('>'));
            if (doctypeEnd >= 0)
            {
                source = source.mid(doctypeEnd + 1).trimmed();
            }
        }

        return source;
    }

    bool tagNameEquals(const iiXml::Parser::TagNode& node, const QString& tagName)
    {
        return QString::compare(
                   QString::fromStdString(node.Range.TagName),
                   tagName,
                   Qt::CaseInsensitive)
            == 0;
    }

    bool fieldNameEquals(
        const iiXml::Parser::TagDocument& document,
        const iiXml::Parser::TagField& field,
        const QString& attributeName)
    {
        return QString::compare(
                   stringFromUtf8View(document.FieldNameView(field)),
                   attributeName,
                   Qt::CaseInsensitive)
            == 0;
    }

    const iiXml::Parser::TagNode* findFirstDescendant(
        const std::vector<iiXml::Parser::TagNode>& nodes,
        const QString& tagName)
    {
        for (const iiXml::Parser::TagNode& node : nodes)
        {
            if (tagNameEquals(node, tagName))
            {
                return &node;
            }

            if (const iiXml::Parser::TagNode* child = findFirstDescendant(node.Children, tagName))
            {
                return child;
            }
        }

        return nullptr;
    }

    void collectDescendants(
        const std::vector<iiXml::Parser::TagNode>& nodes,
        const QString& tagName,
        std::vector<const iiXml::Parser::TagNode*>* outNodes)
    {
        if (outNodes == nullptr)
        {
            return;
        }

        for (const iiXml::Parser::TagNode& node : nodes)
        {
            if (tagNameEquals(node, tagName))
            {
                outNodes->push_back(&node);
            }
            collectDescendants(node.Children, tagName, outNodes);
        }
    }

    QString nodeText(
        const iiXml::Parser::TagDocument& document,
        const iiXml::Parser::TagNode* node)
    {
        if (node == nullptr)
        {
            return {};
        }

        return normalizeTextValue(stringFromUtf8View(document.ValueView(*node)));
    }

    QString attributeValue(
        const iiXml::Parser::TagDocument& document,
        const iiXml::Parser::TagNode* node,
        const QString& attributeName)
    {
        if (node == nullptr)
        {
            return {};
        }

        for (const iiXml::Parser::TagField& field : node->Fields)
        {
            if (!field.HasValue || !fieldNameEquals(document, field, attributeName))
            {
                continue;
            }

            return normalizeTextValue(stringFromUtf8View(document.FieldValueView(field)));
        }

        return {};
    }

    QString attributeValue(
        const iiXml::Parser::TagDocument& document,
        const iiXml::Parser::TagNode* node,
        const QStringList& attributeNames)
    {
        if (node == nullptr)
        {
            return {};
        }

        for (const QString& attributeName : attributeNames)
        {
            const QString value = attributeValue(document, node, attributeName);
            if (!value.isEmpty())
            {
                return value;
            }
        }

        return {};
    }

    iiXml::Parser::TagDocumentResult parseDocument(const QString& sourceText)
    {
        const QByteArray parseableBytes = stripXmlPreamble(sourceText).toUtf8();
        const iiXml::Parser::TagParser parser;
        return parser.ParseAllDocumentResult(
            std::string_view(parseableBytes.constData(), static_cast<std::size_t>(parseableBytes.size())));
    }
} // namespace ThinkingSpace::IiXmlDocumentSupport
