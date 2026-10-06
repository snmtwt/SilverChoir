# H5 UI Native HTML and CSS Compatibility

H5 UI is a native Unreal game UI renderer backed by RmlUi and QuickJS-NG. It
accepts common HTML5 authoring patterns and browser-style CSS, but it is not a
complete Chromium implementation.

In this SilverChoir integration, CEF is retained as an optional, lazily loaded
module. `H5 UI View > Browser > Enable CEF IFrames` defaults to **false**.
While disabled, the view does not allocate a browser canvas, scan for iframe
subviews on each tick, or create a browser instance. The native main menu
explicitly keeps it disabled. Enable it on another view only when using the
iframe capability described below; this does not change the top-level native
document into a browser document.

## Standard animation syntax added in this integration

The native parser accepts `linear` for both `animation` and `transition`,
`s` and `ms` time units, and animation shorthand directions `normal`,
`reverse`, `alternate`, and `alternate-reverse`. `running` is recognized as
the initial play state. Animation names can appear before or after these
keywords. Reverse playback uses the same keyframes and interpolation engine,
including alternating iterations; it does not require JavaScript.

```css
.orbit { animation: orbit 24s linear infinite; }
.counter-orbit { animation: orbit 15000ms linear infinite reverse; }
.panel { transition: opacity 250ms linear 50ms; }
```

This is not full CSS Animations parity: fractional iteration counts, zero
duration, browser fill modes, arbitrary timing functions, and the complete
set of animation longhands are not covered by this extension. The existing
native pause behavior restarts when the animation property changes. Keep
unsupported properties out of native keyframes; the shadow guard described
below still applies.

This document uses four compatibility levels:

| Level | Meaning |
| --- | --- |
| Native | Parsed, laid out, and rendered directly by RmlUi and Slate. |
| Normalized | Browser syntax is converted to an equivalent native form before parsing. |
| Approximate | The visual or behavioral result is useful, but may not be pixel-identical to Chrome. |
| Unsupported | Do not depend on this feature on the native H5 UI path. |

## HTML5 document support

Standard `.html` documents are normalized to RML at load time. `<!doctype
html>` is accepted, `<html>` is mapped to the RML document root, and common
HTML void elements are self-closed automatically. This covers `area`, `base`,
`br`, `col`, `embed`, `hr`, `img`, `input`, `link`, `meta`, `param`, `source`,
`track`, and `wbr`.

The document must still be structurally well formed after normalization.
Mismatched closing tags and malformed nesting are rejected instead of being
repaired with Chromium's HTML error-recovery algorithm.

### Document and layout tags

```text
html head body link title
div section article header footer main nav aside
span p h1 h2 h3 h4 h5 h6
ul ol li dl dt dd
table thead tbody tr td th
figure figcaption blockquote cite
pre code hr address
details summary
```

These tags are native document elements with RmlUi layout, CSS, text, and event
behavior. Only standard HTML tag names are part of the public markup contract;
the plugin does not add application-specific custom elements.

### Forms, links, images, and media

```text
form fieldset legend
button input textarea select option label output
img progress video
a
```

Supported input types include `text`, `password`, `checkbox`, `radio`, `range`,
`number`, `email`, and `color`. Text controls support `placeholder`. `date` and
`file` do not provide browser-native picker dialogs. `select` has a native
value region, arrow, and option popup. `video` is backed by Unreal Media
Framework.

Raster images are supported through normal resource URLs. External SVG files
used by image elements are parsed with NanoSVG and rasterized to premultiplied
BGRA at up to four times their logical resolution, preserving transparency and
sharpness during UI scaling. Inline SVG DOM, SVG scripting, filters, animation,
and browser SVG CSS are not part of the compatibility contract.

`input type="color"` accepts `#rgb` and `#rrggbb` and opens a native preset
palette. The generated palette can be styled with these native selectors:

```css
input[type="color"] colorpicker { width: 180px; padding: 8px; gap: 5px; }
input[type="color"] coloroption { width: 26px; height: 26px; border-radius: 50%; }
input[type="color"] colorvalue { border-radius: 4px; }
```

### Semantic text tags

```text
small strong em b i u s mark
sub sup time abbr kbd samp
```

These tags are safe for semantic text and CSS styling. Provide explicit CSS
when their browser default appearance matters.

## CSS support matrix

