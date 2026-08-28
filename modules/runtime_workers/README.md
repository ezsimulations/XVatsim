# Async fact worker host

This is the composition boundary for removable asynchronous fact adapters. It
owns adapter lifetime and exposes abstract brain worker bindings. It does not
select work, schedule requests, interpret facts, or publish presentation.
