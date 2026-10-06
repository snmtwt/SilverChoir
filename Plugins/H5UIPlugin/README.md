# H5 UI Plugin

H5 UI Plugin is a non-CEF game UI runtime for Unreal Engine 5.8. RmlUi 6.2 owns document parsing, CSS layout, animation, forms, and geometry generation; QuickJS-NG runs page JavaScript in an isolated per-view context. The plugin submits geometry directly to Slate and keeps video frames in Unreal Media Framework textures.

This is a game UI runtime, not a general-purpose web browser. Its native path is designed so that increasing the viewport from 1080p to 4K does not create a four-times-larger browser bitmap or a matching CPU upload.

## Current feature set

- `H5 UI View` UMG widget with Gameface-style `coui://uiresources/` URLs.
- Native Slate custom-vertex rendering with premultiplied alpha, textures, transforms, and stable rectangular clipping.
- DPI-aware font atlases rasterize glyphs at the Slate/UMG pixel scale while keeping CSS layout dimensions unchanged.
- CSS box model, structural/state selectors, media queries, Flexbox (including `order` and direct-text items), typography, images, percentage border radius, transforms, transitions, and animations supported by the native renderer. Browser CSS normalization accepts common border, background, gradient, and trivial fixed-pixel `calc()` forms. Shadow animation is intentionally disabled because RmlUi 6.2 cannot interpolate it safely on the Slate path. Standard HTML buttons receive browser-like centered text defaults.
- HTML document compatibility for common structural, form, media, semantic-text, and document-content tags. Interactive pages use native HTML elements plus ordinary page JavaScript.
- Mouse, keyboard text entry, focus navigation, selection, and clipboard operations.
- Blueprint-to-UI values, legacy UI events, and typed per-view H5UI event handlers.
- Inline and external classic JavaScript, a game-UI DOM facade, events, promises, timers, and a constrained Unreal bridge.
- Vue 3 single-file components through the bundled H5UI custom renderer and Vite authoring workspace.
- Native `<video>` elements backed by `UMediaPlayer` and `UMediaTexture`.
- Active/idle update scheduling and per-view performance statistics.
- Automatic non-UFS staging for the default `Content/UI` directory and plugin demo resources.

## Standards and compatibility status

| Area | Current contract |
| --- | --- |
| HTML5 markup | Common structural, semantic, form, image, link, table, and media elements. Browser void tags are normalized; remaining markup must be well formed. |
| CSS layout | Box model, block/inline layout, Flexbox, static/relative/absolute/fixed positioning, overflow, scrolling, and responsive media queries. CSS Grid and sticky positioning are not supported. |
| CSS visuals | Typography, color, opacity, images, linear gradients, borders, adaptive rounded corners, rectangular overflow clipping, transforms, transitions, animations, and basic shadow approximation. |
| Browser CSS input | Common declarations are normalized where an equivalent RmlUi form exists. Dashed/dotted borders become solid; unsupported filter/object-fit/background-tiling declarations are removed. |
| JavaScript | QuickJS-NG with the H5 UI DOM/event facade, timers, promises, external classic scripts, UE bridge, and Vue custom renderer. It is not the complete browser Web Platform. |
| Browser surface | One experimental CEF `<iframe>` per view. It is composited above the native document and has separate transform/clip/opacity limitations. |

The exact support and fallback matrix is maintained in
[HTML_CSS_COMPATIBILITY.md](Docs/HTML_CSS_COMPATIBILITY.md).

## Quick start

1. Add `H5 UI View` to a Widget Blueprint.
2. Set `URL` to `coui://uiresources/hud.html` for `Content/UI/hud.html`.
3. Add the Widget Blueprint to the viewport as usual.
4. Bind `On Ready For Bindings`, set initial values, and call `Synchronize Models`.
5. For legacy pages, bind `On UI Event`. For typed page events, configure `Event Handler Classes` on the target `H5 UI View` and import `sdk/h5ui-bridge.js`.

Typed `H5UI.emit(...)` calls are sent only to their configured handler. `On UI Event`
is reserved for legacy untyped `window.ue.emit(...)` calls.

The bundled `h5ui://plugin/demo.rml` page is the default URL and can be used as a first-run check.

