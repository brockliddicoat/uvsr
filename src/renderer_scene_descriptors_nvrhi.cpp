/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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

#include "renderer_scene_descriptors_nvrhi.h"
#include <new>
#include <stdint.h>

namespace uvsr
{
namespace
{
    constexpr uint32_t Empty = UINT32_MAX;
#if defined(UVSR_BUILD_TESTING)
    RendererDescriptorFailure failureOperation = RendererDescriptorFailure::None;
    unsigned failureSkip = 0;
    bool Fail(RendererDescriptorFailure operation) noexcept
    {
        if (failureOperation != operation) return false;
        if (failureSkip) { --failureSkip; return false; }
        failureOperation = RendererDescriptorFailure::None;
        return true;
    }
#else
    constexpr bool Fail(RendererDescriptorFailure) noexcept { return false; }
#endif
    uint64_t Hash(const nvrhi::BindingSetItem& item) noexcept
    {
        uint64_t value = 1469598103934665603ull;
        const uint64_t fields[]{uintptr_t(item.resourceHandle), uint64_t(item.type), uint64_t(item.format),
            uint64_t(item.dimension), item.rawData[0], item.rawData[1]};
        for (uint64_t field : fields) { value ^= field; value *= 1099511628211ull; }
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdull;
        value ^= value >> 33;
        value *= 0xc4ceb9fe1a85ec53ull;
        value ^= value >> 33;
        return value;
    }
    bool Equal(const nvrhi::BindingSetItem& a, const nvrhi::BindingSetItem& b) noexcept
    {
        return a.resourceHandle == b.resourceHandle && a.type == b.type && a.format == b.format &&
            a.dimension == b.dimension && a.rawData[0] == b.rawData[0] && a.rawData[1] == b.rawData[1];
    }
}

struct RendererSceneDescriptorsNvrhi::Slot
{
    nvrhi::BindingSetItem item{};
    uint32_t bucket = Empty, next = Empty;
    bool live = false;
};

RendererSceneDescriptorsNvrhi::RendererSceneDescriptorsNvrhi(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout) noexcept
    : m_device(device)
{
    const auto* desc = layout ? layout->getBindlessDesc() : nullptr;
    if (!device || !desc || !desc->maxCapacity || desc->maxCapacity > INT32_MAX)
    { m_error = RendererDescriptorError::Input; return; }
    m_maxCapacity = desc->maxCapacity;
    m_table = Fail(RendererDescriptorFailure::Create) ? nullptr : device->createDescriptorTable(layout);
    if (!m_table || m_table->getCapacity() > m_maxCapacity)
    { m_table = nullptr; m_error = RendererDescriptorError::Gpu; return; }
    if (m_table->getCapacity() && !Grow(m_table->getCapacity(), false)) m_table = nullptr;
}

RendererSceneDescriptorsNvrhi::~RendererSceneDescriptorsNvrhi() noexcept
{
    for (uint32_t index = 0; index < m_capacity; ++index)
        if (m_slots[index].live) m_slots[index].item.resourceHandle->Release();
    delete[] m_slots;
}

size_t RendererSceneDescriptorsNvrhi::StorageBytes() const noexcept { return sizeof(*this) + size_t(m_capacity) * sizeof(Slot); }

bool RendererSceneDescriptorsNvrhi::Grow(uint32_t capacity, bool resize) noexcept
{
    if (capacity <= m_capacity || capacity > m_maxCapacity || size_t(capacity) > size_t(PTRDIFF_MAX) / sizeof(Slot))
    { m_error = RendererDescriptorError::Capacity; return false; }
    Slot* candidate = Fail(RendererDescriptorFailure::Allocation) ? nullptr : new (std::nothrow) Slot[capacity];
    if (!candidate) { m_error = RendererDescriptorError::Allocation; return false; }
    for (uint32_t index = 0; index < m_capacity; ++index)
    {
        if (!m_slots[index].live) continue;
        candidate[index].item = m_slots[index].item;
        candidate[index].live = true;
        const uint32_t bucket = uint32_t(Hash(candidate[index].item) % capacity);
        candidate[index].next = candidate[bucket].bucket;
        candidate[bucket].bucket = index;
    }
    if (resize)
    {
        if (!Fail(RendererDescriptorFailure::Resize)) m_device->resizeDescriptorTable(m_table, capacity);
        if (m_table->getCapacity() != capacity)
        { delete[] candidate; m_error = RendererDescriptorError::Gpu; return false; }
    }
    delete[] m_slots;
    m_slots = candidate;
    m_capacity = capacity;
    m_peakCapacity = capacity;
    return true;
}

int32_t RendererSceneDescriptorsNvrhi::CreateDescriptor(nvrhi::BindingSetItem item) noexcept
{
    m_error = RendererDescriptorError::None;
    if (!IsValid() || !item.resourceHandle || item.arrayElement != 0 ||
        (item.type != nvrhi::ResourceType::Texture_SRV && item.type != nvrhi::ResourceType::RawBuffer_SRV))
    { m_error = RendererDescriptorError::Input; return -1; }
    if (m_capacity)
        for (uint32_t i = m_slots[uint32_t(Hash(item) % m_capacity)].bucket; i != Empty; i = m_slots[i].next)
            if (Equal(m_slots[i].item, item)) return int32_t(i);
    uint32_t index = m_search;
    while (index < m_capacity && m_slots[index].live) ++index;
    if (index == m_capacity)
    {
        if (m_capacity == m_maxCapacity) { m_error = RendererDescriptorError::Capacity; return -1; }
        const uint32_t remaining = m_maxCapacity - m_capacity;
        const uint32_t increment = m_capacity ? (m_capacity < remaining ? m_capacity : remaining) :
            (64u < remaining ? 64u : remaining);
        if (!Grow(m_capacity + increment, true)) return -1;
    }
    item.slot = index;
    if (Fail(RendererDescriptorFailure::Write) || !m_device->writeDescriptorTable(m_table, item))
    { m_error = RendererDescriptorError::Gpu; return -1; }
    Slot& slot = m_slots[index];
    slot.item = item;
    slot.live = true;
    const uint32_t bucket = uint32_t(Hash(item) % m_capacity);
    slot.next = m_slots[bucket].bucket;
    m_slots[bucket].bucket = index;
    item.resourceHandle->AddRef();
    m_search = index + 1;
    ++m_live;
    if (m_live > m_peakLive) m_peakLive = m_live;
    return int32_t(index);
}

nvrhi::BindingSetItem RendererSceneDescriptorsNvrhi::GetDescriptor(int32_t index) const noexcept
{
    return index >= 0 && uint32_t(index) < m_capacity && m_slots[index].live ?
        m_slots[index].item : nvrhi::BindingSetItem::None(0);
}

bool RendererSceneDescriptorsNvrhi::ReleaseDescriptor(int32_t index) noexcept
{
    m_error = RendererDescriptorError::None;
    if (index < 0 || uint32_t(index) >= m_capacity || !m_slots[index].live)
    { m_error = RendererDescriptorError::Input; return false; }
    if (Fail(RendererDescriptorFailure::Write) || !m_device->writeDescriptorTable(m_table, nvrhi::BindingSetItem::None(uint32_t(index))))
    { m_error = RendererDescriptorError::Gpu; return false; }
    Slot& slot = m_slots[index];
    uint32_t* link = &m_slots[uint32_t(Hash(slot.item) % m_capacity)].bucket;
    while (*link != uint32_t(index)) link = &m_slots[*link].next;
    *link = slot.next;
    slot.item.resourceHandle->Release();
    slot.item = nvrhi::BindingSetItem::None(uint32_t(index));
    slot.next = Empty;
    slot.live = false;
    --m_live;
    if (uint32_t(index) < m_search) m_search = uint32_t(index);
    return true;
}

#if defined(UVSR_BUILD_TESTING)
void SetRendererDescriptorFailure(RendererDescriptorFailure operation, unsigned skip) noexcept
{ failureOperation = operation; failureSkip = skip; }
#endif
}
