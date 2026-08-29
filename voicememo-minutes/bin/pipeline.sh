#!/bin/bash
# 1 つの音声ファイルを最後まで処理する:
#   wav 変換 → 文字起こし+話者分離 → Claude で議事録生成 → docx 化
#   → iCloud Drive へ出力 → (条件付き)メール送信 → 元ファイル退避
#
# 使い方: pipeline.sh <音声ファイル>
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG="${VOICEMEMO_CONFIG:-$ROOT_DIR/config/config.env}"
if [ ! -f "$CONFIG" ]; then
  echo "設定ファイルがありません: $CONFIG (config.env.example をコピーして作成してください)"
  exit 1
fi
set -a
# shellcheck disable=SC1090
source "$CONFIG"
set +a

IN="$1"
if [ ! -f "$IN" ]; then
  echo "ファイルがありません: $IN"
  exit 1
fi

BASE="$(basename "$IN")"
BASE="${BASE%.*}"
SAFE="${BASE// /_}"
SAFE="${SAFE//\//_}"
TS="$(date +%Y%m%d_%H%M%S)"
JOB="${SAFE}_${TS}"

WORK="$ROOT_DIR/work/$JOB"
OUT="$OUTPUT_DIR/$JOB"
mkdir -p "$WORK" "$OUT" "$ARCHIVE_DIR"

echo "=== [$JOB] 処理開始: $IN"

# 1) 16kHz モノラル wav へ変換
ffmpeg -hide_banner -loglevel error -y -i "$IN" -ac 1 -ar 16000 "$WORK/audio.wav"

# 2) 文字起こし + 話者分離(すべてオンデバイス)
"$ROOT_DIR/.venv/bin/python" "$SCRIPT_DIR/transcribe.py" "$WORK/audio.wav" "$WORK"

# 3) Claude Code(Max サブスクリプション)で議事録生成
CLAUDE_BIN="${CLAUDE_BIN:-claude}"
CLAUDE_ARGS=(-p --output-format text)
if [ -n "${CLAUDE_MODEL:-}" ]; then
  CLAUDE_ARGS+=(--model "$CLAUDE_MODEL")
fi
echo "=== [$JOB] Claude で議事録を生成中…"
"$CLAUDE_BIN" "${CLAUDE_ARGS[@]}" "$(cat "$ROOT_DIR/templates/minutes_prompt.md")" \
  < "$WORK/transcript.txt" > "$WORK/minutes.md"

if [ ! -s "$WORK/minutes.md" ]; then
  echo "議事録の生成に失敗しました(Claude の出力が空)。claude のログイン状態を確認してください。"
  exit 1
fi

# 4) docx 化
DOCX="議事録_${SAFE}.docx"
"$ROOT_DIR/.venv/bin/python" "$SCRIPT_DIR/make_docx.py" "$WORK/minutes.md" "$WORK/$DOCX"

# 5) 出力を iCloud Drive へ(iPhone の「ファイル」アプリから確認できる)
cp "$WORK/transcript.txt" "$OUT/発言録_${SAFE}.txt"
cp "$WORK/minutes.md" "$OUT/議事録_${SAFE}.md"
cp "$WORK/$DOCX" "$OUT/"

# 6) メール送信の要否判定
SHOULD_MAIL="no"
case "${EMAIL_MODE:-never}" in
  always)
    SHOULD_MAIL="yes"
    ;;
  flag)
    if [[ "$BASE" == *"${EMAIL_FLAG:-送信}"* ]]; then
      SHOULD_MAIL="yes"
    fi
    ;;
esac
if [ "$SHOULD_MAIL" = "yes" ]; then
  echo "=== [$JOB] メール送信中…"
  "$ROOT_DIR/.venv/bin/python" "$SCRIPT_DIR/send_mail.py" \
    --subject "議事録: $BASE" \
    --body "自動生成された議事録を添付します。(ジョブ: $JOB)" \
    --attach "$WORK/$DOCX"
fi

# 7) 元ファイルを退避し、作業ディレクトリを削除
mv "$IN" "$ARCHIVE_DIR/${TS}_$(basename "$IN")"
rm -rf "$WORK"

echo "=== [$JOB] 完了 → $OUT"
