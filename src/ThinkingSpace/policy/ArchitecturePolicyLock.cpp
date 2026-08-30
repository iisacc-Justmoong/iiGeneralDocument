#include "ThinkingSpace/policy/ArchitecturePolicyLock.hpp"

#include <array>
#include <atomic>
#include <QDebug>

namespace
{
    using Layer = ThinkingSpace::Policy::Layer;
    using Row = std::array<bool, 7>;

    constexpr int layerIndex(Layer layer) noexcept
    {
        return static_cast<int>(layer);
    }

    // Fixed architecture contract:
    // View -> Controller
    // Controller -> DataModel, Store, Parser, Creator
    // DataModel -> none
    // Store -> DataModel, Parser, Creator, FileSystem
    // Parser -> DataModel
    // Creator -> DataModel
    // FileSystem -> none
    constexpr std::array<Row, 7> kDependencyMatrix = {
        {
            /* View */ Row{false, true, false, false, false, false, false},
            /* Controller */ Row{false, false, true, true, true, true, false},
            /* DataModel */ Row{false, false, false, false, false, false, false},
            /* Store */ Row{false, false, true, false, true, true, true},
            /* Parser */ Row{false, false, true, false, false, false, false},
            /* Creator */ Row{false, false, true, false, false, false, false},
            /* FileSystem */ Row{false, false, false, false, false, false, false}
        }
    };

    std::atomic_bool g_architecturePolicyLocked{false};
}

namespace ThinkingSpace::Policy
{
    bool ArchitecturePolicyLock::isLocked() noexcept
    {
        return g_architecturePolicyLocked.load(std::memory_order_acquire);
    }

    void ArchitecturePolicyLock::lock() noexcept
    {
        g_architecturePolicyLocked.store(true, std::memory_order_release);
    }

    void ArchitecturePolicyLock::unlockForTests() noexcept
    {
        g_architecturePolicyLocked.store(false, std::memory_order_release);
    }

    const char* layerName(Layer layer) noexcept
    {
        switch (layer)
        {
        case Layer::View:
            return "View";
        case Layer::Controller:
            return "Controller";
        case Layer::DataModel:
            return "DataModel";
        case Layer::Store:
            return "Store";
        case Layer::Parser:
            return "Parser";
        case Layer::Creator:
            return "Creator";
        case Layer::FileSystem:
            return "FileSystem";
        default:
            return "Unknown";
        }
    }

    bool isDependencyAllowed(Layer from, Layer to) noexcept
    {
        const int fromIndex = layerIndex(from);
        const int toIndex = layerIndex(to);
        if (fromIndex < 0 || fromIndex >= static_cast<int>(kDependencyMatrix.size()))
        {
            return false;
        }
        const Row& row = kDependencyMatrix[fromIndex];
        if (toIndex < 0 || toIndex >= static_cast<int>(row.size()))
        {
            return false;
        }
        return row[toIndex];
    }

    bool assertDependencyAllowed(Layer from, Layer to, QString* errorMessage)
    {
        if (isDependencyAllowed(from, to))
        {
            if (errorMessage != nullptr)
            {
                errorMessage->clear();
            }
            return true;
        }

        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("architecture policy violation: %1 -> %2 is not allowed")
                .arg(QString::fromLatin1(layerName(from)),
                     QString::fromLatin1(layerName(to)));
        }
        return false;
    }

    bool verifyDependencyAllowed(
        Layer from,
        Layer to,
        const QString& context,
        QString* errorMessage)
    {
        QString violationMessage;
        if (assertDependencyAllowed(from, to, &violationMessage))
        {
            if (errorMessage != nullptr)
            {
                errorMessage->clear();
            }
            return true;
        }

        const QString formattedMessage = context.trimmed().isEmpty()
            ? violationMessage
            : QStringLiteral("%1 (%2)").arg(violationMessage, context.trimmed());
        qWarning().noquote() << QStringLiteral("[thinkingspace:policy][dependency] %1").arg(formattedMessage);
        if (errorMessage != nullptr)
        {
            *errorMessage = formattedMessage;
        }
        return false;
    }

    bool verifyMutableWiringAllowed(const QString& context, QString* errorMessage)
    {
        if (!ArchitecturePolicyLock::isLocked())
        {
            if (errorMessage != nullptr)
            {
                errorMessage->clear();
            }
            return true;
        }

        const QString trimmedContext = context.trimmed();
        const QString message = trimmedContext.isEmpty()
            ? QStringLiteral("architecture policy is locked")
            : QStringLiteral("architecture policy is locked (%1)").arg(trimmedContext);
        qWarning().noquote() << QStringLiteral("[thinkingspace:policy][lock] %1").arg(message);
        if (errorMessage != nullptr)
        {
            *errorMessage = message;
        }
        return false;
    }

    bool verifyMutableDependencyAllowed(
        Layer from,
        Layer to,
        const QString& context,
        QString* errorMessage)
    {
        if (!verifyMutableWiringAllowed(context, errorMessage))
        {
            return false;
        }
        return verifyDependencyAllowed(from, to, context, errorMessage);
    }
} // namespace ThinkingSpace::Policy
