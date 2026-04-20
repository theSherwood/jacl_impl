/* Auto-generated from prelude.jacl - do not edit */
static const char *jacl_prelude_source =
    "# JACL Prelude - Macro definitions loaded before user code\n"
    "# This file is converted to a C header (prelude_source.h) by build.sh\n"
    "\n"
    "# Lambda shorthand: [\\ + $it 1] expands to proc {^it} { + $it 1 }\n"
    "defmacro \\ {head ..rest} {\n"
    "  def body [make-syntax \"command\" $head $rest]\n"
    "  def blk [make-syntax \"block\" [vec $body]]\n"
    "  def pn [make-syntax \"lit-string-caret\" \"it\"]\n"
    "  def params [make-syntax \"command\" $pn [vec]]\n"
    "  def nm [make-syntax \"lit-string\" \"\"]\n"
    "  [make-syntax \"command\" [make-syntax \"lit-string\" \"proc\"] [vec $nm $params $blk]]\n"
    "}\n"
    ;
