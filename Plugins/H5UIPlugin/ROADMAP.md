# H5 UI Plugin Roadmap

The long-term target is a Gameface-like Unreal workflow with a native high-performance default and narrowly scoped compatibility fallbacks. Each phase keeps the existing `UH5UI_View` and Blueprint surface stable.

## 0.1 Native foundation - complete

- RmlUi document, CSS, layout, animation, forms, and font integration.
- Direct Slate geometry and texture submission.
- UMG widget, URL loader, data bridge, events, keyboard input, and clipboard.
- Media Framework `<video>` element.
- Active/idle scheduling, performance stats, packaging rules, and automation tests.
- Isolated UE 5.8 Win64 Editor, Development, and Shipping `BuildPlugin` validation.

## 0.2 Script runtime - foundation complete

- Complete: embed QuickJS-NG as a separately owned runtime module.
- Complete: classic inline/external scripts, DOM queries/mutation, attributes, classes, dataset, styles, events, promises, timers, and animation frames.
- Complete: `window.ue.emit/getData/setData`, Blueprint `Execute JavaScript`, script error events, and JavaScript performance counters.
- Complete: per-view memory limits, execution budgets, callback caps, teardown, runaway-script interruption, and recovery tests.
- Complete: Vue 3 custom renderer, SFC/Vite authoring workspace, classic IIFE output, and a native reactive example.
- Remaining: typed UObject registration, Promise-based Blueprint/C++ calls, fetch hooks, ES modules, and developer tooling.

Current acceptance: interactive application pages and Vue components can own local UI state without Blueprint handling every change, and an uninterrupted runaway script is forcefully returned to UE within its configured budget. Typed object exposure and browser-style networking remain future work.

## 0.3 Native layer compositor

- Add RDG-backed temporary layers for masks and filter chains.
- Implement rounded clipping, gradients/shaders, shadows, blur, and opacity groups.
- Cache effect outputs until their inputs are dirty.
- Add GPU timing and transient-memory counters to performance stats.

Acceptance: the supported CSS visual suite renders correctly at 1080p and 4K without changing DOM/layout cost with pixel count; pixel-based effects report their separate GPU cost.

## 0.4 Browser subview

- Foundation complete: one visible `<iframe>` can host an `SWebBrowser` child using its RmlUi layout rectangle.
- Foundation complete: create the browser lazily, destroy it when hidden/removed, and bridge typed H5UI events back to the owner view.
- Replace the initial Slate overlay with UE's CEF accelerated-paint/shared-texture compositor where available.
- Composite only the subview rectangle and route focus/input by hit test.
- Isolate cookies, permissions, navigation policy, popups, downloads, and audio.
- Suspend or reduce frame rate for hidden and occluded subviews.

Acceptance: a native HUD can host a specific browser-only panel without converting the entire UI to CEF or paying a full-screen browser redraw cost.

## 0.5 Authoring and production tooling

- Live reload with dependency tracking for RML, CSS, images, and scripts.
- Editor preview, DOM/style inspector, event monitor, and per-view profiler.
- Cook validation for missing resources, unsupported CSS, and platform media formats.
- Automated stress scenes for many views, long lists, animation, video, and 4K/8K output.

Acceptance: designers can build, diagnose, and package a UI without inspecting C++ logs for routine errors.
