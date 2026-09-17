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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include <stdint.h>

namespace uvsr
{
    struct RendererReadbackUint4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    struct RendererPixelReadbackRequest
    {
        uint32_t x;
        uint32_t y;
    };

    enum class RendererReadbackError : uint8_t
    {
        None,
        InvalidInput,
        Uninitialized,
        Busy,
        NotSubmitted,
        AllocationFailed,
        ResourceCreationFailed,
        SubmissionFailed,
        MapFailed
    };

    struct RendererPixelReadbackNvrhi;

    class RendererPixelReadback final
    {
    public:
        RendererPixelReadback() = default;
        ~RendererPixelReadback();
        RendererPixelReadback(const RendererPixelReadback&) = delete;
        RendererPixelReadback& operator=(const RendererPixelReadback&) = delete;
        RendererPixelReadback(RendererPixelReadback&&) = delete;
        RendererPixelReadback& operator=(RendererPixelReadback&&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        // records one mip-0, slice-0 integer texel on the list retained at creation.
        [[nodiscard]] RendererReadbackError Capture(RendererPixelReadbackRequest request);
        // token must be the exact nonzero graphics submission of that same list.
        // it establishes ordering, not GPU completion or a cross-queue identity.
        [[nodiscard]] RendererReadbackError NotifySubmitted(uint64_t token) noexcept;
        // blocking after submission. output is unchanged on failure; one read attempt
        // consumes the request. the backend owns the completion wait and unmapping.
        [[nodiscard]] RendererReadbackError ReadUInts(RendererReadbackUint4& output);
        // only for an abandoned, never-submitted list. cannot undo GPU work.
        void CancelRecorded() noexcept;

    private:
        friend struct RendererPixelReadbackNvrhi;
        struct State;
        State* m_State = nullptr;
    };
}
