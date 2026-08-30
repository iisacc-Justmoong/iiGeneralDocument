#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderParser.hpp"

#include "ThinkingSpace/hierarchy/ThinkingSpaceFolderIdentity.hpp"
#include "ThinkingSpace/file/note/header/ThinkingSpaceBookmarkColorPalette.hpp"
#include "ThinkingSpace/file/note/support/ThinkingSpaceIiXmlDocumentSupport.hpp"
#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"

#include <algorithm>
#include <vector>

namespace
{
    namespace IiXml = ThinkingSpace::IiXmlDocumentSupport;

    QString extractTagText(
        const iiXml::Parser::TagDocument& document,
        const QString& tagName)
    {
        return IiXml::nodeText(document, IiXml::findFirstDescendant(document.Nodes, tagName));
    }

    QStringList extractTagTexts(
        const iiXml::Parser::TagDocument& document,
        const QString& tagName)
    {
        std::vector<const iiXml::Parser::TagNode*> nodes;
        IiXml::collectDescendants(document.Nodes, tagName, &nodes);

        QStringList values;
        values.reserve(static_cast<qsizetype>(nodes.size()));
        for (const iiXml::Parser::TagNode* node : nodes)
        {
            values.push_back(IiXml::nodeText(document, node));
        }

        return values;
    }

    QString extractAttributeValue(
        const iiXml::Parser::TagDocument& document,
        const QString& tagName,
        const QString& attributeName)
    {
        return IiXml::attributeValue(
            document,
            IiXml::findFirstDescendant(document.Nodes, tagName),
            attributeName);
    }

    bool parseBooleanValue(const QString& rawValue, bool fallback)
    {
        const QString normalized = rawValue.trimmed().toCaseFolded();
        if (normalized.isEmpty())
        {
            return fallback;
        }

        if (normalized == QStringLiteral("1")
            || normalized == QStringLiteral("true")
            || normalized == QStringLiteral("yes")
            || normalized == QStringLiteral("on"))
        {
            return true;
        }

        if (normalized == QStringLiteral("0")
            || normalized == QStringLiteral("false")
            || normalized == QStringLiteral("no")
            || normalized == QStringLiteral("off"))
        {
            return false;
        }

        return fallback;
    }

    struct ParsedFolderBindings final
    {
        QStringList folders;
        QStringList folderUuids;
    };

    ParsedFolderBindings extractFolderBindings(const iiXml::Parser::TagDocument& document)
    {
        std::vector<const iiXml::Parser::TagNode*> folderNodes;
        IiXml::collectDescendants(document.Nodes, QStringLiteral("folder"), &folderNodes);
        ParsedFolderBindings bindings;
        for (const iiXml::Parser::TagNode* folderNode : folderNodes)
        {
            bindings.folders.push_back(IiXml::nodeText(document, folderNode));

            const QString folderUuid = ThinkingSpace::FolderIdentity::normalizeFolderUuid(
                IiXml::attributeValue(document, folderNode, QStringLiteral("uuid")));
            bindings.folderUuids.push_back(folderUuid);
        }

        return bindings;
    }

    QStringList parseProgressEnums(const QString& rawEnums)
    {
        QString value = rawEnums.trimmed();
        if (value.startsWith(QLatin1Char('{')) && value.endsWith(QLatin1Char('}')) && value.size() >= 2)
        {
            value = value.mid(1, value.size() - 2);
        }

        QStringList labels;
        const QStringList tokens = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        labels.reserve(tokens.size());

        for (QString token : tokens)
        {
            token = token.trimmed();
            if (token.startsWith(QLatin1Char('"')) && token.endsWith(QLatin1Char('"')) && token.size() >= 2)
            {
                token = token.mid(1, token.size() - 2).trimmed();
            }
            if (token.startsWith(QLatin1Char('\'')) && token.endsWith(QLatin1Char('\'')) && token.size() >= 2)
            {
                token = token.mid(1, token.size() - 2).trimmed();
            }
            if (!token.isEmpty())
            {
                labels.push_back(IiXml::decodeXmlEntities(token));
            }
        }

        return labels;
    }

