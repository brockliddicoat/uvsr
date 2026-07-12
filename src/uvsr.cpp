#include <donut/app/ApplicationBase.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/utils.h>

using namespace donut;

namespace
{
constexpr const char* kWindowTitle = "UVSR — Unified Visibility Stochastic Rendering";

class UvsrRenderer final : public app::IRenderPass
{
public:
    using IRenderPass::IRenderPass;

    bool Init()
    {
        const auto shaderPath = app::GetDirectoryWithExecutable()
            / "shaders/uvsr" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        auto nativeFileSystem = std::make_shared<vfs::NativeFileSystem>();
        engine::ShaderFactory shaderFactory(GetDevice(), nativeFileSystem, shaderPath);
        m_vertexShader = shaderFactory.CreateShader("shaders.hlsl", "main_vs", nullptr, nvrhi::ShaderType::Vertex);
        m_pixelShader = shaderFactory.CreateShader("shaders.hlsl", "main_ps", nullptr, nvrhi::ShaderType::Pixel);

        if (!m_vertexShader || !m_pixelShader)
            return false;

        m_commandList = GetDevice()->createCommandList();
        return true;
    }

    void BackBufferResizing() override
    {
        m_pipeline = nullptr;
    }

    void Animate(float) override
    {
        GetDeviceManager()->SetInformativeWindowTitle(kWindowTitle);
    }

    void Render(nvrhi::IFramebuffer* framebuffer) override
    {
        if (!m_pipeline)
        {
            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.VS = m_vertexShader;
            pipelineDesc.PS = m_pixelShader;
            pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
            pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
            m_pipeline = GetDevice()->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
        }

        m_commandList->open();
        nvrhi::utils::ClearColorAttachment(m_commandList, framebuffer, 0, nvrhi::Color(0.008f, 0.014f, 0.028f, 1.f));

        nvrhi::GraphicsState state;
        state.pipeline = m_pipeline;
        state.framebuffer = framebuffer;
        state.viewport.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
        m_commandList->setGraphicsState(state);

        nvrhi::DrawArguments drawArguments;
        drawArguments.vertexCount = 3;
        m_commandList->draw(drawArguments);

        m_commandList->close();
        GetDevice()->executeCommandList(m_commandList);
    }

private:
    nvrhi::ShaderHandle m_vertexShader;
    nvrhi::ShaderHandle m_pixelShader;
    nvrhi::GraphicsPipelineHandle m_pipeline;
    nvrhi::CommandListHandle m_commandList;
};
}

#ifdef WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
#else
int main(int, const char**)
#endif
{
    const nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);
    app::DeviceManager* deviceManager = app::DeviceManager::Create(api);

    app::DeviceCreationParameters deviceParameters;
#ifdef _DEBUG
    deviceParameters.enableDebugRuntime = true;
    deviceParameters.enableNvrhiValidationLayer = true;
#endif

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParameters, kWindowTitle))
    {
        log::fatal("UVSR could not initialize the requested graphics device.");
        delete deviceManager;
        return 1;
    }

    {
        UvsrRenderer renderer(deviceManager);
        if (renderer.Init())
        {
            deviceManager->AddRenderPassToBack(&renderer);
            deviceManager->RunMessageLoop();
            deviceManager->RemoveRenderPass(&renderer);
        }
    }

    deviceManager->Shutdown();
    delete deviceManager;
    return 0;
}
