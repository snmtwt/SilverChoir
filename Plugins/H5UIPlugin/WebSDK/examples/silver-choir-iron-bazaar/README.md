# Silver Choir — Iron Bazaar (Vue)

Vue marketplace module for CommanderOS browser (`shadow://iron.bazaar`).

## Build

From `Plugins/H5UIPlugin/WebSDK`:

```bash
pnpm install
pnpm --filter silver-choir-iron-bazaar build
# or
pnpm build:iron-bazaar
```

Outputs into `Content/GameCore/H5UI/CommanderOS/V2/`:

| File | Role |
| --- | --- |
| `iron-bazaar.js` | IIFE app + `window.IronBazaar` API |
| `iron-bazaar.css` | Styles |
| `iron-bazaar.catalog.json` | Copy of default catalog (reference / offline) |
| `iron-bazaar.html` | Standalone test page (optional) |

## Architecture

```
src/
  data/catalog.json      # temporary product config
  components/
    ProductDetail.vue    # generic weapon intro (text + image from data)
    ProductDetailHost.vue
    ProductCard.vue …
  special/
    index.ts             # registry: specialPage key → Vue component
    ScarHPage.vue        # example special listing
  shop/state.ts
  main.ts                # mount / unmount / setCatalog API
```

### Generic detail page

Most weapons only need catalog fields:

```json
{
  "id": "ar-m4a1",
  "name": "M4A1 Carbine",
  "desc": "…",
  "price": 1850,
  "image": "weapons/m4a1.png",
  "detail": {
    "subtitle": "…",
    "longDesc": ["para1", "para2"],
    "specs": [{ "label": "Caliber", "value": "5.56" }],
    "highlights": ["…"],
    "image": "weapons/m4a1-hero.png",
    "gallery": ["weapons/m4a1-a.png"],
    "seller": { "name": "FOB-7 Armory", "rating": 4.8 }
  }
}
```

`ProductDetail.vue` renders this dynamically — no per-weapon Vue file required.

### Special pages

When a listing needs a unique layout, set `detail.specialPage` and register a component:

```ts
// special/index.ts
import MyGunPage from './MyGunPage.vue'
const registry = { 'my-gun': MyGunPage }
```

```json
"detail": { "specialPage": "my-gun", ... }
```

## Runtime API

```js
// Mounted automatically by CommanderOS arms route
IronBazaar.mount(element, { catalog?, productId?, onBackToIndex? })
IronBazaar.unmount()

// Replace entire catalog (UE / host)
IronBazaar.setCatalog(catalogObjectOrProductArray)
IronBazaar.upsertProduct(product)
IronBazaar.openProduct('ar-m4a1')
IronBazaar.getCatalog()

// CommanderOS helpers
CommanderOS.setShopCatalog(data)
CommanderOS.upsertShopProduct(product)
CommanderOS.openShopProduct('ar-m4a1')
```

### UE later

1. Push catalog JSON via iframe bridge / `H5UI` event into the page.
2. Call `CommanderOS.setShopCatalog(payload)` or `IronBazaar.setCatalog(payload)`.
3. Images: use `coui://` or packaged relative paths in `image` / `detail.image` / `detail.gallery`.

## Host events

| Event | When |
| --- | --- |
| `ShopProductOpen` | Detail opened |
| `ShopCheckout` | Checkout clicked |

Emitted on `CommanderOS` channel via `H5UI.emit` when available.
