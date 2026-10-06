# Commander OS V2 without CEF

Target: `Content/GameCore/H5UI/CommanderOS/V2` should render correctly through
native H5 UI (RmlUi + Slate), without requiring a CEF iframe.

## Plugin-side compatibility work

### 1. Browser CSS normalization

The normalizer is applied to external CSS files and style blocks in HTML:

| Browser CSS | H5 UI result |
| --- | --- |
| Multi-line gradient, color-function, and `calc(...)` declarations | Collapsed before parsing. |
| `background-image: url(...)` | Converted to a native image decorator. |
| `background` / `background-image: linear-gradient(...)` | Converted to a native linear-gradient decorator. |
| Trivial `calc(12px + 4px)` / `calc(12px - 4px)` | Folded to a fixed length. |
| `border: none` / `border: 0` | Converted to zero border width. |
| Dashed and dotted borders | Kept as solid-border approximations. |
| Unsupported browser-only declarations | Removed without discarding the rest of the rule. |

Unsupported declarations removed on the native path include `outline`,
`user-select`, browser `appearance`, `object-fit`, `object-position`,
`background-size`/repeat forms outside the native decorator model, `filter`,
and `backdrop-filter`.

### 2. Thin-line rasterization

Untextured axis-aligned rules, including common 1 px and 2 px dividers and
wallpaper grids, use Slate antialiased lines with:

- A physical-pixel minimum thickness.
- Pixel-center alignment when no CSS transform is active.
- Stable output across fractional DPI and UMG render scales.

Compound meshes retain their subpixel coordinates. Generic half-pixel snapping
is intentionally not applied to these meshes because it can collapse one edge
of a thin border or create mismatched corners after scaling.

### 3. Rounded borders and clipping

Rounded boxes use adaptive corner tessellation, increasing segment density as
the radius grows. This makes circles, pills, and large rounded panels visibly
smoother than the old fixed low-segment geometry.

Native `overflow: hidden` with `border-radius` uses Slate clip masks that follow
the rounded contour. Rectangular scissor clipping remains available for square
regions. Inverse clip masks and Chromium-style offscreen effect layers are not
implemented, so complex mask/filter compositions still need simpler artwork.

## Remaining Chrome differences

Native H5 UI does not promise Chromium parity for:

- CSS Grid; use Flexbox and wrapping.
- Layout-relative `calc(50% - 12px)` and `min()`/`max()`/`clamp()`.
- `backdrop-filter`, complex CSS filters, and blend-mode compositions.
- Repeated background-size tiling and multi-layer scanline tricks.
- Radial/conic gradient shader parity.
- Inline SVG DOM/effects, Canvas, and WebGL. External SVG image files are supported.
- Complex inset or stacked shadows; prefer one simple drop shadow.

## Authoring checklist

1. Prefer Flexbox instead of Grid.
2. Use solid 1-2 px horizontal and vertical rules for critical separators.
3. Use complete browser border shorthand, for example `border: 1px solid #d4cec0`.
4. Prefer one simple `box-shadow` instead of double-ring or inset stacks.
5. Use explicit image dimensions instead of relying on `object-fit`.
6. `overflow: hidden` uses stable rectangular clipping. Do not rely on rounded child-content clipping yet; `border-radius` still rounds the element's own background and border.
7. Use external SVG image resources or textures instead of inline SVG effects.

## Rebuild note

Rebuild `H5UIPlugin` after native renderer changes. Documentation and page-only
changes require only an H5 UI View reload.
