# ボイスメモ → 議事録 自動化パイプライン

iPhone で録音したボイスメモを、自宅の MacBook Air(常時電源 ON)が自動で

1. **文字起こし + 話者分離**(whisperX / pyannote — すべてオンデバイス)
2. **議事録生成**(Claude Code CLI — Max サブスクリプション、API 課金なし)
3. **docx 化**(游ゴシックの整った Word 文書)
4. **必要に応じて会社 PC へメール送信**

まで行う仕組みです。結果は iCloud Drive に保存されるので、外出先でも iPhone の
「ファイル」アプリからすぐ確認できます。

```
iPhone(ボイスメモ録音)
   │ 共有 → "ファイル"に保存 → iCloud Drive/VoiceMemoInbox
   ▼ (iCloud が自動同期)
自宅 MacBook Air(launchd が常駐監視)
   ├ ffmpeg で wav 変換
   ├ whisperX で文字起こし+話者分離(オンデバイス)
   ├ claude -p で議事録生成(Max サブスク)
   ├ python-docx で 議事録_○○.docx を生成
   ├ iCloud Drive/MeetingMinutes/ へ出力  ← iPhoneから見える
   └ 条件に応じて SMTP でメール送信 → 会社 PC
```

---

## 今すぐ外出先(スマホだけ)でできる準備

Mac を触らずにできるのは以下の 4 つです。先に済ませておくと帰宅後が楽になります。

### 1. Hugging Face トークンの取得(話者分離に必須)

1. https://huggingface.co で無料アカウント作成
2. 同じアカウントでログインしたまま、以下 2 ページを開いて
   **「Agree and access repository」** を押す(フォーム入力あり):
   - https://huggingface.co/pyannote/speaker-diarization-3.1
   - https://huggingface.co/pyannote/segmentation-3.0
3. Settings → Access Tokens → **New token**(Read 権限)を作成し、
   `hf_...` の文字列をメモしておく → 帰宅後に `config.env` の `HF_TOKEN` に記入

### 2. Gmail アプリパスワードの取得(メール送信を使う場合)

1. Google アカウントで **2 段階認証プロセス** を有効化
2. https://myaccount.google.com/apppasswords でアプリパスワードを発行
3. 16 文字のパスワードをメモ → 帰宅後に `SMTP_PASS` に記入

### 3. iPhone ショートカットの作成(任意・共有を 1 タップにする)

ボイスメモの共有シートから直接インボックスに送れるようにします。

1. 「ショートカット」アプリ → 「+」で新規作成、名前は「議事録にする」など
2. アクション **「ファイルを保存」** を追加
   - 「保存先を毎回尋ねる」を **オフ** にし、保存先に
     iCloud Drive 直下の `VoiceMemoInbox` フォルダを指定
     (フォルダは「ファイル」アプリで先に作っておく)
3. ショートカット設定(ⓘ)で **「共有シートに表示」をオン**、
   受け入れる入力を「メディア」「ファイル」に
4. 以後: ボイスメモ → 共有 → 「議事録にする」でインボックスに入る

※ ショートカットを作らなくても、共有 → **「"ファイル"に保存」** →
`VoiceMemoInbox` を選ぶだけでも同じことができます。

### 4. 議事録フォーマットの確認・修正

議事録の構成(まとめ方)は `templates/minutes_prompt.md` で定義しています。
GitHub のスマホ画面からでも編集できるので、好みの形式(章立て・文体など)に
直しておけば、そのまま反映されます。

---

## 帰宅後のセットアップ(ターミナルのみ・約 15 分)

GUI 操作はほぼ不要です(最後の初回実行時に macOS の許可ダイアログを
1〜2 回「許可」するだけ)。

```bash
# 1. リポジトリを取得(既にある場合は pull)
git clone <このリポジトリのURL> && cd <リポジトリ>/voicememo-minutes
git checkout claude/iphone-voice-memo-meeting-notes-vbmzmo

# 2. Claude Code CLI が未導入なら(導入済みならスキップ)
curl -fsSL https://claude.ai/install.sh | bash
claude   # 一度起動して Max アカウントでログイン → 終了

# 3. インストーラ実行(ffmpeg/Python/whisperX 導入、launchd 常駐登録まで自動)
./bin/install.sh

# 4. トークン類を記入
nano config/config.env    # HF_TOKEN、(必要なら)SMTP_* と EMAIL_MODE

# 5. 動作確認
./bin/selftest.sh           # 依存・Claude 応答・docx 変換のチェック
./bin/selftest.sh --models  # モデルを先にダウンロード(推奨・数GB)

# 6. スリープ防止(電源接続時にスリープさせない)
sudo pmset -c sleep 0
# 蓋を閉じたまま運用するなら(電源接続が前提):
sudo pmset -a disablesleep 1
```

