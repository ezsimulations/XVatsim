# XVatsim Manager

XVatsim Manager is a separate Windows application for guided installation,
online update, repair, compatibility checking, and rollback. It is not loaded
into X-Plane or xPilot and does not participate in XVatsim runtime decisions.

At startup the Manager reads the public `docs/xvatsim_update.json` manifest. A
newer release may advertise a direct HTTPS Manager payload with its exact byte
length, SHA-256, and minimum compatible Manager version. The payload must pass
those checks plus the existing ZIP schema, safe-path, inventory, and per-file
verification before it can be offered. Installation remains user-selected and
uses the same transaction and rollback path as the embedded payload.

The payload embedded in the executable remains the offline fallback. It cannot
be used to overwrite an installation whose receipt records a newer version.

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

Generated working files are written under `manager/generated`. The standalone
executable and the separately publishable
`XVatsim_<version>_Manager_Payload.zip` are written under
`manager/artifacts`. Both directories are intentionally excluded from Git.
