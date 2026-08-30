#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QObject>
#include <QStringList>
#include <QVariantList>

class EditorFontFamilyProvider final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QStringList fontFamilies READ fontFamilies NOTIFY fontFamiliesChanged)
    Q_PROPERTY(int fontFamilyCount READ fontFamilyCount NOTIFY fontFamiliesChanged)

public:
    explicit EditorFontFamilyProvider(QObject* parent = nullptr);
    ~EditorFontFamilyProvider() override;

    QStringList fontFamilies() const;
    int fontFamilyCount() const noexcept;

    Q_INVOKABLE QVariantList fontFamilyMenuItems() const;

    static QStringList normalizedFontFamiliesForMenu(const QStringList& families);
    static QVariantList fontFamilyMenuItemsForFamilies(const QStringList& families);

public slots:
    void refreshFontFamilies();
    void requestProviderHook(const QString& reason = QString());

signals:
    void fontFamiliesChanged();
    void providerHookRequested(const QString& reason);

private:
    QStringList m_fontFamilies;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