    int parseNonNegativeIntTagValue(const iiXml::Parser::TagDocument& document, const QString& tagName)
    {
        bool ok = false;
        const int value = extractTagText(document, tagName).toInt(&ok);
        return ok ? std::max(0, value) : 0;
    }

    int parseProgressValue(const iiXml::Parser::TagDocument& document)
    {
        const iiXml::Parser::TagNode* progressNode =
            IiXml::findFirstDescendant(document.Nodes, QStringLiteral("progress"));
        if (progressNode == nullptr)
        {
            return -1;
        }

        const QString progressText = IiXml::nodeText(document, progressNode);
        bool ok = false;
        const int progressNumeric = progressText.toInt(&ok);
        if (ok)
        {
            return progressNumeric;
        }

        const QString valueAttr = IiXml::attributeValue(document, progressNode, QStringLiteral("value"));
        if (!valueAttr.isEmpty())
        {
            const int valueNumeric = valueAttr.toInt(&ok);
            if (ok)
            {
                return valueNumeric;
            }
        }

        if (progressText.isEmpty() && valueAttr.isEmpty())
        {
            return -1;
        }

        const QString enumsAttr = IiXml::attributeValue(document, progressNode, QStringLiteral("enums"));
        const QStringList enumLabels = parseProgressEnums(enumsAttr);

        if (!progressText.isEmpty())
        {
            for (int i = 0; i < enumLabels.size(); ++i)
            {
                if (QString::compare(progressText, enumLabels.at(i), Qt::CaseInsensitive) == 0)
                {
                    return i;
                }
            }
        }

        if (!valueAttr.isEmpty())
        {
            for (int i = 0; i < enumLabels.size(); ++i)
            {
                if (QString::compare(valueAttr, enumLabels.at(i), Qt::CaseInsensitive) == 0)
                {
                    return i;
                }
            }
        }

        return -1;
    }
} // namespace

ThinkingSpaceNoteHeaderParser::ThinkingSpaceNoteHeaderParser() = default;

ThinkingSpaceNoteHeaderParser::~ThinkingSpaceNoteHeaderParser() = default;

