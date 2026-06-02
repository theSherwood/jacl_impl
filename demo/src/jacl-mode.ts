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

// CodeMirror's StreamLanguage looks token names up directly in
// @lezer/highlight `tags`; bare strings like "function" don't resolve
// because `tags.function` is a wrapper (Tag(Tag) -> Tag), not a plain
// Tag. We pre-build the wrapped forms here and hand them to
// StreamLanguage via `tokenTable`. Names that are already plain tags
// (keyword, string, ...) we re-list so the table is the single source
// of truth.
const TOKEN_TABLE = {
  function:     t.function(t.variableName),  // call heads
  keyword:      t.keyword,
  typeName:     t.typeName,
  string:       t.string,
  number:       t.number,
  comment:      t.comment,
  operator:     t.operator,
  punctuation:  t.punctuation,
  variableName: t.variableName,
  macroName:    t.macroName,
};

type Bracket = "[" | "(" | "{";

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
  /** Bracket nesting so we know which JACL parse mode is active:
   *  `[` juxtaposition (head position), `(` infix (no head), `{` command
   *  block (each statement starts with a head). Top of stack drives
   *  head-detection at statement boundaries. */
  bracketStack: Bracket[];
  /** True when the next identifier we tokenize should be styled as a
   *  call head ("function"). Reset to false once an identifier or
   *  value is consumed. Set after `[`, `{`, `|`, `;`, and newlines
   *  whose enclosing mode isn't `(`. */
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
  bracketStack: [],
  // The very first token of the document is at "top of statement",
  // which we treat as a call-head position (matches the top-level {}
  // command mode JACL implies).
  expectingHead: true,
});

const copyState = (s: JaclState): JaclState => ({
  inPragma: s.inPragma,
  tripleString: s.tripleString,
  interpStack: s.interpStack.slice(),
  bracketStack: s.bracketStack.slice(),
  expectingHead: s.expectingHead,
});

/** Top-of-stack helper. Empty stack is treated as `{` (top-level
 *  command mode) for head-position purposes. */
function topMode(stack: Bracket[]): Bracket {
  return stack.length === 0 ? "{" : stack[stack.length - 1];
}

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
    // At the start of a new physical line, reset expectingHead if the
    // enclosing parse mode is statement-oriented (top level or `{}`
    // command block). Inside `[]` juxtaposition or `()` infix a newline
    // is just whitespace -- no implicit statement break.
    if (stream.sol()) {
      const mode = topMode(state.bracketStack);
      if (mode === "{") state.expectingHead = true;
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
    const ch = stream.peek();
    if (ch === "[" || ch === "{" || ch === "(") {
      stream.next();
      state.bracketStack.push(ch as Bracket);
      // `[head args]` always puts the first token in call position.
      // `{}` is ambiguous: command block in `proc main {} { body }`
      // (body's first token IS a head), but destructuring / param /
      // struct-field list in `proc add {a b}` / `def {x, y} val` /
      // `struct Pt {i32 x, i32 y}` (first token is NOT a head). The
      // stream tokenizer can't tell these apart without real syntax
      // context, so we don't auto-set head here; the statement-start
      // rules (SOL, `;`, `|`) catch the common multi-line block
      // pattern at the next line break. The cost is a one-liner like
      // `{ print $it }` not painting `print` as a call.
      // `(` opens infix mode where the first token isn't a callable.
      state.expectingHead = ch === "[";
      return "punctuation";
    }
    if (ch === "]" || ch === "}" || ch === ")") {
      stream.next();
      const top = state.bracketStack[state.bracketStack.length - 1];
      const expectedClose = top === "[" ? "]" : top === "{" ? "}" : top === "(" ? ")" : null;
      if (expectedClose === ch) state.bracketStack.pop();
      // After closing, we're back in the parent mode; conservatively
      // assume this token was an argument/value -- the next thing is
      // not a head unless a pipe / newline / `;` resets it.
      state.expectingHead = false;
      // String interpolation close: `$[..]` or `$(..)` pops here too.
      if ((ch === "]" || ch === ")") &&
          state.interpStack.length > 0 &&
          state.interpStack[state.interpStack.length - 1] === ch) {
        state.interpStack.pop();
      }
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
    // `;` ends a statement; `,` separates items but in `{}` mode also
    // separates statements.
    if (stream.eat(";")) {
      const mode = topMode(state.bracketStack);
      if (mode === "{") state.expectingHead = true;
      return "punctuation";
    }
    if (stream.eat(",")) {
      // `,` is overloaded: statement separator in `{}` blocks AND list
      // separator in param lists / destructuring / struct fields /
      // typed-vec elements. We can't distinguish without parsing, so
      // treat it as a non-resetting list separator. The cost is that
      // `def p 1, def q 2` won't paint the second statement's head;
      // newline-separated statements (the common case) work fine.
      return "punctuation";
    }
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
      // Position-driven: identifier in call-head position is "function".
      if (state.expectingHead) {
        state.expectingHead = false;
        return "function";
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
