#include "renderer_scene_load_worker.h"

#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_CPPUNWIND)
#include <exception>
#endif

namespace
{
    using namespace uvsr;
    using State = RendererSceneLoadWorkerState;

    void Require(bool value, const char* message)
    {
        if (!value)
        {
            fprintf(stderr, "scene worker test failed: %s\n", message);
            exit(EXIT_FAILURE);
        }
    }
    void Wait(HANDLE event)
    {
        Require(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0, "event completion");
    }
    void Signal(HANDLE event) { Require(SetEvent(event) != FALSE, "event signal"); }

#if defined(_CPPUNWIND)
    struct ImportFailure final : std::exception
    {
        explicit ImportFailure(const char* message) : text(message) {}
        const char* what() const noexcept override { return text; }
        const char* text;
    };
#endif

    struct Fixture
    {
        HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE cancelled = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE resetComplete = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        bool waitForCancellation = false, waitForRelease = false, result = true;
        bool throwUnknown = false;
        const char* throwText = nullptr;
        const char* reportText = nullptr;
        bool reportFailure = false;
        bool initiallyCancelled = false;
        int published = 0;
        int* destructionProof = nullptr;
        Fixture() { Require(entered && cancelled && release && resetComplete, "fixture events"); }
        ~Fixture()
        {
            if (destructionProof)
            {
                Require(published == 42, "constructor unwind destroyed a live task context");
                ++*destructionProof;
            }
            Require(CloseHandle(entered) && CloseHandle(cancelled) && CloseHandle(release) &&
                CloseHandle(resetComplete), "fixture handle release");
        }
        Fixture(const Fixture&) = delete;
        Fixture& operator=(const Fixture&) = delete;
        static bool Work(void* context, const RendererSceneLoadCancellation& cancellation)
        {
            auto& self = *static_cast<Fixture*>(context);
            self.initiallyCancelled = cancellation.IsRequested();
            Signal(self.entered);
            if (self.waitForCancellation)
            {
                while (!cancellation.IsRequested())
                    SwitchToThread();
                Signal(self.cancelled);
            }
            if (self.reportFailure)
            {
                char local[1024];
                if (self.reportText)
                {
                    snprintf(local, sizeof(local), "%s", self.reportText);
                    Require(!cancellation.Fail(local), "explicit failure return");
                    memset(local, 'x', sizeof(local));
                }
                else Require(!cancellation.Fail(nullptr), "null failure return");
                Signal(self.cancelled);
            }
            if (self.waitForRelease)
                Wait(self.release);
#if defined(_CPPUNWIND)
            if (self.throwText)
                throw ImportFailure(self.throwText);
            if (self.throwUnknown)
                throw 7;
#endif
            self.published = 42;
            return self.result;
        }
    };

    struct ResetContext
    {
        RendererSceneLoadWorker& worker;
        Fixture& fixture;
        static unsigned __stdcall Run(void* context)
        {
            auto& self = *static_cast<ResetContext*>(context);
            self.worker.Reset();
            Signal(self.fixture.resetComplete);
            return 0;
        }
    };
}