| Area | Level | Supported behavior |
| --- | --- | --- |
| Selectors | Native | Type, class, ID, attribute, descendant/child, state, and structural selectors. |
| Media queries | Native | Width, height, aspect ratio, resolution, orientation, and theme queries with `not`/`and`. |
| Box model | Native | Width/height, min/max size, margin, padding, border, box sizing, and z order. |
| Layout | Native | Block, inline, inline-block, none, Flexbox, static/relative/absolute/fixed positioning, float, and clear. |
| Flexbox | Native | Direction, wrap, grow/shrink/basis, order, gap, alignment, and justification. |
| Typography | Native | Font family/style/weight/size, line height, kerning, letter spacing, alignment, decoration, transform, whitespace, and word break. |
| Color and opacity | Native | Named/hex colors, `rgb`/`rgba`, `hsl`/`hsla`, background color, image tint, visibility, and opacity. |
| Borders | Native/approximate | Width, color, shorthand, per-corner radius, percentage radius, and antialiased thin rules. Dashed/dotted styles become solid. |
| Overflow | Native | Visible, hidden, auto, scroll, text clipping/ellipsis, and stable rectangular clips. |
| Transforms | Native | Translate, scale, rotate, skew, matrix, origin, and perspective-supported RmlUi transforms. |
| Motion | Native/limited | RmlUi transitions and keyframe animations are available for supported properties. Shadow interpolation and browser preference media queries are excluded from the stable native path. |
| Generated decoration | Normalized | Empty-content `::before` and `::after` boxes are represented by internal generated elements for static and script-created DOM nodes. Textual generated content is not supported. |
| Background images | Native/normalized | URL images and linear gradients. Common browser declarations are converted to RmlUi decorators. |
| Shadows/effects | Approximate | Basic box-shadow rendering. Chromium filter stacks and offscreen compositing are unavailable. |
| Scrolling/input | Native | Mouse, wheel, focus, keyboard text entry, selection, clipboard, and native form controls. |

Common supported length units are `px`, `dp`, `em`, `rem`, `vw`, `vh`, and `%`.
Property-specific unitless numbers and angle units are accepted where the
underlying RmlUi property supports them.

Common state selectors include `:hover`, `:active`, `:focus`, `:checked`, and
`:disabled`. Structural selectors include `:first-child`, `:last-child`,
`:first-of-type`, `:last-of-type`, `:only-child`, `:only-of-type`, `:empty`,
`:nth-child(...)`, `:nth-last-child(...)`, `:nth-of-type(...)`,
`:nth-last-of-type(...)`, `:not(...)`, and `:scope`.

Do not invent application pseudo states such as `:selected`, `:open`,
`:dragging`, `:hot`, or `:occupied`; expose those states through classes.

## Browser CSS normalization

`NormalizeCssForRml` runs on external stylesheets and style blocks in HTML
documents before RmlUi parses them.

| Browser CSS input | Native H5 UI result |
| --- | --- |
| Multi-line `linear-gradient`, `radial-gradient`, `repeating-linear-gradient`, `rgb/rgba`, `hsl/hsla`, or `calc` | Function body is collapsed so RmlUi receives one declaration. |
| `background-image: url(...)` | Converted to a native image decorator. |
| `background-image/background: linear-gradient(...)` | Converted to a native linear-gradient decorator. |
| Radial or repeating gradients | Parsed through decorator normalization; renderer parity is approximate and should not be relied on for critical visuals. |
| `calc(12px + 4px)` or `calc(12px - 4px)` | Folded to a fixed pixel length. |
| `animation-delay: 0.16s` or `320ms` | Applied as a native animation longhand and overrides the corresponding delay from `animation`. |
| `selector::before/::after { content: ''; ... }` | Rewritten to an internal generated decoration element; the remaining box styles apply normally. |
| `border: none`, `border: 0`, `border-bottom: none` | Converted to zero border width. |
| `border: 1px dashed ...` or `dotted` | Accepted and rendered as a solid border approximation. |
| `background: 0 0`, `background: transparent` | Converted to a transparent background. |
| `min-height: auto` | Removed so the remaining rule stays valid. |
| `background-size: cover/contain/100%/auto`, centered `background-position`, `background-repeat` | Removed; image placement uses the supported native decorator behavior. |
| `object-fit`, `object-position` | Removed; size the image element explicitly instead. |
| `outline`, `outline-offset`, `user-select`, `appearance`, `caret-color`, `touch-action`, `will-change` | Removed while preserving other declarations in the rule. |
| `filter`, `backdrop-filter` | Removed because the Slate path has no Chromium offscreen filter pipeline. |
| `font: inherit`, `font-family: inherit` and related inherited font longhands | Removed; normal RmlUi property inheritance supplies the parent value. |
| `!important` | Marker removed; native stylesheet specificity and source order decide the result. |
| Keyword `transform-origin` values such as `top left` or `bottom center` | Converted to percentage origins. |
| `writing-mode`, `scrollbar-color`, `scrollbar-width` | Removed; use ordinary horizontal text and the native scrollbar appearance. |
| `@media (prefers-reduced-motion: ...)` | Entire query block removed; expose a game setting/class when reduced motion is required. |
| A transition list containing `box-shadow` | Only the unsupported `box-shadow` item is removed; safe width, opacity, transform, color, and other native transition items remain active. |
| Stylesheet containing both `@keyframes` and `box-shadow` declarations | Animation declarations are removed as a stability guard. RmlUi 6.2 cannot interpolate shadows safely on the Slate path, and an infinite shadow animation can monopolize the game/editor thread. |

