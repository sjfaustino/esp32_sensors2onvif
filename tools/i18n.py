"""The translations of the hub's pages and messages: one gettext file per language in i18n/, such as i18n/pt.po, with the
English as the key, as the code writes it. This reads them, and makes what the hub serves and what its firmware holds:

  - each language's words for the pages, served by the hub as /lang/<code>.js, and a smaller one for the setup page;
  - the table of the hub's own messages, notifications and the daily check, in lib/sensorcam/cam_text_data.cpp;
  - the list of languages the pages offer.

tools/embed_web.py calls it on every build; tools/check_translations.py checks the files. A translator edits a .po file
with any gettext tool, such as Poedit or Weblate: a changed English phrase comes back marked fuzzy, to be looked at again.

Phrases the hub's own messages use have the context "message" (msgctxt), apart from the pages', as a word inside a
sentence can be written differently from one standing alone. A phrase that depends on a count has its plural forms, as
the file's Plural-Forms header says, so a language with three or four forms works as one with two.

A language is offered once its header says X-Complete: yes; until then it can be worked on without being shown.
"""
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FOLDER = os.path.join(ROOT, 'i18n')
MESSAGE = 'message'  # the context of the hub's own messages
SEP = '\u0004'  # between a context and its phrase, as gettext joins them


class Entry:
    def __init__(self, msgid, msgstr='', ctx=None, plural=None, forms=None, fuzzy=False, comments=None):
        self.msgid, self.msgstr, self.ctx, self.plural = msgid, msgstr, ctx, plural
        self.forms = forms or []
        self.fuzzy = fuzzy
        self.comments = comments or []

    @property
    def key(self):
        return (self.ctx + SEP if self.ctx else '') + self.msgid

    def translated(self):
        return not self.fuzzy and (all(self.forms) and self.forms if self.plural else bool(self.msgstr))


class Catalog:
    def __init__(self, code, headers, entries):
        self.code, self.headers, self.entries = code, headers, entries

    @property
    def name(self):
        return self.headers.get('X-Language-Name', self.code)

    @property
    def complete(self):
        return self.headers.get('X-Complete', 'no').strip().lower() == 'yes'

    @property
    def plural_expr(self):
        m = re.search(r'plural=([^;]+);?', self.headers.get('Plural-Forms', ''))
        expr = m.group(1).strip() if m else '(n != 1)'
        if not re.fullmatch(r'[n0-9\s()?:%<>=!&|+-]+', expr):
            raise ValueError(f'i18n/{self.code}.po: a Plural-Forms expression may use only n, numbers and operators')
        return expr

    @property
    def nplurals(self):
        m = re.search(r'nplurals=(\d+)', self.headers.get('Plural-Forms', ''))
        return int(m.group(1)) if m else 2

    def by_key(self):
        return {e.key: e for e in self.entries}


def _unquote(s):
    return json.loads(s)


def read(path):
    """A .po file as a Catalog."""
    code = os.path.splitext(os.path.basename(path))[0]
    entries, cur, field = [], {}, None

    def finish():
        if 'msgid' in cur:
            entries.append(cur.copy())
        cur.clear()

    for raw in open(path, encoding='utf-8').read().split('\n'):
        line = raw.strip()
        if not line:
            finish()
            field = None
            continue
        if line.startswith('#'):
            if 'msgid' in cur and field is not None:
                finish()
                field = None
            if line.startswith('#,') and 'fuzzy' in line:
                cur['fuzzy'] = True
            elif not line.startswith('#,'):
                cur.setdefault('comments', []).append(line)
            continue
        m = re.match(r'(msgctxt|msgid_plural|msgid|msgstr(?:\[(\d+)\])?)\s+(".*")$', line)
        if m:
            name, index, text = m.group(1), m.group(2), _unquote(m.group(3))
            if name == 'msgctxt' and 'msgid' in cur:
                finish()
            if name.startswith('msgstr['):
                field = ('forms', int(index))
                forms = cur.setdefault('forms', [])
                while len(forms) <= int(index):
                    forms.append('')
                forms[int(index)] = text
            else:
                field = name
                cur[name] = text
        elif line.startswith('"') and field:
            text = _unquote(line)
            if isinstance(field, tuple):
                cur['forms'][field[1]] += text
            else:
                cur[field] += text
    finish()
    headers = {}
    out = []
    for e in entries:
        if e['msgid'] == '' and 'msgctxt' not in e:
            for h in e.get('msgstr', '').split('\n'):
                if ':' in h:
                    k, v = h.split(':', 1)
                    headers[k.strip()] = v.strip()
            continue
        out.append(Entry(e['msgid'], e.get('msgstr', ''), e.get('msgctxt'), e.get('msgid_plural'), e.get('forms'), e.get('fuzzy', False),
                         e.get('comments')))
    return Catalog(code, headers, out)


