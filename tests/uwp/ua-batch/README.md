# User activities, jump lists, input capabilities, display requests

Windows 11 probes (unpackaged, C++/WinRT, `cl /std:c++17 /EHsc x.cpp windowsapp.lib runtimeobject.lib`) of the classes
that `dlls/twinapi.appcore` serves from Wine 4b3ed43b and bcb62ad5, and what they showed (2026-11-17):

* `uaprobe.cpp`: KeyboardCapabilities, MouseCapabilities and TouchCapabilities equal the system metrics;
  `UserActivityChannel.GetOrCreateUserActivityAsync("")` is E_INVALIDARG; `SaveAsync` leaves `State` New and the channel
  gives a new activity for the id; `CreateSession` failed with E_ILLEGAL_METHOD_CALL on that machine (the activity
  history may have been off; Wine lets a session publish the activity); `ToJson` has a documented schema Wine does not
  produce; `JumpList` is supported unpackaged, a list never saved has `SystemGroupKind` Recent (2), a saved list loads
  again with its items, separators accept property sets, an empty display name is accepted.
* `dr.cpp`: `DisplayRequest` needs an STA (an MTA construction failed with 8000001d); releasing without a request, a
  third release, and releasing another object's request are all E_ILLEGAL_METHOD_CALL.

The Wine tests are `dlls/twinapi.appcore/tests/activities.c` and `capabilities.c` (x64 and x86).
