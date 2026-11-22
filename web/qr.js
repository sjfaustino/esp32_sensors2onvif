// A QR code for the hub's label, made here, as the hub's pages load nothing from the internet: byte mode, error correction
// M (15%), versions 1 to 6, up to 106 bytes, as ISO/IEC 18004 has it. tools/e2e_pages.py checks every mask against segno.
const qrCode = (() => {
  // For error correction M: each version's data codewords per block, blocks, and error correction codewords per block.
  const BLOCKS = [null, [16, 1, 10], [28, 1, 16], [44, 1, 26], [32, 2, 18], [43, 2, 24], [27, 4, 16]];
  const ALIGN = [null, [], [6, 18], [6, 22], [6, 26], [6, 30], [6, 34]];
  const EXP = new Array(512),
    LOG = new Array(256);
  for (let i = 0, x = 1; i < 255; i++) {
    EXP[i] = x;
    LOG[x] = i;
    x <<= 1;
    if (x & 256) x ^= 0x11d;
  }
  for (let i = 255; i < 512; i++) EXP[i] = EXP[i - 255];
  const mul = (a, b) => (a && b ? EXP[LOG[a] + LOG[b]] : 0);

  // The Reed-Solomon error correction codewords of one block.
  function ecc(data, n) {
    let g = [1];
    for (let i = 0; i < n; i++) {
      const next = new Array(g.length + 1).fill(0);
      for (let j = 0; j < g.length; j++) {
        next[j] ^= g[j];
        next[j + 1] ^= mul(g[j], EXP[i]);
      }
      g = next;
    }
    const r = new Array(n).fill(0);
    for (const d of data) {
      const f = d ^ r.shift();
      r.push(0);
      for (let j = 0; j < n; j++) r[j] ^= mul(g[j + 1], f);
    }
    return r;
  }

  // The codewords, data then error correction, each interleaved across the blocks.
  function codewords(bytes, version) {
    const [per, blocks, ec] = BLOCKS[version],
      capacity = per * blocks,
      bits = [];
    const put = (value, n) => {
      for (let i = n - 1; i >= 0; i--) bits.push((value >> i) & 1);
    };
    put(4, 4); // byte mode
    put(bytes.length, 8);
    for (const b of bytes) put(b, 8);
    put(0, Math.min(4, capacity * 8 - bits.length));
    while (bits.length % 8) bits.push(0);
    const data = [];
    for (let i = 0; i < bits.length; i += 8) data.push(bits.slice(i, i + 8).reduce((a, b) => (a << 1) | b, 0));
    for (let pad = 0xec; data.length < capacity; pad ^= 0xec ^ 0x11) data.push(pad);
    const parts = Array.from({ length: blocks }, (_, b) => data.slice(b * per, (b + 1) * per)),
      checks = parts.map((p) => ecc(p, ec)),
      out = [];
    for (let i = 0; i < per; i++) for (const p of parts) out.push(p[i]);
    for (let i = 0; i < ec; i++) for (const c of checks) out.push(c[i]);
    return out;
  }

  const MASKS = [
    (r, c) => (r + c) % 2 == 0,
    (r) => r % 2 == 0,
    (r, c) => c % 3 == 0,
    (r, c) => (r + c) % 3 == 0,
    (r, c) => (Math.floor(r / 2) + Math.floor(c / 3)) % 2 == 0,
    (r, c) => ((r * c) % 2) + ((r * c) % 3) == 0,
    (r, c) => (((r * c) % 2) + ((r * c) % 3)) % 2 == 0,
    (r, c) => (((r + c) % 2) + ((r * c) % 3)) % 2 == 0,
  ];

  // The symbol with one mask: rows of true for a dark module.
  function symbol(version, words, mask, withFormat = true) {
    const size = 17 + 4 * version,
      dark = Array.from({ length: size }, () => new Array(size).fill(false)),
      fixed = Array.from({ length: size }, () => new Array(size).fill(false));
    const set = (r, c, on) => {
      dark[r][c] = on;
      fixed[r][c] = true;
    };
    for (const [r0, c0] of [
      [0, 0],
      [size - 7, 0],
      [0, size - 7],
    ])
      for (let r = -1; r <= 7; r++)
        for (let c = -1; c <= 7; c++) {
          const rr = r0 + r,
            cc = c0 + c;
          if (rr < 0 || cc < 0 || rr >= size || cc >= size) continue;
          const ring = Math.max(Math.abs(r - 3), Math.abs(c - 3));
          set(rr, cc, ring != 2 && ring != 4);
        }
    for (let i = 8; i < size - 8; i++) {
      set(6, i, i % 2 == 0);
      set(i, 6, i % 2 == 0);
    }
    const at = ALIGN[version],
      last = at.length - 1;
    for (let a = 0; a < at.length; a++)
      for (let b = 0; b < at.length; b++) {
        if ((a == 0 && b == 0) || (a == 0 && b == last) || (a == last && b == 0)) continue;
        for (let r = -2; r <= 2; r++)
          for (let c = -2; c <= 2; c++) set(at[a] + r, at[b] + c, Math.max(Math.abs(r), Math.abs(c)) != 1);
      }
    // The format's places, kept free for now, and the dark module.
    for (let i = 0; i < 9; i++) {
      fixed[8][i] = fixed[i][8] = true;
    }
    for (let i = 0; i < 8; i++) {
      fixed[8][size - 1 - i] = fixed[size - 1 - i][8] = true;
    }
    set(size - 8, 8, true);
    // The codewords, two columns at a time from the right, up and down in turn, then masked.
    let bit = 0;
    const total = words.length * 8;
    for (let right = size - 1, up = true; right >= 1; right -= 2, up = !up) {
      if (right == 6) right = 5;
      for (let i = 0; i < size; i++) {
        const r = up ? size - 1 - i : i;
        for (const c of [right, right - 1]) {
          if (fixed[r][c]) continue;
          const on = bit < total && ((words[bit >> 3] >> (7 - (bit & 7))) & 1) == 1;
          bit++;
          dark[r][c] = on != MASKS[mask](r, c);
        }
      }
    }
    if (!withFormat) return dark; // scored so, as segno scores the masks before the format goes in
    // The format: error correction M, the mask, and its BCH code, in its two places.
    let rem = mask;
    for (let i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    const format = ((mask << 10) | rem) ^ 0x5412,
      f = (i) => ((format >> i) & 1) == 1;
    for (let i = 0; i <= 5; i++) dark[i][8] = f(i);
    dark[7][8] = f(6);
    dark[8][8] = f(7);
    dark[8][7] = f(8);
    for (let i = 9; i < 15; i++) dark[8][14 - i] = f(i);
    for (let i = 0; i < 8; i++) dark[8][size - 1 - i] = f(i);
    for (let i = 8; i < 15; i++) dark[size - 15 + i][8] = f(i);
    return dark;
  }

  // How hard a symbol is to read, by the standard's four rules: the mask with the least is used.
  function penalty(m) {
    const n = m.length;
    let score = 0;
    const lines = [];
    for (let i = 0; i < n; i++) {
      lines.push(m[i]);
      lines.push(m.map((row) => row[i]));
    }
    for (const line of lines) {
      for (let i = 0, run = 1; i < n; i++) {
        if (i + 1 < n && line[i + 1] == line[i]) run++;
        else {
          if (run >= 5) score += run - 2;
          run = 1;
        }
      }
      // Dark, light, three dark, light, dark, with four light modules, or the edge, on one side: as segno reads the rule.
      const s = line.map((d) => (d ? "1" : "0")).join("");
      for (let i = s.indexOf("1011101"); i >= 0; ) {
        const before = s.slice(Math.max(i - 4, 0), i),
          after = s.slice(i + 7, Math.min(i + 11, n));
        const counts = i == 0 || i == n - 7 || !before.includes("1") || !after.includes("1");
        if (counts) score += 40;
        i = s.indexOf("1011101", counts ? i + 7 : i + 4);
      }
    }
    for (let r = 0; r + 1 < n; r++)
      for (let c = 0; c + 1 < n; c++)
        if (m[r][c] == m[r][c + 1] && m[r][c] == m[r + 1][c] && m[r][c] == m[r + 1][c + 1]) score += 3;
    const darkCount = m.reduce((a, row) => a + row.filter(Boolean).length, 0);
    score += Math.floor(Math.abs((darkCount * 20) / (n * n) - 10)) * 10;
    return score;
  }

  // The symbol for the text, as rows of true for dark, with the mask given or the best one; null when it is too long.
  function matrix(text, mask) {
    const bytes = Array.from(new TextEncoder().encode(text));
    let version = 1;
    while (version < BLOCKS.length && BLOCKS[version][0] * BLOCKS[version][1] < bytes.length + 2) version++;
    if (version >= BLOCKS.length) return null;
    const words = codewords(bytes, version);
    if (mask != null) return symbol(version, words, mask);
    let best = null,
      bestScore = Infinity;
    for (let k = 0; k < 8; k++) {
      const s = penalty(symbol(version, words, k, false));
      if (s < bestScore) [best, bestScore] = [k, s];
    }
    return symbol(version, words, best);
  }

  // An SVG of it, with the quiet zone of four modules, each module a square.
  function svg(text) {
    const m = matrix(text);
    if (!m) return null;
    const n = m.length + 8,
      out = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    out.setAttribute("viewBox", "0 0 " + n + " " + n);
    out.setAttribute("class", "qr");
    let d = "";
    m.forEach((row, r) => row.forEach((on, c) => on && (d += "M" + (c + 4) + " " + (r + 4) + "h1v1h-1z")));
    const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
    path.setAttribute("d", d);
    out.append(path);
    return out;
  }
  return { matrix, svg };
})();