def _q(s):
    return json.dumps(s, ensure_ascii=False)


def write(catalog, path):
    """A Catalog as a .po file, its entries in the order given."""
    lines = ['msgid ""', 'msgstr ""']
    lines += [_q(f'{k}: {v}\n') for k, v in catalog.headers.items()]
    for e in catalog.entries:
        lines.append('')
        lines += e.comments
        if e.fuzzy:
            lines.append('#, fuzzy')
        if e.ctx:
            lines.append(f'msgctxt {_q(e.ctx)}')
        lines.append(f'msgid {_q(e.msgid)}')
        if e.plural:
            lines.append(f'msgid_plural {_q(e.plural)}')
            for i, f in enumerate(e.forms):
                lines.append(f'msgstr[{i}] {_q(f)}')
        else:
            lines.append(f'msgstr {_q(e.msgstr)}')
    open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')


def catalogs():
    """Every language in i18n/, by its code."""
    if not os.path.isdir(FOLDER):
        return {}
    return {os.path.splitext(n)[0]: read(os.path.join(FOLDER, n)) for n in sorted(os.listdir(FOLDER)) if n.endswith('.po')}


def offered():
    """The languages the pages and the messages offer: English, and each one marked complete."""
    return {'en': 'English', **{c.code: c.name for c in catalogs().values() if c.complete}}


def page_words(catalog, keep=None):
    """A language's words for the pages, as the script the hub serves as /lang/<code>.js: each phrase by its key, its
    plural forms as a list, and the language's plural rule. keep, when given, says which keys to take."""
    words = {}
    for e in catalog.entries:
        if e.ctx == MESSAGE or not e.translated() or (keep is not None and e.key not in keep):
            continue
        words[e.key] = e.forms if e.plural else e.msgstr
    return ('// The words of the pages in ' + catalog.name + ', made from i18n/' + catalog.code + '.po by tools/i18n.py.\n'
            'window.LANG_WORDS = { tag: ' + json.dumps(catalog.headers.get('Language', catalog.code).replace('_', '-')) +
            ', words: ' + json.dumps(words, ensure_ascii=False, separators=(',', ':')) +
            ', plural: (n) => Number(' + catalog.plural_expr + ') };\n')


def _c(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'


def message_table(cats):
    """The C++ of the hub's own messages in each language offered: lib/sensorcam/cam_text_data.cpp."""
    out = ['// Written by tools/i18n.py from i18n/*.po on every build; not kept in git. The hub\'s own messages in each language',
           '// offered, the phrases with the context "message"; English is the key, and needs no table.',
           '#include "cam_text.h"', '']
    langs = []
    for c in cats:
        name = re.sub(r'\W', '_', c.code)
        rows = []
        for e in c.entries:
            if e.ctx != MESSAGE or not e.translated():
                continue
            if e.plural:
                rows += [f'    {{{_c(e.msgid)}, {_c(f)}, {i}}},' for i, f in enumerate(e.forms)]
            else:
                rows.append(f'    {{{_c(e.msgid)}, {_c(e.msgstr)}, 0}},')
        out.append(f'static const CamPhrase PHRASES_{name}[] = {{')
        out += rows or ['    {"", "", 0},']
        out.append('};')
        out.append(f'static int plural_{name}(long long n) {{ return (int)({c.plural_expr}); }}')
        langs.append(f'    {{{_c(c.code)}, {_c(c.name)}, PHRASES_{name}, sizeof(PHRASES_{name}) / sizeof(PHRASES_{name}[0]), plural_{name}}},')
    out.append('const CamLanguageText CAM_LANGUAGES[] = {')
    out += langs or ['    {"", "", nullptr, 0, nullptr},']
    out.append('};')
    out.append(f'const size_t CAM_LANGUAGE_COUNT = {len(langs)};')
    return '\n'.join(out) + '\n'


def write_if_changed(path, text):
    try:
        if open(path, encoding='utf-8').read() == text:
            return
    except OSError:
        pass
    open(path, 'w', encoding='utf-8', newline='\n').write(text)