For a full feature check, load `h5ui://plugin/showcase.html`. It includes native layout, forms, keyboard input, data binding, UE events, external JavaScript, dynamic DOM, timers, image loading, animation, scrolling, performance fields, and local video playback. The exact empty-level setup is available as the standalone [TESTING.html](Docs/TESTING.html) guide, with [TESTING.md](TESTING.md) retained as a source-friendly fallback.

The bundled Vue check is `h5ui://plugin/vue-basic/vue-basic.html`. It demonstrates a Vue 3 SFC, a child component, `v-for`, `v-if`, computed state, text input, and click-driven reactive updates.

## Resource URLs

| URL | Filesystem location |
| --- | --- |
| `coui://uiresources/hud.html` | `<Project>/Content/UI/hud.html` |
| `h5ui://plugin/demo.rml` | `<Plugin>/Resources/UI/demo.rml` |
| `ueasset:///Game/UI/Icon.Icon` | An Unreal `UTexture` asset rendered directly through Slate |
| `file:///...` | Disabled unless `bAllowAbsoluteFilePaths` is enabled |

Relative CSS, image, and media paths are resolved from the document URL. Attempts to escape a resource root with `../` are rejected.

Documents may use `.html`, `.rml`, `.css`, or `.rcss` extensions. Markup must be valid RML/XHTML-style markup; malformed browser-tolerated HTML is outside the native compatibility contract.

For stylesheets that must also render in a browser, use standards-complete shorthands such as `border: 1px solid #245a7d`. The native parser accepts the `solid` token for browser compatibility; omitting it relies on legacy RmlUi border semantics and browsers compute the border style as `none`.

See [HTML_CSS_COMPATIBILITY.md](Docs/HTML_CSS_COMPATIBILITY.md) for the supported tag/property matrix, extended component lifecycle, state classes, and the native renderer's remaining boundaries.

For AI-generated pages, provide [AI_HTML_GENERATION_GUIDE.md](Docs/AI_HTML_GENERATION_GUIDE.md) to the generation model before it writes markup.

## Blueprint API

- `Load URL`, `Load HTML String`, `Reload`, and `Close` control document lifetime.
- New document loads, including `Reload`, invalidate cached external stylesheets and templates before parsing, so edits made to UI files on disk are visible without restarting the editor.
- `RenderScale` and `Set HTML Render Scale` apply an additional `0.5`-`4.0` raster-resolution multiplier after automatic Slate/UMG screen-density detection. Values of `2.0`-`3.0` are the usual quality/performance range; `4.0` provides maximum text supersampling for small or fractionally scaled views. Newly created H5 UI View widgets default to `1.0`; existing Widget Blueprint assets retain their serialized value until changed.
- `Dispatch HTML Event` sends a named custom event and optional detail string/JSON directly from Blueprint to the page document/window. Reactive Vue updates are flushed before the call returns.
- `Set Data String`, `Set Data Number`, and `Set Data Boolean` update the view model.
- `Set Page Data (Struct)` accepts any Blueprint structure, serializes it to JSON as `pageData`, and dispatches `H5UIPageData` when the page is already loaded. Texture and soft-texture properties are exported as `ueasset://` URLs automatically.
- Native adapter plugins can use `DispatchHtmlEventFromStruct` to send the same reflected-structure JSON (including texture URL conversion) as a transient event without replacing persistent `pageData`.
- `Synchronize Models` applies queued values to bound elements.
- `Focus Element By Id` moves keyboard focus to a form control.
- `Execute JavaScript` runs code in the loaded view and returns its final string or JSON-compatible result.
- `Get Performance Stats` reports update, submit, and JavaScript time; JavaScript heap/timer counts; batches, vertices, triangles, clip masks, and viewport size.
- `On Ready For Bindings`, `On Load Failed`, `On JavaScript Error`, and `On UI Event` expose view lifecycle, script errors, and UI events.

To trigger a page transition from Blueprint, call `Dispatch HTML Event` with an event name such as `SilverUIEnter`. Page code receives it through the standard event API:

```javascript
window.addEventListener('SilverUIEnter', event => {
  playEnterEffect(event.detail)
})
```

The bundled Silver Choir start menu exposes three screen-state events through the same Blueprint node:

| Event name | Result |
| --- | --- |
| `SilverUIBeforeEnter` | Immediately switches to and holds the hidden pre-entrance frame. Call this at game startup. |
| `SilverUIEnter` | Plays or replays the entrance animation, then holds the visible entered state. |
| `SilverUIAfterLeave` | Immediately switches to and holds the hidden post-exit frame. |

