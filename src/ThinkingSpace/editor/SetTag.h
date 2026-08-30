#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class SetTag final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString tagName READ tagName WRITE setTagName NOTIFY tagNameChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit SetTag(QObject* parent = nullptr);

    QString tagName() const;
    QString lastError() const;

    Q_INVOKABLE QStringList availableTagNames() const;
    Q_INVOKABLE QVariantMap staticTagDescriptor(const QString& tagName) const;
    Q_INVOKABLE bool configureTagName(const QString& tagName);

    Q_INVOKABLE QVariantMap insertIntoSource(
        const QString& bodySourceText,
        int cursorPosition,
        int selectionLength = 0);
    Q_INVOKABLE QVariantMap insertNamedTagIntoSource(
        const QString& tagName,
        const QString& bodySourceText,
        int cursorPosition,
        int selectionLength = 0);
    Q_INVOKABLE QVariantMap insertStyleTagIntoSource(
        const QString& styleValue,
        const QString& bodySourceText,
        int cursorPosition,
        int selectionLength = 0);
    Q_INVOKABLE QVariantMap insertStyleFontTagIntoSource(
        const QString& fontFamily,
        const QString& bodySourceText,
        int cursorPosition,
        int selectionLength = 0);
    Q_INVOKABLE QVariantMap insertStyleFontSizeTagIntoSource(
        const QString& fontSize,
        const QString& bodySourceText,
        int cursorPosition,
        int selectionLength = 0);
    Q_INVOKABLE QVariantMap insertStyleFontWeightTagIntoSource(
        const QString& fontWeight,
        const QString& bodySourceText,
        int cursorPosition,
        int selectionLength = 0);
    Q_INVOKABLE QVariantMap insertStyleBackgroundTagIntoSource(
        const QString& backgroundColor,
        const QString& bodySourceText,
        int cursorPosition,
        int selectionLength = 0);
    Q_INVOKABLE QVariantMap insertIntoBodyDocument(
        const QString& noteId,
        const QString& bodyDocumentText,
        int cursorPosition,
        int selectionLength = 0);
    Q_INVOKABLE QVariantMap insertNamedTagIntoBodyDocument(
        const QString& tagName,
        const QString& noteId,
        const QString& bodyDocumentText,
        int cursorPosition,
        int selectionLength = 0);

public slots:
    void setTagName(const QString& tagName);
    void clearLastError();

signals:
    void tagNameChanged();
    void lastErrorChanged();
    void tagInserted(const QVariantMap& result);

private:
    void updateLastError(const QString& message);

    QString m_tagName;
    QString m_lastError;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
