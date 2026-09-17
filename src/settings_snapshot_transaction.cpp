#include "settings_snapshot_transaction.h"
#include "settings_snapshot_schema.h"

#include <algorithm>
#include <utility>
#include <initializer_list>
#include <new>

namespace uvsr
{
    namespace
    {
        constexpr std::string_view UnavailableValue = "<unavailable>";

        struct OperationScope
        {
            bool& active;
            explicit OperationScope(bool& flag) noexcept : active(flag) { active = true; }
            ~OperationScope() noexcept { active = false; }
        };

#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
        thread_local std::size_t stateAllocationsBeforeFailure = SIZE_MAX;
#endif
        bool MayAllocateState() noexcept
        {
#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
            if (stateAllocationsBeforeFailure == 0) return false;
            if (stateAllocationsBeforeFailure != SIZE_MAX) --stateAllocationsBeforeFailure;
#endif
            return true;
        }

        SettingsSnapshotError Failure(const char* message,
            SettingsSnapshotErrorCode code = SettingsSnapshotErrorCode::InvalidInput) noexcept
        {
            return {code, 0, 0, message, {}};
        }

        SettingsSnapshotError Failure(std::initializer_list<std::string_view> parts,
            const SettingsSnapshotError* cause = nullptr) noexcept
        {
            return ComposeSettingsSnapshotError(parts,
                cause && cause->code != SettingsSnapshotErrorCode::None
                    ? cause->code : SettingsSnapshotErrorCode::InvalidInput,
                cause ? cause->nativeCode : 0, cause ? cause->cleanupCode : 0);
        }

        std::string_view Reason(const SettingsSnapshotError& error) noexcept
        {
            return error.MessageView().empty() ? "no reason was reported" : error.MessageView();
        }

        [[nodiscard]] bool IsPlannedValue(
            const UiSettingsCommandDefinition& definition) noexcept
        {
            return definition.applicationRole != SettingsSnapshotApplicationMode::Selector &&
                definition.applicationRole !=
                    SettingsSnapshotApplicationMode::StartupPrecondition;
        }

    }

    struct SettingsSnapshotTransactionCoordinator::Entry
    {
        SettingId id = SettingId::Invalid;
        SettingsSnapshotText requestedValue;
        const UiSettingsCommandDefinition* definition = nullptr;
    };

    struct SettingsSnapshotTransactionCoordinator::State
    {
        Entry entries[TransactionCapacity];
        std::size_t count = 0;
        SettingsSnapshotText sourceVisible[TransactionCapacity];
        SettingsSnapshotText sourceRaw[TransactionCapacity];
        SettingsSnapshotText targetRaw[TransactionCapacity];
        std::size_t valueOrder[TransactionCapacity]{};
        std::size_t valueOrderCount = 0;
        bool prerequisiteApplied[TransactionCapacity]{};
        bool mutated[TransactionCapacity]{};
        std::size_t cursor = 0;
        bool selectorRequestIssued = false;
        bool mutationStarted = false;
        SettingsSnapshotStagedRuntimeAccess access;
    };

    bool BuildSettingsSnapshotTransaction(const DecodedSettings& decoded,
        ArrayView<SettingsSnapshotTransactionEntry> transaction,
        std::size_t& count, SettingsSnapshotError& error) noexcept
    {
        count = 0;
        error = {};
        if (!transaction.IsValid())
        {
            error = Failure("invalid snapshot transaction output");
            return false;
        }
        for (std::size_t index = 0; index < decoded.Count(); ++index)
        {
            const auto name = decoded.Entries()[index].name;
            const auto definition = std::find_if(UiSettingsCommandCatalog.begin(),
                UiSettingsCommandCatalog.end(), [name](const auto& candidate) {
                    return candidate.name == name && IsSettingsSnapshotValue(candidate);
                });
            if (definition == UiSettingsCommandCatalog.end())
            {
                error = Failure({"snapshot contains unknown setting '", name, "'"});
                return false;
            }
        }
        std::size_t required = 0;
        for (const auto& definition : UiSettingsCommandCatalog)
        {
            if (!IsSettingsSnapshotValue(definition)) continue;
            if (!decoded.Find(definition.name))
            {
                error = Failure({"snapshot is missing required setting '", definition.name, "'"});
                return false;
            }
            ++required;
        }
        if (required > transaction.count)
        {
            error = Failure("snapshot transaction output is too small", SettingsSnapshotErrorCode::Capacity);
            return false;
        }
        for (const auto& definition : UiSettingsCommandCatalog)
            if (IsSettingsSnapshotValue(definition))
                transaction.data[count++] = {definition.id, decoded.Find(definition.name)->value};
        return true;
    }

