#include "ThinkingSpace/file/diff/ThinkingSpaceNoteVersionDiffBuilder.hpp"

#include <QDateTime>
#include <QStringList>
#include <QVector>

namespace
{
    QStringList linesForPatch(const QString& text)
    {
        if (text.isEmpty())
        {
            return {};
        }
        QString normalized = text;
        if (normalized.endsWith(QLatin1Char('\n')))
        {
            normalized.chop(1);
        }
        return normalized.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    }

    QString unifiedPatch(const QString& label, const QString& before, const QString& after)
    {
        const QStringList beforeLines = linesForPatch(before);
        const QStringList afterLines = linesForPatch(after);

        QString patch;
        patch += QStringLiteral("--- a/%1\n").arg(label);
        patch += QStringLiteral("+++ b/%1\n").arg(label);
        patch += QStringLiteral("@@ -1,%1 +1,%2 @@\n")
                     .arg(QString::number(beforeLines.size()), QString::number(afterLines.size()));
        for (const QString& line : beforeLines)
        {
            patch += QLatin1Char('-') + line + QLatin1Char('\n');
        }
        for (const QString& line : afterLines)
        {
            patch += QLatin1Char('+') + line + QLatin1Char('\n');
        }
        return patch;
    }

    QVector<int> occurrenceOffsets(
        const QString& text,
        const QString& needle)
    {
        QVector<int> offsets;
        if (needle.isEmpty())
        {
            return offsets;
        }

        int offset = 0;
        while (offset <= text.size())
        {
            const int found = text.indexOf(needle, offset);
            if (found < 0)
            {
                break;
            }

            offsets.push_back(found);
            offset = found + qMax(1, needle.size());
        }
        return offsets;
    }

    QString trailingAnchor(const QString& text)
    {
        if (text.isEmpty())
        {
            return {};
        }

        const int lineBreak = text.lastIndexOf(QLatin1Char('\n'));
        QString anchor = lineBreak >= 0 ? text.mid(lineBreak + 1) : text;
        if (anchor.size() > 64)
        {
            anchor = anchor.right(64);
        }
        return anchor;
    }

    QString leadingAnchor(const QString& text)
    {
        if (text.isEmpty())
        {
            return {};
        }

        const int lineBreak = text.indexOf(QLatin1Char('\n'));
        QString anchor = lineBreak >= 0 ? text.left(lineBreak) : text;
        if (anchor.size() > 64)
        {
            anchor = anchor.left(64);
        }
        return anchor;
    }

    bool segmentMatchesBase(
        const QString& base,
        const ThinkingSpaceNoteVersionDiffSegment& segment)
    {
        if (segment.prefixLength < 0
            || segment.suffixLength < 0
            || segment.prefixLength > base.size()
            || segment.suffixLength > base.size()
            || segment.prefixLength + segment.suffixLength > base.size())
        {
            return false;
        }

        const int removedLength = base.size() - segment.prefixLength - segment.suffixLength;
        return base.mid(segment.prefixLength, removedLength) == segment.removedText;
    }

    bool contextMatches(
        const QString& current,
        const int start,
        const int removedLength,
        const QString& prefixAnchor,
        const QString& suffixAnchor)
    {
        if (!prefixAnchor.isEmpty()
            && !current.left(start).endsWith(prefixAnchor))
        {
            return false;
        }
        if (!suffixAnchor.isEmpty()
            && !current.mid(start + removedLength).startsWith(suffixAnchor))
        {
            return false;
        }
        return true;
    }

    bool pickUniqueOffset(
        const QVector<int>& offsets,
        int* outOffset)
    {
        if (offsets.size() != 1)
        {
            return false;
        }
        if (outOffset != nullptr)
        {
            *outOffset = offsets.constFirst();
        }
        return true;
    }
} // namespace

ThinkingSpaceNoteVersionDiffSegment ThinkingSpaceNoteVersionDiffBuilder::diffSegment(
    const QString& before,
    const QString& after,
    const QString& label) const
{
    ThinkingSpaceNoteVersionDiffSegment segment;
    const int prefixLimit = qMin(before.size(), after.size());
    while (segment.prefixLength < prefixLimit
           && before.at(segment.prefixLength) == after.at(segment.prefixLength))
    {
        ++segment.prefixLength;
    }

    const int suffixLimit = qMin(before.size(), after.size()) - segment.prefixLength;
    while (segment.suffixLength < suffixLimit
           && before.at(before.size() - 1 - segment.suffixLength)
                  == after.at(after.size() - 1 - segment.suffixLength))
    {
        ++segment.suffixLength;
    }

    const int removedLength = qMax(0, before.size() - segment.prefixLength - segment.suffixLength);
    const int insertedLength = qMax(0, after.size() - segment.prefixLength - segment.suffixLength);
    segment.removedText = before.mid(segment.prefixLength, removedLength);
    segment.insertedText = after.mid(segment.prefixLength, insertedLength);
    segment.unifiedPatch = unifiedPatch(label, before, after);
    segment.generatedAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    return segment;
}

