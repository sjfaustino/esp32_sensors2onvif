// The pages' words in the viewer's language: Portuguese (European) when the browser prefers it, or when chosen with the
// switch in the menu, and English otherwise. The English stays in the code and the HTML as the key: tr("Saved.") gives
// its Portuguese from PT in pt.js, or the English itself when there is none. {name} in a phrase is filled from values,
// so a sentence keeps its own word order in each language. The page's own text in the HTML is put in the language here,
// before the other scripts run, and so are the hub's messages, through hubText. tools/check_translations.py, run with
// the other quick checks, fails while any phrase of the pages has no Portuguese.
const LANGUAGES = { en: "English", pt: "Português" };
const LANG = (() => {
  let chosen = "";
  try {
    chosen = localStorage.getItem("lang") || "";
  } catch (e) {}
  if (LANGUAGES[chosen]) return chosen;
  const preferred =
    navigator.languages && navigator.languages.length ? navigator.languages : [navigator.language || ""];
  return /^pt\b/i.test(preferred[0] || "") ? "pt" : "en";
})();
const TRANSLATION = LANG == "pt" && typeof PT != "undefined" ? PT : {};
document.documentElement.lang = LANG == "pt" ? "pt-PT" : "en";

function tr(text, values) {
  const s = Object.prototype.hasOwnProperty.call(TRANSLATION, text) ? TRANSLATION[text] : text;
  return values ? s.replace(/\{(\w+)\}/g, (m, k) => (k in values ? values[k] : m)) : s;
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

// One of two phrases by a count, each with {n} for it: "1 sensor", "3 sensors". Portuguese, like English, takes the
// singular for 1 alone.
const trCount = (n, one, many) => tr(n == 1 ? one : many, { n });

// A message from the hub, in English: as a whole if PT has it, or by a pattern there with {name} for each changing part,
// such as "no such sensor: {name}"; else as it came.
const hubText = (() => {
  let patterns = null;
  return (text) => {
    if (!text || LANG == "en") return text || "";
    if (Object.prototype.hasOwnProperty.call(TRANSLATION, text)) return TRANSLATION[text];
    if (!patterns)
      patterns = Object.keys(TRANSLATION)
        .filter((k) => /\{\w+\}/.test(k))
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
// aria-label, as PT has them once their spaces are made single.
function translatePage(root) {
  if (LANG == "en") return;
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  for (let n = walker.nextNode(); n; n = walker.nextNode()) {
    const text = n.nodeValue.replace(/\s+/g, " ").trim();
    if (!text || !Object.prototype.hasOwnProperty.call(TRANSLATION, text)) continue;
    const lead = /^\s/.test(n.nodeValue) ? " " : "",
      trail = /\s$/.test(n.nodeValue) ? " " : "";
    n.nodeValue = lead + TRANSLATION[text] + trail;
  }
  for (const attr of ["placeholder", "title", "alt", "aria-label"])
    for (const e of root.querySelectorAll("[" + attr + "]")) {
      const text = e.getAttribute(attr).replace(/\s+/g, " ").trim();
      if (Object.prototype.hasOwnProperty.call(TRANSLATION, text)) e.setAttribute(attr, TRANSLATION[text]);
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