実際の音声で最初の 1 本を試すには:

```bash
./bin/selftest.sh --audio ~/Downloads/テスト録音.m4a
```

---

## 日々の使い方

1. iPhone のボイスメモで録音
2. 共有 → 「議事録にする」(または "ファイル"に保存 → `VoiceMemoInbox`)
3. あとは全自動。しばらくすると iCloud Drive の
   `MeetingMinutes/<録音名>_<日時>/` に以下が生成されます:
   - `議事録_○○.docx` — 指定フォーマットの議事録(Word)
   - `議事録_○○.md` — 同内容の Markdown
   - `発言録_○○.txt` — タイムスタンプ・話者ラベル付きの発言録
4. メール送信は `config.env` の `EMAIL_MODE` で制御:
   - `flag`(推奨): ファイル名に「送信」を含めたときだけ会社 PC へ送信
     (例: 録音を「経営会議_送信」にリネームして共有)
   - `always` / `never`

処理時間の目安: 1 時間の会議で、文字起こし 20〜60 分(モデルと機種による)+
議事録生成 数分。

---

## ファイル構成

| パス | 役割 |
|---|---|
| `bin/install.sh` | 初回セットアップ(依存導入・launchd 登録) |
| `bin/process_inbox.sh` | インボックス走査(launchd から起動される入口) |
| `bin/pipeline.sh` | 1 ファイルの処理全体(変換→文字起こし→議事録→docx→メール) |
| `bin/transcribe.py` | whisperX による文字起こし+話者分離 |
| `bin/make_docx.py` | Markdown → docx 変換(游ゴシック) |
| `bin/send_mail.py` | SMTP でのメール送信 |
| `bin/selftest.sh` | 動作確認・モデル事前DL・テストメール |
| `templates/minutes_prompt.md` | **議事録フォーマット定義(自由に編集)** |
| `config/config.env.example` | 設定のひな形(コピーして `config.env` に) |
| `launchd/*.plist.template` | 常駐設定(フォルダ監視+定期起動、スリープ防止) |
| `samples/` | サンプル文字起こしと期待される議事録の例 |

---

## トラブルシューティング

- **ログ**: `logs/pipeline.log`(処理の全記録)、`logs/launchd.*.log`
- **手動で今すぐ処理**: `./bin/process_inbox.sh` を直接実行
- **失敗した音声**: `MeetingMinutes/_failed/` に移動されます。原因修正後に
  `VoiceMemoInbox` へ戻せば再処理されます
- **Claude が応答しない**: `claude` を起動し `/login`。Max のレート制限
  (5 時間ウィンドウ)に達している場合は時間を置く
- **話者分離がスキップされる**: `HF_TOKEN` 未記入か、pyannote の 2 つの
  モデルページで Agree していないのが原因のほとんど
- **メモリ 8GB で遅い/落ちる**: `config.env` の `WHISPER_MODEL` を
  `medium` か `small` に変更
- **常駐の停止**: `launchctl bootout gui/$(id -u)/com.voicememo.minutes`
  (スリープ防止は `com.voicememo.keepawake`)

## 制約・注意

- 話者ラベルは「話者1/話者2…」です。会話中の自己紹介や呼びかけから
  Claude が名前を推定できた場合のみ「話者1(田中)」と付記されます。
- 1 台の iPhone を置いた遠距離録音では話者分離の精度が落ちます。
  可能ならマイクを話者の中央に置いてください。
- `config.env`(トークン・パスワード)は `.gitignore` 済みで
  リポジトリにはコミットされません。
- 会議内容を個人のクラウド経由で処理して社用メールへ送ることは、
  勤務先の情報セキュリティポリシーに抵触する可能性があります。
  運用前に確認することをおすすめします。
