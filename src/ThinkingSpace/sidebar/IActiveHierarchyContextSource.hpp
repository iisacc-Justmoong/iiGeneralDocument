#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/sidebar/IActiveHierarchySource.hpp"

#include <QObject>

class IActiveHierarchyContextSource : public IActiveHierarchySource
{
    Q_OBJECT

public:
    explicit IActiveHierarchyContextSource(QObject* parent = nullptr)
        : IActiveHierarchySource(parent)
    {
    }

    ~IActiveHierarchyContextSource() override = default;

    virtual QObject* activeHierarchyController() const = 0;
    virtual QObject* activeNoteListModel() const = 0;

signals:
    void activeBindingsChanged();
    void activeHierarchyControllerChanged();
    void activeNoteListModelChanged();
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
