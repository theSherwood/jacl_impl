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
});

const copyState = (s: JaclState): JaclState => ({
  inPragma: s.inPragma,
  tripleString: s.tripleString,
  interpStack: s.interpStack.slice(),
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
  token(stream, state) {
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
      return "variableName";
    }

    // Shell external: !cmd.
    if (stream.eat("!")) {
      stream.eatWhile(/[A-Za-z0-9_\-./]/);
      return "macroName";
    }

    // Numbers: 0x..., 0b..., decimal, float, scientific.
    if (/[0-9]/.test(q ?? "") || (q === "-" && /[0-9]/.test(stream.string.charAt(stream.pos + 1) ?? ""))) {
      // Optional leading minus
      stream.eat("-");
      if (stream.match(/0x[0-9a-fA-F_]+/)) return "number";
      if (stream.match(/0b[01_]+/)) return "number";
      if (stream.match(/[0-9_]+\.[0-9_]+([eE][-+]?[0-9]+)?/)) return "number";
      if (stream.match(/[0-9_]+[eE][-+]?[0-9]+/)) return "number";
      if (stream.match(/[0-9_]+/)) return "number";
    }

    // Brackets / parens / braces.
    const ch = stream.peek();
    if (ch === "[" || ch === "]" ||
        ch === "{" || ch === "}" ||
        ch === "(" || ch === ")") {
      stream.next();
      // Close-paren / close-brace pops an interpolation level.
      if ((ch === "]" || ch === ")") &&
          state.interpStack.length > 0 &&
          state.interpStack[state.interpStack.length - 1] === ch) {
        state.interpStack.pop();
      }
      return "punctuation";
    }

    // Standalone operators.
    if (stream.match(/->/)) return "operator";
    if (stream.match(/=>/)) return "operator";
    if (stream.match(/\?\./)) return "operator";
    if (stream.match(/::/)) return "operator";
    if (stream.match(/\.\.</)) return "operator";
    if (stream.match(/\.\.=/)) return "operator";
    if (stream.match(/\.\./)) return "operator";
    if (stream.match(/[+\-*/%<>=!|&^~]/)) return "operator";
    if (stream.match(/[:,;]/)) return "punctuation";
    if (stream.match(/[.@]/)) return "punctuation";

    // Identifiers — keywords, type names, or generic.
    if (stream.match(/[A-Za-z_][A-Za-z0-9_\-?!]*/)) {
      const ident = stream.current();
      if (KEYWORDS.has(ident)) return "keyword";
      if (TYPE_NAMES.has(ident)) return "typeName";
      // Capitalized → likely a struct constructor or type ref.
      if (/^[A-Z]/.test(ident)) return "typeName";
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
