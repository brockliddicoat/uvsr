#include "settings_snapshot_transaction.h"
#include "settings_snapshot_schema.h"

#include <algorithm>
#include <set>
#include <utility>

namespace uvsr
{
    namespace
    {
        constexpr std::string_view UnavailableValue = "<unavailable>";

        [[nodiscard]] bool IsPlannedValue(
            const UiSettingsCommandDefinition& definition) noexcept
        {
            return definition.applicationRole != SettingsSnapshotApplicationMode::Selector &&
                definition.applicationRole !=
                    SettingsSnapshotApplicationMode::StartupPrecondition;
        }

    }

    bool BuildSettingsSnapshotTransaction(
        const DecodedSettings& decoded,
        std::vector<SettingsSnapshotTransactionEntry>& transaction,
        std::string& error)
    {
        transaction.clear();
        error.clear();
        for (const auto& [name, value] : decoded)
        {
            const auto definition = std::find_if(UiSettingsCommandCatalog.begin(),
                UiSettingsCommandCatalog.end(), [&name](const auto& candidate) {
                    return candidate.name == name && IsSettingsSnapshotValue(candidate);
                });
            if (definition == UiSettingsCommandCatalog.end())
            {
                error = "snapshot contains unknown setting '" + name + "'";
                return false;
            }
        }
        for (const auto& definition : UiSettingsCommandCatalog)
        {
            if (!IsSettingsSnapshotValue(definition))
                continue;
            const auto found = decoded.find(definition.name);
            if (found == decoded.end())
            {
                error = "snapshot is missing required setting '" + std::string(definition.name) + "'";
                transaction.clear();
                return false;
            }
            transaction.push_back({ definition.id, found->second });
        }
        return true;
    }
    std::size_t SettingsSnapshotTransactionCoordinator::FindTransactionEntry(
        SettingId id) const
    {
        const auto& transaction = m_Transaction;
        for (std::size_t index = 0u; index < transaction.size(); ++index)
        {
            if (transaction[index].id == id)
                return index;
        }
        return transaction.size();
    }

