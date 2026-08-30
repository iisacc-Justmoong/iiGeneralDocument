#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/clipboard/ClipboardResourceImport.h"

#include <QObject>
#include <QString>
#include <QVariantMap>

class InAppClipboardStore final : public QObject
{
    Q_OBJECT

public:
    explicit InAppClipboardStore(QObject* parent = nullptr);
    ~InAppClipboardStore() override;

    bool hasResource() const noexcept;
    QString resourceFileName() const;
    QString resourceFormat() const;
    QString resourceType() const;
    QString resourceBucket() const;
    QString resourceMimeType() const;
    QVariantMap resourceEntry() const;

    const ThinkingSpace::Clipboard::ClipboardResourceImport& resourceImport() const noexcept;
    ThinkingSpace::Clipboard::ClipboardResourceImport takeResourceImport();

    bool setResourceImport(ThinkingSpace::Clipboard::ClipboardResourceImport resourceImport);
    void clear();

signals:
    void resourceChanged();

private:
    ThinkingSpace::Clipboard::ClipboardResourceImport m_resourceImport;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
