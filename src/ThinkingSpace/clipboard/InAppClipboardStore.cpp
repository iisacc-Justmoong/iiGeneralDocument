#include "ThinkingSpace/clipboard/InAppClipboardStore.h"

#include <utility>

InAppClipboardStore::InAppClipboardStore(QObject* parent)
    : QObject(parent)
{
}

InAppClipboardStore::~InAppClipboardStore() = default;

bool InAppClipboardStore::hasResource() const noexcept
{
    return m_resourceImport.valid();
}

QString InAppClipboardStore::resourceFileName() const
{
    return m_resourceImport.fileName.trimmed();
}

QString InAppClipboardStore::resourceFormat() const
{
    return m_resourceImport.format.trimmed();
}

QString InAppClipboardStore::resourceType() const
{
    return m_resourceImport.type.trimmed();
}

QString InAppClipboardStore::resourceBucket() const
{
    return m_resourceImport.bucket.trimmed();
}

QString InAppClipboardStore::resourceMimeType() const
{
    return m_resourceImport.mimeType.trimmed();
}

QVariantMap InAppClipboardStore::resourceEntry() const
{
    return m_resourceImport.toVariantMap();
}

const ThinkingSpace::Clipboard::ClipboardResourceImport& InAppClipboardStore::resourceImport() const noexcept
{
    return m_resourceImport;
}

ThinkingSpace::Clipboard::ClipboardResourceImport InAppClipboardStore::takeResourceImport()
{
    ThinkingSpace::Clipboard::ClipboardResourceImport resourceImport = m_resourceImport;
    clear();
    return resourceImport;
}

bool InAppClipboardStore::setResourceImport(ThinkingSpace::Clipboard::ClipboardResourceImport resourceImport)
{
    if (!resourceImport.valid())
    {
        clear();
        return false;
    }

    m_resourceImport = std::move(resourceImport);
    emit resourceChanged();
    return true;
}

void InAppClipboardStore::clear()
{
    if (!m_resourceImport.valid()
        && m_resourceImport.fileName.trimmed().isEmpty()
        && m_resourceImport.mimeType.trimmed().isEmpty())
    {
        return;
    }

    m_resourceImport = {};
    emit resourceChanged();
}
