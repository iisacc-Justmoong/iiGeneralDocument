#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>

namespace ThinkingSpace::EditorComponent
{
    struct StyleToken final
    {
        bool valid = false;
        QString name;
        int pixelSize = 0;
        int weight = 0;
        QString styleName;
        int lineHeight = 0;
    };

    struct StyleSourceBaseline final
    {
        bool italic = false;
        int weight = 0;
        QString background;
    };

    class Style final
    {
    public:
        static QString canonicalName();
        static QString openingToken();
        static QString closingToken();
        static QString defaultEditorFontFamily();
        static QStringList styleAttributeValues();
        static QString normalizedStyleAttributeValue(QString value);
        static QString openingTokenForStyleAttributeValue(QString value);
        static QString normalizedFontFamilyAttributeValue(QString value);
        static QString openingTokenForFontFamily(QString value);
        static QString normalizedFontSizeAttributeValue(QString value);
        static QString openingTokenForFontSize(QString value);
        static QString normalizedFontWeightAttributeValue(QString value);
        static QString openingTokenForFontWeight(QString value);
        static QString normalizedBackgroundAttributeValue(QString value);
        static QString openingTokenForBackground(QString value);
        static StyleToken lvrsTextStyleTokenFromName(QString tokenName);
        static QString bodyEditorCssDeclaration();
        static QString attributeValueFromRawToken(const QString& rawTagText, const QString& attributeName);
        static QString cssDeclarationFromRawToken(const QString& rawTagText);
        static QString openingHtmlFromRawToken(const QString& rawTagText);
        static QString closingHtml();
        static QString markerContentHtml(QString markerBody);
        static int weightValue(const QString& value);
        static bool spanMatchesOpeningToken(const QString& spanOpeningTag, const QString& openingToken);
        static StyleSourceBaseline sourceBaselineFromOpeningToken(const QString& openingToken);
    };
} // namespace ThinkingSpace::EditorComponent

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
