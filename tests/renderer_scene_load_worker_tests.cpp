#include "renderer_scene_load_worker.h"

#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Renderer scene-load worker test failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void TestSuccessAndReuse()
    {
        uvsr::RendererSceneLoadWorker worker;
        int publishedValue = 0;
        Require(worker.Start([&publishedValue]
            {
                publishedValue = 42;
                return true;
            }),
            "valid task did not start");
        Require(worker.Join(), "successful task failed");
        Require(
            worker.GetState() ==
                uvsr::RendererSceneLoadWorkerState::Succeeded &&
                publishedValue == 42,
            "successful task handoff was not published");
        Require(worker.Start([] { return true; }),
            "joined worker could not be reused");
        Require(worker.Join(), "reused worker failed");
        worker.Reset();
        Require(
            worker.GetState() ==
                uvsr::RendererSceneLoadWorkerState::Idle,
            "reset worker was not idle");
    }

    void TestFailureAndException()
    {
        uvsr::RendererSceneLoadWorker worker;
        Require(worker.Start([] { return false; }),
            "false-result task did not start");
        Require(!worker.Join(), "false-result task succeeded");
        Require(!worker.GetException(),
            "false-result task invented an exception");

        Require(worker.Start([]() -> bool
            {
                throw std::runtime_error("scene import failed");
            }),
            "throwing task did not start");
        Require(!worker.Join(), "throwing task succeeded");
        const std::exception_ptr failure = worker.GetException();
        Require(bool(failure), "throwing task lost its exception");
        try
        {
            std::rethrow_exception(failure);
        }
        catch (const std::runtime_error& error)
        {
            Require(std::string(error.what()) == "scene import failed",
                "throwing task changed its diagnostic");
            return;
        }
        catch (...)
        {
        }
        Require(false, "throwing task changed exception type");
    }

    void TestConcurrentStartRejection()
    {
        uvsr::RendererSceneLoadWorker worker;
        std::promise<void> entered;
        std::promise<void> release;
        const std::shared_future<void> releaseSignal =
            release.get_future().share();
        Require(worker.Start([&entered, releaseSignal]
            {
                entered.set_value();
                releaseSignal.wait();
                return true;
            }),
            "blocking task did not start");
        entered.get_future().wait();
        Require(worker.IsRunning(), "blocking task was not running");
        Require(!worker.Start([] { return true; }),
            "concurrent task replaced a live worker");
        release.set_value();
        Require(worker.Join(), "released blocking task failed");
    }

    void TestDestructorJoins()
    {
        std::atomic_bool finished = false;
        {
            uvsr::RendererSceneLoadWorker worker;
            Require(worker.Start([&finished]
                {
                    finished.store(true, std::memory_order_release);
                    return true;
                }),
                "destructor task did not start");
        }
        Require(finished.load(std::memory_order_acquire),
            "worker destructor returned before its task");
    }

    void TestEmptyTaskRejection()
    {
        uvsr::RendererSceneLoadWorker worker;
        Require(!worker.Start({}), "empty task was accepted");
        Require(
            worker.GetState() ==
                uvsr::RendererSceneLoadWorkerState::Idle &&
                !worker.IsJoinable(),
            "empty task changed worker state");
    }
}

int main()
{
    TestSuccessAndReuse();
    TestFailureAndException();
    TestConcurrentStartRejection();
    TestDestructorJoins();
    TestEmptyTaskRejection();
    return EXIT_SUCCESS;
}
