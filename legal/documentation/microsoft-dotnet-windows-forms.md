# Microsoft .NET and Windows Forms

## Record

- Relationship: Dependency Integration
- Status: Current launcher runtime
- Confidence: Confirmed
- Upstream: [.NET Runtime 10.0.11](https://github.com/dotnet/runtime/tree/v10.0.11) and [Windows Forms 10.0.11](https://github.com/dotnet/winforms/tree/v10.0.11)
- Build SDK: .NET SDK `10.0.400`, pinned by `launcher/global.json`
- Runtime Boundary: `Microsoft.NETCore.App.Runtime.win-x64` and `Microsoft.WindowsDesktop.App.Runtime.win-x64` `10.0.11`; Windows SDK reference `10.0.22000.57`
- Governing Terms: the SDK's Microsoft .NET Library license and complete third-party notices

## UVSR Relationship

`uvsr-launcher.exe` is a self-contained, untrimmed, single-file Windows Forms
application for `net10.0-windows10.0.22000.0` and `win-x64`. It embeds its .NET
and Windows Forms runtime instead of requiring a machine-wide .NET install.

The build also embeds the pinned SDK's `LICENSE.txt` (9,519 bytes, SHA-256
`7f6839a61ce892b79c6549e2dc5a81fdbd240a0b260f8881216b45b7fda8b45d`)
and `ThirdPartyNotices.txt` (78,887 bytes, SHA-256
`deb4427a295e1ed474b0d81c5a0d972c1b550b9a715cda939cdfa9236b1b418f`).
The launcher's **Notices** window displays both resources. They remain inside
the single executable; the renderer ZIP does not duplicate them.

## Evidence

- [Pinned SDK](../../launcher/global.json)
- [Self-Contained Project and Embedded Notices](../../launcher/src/UVSR.Installer/UVSR.Installer.csproj)
- [Notice Display](../../launcher/src/UVSR.Installer/MainForm.cs)
- [Pinned Build Gate](../../launcher/build.ps1)

## Commercial Clearance

Preserve the complete embedded Microsoft license and third-party notices in
every distributed launcher. A self-contained build does not relicense the
embedded runtime under UVSR's project terms.
