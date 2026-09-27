# XVatsim Manager

XVatsim Manager is a separate Windows application for guided installation,
repair, compatibility checking, and rollback. It is not loaded into X-Plane or
xPilot and does not participate in XVatsim runtime decisions.

Build the self-contained test executable from the repository root:

```powershell
$plugin = Resolve-Path '.\build\v2.1.0-release\dist\XVatsim\win_x64\XVatsim.xpl'
$pluginHash = (Get-FileHash $plugin -Algorithm SHA256).Hash
.\scripts\build_xvatsim_manager.ps1 `
    -ProductVersion '2.1.0' `
    -PluginPath $plugin `
    -ExpectedPluginSha256 $pluginHash
```

The required hash argument prevents an unverified plugin build from being
embedded accidentally. Release builds should also pass the expected bridge
hash after its focused probe succeeds.

Generated payloads and executables are written under `manager/generated` and
`manager/artifacts`; both are intentionally excluded from Git.
