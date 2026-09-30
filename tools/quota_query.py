#!/usr/bin/env python3
"""Read-only quota reporter for the Cockpit Agent API.

The credential file is intentionally supplied at runtime and is never part of
the repository.  Only the fixed, read-only quota proxy call documented by the
service contract is used; this client does not expose account-control or proxy
mutation operations.
"""

from __future__ import annotations

import argparse
import base64
import json
import ssl
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


MAX_ATTEMPTS = 4
RETRY_DELAYS_SECONDS = (1, 2, 4)
FIXED_QUOTA_URL = "https://chatgpt.com/backend-api/wham/usage"


class SameHostRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        old = urllib.parse.urlsplit(req.full_url)
        new = urllib.parse.urlsplit(newurl)
        if (old.scheme, old.netloc) != (new.scheme, new.netloc):
            raise urllib.error.HTTPError(
                req.full_url, code, "cross-host redirect refused", headers, fp
            )
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def load_config(path: Path) -> dict[str, str]:
    data = json.loads(path.read_text(encoding="utf-8"))
    root = str(data["base_url"]).rstrip("/")
    parsed = urllib.parse.urlsplit(root)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname:
        raise ValueError("invalid base_url")
    if parsed.path not in {"", "/"}:
        raise ValueError("base_url must not include an API path")
    if str(data.get("auth_type", "basic")).lower() != "basic":
        raise ValueError("this client only accepts the documented Basic Auth configuration")
    return {
        "root": root,
        "username": str(data["username"]),
        "password": str(data["password"]),
    }


def opener_for(url: str) -> urllib.request.OpenerDirector:
    handlers: list[Any] = [SameHostRedirect()]
    if urllib.parse.urlsplit(url).scheme == "https":
        handlers.append(urllib.request.HTTPSHandler(context=ssl.create_default_context()))
    return urllib.request.build_opener(*handlers)


def auth_header(config: dict[str, str]) -> str:
    raw = f"{config['username']}:{config['password']}".encode("utf-8")
    return "Basic " + base64.b64encode(raw).decode("ascii")