    bool SettingsSnapshotTransactionCoordinator::BuildTransactionValueOrder(
        const std::vector<std::string>& desiredValues,
        const std::vector<std::string>& liveValues,
        bool reverseDependencies,
        std::vector<std::size_t>& order,
        std::string& error) const
    {
        const auto& transaction = m_Transaction;
        order.clear();
        error.clear();
        if (desiredValues.size() != transaction.size() ||
            liveValues.size() != transaction.size())
        {
            error = "transaction value planner received incomplete state";
            return false;
        }

        const std::size_t count = transaction.size();
        std::vector<std::vector<std::size_t>> outgoing(count);
        std::vector<std::size_t> indegree(count, 0u);
        std::size_t plannedCount = 0u;
        for (const Entry& entry : transaction)
        {
            if (IsPlannedValue(*entry.definition))
                ++plannedCount;
        }

        const auto addEdge = [&](std::size_t before, std::size_t after)
        {
            if (before == after ||
                std::find(
                    outgoing[before].begin(),
                    outgoing[before].end(),
                    after) != outgoing[before].end())
            {
                return;
            }
            outgoing[before].push_back(after);
            ++indegree[after];
        };

        for (std::size_t child = 0u; child < count; ++child)
        {
            if (!IsPlannedValue(*transaction[child].definition))
                continue;
            const std::array<SettingId, 2u> dependencies = {
                transaction[child].definition->dependsOn,
                transaction[child].definition->valueDependsOn
            };
            for (const SettingId dependency : dependencies)
            {
                const std::size_t parent =
                    FindTransactionEntry(dependency);
                if (parent == count || !IsPlannedValue(*transaction[parent].definition))
                    continue;
                if (reverseDependencies)
                    addEdge(child, parent);
                else
                    addEdge(parent, child);
            }
        }

        for (const auto bounds : { std::pair{ SettingId::LightSelectedInnerAngle, SettingId::LightSelectedOuterAngle },
                std::pair{ SettingId::PathingMinimumBounces, SettingId::PathingMaximumBounces } })
        {
            const std::size_t inner = FindTransactionEntry(bounds.first);
            const std::size_t outer = FindTransactionEntry(bounds.second);
            if (inner != count && outer != count &&
                desiredValues[inner] != UnavailableValue &&
                desiredValues[outer] != UnavailableValue &&
                liveValues[outer] != UnavailableValue)
            {
                float desiredInner = 0.f;
                float desiredOuter = 0.f;
                float liveOuter = 0.f;
                if (!ParseCanonicalSettingsFloat(desiredValues[inner], desiredInner) ||
                    !ParseCanonicalSettingsFloat(desiredValues[outer], desiredOuter) ||
                    !ParseCanonicalSettingsFloat(liveValues[outer], liveOuter))
                {
                    error = "range planner received a noncanonical value";
                    return false;
                }
                if (desiredInner > desiredOuter)
                {
                    error = bounds.first == SettingId::PathingMinimumBounces
                        ? "minimum bounces must not exceed maximum bounces"
                        : "the requested inner spot angle exceeds the outer angle";
                    return false;
                }
                if (desiredInner > liveOuter)
                    addEdge(outer, inner);
                else
                    addEdge(inner, outer);
            }
        }

        std::vector<bool> emitted(count, false);
        order.reserve(plannedCount);
        while (order.size() != plannedCount)
        {
            std::size_t best = count;
            for (std::size_t index = 0u; index < count; ++index)
            {
                if (!IsPlannedValue(*transaction[index].definition) || emitted[index] ||
                    indegree[index] != 0u)
                {
                    continue;
                }
                if (best == count ||
                    transaction[index].definition->bindingIndex <
                        transaction[best].definition->bindingIndex)
                {
                    best = index;
                }
            }
            if (best == count)
            {
                error = "settings value dependency graph contains a cycle";
                order.clear();
                return false;
            }
            emitted[best] = true;
            order.push_back(best);
            for (const std::size_t dependent : outgoing[best])
                --indegree[dependent];
        }
        return true;
    }

    bool SettingsSnapshotTransactionCoordinator::ValidateRequestedSpotPair(
        std::string& error) const
    {
        const auto& transaction = m_Transaction;
        const std::size_t inner = FindTransactionEntry(SettingId::LightSelectedInnerAngle);
        const std::size_t outer = FindTransactionEntry(SettingId::LightSelectedOuterAngle);
        if (inner == transaction.size() || outer == transaction.size() ||
            transaction[inner].requestedValue == UnavailableValue ||
            transaction[outer].requestedValue == UnavailableValue)
        {
            return true;
        }
        float innerAngle = 0.f;
        float outerAngle = 0.f;
        if (!ParseCanonicalSettingsFloat(
                transaction[inner].requestedValue, innerAngle) ||
            !ParseCanonicalSettingsFloat(
                transaction[outer].requestedValue, outerAngle))
        {
            return true;
        }
        if (innerAngle <= outerAngle)
            return true;
        error = "the requested inner spot angle exceeds the outer angle";
        return false;
    }

    bool SettingsSnapshotTransactionCoordinator::IsActive() const noexcept
    {
        return m_Phase != Phase::Idle && m_Phase != Phase::Succeeded &&
            m_Phase != Phase::Failed;
    }

    void SettingsSnapshotTransactionCoordinator::Reset() noexcept
    {
        m_Transaction.clear();
        m_SourceVisibleValues.clear();
        m_SourceRawValues.clear();
        m_TargetRawValues.clear();
        m_ValueOrder.clear();
        m_PrerequisiteApplied.clear();
        m_Mutated.clear();
        m_Phase = Phase::Idle;
        m_Cursor = 0u;
        m_SelectorRequestIssued = false;
        m_MutationStarted = false;
        m_Result = {};
    }

