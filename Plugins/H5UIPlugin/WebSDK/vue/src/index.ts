import {
  createRenderer,
  type Component,
  type RendererElement,
  type RendererNode
} from 'vue'

type H5UINode = RendererNode & Element
type H5UIElement = RendererElement & Element

const eventInvokers = new WeakMap<Element, Map<string, EventListener>>()

function cssName(name: string): string {
  return name.replace(/[A-Z]/g, value => `-${value.toLowerCase()}`)
}

function patchStyle(element: H5UIElement, previous: unknown, next: unknown): void {
  if (!next) {
    if (previous && typeof previous === 'object') {
      for (const name of Object.keys(previous)) element.style.removeProperty(cssName(name))
    }
    return
  }
  if (typeof next === 'string') {
    element.setAttribute('style', next)
    return
  }
  const nextStyle = next as Record<string, unknown>
  const previousStyle = previous && typeof previous === 'object' ? previous as Record<string, unknown> : {}
  for (const name of Object.keys(previousStyle)) {
    if (!(name in nextStyle)) element.style.removeProperty(cssName(name))
  }
  for (const [name, value] of Object.entries(nextStyle)) {
    element.style.setProperty(cssName(name), value == null ? '' : String(value))
  }
}

function patchEvent(element: H5UIElement, rawName: string, next: unknown): void {
  const type = rawName.slice(2).replace(/Once$/, '').toLowerCase()
  let invokers = eventInvokers.get(element)
  if (!invokers) {
    invokers = new Map()
    eventInvokers.set(element, invokers)
  }
  const previous = invokers.get(rawName)
  if (previous) element.removeEventListener(type, previous)
  if (typeof next === 'function' || Array.isArray(next)) {
    const invoker = ((event: Event) => {
      const handlers = Array.isArray(next) ? next : [next]
      for (const handler of handlers) if (typeof handler === 'function') handler(event)
    }) as EventListener
    invokers.set(rawName, invoker)
    element.addEventListener(type, invoker, rawName.endsWith('Once') ? { once: true } : undefined)
  } else {
    invokers.delete(rawName)
  }
}

const renderer = createRenderer<H5UINode, H5UIElement>({
  patchProp(element, key, previous, next) {
    if (key === 'class') {
      element.className = next == null ? '' : String(next)
    } else if (key === 'style') {
      patchStyle(element, previous, next)
    } else if (/^on[A-Z]/.test(key)) {
      patchEvent(element, key, next)
    } else if (key === 'value' || key === 'checked' || key === 'disabled') {
      ;(element as unknown as Record<string, unknown>)[key] = next
    } else if (next == null || next === false) {
      element.removeAttribute(key)
    } else {
      element.setAttribute(key, next === true ? '' : String(next))
    }
  },
  insert(child, parent, anchor) {
    parent.insertBefore(child, anchor || null)
  },
  remove(child) {
    child.parentNode?.removeChild(child)
  },
  createElement(type) {
    return document.createElement(type) as H5UIElement
  },
  createText(text) {
    return document.createTextNode(text) as unknown as H5UINode
  },
  createComment(text) {
    return document.createComment(text) as unknown as H5UINode
  },
  setText(node, text) {
    node.textContent = text
  },
  setElementText(element, text) {
    element.textContent = text
  },
  parentNode(node) {
    return node.parentNode as H5UIElement | null
  },
  nextSibling(node) {
    return node.nextSibling as H5UINode | null
  },
  querySelector(selector) {
    return document.querySelector(selector) as H5UIElement | null
  },
  setScopeId(element, id) {
    element.setAttribute(id, '')
  },
  insertStaticContent(content, parent, anchor) {
    const container = document.createElement('h5ui-static') as H5UIElement
    container.innerHTML = content
    const nodes = Array.from(container.childNodes) as H5UINode[]
    let first: H5UINode | null = null
    let last: H5UINode | null = null
    for (const node of nodes) {
      parent.insertBefore(node, anchor || null)
      first ||= node
      last = node
    }
    if (!first || !last) {
      const placeholder = document.createComment('static') as unknown as H5UINode
      parent.insertBefore(placeholder, anchor || null)
      first = last = placeholder
    }
    return [first, last]
  }
})

export function createApp(rootComponent: Component, rootProps?: Record<string, unknown> | null) {
  const app = renderer.createApp(rootComponent, rootProps || undefined)
  const mount = app.mount
  app.mount = ((container: H5UIElement | string) => {
    const target = typeof container === 'string'
      ? document.querySelector(container) as H5UIElement | null
      : container
    if (!target) throw new Error(`Failed to mount Vue app: selector "${container}" returned null.`)
    return mount(target)
  }) as typeof app.mount
  return app
}

export const createH5UIApp = createApp
export const render = renderer.render

export * from 'vue'
