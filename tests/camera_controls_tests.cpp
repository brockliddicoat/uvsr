#include "camera_controllers.h"
#include "camera_collision.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <utility>
#include <vector>

using namespace uvsr;

namespace
{
    const gpu_contract::Float3 Position{3.f, -2.f, 7.f};
    const gpu_contract::Float3 Direction{0.f, 0.f, 1.f};
    constexpr float HalfDiagonal = 0.707106781f;

    void Require(bool condition, const char* message)
    {
        if (!condition)
            { std::fprintf(stderr, "camera acceptance failed: %s\n", message); std::exit(1); }
    }

    bool NearlyEqual(float actual, float expected, float tolerance = 1e-4f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    bool NearlyEqual(gpu_contract::Float3 actual, gpu_contract::Float3 expected, float tolerance = 1e-4f)
    {
        return LengthSquared(actual - expected) <= tolerance * tolerance;
    }

    bool SameMatrix(const gpu_contract::Float4x4& actual, const gpu_contract::Float4x4& expected)
    {
        for (size_t lane = 0; lane < 16; ++lane)
            if (actual.values[lane] != expected.values[lane])
                return false;
        return true;
    }

    template<class Camera>
    void Key(Camera& camera, int key, int action)
    {
        camera.KeyboardUpdate(key, 0, action, 0);
    }

    template<class Camera>
    void Frames(Camera& camera, int count, float duration = 1.f / 60.f)
    {
        for (int frame = 0; frame < count; ++frame)
            camera.Animate(duration);
    }

    template<class Camera>
    void RolledPose(Camera& camera)
    {
        camera.SetExactPose(Position, Direction,
            gpu_contract::Float3{-HalfDiagonal, HalfDiagonal, 0}, gpu_contract::Float3{-HalfDiagonal, -HalfDiagonal, 0});
    }

    void CheckModesAndKeys()
    {
        UvsrThirdPersonCamera free;
        free.SetExactPose(Position, gpu_contract::Float3{0, .6f, .8f}, gpu_contract::Float3{0, .8f, -.6f}, gpu_contract::Float3{-1, 0, 0});
        const auto view = free.GetWorldToViewMatrix();
        Require((gpu_contract::Float3{view.values[0], view.values[1], view.values[2]} == gpu_contract::Float3{-1, 0, 0}) &&
            (gpu_contract::Float3{view.values[4], view.values[5], view.values[6]} == gpu_contract::Float3{0, .8f, .6f}) &&
            (gpu_contract::Float3{view.values[8], view.values[9], view.values[10]} == gpu_contract::Float3{0, -.6f, .8f}), "exact pose lost its framing basis");
        StaticViewCamera locked;
        locked.SetExactPose(Position, free.GetDir(), free.GetUp(), gpu_contract::Float3{-1, 0, 0});
        Require(SameMatrix(locked.GetWorldToViewMatrix(), view), "Locked changed the Freelook spawn matrix");
        Key(locked, GLFW_KEY_W, GLFW_PRESS);
        locked.MousePosUpdate(900, 600);
        locked.MouseButtonUpdate(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        locked.Animate(1);
        Require(SameMatrix(locked.GetWorldToViewMatrix(), view), "Locked accepted movement or look input");

        UvsrFirstPersonCamera first(true);
        first.LookTo(gpu_contract::Float3{0.f}, gpu_contract::Float3{1, 0, 0});
        Key(first, GLFW_KEY_W, GLFW_PRESS);
        first.Animate(1);
        Key(first, GLFW_KEY_W, GLFW_RELEASE);
        Require(NearlyEqual(first.GetPosition(), gpu_contract::Float3{6, 0, 0}), "first-person forward speed changed");
        for (const int key : { GLFW_KEY_Q, GLFW_KEY_E })
        {
            Key(first, key, GLFW_PRESS);
            first.Animate(.25f);
            Key(first, key, GLFW_RELEASE);
            Require(key == GLFW_KEY_Q ? first.GetPosition().y > 0 : NearlyEqual(first.GetPosition().y, 0),
                "Q/E vertical movement changed");
        }
        for (const int key : { GLFW_KEY_SPACE, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT, GLFW_KEY_Z })
        {
            const auto before = first.GetWorldToViewMatrix();
            Key(first, key, GLFW_PRESS);
            first.Animate(.25f);
            Key(first, key, GLFW_RELEASE);
            Require(SameMatrix(first.GetWorldToViewMatrix(), before), "reserved input changed the camera");
            if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT)
            {
                first.LookTo(gpu_contract::Float3{0.f}, gpu_contract::Float3{1, 0, 0});
                Key(first, key, GLFW_PRESS);
                Key(first, GLFW_KEY_W, GLFW_PRESS);
                first.Animate(1);
                Key(first, GLFW_KEY_W, GLFW_RELEASE);
                Key(first, key, GLFW_RELEASE);
                Require(NearlyEqual(first.GetPosition(), gpu_contract::Float3{12, 0, 0}), "Shift must double movement without vertical motion");
            }
        }
        for (const int key : { GLFW_KEY_X, GLFW_KEY_C })
        {
            first.LookTo(Position, Direction);
            Key(first, key, GLFW_PRESS);
            first.Animate(.25f);
            Key(first, key, GLFW_RELEASE);
            Require(std::abs(first.GetUp().x) > .001f, "X/C roll control stopped moving the camera");
        }
        for (const bool translation : { false, true })
        {
            UvsrFirstPersonCamera camera(translation);
            camera.LookTo(Position, Direction);
            Key(camera, GLFW_KEY_W, GLFW_PRESS);
            Key(camera, GLFW_KEY_UP, GLFW_PRESS);
            camera.Animate(.25f);
            Require(NearlyEqual(camera.GetPosition(), Position) != translation && !NearlyEqual(camera.GetDir(), Direction),
                "pivot translation gating or arrow-key look changed");
        }
        free.LookTo(Position, Direction);
        Key(free, GLFW_KEY_RIGHT, GLFW_PRESS);
        free.Animate(.25f);
        Require(!NearlyEqual(free.GetDir(), Direction), "Freelook arrow-key rotation failed");
        free.CancelPendingMotion();
        for (const int key : { GLFW_KEY_SPACE, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT })
        {
            const auto before = free.GetPosition();
            Key(free, key, GLFW_PRESS);
            Frames(free, 60);
            Key(free, key, GLFW_RELEASE);
            Require((free.GetPosition() == before), "Freelook accepted retired translation input");
        }
    }

    template<class Camera>
    void CheckDoubleMovementSpeed()
    {
        for (const int movement : { GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_Q })
        {
            Camera normal, fast;
            normal.LookTo(gpu_contract::Float3{0.f}, Direction);
            fast.LookTo(gpu_contract::Float3{0.f}, Direction);
            Key(normal, movement, GLFW_PRESS);
            Key(fast, movement, GLFW_PRESS);
            Key(fast, GLFW_KEY_LEFT_SHIFT, GLFW_PRESS);
            Key(fast, GLFW_KEY_RIGHT_SHIFT, GLFW_PRESS);
            Key(fast, GLFW_KEY_LEFT_SHIFT, GLFW_RELEASE);
            Frames(normal, 60);
            Frames(fast, 60);
            Require(NearlyEqual(fast.GetPosition(), normal.GetPosition() * 2.f),
                "either held Shift must produce exactly twice the keyboard displacement");
            const auto beforeNormal = normal.GetPosition();
            const auto beforeFast = fast.GetPosition();
            Key(fast, GLFW_KEY_RIGHT_SHIFT, GLFW_RELEASE);
            Frames(normal, 30);
            Frames(fast, 30);
            Require(NearlyEqual(fast.GetPosition() - beforeFast, normal.GetPosition() - beforeNormal),
                "releasing both Shift keys must restore normal speed immediately");
        }
    }

    template<class Camera>
    void CheckLeveling(Camera& camera)
    {
        RolledPose(camera);
        const auto originalUp = camera.GetUp();
        Key(camera, GLFW_KEY_V, GLFW_PRESS);
        Require((camera.GetUp() == originalUp), "V snapped the captured pose");
        int crossings = 0;
        float previous = RendererPiF / 4, peak = 0;
        for (int frame = 0; frame < 120; ++frame)
        {
            camera.Animate(.01f);
            const auto up = camera.GetUp();
            const float roll = std::atan2(-up.x, up.y);
            if (std::abs(roll) > 1e-6f)
            {
                if (roll * previous < 0)
                    ++crossings;
                previous = roll;
            }
            peak = std::min(peak, roll);
            Require((camera.GetPosition() == Position) && (camera.GetDir() == Direction),
                "roll leveling moved or redirected the eye");
        }
        Require(crossings == 1 && std::abs(peak / (RendererPiF / 4)) >= .145f &&
            std::abs(peak / (RendererPiF / 4)) <= .155f && (camera.GetUp() == gpu_contract::Float3{0, 1, 0}),
            "roll leveling lost its single bounded overshoot or exact endpoint");
    }

    void CheckRollInput()
    {
        UvsrFirstPersonCamera first(true);
        UvsrThirdPersonCamera third;
        CheckLeveling(first);
        CheckLeveling(third);
        std::array<gpu_contract::Float3, 4> results;
        const std::array<int, 3> rates{ 30, 60, 144 };
        for (size_t i = 0; i < results.size(); ++i)
        {
            UvsrFirstPersonCamera camera(true);
            RolledPose(camera);
            Key(camera, GLFW_KEY_V, GLFW_PRESS);
            if (i < rates.size())
                Frames(camera, rates[i] / 2, 1.f / float(rates[i]));
            else
                for (float duration : { .017f, .041f, .099f, .013f, .137f, .071f, .122f })
                    camera.Animate(duration);
            results[i] = camera.GetUp();
            Require(NearlyEqual(results[i], results[0], 2e-5f), "roll response depends on frame partitioning");
        }
        UvsrFirstPersonCamera control(true), repeated(true), restarted(true);
        for (auto* camera : { &control, &repeated, &restarted })
        {
            RolledPose(*camera);
            Key(*camera, GLFW_KEY_V, GLFW_PRESS);
            camera->Animate(.12f);
        }
        Key(repeated, GLFW_KEY_V, GLFW_REPEAT);
        Key(repeated, GLFW_KEY_V, GLFW_RELEASE);
        Key(restarted, GLFW_KEY_V, GLFW_PRESS);
        Key(restarted, GLFW_KEY_V, GLFW_RELEASE);
        for (auto* camera : { &control, &repeated, &restarted })
            camera->Animate(.18f);
        Require(NearlyEqual(control.GetUp(), repeated.GetUp(), 1e-6f) && !NearlyEqual(control.GetUp(), restarted.GetUp(), 1e-3f),
            "V repeat/release restarted leveling or a new press failed to restart");

        for (int input = 0; input < 3; ++input)
        {
            UvsrFirstPersonCamera camera(true);
            RolledPose(camera);
            camera.MousePosUpdate(640, 360);
            camera.MouseButtonUpdate(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
            camera.Animate(.01f);
            if (input == 1)
                camera.MousePosUpdate(672, 376);
            Key(camera, GLFW_KEY_V, GLFW_PRESS);
            const auto capturedDirection = camera.GetDir();
            camera.Animate(.12f);
            if (input == 2)
            {
                camera.MousePosUpdate(672, 376);
                camera.Animate(.01f);
                const auto moved = camera.GetWorldToViewMatrix();
                camera.MouseButtonUpdate(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
                camera.Animate(.5f);
                Require(!NearlyEqual(camera.GetDir(), capturedDirection) && SameMatrix(camera.GetWorldToViewMatrix(), moved),
                    "new trackpad motion did not cancel pending leveling");
            }
            else
            {
                for (int frame = 0; frame < 120; ++frame)
                {
                    camera.MousePosUpdate(input == 1 ? 672 : 640, input == 1 ? 376 : 360);
                    camera.Animate(.01f);
                }
                const auto direction = camera.GetDir();
                Require((camera.GetPosition() == Position) && (direction == capturedDirection) &&
                    (input == 0 || !NearlyEqual(direction, Direction)) &&
                    NearlyEqual(Dot(camera.GetUp(), gpu_contract::Float3{0, 1, 0}), std::sqrt(1 - direction.y * direction.y), 2e-5f),
                    "stationary trackpad canceled leveling or V lost the queued input pose");
            }
        }
        for (const bool replacePose : { false, true })
        {
            UvsrFirstPersonCamera camera(true);
            RolledPose(camera);
            Key(camera, GLFW_KEY_V, GLFW_PRESS);
            camera.Animate(.12f);
            if (replacePose)
                RolledPose(camera);
            else
            {
                Key(camera, GLFW_KEY_LEFT, GLFW_PRESS);
                camera.Animate(.05f);
                Key(camera, GLFW_KEY_LEFT, GLFW_RELEASE);
            }
            const auto stopped = camera.GetWorldToViewMatrix();
            camera.Animate(.7f);
            Require(SameMatrix(camera.GetWorldToViewMatrix(), stopped), "pose replacement or input failed to cancel leveling");
        }
        first.SetExactPose(Position, gpu_contract::Float3{1e-5f, 1, 0},
            gpu_contract::Float3{HalfDiagonal, -1e-5f * HalfDiagonal, HalfDiagonal},
            gpu_contract::Float3{HalfDiagonal, -1e-5f * HalfDiagonal, -HalfDiagonal});
        Key(first, GLFW_KEY_V, GLFW_PRESS);
        constexpr std::array<float, 5> times{ .003f, .011f, .007f, .019f, .005f };
        for (size_t frame = 0; frame < 140; ++frame)
        {
            first.Animate(times[frame % times.size()]);
            Require(std::isfinite(Length(first.GetUp())) && NearlyEqual(Length(first.GetUp()), 1) &&
                std::abs(Dot(first.GetDir(), first.GetUp())) < 1e-4f, "near-vertical leveling produced an invalid basis");
        }
        Require((first.GetPosition() == Position) && (first.GetDir() == gpu_contract::Float3{1e-5f, 1, 0}) &&
            NearlyEqual(first.GetUp(), gpu_contract::Float3{0, 0, 1}, 2e-5f), "near-vertical leveling changed the captured pose");
    }

    void CheckDolly()
    {
        UvsrThirdPersonCamera camera;
        camera.LookTo(Position, Direction);
        camera.ResetZoomReferenceDistance(10);
        const float step = camera.GetBaseWheelStepDistance();
        camera.MouseScrollUpdate(0, 1);
        Frames(camera, 1);
        Require(camera.GetPosition().z > Position.z && camera.GetPosition().z < Position.z + step * .5f,
            "wheel dolly jumped instead of damping");
        Frames(camera, 180);
        Require(NearlyEqual(camera.GetPosition().z - Position.z, step, 2e-4f) && camera.GetDollyScale() < 1,
            "wheel dolly missed its requested step");

        struct Move { int key; gpu_contract::Float3 direction; };
        for (const auto& move : { Move{ GLFW_KEY_W, Direction }, { GLFW_KEY_S, -Direction },
                 { GLFW_KEY_Q, gpu_contract::Float3{0, 1, 0} }, { GLFW_KEY_E, gpu_contract::Float3{0, -1, 0} },
                 { GLFW_KEY_D, gpu_contract::Float3{-1, 0, 0} }, { GLFW_KEY_A, gpu_contract::Float3{1, 0, 0} } })
        {
            camera.LookTo(Position, Direction, move.key == GLFW_KEY_Q || move.key == GLFW_KEY_E
                ? gpu_contract::Float3{1, 0, 0} : gpu_contract::Float3{0, 1, 0});
            camera.ResetZoomReferenceDistance(10);
            Key(camera, move.key, GLFW_PRESS);
            Frames(camera, 1);
            const auto firstStep = camera.GetPosition() - Position;
            Frames(camera, 1);
            Require(Dot(firstStep, move.direction) > 0 &&
                Dot(camera.GetPosition() - Position - firstStep, move.direction) > Dot(firstStep, move.direction),
                "keyboard movement lost its acceleration");
            Frames(camera, 58);
            const auto displacement = camera.GetPosition() - Position;
            Require(Dot(displacement, move.direction) > 0 &&
                NearlyEqual(displacement, move.direction * Dot(displacement, move.direction)),
                "keyboard movement changed direction or added Cross-axis drift");
            Key(camera, move.key, GLFW_RELEASE);
            const auto releasePosition = camera.GetPosition();
            Frames(camera, 1);
            Require(Dot(camera.GetPosition() - releasePosition, move.direction) > 0, "key release snapped movement to rest");
            Frames(camera, 180);
            Require(std::abs(camera.GetKeyboardDollyVelocity()) < 1e-4f &&
                std::abs(camera.GetKeyboardStrafeVelocity()) < 1e-4f &&
                std::abs(camera.GetKeyboardVerticalVelocity()) < 1e-4f, "released movement never reached rest");
        }
        camera.LookTo(Position, Direction);
        camera.ResetZoomReferenceDistance(10);
        Key(camera, GLFW_KEY_W, GLFW_PRESS);
        Frames(camera, 900);
        Require(camera.GetDollyScale() > 0 && camera.GetDollyScale() < 1 &&
            camera.GetKeyboardDollyVelocity() > 0, "sustained dolly stopped at minimum sensitivity");
        camera.CancelPendingMotion();
        const auto stopped = camera.GetPosition();
        Frames(camera, 30);
        Require((camera.GetPosition() == stopped), "canceling pending motion left residual movement");
        Key(camera, GLFW_KEY_A, GLFW_PRESS);
        Frames(camera, 60);
        const auto resetPosition = camera.GetPosition();
        camera.ResetZoomReferenceDistance(12);
        Frames(camera, 30);
        Require((camera.GetPosition() == resetPosition) && camera.GetKeyboardStrafeVelocity() == 0,
            "movement reference reset retained strafe input");
        for (int notch = 0; notch < 80; ++notch)
            camera.MouseScrollUpdate(0, 1);
        Frames(camera, 240);
        const auto minimum = camera.GetDollyScale();
        const auto before = camera.GetPosition();
        for (int notch = 0; notch < 10; ++notch)
            camera.MouseScrollUpdate(0, 1);
        Frames(camera, 240);
        Require(NearlyEqual(camera.GetDollyScale(), minimum) && Dot(camera.GetPosition() - before, camera.GetDir()) > .02f,
            "wheel dolly stopped at its sensitivity floor");
        const auto reference = camera.GetReferenceZoomDistance();
        const auto wheel = camera.GetBaseWheelStepDistance();
        const auto direction = camera.GetDir();
        camera.ApplyCollisionPosition(gpu_contract::Float3{2, 3, 4});
        Require((camera.GetPosition() == gpu_contract::Float3{2, 3, 4}) && camera.GetReferenceZoomDistance() == reference &&
            camera.GetBaseWheelStepDistance() == wheel && camera.GetDollyScale() == minimum && (camera.GetDir() == direction),
            "collision changed look or dolly state");
    }

    void CheckCollision()
    {
        CameraCollisionWorld world;
        const auto point = [](gpu_contract::Float3 value) { return CameraCollisionWorld::Point{value.x, value.y, value.z}; };
        const auto move = [&](gpu_contract::Float3 start, gpu_contract::Float3 end, float radius)
        {
            const auto value = world.MoveSphere(point(start), point(end), radius);
            return gpu_contract::Float3{value.x, value.y, value.z};
        };
        const auto resolve = [&](gpu_contract::Float3 start, gpu_contract::Float3 movement, float radius)
        {
            const auto value = world.ResolveSphere(point(start), point(movement), radius);
            return gpu_contract::Float3{value.x, value.y, value.z};
        };
        Require((move(gpu_contract::Float3{-1, 2, 3}, gpu_contract::Float3{4, 5, 6}, .25f) == gpu_contract::Float3{4, 5, 6}),
            "empty collision world blocked movement");
        CameraCollisionWorld::Triangle wall[128];
        size_t triangle = 0;
        for (int y = 0; y < 8; ++y)
        for (int z = 0; z < 8; ++z)
        {
            const float y0 = -10.f + float(y) * 2.5f, z0 = -10.f + float(z) * 2.5f;
            wall[triangle++] = {{0, y0, z0}, {0, y0 + 2.5f, z0}, {0, y0 + 2.5f, z0 + 2.5f}};
            wall[triangle++] = {{0, y0, z0}, {0, y0 + 2.5f, z0 + 2.5f}, {0, y0, z0 + 2.5f}};
        }
        Require(world.Build(wall) == CameraCollisionBuildError::None, "collision build failed");
        Require(world.GetTriangleCount() == 128, "collision BVH lost valid triangles");
        for (float radius : { .25f, .5f })
        {
            const auto hit = move(gpu_contract::Float3{-1, 0, 0}, gpu_contract::Float3{1, 0, 0}, radius);
            Require(NearlyEqual(hit.x, -radius, .002f) && NearlyEqual(hit.y, 0) && NearlyEqual(hit.z, 0), "thin wall sweep tunneled or drifted");
            const auto pressed = move(hit, gpu_contract::Float3{1, 0, 0}, radius);
            Require(NearlyEqual(pressed, hit, .002f), "continued wall pressure changed the standoff");
        }
        const auto slide = move(gpu_contract::Float3{-1, -1, 0}, gpu_contract::Float3{1, 1, 0}, .25f);
        Require(NearlyEqual(slide.x, -.25f, .002f) && slide.y > .9f, "wall contact lost tangential sliding");
        Require(NearlyEqual(move(gpu_contract::Float3{-1, 0, 0}, gpu_contract::Float3{-2, .5f, 0}, .25f), gpu_contract::Float3{-2, .5f, 0}),
            "movement away from geometry changed");
        Require((move(gpu_contract::Float3{-1, 0, 0}, gpu_contract::Float3{-1, 0, 0}, .25f) == gpu_contract::Float3{-1, 0, 0}),
            "stationary camera lost its idle position");
        Require(NearlyEqual(resolve(gpu_contract::Float3{-.1f, 0, 0}, gpu_contract::Float3{1, 0, 0}, .25f).x, -.25f, .002f),
            "enlarged mounted hitbox could not repair overlap");
        const auto activation = resolve(gpu_contract::Float3{-.1f, 0, 0}, gpu_contract::Float3{.4f, 0, 0}, .1f);
        Require(NearlyEqual(move(activation, gpu_contract::Float3{.3f, 0, 0}, .1f).x, -.1f, .002f),
            "first activation appeared through the wall");
        const CameraCollisionWorld::Triangle degenerate[1]{};
        Require(world.Build(degenerate) == CameraCollisionBuildError::None,
            "empty collision candidate failed");
        Require(world.Empty(), "degenerate triangles entered collision");
    }
}

namespace
{
    void TraceFloat(float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        std::printf(" %08x", bits);
    }

    template<class Camera>
    void TraceCamera(const char* name, Camera& camera)
    {
        camera.LookTo({3.f, -2.f, 7.f}, {.2f, .1f, 1.f}, {0.f, 1.f, 0.f});
        constexpr float steps[] = {1.f / 120.f, 1.f / 60.f, 1.f / 30.f, 0.f, 1.f / 144.f};
        for (unsigned frame = 0; frame < 160; ++frame)
        {
            switch (frame)
            {
            case 0: camera.MousePosUpdate(100, 200); camera.MouseButtonUpdate(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0); break;
            case 1: camera.MousePosUpdate(113.5, 193.25); break;
            case 2: camera.MousePosUpdate(117.25, 198.5); break;
            case 5: camera.MouseButtonUpdate(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0); break;
            case 8: Key(camera, GLFW_KEY_W, GLFW_PRESS); break;
            case 14: Key(camera, GLFW_KEY_LEFT_SHIFT, GLFW_PRESS); break;
            case 18: Key(camera, GLFW_KEY_RIGHT_SHIFT, GLFW_PRESS); break;
            case 20: Key(camera, GLFW_KEY_LEFT_SHIFT, GLFW_RELEASE); break;
            case 24: Key(camera, GLFW_KEY_LEFT_CONTROL, GLFW_PRESS); break;
            case 28: Key(camera, GLFW_KEY_RIGHT_CONTROL, GLFW_PRESS); break;
            case 30: Key(camera, GLFW_KEY_LEFT_CONTROL, GLFW_RELEASE); break;
            case 32: Key(camera, GLFW_KEY_RIGHT_CONTROL, GLFW_RELEASE); break;
            case 35: Key(camera, GLFW_KEY_RIGHT_SHIFT, GLFW_RELEASE); break;
            case 40: Key(camera, GLFW_KEY_W, GLFW_RELEASE); break;
            case 41: Key(camera, GLFW_KEY_Q, GLFW_PRESS); break;
            case 45: Key(camera, GLFW_KEY_Q, GLFW_RELEASE); Key(camera, GLFW_KEY_E, GLFW_PRESS); break;
            case 49: Key(camera, GLFW_KEY_E, GLFW_RELEASE); break;
            case 50: Key(camera, GLFW_KEY_A, GLFW_PRESS); break;
            case 54: Key(camera, GLFW_KEY_A, GLFW_RELEASE); Key(camera, GLFW_KEY_D, GLFW_PRESS); break;
            case 58: Key(camera, GLFW_KEY_D, GLFW_RELEASE); break;
            case 60: Key(camera, GLFW_KEY_X, GLFW_PRESS); break;
            case 64: Key(camera, GLFW_KEY_X, GLFW_RELEASE); break;
            case 65: Key(camera, GLFW_KEY_V, GLFW_PRESS); break;
            case 66: Key(camera, GLFW_KEY_V, GLFW_REPEAT); break;
            case 67: Key(camera, GLFW_KEY_V, GLFW_RELEASE); break;
            case 85: camera.SetExactPose({3.f, -2.f, 7.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}, {-1.f, 0.f, 0.f}); break;
            case 100: Key(camera, GLFW_KEY_LEFT, GLFW_PRESS); break;
            case 104: Key(camera, GLFW_KEY_UP, GLFW_PRESS); break;
            case 108: Key(camera, GLFW_KEY_LEFT, GLFW_RELEASE); Key(camera, GLFW_KEY_UP, GLFW_RELEASE); break;
            case 110: Key(camera, GLFW_KEY_C, GLFW_PRESS); break;
            case 114: Key(camera, GLFW_KEY_C, GLFW_RELEASE); break;
            case 116: Key(camera, GLFW_KEY_V, GLFW_PRESS); break;
            case 117: Key(camera, GLFW_KEY_V, GLFW_RELEASE); break;
            case 120: camera.MouseButtonUpdate(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0); break;
            case 121: camera.MousePosUpdate(125.75, 182.5); break;
            case 124: camera.MouseButtonUpdate(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0); break;
            case 128: camera.MouseScrollUpdate(0, 1); break;
            case 130: camera.MouseScrollUpdate(0, -1); break;
            case 132: camera.MouseScrollUpdate(0, 2); break;
            case 136: Key(camera, GLFW_KEY_S, GLFW_PRESS); break;
            case 140: Key(camera, GLFW_KEY_S, GLFW_RELEASE); break;
            default: break;
            }
            camera.Animate(steps[frame % 5]);
            std::printf("%s %u", name, frame);
            for (const auto value : {camera.GetPosition(), camera.GetDir(), camera.GetUp()})
            {
                TraceFloat(value.x); TraceFloat(value.y); TraceFloat(value.z);
            }
            const auto& matrix = camera.GetWorldToViewMatrix();
            for (unsigned row = 0; row < 4; ++row)
                for (unsigned column = 0; column < 4; ++column)
                    TraceFloat(matrix.values[row * 4 + column]);
            std::putchar('\n');
        }
    }

    void TraceModes()
    {
        UvsrFirstPersonCamera first;
        UvsrFirstPersonCamera pivot(false);
        UvsrThirdPersonCamera third;
        StaticViewCamera fixed;
        TraceCamera("first", first);
        TraceCamera("pivot", pivot);
        TraceCamera("third", third);
        TraceCamera("static", fixed);
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--trace") == 0)
    {
        TraceModes();
        return 0;
    }
    CheckModesAndKeys();
    CheckDoubleMovementSpeed<UvsrFirstPersonCamera>();
    CheckDoubleMovementSpeed<UvsrThirdPersonCamera>();
    CheckRollInput();
    CheckDolly();
    CheckCollision();
    std::puts("camera acceptance passed");
    return 0;
}
