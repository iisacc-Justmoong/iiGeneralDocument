#include "ThinkingSpace/editor/EditorFontFamilyProvider.hpp"

#include <QFontDatabase>
#include <QSet>
#include <QVariantMap>

#include <algorithm>

EditorFontFamilyProvider::EditorFontFamilyProvider(QObject* parent)
    : QObject(parent)
{
    refreshFontFamilies();
}

EditorFontFamilyProvider::~EditorFontFamilyProvider() = default;

QStringList EditorFontFamilyProvider::fontFamilies() const
{
    return m_fontFamilies;
}

int EditorFontFamilyProvider::fontFamilyCount() const noexcept
{
    return static_cast<int>(m_fontFamilies.size());
}

QVariantList EditorFontFamilyProvider::fontFamilyMenuItems() const
{
    return fontFamilyMenuItemsForFamilies(m_fontFamilies);
}

QStringList EditorFontFamilyProvider::normalizedFontFamiliesForMenu(const QStringList& families)
{
    QStringList normalizedFamilies;
    QSet<QString> seenFamilyKeys;

    for (const QString& family : families)
    {
        const QString trimmedFamily = family.trimmed();
        if (trimmedFamily.isEmpty())
        {
            continue;
        }

        const QString familyKey = trimmedFamily.toCaseFolded();
        if (seenFamilyKeys.contains(familyKey))
        {
            continue;
        }

        seenFamilyKeys.insert(familyKey);
        normalizedFamilies.append(trimmedFamily);
    }

    std::sort(
        normalizedFamilies.begin(),
        normalizedFamilies.end(),
        [](const QString& lhs, const QString& rhs)
        {
            return QString::localeAwareCompare(lhs, rhs) < 0;
        });
    return normalizedFamilies;
}

QVariantList EditorFontFamilyProvider::fontFamilyMenuItemsForFamilies(const QStringList& families)
{
    QVariantList menuItems;
    const QStringList normalizedFamilies = normalizedFontFamiliesForMenu(families);
    menuItems.reserve(normalizedFamilies.size());

    for (int index = 0; index < normalizedFamilies.size(); ++index)
    {
        const QString& family = normalizedFamilies.at(index);
        QVariantMap eventPayload;
        eventPayload.insert(QStringLiteral("fontFamily"), family);

        QVariantMap item;
        item.insert(QStringLiteral("id"), QStringLiteral("font-family-%1").arg(index));
        item.insert(QStringLiteral("label"), family);
        item.insert(QStringLiteral("fontFamily"), family);
        item.insert(QStringLiteral("showIconSlot"), false);
        item.insert(QStringLiteral("keyVisible"), false);
        item.insert(QStringLiteral("eventName"), QStringLiteral("editor.toolbar.font"));
        item.insert(QStringLiteral("eventPayload"), eventPayload);
        menuItems.append(item);
    }

    return menuItems;
}

void EditorFontFamilyProvider::refreshFontFamilies()
{
    const QStringList nextFamilies = normalizedFontFamiliesForMenu(QFontDatabase::families());
    if (m_fontFamilies == nextFamilies)
    {
        return;
    }

    m_fontFamilies = nextFamilies;
    emit fontFamiliesChanged();
}

void EditorFontFamilyProvider::requestProviderHook(const QString& reason)
{
    emit providerHookRequested(reason);
}
