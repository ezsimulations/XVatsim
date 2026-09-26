# XVatsim / xPilot 4 Plugin SDK Technical Questionnaire

Prepared: 2026-09-24
Project: XVatsim
Purpose: Read-only enhancement integration for xPilot; not a replacement pilot
client and not an independent VATSIM network connection.

Thank you for allowing us to build against the xPilot 4 Plugin SDK. XVatsim is
an X-Plane plugin that presents controller, radio, weather, and message guidance.
Our architecture requires source adapters to forward complete observations to a
central decision engine (the "Brain"). The xPilot companion will not transmit to
VATSIM, select controllers, classify messages, or suppress data.

## 1. SDK compatibility and lifecycle

1. Is `net10.0` the intended target framework for third-party xPilot 4 plugins?
2. How should a plugin declare the minimum compatible xPilot/SDK version, and
   how can it detect an incompatible host before subscribing to events?
3. While the SDK is beta, will breaking changes receive a new assembly/API
   version or another machine-readable compatibility marker?
4. Is `IPlugin.Initialize(IBroker)` called exactly once per xPilot process? Can
   plugins be reloaded without restarting xPilot?
5. Is `IBroker.SessionEnded` the required cleanup boundary? Should handlers be
   unsubscribed there, and is there any other unload/dispose callback?
6. What happens when a plugin throws during construction, initialization, or an
   event callback? Is the plugin isolated or can it affect the xPilot client?

## 2. Private-message and PDC event semantics

7. Does `PrivateMessageReceived` fire once for every incoming private message
   displayed by xPilot, including controller PDC/ACARS-style messages?
8. Does it also fire for automated/system private messages, and are any incoming
   private messages filtered before the event is raised?
9. Is `From` always the original network sender callsign/identifier? Can it be
   blank, aliased, or changed by xPilot?
10. Is `Message` delivered verbatim, including line breaks, spacing, Unicode,
    and long payloads? What are its maximum length and encoding guarantees?
11. Are events delivered in network/display order? Can duplicate delivery,
    replay, loss, or reordering occur during reconnect, plugin reload, or client
    restart?
12. Are messages received before plugin initialization ever replayed, or should
    the bridge treat startup as a new observation epoch?
13. On which thread are broker events raised? Must handlers return immediately,
    and is copying an event into a bounded in-memory/file/IPC queue supported?

## 3. Network state and identity

14. If the plugin initializes while xPilot is already connected, does it receive
    a current-state `NetworkConnected` event or only the next transition?
15. Does `NetworkConnectedEventArgs.Callsign` always represent the active pilot
    identity used for incoming private-message routing?
16. What ordering is guaranteed between `NetworkConnected`, private-message,
    `NetworkDisconnected`, and `SessionEnded` events?

## 4. Controller and radio-board evidence

XVatsim may later use xPilot controller events as one additional evidence source
for its Brain. It will not treat a worker event as a display decision.

17. When a plugin initializes or the network connects, are all currently known
    controllers emitted through `ControllerAdded`, or only later additions?
18. Do `ControllerAdded`, `ControllerDeleted`,
    `ControllerFrequencyChanged`, and `ControllerLocationChanged` represent the
    same controller set shown in xPilot's nearby-controller/radio display?
19. Does xPilot expose the exact current nearby-controller board or snapshot, or
    must a plugin reconstruct a snapshot from controller lifecycle events?
20. Are station aliases, controller type/role, range, visibility, and any xPilot
    nearby/relevance result available through the SDK? If not, are additional
    read-only fields/events planned?
21. Please confirm frequency units and coordinate semantics, plus any cases where
    frequency or location can be unknown/defaulted.

## 5. Packaging and distribution

22. What is the supported Windows plugin folder layout, file naming convention,
    dependency layout, and discovery rule?
23. Is a manifest, signing step, registration, or allow-list required now or
    planned before xPilot 4 stable?
24. May the XVatsim installer place its companion plugin directly into xPilot's
    supported plugin directory, with user consent?
25. May XVatsim redistribute the SDK assembly under its MIT License by including
    the copyright/license notice, or should each plugin build/embed the SDK
    reference in another preferred way?
26. Can plugin dependencies conflict with xPilot or other plugins? Does xPilot
    provide an isolated assembly load context per plugin?
27. How should a plugin publish its own version, and where are load failures or
    compatibility errors recorded for support diagnostics?

## 6. Beta coordination

28. Is there a preferred channel for notification of SDK changes and beta builds
    that need integration retesting?
29. Would you be willing to review a minimal proof-of-concept that only subscribes
    to read-only events and forwards neutral observations to XVatsim?
30. After technical validation, would you be open to discussing XVatsim as an
    optional officially listed xPilot enhancement while keeping both projects'
    ownership and responsibilities clear?

Answers may be brief. Links to existing documentation or source are welcome.
