#include <imgui.h>
#include <imgui_internal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    size_t allocations = 0;
    void* Allocate(size_t size, void*) noexcept { ++allocations; return malloc(size); }
    void Release(void* memory, void*) noexcept { free(memory); }
    void Require(bool value, const char* reason) noexcept
    {
        if (value) return;
        fprintf(stderr, "ImGui traversal failed: %s\n", reason); exit(1);
    }
    uint64_t Hash(uint64_t value, const void* memory, size_t size) noexcept
    {
        const auto* bytes = static_cast<const unsigned char*>(memory);
        for (size_t i = 0; i < size; ++i) value = (value ^ bytes[i]) * 1099511628211ull;
        return value;
    }
    void Paint() noexcept
    {
        const auto p = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled({p.x + 2, p.y + 2}, {p.x + 12, p.y + 12}, IM_COL32(19,173,229,255));
    }
}

int main(int argc, char** argv)
{
    const char* invalid = argc > 1 ? argv[1] : nullptr;
    ImGui::SetAllocatorFunctions(Allocate, Release);
    ImGuiContext* context = ImGui::CreateContext();
    Require(context != nullptr, "context");
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = {3000,1000}; io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Require(pixels && width > 0 && height > 0, "font atlas");
    io.Fonts->SetTexID(ImTextureID(1));

    constexpr int roots = 3, depth = 64, siblings = 24;
    constexpr size_t count = roots * (1 + depth + siblings);
    ImGuiWindow* expected[count]{};
    constexpr size_t branch = 1 + depth + siblings;
    // NoBringToFrontOnFocus inserts new roots at the front. tooltip draw data
    // still follows the ordinary roots in its separate display layer.
    const auto sorted = [&](size_t i) { return expected[(roots - 1 - i / branch) * branch + i % branch]; };
    const auto drawn = [&](size_t i) { const size_t r = i / branch; return expected[(r < 2 ? 1 - r : 2) * branch + i % branch]; };
    printf("ImGui object sizes: window %zu, context %zu\n", sizeof(ImGuiWindow), sizeof(ImGuiContext));
    uint64_t stableHash = 0;
    for (int frame = 0; frame < 6; ++frame)
    {
        const size_t before = allocations;
        ImGui::NewFrame();
        size_t used = 0;
        for (int root = 0; root < roots; ++root)
        {
            char name[32]; snprintf(name, sizeof(name), "root %d", root);
            const float x = float(root * 1000);
            ImGui::SetNextWindowPos({x,0}); ImGui::SetNextWindowSize({800,800});
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoBringToFrontOnFocus;
            if (root == roots - 1) flags |= ImGuiWindowFlags_Tooltip;
            ImGui::Begin(name, nullptr, flags);
            expected[used++] = ImGui::GetCurrentWindow(); Paint();
            for (int child = 0; child < depth; ++child)
            {
                snprintf(name, sizeof(name), "depth %d", child);
                ImGui::SetNextWindowPos({x + 16,16});
                ImGui::BeginChild(name, {600,600});
                expected[used++] = ImGui::GetCurrentWindow(); Paint();
            }
            for (int child = 0; child < depth; ++child) ImGui::EndChild();
            for (int child = 0; child < siblings; ++child)
            {
                snprintf(name, sizeof(name), "sibling %d", child);
                ImGui::SetNextWindowPos({x + 32 + float(child * 4),32});
                ImGui::BeginChild(name, {40,40});
                expected[used++] = ImGui::GetCurrentWindow(); Paint(); ImGui::EndChild();
            }
            ImGui::End();
        }
        Require(used == count, "fixture window count");
        if (frame == 2)
        {
            // exercise wrap with stale marks that would otherwise look current.
            context->WindowTraversalSerial = ~ImU64(0);
            for (ImGuiWindow* window : context->Windows) window->DC.ChildTraversalSerial = 1;
            if (invalid)
            {
                if (!strcmp(invalid, "sort-duplicate"))
                    expected[0]->DC.ChildWindows[1] = expected[1];
                else if (!strcmp(invalid, "sort-count"))
                    expected[0]->DC.ChildWindows.Size = context->Windows.Size + 1;
                else
                {
                    ImGui::EndFrame();
                    if (!strcmp(invalid, "draw-cycle"))
                    {
                        expected[depth]->DC.ChildWindows.push_back(expected[0]);
                        expected[0]->ParentWindow = expected[depth];
                    }
                    else if (!strcmp(invalid, "draw-parent"))
                        expected[1]->ParentWindow = nullptr;
                    else
                        Require(false, "unknown invalid fixture");
                }
            }
        }
        ImGui::Render();
        if (frame == 2)
        {
            Require(invalid == nullptr, "invalid hierarchy was accepted");
            Require(context->WindowTraversalSerial > 0 && context->WindowTraversalSerial < ImU64(context->Windows.Size * 2), "serial wrap");
        }

        size_t position = 0;
        for (ImGuiWindow* window : context->Windows)
        {
            if (!window->Active) continue;
            while (position < count && !sorted(position)->Active) ++position;
            if (position < count && window != sorted(position))
                fprintf(stderr, "sort entry %zu: actual %s, expected %s\n", position, window->Name, sorted(position)->Name);
            Require(position < count && window == sorted(position), "parent and sibling sort order");
            ++position;
        }
        while (position < count && !sorted(position)->Active) ++position;
        Require(position == count, "sorted active window count");
        const ImDrawData* draw = ImGui::GetDrawData();
        uint64_t hash = 14695981039346656037ull;
        position = 0;
        for (const ImDrawList* list : draw->CmdLists)
        {
            while (position < count && (!drawn(position)->Active || drawn(position)->Hidden || drawn(position)->DrawList->VtxBuffer.empty())) ++position;
            Require(position < count && list == drawn(position)->DrawList, "draw order and inherited tooltip layer");
            ++position;
            hash = Hash(hash, list->_OwnerName, strlen(list->_OwnerName));
            hash = Hash(hash, list->VtxBuffer.Data, size_t(list->VtxBuffer.Size) * sizeof(ImDrawVert));
            hash = Hash(hash, list->IdxBuffer.Data, size_t(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
            for (const ImDrawCmd& command : list->CmdBuffer)
            {
                Require(command.UserCallback == nullptr, "fixture has no renderer callback");
                hash = Hash(hash, &command.ClipRect, sizeof(command.ClipRect));
                hash = Hash(hash, &command.ElemCount, sizeof(command.ElemCount));
            }
        }
        if (frame >= 3)
        {
            if (frame == 3) stableHash = hash;
            Require(hash == stableHash, "stable warmed draw data");
        }
        printf("frame %d: %zu windows, %d draw lists, %zu allocations, digest %016llx\n",
            frame, count, draw->CmdListsCount, allocations - before, static_cast<unsigned long long>(hash));
    }
    ImGui::DestroyContext(context);
    printf("ImGui window traversal passed: 64 child levels, 24 siblings per root, inherited tooltip layer and serial wrap\n");
    return 0;
}
