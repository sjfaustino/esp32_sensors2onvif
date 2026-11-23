// The pages' words in the viewer's language, as lang-pick.js chose it and fetched them: the English stays in the code and
// the HTML as the key, and tr("Saved.") gives its translation from i18n/<code>.po, or the English itself when there is
// none. {name} in a phrase is filled from values, so a sentence keeps its own word order in each language; trc() names a
// context, for a word said differently in two places; trCount() takes the plural form the language's own rule picks.
// tools/check_translations.py, one of the quick checks, fails while any phrase of the pages has no translation.
const LANG_TEXT = (LANG != "en" && window.LANG_WORDS) || { words: {}, plural: (n) => (n == 1 ? 0 : 1), tag: "en" };
const TRANSLATION = LANG_TEXT.words;
document.documentElement.lang = LANG_TEXT.tag || LANG;

const i18nHas = (key) => Object.prototype.hasOwnProperty.call(TRANSLATION, key);
const i18nFill = (s, values) => (values ? s.replace(/\{(\w+)\}/g, (m, k) => (k in values ? values[k] : m)) : s);
const i18nSaid = (key, english) => {
  const s = i18nHas(key) ? TRANSLATION[key] : english;
  return Array.isArray(s) ? s[0] : s; // a phrase with plural forms, asked for as one: its singular
};

function tr(text, values) {
  return i18nFill(i18nSaid(text, text), values);
}
// The same, in a context: trc("battery", "None"), where a word is written differently in another place.
function trc(context, text, values) {
  return i18nFill(i18nSaid(context + "\u0004" + text, text), values);
}

// A sentence with things of the page inside it, such as a field: "An alert lasts {hold} seconds." with { hold: input }
// gives the words and the field in the order of the language, to spread into el().
const trParts = (text, nodes) =>
  tr(text)
    .split(/(\{\w+\})/)
    .filter(Boolean)
    .map((p) => {
      const m = p.match(/^\{(\w+)\}$/);
      return m && m[1] in nodes ? nodes[m[1]] : p;
    });

// A phrase by a count, with {n} for it: one and many are the English, and the language has as many forms as it needs.
function trCount(n, one, many) {
  const forms = i18nHas(one) ? TRANSLATION[one] : null;
  if (!Array.isArray(forms)) return i18nFill(n == 1 ? one : many, { n });
  return i18nFill(forms[Math.min(LANG_TEXT.plural(n), forms.length - 1)], { n });
}

// A message from the hub. Its reply may give the message as a phrase with its values, { say, values }, which is
// translated as any phrase; else its English, as a whole, or by a pattern with {name} for each part that changes, as the
// details of a few hub events still come.
function hubSaid(reply) {
  if (reply && reply.say) return tr(reply.say, reply.values);
  return hubText(reply && reply.error);
}
const hubText = (() => {
  let patterns = null;
  return (text) => {
    if (!text || LANG == "en") return text || "";
    if (i18nHas(text)) return i18nSaid(text, text);
    if (!patterns)
      patterns = Object.keys(TRANSLATION)
        .filter((k) => /\{\w+\}/.test(k) && !Array.isArray(TRANSLATION[k]))
        .map((k) => {
          const names = [];
          const re = k.replace(/[.*+?^$()|[\]\\]/g, "\\$&").replace(/\{(\w+)\}/g, (m, name) => {
            names.push(name);
            return "(.+?)";
          });
          return { re: new RegExp("^" + re + "$"), names, key: k };
        });
    for (const p of patterns) {
      const m = text.match(p.re);
      if (m) return tr(p.key, Object.fromEntries(p.names.map((n, i) => [n, hubText(m[i + 1])]))); // its parts too
    }
    return text;
  };
})();

// The page's own text: each run of words in the HTML, and the words of a field's placeholder, a title, an alt and an
// aria-label, once their spaces are made single.
function translatePage(root) {
  if (LANG == "en") return;
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  for (let n = walker.nextNode(); n; n = walker.nextNode()) {
    const text = n.nodeValue.replace(/\s+/g, " ").trim();
    if (!text || !i18nHas(text)) continue;
    const lead = /^\s/.test(n.nodeValue) ? " " : "",
      trail = /\s$/.test(n.nodeValue) ? " " : "";
    n.nodeValue = lead + i18nSaid(text, text) + trail;
  }
  for (const attr of ["placeholder", "title", "alt", "aria-label"])
    for (const e of root.querySelectorAll("[" + attr + "]")) {
      const text = e.getAttribute(attr).replace(/\s+/g, " ").trim();
      if (i18nHas(text)) e.setAttribute(attr, i18nSaid(text, text));
    }
}
translatePage(document.body);
document.title = tr(document.title);

// The switch in the menu, for this device: the page loads again in the language chosen.
function chooseLanguage(lang) {
  try {
    localStorage.setItem("lang", lang);
  } catch (e) {}
  location.reload();
}
// Each language offered, as a choice in a list such as the switch, by its own name.
function languageOptions(select) {
  select.replaceChildren(
    ...Object.entries(LANGUAGES).map(([code, name]) => el("option", { value: code, textContent: name })),
  );
}
