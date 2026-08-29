#!/bin/bash
# インボックス(iCloud Drive)を走査し、未処理の音声を順に処理する。
# launchd(com.voicememo.minutes)から自動起動される。手動実行も可。
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG="${VOICEMEMO_CONFIG:-$ROOT_DIR/config/config.env}"
if [ ! -f "$CONFIG" ]; then
  echo "設定ファイルがありません: $CONFIG"
  exit 1
fi
set -a
# shellcheck disable=SC1090
source "$CONFIG"
set +a

LOG_DIR="$ROOT_DIR/logs"
mkdir -p "$LOG_DIR" "$ROOT_DIR/work"
exec >> "$LOG_DIR/pipeline.log" 2>&1

echo "----- $(date '+%Y-%m-%d %H:%M:%S') インボックス走査 -----"

# 多重起動防止(処理中に launchd から再度起動された場合は何もしない)
LOCK="$ROOT_DIR/work/.lock"
if ! mkdir "$LOCK" 2>/dev/null; then
  echo "別の処理が実行中のためスキップ"
  exit 0
fi
trap 'rmdir "$LOCK" 2>/dev/null' EXIT

mkdir -p "$INBOX_DIR"

# iCloud 上のプレースホルダ(未ダウンロードのファイル)を実体化する
if command -v brctl >/dev/null 2>&1; then
  brctl download "$INBOX_DIR" >/dev/null 2>&1 || true
  sleep 5
fi

shopt -s nullglob nocaseglob
FOUND=0
for f in "$INBOX_DIR"/*.m4a "$INBOX_DIR"/*.mp3 "$INBOX_DIR"/*.wav \
         "$INBOX_DIR"/*.aac "$INBOX_DIR"/*.mp4 "$INBOX_DIR"/*.aiff \
         "$INBOX_DIR"/*.flac; do
  [ -f "$f" ] || continue
  FOUND=1

  # 同期途中のファイルはスキップ(サイズが安定してから処理する)
  S1=$(stat -f%z "$f" 2>/dev/null || echo 0)
  sleep 5
  S2=$(stat -f%z "$f" 2>/dev/null || echo 0)
  if [ "$S1" != "$S2" ] || [ "$S1" = "0" ]; then
    echo "同期中のためスキップ(次回処理): $f"
    continue
  fi

  if "$SCRIPT_DIR/pipeline.sh" "$f"; then
    echo "成功: $f"
  else
    echo "失敗: $f → _failed へ移動(ログ: logs/pipeline.log)"
    mkdir -p "$OUTPUT_DIR/_failed"
    mv "$f" "$OUTPUT_DIR/_failed/" || true
  fi
done

if [ "$FOUND" = "0" ]; then
  echo "新しい音声はありません"
fi
