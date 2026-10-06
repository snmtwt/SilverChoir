# Tactical UI arrows

## Current production assets (2026-10-05)

- ArrowUp.svg / ArrowDown.svg: editable geometric originals, white polygon with a transparent background. Down is the exact 180-degree counterpart of Up.
- ArrowUp.png / ArrowDown.png: 512 x 512 RGBA raster exports for Unreal's existing T_ArrowUp and T_ArrowDown assets.
- All visible RGB values are exactly (255,255,255). Coverage is stored only in alpha, including antialiased boundaries, so the widget's runtime color tint is authoritative.
- Each export has 58,152 visible pixels, 203,992 fully transparent pixels and 720 antialiased pixels.

The final SVG paths are the complete reproducible source. The output was rasterized with sharp. Previous PNGs were backed up under Saved/RaisedImageButton/SourceBackup before replacement.

## Design generation

Two upward-arrow candidates were generated with the built-in image_gen tool in transparent mode. Their alpha fringes did not meet production quality, so neither was imported. No fallback image API was used. The simple production arrows were authored as vector geometry instead of retouching those generated bitmaps.

Prompt used for the final generated candidate (not imported):

> Create one production-ready minimalist UP ARROW icon as a flawless mathematical flat 2D silhouette. Use case logo-brand UI alpha mask. Square transparent canvas. A single solid pure white #FFFFFF polygon: triangular arrowhead at top joins short rectangular shaft below. Centered with generous uniform transparent margins, overall icon width 60 percent of canvas and height 70 percent. Completely clean ruler-straight lines, sharp corners, symmetric, SVG-like vector rasterization, perfect uninterrupted edges and completely uniform solid white interior. This is a utilitarian compact 20px game HUD icon, not a painting. No ornament. No distressed or broken edges, NO stray pixels or flecks, NO grain, NO paper texture, NO surface texture, NO shading, NO gradients, NO gray, NO off-white, NO blue, NO cyan, NO glow, NO shadow, NO outlines, NO frame, NO button, NO background, NO words. Only white arrow on true alpha transparency. All visible RGB is white. Anti-aliasing at clean boundaries uses alpha only. Straight UP direction at 12 o'clock.
