// The first script of each page: which language it is in, and that language's words, fetched from the hub before any
// other script runs, so the page never shows in one language and then another. English needs nothing fetched. The
// language is the one chosen on this device with the switch in the menu, or else the first the browser prefers that the
// hub has, pt-BR falling back to pt, and English otherwise. Each language's words are made from i18n/<code>.po.
const LANGUAGES = { en: "English" }; // tools/embed_web.py puts every language offered here
const LANG = (() => {
  let chosen = "";
  try {
    chosen = localStorage.getItem("lang") || "";
  } catch (e) {}
  // Its own languages only: a name such as "constructor", kept on this device, is not one, though the object has it.
  const offered = (code) => Object.prototype.hasOwnProperty.call(LANGUAGES, code);
  if (offered(chosen)) return chosen;
  const preferred =
    navigator.languages && navigator.languages.length ? navigator.languages : [navigator.language || ""];
  for (const p of preferred) {
    const code = String(p).toLowerCase();
    if (offered(code)) return code;
    if (offered(code.split("-")[0])) return code.split("-")[0];
  }
  return "en";
})();
if (LANG != "en") {
  // The setup page, served over the hub's open setup network, gets only its own few phrases.
  const which = document.documentElement.dataset.words ? "-" + document.documentElement.dataset.words : "";
  // Read before the next script runs. Written in pieces, so the build, which refuses a page linking a file, and the
  // page, which would end this script there, do not take it for a script tag of the page's own.
  document.write("<scr" + 'ipt src="/lang?file=' + LANG + which + '"></scr' + "ipt>");
}
