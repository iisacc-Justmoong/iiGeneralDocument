#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

class SetProperty final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit SetProperty(QObject* parent = nullptr);

    QString lastError() const;

    Q_INVOKABLE QVariantMap setPropertyInSource(
        const QString& bodySourceText,
        int tagPosition,
        const QString& propertyName,
        const QVariant& value);
    Q_INVOKABLE QVariantMap setPropertyInBodyDocument(
        const QString& noteId,
        const QString& bodyDocumentText,
        int tagPosition,
        const QString& propertyName,
        const QVariant& value);

public slots:
    void clearLastError();

signals:
    void lastErrorChanged();
    void propertySet(const QVariantMap& result);

private:
    void updateLastError(const QString& message);

    QString m_lastError;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