Clicking New Game plays the exit animation and automatically settles in `SilverUIAfterLeave`; no second Blueprint call is required. The optional `Detail` input may be left empty for all three events.

### Silver Choir base control HUD

The in-game base HUD is loaded with `coui://uiresources/BaseControl/base-control.html`. Its center remains transparent for the 3D scene, while reusable Vue components provide scene navigation, camera targets, currency, and strategic time controls.

Page-to-Unreal events received through `On UI Event`:

| Event name | Payload |
| --- | --- |
| `BaseHUDReady` | `{ scene, location, language }` |
| `BaseSceneChanged` | `{ scene, location }` |
| `CameraFocusRequested` | `{ scene, location }` |
| `TimeControlChanged` | `{ paused, speed }` where speed is `0`, `1`, `5`, `10`, or `20` |
| `OpenBaseMenuRequested` | `{ scene, location }` when the icon button beside the upper-right currency panel is pressed |

Unreal-to-page events sent through `Dispatch HTML Event`:

| Event name | Detail |
| --- | --- |
| `SilverBaseHUDState` | JSON state containing any of `language`, `scene`, `location`, `currency`, `currencyDelta`, `currencyIcon`, `paused`, `speed`, and `time` |
| `H5UIPageData` | The JSON generated by `Set Page Data (Struct)`; the page also reads the same JSON from `window.ue.getData('pageData')` during initialization |
| `SilverBaseHUDLanguage` | `zh-CN`, `en-US`, or JSON such as `{ "language": "en-US" }` |

For example, send this as the `Detail` of `SilverBaseHUDState`:

```json
{
  "scene": "haven",
  "location": "kitchen",
  "currency": 128460,
  "currencyDelta": 2340,
  "currencyIcon": "ueasset:///Game/Resources/Texture/Currency/val-currency-symbol-white.val-currency-symbol-white",
  "paused": false,
  "speed": 1,
  "time": { "month": 9, "day": 18, "hour": 21, "minute": 40 }
}
```

For a Blueprint-native workflow, create a structure with the same fields (for example `Currency`, `CurrencyDelta`, `CurrencyIcon`, and a nested `Time` structure), connect it to `Set Page Data (Struct)`, and leave `Event Name` as `H5UIPageData`. Unreal property names are exported in JSON form (`CurrencyIcon` becomes `currencyIcon`). The value is safe to send either before or after the page has finished loading.

## Resolution and scale

`DefaultSize` is the widget's desired logical layout size, not a fixed backing-buffer resolution. At runtime the native renderer derives the actual screen-pixel scale from the widget's final Slate render transform and calculates:

```text
EffectivePixelRatio = clamp(max(ScreenPixelScale, 1.0) * RenderScale, 1.0, 4.0)
RenderSize = CSS viewport size * EffectivePixelRatio
```

The lower clamp keeps one raster pixel per CSS pixel at minimum. A parent UMG downscale no longer cancels explicit supersampling, so `RenderScale = 3.0` still selects a 3x font atlas below a `1.0` screen scale. The extra scale changes raster resource density without changing CSS layout dimensions. Higher values consume more glyph-atlas memory and upload bandwidth, so use `3.0` first and reserve `4.0` for very small text or presentation captures. Runtime code and Vue pages can read `window.innerWidth`, `window.innerHeight`, `window.devicePixelRatio`, `window.silverRenderScale`, `window.silverEffectivePixelRatio`, `window.silverRenderWidth`, and `window.silverRenderHeight`. The same values are available through `ue.getData()` under `viewport.width`, `viewport.height`, `viewport.devicePixelRatio`, `viewport.renderScale`, `viewport.effectivePixelRatio`, `viewport.renderWidth`, and `viewport.renderHeight`.

## UE bridge attributes

```html
<input
  id="player-name"
  data-ue-model="player.name"
  data-ue-event="PlayerNameChanged"
  data-ue-event-type="change"
/>

<span data-bind="player.name"></span>

<button data-ue-event="StartGame" data-ue-payload="campaign">
  Start
</button>
```

- `data-bind` writes a Blueprint model value into an element's text content.
- `data-ue-model` provides two-way form value synchronization.
- `data-ue-event` declares the event name sent through `On UI Event`.
- `data-ue-event-type` defaults to `click`.
- `data-ue-payload` provides a static payload. Form controls send their current value instead.