    std::size_t SettingsSnapshotTransactionCoordinator::FindTransactionEntry(
        SettingId id) const noexcept
    {
        const auto& transaction = m_State->entries;
        for (std::size_t index = 0u; index < m_State->count; ++index)
        {
            if (transaction[index].id == id)
                return index;
        }
        return m_State->count;
    }

    bool SettingsSnapshotTransactionCoordinator::BuildTransactionValueOrder(
        ArrayView<const std::string_view> desiredValues,
        ArrayView<const std::string_view> liveValues,
        bool reverseDependencies,
        SettingsSnapshotError& error) noexcept
    {
        const auto& transaction = m_State->entries;
        m_State->valueOrderCount = 0u;
        error = {};
        if (!desiredValues.IsValid() || !liveValues.IsValid() ||
            desiredValues.count != m_State->count ||
            liveValues.count != m_State->count ||
            m_State->count > TransactionCapacity)
        {
            error = Failure("transaction value planner received incomplete state");
            return false;
        }

        const std::size_t count = m_State->count;
        struct Edge { std::size_t before, after; };
        // each setting has two catalog dependencies; two range pairs add one each.
        Edge edges[2u * TransactionCapacity + 2u]{};
        std::size_t edgeCount = 0u;
        std::size_t indegree[TransactionCapacity]{};
        std::size_t plannedCount = 0u;
        for (std::size_t index = 0; index < m_State->count; ++index)
        {
            const Entry& entry = transaction[index];
            if (IsPlannedValue(*entry.definition))
                ++plannedCount;
        }

        const auto addEdge = [&](std::size_t before, std::size_t after)
        {
            if (before == after)
                return;
            for (std::size_t index = 0u; index < edgeCount; ++index)
                if (edges[index].before == before && edges[index].after == after)
                    return;
            edges[edgeCount++] = {before, after};
            ++indegree[after];
        };

        for (std::size_t child = 0u; child < count; ++child)
        {
            if (!IsPlannedValue(*transaction[child].definition))
                continue;
            const SettingId dependencies[] = {
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

        struct Bounds { SettingId inner, outer; };
        const Bounds ranges[] = {
            {SettingId::LightSelectedInnerAngle, SettingId::LightSelectedOuterAngle},
            {SettingId::PathingMinimumBounces, SettingId::PathingMaximumBounces}};
        for (const auto bounds : ranges)
        {
            const std::size_t inner = FindTransactionEntry(bounds.inner);
            const std::size_t outer = FindTransactionEntry(bounds.outer);
            if (inner != count && outer != count &&
                desiredValues.data[inner] != UnavailableValue &&
                desiredValues.data[outer] != UnavailableValue &&
                liveValues.data[outer] != UnavailableValue)
            {
                float desiredInner = 0.f;
                float desiredOuter = 0.f;
                float liveOuter = 0.f;
                SettingsSnapshotError valueError;
                if (!ParseCanonicalSettingsFloat(desiredValues.data[inner], desiredInner, valueError) ||
                    !ParseCanonicalSettingsFloat(desiredValues.data[outer], desiredOuter, valueError) ||
                    !ParseCanonicalSettingsFloat(liveValues.data[outer], liveOuter, valueError))
                {
                    error = valueError.code == SettingsSnapshotErrorCode::OutOfMemory
                        ? std::move(valueError) : Failure("range planner received a noncanonical value");
                    return false;
                }
                if (desiredInner > desiredOuter)
                {
                    error = Failure(bounds.inner == SettingId::PathingMinimumBounces
                        ? "minimum bounces must not exceed maximum bounces"
                        : "the requested inner spot angle exceeds the outer angle");
                    return false;
                }
                if (desiredInner > liveOuter)
                    addEdge(outer, inner);
                else
                    addEdge(inner, outer);
            }
        }

        bool emitted[TransactionCapacity]{};
        while (m_State->valueOrderCount != plannedCount)
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
                error = Failure("settings value dependency graph contains a cycle");
                m_State->valueOrderCount = 0u;
                return false;
            }
            emitted[best] = true;
            m_State->valueOrder[m_State->valueOrderCount++] = best;
            for (std::size_t index = 0u; index < edgeCount; ++index)
                if (edges[index].before == best)
                    --indegree[edges[index].after];
        }
        return true;
    }