Thin untextured, axis-aligned one-pixel rules are submitted through Slate's
stable line path. Compound geometry keeps subpixel coordinates so adjacent border
edges do not collapse during scaling. Rounded corners use adaptive high-density
tessellation. Overflow inside translated or scaled elements is converted from
RmlUi clip-mask geometry to a transformed Slate clipping rectangle, including
nested intersection. Overflow regions propagate through the DOM ancestor chain,
so ordinary-flow, inline, and positioned descendants share the same clip. As a
final safety layer, submitted vertices are constrained to the active rectangular
clip before Slate batching, covering text, images, status bars, and nested item
slots consistently. Exact rounded, rotated, perspective, and inverse clip masks
remain unsupported; their axis-aligned rectangular bounds are used where
possible.

See [COMMANDEROS_H5UI_SUPPORT.md](COMMANDEROS_H5UI_SUPPORT.md) for the rendering
rules used by the Commander OS V2 compatibility target.

## JavaScript and DOM scope

### Native Canvas 2D subset (2026-10-03)

`<canvas>` now has its own native bitmap and a stable `getContext('2d')` object.
It uses QuickJS for page code, a C++ rasterizer for paths, and one persistent
Unreal texture per bitmap size. Dirty pixels are uploaded at paint time, at most
once per painted frame; static canvases do not upload continuously. No CEF view
or browser process is created. CSS sizing remains separate from bitmap size;
use `canvas.width/height` and `setTransform(dpr,0,0,dpr,0,0)` for screen density.

Implemented APIs:

- `beginPath`, `moveTo`, `lineTo`, `closePath`, `rect`, `bezierCurveTo`,
  `quadraticCurveTo`, `arc`, and `stroke` with antialiased lines.
- `fillRect` and partial `clearRect`, including the current transform. Clearing
  restores transparent black and ignores global alpha/compositing.
- CSS solid colors and `createLinearGradient` / `addColorStop`. Canvas comma-form
  `rgba()` uses browser alpha 0–1 (or percentages), adapting RmlUi's legacy
  integer-alpha parser.
- `lineWidth`, butt/round `lineCap`, round joins, `globalAlpha`, and
  `source-over` / `lighter` composition.
- `save` / `restore`, `setTransform(a,b,c,d,e,f)`, `resetTransform`,
  `transform`, `translate`, `scale`, and `rotate`.
- Bounded `getImageData` with RGBA `Uint8ClampedArray` output.

