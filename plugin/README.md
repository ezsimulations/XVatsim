# XVatsim Plugin

This folder contains the in-sim X-Plane plugin host for XVatsim.

## Role in the system

The plugin should stay thin.

It exists to:

- load inside X-Plane
- host the visible overlay surface
- expose simulator hooks
- forward observations to the brain
- execute brain-approved actions such as tuning

Decision logic should live in the brain, not in ad hoc plugin code.
