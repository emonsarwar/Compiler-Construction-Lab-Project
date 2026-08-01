"""
Backend for the MiniLang web playground.

This is a THIN WRAPPER around the real compiler (src/main.c, built via
the project's own Makefile as `minilangc`). It does not reimplement any
compiler logic — every AST / TAC / error / program-output result shown
in the browser comes directly from running the actual, already-graded
compiler binary as a subprocess. This is deliberate: the web UI is a
new way to *use* the project, not a second copy of the project.

Endpoints:
    GET  /api/health              -> {"ok": true, "binary": "<path or null>"}
    GET  /api/examples            -> list of example .mc files shipped in examples/
    GET  /api/examples/<name>     -> {"source": "..."} for one example file
    POST /api/compile             -> run the compiler on posted source code

POST /api/compile body (JSON):
    {
        "source": "int x; ...",
        "optimize": false,   # pass -O
        "run": false         # pass -run
    }

Response (JSON):
    {
        "ok": true,
        "exit_code": 0,
        "stdout": "=== Abstract Syntax Tree ===\n...",
        "stderr": "",
        "sections": {              # stdout, split on the compiler's own
            "Abstract Syntax Tree": "...",   # "=== Section ===" headers
            "Annotated Abstract Syntax Tree": "...",
            "Three Address Code": "...",
            "Program Output (-run)": "..."
        }
    }

Run locally (after `make` has produced ../../minilangc):
    pip install flask flask-cors
    python3 server.py
    -> serves the API on http://localhost:5001
      (open web/frontend/index.html directly, or `python3 -m http.server`
      in web/frontend/ and visit http://localhost:8000)
"""
import os
import re
import shutil
import subprocess
import tempfile

from flask import Flask, jsonify, request
from flask_cors import CORS

# Project root is two levels up from this file (web/backend/server.py -> project root)
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXAMPLES_DIR = os.path.join(ROOT, "examples")
BINARY_CANDIDATES = [
    os.path.join(ROOT, "minilangc"),
    os.path.join(ROOT, "minilangc.exe"),
]

COMPILE_TIMEOUT_SECONDS = 5

app = Flask(__name__)
CORS(app)


def find_binary():
    for path in BINARY_CANDIDATES:
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return path
    return None


def try_build():
    """Best-effort `make` if the binary isn't there yet (needs flex/bison/gcc)."""
    if shutil.which("make") is None:
        return None
    try:
        subprocess.run(
            ["make"], cwd=ROOT, check=True, capture_output=True, timeout=60
        )
    except Exception:
        return None
    return find_binary()


def split_sections(stdout: str):
    """Mirror src/main.c's `print_section()` framing: '=== Title ===' headers."""
    sections = {}
    parts = re.split(r"\n?=== (.+?) ===\n", stdout)
    # parts[0] is any preamble before the first header; then alternating title/body
    for i in range(1, len(parts), 2):
        title = parts[i]
        body = parts[i + 1] if i + 1 < len(parts) else ""
        sections[title] = body.strip("\n")
    return sections


@app.route("/api/health")
def health():
    binary = find_binary() or try_build()
    return jsonify({"ok": True, "binary": binary})


@app.route("/api/examples")
def list_examples():
    if not os.path.isdir(EXAMPLES_DIR):
        return jsonify([])
    files = sorted(f for f in os.listdir(EXAMPLES_DIR) if f.endswith(".mc"))
    return jsonify(files)


@app.route("/api/examples/<name>")
def get_example(name):
    # Guard against path traversal - only allow plain filenames from the dir listing.
    safe_name = os.path.basename(name)
    path = os.path.join(EXAMPLES_DIR, safe_name)
    if not (safe_name.endswith(".mc") and os.path.isfile(path)):
        return jsonify({"error": "not found"}), 404
    with open(path, "r", encoding="utf-8") as f:
        return jsonify({"source": f.read()})


@app.route("/api/compile", methods=["POST"])
def compile_source():
    data = request.get_json(force=True, silent=True) or {}
    source = data.get("source", "")
    optimize = bool(data.get("optimize"))
    run_flag = bool(data.get("run"))

    if not source.strip():
        return jsonify({"ok": False, "error": "empty source"}), 400

    binary = find_binary() or try_build()
    if binary is None:
        return (
            jsonify(
                {
                    "ok": False,
                    "error": (
                        "minilangc binary not found and could not be built. "
                        "Run `make` in the project root first (requires flex, "
                        "bison, and gcc)."
                    ),
                }
            ),
            503,
        )

    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".mc", delete=False, dir=tempfile.gettempdir()
    ) as tmp:
        tmp.write(source)
        tmp_path = tmp.name

    args = [binary, tmp_path]
    if optimize:
        args.append("-O")
    if run_flag:
        args.append("-run")

    try:
        proc = subprocess.run(
            args,
            capture_output=True,
            text=True,
            timeout=COMPILE_TIMEOUT_SECONDS,
        )
        stdout, stderr, exit_code = proc.stdout, proc.stderr, proc.returncode
        timed_out = False
    except subprocess.TimeoutExpired:
        stdout, stderr, exit_code = "", "", -1
        timed_out = True
    finally:
        os.unlink(tmp_path)

    return jsonify(
        {
            "ok": exit_code == 0 and not timed_out,
            "timed_out": timed_out,
            "exit_code": exit_code,
            "stdout": stdout,
            "stderr": stderr,
            "sections": split_sections(stdout),
        }
    )


if __name__ == "__main__":
    port = int(os.environ.get("PORT", "5001"))
    print(f"MiniLang web backend starting on http://localhost:{port}")
    print(f"Project root: {ROOT}")
    binary = find_binary()
    if binary:
        print(f"Found compiler binary: {binary}")
    else:
        print("No compiler binary found yet — run `make` in the project root, "
              "or the first /api/compile call will try to build it automatically.")
    app.run(host="0.0.0.0", port=port, debug=True)
