# Typed H5 UI Event Handlers

Use typed event handlers when a page needs to invoke Blueprint or C++ functions
in Unreal. This is a one-way HTML-to-Unreal route. Unreal-to-HTML calls remain
`Dispatch HTML Event` and `Execute JavaScript` on `H5 UI View`.

## Configure routes

1. Create a Blueprint class derived from `H5UI_EventHandler`, or a C++ class
   derived from `UH5UI_EventHandler`.
2. Add Blueprint custom events or `UFUNCTION`s to that class. The custom-event
   name is the function name used by HTML.
3. Select the target **H5 UI View** and add entries to its
   **Events > Event Handler Classes** map in the Details panel. The map key is
   the event type and the map value is the handler class. This is a per-view
   configuration.

Each `H5 UI View` creates one handler instance for each configured map entry
when its document becomes ready. `On H5 UI Started` receives the owning view
and the map key. `On H5 UI Stopped` is called when the view closes, reloads, or
is destroyed.

Every handler also has a Blueprint-visible **Context Object**. It defaults to
the owning `H5 UI View` when the handler is bound. Game code or Blueprint may
replace it with any `UObject` by calling **Set H5UI Event Handler Context**;
handler logic retrieves it with **Get H5UI Event Handler Context** and casts it
to the project-specific type.

The project-level **Project Settings > Plugins > H5 UI Plugin > Events > Event
Handler Classes** map remains available as a fallback for every view. A
per-view entry overrides the project-level entry with the same key. A handler
registered by `Register H5UI Event Handler` takes precedence over both.

## Dynamic C++ registration

Adapters can create a handler object themselves and register it on the concrete
view. `RegisterEventHandler` immediately assigns the handler's `GetH5UIView()`
and event-type key. It activates `On H5 UI Started` immediately for a ready
view, or when a loading view becomes ready. Dynamic routes take precedence over
a Project Settings entry with the same key.

```cpp
USampleH5UIEventHandler* Handler = NewObject<USampleH5UIEventHandler>(H5UIView);
Handler->Configure(MySubsystem); // Adapter-specific initialization.
H5UIView->RegisterEventHandler(TEXT("Sample"), Handler);
```

The registered object survives document reloads on that same `UH5UI_View` and
receives a stop/start lifecycle transition around the reload. Call
`UnregisterEventHandler` to remove it explicitly; H5UI destruction removes all
dynamic handlers automatically. A handler object can be registered to only one
H5UI view at a time.

For example, configure this map:

| Event type | Handler class |
| --- | --- |
| `Inventory` | `BP_InventoryH5EventHandler` |
| `HUD` | `BP_HudH5EventHandler` |

## HTML bridge

Import the bundled bridge before page code:

```html
<script src="sdk/h5ui-bridge.js"></script>
<script src="hud.js"></script>
```

Call the bridge as follows:

```javascript
H5UI.emit('Inventory', 'MoveItem', 'player-bag', 12);
```

This resolves the `Inventory` entry in `Event Handler Classes`, then invokes
`MoveItem` on that handler. A matching Blueprint custom event or C++ UFUNCTION
uses parameters in the same order:

```text
MoveItem(FString InventoryId, int32 SlotIndex)
```

The bridge serializes its remaining arguments as an array. Supported input
types are `FString`, `FName`, `FText`, `bool`, integer types, and floating-point
types. Argument count and type must match exactly. Unsupported parameters,
missing functions, or missing event-type keys are reported in the Unreal log
and do not invoke a function.

`H5UI.emit` returns `true` when the native bridge accepts the event and `false`
when it is unavailable, such as when the same page is opened in a normal
browser. For high-frequency inputs, throttle or coalesce events in page code;
do not emit every mouse-move sample into Blueprint.

## Compatibility

The older `window.ue.emit(Name, Payload, ElementId)` remains available and
continues to broadcast `On UI Event`. It is intentionally untyped and does not
select an `H5UI_EventHandler`. Typed `H5UI.emit(...)` calls are delivered only
to their selected handler and do not broadcast `On UI Event` or the global
native UI-event delegate. New pages should use `H5UI.emit`.

To listen for Unreal-to-HTML events through the bridge, use:

```javascript
const unsubscribe = H5UI.on('InventoryChanged', function (event) {
  applySnapshot(event.detail);
});
```

The game sends that event with `Dispatch HTML Event` exactly as before.
