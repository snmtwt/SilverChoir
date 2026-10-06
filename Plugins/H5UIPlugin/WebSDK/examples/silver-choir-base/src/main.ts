import { createH5UIApp } from '@h5ui-plugin/vue'
import App from './App.vue'
import localizationConfig from './localization.json'

const initialCopy = localizationConfig.languages[localizationConfig.defaultLanguage as keyof typeof localizationConfig.languages]
document.querySelector('html')?.setAttribute('lang', initialCopy.htmlLanguage)
const title = document.querySelector('title')
if (title) title.textContent = initialCopy.documentTitle

createH5UIApp(App).mount('#app')