## JavaScript

Classic inline scripts and external `<script src="...">` files execute after the native document has loaded. External files use the same `h5ui://plugin/` or `coui://uiresources/` root security as CSS and media resources.

```html
<button id="launch">Launch</button>
<span id="state">Idle</span>
<script src="hud.js"></script>
```

```javascript
const state = document.getElementById('state');

document.getElementById('launch').addEventListener('click', () => {
  state.textContent = 'Launching';
  state.classList.add('active');
  window.ue.emit('LaunchRequested', { mode: 'campaign' }, 'launch');
});

window.ue.setData('session.status', 'Ready');
console.log(window.ue.getData('player.name'));
```

### Typed event handlers

For new HTML-to-Unreal calls, create a Blueprint class derived from
`H5UI_EventHandler`, then select the target `H5 UI View` and add it under
**Events > Event Handler Classes** in that View's Details panel. The map key is
supplied by HTML to select exactly one handler instance for that `H5 UI View`:

```html
<script src="sdk/h5ui-bridge.js"></script>
```

```javascript
H5UI.emit('Inventory', 'MoveItem', 'player-bag', 12);
```

The selected handler receives `MoveItem(FString InventoryId, int32 SlotIndex)`.
Event handler instances are created when a view becomes ready and released on
close, reload, and destruction. `Dispatch HTML Event` remains the Unreal-to-
HTML mechanism. See [H5UI_EVENT_HANDLER.md](Docs/H5UI_EVENT_HANDLER.md) for
the full setup, supported parameter types, and high-frequency event guidance.
If no handler is configured for a typed event, the event retains its `EventType`
and falls back to the view's `OnUIEvent` Blueprint delegate and native global
event surface instead of being discarded.

The current facade covers element queries, attributes, `classList`, `dataset`, inline styles, text/value/checked state, parent/children traversal, DOM creation/removal, focus, click and custom events. It also provides `setTimeout`, `setInterval`, `requestAnimationFrame`, promises, `queueMicrotask`, `console`, and `performance.now`.

Each view has its own memory limit and uninterrupted execution budget. Scripts do not receive filesystem, socket, `fetch`, XHR, or browser navigation globals. Disable `bEnableJavaScript` on a `H5 UI View` when a document should be declarative only.

## Vue 3

`WebSDK/vue` provides a Vue 3 custom renderer for the H5UI DOM facade. It keeps Vue's component model and reactivity while sending host operations to the native RmlUi/Slate document instead of `runtime-dom` owning a browser page.

For Vue SFC development, install and build the included workspace:

```powershell
cd Plugins/H5UIPlugin/WebSDK
pnpm install
pnpm build
```

The example uses `createH5UIApp` in place of Vue DOM's `createApp`:

```typescript
import { createH5UIApp } from '@h5ui-plugin/vue'
import App from './App.vue'

createH5UIApp(App).mount('#app')
```

The build produces classic IIFE scripts because ES modules are not supported by the page runtime yet. The standalone `h5ui://plugin/sdk/h5ui-vue.global.js` bundle exposes the same SDK as `H5UIVue` for pages authored without a bundler.

Vue core features and compiled SFC templates are supported. Packages that directly require a full browser DOM, Canvas, WebGL, networking, or `runtime-dom`-specific behavior remain outside the compatibility contract.

## Video

```html
<video src="coui://uiresources/movies/intro.mp4" autoplay loop></video>
```

The native element supports `src`, `autoplay`, `loop`, and `muted`. Local files are opened with `UMediaPlayer::OpenFile`; stream URLs are passed to `OpenUrl`. Playback format and protocol support follow the Media Framework player plugins enabled for the target platform.

Video frames stay in a `UMediaTexture` and are referenced by Slate. They are not copied into a full-page browser surface. Active playback requests the target view frame rate even when the rest of the document is idle.

## Input

Mouse and ordinary keyboard text input are implemented. Clicking a form control focuses it, and Blueprint can call `Focus Element By Id`. Copy, cut, and paste use Unreal's platform clipboard interface.

Gamepad, touch, and full IME composition are not implemented in this milestone. Basic Unicode characters delivered through Slate `OnKeyChar` are accepted, but a production Chinese/Japanese/Korean composition window needs a dedicated `ITextInputMethodContext` adapter.

## Project settings

Open **Project Settings > Plugins > H5 UI Plugin**.

