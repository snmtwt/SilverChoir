/** Shop product schema — driven by JSON now, UE later. */

export type ShopSpec = {
  label: string
  value: string
}

export type ShopSeller = {
  name: string
  rating?: number
  deals?: number
  note?: string
}

/**
 * Optional product detail block.
 * Generic ProductDetail.vue renders these fields (text + image).
 * Omit any field to fall back to list-level data.
 */
export type ShopProductDetail = {
  subtitle?: string
  /** Long copy paragraphs */
  longDesc?: string[]
  /** Spec rows for the detail table */
  specs?: ShopSpec[]
  highlights?: string[]
  seller?: ShopSeller
  condition?: string
  /** Relative image path under the H5UI/content root, or coui/absolute URL */
  image?: string
  /** Extra gallery images (optional) */
  gallery?: string[]
  tags?: string[]
  /**
   * Special page component key registered in special/index.ts.
   * When set, that component is shown instead of the generic detail page.
   */
  specialPage?: string
}

export type ShopProduct = {
  id: string
  cat: string
  name: string
  /** Short list description */
  desc: string
  price: number
  was?: number
  rating: number
  sold: number
  stock: number
  badge?: string
  featured?: number
  /** List thumbnail (optional) */
  image?: string
  detail?: ShopProductDetail
}

export type ShopCatalog = {
  version?: number
  title?: string
  tagline?: string
  currency?: string
  freeShippingOver?: number
  shippingFee?: number
  categories?: { id: string; label: string }[]
  products: ShopProduct[]
}

export type ShopView = 'browse' | 'detail'

export type ShopSort = 'featured' | 'price-asc' | 'price-desc' | 'rating'

export type MountOptions = {
  /** Full catalog override (from UE or external JSON) */
  catalog?: ShopCatalog | ShopProduct[] | null
  /** Open this product detail immediately */
  productId?: string | null
  /** Called when user presses back-to-index (desktop hub) */
  onBackToIndex?: () => void
  /** Emit host events (defaults to H5UI / window bridge) */
  emitHost?: (eventName: string, payload?: unknown) => void
}
