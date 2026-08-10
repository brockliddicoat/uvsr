# Incorporated Code Samples

This directory contains upstream source incorporated into UVSR. These files are
compiled product inputs, not merely tutorial snippets and not first-party UVSR
code.

## Intel CMAA2

[`intel-cmaa2/CMAA2.hlsl`](intel-cmaa2/CMAA2.hlsl) is pinned from Intel's CMAA2
2.3 implementation with documented UVSR integration hooks. It remains governed
by Intel's [Apache License 2.0](../licenses/Intel-CMAA2-Apache-2.0.txt), with a
Microsoft MIT notice for the Microsoft-derived portion. See the
[Intel CMAA2 source record](../documentation/intel-cmaa2.md).

Keeping incorporated code here makes the boundary visible: UVSR's public
license does not replace upstream terms, copyright notices, or attribution.
