# MiniLang Web Playground (frontend + backend)

This adds a small **web front-end** and **backend API** on top of the
existing `minilangc` CLI compiler — a browser page where you can type
MiniLang source, hit Compile, and see the AST / TAC / errors / program
output.

**Important, for the report/viva:** this is a thin wrapper, not a
second implementation of the compiler. The backend does nothing but
call the real `minilangc` binary (built from `src/` via the project's
own `Makefile`) as a subprocess and forward its stdout/stderr to the
browser. Every result shown in the UI is genuinely produced by the
same compiler that `make test` already validates — nothing about the
lexer, parser, semantic analysis, or TAC generation is duplicated
here.

```
web/
├── backend/
│   ├── server.py         Flask API: /api/compile, /api/examples, /api/health
│   └── requirements.txt
└── frontend/
    └── index.html         Single-page UI (vanilla HTML/CSS/JS, no build step)
```

## Running it

1. Build the compiler first, from the project root (needs flex, bison, gcc):

   ```bash
   make
   ```

2. Start the backend:

   ```bash
   cd web/backend
   pip install -r requirements.txt
   python3 server.py
   # -> http://localhost:5001
   ```

3. Open the frontend — either just double-click `web/frontend/index.html`,
   or serve it so relative paths behave the same as a real deployment:

   ```bash
   cd web/frontend
   python3 -m http.server 8000
   # -> http://localhost:8000
   ```

4. Type or load an example program, tick `-O` / `-run` if you want
   optimization or execution, and press **Compile**. The right-hand
   panel shows tabs for each section the compiler printed
   (`Abstract Syntax Tree`, `Annotated Abstract Syntax Tree`,
   `Three Address Code`, `Program Output (-run)`, and any diagnostics).

## API summary

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/health` | GET | Whether the backend can find/build `minilangc` |
| `/api/examples` | GET | List of `.mc` files under `examples/` |
| `/api/examples/<name>` | GET | Source of one example file |
| `/api/compile` | POST | `{source, optimize, run}` → compiler stdout/stderr, split into sections |

## Design notes

- **No reimplementation risk:** the backend refuses to guess at
  results — if `minilangc` hasn't been built yet, `/api/compile`
  returns a clear `503` telling you to run `make` first (it also
  makes one best-effort attempt to build it automatically).
- **Isolation:** each request writes the submitted source to a fresh
  temporary `.mc` file and runs the compiler as a subprocess with a
  5-second timeout, so one bad input (e.g. an infinite loop reached
  via `-run`) can't hang the server.
- **Why Flask + vanilla JS, not a framework:** the compiler itself is
  the substantial part of this project; the web layer's job is only
  to expose it, so it's kept intentionally small and dependency-light
  (two Python packages, zero JS build tooling) rather than adding a
  React/Node stack for its own sake.
- **Not covered:** authentication, rate limiting, and production
  deployment (HTTPS, a WSGI server instead of Flask's dev server) —
  out of scope for a course-project playground, called out here
  explicitly rather than left implicit.
