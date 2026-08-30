#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QObject>

class IActiveHierarchySource : public QObject
{
    Q_OBJECT

public:
    explicit IActiveHierarchySource(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~IActiveHierarchySource() override = default;

    virtual int activeHierarchyIndex() const noexcept = 0;

signals:
    void activeHierarchyIndexChanged();
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
