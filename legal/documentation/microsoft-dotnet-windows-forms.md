# Microsoft .NET and Windows Forms

## Record

- Relationship: Dependency Integration
- Status: Retired launcher runtime, historical distribution record
- Confidence: Confirmed
- Upstream: [.NET Runtime 10.0.11](https://github.com/dotnet/runtime/tree/v10.0.11) and [Windows Forms 10.0.11](https://github.com/dotnet/winforms/tree/v10.0.11)
- Build SDK: .NET SDK `10.0.400`, formerly pinned by `launcher/global.json`
- Runtime Boundary: `Microsoft.NETCore.App.Runtime.win-x64` and `Microsoft.WindowsDesktop.App.Runtime.win-x64` `10.0.11`; Windows SDK reference `10.0.22000.57`
- Governing Terms: the SDK's Microsoft .NET Library license and complete third-party notices

## UVSR Relationship

the former managed `uvsr-launcher.exe` was a self-contained, untrimmed, single-file Windows Forms
application for `net10.0-windows10.0.22000.0` and `win-x64`. it embedded its .NET
and Windows Forms runtime instead of requiring a machine-wide .NET install.

the historical build also embedded the pinned SDK's `LICENSE.txt` (9,519 bytes, SHA-256
`7f6839a61ce892b79c6549e2dc5a81fdbd240a0b260f8881216b45b7fda8b45d`)
and `ThirdPartyNotices.txt` (78,887 bytes, SHA-256
`deb4427a295e1ed474b0d81c5a0d972c1b550b9a715cda939cdfa9236b1b418f`).
the historical launcher's **Notices** window displayed both resources. they remain inside
the single executable; the renderer ZIP does not duplicate them.

## Evidence

- managed source is recoverable from commit `c4dbcb31c2a241f5864cb08ae2f80551ef2615fb`.
- the exact later dirty source uses the [AA/launcher recovery archive](../../docs/postmortem/taa.md#removal-and-recovery).
- [native successor build and notices](../../launcher/README.md#build-and-verification)

the native successor does not link or package .NET or Windows Forms. preserve
the old license resources when redistributing a historical managed artifact.

## Commercial Clearance

Preserve the complete embedded Microsoft license and third-party notices in
every distributed managed launcher containing those components. A self-contained build does not relicense the
embedded runtime under UVSR's project terms.
