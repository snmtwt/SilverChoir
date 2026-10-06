# AI Native HTML Generation Guide

Read this file before generating H5 UI markup. Generate valid RML/XHTML-style
HTML using only standard HTML tags. Do not invent custom element names.

## Required rules

1. Emit one `<html>` root with `<head>` and `<body>`.
2. Close every tag and quote every attribute value.
3. Use Flexbox for layout. Do not generate CSS Grid.
4. Use `coui://uiresources/` or `h5ui://plugin/` for local resources.
5. For Unreal messages, import `sdk/h5ui-bridge.js` and use
   `H5UI.emit(EventType, FunctionName, ...Arguments)`. EventType must be a key
   in the target H5 UI View's `Event Handler Classes` map.
6. Provide all interaction state with standard tags, classes, and page
   JavaScript. Do not use `H5UIComponents`.

## Allowed standard tags

```text
html head body link title
div section article header footer main nav aside
span p h1 h2 h3 h4 h5 h6
ul ol li dl dt dd
table thead tbody tr td th
figure figcaption blockquote cite pre code hr address
details summary
form fieldset legend
button input textarea select option label output
img progress video a svg
small strong em b i u s mark sub sup time abbr kbd samp
dialog
```

Use `svg` only as an accepted DOM tag. Use `img` or an Unreal texture URL
for visible artwork.

## Native interaction recipes

- Use `input type="radio"` and `label` for a single-choice group.
- Use `input type="checkbox"` and `label` for multiple choices.
- Use `select` and `option` for a dropdown.
- Use `input type="password"` for secrets.
- Use `input type="color" value="#5ed1a8"` for a native preset colour palette.
  The popup can be themed with the generated `colorpicker`, `coloroption`, and
  `colorvalue` CSS selectors; never write those elements in HTML.
- Use `placeholder="..."` on empty `input` and `textarea` fields to provide
  an in-field prompt. It is native renderer text, not a JavaScript fallback.
- Use `dialog` plus a CSS class and click listeners for an overlay. Do not
  call browser-only `showModal()`.
- Use a button and sibling span for a styled tooltip.
- Use a div and CSS animation events for animation state.

## Unreal event bridge

### Required routing contract

The game normally configures typed HTML-to-Unreal routes in the target **H5 UI
View > Events > Event Handler Classes** Details-panel map:

```text
TMap<FName, TSoftClassPtr<UH5UI_EventHandler>>
```

Each map entry has exactly one event-type key and one Blueprint/C++ handler
class. Every `H5 UI View` creates its own instance of every configured handler
when its document becomes ready. Never assume that a handler instance is shared
by two H5 UI widgets. The similarly named project-settings map is a fallback
for views without a local entry for that event type.

Some C++ adapter plugins dynamically register a handler on a specific H5 UI
view instead of using the Details-panel map. Dynamic registration has priority
over both View and project-settings entries for its event type. The HTML
contract is unchanged: use the event type documented by that adapter as the
first `H5UI.emit` argument. Never generate configuration markup or attempt to
create an Unreal handler from HTML.

When more than one entry exists, HTML **must** provide the event-type key as the
first argument. Do not infer a handler from the function name, DOM id, page URL,
or payload. For example, these routes are distinct even if both handler classes
have a function named `Open`:

```text
Inventory -> BP_InventoryH5EventHandler
HUD       -> BP_HudH5EventHandler
```

```javascript
H5UI.emit('Inventory', 'Open', 'player-bag');
H5UI.emit('HUD', 'Open', 'mission-panel');
```

Import the bridge before page JavaScript:

```html
<script src="sdk/h5ui-bridge.js"></script>
<script src="page.js"></script>
```

Use the configured event-type key first, then the Blueprint custom-event or
C++ UFUNCTION name, followed by positional arguments:

```javascript
H5UI.emit('Inventory', 'MoveItem', 'player-bag', 12);
```

This calls `MoveItem(FString InventoryId, int32 SlotIndex)` on the handler
configured for `Inventory`. Do not use `window.ue.emit` in newly generated
pages; it is retained only for legacy `On UI Event` integrations. For the full
configuration and type rules, read [H5UI_EVENT_HANDLER.md](H5UI_EVENT_HANDLER.md).

### Parameter rules

`H5UI.emit` serializes all arguments after FunctionName as an ordered array.
Generate the argument count, order, and primitive type to match the Unreal
function exactly.

| JavaScript value | Unreal parameter |
| --- | --- |
| `'player-bag'` | `FString`, `FName`, or `FText` |
| `12` | `int32`, `int64`, `float`, or `double` |
| `true` | `bool` |

Do not pass object literals, arrays, DOM elements, callbacks, `undefined`, or
event objects to `H5UI.emit`. Convert complex data to explicitly agreed
primitive fields, or serialize it yourself into a string when the Unreal
function expects `FString`.

```javascript
// Correct: MoveItem(FString InventoryId, int32 SlotIndex)
H5UI.emit('Inventory', 'MoveItem', 'player-bag', 12);

// Incorrect: object and Event are not reflected parameter types.
H5UI.emit('Inventory', 'MoveItem', { inventory: 'player-bag' }, event);
```

### UE-to-HTML direction

Do not generate a second reverse bridge. Unreal continues to use `Dispatch
HTML Event`, and page code listens through the standard event API or `H5UI.on`:

```javascript
H5UI.on('InventoryChanged', function (event) {
  refreshInventory(event.detail);
});
```

### High-frequency input

Do not emit an Unreal event for every `mousemove`, `pointermove`, `scroll`, or
animation frame. Keep continuous visual updates inside HTML. Emit only a final
commit event, a state transition, or a throttled/coalesced update at a deliberate
rate. This preserves the O(1) typed route lookup and avoids unnecessary
Blueprint execution.

## CSS

Allowed property families include layout and size, box model, typography,
Flexbox, backgrounds, transforms, transitions, animation, and `image-color`.
Use `dp`, `em`, `rem`, `vw`, `vh`, `%`, `inherit`, `initial`, and
`unset`.

Use `:hover`, `:active`, `:focus`, `:checked`, `:disabled`,
`:first-child`, `:last-child`, `:nth-child(n)`, and `:not(...)`.
Do not generate `:selected`, `:open`, `:dragging`, `:valid`,
`:invalid`, `:hot`, or `:occupied`.

## Do not generate

Do not generate custom elements including `drag-area`, `drag-cell`,
`drag-item`, `handle`, `radio-group`, `radio-option`,
`checkbox-group`, `checkbox-option`, `dropdown`, `dropdown-option`,
`password`, `modal`, `modal-title`, `modal-footer`, `tooltips`,
`tooltips-content`, or `animation-watch`.

Also avoid iframe, Canvas, WebGL, browser navigation, fetch, XHR, WebSocket,
CSS Grid, position sticky, calc(), min(), max(), and full SVG artwork.
Do not generate browser HTML5 Drag and Drop (`draggable`, `dataTransfer`).

For movable windows/panels use the first-class free-move model instead:

```html
<section data-h5ui-move-root class="app-window">
  <header data-h5ui-move-handle>Title</header>
  <button type="button">Close</button>
</section>
```

```javascript
H5UI.drag.bindTree(root, {
  canStart: function (windowEl) { return !windowEl.classList.contains('is-maximized'); }
});
```

`H5UI.drag` uses standard mouse events (works in H5 UI and desktop browser
preview). Do not invent custom elements such as `drag-area` / `drag-item`.

For detailed renderer limits, read
[HTML_CSS_COMPATIBILITY.md](HTML_CSS_COMPATIBILITY.md).