    bool SettingsSnapshotTransactionCoordinator::ValidateRequestedSpotPair(
        SettingsSnapshotError& error) const noexcept
    {
        const auto& transaction = m_State->entries;
        const std::size_t inner = FindTransactionEntry(SettingId::LightSelectedInnerAngle);
        const std::size_t outer = FindTransactionEntry(SettingId::LightSelectedOuterAngle);
        if (inner == m_State->count || outer == m_State->count ||
            transaction[inner].requestedValue.View() == UnavailableValue ||
            transaction[outer].requestedValue.View() == UnavailableValue)
        {
            return true;
        }
        float innerAngle = 0.f;
        float outerAngle = 0.f;
        SettingsSnapshotError valueError;
        if (!ParseCanonicalSettingsFloat(
                transaction[inner].requestedValue.View(), innerAngle, valueError) ||
            !ParseCanonicalSettingsFloat(
                transaction[outer].requestedValue.View(), outerAngle, valueError))
        {
            if (valueError.code == SettingsSnapshotErrorCode::OutOfMemory)
            {
                error = std::move(valueError);
                return false;
            }
            return true;
        }
        if (innerAngle <= outerAngle)
            return true;
        error = Failure("the requested inner spot angle exceeds the outer angle");
        return false;
    }

    bool SettingsSnapshotTransactionCoordinator::IsActive() const noexcept
    {
        return m_InOperation || (m_Phase != Phase::Idle && m_Phase != Phase::Succeeded &&
            m_Phase != Phase::Failed);
    }

    SettingsSnapshotTransactionCoordinator::~SettingsSnapshotTransactionCoordinator() noexcept
    {
        delete m_State;
    }

    void SettingsSnapshotTransactionCoordinator::Reset() noexcept
    {
        if (IsActive()) return;
        delete m_State;
        m_State = nullptr;
        m_Phase = Phase::Idle;
        m_Result = {};
    }

    SettingsSnapshotTransactionStep SettingsSnapshotTransactionCoordinator::Step(
        SettingsSnapshotTransactionProgress progress, std::string_view waitingFor) const noexcept
    {
        SettingsSnapshotTransactionStep step;
        step.progress = progress;
        step.waitingFor = waitingFor;
        step.result.succeeded = m_Result.succeeded;
        step.result.rollbackAttempted = m_Result.rollbackAttempted;
        step.result.rollbackSucceeded = m_Result.rollbackSucceeded;
        step.result.changedValueCount = m_Result.changedValueCount;
        step.result.failureStage = m_Result.failureStage;
        SettingsSnapshotError copyError;
        if (!m_Result.error.CloneTo(step.result.error, copyError))
        {
            step.result.error = Failure("transaction failed; diagnostic copy could not be allocated", copyError.code);
            step.result.error.nativeCode = m_Result.error.nativeCode;
            step.result.error.cleanupCode = m_Result.error.cleanupCode;
        }
        return step;
    }