| Setting | Purpose |
| --- | --- |
| `ResourceDirectory` | Directory below project `Content` used by `coui://uiresources/` |
| `FallbackFonts` | Project-relative or absolute fallback TTF/OTF files |
| `ActiveUpdateRate` | Default view rate while input, animation, or video is active |
| `IdleUpdateRate` | Maximum polling rate for a static document |
| `IdleAfterSeconds` | Active grace period after input or model changes |
| `JavaScriptMemoryLimitMB` | Independent heap limit for each view; default `16` MB |
| `JavaScriptExecutionTimeLimitMilliseconds` | Maximum uninterrupted script/callback time; default `50` ms. This is a runaway-script watchdog, not a per-frame animation budget. |
| `JavaScriptInitialExecutionTimeLimitMilliseconds` | One-time watchdog budget for each page script to initialise bundled frameworks; default `500` ms |
| `JavaScriptMaxCallbacksPerFrame` | Timer and promise callback cap per view per frame; default `100` |
| `bAllowAbsoluteFilePaths` | Opt-in access to absolute local files |
| `EventHandlerClasses` | Fallback event type to `H5UI_EventHandler` class routes created per H5 UI View |

## Packaging

The default `Content/UI` directory and plugin demo resources are staged as non-UFS files by the module rules. If `ResourceDirectory` is changed, add the custom directory under **Packaging > Additional Non-Asset Directories to Package**.

The plugin can be validated and packaged independently with:

```powershell
& "$env:UE_ROOT\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin `
  -Plugin="<Project>\Plugins\H5UIPlugin\H5UIPlugin.uplugin" `
  -Package="<Output>\H5UIPlugin" `
  -TargetPlatforms=Win64 `
  -Rocket
```

## Performance contract

- The native backend does not use CEF or a full-view pixel buffer.
- DOM/layout updates are capped per view and static documents drop to the idle rate.
- RmlUi compiles document geometry; Slate submission scales with draw geometry rather than pixel count.
- Images and font atlases are cached Slate resources.
- Video remains in Media Framework textures.
- JavaScript execution is event/dirty driven and bounded by per-view time, memory, and callback limits.
- `Get Performance Stats` makes view cost visible to game code and profiling tools.

On the included demo, the UE 5.8 Win64 offscreen D3D automation test measured approximately `0.0133 ms` at 1080p and `0.0126 ms` at 4K for a native update. The 4K CPU render submission measured approximately `0.1541 ms`, with `19` batches and `1072` vertices. The same geometry counts were asserted at both resolutions. These are local baseline numbers for a small page, not a guarantee for arbitrary UI complexity or GPU cost.

## Current limits

- No ES modules/import maps, `fetch`/XHR/WebSocket, browser navigation, or arbitrary UObject reflection yet.
- The DOM surface is a game-UI compatibility facade, not the complete WHATWG browser DOM.
- Experimental single `<iframe>` subview backed by UE CEF; the browser instance is created only while the element is visible.
- The native DOM has no Canvas, WebGL, or networking stack; those capabilities exist only inside the opt-in CEF iframe.
- No CSS Grid.
- No rounded clip masks, blurred shadows, backdrop filters, or general offscreen layer effects yet.
- No gamepad, touch, or full IME composition yet.

See [ROADMAP.md](ROADMAP.md) for the staged compatibility plan. The native View and Blueprint API are intentionally separated from future script, layer-compositor, and browser-subview backends.

## Verification

The automation test is registered as:

```text
H5UIPlugin.Runtime.NativeSmoke
```

It covers resource-root security, document/stylesheet/external-script loading, browser font stacks, DPI-specific font rasterization with stable CSS dimensions, percentage border radii, Flexbox `order` and anonymous direct text, centered native button labels, common transform shorthands, JavaScript DOM mutation, Vue SFC mounting and reactive updates, events, JSON payloads, timers, UE data exchange, runaway-script interruption and recovery, model synchronization, keyboard text entry, 1080p/4K updates, real Slate geometry submission, resolution-independent geometry counts, and clean context shutdown.

## Third-party software

RmlUi 6.2 is included under the MIT License. Its license is stored at `Source/H5UIPluginRml/Private/RmlUi/LICENSE.txt`.

QuickJS-NG 0.15.1 is included as the official amalgam under the MIT License. Its license is stored at `Source/H5UIPluginQuickJS/Private/QuickJS/LICENSE.txt`.