bool ThinkingSpaceNoteHeaderParser::parse(
    const QString& tsnHeadText,
    ThinkingSpaceNoteHeaderStore* outStore,
    QString* errorMessage) const
{
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.parser"),
                              QStringLiteral("parse.begin"),
                              QStringLiteral("textLength=%1").arg(tsnHeadText.size()));

    if (outStore == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("outStore must not be null.");
        }
        ThinkingSpace::Debug::traceSelf(this,
                                  QStringLiteral("note.header.parser"),
                                  QStringLiteral("parse.failed"),
                                  QStringLiteral("outStore is null"));
        return false;
    }

    if (tsnHeadText.trimmed().isEmpty())
    {
        outStore->clear();
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("tsnHeadText must not be empty.");
        }
        ThinkingSpace::Debug::traceSelf(this,
                                  QStringLiteral("note.header.parser"),
                                  QStringLiteral("parse.failed"),
                                  QStringLiteral("tsnHeadText is empty"));
        return false;
    }

    const iiXml::Parser::TagDocumentResult parsedDocument = IiXml::parseDocument(tsnHeadText);
    if (parsedDocument.Status != iiXml::Parser::TagTreeParseStatus::Parsed || !parsedDocument.Document.has_value())
    {
        outStore->clear();
        const QString diagnostic = QString::fromStdString(parsedDocument.Diagnostic.Reason);
        if (errorMessage != nullptr)
        {
            *errorMessage = diagnostic.isEmpty()
                                ? QStringLiteral("iiXml failed to parse .tsnhead document.")
                                : QStringLiteral("iiXml failed to parse .tsnhead document: %1").arg(diagnostic);
        }
        ThinkingSpace::Debug::traceSelf(this,
                                  QStringLiteral("note.header.parser"),
                                  QStringLiteral("parse.failed"),
                                  errorMessage != nullptr ? *errorMessage : QString());
        return false;
    }

    const iiXml::Parser::TagDocument& document = parsedDocument.Document.value();
    outStore->clear();
    outStore->setNoteId(extractAttributeValue(document, QStringLiteral("contents"), QStringLiteral("id")));
    outStore->setCreatedAt(extractTagText(document, QStringLiteral("created")));
    outStore->setAuthor(extractTagText(document, QStringLiteral("author")));
    outStore->setLastModifiedAt(extractTagText(document, QStringLiteral("lastModified")));
    outStore->setLastOpenedAt(extractTagText(document, QStringLiteral("lastOpened")));
    outStore->setModifiedBy(extractTagText(document, QStringLiteral("modifiedBy")));
    const ParsedFolderBindings folderBindings = extractFolderBindings(document);
    outStore->setFolderBindings(folderBindings.folders, folderBindings.folderUuids);
    outStore->setProject(extractTagText(document, QStringLiteral("project")));
    outStore->setBookmarked(parseBooleanValue(
        extractAttributeValue(document, QStringLiteral("bookmarks"), QStringLiteral("state")),
        false));
    outStore->setBookmarkColors(ThinkingSpace::Bookmarks::parseBookmarkColorsAttribute(
        extractAttributeValue(document, QStringLiteral("bookmarks"), QStringLiteral("colors"))));
    outStore->setTags(extractTagTexts(document, QStringLiteral("tag")));
    outStore->setTotalFolders(parseNonNegativeIntTagValue(document, QStringLiteral("totalFolders")));
    outStore->setTotalTags(parseNonNegativeIntTagValue(document, QStringLiteral("totalTags")));
    outStore->setLetterCount(parseNonNegativeIntTagValue(document, QStringLiteral("letterCount")));
    outStore->setWordCount(parseNonNegativeIntTagValue(document, QStringLiteral("wordCount")));
    outStore->setSentenceCount(parseNonNegativeIntTagValue(document, QStringLiteral("sentenceCount")));
    outStore->setParagraphCount(parseNonNegativeIntTagValue(document, QStringLiteral("paragraphCount")));
    outStore->setSpaceCount(parseNonNegativeIntTagValue(document, QStringLiteral("spaceCount")));
    outStore->setIndentCount(parseNonNegativeIntTagValue(document, QStringLiteral("indentCount")));
    outStore->setLineCount(parseNonNegativeIntTagValue(document, QStringLiteral("lineCount")));
    outStore->setOpenCount(parseNonNegativeIntTagValue(document, QStringLiteral("openCount")));
    outStore->setModifiedCount(parseNonNegativeIntTagValue(document, QStringLiteral("modifiedCount")));
    outStore->setBacklinkToCount(parseNonNegativeIntTagValue(document, QStringLiteral("backlinkToCount")));
    outStore->setBacklinkByCount(parseNonNegativeIntTagValue(document, QStringLiteral("backlinkByCount")));
    outStore->setIncludedResourceCount(
        parseNonNegativeIntTagValue(document, QStringLiteral("includedResourceCount")));
    outStore->setProgressEnums(parseProgressEnums(
        extractAttributeValue(document, QStringLiteral("progress"), QStringLiteral("enums"))));
    outStore->setProgress(parseProgressValue(document));

    QString isPresetValue = extractTagText(document, QStringLiteral("isPreset"));
    if (isPresetValue.isEmpty())
    {
        isPresetValue = extractAttributeValue(document, QStringLiteral("isPreset"), QStringLiteral("value"));
    }
    outStore->setPreset(parseBooleanValue(isPresetValue, false));

    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.parser"),
                              QStringLiteral("parse.success"),
                              QStringLiteral(
                                  "id=%1 folderCount=%2 tagCount=%3 openCount=%4 modifiedCount=%5 backlinkTo=%6 backlinkBy=%7 progressEnumCount=%8 progress=%9 lastOpened=%10 bookmarked=%11 preset=%12")
                              .arg(outStore->noteId())
                              .arg(outStore->folders().size())
                              .arg(outStore->tags().size())
                              .arg(outStore->openCount())
                              .arg(outStore->modifiedCount())
                              .arg(outStore->backlinkToCount())
                              .arg(outStore->backlinkByCount())
                              .arg(outStore->progressEnums().size())
                              .arg(outStore->progress())
                              .arg(outStore->lastOpenedAt())
                              .arg(outStore->isBookmarked() ? QStringLiteral("true") : QStringLiteral("false"))
                              .arg(outStore->isPreset() ? QStringLiteral("true") : QStringLiteral("false")));

    return true;
}