    SettingsSnapshotTransactionStep SettingsSnapshotTransactionCoordinator::Begin(
        ArrayView<const SettingsSnapshotTransactionEntry> requests,
        const SettingsSnapshotStagedRuntimeAccess& access) noexcept
    {
        if (IsActive())
        {
            SettingsSnapshotTransactionStep rejected;
            rejected.result.failureStage = SettingsSnapshotTransactionFailureStage::Configuration;
            rejected.result.error = Failure("a staged settings transaction is already active");
            return rejected;
        }
        Reset();
        OperationScope operation(m_InOperation);
        const auto reject = [this](SettingsSnapshotTransactionFailureStage stage, SettingsSnapshotError error) {
            m_Result.failureStage = stage;
            m_Result.error = std::move(error);
            m_Phase = Phase::Failed;
            return Step(SettingsSnapshotTransactionProgress::Failed);
        };
        if (!access.validateValue || !access.readValue || !access.readRawValue || !access.writeValue)
            return reject(SettingsSnapshotTransactionFailureStage::Configuration,
                Failure("staged settings transaction has no validator, visible reader, raw reader, or writer"));
        if (!requests.IsValid() || requests.count > TransactionCapacity)
            return reject(SettingsSnapshotTransactionFailureStage::Configuration,
                Failure("staged settings transaction has an unknown, nonpersistent, or duplicate entry"));
        m_State = MayAllocateState() ? new (std::nothrow) State{} : nullptr;
        if (!m_State)
            return reject(SettingsSnapshotTransactionFailureStage::Configuration,
                Failure("cannot allocate settings transaction state", SettingsSnapshotErrorCode::OutOfMemory));
        m_State->access = access;
        for (std::size_t index = 0; index < requests.count; ++index)
        {
            const auto& request = requests.data[index];
            const auto definition = std::find_if(UiSettingsCommandCatalog.begin(),
                UiSettingsCommandCatalog.end(), [&request](const auto& candidate) {
                    return candidate.id == request.id && IsSettingsSnapshotValue(candidate);
                });
            if (definition == UiSettingsCommandCatalog.end() ||
                FindTransactionEntry(request.id) != m_State->count)
                return reject(SettingsSnapshotTransactionFailureStage::Configuration,
                    Failure("staged settings transaction has an unknown, nonpersistent, or duplicate entry"));
            Entry& entry = m_State->entries[m_State->count];
            SettingsSnapshotError error;
            if (!entry.requestedValue.Assign(request.requestedValue, error))
                return reject(SettingsSnapshotTransactionFailureStage::Configuration, std::move(error));
            entry.id = request.id;
            entry.definition = &*definition;
            ++m_State->count;
        }
        const auto requestedDependencySelector = [this](const Entry& entry) -> std::string_view {
            SettingId dependency = entry.definition->dependsOn;
            for (std::size_t depth = 0; dependency != SettingId::Invalid && depth < m_State->count; ++depth)
            {
                const std::size_t index = FindTransactionEntry(dependency);
                if (index == m_State->count) break;
                const auto& ancestor = m_State->entries[index];
                if (ancestor.definition->applicationRole == SettingsSnapshotApplicationMode::Selector)
                    return ancestor.requestedValue.View();
                dependency = ancestor.definition->dependsOn;
            }
            return {};
        };
        for (std::size_t index = 0; index < m_State->count; ++index)
        {
            const auto& entry = m_State->entries[index];
            const auto mode = entry.definition->applicationRole;
            const bool selector = mode == SettingsSnapshotApplicationMode::Selector ||
                mode == SettingsSnapshotApplicationMode::StartupPrecondition;
            const auto name = entry.definition->name;
            const auto requested = entry.requestedValue.View();
            if (selector && requested == UnavailableValue)
                return reject(SettingsSnapshotTransactionFailureStage::Preflight,
                    Failure({"setting '", name, "' cannot use the unavailable sentinel"}));
            SettingsSnapshotError error;
            const bool valid = selector
                ? ValidateSettingsSnapshotSelectorToken(entry.id, requested, error)
                : requested == UnavailableValue || access.validateValue(
                    access.context, entry.id, requested, requestedDependencySelector(entry), error);
            if (!valid)
                return reject(SettingsSnapshotTransactionFailureStage::Preflight,
                    Failure({"snapshot preflight rejected '", name, "': ", Reason(error)}, &error));
        }
        SettingsSnapshotError coupledError;
        if (!ValidateRequestedSpotPair(coupledError))
            return reject(SettingsSnapshotTransactionFailureStage::Preflight,
                Failure({"snapshot preflight rejected coupled state: ", coupledError.MessageView()}, &coupledError));
        for (std::size_t index = 0; index < m_State->count; ++index)
        {
            const auto& entry = m_State->entries[index];
            SettingsSnapshotText visible;
            SettingsSnapshotText raw;
            SettingsSnapshotError error;
            const auto name = entry.definition->name;
            if (!access.readValue(access.context, entry.id, visible, error) ||
                !access.readRawValue(access.context, entry.id, raw, error))
                return reject(SettingsSnapshotTransactionFailureStage::Capture,
                    Failure({"could not capture '", name, "': ", Reason(error)}, &error));
            const auto mode = entry.definition->applicationRole;
            const bool selector = mode == SettingsSnapshotApplicationMode::Selector ||
                mode == SettingsSnapshotApplicationMode::StartupPrecondition;
            if (selector && visible.View() == UnavailableValue)
                return reject(SettingsSnapshotTransactionFailureStage::Capture,
                    Failure({"required setting '", name, "' is unavailable"}));
            if (selector && !ValidateSettingsSnapshotSelectorToken(entry.id, visible.View(), error))
                return reject(SettingsSnapshotTransactionFailureStage::Capture,
                    Failure({"selector GET returned a noncanonical token for '", name, "': ", error.MessageView()}, &error));
            if (!selector && raw.View() != UnavailableValue &&
                !access.validateValue(access.context, entry.id, raw.View(), {}, error))
                return reject(SettingsSnapshotTransactionFailureStage::Capture,
                    Failure({"captured raw value for '", name, "' is not canonical: ", Reason(error)}, &error));
            if (mode == SettingsSnapshotApplicationMode::StartupPrecondition && visible.View() != entry.requestedValue.View())
                return reject(SettingsSnapshotTransactionFailureStage::Preflight,
                    Failure({"startup precondition '", name, "' does not match the active value; "
                        "select the requested adapter with -adapter before loading this snapshot"}));
            m_State->sourceVisible[index] = std::move(visible);
            m_State->sourceRaw[index] = std::move(raw);
        }
        m_Phase = Phase::ApplyScene;
        return AdvanceState();
    }
    SettingsSnapshotTransactionStep
    SettingsSnapshotTransactionCoordinator::Advance() noexcept
    {
        if (m_InOperation)
        {
            SettingsSnapshotTransactionStep rejected;
            rejected.result.failureStage = SettingsSnapshotTransactionFailureStage::Configuration;
            rejected.result.error = Failure("a settings transaction callback is already active");
            return rejected;
        }
        OperationScope operation(m_InOperation);
        return AdvanceState();
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotTransactionCoordinator::AdvanceState() noexcept
    {
        if (m_Phase == Phase::Succeeded)
            return Step(SettingsSnapshotTransactionProgress::Succeeded);
        if (m_Phase == Phase::Failed || m_Phase == Phase::Idle)
            return Step(SettingsSnapshotTransactionProgress::Failed);
        const auto& access = m_State->access;

        const auto findEntry = [this](SettingId id)
        {
            return FindTransactionEntry(id);
        };
        const auto hasMutatedValue = [this]()
        {
            for (std::size_t index = 0u; index < m_State->count; ++index)
            {
                if (m_State->mutated[index] && IsPlannedValue(*m_State->entries[index].definition))
                    return true;
            }
            return false;
        };
        const auto startFailure = [this](
            SettingsSnapshotTransactionFailureStage stage,
            SettingsSnapshotError error)
        {
            m_Result.failureStage = stage;
            m_Result.error = std::move(error);
            if (!m_State->mutationStarted)
            {
                m_Phase = Phase::Failed;
                return;
            }
            m_Result.rollbackAttempted = true;
            m_Phase = Phase::RollbackTargetValues;
            m_State->cursor = 0u;
            m_State->valueOrderCount = 0u;
            m_State->selectorRequestIssued = false;
        };
        const auto rollbackFailure = [this](SettingsSnapshotError error)
        {
            m_Result.rollbackAttempted = true;
            m_Result.rollbackSucceeded = false;
            m_Result.error = Failure({m_Result.error.MessageView(), "; rollback failed: ", error.MessageView()}, &m_Result.error);
            m_Phase = Phase::Failed;
        };
        const auto readVisible = [&access](
            const Entry& entry,
            SettingsSnapshotText& value,
            SettingsSnapshotError& error) noexcept
        {
            error = {};
            SettingsSnapshotError readError;
            if (access.readValue(access.context, entry.id, value, readError)) return true;
            error = std::move(readError);
            return false;
        };
        const auto readRaw = [&access](
            const Entry& entry,
            SettingsSnapshotText& value,
            SettingsSnapshotError& error) noexcept
        {
            error = {};
            SettingsSnapshotError readError;
            if (access.readRawValue(access.context, entry.id, value, readError)) return true;
            error = std::move(readError);
            return false;
        };
        const auto writeRawIfDifferent = [&access, &readRaw](
            const Entry& entry,
            std::string_view desired,
            SettingsSnapshotError& error) noexcept
        {
            SettingsSnapshotText current;
            if (!readRaw(entry, current, error))
                return false;
            if (current.View() == desired)
                return true;
            if (!access.writeValue(access.context, entry.id, desired, error))
                return false;
            current = {};
            if (!readRaw(entry, current, error))
                return false;
            if (current.View() != desired)
            {
                error = Failure({"readback mismatch for '", entry.definition->name, "'"});
                return false;
            }
            return true;
        };
        const auto applyValue = [this, &access, &readRaw, &startFailure](
            std::size_t index)
        {
            const Entry& entry =
                m_State->entries[index];
            SettingsSnapshotText current;
            SettingsSnapshotError operationError;
            if (!readRaw(entry, current, operationError))
            {
                startFailure(SettingsSnapshotTransactionFailureStage::Apply,
                            Failure({"could not read '", entry.definition->name, "' before applying it: ", operationError.MessageView()}, &operationError));
                return false;
            }
            if (current.View() == entry.requestedValue.View())
                return true;

            m_State->mutationStarted = true;
            m_State->mutated[index] = true;
            if (!access.writeValue(
                    access.context, entry.id, entry.requestedValue.View(), operationError))
            {
                startFailure(SettingsSnapshotTransactionFailureStage::Apply,
                            Failure({"could not apply '", entry.definition->name, "': ", Reason(operationError)}, &operationError));
                return false;
            }
            current = {};
            operationError = {};
            if (!readRaw(entry, current, operationError) ||
                current.View() != entry.requestedValue.View())
            {
                startFailure(SettingsSnapshotTransactionFailureStage::Readback,
                            Failure({"applied value mismatch for '", entry.definition->name, "'", (operationError.MessageView().empty() ? std::string_view{} : std::string_view{": "}), operationError.MessageView()}, &operationError));
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
                -> SettingsSnapshotTransactionProgress
            {
                const std::size_t index = findEntry(id);
                if (index == m_State->count)
                {
                    m_Phase = nextPhase;
                    m_State->cursor = 0u;
                    m_State->selectorRequestIssued = false;
                    return SettingsSnapshotTransactionProgress::Succeeded;
                }

                const Entry& entry =
                    m_State->entries[index];
                SettingsSnapshotText current;
                SettingsSnapshotError operationError;
                if (!readVisible(entry, current, operationError))
                {
                    if (rollback)
                        rollbackFailure(Failure({"could not read selector '", entry.definition->name, "': ", operationError.MessageView()}, &operationError));
                    else
                        startFailure(SettingsSnapshotTransactionFailureStage::Selector,
                            Failure({"could not read selector '", entry.definition->name, "': ", operationError.MessageView()}, &operationError));
                    return SettingsSnapshotTransactionProgress::Failed;
                }
                if (!m_State->selectorRequestIssued && current.View() == desired)
                {
                    m_Phase = nextPhase;
                    m_State->cursor = 0u;
                    return SettingsSnapshotTransactionProgress::Succeeded;
                }
                if (!access.driveSelector)
                {
                    if (rollback)
                        rollbackFailure(Failure({"no selector driver can restore '", entry.definition->name, "'"}));
                    else
                        startFailure(SettingsSnapshotTransactionFailureStage::Selector,
                            Failure({"no selector driver can apply '", entry.definition->name, "'"}));
                    return SettingsSnapshotTransactionProgress::Failed;
                }

                const bool begin = !m_State->selectorRequestIssued;
                if (begin && !rollback)
                {
                    m_State->mutationStarted = true;
                    m_State->mutated[index] = true;
                }
                const SettingsSnapshotSelectorTransition transition =
                    access.driveSelector(
                        access.context, id, desired, begin, rollback, operationError);
                if (transition == SettingsSnapshotSelectorTransition::Failed)
                {
                    if (rollback)
                        rollbackFailure(Failure({"selector rollback failed for '", entry.definition->name, "': ", operationError.MessageView()}, &operationError));
                    else
                        startFailure(SettingsSnapshotTransactionFailureStage::Selector,
                            Failure({"selector transition failed for '", entry.definition->name, "': ", operationError.MessageView()}, &operationError));
                    return SettingsSnapshotTransactionProgress::Failed;
                }

                m_State->selectorRequestIssued = true;
                if (!rollback && begin)
                    ++m_Result.changedValueCount;
                if (transition == SettingsSnapshotSelectorTransition::Pending)
                {
                    return SettingsSnapshotTransactionProgress::Pending;
                }

                current = {};
                operationError = {};
                if (!readVisible(entry, current, operationError) ||
                    current.View() != desired)
                {
                    const std::string_view failure = operationError.MessageView().empty()
                        ? "selector reported ready without publishing its token"
                        : operationError.MessageView();
                    if (rollback)
                        rollbackFailure(Failure({"selector rollback readback failed for '", entry.definition->name, "': ", failure}));
                    else
                        startFailure(SettingsSnapshotTransactionFailureStage::Readback,
                            Failure({"selector readback failed for '", entry.definition->name, "': ", failure}));
                    return SettingsSnapshotTransactionProgress::Failed;
                }
                m_Phase = nextPhase;
                m_State->cursor = 0u;
                m_State->selectorRequestIssued = false;
                return SettingsSnapshotTransactionProgress::Succeeded;
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
                const std::string_view desired = index == m_State->count ? std::string_view{} :
                    rollback ? m_State->sourceVisible[index].View() : std::string_view(m_State->entries[index].requestedValue.View());
                auto selectorStep = advanceSelector(id, desired, rollback, next);
                if (m_Phase == next || (!rollback && m_Phase == Phase::RollbackTargetValues))
                    continue;
                return Step(selectorStep, selectorStep == SettingsSnapshotTransactionProgress::Pending
                    ? m_State->entries[index].definition->name : std::string_view{});
            }

            case Phase::CaptureTarget:
            {
                for (; m_State->cursor < m_State->count; ++m_State->cursor)
                {
                    const Entry& entry =
                        m_State->entries[m_State->cursor];
                    SettingsSnapshotText rawValue;
                    SettingsSnapshotError operationError;
                    if (!readRaw(entry, rawValue, operationError))
                    {
                        startFailure(SettingsSnapshotTransactionFailureStage::Capture,
                            Failure({"could not capture target raw value '", entry.definition->name, "': ", operationError.MessageView()}, &operationError));
                        break;
                    }
                    if (IsPlannedValue(*entry.definition) &&
                        rawValue.View() != UnavailableValue &&
                        !access.validateValue(
                            access.context, entry.id, rawValue.View(), {}, operationError))
                    {
                        startFailure(SettingsSnapshotTransactionFailureStage::Capture,
                            Failure({"target raw value for '", entry.definition->name, "' is not canonical: ", operationError.MessageView()}, &operationError));
                        break;
                    }
                    m_State->targetRaw[m_State->cursor] = std::move(rawValue);
                }
                if (m_Phase == Phase::RollbackTargetValues ||
                    m_Phase == Phase::Failed)
                {
                    continue;
                }

                std::string_view requestedValues[TransactionCapacity];
                std::string_view targetValues[TransactionCapacity];
                for (std::size_t index = 0u; index < m_State->count; ++index)
                {
                    requestedValues[index] = m_State->entries[index].requestedValue.View();
                    targetValues[index] = m_State->targetRaw[index].View();
                }
                SettingsSnapshotError planError;
                if (!BuildTransactionValueOrder({requestedValues, m_State->count},
                        {targetValues, m_State->count},
                        false,
                        planError))
                {
                    startFailure(SettingsSnapshotTransactionFailureStage::Preflight,
                            Failure({"could not plan settings values: ", planError.MessageView()}, &planError));
                    continue;
                }
                m_State->cursor = 0u;
                m_Phase = Phase::ApplyPrerequisites;
                continue;
            }
            case Phase::ApplyPrerequisites:
            {
                for (; m_State->cursor < m_State->valueOrderCount; ++m_State->cursor)
                {
                    const std::size_t index = m_State->valueOrder[m_State->cursor];
                    const SettingId id = m_State->entries[index].id;
                    const bool prerequisite = std::any_of(
                        m_State->entries,
                        m_State->entries + m_State->count,
                        [id](const Entry& entry)
                        {
                            return entry.definition->valueDependsOn == id;
                        });
                    if (!prerequisite)
                        continue;
                    m_State->prerequisiteApplied[index] = true;
                    if (m_State->entries[index].requestedValue.View() ==
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
                m_State->cursor = 0u;
                m_Phase = Phase::ValidateTarget;
                continue;
            }
            case Phase::ValidateTarget:
            {
                for (; m_State->cursor < m_State->count; ++m_State->cursor)
                {
                    const Entry& entry =
                        m_State->entries[m_State->cursor];
                    if (!IsPlannedValue(*entry.definition))
                        continue;

                    SettingsSnapshotError operationError;
                    if (entry.requestedValue.View() != UnavailableValue &&
                        !access.validateValue(
                            access.context, entry.id,
                            entry.requestedValue.View(),
                            {},
                            operationError))
                    {
                        startFailure(SettingsSnapshotTransactionFailureStage::Preflight,
                            Failure({"target context rejected '", entry.definition->name, "': ", operationError.MessageView()}, &operationError));
                        break;
                    }

                    SettingsSnapshotText visibleValue;
                    if (!readVisible(entry, visibleValue, operationError))
                    {
                        startFailure(SettingsSnapshotTransactionFailureStage::Capture,
                            Failure({"could not read target availability for '", entry.definition->name, "': ", operationError.MessageView()}, &operationError));
                        break;
                    }
                    if ((entry.requestedValue.View() == UnavailableValue) !=
                        (visibleValue.View() == UnavailableValue))
                    {
                        startFailure(SettingsSnapshotTransactionFailureStage::Preflight,
                            Failure({"target object availability does not match '", entry.definition->name, "'"}));
                        break;
                    }
                }
                if (m_Phase == Phase::RollbackTargetValues ||
                    m_Phase == Phase::Failed)
                {
                    continue;
                }
                m_State->cursor = 0u;
                m_Phase = Phase::ApplyValues;
                continue;
            }
            case Phase::ApplyValues:
            {
                for (; m_State->cursor < m_State->valueOrderCount; ++m_State->cursor)
                {
                    const std::size_t index = m_State->valueOrder[m_State->cursor];
                    if (m_State->prerequisiteApplied[index] ||
                        m_State->entries[index].requestedValue.View() ==
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
                m_State->cursor = 0u;
                m_Phase = Phase::Verify;
                continue;
            }
            case Phase::Verify:
            {
                for (; m_State->cursor < m_State->count; ++m_State->cursor)
                {
                    const Entry& entry =
                        m_State->entries[m_State->cursor];
                    SettingsSnapshotText current;
                    SettingsSnapshotError operationError;
                    if (!readVisible(entry, current, operationError) ||
                        current.View() != entry.requestedValue.View())
                    {
                        startFailure(SettingsSnapshotTransactionFailureStage::Readback,
                            Failure({"applied value mismatch for '", entry.definition->name, "'", (operationError.MessageView().empty() ? std::string_view{} : std::string_view{": "}), operationError.MessageView()}, &operationError));
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
                return Step(SettingsSnapshotTransactionProgress::Succeeded);
            }
            case Phase::RollbackTargetValues:
            case Phase::RollbackSourceValues:
            {
                const bool target = m_Phase == Phase::RollbackTargetValues;
                const Phase next = target ? Phase::RollbackScene : Phase::RollbackVerify;
                if (target && !hasMutatedValue())
                {
                    m_Phase = next;
                    m_State->cursor = 0u;
                    continue;
                }
                const auto& baseline = target ? m_State->targetRaw : m_State->sourceRaw;
                const std::string_view owner = target ? "target" : "source";
                if (m_State->valueOrderCount == 0u)
                {
                    SettingsSnapshotText currentRaw[TransactionCapacity];
                    std::string_view currentViews[TransactionCapacity];
                    std::string_view baselineViews[TransactionCapacity];
                    SettingsSnapshotError error;
                    for (std::size_t index = 0u; index < m_State->count; ++index)
                    {
                        if (!readRaw(m_State->entries[index], currentRaw[index], error))
                        {
                            rollbackFailure(Failure({"could not capture ", owner, " restore state: ", error.MessageView()}, &error));
                            break;
                        }
                        currentViews[index] = currentRaw[index].View();
                        baselineViews[index] = baseline[index].View();
                    }
                    if (m_Phase == Phase::Failed)
                        continue;
                    if (!BuildTransactionValueOrder({baselineViews, m_State->count},
                            {currentViews, m_State->count}, target, error))
                    {
                        rollbackFailure(Failure({"could not plan ", owner, " restore: ", error.MessageView()}, &error));
                        continue;
                    }
                    m_State->cursor = 0u;
                }
                for (; m_State->cursor < m_State->valueOrderCount; ++m_State->cursor)
                {
                    const std::size_t index = m_State->valueOrder[m_State->cursor];
                    if ((target && !m_State->mutated[index]) || baseline[index].View() == UnavailableValue)
                        continue;
                    SettingsSnapshotError error;
                    if (!writeRawIfDifferent(m_State->entries[index], baseline[index].View(), error))
                    {
                        rollbackFailure(Failure({"could not restore ", owner, " raw value '", m_State->entries[index].definition->name, "': ", error.MessageView()}, &error));
                        break;
                    }
                }
                if (m_Phase == Phase::Failed)
                    continue;
                m_State->valueOrderCount = 0u;
                m_State->cursor = 0u;
                m_Phase = next;
                continue;
            }

            case Phase::RollbackVerify:
            {
                for (; m_State->cursor < m_State->count; ++m_State->cursor)
                {
                    SettingsSnapshotText rawValue;
                    SettingsSnapshotText visibleValue;
                    SettingsSnapshotError operationError;
                    if (!readRaw(
                            m_State->entries[m_State->cursor],
                            rawValue,
                            operationError) ||
                        rawValue.View() != m_State->sourceRaw[m_State->cursor].View() ||
                        !readVisible(
                            m_State->entries[m_State->cursor],
                            visibleValue,
                            operationError) ||
                        visibleValue.View() != m_State->sourceVisible[m_State->cursor].View())
                    {
                        rollbackFailure(Failure({"source-state verification failed for '", m_State->entries[m_State->cursor].definition->name, "'"}));
                        break;
                    }
                }
                if (m_Phase == Phase::Failed &&
                    !m_Result.rollbackSucceeded)
                {
                    return Step(SettingsSnapshotTransactionProgress::Failed);
                }
                m_Result.rollbackSucceeded = true;
                m_Result.succeeded = false;
                m_Phase = Phase::Failed;
                return Step(SettingsSnapshotTransactionProgress::Failed);
            }
            case Phase::Succeeded:
                return Step(SettingsSnapshotTransactionProgress::Succeeded);
            case Phase::Failed:
            case Phase::Idle:
                return Step(SettingsSnapshotTransactionProgress::Failed);
            }
        }
    }

#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
    void FailSettingsSnapshotTransactionAllocationAfter(std::size_t successfulAllocations) noexcept
    {
        stateAllocationsBeforeFailure = successfulAllocations;
    }
    void ClearSettingsSnapshotTransactionAllocationFailure() noexcept
    {
        stateAllocationsBeforeFailure = SIZE_MAX;
    }
#endif

}
