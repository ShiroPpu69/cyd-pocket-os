"""Authorize CYD OS with Spotify and write the local firmware config."""

from __future__ import annotations

import base64
import argparse
import getpass
import json
import re
import secrets
import threading
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path


REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPES = (
    "user-read-playback-state "
    "user-read-currently-playing "
    "user-modify-playback-state "
    "playlist-read-private"
)
CONFIG_PATH = (
    Path(__file__).resolve().parents[1]
    / "esp32_system"
    / "spotify_config.h"
)


def exchange_code(client_id: str, client_secret: str, code: str) -> dict:
    credentials = base64.b64encode(
        f"{client_id}:{client_secret}".encode("utf-8")
    ).decode("ascii")
    body = urllib.parse.urlencode(
        {
            "grant_type": "authorization_code",
            "code": code,
            "redirect_uri": REDIRECT_URI,
        }
    ).encode("ascii")
    request = urllib.request.Request(
        "https://accounts.spotify.com/api/token",
        data=body,
        headers={
            "Authorization": f"Basic {credentials}",
            "Content-Type": "application/x-www-form-urlencoded",
        },
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def cpp_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def load_existing_credentials() -> tuple[str, str]:
    if not CONFIG_PATH.exists():
        raise SystemExit("No existing spotify_config.h was found.")
    content = CONFIG_PATH.read_text(encoding="utf-8")

    def read_value(name: str) -> str:
        match = re.search(
            rf'{name}\s*=\s*"((?:\\.|[^"\\])*)"', content
        )
        if not match:
            raise SystemExit(f"Could not read {name} from spotify_config.h.")
        return bytes(match.group(1), "utf-8").decode("unicode_escape")

    return read_value("SPOTIFY_CLIENT_ID"), read_value(
        "SPOTIFY_CLIENT_SECRET"
    )


def save_config(
    client_id: str, client_secret: str, refresh_token: str
) -> None:
    CONFIG_PATH.write_text(
        "#pragma once\n\n"
        f'constexpr const char* SPOTIFY_CLIENT_ID = "{cpp_string(client_id)}";\n'
        f'constexpr const char* SPOTIFY_CLIENT_SECRET = "{cpp_string(client_secret)}";\n'
        f'constexpr const char* SPOTIFY_REFRESH_TOKEN = "{cpp_string(refresh_token)}";\n'
        'constexpr const char* SPOTIFY_MARKET = "HK";\n',
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--reuse-config",
        action="store_true",
        help="reuse the Client ID and Client Secret already stored locally",
    )
    args = parser.parse_args()

    print("Spotify CYD OS authorization")
    print(f"Dashboard Redirect URI must be exactly: {REDIRECT_URI}")
    if args.reuse_config:
        client_id, client_secret = load_existing_credentials()
        print("Reusing locally stored Spotify application credentials.")
    else:
        client_id = input("Client ID: ").strip()
        client_secret = getpass.getpass("Client Secret: ").strip()
    if not client_id or not client_secret:
        raise SystemExit("Client ID and Client Secret are required.")

    state = secrets.token_urlsafe(24)
    result: dict[str, str] = {}
    completed = threading.Event()

    class CallbackHandler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:  # noqa: N802
            parsed = urllib.parse.urlparse(self.path)
            params = urllib.parse.parse_qs(parsed.query)
            if parsed.path != "/callback":
                self.send_error(404)
                return
            if params.get("state", [""])[0] != state:
                result["error"] = "OAuth state mismatch"
            elif "error" in params:
                result["error"] = params["error"][0]
            else:
                result["code"] = params.get("code", [""])[0]

            message = (
                "Authorization received. You may close this window."
                if "code" in result
                else f"Authorization failed: {result.get('error', 'unknown')}"
            )
            body = message.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            completed.set()

        def log_message(self, _format: str, *_args: object) -> None:
            pass

    query = urllib.parse.urlencode(
        {
            "client_id": client_id,
            "response_type": "code",
            "redirect_uri": REDIRECT_URI,
            "scope": SCOPES,
            "state": state,
            "show_dialog": "true",
        }
    )
    authorization_url = f"https://accounts.spotify.com/authorize?{query}"

    server = HTTPServer(("127.0.0.1", 8888), CallbackHandler)
    server.timeout = 1
    print("Opening Spotify authorization in your browser...")
    webbrowser.open(authorization_url)
    while not completed.is_set():
        server.handle_request()
    server.server_close()

    if "code" not in result:
        raise SystemExit(f"Authorization failed: {result.get('error')}")

    try:
        token = exchange_code(client_id, client_secret, result["code"])
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise SystemExit(f"Token exchange failed: {error.code} {detail}") from error

    refresh_token = token.get("refresh_token", "")
    if not refresh_token:
        raise SystemExit("Spotify did not return a refresh token.")
    save_config(client_id, client_secret, refresh_token)
    print(f"Saved local firmware config: {CONFIG_PATH}")
    print("Recompile and upload the firmware to your board's serial port.")


if __name__ == "__main__":
    main()
