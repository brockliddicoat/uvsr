// Converts the previous GI bounce's GPU continuation flag into the indirect
// argument for the next bounce. This keeps convergence entirely on the GPU:
// no continuation signal means a zero-group dispatch and therefore no further
// full-screen tracing work.
RWByteAddressBuffer u_BounceContinuation : register(u0);
RWByteAddressBuffer u_BounceIndirectArguments : register(u1);

[numthreads(1, 1, 1)]
void main()
{
    uint continueTracing = 0u;
    u_BounceContinuation.InterlockedExchange(
        0u, 0u, continueTracing);

    const uint multiplier = continueTracing != 0u ? 1u : 0u;
    const uint3 maximumDispatch = u_BounceIndirectArguments.Load3(12u);
    u_BounceIndirectArguments.Store3(
        0u, maximumDispatch * multiplier);
}