    SettingsSnapshotTransactionStep SettingsSnapshotTransactionCoordinator::Begin(
        const std::vector<SettingsSnapshotTransactionEntry>& requests,
        const SettingsSnapshotStagedRuntimeAccess& access)
    {
        Reset();
        const auto reject = [this](SettingsSnapshotTransactionFailureStage stage, std::string error) {
            m_Result.failureStage = stage;
            m_Result.error = std::move(error);
            m_Phase = Phase::Failed;
            return SettingsSnapshotTransactionStep{ SettingsSnapshotTransactionProgress::Failed, m_Result, {} };
        };
        if (!access.validateValue || !access.readValue || !access.readRawValue || !access.writeValue)
            return reject(SettingsSnapshotTransactionFailureStage::Configuration,
                "staged settings transaction has no validator, visible reader, raw reader, or writer");

        std::set<SettingId> ids;
        m_Transaction.reserve(requests.size());
        for (const auto& request : requests)
        {
            const auto definition = std::find_if(UiSettingsCommandCatalog.begin(),
                UiSettingsCommandCatalog.end(), [&request](const auto& candidate) {
                    return candidate.id == request.id && IsSettingsSnapshotValue(candidate);
                });
            if (definition == UiSettingsCommandCatalog.end() || !ids.insert(request.id).second)
                return reject(SettingsSnapshotTransactionFailureStage::Configuration,
                    "staged settings transaction has an unknown, nonpersistent, or duplicate entry");
            m_Transaction.push_back({ request, &*definition });
        }
        const auto requestedDependencySelector = [this](const Entry& entry) -> std::string_view {
            SettingId dependency = entry.definition->dependsOn;
            for (std::size_t depth = 0u; dependency != SettingId::Invalid && depth < m_Transaction.size(); ++depth)
            {
                const std::size_t index = FindTransactionEntry(dependency);
                if (index == m_Transaction.size())
                    break;
                const auto& ancestor = m_Transaction[index];
                if (ancestor.definition->applicationRole == SettingsSnapshotApplicationMode::Selector)
                    return ancestor.requestedValue;
                dependency = ancestor.definition->dependsOn;
            }
            return {};
        };
        for (const Entry& entry : m_Transaction)
        {
            const auto mode = entry.definition->applicationRole;
            const bool selector = mode == SettingsSnapshotApplicationMode::Selector ||
                mode == SettingsSnapshotApplicationMode::StartupPrecondition;
            const std::string name(entry.definition->name);
            if (selector && entry.requestedValue == UnavailableValue)
                return reject(SettingsSnapshotTransactionFailureStage::Preflight,
                    "setting '" + name + "' cannot use the unavailable sentinel");
            std::string error;
            const bool valid = selector
                ? ValidateSettingsSnapshotSelectorToken(entry.id, entry.requestedValue, error)
                : entry.requestedValue == UnavailableValue || access.validateValue(
                    entry.id, entry.requestedValue, requestedDependencySelector(entry), error);
            if (!valid)
                return reject(SettingsSnapshotTransactionFailureStage::Preflight,
                    "snapshot preflight rejected '" + name + "': " +
                    (error.empty() ? "no reason was reported" : error));
        }
        std::string coupledError;
        if (!ValidateRequestedSpotPair(coupledError))
            return reject(SettingsSnapshotTransactionFailureStage::Preflight,
                "snapshot preflight rejected coupled state: " + coupledError);

        for (const Entry& entry : m_Transaction)
        {
            std::string visible;
            std::string raw;
            std::string error;
            const std::string name(entry.definition->name);
            if (!access.readValue(entry.id, visible, error) || !access.readRawValue(entry.id, raw, error))
                return reject(SettingsSnapshotTransactionFailureStage::Capture,
                    "could not capture '" + name + "': " + (error.empty() ? "no reason was reported" : error));
            const auto mode = entry.definition->applicationRole;
            const bool selector = mode == SettingsSnapshotApplicationMode::Selector ||
                mode == SettingsSnapshotApplicationMode::StartupPrecondition;
            if (selector && visible == UnavailableValue)
                return reject(SettingsSnapshotTransactionFailureStage::Capture,
                    "required setting '" + name + "' is unavailable");
            if (selector && !ValidateSettingsSnapshotSelectorToken(entry.id, visible, error))
                return reject(SettingsSnapshotTransactionFailureStage::Capture,
                    "selector GET returned a noncanonical token for '" + name + "': " + error);
            if (!selector && raw != UnavailableValue && !access.validateValue(entry.id, raw, {}, error))
                return reject(SettingsSnapshotTransactionFailureStage::Capture,
                    "captured raw value for '" + name + "' is not canonical: " +
                    (error.empty() ? "no reason was reported" : error));
            if (mode == SettingsSnapshotApplicationMode::StartupPrecondition && visible != entry.requestedValue)
                return reject(SettingsSnapshotTransactionFailureStage::Preflight,
                    "startup precondition '" + name + "' does not match the active value; "
                    "select the requested adapter with -adapter before loading this snapshot");
            m_SourceVisibleValues.push_back(std::move(visible));
            m_SourceRawValues.push_back(std::move(raw));
        }
        m_TargetRawValues.resize(m_Transaction.size());
        m_PrerequisiteApplied.assign(m_Transaction.size(), false);
        m_Mutated.assign(m_Transaction.size(), false);
        m_Phase = Phase::ApplyScene;
        return Advance(access);
    }
    SettingsSnapshotTransactionStep
    SettingsSnapshotTransactionCoordinator::Advance(
        const SettingsSnapshotStagedRuntimeAccess& access)
    {
        const auto step = [this](
            SettingsSnapshotTransactionProgress progress,
            std::string waitingFor = {})
        {
            return SettingsSnapshotTransactionStep{
                progress, m_Result, std::move(waitingFor) };
        };
        if (m_Phase == Phase::Succeeded)
            return step(SettingsSnapshotTransactionProgress::Succeeded);
        if (m_Phase == Phase::Failed || m_Phase == Phase::Idle)
            return step(SettingsSnapshotTransactionProgress::Failed);
        if (!access.validateValue || !access.readValue ||
            !access.readRawValue || !access.writeValue)
        {
            m_Result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            m_Result.error = "staged settings transaction lost runtime access";
            m_Phase = Phase::Failed;
            return step(SettingsSnapshotTransactionProgress::Failed);
        }

        const auto findEntry = [this](SettingId id)
        {
            return FindTransactionEntry(id);
        };
        const auto hasMutatedValue = [this]()
        {
            for (std::size_t index = 0u; index < m_Transaction.size(); ++index)
            {
                if (m_Mutated[index] && IsPlannedValue(*m_Transaction[index].definition))
                    return true;
            }
            return false;
        };
        const auto startFailure = [this](
            SettingsSnapshotTransactionFailureStage stage,
            std::string error)
        {
            m_Result.failureStage = stage;
            m_Result.error = std::move(error);
            if (!m_MutationStarted)
            {
                m_Phase = Phase::Failed;
                return;
            }
            m_Result.rollbackAttempted = true;
            m_Phase = Phase::RollbackTargetValues;
            m_Cursor = 0u;
            m_ValueOrder.clear();
            m_SelectorRequestIssued = false;
        };
        const auto rollbackFailure = [this](std::string error)
        {
            m_Result.rollbackAttempted = true;
            m_Result.rollbackSucceeded = false;
            m_Result.error += "; rollback failed: " + std::move(error);
            m_Phase = Phase::Failed;
        };
        const auto readVisible = [&access](
            const Entry& entry,
            std::string& value,
            std::string& error)
        {
            error.clear();
            return access.readValue(entry.id, value, error);
        };
        const auto readRaw = [&access](
            const Entry& entry,
            std::string& value,
            std::string& error)
        {
            error.clear();
            return access.readRawValue(entry.id, value, error);
        };
        const auto writeRawIfDifferent = [&access, &readRaw](
            const Entry& entry,
            std::string_view desired,
            std::string& error)
        {
            std::string current;
            if (!readRaw(entry, current, error))
                return false;
            if (current == desired)
                return true;
            if (!access.writeValue(entry.id, desired, error))
                return false;
            current.clear();
            if (!readRaw(entry, current, error))
                return false;
            if (current != desired)
            {
                error = "readback mismatch for '" + std::string(entry.definition->name) + "'";
                return false;
            }
            return true;
        };
        const auto applyValue = [this, &access, &readRaw, &startFailure](
            std::size_t index)
        {
            const Entry& entry =
                m_Transaction[index];
            std::string current;
            std::string operationError;
            if (!readRaw(entry, current, operationError))
            {
                startFailure(
                    SettingsSnapshotTransactionFailureStage::Apply,
                    "could not read '" + std::string(entry.definition->name) +
                        "' before applying it: " + operationError);
                return false;
            }
            if (current == entry.requestedValue)
                return true;

            m_MutationStarted = true;
            m_Mutated[index] = true;
            if (!access.writeValue(
                    entry.id, entry.requestedValue, operationError))
            {
                startFailure(
                    SettingsSnapshotTransactionFailureStage::Apply,
                    "could not apply '" + std::string(entry.definition->name) + "': " +
                        (operationError.empty()
                            ? "no reason was reported"
                            : operationError));
                return false;
            }
            current.clear();
            operationError.clear();
            if (!readRaw(entry, current, operationError) ||
                current != entry.requestedValue)
            {
                startFailure(
                    SettingsSnapshotTransactionFailureStage::Readback,
                    "applied value mismatch for '" + std::string(entry.definition->name) + "'" +
                        (operationError.empty()
                            ? std::string{}
                            : ": " + operationError));
                return false;
            }
            ++m_Result.changedValueCount;
            return true;
        };

        for (;;)
        {
            const auto advanceSelector = [&](
                SettingId id,
                std::string_view desired,
                bool rollback,
                Phase nextPhase)
                -> SettingsSnapshotTransactionStep
            {
                const std::size_t index = findEntry(id);
                if (index == m_Transaction.size())
                {
                    m_Phase = nextPhase;
                    m_Cursor = 0u;
                    m_SelectorRequestIssued = false;
                    return step(SettingsSnapshotTransactionProgress::Succeeded);
                }

                const Entry& entry =
                    m_Transaction[index];
                std::string current;
                std::string operationError;
                if (!readVisible(entry, current, operationError))
                {
                    if (rollback)
                        rollbackFailure("could not read selector '" +
                            std::string(entry.definition->name) + "': " + operationError);
                    else
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Selector,
                            "could not read selector '" + std::string(entry.definition->name) +
                                "': " + operationError);
                    return step(SettingsSnapshotTransactionProgress::Failed);
                }
                if (!m_SelectorRequestIssued && current == desired)
                {
                    m_Phase = nextPhase;
                    m_Cursor = 0u;
                    return step(SettingsSnapshotTransactionProgress::Succeeded);
                }
                if (!access.driveSelector)
                {
                    if (rollback)
                        rollbackFailure("no selector driver can restore '" +
                            std::string(entry.definition->name) + "'");
                    else
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Selector,
                            "no selector driver can apply '" + std::string(entry.definition->name) + "'");
                    return step(SettingsSnapshotTransactionProgress::Failed);
                }

                const bool begin = !m_SelectorRequestIssued;
                if (begin && !rollback)
                {
                    m_MutationStarted = true;
                    m_Mutated[index] = true;
                }
                const SettingsSnapshotSelectorTransition transition =
                    access.driveSelector(
                        id, desired, begin, rollback, operationError);
                if (transition == SettingsSnapshotSelectorTransition::Failed)
                {
                    if (rollback)
                        rollbackFailure("selector rollback failed for '" +
                            std::string(entry.definition->name) + "': " + operationError);
                    else
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Selector,
                            "selector transition failed for '" + std::string(entry.definition->name) +
                                "': " + operationError);
                    return step(SettingsSnapshotTransactionProgress::Failed);
                }