Setting width/height, including assigning the same value, resets pixels, path
and context state. Different canvases retain independent pixels and state.
Recursive `requestAnimationFrame` calls are eligible only on the next runtime
update, not later in the same update. These lifecycle and bitmap semantics are
based on the [HTML Canvas specification](https://html.spec.whatwg.org/multipage/canvas.html).

This is a focused game-UI subset, not complete browser Canvas compatibility.
Arbitrary path fill/clip, `drawImage`, text drawing, radial gradients, shadows,
filters, image writing/export, `Path2D`, `OffscreenCanvas` and WebGL are not
implemented. Stroke joins are round; nonuniform/skewed stroke width uses an
approximate average scale. Negative-size `getImageData` is not supported.
Canvas bitmap dimensions are capped at 2048 per axis and 1,048,576 pixels total;
JS size properties reject larger allocations, while HTML attributes are bounded.
Paths are capped at 16,384 points, state saves at 128, gradient stops at 64 and
pixel reads at 262,144 pixels. Use small surfaces for frequently animated HUDs.

The SilverChoir ECG page (`Content/UI/ECG/ecg.html`) demonstrates a scan head,
an erase gap, fading history, gradient traces and translucent stroke layers for
glow. It does not depend on unsupported Canvas shadow filters. Its bitmap is
rebuilt only on viewport resize; color/rate/intensity changes update the live
page without reloading. `H5UIPlugin.Canvas.*` covers the native API and ECG
parameter behavior.

Classic inline scripts and external scripts execute in an isolated QuickJS-NG
context. The plugin provides the DOM/event subset needed by native game UI,
including element lookup, attributes, classes, dataset, inline style, text and
form values, tree mutation, standard mouse/keyboard/form events, promises, and
timers. Vue 3 is supported through the bundled H5UI custom renderer.

This is not a browser security or Web Platform environment. Browser navigation,
service workers, Web Components, HTML modules, browser storage, and unrestricted
network APIs are not compatibility promises unless a plugin bridge explicitly
provides them.

## Drag model

H5 UI does not implement browser HTML5 Drag and Drop (`draggable`,
`dataTransfer`, or browser payload transfer). For movable windows, use
`H5UI.drag` with normal mouse events and `getBoundingClientRect`:

```html
<section class="app-window" data-h5ui-move-root>
  <header data-h5ui-move-handle>Title</header>
  <button type="button" data-h5ui-no-move>X</button>
</section>
```

Primary helpers are `bind`, `bindTree`, `pin`, and `setArmed`. RmlUi's native
`drag: none | drag | drag-drop | block | clone` property remains available for
advanced game UI targets, but it is not browser-compatible HTML5 drag behavior.

## Unsupported or limited features

| Feature | Status / alternative |
| --- | --- |
| CSS Grid | Unsupported; use Flexbox and wrapping. |
| `position: sticky` | Unsupported; update fixed/absolute positions in page code. |
| Layout-relative `calc(50% - 12px)` | Unsupported; use Flexbox, percentages, or precomputed values. |
| CSS `min()`, `max()`, `clamp()` | Unsupported. |
| CSS custom-property parity | Not guaranteed; use generated concrete values for shared themes. |
| `object-fit`, advanced background sizing/repeat | Unsupported on the normalized native path; size elements explicitly. |
| Complex/inset/multi-layer shadows | Approximate; prefer one simple drop shadow. |
| Rounded, rotated, perspective, or inverse overflow masks | Approximate; translated/scaled rectangular overflow is supported, while complex mask geometry is reduced to axis-aligned bounds. |
| `box-shadow` transitions or keyframe animation | Unsupported on the stable native path. Keep shadows static and animate `opacity` or `transform` in a stylesheet that does not contain shadow keyframes. |
| Multi-property browser transition lists involving `visibility` | Not portable; toggle a class and change visibility/opacity directly. |
| `currentColor` inside border, background, shadow, or generated icon declarations | Not guaranteed; write an explicit color value. |
| `@media (prefers-reduced-motion)` | Unsupported; use an Unreal/game setting reflected as a document class. |
| Browser scrollbar styling and `writing-mode` | Unsupported; use native scrollbars and normal horizontal layout. |
| `filter`, `backdrop-filter`, blend modes | Unsupported without an offscreen layer compositor. |
| Canvas | Native 2D subset described above; advanced browser APIs are unavailable. |
| WebGL | Unsupported. |
| Inline SVG DOM and SVG effects | Unsupported; use an external SVG image or a texture. |
| Browser dialogs and native date/file pickers | Unsupported; build native styled elements and toggle classes. |
| HTML5 Drag and Drop | Unsupported; use `H5UI.drag` or RmlUi drag. |

When `Enable CEF IFrames` is explicitly enabled, one visible `<iframe>` per H5 UI view is supported experimentally through UE's
CEF-backed `SWebBrowser`. Its rectangle participates in native layout while its
contents render as a Slate child above the native document. Local
`coui://uiresources/` pages and HTTP(S) URLs are accepted. CSS transforms,
rounded clipping, opacity groups, and native elements painted above the iframe
are not composited with the browser surface. Native RmlUi content currently
uses rectangular overflow clipping as described above.