void TestRendererSceneLoadWorker()
{
    static_assert(sizeof(RendererSceneLoadFailureText) == 512);
    static_assert(noexcept(RendererSceneLoadFailureText{}.Assign("failure")));
    RendererSceneLoadFailureText retainedText;
    Require(retainedText.Empty() && retainedText.Data()[0] == '\0', "default diagnostic owner");
    char source[1026], expected[RendererSceneLoadFailureText::Capacity];
    unsigned textChecks = 1;
    for (unsigned length = 0; length <= 1024; ++length)
    {
        for (unsigned index = 0; index < length; ++index) source[index] = char(1 + index % 255);
        source[length] = '\0';
        (void)snprintf(expected, sizeof(expected), "%s", source);
        retainedText.Assign(source);
        Require(strcmp(retainedText.Data(), expected) == 0, "fixed owner changed the existing diagnostic prefix");
        ++textChecks;
        source[0] = 'x';
        Require(strcmp(retainedText.Data(), expected) == 0, "diagnostic still borrowed caller storage");
        ++textChecks;
    }
    const RendererSceneLoadFailureText copiedText = retainedText;
    retainedText.Clear();
    Require(retainedText.Empty() && strcmp(copiedText.Data(), expected) == 0, "clearing one diagnostic changed its copy");
    ++textChecks;
    fprintf(stdout, "scene failure text: %u assertions including fixture checks\n", textChecks);
    RendererSceneLoadWorker worker;
    Require(!worker.Start(nullptr, nullptr) && worker.GetState() == State::Idle, "null task changed state");
    {
        Fixture fixture;
        Require(worker.Start(Fixture::Work, &fixture), "success task start");
        while (worker.GetState() == State::Running)
            SwitchToThread();
        Require(worker.GetState() == State::Succeeded && !worker.Start(Fixture::Work, &fixture),
            "terminal but unjoined task was replaced");
        worker.RequestCancel();
        Require(worker.GetState() == State::Succeeded && worker.Join() && fixture.published == 42 &&
            !fixture.initiallyCancelled && !*worker.GetFailureText(), "success publication or completion-wins cancellation");
        fixture.result = false;
        Require(worker.Start(Fixture::Work, &fixture) && !worker.Join() && worker.GetState() == State::Failed &&
            !*worker.GetFailureText(), "false result or reuse after Join");
    }
    worker.Reset();
    Require(worker.GetState() == State::Idle && !*worker.GetFailureText(), "reset retained failure");
    {
        Fixture fixture;
        fixture.waitForCancellation = fixture.waitForRelease = true;
        Require(worker.Start(Fixture::Work, &fixture), "cancel task start");
        Wait(fixture.entered);
        Require(!worker.Start(Fixture::Work, &fixture), "running task was replaced");
        worker.RequestCancel();
        Wait(fixture.cancelled);
        Require(worker.GetState() == State::CancelRequested && fixture.published == 0,
            "cancellation claimed completion before the task returned");
        Signal(fixture.release);
        Require(!worker.Join() && worker.GetState() == State::Cancelled && fixture.published == 42,
            "cancelled handoff was accepted as success or not drained");
    }
    {
        Fixture fixture;
        Require(worker.Start(Fixture::Work, &fixture) && worker.Join() && !fixture.initiallyCancelled,
            "a later task inherited cancellation");
    }
    {
        Fixture fixture;
        fixture.waitForCancellation = fixture.waitForRelease = true;
        Require(worker.Start(Fixture::Work, &fixture), "reset task start");
        Wait(fixture.entered);
        ResetContext context{worker, fixture};
        const uintptr_t thread = _beginthreadex(nullptr, 0, ResetContext::Run, &context, 0, nullptr);
        Require(thread != 0, "reset helper thread");
        Wait(fixture.cancelled);
        Require(worker.GetState() == State::CancelRequested &&
            WaitForSingleObject(fixture.resetComplete, 0) == WAIT_TIMEOUT, "Reset returned with a live borrowed context");
        Signal(fixture.release);
        Wait(reinterpret_cast<HANDLE>(thread));
        Require(CloseHandle(reinterpret_cast<HANDLE>(thread)) != FALSE, "reset helper handle release");
        Require(worker.GetState() == State::Idle && fixture.published == 42, "Reset did not drain and clear");
    }
    {
        Fixture fixture;
        fixture.waitForCancellation = true;
        {
            RendererSceneLoadWorker scoped;
            Require(scoped.Start(Fixture::Work, &fixture), "destructor task start");
            Wait(fixture.entered);
        }
        Require(fixture.published == 42, "worker outlived its borrowed context");
    }
#if defined(_CPPUNWIND)
    {
        struct FailingOwner
        {
            Fixture context;
            RendererSceneLoadWorker worker;
            explicit FailingOwner(int& proof)
            {
                context.destructionProof = &proof;
                context.waitForCancellation = true;
                Require(worker.Start(Fixture::Work, &context), "constructor-unwind task start");
                Wait(context.entered);
                throw 7;
            }
        };
        int drained = 0;
        bool failed = false;
        try { FailingOwner owner(drained); }
        catch (int) { failed = true; }
        Require(failed && drained == 1, "constructor-unwind ownership ordering");
    }
    {
        Fixture fixture;
        fixture.throwText = "scene import failed";
        Require(worker.Start(Fixture::Work, &fixture) && !worker.Join() && worker.GetState() == State::Failed &&
            strcmp(worker.GetFailureText(), fixture.throwText) == 0, "exception diagnostic did not survive its object");
        fixture.throwText = nullptr;
        fixture.throwUnknown = true;
        Require(worker.Start(Fixture::Work, &fixture) && !worker.Join() &&
            strcmp(worker.GetFailureText(), "The scene importer threw an unknown exception.") == 0,
            "unknown exception diagnostic");
    }
    {
        Fixture fixture;
        fixture.waitForCancellation = fixture.waitForRelease = true;
        fixture.throwText = "failure after cancellation";
        Require(worker.Start(Fixture::Work, &fixture), "cancel then exception start");
        Wait(fixture.entered);
        worker.RequestCancel();
        Wait(fixture.cancelled);
        Signal(fixture.release);
        Require(!worker.Join() && worker.GetState() == State::Failed &&
            strcmp(worker.GetFailureText(), fixture.throwText) == 0, "cancellation hid an exception");
    }
    {
        Fixture fixture;
        char message[600];
        memset(message, 'c', sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        fixture.throwText = message;
        Require(worker.Start(Fixture::Work, &fixture) && !worker.Join(), "long diagnostic task");
        message[0] = 'x';
        Require(strlen(worker.GetFailureText()) == 511, "diagnostic bound or termination");
        for (size_t index = 0; index < 511; ++index)
            Require(worker.GetFailureText()[index] == 'c', "diagnostic borrowed temporary text");
        retainedText.Assign(worker.GetFailureText());
        worker.Reset();
        Require(worker.GetFailureText()[0] == '\0' && strlen(retainedText.Data()) == 511,
            "worker Reset invalidated the retained diagnostic");
        for (size_t index = 0; index < 511; ++index)
            Require(retainedText.Data()[index] == 'c', "worker Reset changed retained diagnostic bytes");
    }
#endif
    unsigned checkedAssertions = 0;
    {
        Fixture fixture;
        char message[600]; memset(message, 'r', sizeof(message) - 1); message[599] = '\0';
        fixture.reportFailure = fixture.waitForRelease = true;
        fixture.reportText = message;
        Require(worker.Start(Fixture::Work, &fixture), "checked task start"); ++checkedAssertions;
        Wait(fixture.cancelled);
        Require(worker.GetState() == State::Running && !*worker.GetFailureText(),
            "explicit failure published terminal state before task return"); ++checkedAssertions;
        memset(message, 'z', sizeof(message));
        Signal(fixture.release);
        Require(!worker.Join() && worker.GetState() == State::Failed && strlen(worker.GetFailureText()) == 511,
            "ignored explicit failure or diagnostic bound"); ++checkedAssertions;
        for (unsigned i = 0; i < 511; ++i)
        { Require(worker.GetFailureText()[i] == 'r', "checked diagnostic borrowed task storage"); ++checkedAssertions; }
        retainedText.Assign(worker.GetFailureText());
        worker.Reset();
        Require(!*worker.GetFailureText() && strlen(retainedText.Data()) == 511,
            "checked diagnostic did not survive Reset copy"); ++checkedAssertions;
        fixture.reportFailure = fixture.waitForRelease = false;
        Require(worker.Start(Fixture::Work, &fixture) && worker.Join() && !*worker.GetFailureText(),
            "later Start inherited explicit failure"); ++checkedAssertions;
    }
    {
        Fixture fixture;
        fixture.reportFailure = fixture.waitForCancellation = fixture.waitForRelease = true;
        fixture.reportText = "checked failure after cancellation";
        Require(worker.Start(Fixture::Work, &fixture), "cancel checked task start"); ++checkedAssertions;
        Wait(fixture.entered); worker.RequestCancel(); Wait(fixture.cancelled); Signal(fixture.release);
        Require(!worker.Join() && worker.GetState() == State::Failed &&
            strcmp(worker.GetFailureText(), fixture.reportText) == 0, "cancellation hid checked failure"); ++checkedAssertions;
    }
    for (unsigned empty = 0; empty < 2; ++empty)
    {
        Fixture fixture; fixture.reportFailure = true; fixture.reportText = empty ? "" : nullptr;
        Require(worker.Start(Fixture::Work, &fixture) && !worker.Join() && worker.GetState() == State::Failed &&
            strcmp(worker.GetFailureText(), "The scene task reported a failure.") == 0,
            "empty checked failure became success"); ++checkedAssertions;
    }
#if defined(_CPPUNWIND)
    {
        Fixture fixture; fixture.reportFailure = true; fixture.reportText = "first checked failure";
        fixture.throwText = "later exception";
        Require(worker.Start(Fixture::Work, &fixture) && !worker.Join() &&
            strcmp(worker.GetFailureText(), fixture.throwText) == 0, "checked diagnostic hid exception"); ++checkedAssertions;
    }
#endif
    printf("scene checked failure: %u assertions including fixture checks\n", checkedAssertions);
    worker.Reset();
    printf("scene worker: named context, cancellation, drain, reuse and copied diagnostics passed\n");
}
