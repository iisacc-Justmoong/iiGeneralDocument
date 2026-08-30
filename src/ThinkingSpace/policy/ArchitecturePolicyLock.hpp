#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>

namespace ThinkingSpace::Policy
{
    enum class Layer : int
    {
        View = 0,
        Controller,
        DataModel,
        Store,
        Parser,
        Creator,
        FileSystem
    };

    class ArchitecturePolicyLock final
    {
    public:
        static bool isLocked() noexcept;
        static void lock() noexcept;
        static void unlockForTests() noexcept;

    private:
        ArchitecturePolicyLock() = delete;
    };

    const char* layerName(Layer layer) noexcept;
    bool isDependencyAllowed(Layer from, Layer to) noexcept;
    bool assertDependencyAllowed(Layer from, Layer to, QString* errorMessage = nullptr);
    bool verifyDependencyAllowed(Layer from,
                                 Layer to,
                                 const QString& context = QString(),
                                 QString* errorMessage = nullptr);
    bool verifyMutableWiringAllowed(const QString& context = QString(),
                                    QString* errorMessage = nullptr);
    bool verifyMutableDependencyAllowed(Layer from,
                                        Layer to,
                                        const QString& context = QString(),
                                        QString* errorMessage = nullptr);
} // namespace ThinkingSpace::Policy

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