def request_json(
    config: dict[str, str],
    path: str,
    *,
    method: str = "GET",
    body: dict[str, Any] | None = None,
) -> Any:
    if not path.startswith("/") or ".." in path:
        raise ValueError("unsafe API path")
    url = config["root"] + path
    payload = None if body is None else json.dumps(body).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=payload,
        method=method,
        headers={
            "Authorization": auth_header(config),
            "Accept": "application/json",
            "Content-Type": "application/json",
            "User-Agent": "FoloToy-Quota-Reader/1.0",
        },
    )
    try:
        with opener_for(url).open(request, timeout=30) as response:
            return json.load(response)
    except urllib.error.HTTPError as exc:
        # Do not print response bodies: they may contain account metadata.
        raise RuntimeError(f"HTTP {exc.code} for {path}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"network error for {path}: {exc.reason}") from exc


def mask_label(value: Any) -> str:
    text = str(value or "")
    if "@" in text:
        local, domain = text.split("@", 1)
        return (local[:2] + "***@" + domain) if local else "***@" + domain
    return text[:3] + "***" if len(text) > 3 else "***"


def json_body(value: Any) -> dict[str, Any]:
    if isinstance(value, dict):
        return value
    if isinstance(value, str):
        parsed = json.loads(value)
        if isinstance(parsed, dict):
            return parsed
    raise ValueError("unexpected JSON body")


def read_quota_for_account(
    config: dict[str, str], account: dict[str, Any]
) -> dict[str, Any]:
    label = mask_label(account.get("email") or account.get("label") or account.get("name"))
    request_body = {
        "auth_index": str(account["auth_index"]),
        "method": "GET",
        "url": FIXED_QUOTA_URL,
        "header": {
            "Authorization": "Bearer $TOKEN$",
            "Accept": "application/json",
            "Referer": "https://chatgpt.com/",
            "Origin": "https://chatgpt.com",
            "User-Agent": "Mozilla/5.0",
        },
    }
    last_status: int | None = None
    for attempt in range(1, MAX_ATTEMPTS + 1):
        try:
            result = request_json(
                config,
                "/operator-api/management/api-call",
                method="POST",
                body=request_body,
            )
            last_status = int(result.get("status_code") or 0)
            if last_status != 200:
                raise RuntimeError(f"upstream HTTP {last_status}")
            usage = json_body(result.get("body") or "{}")
            rate = usage.get("rate_limit") or {}
            if not rate:
                raise RuntimeError("quota data missing")
            return {
                "label": label,
                "plan": usage.get("plan_type") or account.get("plan"),
                "online": True,
                "query_status": "ok",
                "query_attempts": attempt,
                "allowed": rate.get("allowed"),
                "limit_reached": rate.get("limit_reached"),
                "primary_window": rate.get("primary_window"),
                "secondary_window": rate.get("secondary_window"),
            }
        except (RuntimeError, ValueError, TypeError):
            if attempt < MAX_ATTEMPTS:
                time.sleep(RETRY_DELAYS_SECONDS[attempt - 1])
    return {
        "label": label,
        "plan": account.get("plan"),
        "online": None,
        "query_status": "unknown",
        "query_attempts": MAX_ATTEMPTS,
        "last_status": last_status,
    }


def collect(config: dict[str, str]) -> dict[str, Any]:
    health = request_json(config, "/agent/v1/health")
    summary = request_json(config, "/agent/v1/accounts/summary")
    files_payload = request_json(config, "/operator-api/management/auth-files")
    files = files_payload.get("files") or []
    codex_accounts = [
        item
        for item in files
        if str(item.get("type") or item.get("provider") or "").lower() == "codex"
        and item.get("auth_index")
    ]
    quota_accounts = [read_quota_for_account(config, item) for item in codex_accounts]
    return {
        "checked_at": datetime.now(timezone.utc).isoformat(),
        "health": {
            "ok": health.get("ok"),
            "server_time": health.get("server_time"),
        },
        "account_summary": summary.get("data", {}).get("totals"),
        "accounts": quota_accounts,
    }


def remaining_percent(window: dict[str, Any] | None) -> float | None:
    if not isinstance(window, dict):
        return None
    used = window.get("used_percent")
    if not isinstance(used, (int, float)):
        return None
    return max(0.0, min(100.0, 100.0 - float(used)))


def countdown(window: dict[str, Any] | None) -> str:
    if not isinstance(window, dict):
        return "未知"
    seconds = window.get("reset_after_seconds")
    if not isinstance(seconds, (int, float)):
        return "未知"
    total = max(0, int(seconds))
    days, remainder = divmod(total, 86400)
    hours, remainder = divmod(remainder, 3600)
    minutes, _ = divmod(remainder, 60)
    if days:
        return f"{days}天{hours}小时"
    if hours:
        return f"{hours}小时{minutes}分钟"
    return f"{minutes}分钟"


def report(data: dict[str, Any]) -> str:
    lines = [
        "额度查询",
        f"检查时间（UTC）：{data['checked_at']}",
        f"服务状态：{'正常' if data['health'].get('ok') else '异常'}",
    ]
    totals = data.get("account_summary") or {}
    if isinstance(totals, dict):
        lines.append(
            f"账号池：总数 {totals.get('total', '?')}，"
            f"可用 {totals.get('available', '?')}"
        )
    for index, item in enumerate(data.get("accounts") or [], 1):
        lines.append("")
        lines.append(f"{index}. {item.get('label', '未知账号')} [{item.get('plan', '?')}]")
        if item.get("query_status") != "ok":
            lines.append("   状态：额度查询失败或暂不可用")
            continue
        lines.append(f"   当前状态：{'可用' if item.get('allowed') else '受限'}")
        for title, key in (("主要额度", "primary_window"), ("次要额度", "secondary_window")):
            window = item.get(key)
            if not isinstance(window, dict):
                continue
            left = remaining_percent(window)
            left_text = f"{left:.0f}%" if left is not None else "未知"
            lines.append(f"   {title}：剩余 {left_text}，重置倒计时 {countdown(window)}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="read-only Cockpit quota query")
    parser.add_argument("--credentials", type=Path, required=True)
    parser.add_argument("--json", action="store_true", dest="as_json")
    args = parser.parse_args()
    try:
        data = collect(load_config(args.credentials))
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"额度查询失败：{exc}", file=sys.stderr)
        return 2
    if args.as_json:
        print(json.dumps(data, ensure_ascii=False, indent=2))
    else:
        print(report(data))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
