#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""議事録 docx をメールで送信する。SMTP 設定は環境変数(config.env)から。

使い方:
    python send_mail.py --subject "議事録: ○○" \
        [--body "本文" | --body-file 本文.txt] [--attach 議事録.docx] ...

必要な環境変数:
    SMTP_HOST, SMTP_PORT, SMTP_USER, SMTP_PASS, EMAIL_TO
    (EMAIL_FROM は省略時 SMTP_USER)
"""
import argparse
import mimetypes
import os
import smtplib
import ssl
import sys
from email.message import EmailMessage
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--subject", required=True)
    ap.add_argument("--body", default="")
    ap.add_argument("--body-file", default="")
    ap.add_argument("--attach", action="append", default=[])
    args = ap.parse_args()

    host = os.environ.get("SMTP_HOST", "").strip()
    port = int(os.environ.get("SMTP_PORT", "587") or 587)
    user = os.environ.get("SMTP_USER", "").strip()
    password = os.environ.get("SMTP_PASS", "").strip()
    sender = os.environ.get("EMAIL_FROM", "").strip() or user
    to = os.environ.get("EMAIL_TO", "").strip()

    missing = [n for n, v in [("SMTP_HOST", host), ("SMTP_USER", user),
                              ("SMTP_PASS", password), ("EMAIL_TO", to)] if not v]
    if missing:
        print(f"[send_mail] config.env の設定が不足しています: {', '.join(missing)}",
              file=sys.stderr)
        sys.exit(1)

    body = args.body
    if args.body_file:
        body = Path(args.body_file).read_text(encoding="utf-8")

    msg = EmailMessage()
    msg["Subject"] = args.subject
    msg["From"] = sender
    msg["To"] = to
    msg.set_content(body or "自動生成された議事録を添付します。")

    for path in args.attach:
        p = Path(path)
        ctype, _ = mimetypes.guess_type(p.name)
        maintype, subtype = (ctype or "application/octet-stream").split("/", 1)
        msg.add_attachment(p.read_bytes(), maintype=maintype,
                           subtype=subtype, filename=p.name)

    context = ssl.create_default_context()
    if port == 465:
        with smtplib.SMTP_SSL(host, port, context=context) as s:
            s.login(user, password)
            s.send_message(msg)
    else:
        with smtplib.SMTP(host, port, timeout=60) as s:
            s.starttls(context=context)
            s.login(user, password)
            s.send_message(msg)

    print(f"[send_mail] 送信完了: {to}")


if __name__ == "__main__":
    main()
