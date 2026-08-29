#!/bin/bash
# 自宅 Mac での初回セットアップ。すべてターミナルで完結する
# (GUI 操作は、初回実行時に macOS が出すファイルアクセス許可ダイアログのみ)。
#
# 使い方:
#   cd <このリポジトリ>/voicememo-minutes
#   ./bin/install.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ "$(uname)" != "Darwin" ] && [ "${FORCE:-0}" != "1" ]; then
  echo "このスクリプトは macOS 用です(テスト用の強制実行: FORCE=1 ./bin/install.sh)"
  exit 1
fi

echo "==> 1/6 設定ファイル"
if [ ! -f "$ROOT_DIR/config/config.env" ]; then
  cp "$ROOT_DIR/config/config.env.example" "$ROOT_DIR/config/config.env"
  echo "    config/config.env を作成しました(後で HF_TOKEN 等を記入してください)"
else
  echo "    既存の config/config.env をそのまま使います"
fi
set -a
# shellcheck disable=SC1091
source "$ROOT_DIR/config/config.env"
set +a

echo "==> 2/6 依存ツール確認"
if ! command -v ffmpeg >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then
    echo "    ffmpeg をインストールします(Homebrew)"
    brew install ffmpeg
  else
    echo "    ffmpeg がありません。先に Homebrew を入れてから再実行してください:"
    echo '      /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"'
    exit 1
  fi
fi
echo "    ffmpeg: $(command -v ffmpeg)"

# Python 3.10 以上を探す(なければ Homebrew で導入)
PYBIN=""
for c in python3.12 python3.11 python3.10 python3; do
  if command -v "$c" >/dev/null 2>&1; then
    if "$c" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null; then
      PYBIN="$(command -v "$c")"
      break
    fi
  fi
done
if [ -z "$PYBIN" ]; then
  if command -v brew >/dev/null 2>&1; then
    echo "    Python 3.12 をインストールします(Homebrew)"
    brew install python@3.12
    PYBIN="$(brew --prefix)/bin/python3.12"
  else
    echo "    Python 3.10 以上が見つかりません。Homebrew を入れてから再実行してください。"
    exit 1
  fi
fi
echo "    Python: $PYBIN"

echo "==> 3/6 Python 仮想環境と依存ライブラリ(初回は 10 分前後かかります)"
if [ ! -d "$ROOT_DIR/.venv" ]; then
  "$PYBIN" -m venv "$ROOT_DIR/.venv"
fi
"$ROOT_DIR/.venv/bin/pip" install --quiet --upgrade pip wheel setuptools
"$ROOT_DIR/.venv/bin/pip" install --quiet whisperx python-docx
echo "    whisperx / python-docx を導入しました"

echo "==> 4/6 フォルダ作成"
mkdir -p "$INBOX_DIR" "$OUTPUT_DIR" "$ARCHIVE_DIR" "$ROOT_DIR/logs" "$ROOT_DIR/work"
echo "    インボックス: $INBOX_DIR"
echo "    出力先:       $OUTPUT_DIR"

echo "==> 5/6 Claude Code CLI 検出"
CLAUDE_PATH=""
for c in claude "$HOME/.claude/local/claude" /opt/homebrew/bin/claude /usr/local/bin/claude; do
  if command -v "$c" >/dev/null 2>&1; then
    CLAUDE_PATH="$(command -v "$c")"
    break
  fi
done
if [ -n "$CLAUDE_PATH" ]; then
  echo "    claude: $CLAUDE_PATH"
  # launchd からは PATH が引けないので、絶対パスを config.env に書き込む
  sed -i '' "s|^CLAUDE_BIN=.*|CLAUDE_BIN=\"$CLAUDE_PATH\"|" "$ROOT_DIR/config/config.env"
else
  echo "    ⚠ claude コマンドが見つかりません。以下でインストール後、再度 install.sh を実行してください:"
  echo "      curl -fsSL https://claude.ai/install.sh | bash"
  echo "      claude       # 一度起動して Max アカウントでログイン"
fi

echo "==> 6/6 launchd 常駐設定"
LA_DIR="$HOME/Library/LaunchAgents"
mkdir -p "$LA_DIR"
for tmpl in "$ROOT_DIR/launchd/"*.plist.template; do
  name="$(basename "${tmpl%.template}")"
  dest="$LA_DIR/$name"
  label="${name%.plist}"
  sed -e "s|__ROOT__|$ROOT_DIR|g" -e "s|__INBOX__|$INBOX_DIR|g" "$tmpl" > "$dest"
  launchctl bootout "gui/$(id -u)/$label" 2>/dev/null || true
  launchctl bootstrap "gui/$(id -u)" "$dest"
  echo "    常駐登録: $label"
done

cat <<'EOF'

================================================================
セットアップ完了。残りの手順:

 1. 設定を記入:   nano config/config.env
      - HF_TOKEN(話者分離に必要)
      - メール送信を使うなら SMTP_USER / SMTP_PASS / EMAIL_TO と
        EMAIL_MODE(always / flag)
 2. Claude ログイン確認:
      claude -p "pong とだけ返して"
 3. 動作確認:
      ./bin/selftest.sh            # 基本チェック
      ./bin/selftest.sh --models   # モデルの事前ダウンロード(推奨)
 4. スリープ防止(電源接続時にスリープさせない):
      sudo pmset -c sleep 0
    蓋を閉じたまま運用したい場合はさらに:
      sudo pmset -a disablesleep 1
 5. 使い方: iPhone のボイスメモ → 共有 → 「"ファイル"に保存」→
    iCloud Drive/VoiceMemoInbox に保存すれば、あとは全自動です。

 ※ 初回の自動実行時に「バックグラウンド項目」やファイルアクセスの
    許可ダイアログが出たら「許可」を押してください(1 回だけ)。
================================================================
EOF