                m_SelectorRequestIssued = true;
                if (!rollback && begin)
                    ++m_Result.changedValueCount;
                if (transition == SettingsSnapshotSelectorTransition::Pending)
                {
                    return step(
                        SettingsSnapshotTransactionProgress::Pending,
                        std::string(entry.definition->name));
                }

                current.clear();
                operationError.clear();
                if (!readVisible(entry, current, operationError) ||
                    current != desired)
                {
                    const std::string failure = operationError.empty()
                        ? "selector reported ready without publishing its token"
                        : operationError;
                    if (rollback)
                        rollbackFailure("selector rollback readback failed for '" +
                            std::string(entry.definition->name) + "': " + failure);
                    else
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Readback,
                            "selector readback failed for '" + std::string(entry.definition->name) +
                                "': " + failure);
                    return step(SettingsSnapshotTransactionProgress::Failed);
                }
                m_Phase = nextPhase;
                m_Cursor = 0u;
                m_SelectorRequestIssued = false;
                return step(SettingsSnapshotTransactionProgress::Succeeded);
            };

            switch (m_Phase)
            {
            case Phase::ApplyScene:
            case Phase::ApplyLight:
            case Phase::ApplyMaterial:
            case Phase::RollbackScene:
            case Phase::RollbackLight:
            case Phase::RollbackMaterial:
            {
                const bool rollback = m_Phase == Phase::RollbackScene ||
                    m_Phase == Phase::RollbackLight || m_Phase == Phase::RollbackMaterial;
                SettingId id;
                Phase next;
                if (m_Phase == Phase::ApplyScene || m_Phase == Phase::RollbackScene)
                {
                    id = SettingId::SceneCurrent;
                    next = rollback ? Phase::RollbackLight : Phase::ApplyLight;
                }
                else if (m_Phase == Phase::ApplyLight || m_Phase == Phase::RollbackLight)
                {
                    id = SettingId::LightSelected;
                    next = rollback ? Phase::RollbackMaterial : Phase::ApplyMaterial;
                }
                else
                {
                    id = SettingId::MaterialSelected;
                    next = rollback ? Phase::RollbackSourceValues : Phase::CaptureTarget;
                }
                const std::size_t index = findEntry(id);
                const std::string_view desired = index == m_Transaction.size() ? std::string_view{} :
                    rollback ? m_SourceVisibleValues[index] : m_Transaction[index].requestedValue;
                const auto selectorStep = advanceSelector(id, desired, rollback, next);
                if (m_Phase == next || (!rollback && m_Phase == Phase::RollbackTargetValues))
                    continue;
                return selectorStep;
            }

            case Phase::CaptureTarget:
            {
                for (; m_Cursor < m_Transaction.size(); ++m_Cursor)
                {
                    const Entry& entry =
                        m_Transaction[m_Cursor];
                    std::string rawValue;
                    std::string operationError;
                    if (!readRaw(entry, rawValue, operationError))
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Capture,
                            "could not capture target raw value '" +
                                std::string(entry.definition->name) + "': " + operationError);
                        break;
                    }
                    if (IsPlannedValue(*entry.definition) &&
                        rawValue != UnavailableValue &&
                        !access.validateValue(
                            entry.id, rawValue, {}, operationError))
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Capture,
                            "target raw value for '" + std::string(entry.definition->name) +
                                "' is not canonical: " + operationError);
                        break;
                    }
                    m_TargetRawValues[m_Cursor] = std::move(rawValue);
                }
                if (m_Phase == Phase::RollbackTargetValues ||
                    m_Phase == Phase::Failed)
                {
                    continue;
                }

                std::vector<std::string> requestedValues;
                requestedValues.reserve(m_Transaction.size());
                for (const Entry& entry :
                    m_Transaction)
                {
                    requestedValues.push_back(entry.requestedValue);
                }
                std::string planError;
                if (!BuildTransactionValueOrder(requestedValues,
                        m_TargetRawValues,
                        false,
                        m_ValueOrder,
                        planError))
                {
                    startFailure(
                        SettingsSnapshotTransactionFailureStage::Preflight,
                        "could not plan settings values: " + planError);
                    continue;
                }
                m_Cursor = 0u;
                m_Phase = Phase::ApplyPrerequisites;
                continue;
            }
            case Phase::ApplyPrerequisites:
            {
                for (; m_Cursor < m_ValueOrder.size(); ++m_Cursor)
                {
                    const std::size_t index = m_ValueOrder[m_Cursor];
                    const SettingId id = m_Transaction[index].id;
                    const bool prerequisite = std::any_of(
                        m_Transaction.begin(),
                        m_Transaction.end(),
                        [id](const Entry& entry)
                        {
                            return entry.definition->valueDependsOn == id;
                        });
                    if (!prerequisite)
                        continue;
                    m_PrerequisiteApplied[index] = true;
                    if (m_Transaction[index].requestedValue ==
                        UnavailableValue)
                    {
                        continue;
                    }
                    if (!applyValue(index))
                        break;
                }
                if (m_Phase == Phase::RollbackTargetValues ||
                    m_Phase == Phase::Failed)
                {
                    continue;
                }
                m_Cursor = 0u;
                m_Phase = Phase::ValidateTarget;
                continue;
            }
            case Phase::ValidateTarget:
            {
                for (; m_Cursor < m_Transaction.size(); ++m_Cursor)
                {
                    const Entry& entry =
                        m_Transaction[m_Cursor];
                    if (!IsPlannedValue(*entry.definition))
                        continue;

                    std::string operationError;
                    if (entry.requestedValue != UnavailableValue &&
                        !access.validateValue(
                            entry.id,
                            entry.requestedValue,
                            {},
                            operationError))
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Preflight,
                            "target context rejected '" + std::string(entry.definition->name) +
                                "': " + operationError);
                        break;
                    }

                    std::string visibleValue;
                    if (!readVisible(entry, visibleValue, operationError))
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Capture,
                            "could not read target availability for '" +
                                std::string(entry.definition->name) + "': " + operationError);
                        break;
                    }
                    if ((entry.requestedValue == UnavailableValue) !=
                        (visibleValue == UnavailableValue))
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Preflight,
                            "target object availability does not match '" +
                                std::string(entry.definition->name) + "'");
                        break;
                    }
                }
                if (m_Phase == Phase::RollbackTargetValues ||
                    m_Phase == Phase::Failed)
                {
                    continue;
                }
                m_Cursor = 0u;
                m_Phase = Phase::ApplyValues;
                continue;
            }
            case Phase::ApplyValues:
            {
                for (; m_Cursor < m_ValueOrder.size(); ++m_Cursor)
                {
                    const std::size_t index = m_ValueOrder[m_Cursor];
                    if (m_PrerequisiteApplied[index] ||
                        m_Transaction[index].requestedValue ==
                            UnavailableValue)
                    {
                        continue;
                    }
                    if (!applyValue(index))
                        break;
                }
                if (m_Phase == Phase::RollbackTargetValues ||
                    m_Phase == Phase::Failed)
                {
                    continue;
                }
                m_Cursor = 0u;
                m_Phase = Phase::Verify;
                continue;
            }
            case Phase::Verify:
            {
                for (; m_Cursor < m_Transaction.size(); ++m_Cursor)
                {
                    const Entry& entry =
                        m_Transaction[m_Cursor];
                    std::string current;
                    std::string operationError;
                    if (!readVisible(entry, current, operationError) ||
                        current != entry.requestedValue)
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Readback,
                            "applied value mismatch for '" + std::string(entry.definition->name) +
                                "'" + (operationError.empty()
                                    ? std::string{}
                                    : ": " + operationError));
                        break;
                    }
                }
                if (m_Phase == Phase::RollbackTargetValues ||
                    m_Phase == Phase::Failed)
                {
                    continue;
                }
                m_Result.succeeded = true;
                m_Result.failureStage =
                    SettingsSnapshotTransactionFailureStage::None;
                m_Phase = Phase::Succeeded;
                return step(SettingsSnapshotTransactionProgress::Succeeded);
            }
            case Phase::RollbackTargetValues:
            case Phase::RollbackSourceValues:
            {
                const bool target = m_Phase == Phase::RollbackTargetValues;
                const Phase next = target ? Phase::RollbackScene : Phase::RollbackVerify;
                if (target && !hasMutatedValue())
                {
                    m_Phase = next;
                    m_Cursor = 0u;
                    continue;
                }
                const auto& baseline = target ? m_TargetRawValues : m_SourceRawValues;
                const std::string owner = target ? "target" : "source";
                if (m_ValueOrder.empty())
                {
                    std::vector<std::string> currentRaw(m_Transaction.size());
                    std::string error;
                    for (std::size_t index = 0u; index < m_Transaction.size(); ++index)
                    {
                        if (!readRaw(m_Transaction[index], currentRaw[index], error))
                        {
                            rollbackFailure("could not capture " + owner + " restore state: " + error);
                            break;
                        }
                    }
                    if (m_Phase == Phase::Failed)
                        continue;
                    if (!BuildTransactionValueOrder(baseline, currentRaw, target, m_ValueOrder, error))
                    {
                        rollbackFailure("could not plan " + owner + " restore: " + error);
                        continue;
                    }
                    m_Cursor = 0u;
                }
                for (; m_Cursor < m_ValueOrder.size(); ++m_Cursor)
                {
                    const std::size_t index = m_ValueOrder[m_Cursor];
                    if ((target && !m_Mutated[index]) || baseline[index] == UnavailableValue)
                        continue;
                    std::string error;
                    if (!writeRawIfDifferent(m_Transaction[index], baseline[index], error))
                    {
                        rollbackFailure("could not restore " + owner + " raw value '" +
                            std::string(m_Transaction[index].definition->name) + "': " + error);
                        break;
                    }
                }
                if (m_Phase == Phase::Failed)
                    continue;
                m_ValueOrder.clear();
                m_Cursor = 0u;
                m_Phase = next;
                continue;
            }

            case Phase::RollbackVerify:
            {
                for (; m_Cursor < m_Transaction.size(); ++m_Cursor)
                {
                    std::string rawValue;
                    std::string visibleValue;
                    std::string operationError;
                    if (!readRaw(
                            m_Transaction[m_Cursor],
                            rawValue,
                            operationError) ||
                        rawValue != m_SourceRawValues[m_Cursor] ||
                        !readVisible(
                            m_Transaction[m_Cursor],
                            visibleValue,
                            operationError) ||
                        visibleValue != m_SourceVisibleValues[m_Cursor])
                    {
                        rollbackFailure(
                            "source-state verification failed for '" +
                            std::string(m_Transaction[m_Cursor].definition->name) + "'");
                        break;
                    }
                }
                if (m_Phase == Phase::Failed &&
                    !m_Result.rollbackSucceeded)
                {
                    return step(SettingsSnapshotTransactionProgress::Failed);
                }
                m_Result.rollbackSucceeded = true;
                m_Result.succeeded = false;
                m_Phase = Phase::Failed;
                return step(SettingsSnapshotTransactionProgress::Failed);
            }
            case Phase::Succeeded:
                return step(SettingsSnapshotTransactionProgress::Succeeded);
            case Phase::Failed:
            case Phase::Idle:
                return step(SettingsSnapshotTransactionProgress::Failed);
            }
        }
    }

}
