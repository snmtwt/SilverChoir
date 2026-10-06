/**
 * One-shot patch for Content/.../iron-bazaar.js when Vite cannot run (old Node).
 * Source of truth remains src/shop/state.ts + App.vue — rebuild when Node is new enough.
 */
const fs = require('fs')
const path = require('path')

const target = path.resolve(
  __dirname,
  '../../../../../../Content/GameCore/H5UI/CommanderOS/V2/iron-bazaar.js'
)

let s = fs.readFileSync(target, 'utf8')

const oldOpen =
  'function C(e){let t=p.value.find(t=>t.id===e);if(!t){b(`Product not found`);return}r.value=e,n.value=`detail`,f(`ShopProductOpen`,{id:e,name:t.name})}function w(){n.value=`browse`,r.value=null}'

const newOpen =
  'function C(e){let t=p.value.find(t=>t.id===e);if(!t){b(`Product not found`);return}if(n.value===`browse`){try{let e=document.querySelector(`[data-shop-scroll]`);e&&(E=e.scrollTop||0,D=e.scrollLeft||0)}catch{}}r.value=e,n.value=`detail`,f(`ShopProductOpen`,{id:e,name:t.name})}function w(){n.value=`browse`,r.value=null,O()}function O(){if(A)return;A=!0;sn(()=>{A=!1;let e=null;try{e=document.querySelector(`[data-shop-scroll]`)}catch{}if(!e)return;k(e);let t=E,n=D,r=()=>{let e=null;try{e=document.querySelector(`[data-shop-scroll]`)}catch{}e&&(E=t,D=n,k(e))};typeof requestAnimationFrame==`function`?requestAnimationFrame(()=>requestAnimationFrame(r)):setTimeout(r,0)})}function k(e){e.scrollTop=E,e.scrollLeft=D;if(typeof e.scrollTo==`function`)try{e.scrollTo(D,E)}catch{}}'

const oldVars = 'l=R(``),u,d=e.onBackToIndex'
const newVars = 'l=R(``),u,E=0,D=0,A=!1,d=e.onBackToIndex'

const oldMain = 'J(`div`,{class:`shop-main`}'
const newMain = 'J(`div`,{class:`shop-main`,"data-shop-scroll":"","data-scroll-region":""}'

function mustReplace(label, from, to) {
  if (!s.includes(from)) {
    console.error('FAIL: missing block for', label)
    process.exit(1)
  }
  if (from === to) return
  const next = s.replace(from, to)
  if (next === s) {
    console.error('FAIL: replace no-op for', label)
    process.exit(1)
  }
  s = next
  console.log('ok:', label)
}

// Already patched?
if (s.includes('data-shop-scroll') && s.includes('function O(){if(A)return')) {
  console.log('already patched:', target)
  process.exit(0)
}

mustReplace('open/back+restore', oldOpen, newOpen)
mustReplace('scroll vars', oldVars, newVars)
mustReplace('shop-main attrs', oldMain, newMain)

fs.writeFileSync(target, s)
console.log('patched', target, 'size', s.length)
