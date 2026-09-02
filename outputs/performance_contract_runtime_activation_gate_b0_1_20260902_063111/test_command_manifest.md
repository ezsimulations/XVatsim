# Runtime Activation Gate B0.1 final offline commands

Build directory: `build/performance-contract-runtime-activation-gate`

Release build:

```powershell
cmake --build build/performance-contract-runtime-activation-gate --config Release --target XVatsimPlugin XVatsimRegressionHarness --parallel
```

Focused probes:

```powershell
XVatsimRegressionHarness.exe --performance-contract-gate-a
XVatsimRegressionHarness.exe --performance-contract-gate-a-calm-1
XVatsimRegressionHarness.exe --performance-contract-gate-a-calm-2
XVatsimRegressionHarness.exe --product-calm-1
XVatsimRegressionHarness.exe --performance-contract-gate-b
XVatsimRegressionHarness.exe --performance-contract-gate-b-telemetry
XVatsimRegressionHarness.exe --runtime-activation-gate
```

Step 6 PDC:

- Enumerated the 61 sorted
  `v2_step6_pdc_private_messages_*.scn` scenarios.
- Ran each through the exact regression harness.
- Required 61/61.

Frozen regression:

- Enumerated all 861 sorted `.scn` files.
- Ran each as `XVatsimRegressionHarness.exe <scenario> --route-worker-parity`.
- Required 861/861.

Parity:

- Selected the exact 64 scenarios containing `resolver.route_resolve=true`.
- Ran route-only, authority-only, and combined authority/route parity.
- Required 64/64 for each set.

Static acceptance:

```powershell
git diff --check
```
