/**
 * JACL syntax highlighting for CodeMirror 6.
 *
 * Uses StreamLanguage (the simple per-line regex-tokenizer adapter)
 * rather than a Lezer LR grammar — JACL's three-mode delimiter system
 * ([] juxtaposition, () infix, {} command) doesn't need structural
 * parsing for highlighting purposes, and Lezer would add a build step
 * for the grammar file. Trades folding / fancy indentation for a
 * ~150-line file that ships with the editor bundle.
 *
 * Token categories chosen to match what SYNTAX.md groups:
 *  - comments        `# ...`  and `## ...`  (doc) and `#{ ... }` pragma
 *  - strings         "..."  '...'  """..."""  with $var / $[expr] / $(expr) interpolation
 *  - numbers         decimal / hex (0x) / binary (0b) / float / scientific
 *  - var refs        $identifier
 *  - keyword heads   def mut set proc struct ctx if elif else while for ...
 *  - type names      i32 i64 u32 u64 f32 f64 bool str dyn nil
 *                    Vec Map Buf Box Ptr Stream Arr Future
 *  - shell           !cmd
 *  - operators       basic punctuation
 */

import { StreamLanguage, StringStream } from "@codemirror/language";
import { tags as t } from "@lezer/highlight";

// `function` is the only token name we need a custom mapping for --
// `tags.function` in @lezer/highlight is a Tag-wrapping function
// (Tag(Tag) -> Tag), not a bare Tag, so the StreamLanguage default
// lookup of `tags["function"]` doesn't produce a usable tag. We
// pre-build the wrapped form and hand it to StreamLanguage via
// `tokenTable`. Everything else ("string", "number", "comment",
// "keyword", "variableName", "typeName", "operator", "punctuation",
// "macroName") resolves via the fall-through path inside
// createTokenType (`extra[part] || tags[part]`), so the table can
// stay minimal.
const TOKEN_TABLE = {
  callee: t.function(t.variableName),
};

interface JaclState {
  /** Set when inside a `#{ ... }` pragma block, so we keep coloring
   *  the body as comment across line breaks. */
  inPragma: boolean;
  /** Triple-quoted string in progress, or null. Tracks the quote
   *  char ('"' or "'") so we know when to close. */
  tripleString: '"' | "'" | null;
  /** Single-line string interpolation depth — we color the `[expr]` or
   *  `(expr)` payload as code, not string. Stack of close chars. */
  interpStack: string[];
  /** Count of unclosed `{` braces. Used to detect when we're inside a
   *  multi-line `{..}` block: at SOL with braceDepth > 0, an indented
   *  line whose first non-whitespace char is a bareword still gets
   *  its first identifier marked as a call head. For single-line
   *  `{ a, b }` the depth goes +1 then -1 on the same line, so the
   *  content never benefits from the brace-block SOL rule -- matching
   *  the "don't mark inside single-line {..}" expectation. */
  braceDepth: number;
  /** True when the next identifier we tokenize should be styled as a
   *  call head ("callee"). Set by the SOL / `[` / `|` rules below;
   *  reset on consuming any identifier or value. */
  expectingHead: boolean;
}

const KEYWORDS = new Set([
  "def", "mut", "set",
  "proc", "struct", "ctx", "defmacro", "syntax-quote",
  "if", "elif", "else", "while", "for", "in", "match", "case",
  "return", "break", "continue", "yield",
  "spawn", "await", "parallel", "race", "sleep",
  "with-ctx", "watch", "unwatch", "swap", "reset", "deref", "box", "atom",
  "try", "catch", "error", "throw",
  "use", "import", "export", "extern", "alias",
  "pub", "priv",
  "true", "false", "nil",
  "and", "or", "not",
  "to", "to-string", "concat",
]);

const TYPE_NAMES = new Set([
  "i8", "i16", "i32", "i64",
  "u8", "u16", "u32", "u64",
  "f32", "f64",
  "bool", "str", "dyn",
  "Vec", "Map", "Buf", "Box", "Ptr", "Stream", "Arr", "Future",
  "bigint", "bigfloat",
]);

const startState = (): JaclState => ({
  inPragma: false,
  tripleString: null,
  interpStack: [],
  braceDepth: 0,
  expectingHead: false,
});

const copyState = (s: JaclState): JaclState => ({
  inPragma: s.inPragma,
  tripleString: s.tripleString,
  interpStack: s.interpStack.slice(),
  braceDepth: s.braceDepth,
  expectingHead: s.expectingHead,
});

/** Match the inside of a string from the current position, handling
 *  `$ident`, `$[expr]`, and `$(expr)` interpolation forms by either
 *  returning the styled-as-variable token or pushing onto interpStack
 *  so the next tokens parse as code. */
