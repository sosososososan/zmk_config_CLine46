#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""音声ファイルを whisperX で文字起こしし、話者分離付きトランスクリプトを出力する。

使い方:
    python transcribe.py <input_audio> <output_dir>

出力:
    <output_dir>/transcript.txt   話者ラベル付きの読みやすいテキスト
    <output_dir>/transcript.json  タイムスタンプ付きの生データ

環境変数(config.env から渡される):
    WHISPER_MODEL, LANGUAGE, BATCH_SIZE, HF_TOKEN,
    DIARIZATION, MIN_SPEAKERS, MAX_SPEAKERS
"""
import gc
import json
import os
import sys
from pathlib import Path


def log(msg):
    print(f"[transcribe] {msg}", flush=True)


def fmt_ts(sec):
    sec = max(0, int(sec or 0))
    h, rem = divmod(sec, 3600)
    m, s = divmod(rem, 60)
    return f"{h:02d}:{m:02d}:{s:02d}"


def overlap(a_start, a_end, b_start, b_end):
    return max(0.0, min(a_end, b_end) - max(a_start, b_start))


def assign_speakers_by_overlap(segments, diar_df):
    """単語アライメントが使えない場合のフォールバック。

    話者分離結果(pandas DataFrame: start/end/speaker)と発話セグメントの
    重なり時間が最大の話者を割り当てる。
    """
    rows = list(diar_df.itertuples(index=False))
    for seg in segments:
        if seg.get("speaker"):
            continue
        best, best_ov = None, 0.0
        for r in rows:
            ov = overlap(seg.get("start", 0) or 0, seg.get("end", 0) or 0,
                         r.start, r.end)
            if ov > best_ov:
                best, best_ov = r.speaker, ov
        if best:
            seg["speaker"] = best


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    audio_path = sys.argv[1]
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    model_name = os.environ.get("WHISPER_MODEL", "large-v3-turbo").strip()
    lang = os.environ.get("LANGUAGE", "ja").strip()
    batch_size = int(os.environ.get("BATCH_SIZE", "4") or 4)
    hf_token = os.environ.get("HF_TOKEN", "").strip()
    diar_mode = os.environ.get("DIARIZATION", "auto").strip().lower()

    import whisperx  # 起動が重いので main 内で import

    device = "cpu"
    compute_type = "int8"

    log(f"音声読み込み: {audio_path}")
    audio = whisperx.load_audio(audio_path)
    duration = len(audio) / 16000.0
    log(f"長さ: {fmt_ts(duration)} / モデル: {model_name}")

    log("文字起こし中…(録音時間の数分の一〜同程度の時間がかかります)")
    model = whisperx.load_model(model_name, device,
                                compute_type=compute_type, language=lang)
    result = model.transcribe(audio, batch_size=batch_size, language=lang)
    segments = result["segments"]
    del model
    gc.collect()
    log(f"文字起こし完了: {len(segments)} セグメント")

    # 単語アライメント(話者割り当ての精度向上のため。失敗しても続行)
    aligned = None
    try:
        log("単語アライメント中…")
        model_a, meta = whisperx.load_align_model(language_code=lang,
                                                  device=device)
        aligned = whisperx.align(segments, model_a, meta, audio, device,
                                 return_char_alignments=False)
        del model_a
        gc.collect()
    except Exception as e:  # noqa: BLE001
        log(f"アライメントをスキップ({e})")

    work = aligned if aligned else {"segments": segments}

    # 話者分離
    diarized = False
    if diar_mode != "off" and hf_token:
        try:
            log("話者分離中…(初回はモデルのダウンロードが入ります)")
            try:
                from whisperx.diarize import DiarizationPipeline
            except ImportError:
                DiarizationPipeline = whisperx.DiarizationPipeline
            dia = DiarizationPipeline(use_auth_token=hf_token, device=device)
            kwargs = {}
            if os.environ.get("MIN_SPEAKERS", "").strip():
                kwargs["min_speakers"] = int(os.environ["MIN_SPEAKERS"])
            if os.environ.get("MAX_SPEAKERS", "").strip():
                kwargs["max_speakers"] = int(os.environ["MAX_SPEAKERS"])
            diar_df = dia(audio, **kwargs)
            try:
                work = whisperx.assign_word_speakers(diar_df, work)
            except Exception as e:  # noqa: BLE001
                log(f"単語単位の話者割り当てに失敗、セグメント単位で継続({e})")
            assign_speakers_by_overlap(work["segments"], diar_df)
            diarized = True
        except Exception as e:  # noqa: BLE001
            log(f"話者分離をスキップ({e})")
            log("HF_TOKEN と、pyannote 両モデルページでの Agree 済みかを確認してください")
    elif diar_mode != "off":
        log("HF_TOKEN 未設定のため話者分離をスキップします(全発言が話者1になります)")

    # 話者ラベルを登場順に 話者1, 話者2, … へ変換
    speaker_map = {}

    def jp_speaker(raw):
        if raw not in speaker_map:
            speaker_map[raw] = f"話者{len(speaker_map) + 1}"
        return speaker_map[raw]

    # 連続する同一話者の発言をまとめる
    blocks = []
    for seg in work["segments"]:
        text = (seg.get("text") or "").strip()
        if not text:
            continue
        raw = seg.get("speaker")
        if diarized:
            if raw:
                spk = jp_speaker(raw)
            elif blocks:
                spk = blocks[-1]["speaker"]  # 話者不明区間は直前の話者に接続
            else:
                spk = "話者1"
        else:
            spk = "話者1"
        if blocks and blocks[-1]["speaker"] == spk:
            blocks[-1]["text"] += text
            blocks[-1]["end"] = seg.get("end", blocks[-1]["end"])
        else:
            blocks.append({
                "speaker": spk,
                "start": seg.get("start", 0) or 0,
                "end": seg.get("end", 0) or 0,
                "text": text,
            })

    txt_path = out_dir / "transcript.txt"
    with open(txt_path, "w", encoding="utf-8") as f:
        f.write(f"# 自動文字起こし(長さ {fmt_ts(duration)} / "
                f"話者分離: {'あり' if diarized else 'なし'})\n\n")
        for b in blocks:
            f.write(f"[{fmt_ts(b['start'])}] {b['speaker']}: {b['text']}\n")

    json_path = out_dir / "transcript.json"
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(
            {
                "audio": str(audio_path),
                "duration_sec": duration,
                "model": model_name,
                "diarized": diarized,
                "segments": [
                    {
                        "start": s.get("start"),
                        "end": s.get("end"),
                        "speaker": (jp_speaker(s.get("speaker"))
                                    if diarized and s.get("speaker")
                                    else ("話者?" if diarized else "話者1")),
                        "text": (s.get("text") or "").strip(),
                    }
                    for s in work["segments"]
                ],
            },
            f,
            ensure_ascii=False,
            indent=1,
        )

    log(f"話者数: {max(1, len(speaker_map)) if diarized else '未分離'}")
    log(f"出力: {txt_path}")
    log(f"出力: {json_path}")


if __name__ == "__main__":
    main()
