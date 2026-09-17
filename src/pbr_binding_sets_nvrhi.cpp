/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "pbr_binding_sets_nvrhi.h"
#include <new>

namespace uvsr
{
namespace
{
    bool Equal(const nvrhi::BindingSetDesc& a, const nvrhi::BindingSetDesc& b) noexcept
    {
        if (a.trackLiveness != b.trackLiveness || a.bindings.size() != b.bindings.size()) return false;
        for (size_t index = 0; index < a.bindings.size(); ++index)
            if (a.bindings[index] != b.bindings[index] || a.bindings[index].arrayElement != b.bindings[index].arrayElement)
                return false;
        return true;
    }
#if defined(UVSR_BUILD_TESTING)
    PbrBindingFailure failureOperation = PbrBindingFailure::None;
    bool Fail(PbrBindingFailure operation) noexcept
    {
        if (failureOperation != operation) return false;
        failureOperation = PbrBindingFailure::None;
        return true;
    }
#else
    constexpr bool Fail(PbrBindingFailure) noexcept { return false; }
#endif
}

struct PbrBindingSetsNvrhi::Entry
{
    nvrhi::BindingSetHandle binding;
    Entry* next = nullptr;
};

nvrhi::BindingSetHandle PbrBindingSetsNvrhi::GetOrCreateBindingSet(
    const nvrhi::BindingSetDesc& desc, nvrhi::IBindingLayout* layout) noexcept
{
    if (!m_device || !layout || !desc.trackLiveness) return nullptr;
    for (const Entry* entry = m_entries; entry; entry = entry->next)
    {
        const auto* key = entry->binding->getDesc();
        if (entry->binding->getLayout() == layout && key && Equal(*key, desc))
            return entry->binding;
    }
    if (m_count >= size_t(PTRDIFF_MAX) / sizeof(Entry)) return nullptr;
    Entry* candidate = Fail(PbrBindingFailure::Allocation) ? nullptr : new (std::nothrow) Entry;
    if (!candidate) return nullptr;
    candidate->binding = Fail(PbrBindingFailure::Create) ? nullptr : m_device->createBindingSet(desc, layout);
    if (!candidate->binding) { delete candidate; return nullptr; }
    candidate->next = m_entries;
    m_entries = candidate;
    ++m_count;
    if (m_count > m_peak) m_peak = m_count;
    return candidate->binding;
}

void PbrBindingSetsNvrhi::Clear() noexcept
{
    while (m_entries)
    {
        Entry* entry = m_entries;
        m_entries = entry->next;
        delete entry;
    }
    m_count = 0;
}

size_t PbrBindingSetsNvrhi::StorageBytes() const noexcept { return sizeof(*this) + m_count * sizeof(Entry); }

#if defined(UVSR_BUILD_TESTING)
void SetPbrBindingFailure(PbrBindingFailure operation) noexcept { failureOperation = operation; }
#endif
}