function tokenizeStringBody(
  stream: StringStream,
  state: JaclState,
  closeChar: '"' | "'",
  triple: boolean,
): string {
  if (triple) {
    // Triple-quoted: look for matching triple. Anything else is string.
    if (stream.match(closeChar.repeat(3))) {
      state.tripleString = null;
      return "string";
    }
  } else {
    // Single-quoted: closing single char ends string.
    if (stream.eat(closeChar)) return "string";
  }

  // Interpolation: $ followed by ident, [, (
  if (stream.peek() === "$") {
    stream.next();
    const next = stream.peek();
    if (next === "[" || next === "(") {
      stream.next();
      state.interpStack.push(next === "[" ? "]" : ")");
      return "punctuation";
    }
    // $ident interpolation
    stream.eatWhile(/[A-Za-z0-9_\-?!]/);
    return "variableName";
  }

  // Escape
  if (stream.peek() === "\\") {
    stream.next();
    stream.next();
    return "string";
  }

  // Consume run of plain string characters
  while (!stream.eol()) {
    const ch = stream.peek();
    if (ch === "$" || ch === "\\" || ch === closeChar) break;
    stream.next();
  }
  return "string";
}

const jaclMode = StreamLanguage.define<JaclState>({
  name: "jacl",
  startState,
  copyState,
  tokenTable: TOKEN_TABLE,
  token(stream, state) {
    // Call-position SOL rules:
    //   - Top level (braceDepth == 0): line must start with a bareword
    //     at column 0. Indented continuation lines of multi-line `[..]`
    //     args don't qualify and stay plain.
    //   - Inside a multi-line `{..}` block (braceDepth > 0): any line
    //     whose first non-whitespace char is a bareword qualifies.
    //     Indented body lines of `proc main { ... }` get their lead
    //     identifier marked; a single-line `{ a, b }` never sees a
    //     depth-elevated SOL because `{` and `}` open and close on
    //     the same line, so its params stay plain.
    if (stream.sol()) {
      const pattern = state.braceDepth > 0 ? /^\s*[A-Za-z_]/ : /^[A-Za-z_]/;
      if (pattern.test(stream.string)) state.expectingHead = true;
    }

    // Continue a triple-quoted string spanning lines.
    if (state.tripleString) {
      return tokenizeStringBody(stream, state, state.tripleString, true);
    }
    // Continue a #{ ... } pragma block spanning lines.
    if (state.inPragma) {
      while (!stream.eol()) {
        if (stream.eat("}")) {
          state.inPragma = false;
          return "comment";
        }
        stream.next();
      }
      return "comment";
    }

    // Whitespace.
    if (stream.eatSpace()) return null;

    // Pragma block `#{ ... }` — may span lines.
    if (stream.match("#{")) {
      state.inPragma = true;
      return "comment";
    }
    // Line comments: `## ...` doc, `# ...` regular.
    if (stream.eat("#")) {
      stream.skipToEnd();
      return "comment";
    }
    // `;` is a statement separator, not a comment, in JACL. Skip.

    // Strings.
    const q = stream.peek();
    if (q === '"' || q === "'") {
      state.expectingHead = false;
      // Look for triple-quoted opener.
      if (stream.match(q.repeat(3))) {
        state.tripleString = q;
        return "string";
      }
      stream.next();
      // Tokenize body up to matching quote on this same line.
      while (!stream.eol()) {
        const ch = stream.peek();
        if (ch === q) { stream.next(); return "string"; }
        if (ch === "$") {
          // Stop — let next call handle interpolation as a single token.
          break;
        }
        if (ch === "\\") { stream.next(); stream.next(); continue; }
        stream.next();
      }
      return "string";
    }

    // Variable reference: $identifier.
    if (stream.eat("$")) {
      stream.eatWhile(/[A-Za-z0-9_\-?!]/);
      state.expectingHead = false;
      return "variableName";
    }

    // Shell external: !cmd. The cmd name IS the call head, so this
    // token's also the act of invocation -- consume the head slot.
    if (stream.eat("!")) {
      stream.eatWhile(/[A-Za-z0-9_\-./]/);
      state.expectingHead = false;
      return "macroName";
    }

    // Numbers: 0x..., 0b..., decimal, float, scientific.
    if (/[0-9]/.test(q ?? "") || (q === "-" && /[0-9]/.test(stream.string.charAt(stream.pos + 1) ?? ""))) {
      // Optional leading minus
      stream.eat("-");
      if (stream.match(/0x[0-9a-fA-F_]+/)) { state.expectingHead = false; return "number"; }
      if (stream.match(/0b[01_]+/))         { state.expectingHead = false; return "number"; }
      if (stream.match(/[0-9_]+\.[0-9_]+([eE][-+]?[0-9]+)?/)) { state.expectingHead = false; return "number"; }
      if (stream.match(/[0-9_]+[eE][-+]?[0-9]+/))             { state.expectingHead = false; return "number"; }
      if (stream.match(/[0-9_]+/))                            { state.expectingHead = false; return "number"; }
    }

    // Brackets / parens / braces.
    //   - `[` opens juxtaposition mode: the first token IS a call
    //     head. Set expectingHead unconditionally.
    //   - `{` is overloaded -- single-line `{a, b}` is a param /
    //     destructure / struct-field list (NO call head), multi-line
    //     `{ body }` is a command block where each line's first
    //     bareword IS a head. We can't tell which it is until we see
    //     the closing `}`, so we don't mark the immediate next ident
    //     after `{`. Instead, the SOL rule (above) consults
    //     braceDepth and marks indented bareword line starts when
    //     we're inside an unclosed brace.
    //   - `(` opens infix mode -- no head.
    const ch = stream.peek();
    if (ch === "[") {
      stream.next();
      state.expectingHead = true;
      return "punctuation";
    }
    if (ch === "{") {
      stream.next();
      state.braceDepth++;
      state.expectingHead = false;
      return "punctuation";
    }
    if (ch === "(") {
      stream.next();
      state.expectingHead = false;
      return "punctuation";
    }
    if (ch === "]" || ch === ")") {
      stream.next();
      state.expectingHead = false;
      // String interpolation close: `$[..]` or `$(..)` pops here.
      if (state.interpStack.length > 0 &&
          state.interpStack[state.interpStack.length - 1] === ch) {
        state.interpStack.pop();
      }
      return "punctuation";
    }
    if (ch === "}") {
      stream.next();
      if (state.braceDepth > 0) state.braceDepth--;
      state.expectingHead = false;
      return "punctuation";
    }

    // Standalone operators.
    // Pipe `|` between commands resets head-expectation: the next
    // identifier starts a new pipeline stage.
    if (stream.match(/\|/)) { state.expectingHead = true; return "operator"; }
    if (stream.match(/->/)) { state.expectingHead = false; return "operator"; }
    if (stream.match(/=>/)) { state.expectingHead = false; return "operator"; }
    if (stream.match(/\?\./)) { state.expectingHead = false; return "operator"; }
    if (stream.match(/::/)) { state.expectingHead = false; return "operator"; }
    if (stream.match(/\.\.</)) { state.expectingHead = false; return "operator"; }
    if (stream.match(/\.\.=/)) { state.expectingHead = false; return "operator"; }
    if (stream.match(/\.\./)) { state.expectingHead = false; return "operator"; }
    if (stream.match(/[+\-*/%<>=!&^~]/)) {
      // Operators in head position (e.g. `[+ 1 2]`) are themselves the
      // call -- don't disturb expectingHead semantics for the args.
      state.expectingHead = false;
      return "operator";
    }
    // `;` ends a statement -- next identifier on this line is a head.
    if (stream.eat(";")) {
      state.expectingHead = true;
      return "punctuation";
    }
    // `,` is overloaded (statement separator vs list separator). We
    // can't distinguish without parsing, so we leave it as a neutral
    // list separator. Newline-separated statements are the common case.
    if (stream.eat(",")) return "punctuation";
    if (stream.eat(":")) { state.expectingHead = false; return "punctuation"; }
    if (stream.eat(".")) { state.expectingHead = false; return "punctuation"; }
    if (stream.eat("@")) { state.expectingHead = false; return "punctuation"; }

    // Identifiers — keywords, type names, or generic.
    if (stream.match(/[A-Za-z_][A-Za-z0-9_\-?!]*/)) {
      const ident = stream.current();
      // Keywords always win, regardless of position. A keyword "consumes"
      // the head slot (we don't want `n` in `def n 7` painted as a call).
      if (KEYWORDS.has(ident)) {
        state.expectingHead = false;
        return "keyword";
      }
      if (TYPE_NAMES.has(ident) || /^[A-Z]/.test(ident)) {
        state.expectingHead = false;
        return "typeName";
      }
      // Position-driven: identifier in call-head position. Emit
      // "callee" so it picks up the function(variableName) tag from
      // our TOKEN_TABLE (the bare "function" name doesn't resolve --
      // see comment on TOKEN_TABLE above).
      if (state.expectingHead) {
        state.expectingHead = false;
        return "callee";
      }
      // Otherwise it's an argument-position bareword (binding name,
      // field name, atom-style identifier, ...).
      state.expectingHead = false;
      return null;
    }

    // Anything else — skip a char to avoid infinite loop.
    stream.next();
    return null;
  },
  languageData: {
    commentTokens: { line: "#" },
    closeBrackets: { brackets: ["[", "(", "{", '"', "'"] },
  },
});

export const jacl = () => jaclMode;
