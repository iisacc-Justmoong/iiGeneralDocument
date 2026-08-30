#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/editor/SetTag.h"
#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteFileStore.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class TagInsertionWriter final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString tagName READ tagName WRITE setTagName NOTIFY tagNameChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit TagInsertionWriter(QObject* parent = nullptr);

    QString tagName() const;
    QString lastError() const;

    Q_INVOKABLE QStringList availableTagNames() const;
    Q_INVOKABLE QVariantMap staticTagDescriptor(const QString& tagName) const;
    Q_INVOKABLE bool configureTagName(const QString& tagName);

    Q_INVOKABLE QVariantMap insertIntoNote(
        const QString& noteId,
        const QString& noteDirectoryPath,
        int cursorPosition,
        int selectionLength = 0);
    Q_INVOKABLE QVariantMap insertNamedTagIntoNote(
        const QString& tagName,
        const QString& noteId,
        const QString& noteDirectoryPath,
        int cursorPosition,
        int selectionLength = 0);

public slots:
    void setTagName(const QString& tagName);
    void clearLastError();

signals:
    void tagNameChanged();
    void lastErrorChanged();
    void tagWriteRequested(
        const QString& noteId,
        const QString& noteDirectoryPath,
        const QString& tagName);
    void tagWriteFinished(const QVariantMap& result);

private:
    QVariantMap buildFailureResult(
        const QString& tagName,
        const QString& noteId,
        const QString& noteDirectoryPath,
        int cursorPosition,
        int selectionLength,
        const QString& bodySourceText,
        const QString& errorMessage);
    QVariantMap finishResult(QVariantMap result);
    void updateLastError(const QString& message);

    SetTag m_setTag;
    ThinkingSpaceLocalNoteFileStore m_fileStore;
    QString m_lastError;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
