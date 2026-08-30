#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QVector>

class IActiveHierarchyContextSource;

class NoteActiveStateTracker final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject* hierarchyContextSource READ hierarchyContextSource WRITE setHierarchyContextSource NOTIFY hierarchyContextSourceChanged)
    Q_PROPERTY(int activeHierarchyIndex READ activeHierarchyIndex NOTIFY activeHierarchyIndexChanged)
    Q_PROPERTY(QObject* activeHierarchyController READ activeHierarchyController NOTIFY activeHierarchyControllerChanged)
    Q_PROPERTY(QObject* activeNoteListModel READ activeNoteListModel NOTIFY activeNoteListModelChanged)
    Q_PROPERTY(QVariantMap activeNoteEntry READ activeNoteEntry NOTIFY activeNoteEntryChanged)
    Q_PROPERTY(QString activeNoteId READ activeNoteId NOTIFY activeNoteIdChanged)
    Q_PROPERTY(QString activeNoteDirectoryPath READ activeNoteDirectoryPath NOTIFY activeNoteDirectoryPathChanged)
    Q_PROPERTY(QString activeNoteBodyPath READ activeNoteBodyPath NOTIFY activeNoteBodyPathChanged)
    Q_PROPERTY(QString activeNoteBodyText READ activeNoteBodyText NOTIFY activeNoteBodyTextChanged)
    Q_PROPERTY(bool hasActiveNote READ hasActiveNote NOTIFY hasActiveNoteChanged)

public:
    explicit NoteActiveStateTracker(QObject* parent = nullptr);
    ~NoteActiveStateTracker() override;

    QObject* hierarchyContextSource() const noexcept;
    void setHierarchyContextSource(QObject* source);

    int activeHierarchyIndex() const noexcept;
    QObject* activeHierarchyController() const noexcept;
    QObject* activeNoteListModel() const noexcept;
    QVariantMap activeNoteEntry() const;
    QString activeNoteId() const;
    QString activeNoteDirectoryPath() const;
    QString activeNoteBodyPath() const;
    QString activeNoteBodyText() const;
    bool hasActiveNote() const noexcept;

    Q_INVOKABLE QVariantMap readActiveNoteEntry() const;
    Q_INVOKABLE void refresh();

signals:
    void hierarchyContextSourceChanged();
    void activeHierarchyIndexChanged();
    void activeHierarchyControllerChanged();
    void activeNoteListModelChanged();
    void activeNoteEntryChanged();
    void activeNoteIdChanged();
    void activeNoteDirectoryPathChanged();
    void activeNoteBodyPathChanged();
    void activeNoteBodyTextChanged();
    void hasActiveNoteChanged();
    void activeNoteStateChanged();

private slots:
    void handleHierarchyContextDestroyed();
    void handleActiveNoteListModelDestroyed();
    void synchronizeActiveBindings();
    void refreshActiveNoteState();

private:
    void disconnectHierarchyContextSource();
    void disconnectActiveNoteListModel();
    void setActiveNoteListModel(QObject* model);
    void setActiveNoteState(
        QVariantMap noteEntry,
        QString noteId,
        QString noteDirectoryPath,
        QString noteBodyText);

    QPointer<IActiveHierarchyContextSource> m_hierarchyContextSource;
    QPointer<QObject> m_activeHierarchyController;
    QPointer<QObject> m_activeNoteListModel;
    QVector<QMetaObject::Connection> m_hierarchyConnections;
    QVector<QMetaObject::Connection> m_noteListConnections;
    QVariantMap m_activeNoteEntry;
    QString m_activeNoteId;
    QString m_activeNoteDirectoryPath;
    QString m_activeNoteBodyPath;
    QString m_activeNoteBodyText;
    int m_activeHierarchyIndex = 0;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
