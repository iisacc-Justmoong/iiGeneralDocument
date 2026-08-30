#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

class GetProperty final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString tagName READ tagName NOTIFY tagNameChanged)
    Q_PROPERTY(QVariantMap properties READ properties NOTIFY propertiesChanged)
    Q_PROPERTY(QVariantMap valueKinds READ valueKinds NOTIFY propertiesChanged)
    Q_PROPERTY(int propertyCount READ propertyCount NOTIFY propertiesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit GetProperty(QObject* parent = nullptr);

    QString tagName() const;
    QVariantMap properties() const;
    QVariantMap valueKinds() const;
    int propertyCount() const;
    QString lastError() const;

    Q_INVOKABLE QVariantMap readPropertiesFromSource(
        const QString& bodySourceText,
        int tagPosition);
    Q_INVOKABLE QVariantMap readPropertiesFromBodyDocument(
        const QString& bodyDocumentText,
        int tagPosition);
    Q_INVOKABLE bool containsProperty(const QString& propertyName) const;
    Q_INVOKABLE QVariant propertyValue(const QString& propertyName) const;

public slots:
    void clearProperties();
    void clearLastError();

signals:
    void tagNameChanged();
    void propertiesChanged();
    void lastErrorChanged();
    void propertiesCaptured(const QVariantMap& result);

private:
    void applyCapturedProperties(
        const QString& tagName,
        const QVariantMap& properties,
        const QVariantMap& valueKinds);
    void updateLastError(const QString& message);

    QString m_tagName;
    QVariantMap m_properties;
    QVariantMap m_valueKinds;
    QString m_lastError;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
