# Third Party Notices

UVSR uses optional third party components under their own terms. Those terms
are not replaced by the UVSR license.

## NVIDIA NRD

An optional build can incorporate NVIDIA Real Time Denoisers version 4.17.3
from commit `792eff196afdd350fd9c3f862119017ccb438a0e`.

This software contains source code provided by NVIDIA Corporation.

NRD is licensed under the NVIDIA RTX SDK License Agreement. When NRD is
enabled, the exact license fetched with the pinned source is installed as
`licenses/NRD-LICENSE.txt` with the executable. The upstream license is also
available at:

<https://github.com/NVIDIA-RTX/NRD/blob/792eff196afdd350fd9c3f862119017ccb438a0e/LICENSE.txt>

Anyone distributing an NRD enabled application must independently satisfy the
RTX SDK License Agreement, including its application functionality,
protective terms, incorporated distribution, notice, and attribution
requirements. Including this notice or the license file does not by itself
satisfy every distribution obligation.

## AMD FidelityFX SDK

UVSR's first-party stochastic screen space and hybrid reflection implementation
is adapted from the FidelityFX SDK version 1.1.4 reflection samples at commit
`c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`.

This file is part of the FidelityFX SDK. Copyright (C) 2024 Advanced Micro
Devices, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions: The above copyright notice and this
permission notice shall be included in all copies or substantial portions of
the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

The pinned upstream license is available at:

<https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55/LICENSE.txt>