QString ThinkingSpaceNoteVersionDiffBuilder::applyDiffSegmentOntoCurrent(
    const QString& base,
    const QString& current,
    const ThinkingSpaceNoteVersionDiffSegment& segment,
    bool* applied,
    QString* errorMessage) const
{
    if (applied != nullptr)
    {
        *applied = false;
    }
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (!segmentMatchesBase(base, segment))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Diff segment does not match the supplied base document.");
        }
        return current;
    }

    const QString target = base.left(segment.prefixLength)
        + segment.insertedText
        + base.right(segment.suffixLength);
    if (current == target)
    {
        if (applied != nullptr)
        {
            *applied = true;
        }
        return current;
    }

    if (base == current)
    {
        if (applied != nullptr)
        {
            *applied = true;
        }
        return target;
    }

    if (segment.removedText.isEmpty() && segment.insertedText.isEmpty())
    {
        if (applied != nullptr)
        {
            *applied = true;
        }
        return current;
    }

    const bool replacesWholeBase =
        segment.prefixLength == 0
        && segment.suffixLength == 0
        && segment.removedText == base
        && !base.isEmpty();
    if (replacesWholeBase)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Refusing to apply a whole document diff onto a diverged current document.");
        }
        return current;
    }

    const QString prefix = base.left(segment.prefixLength);
    const QString suffix = base.right(segment.suffixLength);
    const QString prefixAnchor = trailingAnchor(prefix);
    const QString suffixAnchor = leadingAnchor(suffix);

    int applyOffset = -1;
    if (!segment.removedText.isEmpty())
    {
        QVector<int> candidates;
        const QVector<int> offsets = occurrenceOffsets(current, segment.removedText);
        for (const int offset : offsets)
        {
            if (contextMatches(
                    current,
                    offset,
                    segment.removedText.size(),
                    prefixAnchor,
                    suffixAnchor))
            {
                candidates.push_back(offset);
            }
        }
        if (candidates.isEmpty())
        {
            candidates = offsets;
        }
        if (!pickUniqueOffset(candidates, &applyOffset))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to locate an unambiguous removed-text diff target.");
            }
            return current;
        }

        QString merged = current;
        merged.replace(applyOffset, segment.removedText.size(), segment.insertedText);
        if (applied != nullptr)
        {
            *applied = true;
        }
        return merged;
    }

    if (!suffix.isEmpty())
    {
        QVector<int> candidates;
        const QVector<int> offsets = occurrenceOffsets(current, suffix);
        for (const int offset : offsets)
        {
            if (prefixAnchor.isEmpty()
                || current.left(offset).endsWith(prefixAnchor))
            {
                candidates.push_back(offset);
            }
        }
        if (candidates.isEmpty())
        {
            candidates = offsets;
        }
        if (pickUniqueOffset(candidates, &applyOffset))
        {
            QString merged = current;
            merged.insert(applyOffset, segment.insertedText);
            if (applied != nullptr)
            {
                *applied = true;
            }
            return merged;
        }
    }

    if (!prefix.isEmpty())
    {
        QVector<int> candidates;
        const QVector<int> offsets = occurrenceOffsets(current, prefix);
        for (const int offset : offsets)
        {
            if (suffixAnchor.isEmpty()
                || current.mid(offset + prefix.size()).startsWith(suffixAnchor))
            {
                candidates.push_back(offset + prefix.size());
            }
        }
        if (candidates.isEmpty())
        {
            const QString prefixTail = trailingAnchor(prefix);
            const QVector<int> tailOffsets = occurrenceOffsets(current, prefixTail);
            for (const int offset : tailOffsets)
            {
                candidates.push_back(offset + prefixTail.size());
            }
        }
        if (pickUniqueOffset(candidates, &applyOffset))
        {
            QString merged = current;
            merged.insert(applyOffset, segment.insertedText);
            if (applied != nullptr)
            {
                *applied = true;
            }
            return merged;
        }
    }

    if (base.isEmpty() && current.isEmpty())
    {
        if (applied != nullptr)
        {
            *applied = true;
        }
        return segment.insertedText;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = QStringLiteral("Failed to locate an unambiguous insertion diff target.");
    }
    return current;
}
