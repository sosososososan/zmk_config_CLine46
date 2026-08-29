#!/bin/bash
# 動作確認スクリプト
#
# 使い方:
#   ./bin/selftest.sh                    基本チェック(依存・claude・docx 変換)
#   ./bin/selftest.sh --models           Whisper/話者分離モデルの事前ダウンロード
#   ./bin/selftest.sh --email            テストメールを送信
#   ./bin/selftest.sh --audio <ファイル>  実際の音声でフルパイプラインを実行
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG="${VOICEMEMO_CONFIG:-$ROOT_DIR/config/config.env}"
if [ -f "$CONFIG" ]; then
  set -a
  # shellcheck disable=SC1090
  source "$CONFIG"
  set +a
fi

PASS=0
FAIL=0
ok() { echo "  ✅ $1"; PASS=$((PASS + 1)); }
ng() { echo "  ❌ $1"; FAIL=$((FAIL + 1)); }

case "${1:-}" in
  --models)
    echo "== モデルの事前ダウンロード(数 GB・回線次第で時間がかかります)=="
    exec "$ROOT_DIR/.venv/bin/python" - <<'PY'
import os

model = os.environ.get("WHISPER_MODEL", "large-v3-turbo")
lang = os.environ.get("LANGUAGE", "ja")

import whisperx

print(f"Whisper モデル取得: {model}")
whisperx.load_model(model, "cpu", compute_type="int8", language=lang)
print("アライメントモデル取得")
whisperx.load_align_model(language_code=lang, device="cpu")

tok = os.environ.get("HF_TOKEN", "").strip()
if tok:
    print("話者分離モデル取得(pyannote)")
    try:
        from whisperx.diarize import DiarizationPipeline
    except ImportError:
        from whisperx import DiarizationPipeline
    DiarizationPipeline(use_auth_token=tok, device="cpu")
    print("話者分離モデル OK")
else:
    print("HF_TOKEN 未設定のため話者分離モデルはスキップしました")
print("完了")
PY
    ;;

  --email)
    echo "== テストメール送信 =="
    exec "$ROOT_DIR/.venv/bin/python" "$SCRIPT_DIR/send_mail.py" \
      --subject "【テスト】議事録パイプライン" \
      --body "これはボイスメモ議事録パイプラインからのテストメールです。"
    ;;

  --audio)
    shift
    if [ -z "${1:-}" ]; then
      echo "使い方: ./bin/selftest.sh --audio <音声ファイル>"
      exit 2
    fi
    echo "== フルパイプラインテスト: $1 =="
    exec "$SCRIPT_DIR/pipeline.sh" "$1"
    ;;

  "")
    echo "== 基本チェック =="
    if command -v ffmpeg >/dev/null 2>&1; then ok "ffmpeg"; else ng "ffmpeg がありません(install.sh を実行)"; fi
    if [ -f "$CONFIG" ]; then ok "config.env"; else ng "config.env がありません(install.sh を実行)"; fi
    if [ -x "$ROOT_DIR/.venv/bin/python" ]; then ok "Python venv"; else ng "venv がありません(install.sh を実行)"; fi
    if "$ROOT_DIR/.venv/bin/python" -c "import docx" 2>/dev/null; then ok "python-docx"; else ng "python-docx が import できません"; fi
    if "$ROOT_DIR/.venv/bin/python" -c "import whisperx" 2>/dev/null; then ok "whisperx"; else ng "whisperx が import できません"; fi

    CLAUDE_BIN="${CLAUDE_BIN:-claude}"
    if command -v "$CLAUDE_BIN" >/dev/null 2>&1; then
      ok "claude CLI ($(command -v "$CLAUDE_BIN"))"
      echo "  … Claude 応答テスト中(数十秒かかることがあります)"
      R="$("$CLAUDE_BIN" -p --output-format text "pong とだけ返してください" 2>/dev/null || true)"
      if [ -n "$R" ]; then
        ok "Claude 応答: $R"
      else
        ng "Claude が応答しません(claude を起動して /login でログインしてください)"
      fi
    else
      ng "claude CLI が見つかりません(curl -fsSL https://claude.ai/install.sh | bash)"
    fi

    echo "== docx 変換テスト =="
    mkdir -p "$ROOT_DIR/work"
    if "$ROOT_DIR/.venv/bin/python" "$SCRIPT_DIR/make_docx.py" \
        "$ROOT_DIR/samples/sample_minutes.md" "$ROOT_DIR/work/selftest.docx" >/dev/null; then
      ok "docx 生成 → work/selftest.docx(開いて体裁を確認できます)"
    else
      ng "docx 生成に失敗"
    fi

    if [ -n "${HF_TOKEN:-}" ]; then
      ok "HF_TOKEN 設定済み(話者分離が有効)"
    else
      ng "HF_TOKEN 未設定(話者分離なしで動作します)"
    fi

    echo
    echo "結果: 成功 $PASS / 失敗 $FAIL"
    if [ "$FAIL" -gt 0 ]; then exit 1; fi
    ;;

  *)
    echo "不明なオプション: $1"
    echo "使い方: selftest.sh [--models | --email | --audio <ファイル>]"
    exit 2
    ;;
esac
