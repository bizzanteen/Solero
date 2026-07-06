#!/usr/bin/env bash
# Guard against a recurring Qt i18n bug: a multi-byte UTF-8 sequence written as
# \xNN BYTE ESCAPES inside a QStringLiteral / QLatin1String / u"..." literal.
#
# Why it's wrong: QStringLiteral (and u"...") build a UTF-16 literal, so each
# \xNN is taken as one UTF-16 code unit, NOT decoded as UTF-8. So
#     QStringLiteral("-")   ->  U+00E2 U+0080 U+0094  ("â??")  ✗
# whereas the SAME bytes in a plain "..." (const char* -> QString) are decoded by
# QString::fromUtf8 and render correctly.
#
# Correct forms for a non-ASCII glyph in a QStringLiteral context:
#   QStringLiteral("...") + QChar('-') + QStringLiteral("...")   // em dash
#   QString::fromUtf8("-")
#   an actual UTF-8 character typed in the source (the compiler transcodes it)
#
# Usage: check-no-mojibake.sh [src-dir]   (default: ../src relative to this script)
set -euo pipefail

dir="${1:-$(cd "$(dirname "$0")/../src" && pwd)}"

# QStringLiteral(/QLatin1String(/QLatin1Char(/u"  … containing a high byte escape.
pattern='(QStringLiteral|QLatin1String|QLatin1Char|u")[^;]*\\x[89a-fA-F][0-9a-fA-F]'

if hits=$(grep -rnE "$pattern" "$dir"); then
    echo "ERROR: UTF-8 byte escape(s) inside a QStringLiteral/QLatin1String/u\"\" literal."
    echo "These render as mojibake. Use QChar(0x…) or QString::fromUtf8() instead."
    echo
    echo "$hits"
    exit 1
fi
echo "check-no-mojibake: OK (no UTF-8 byte escapes in UTF-16 literals under $dir)"
