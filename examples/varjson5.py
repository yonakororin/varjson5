"""
varjson5 Python binding via ctypes.

Usage:
    from examples.varjson5 import Varjson5
    vj = Varjson5()                          # auto-locate libvarjson5.so
    vj = Varjson5("/path/to/libvarjson5.so") # explicit path

    result = vj.process('{"vars":{"k":"hi"},"body":{"t":"{{k}}"}}')
    result = vj.process(json5_str, filter=".body", raw=False, compact=False)
"""

import ctypes
import ctypes.util
import json
import os
import sys
from pathlib import Path


# ---------------------------------------------------------------------------
# Flags (mirror varjson5.h)
# ---------------------------------------------------------------------------
VARJSON5_RAW     = 1
VARJSON5_COMPACT = 2


# ---------------------------------------------------------------------------
# Low-level loader
# ---------------------------------------------------------------------------
def _find_lib() -> str:
    """Return path to libvarjson5.so, searching common locations."""
    # 1. Same directory as this script (handy during development)
    here = Path(__file__).resolve().parent
    for candidate in [
        here / "libvarjson5.so",
        here.parent / "build" / "libvarjson5.so",
        here.parent / "libvarjson5.so",
    ]:
        if candidate.exists():
            return str(candidate)

    # 2. System search (LD_LIBRARY_PATH, /usr/lib, ...)
    found = ctypes.util.find_library("varjson5")
    if found:
        return found

    raise FileNotFoundError(
        "libvarjson5.so not found. "
        "Build the project first (cmake + make) or set LD_LIBRARY_PATH."
    )


def _load(path: str | None = None) -> ctypes.CDLL:
    lib = ctypes.CDLL(path or _find_lib())

    # char* varjson5_process(const char* input, const char* filter, int flags)
    # Use c_void_p to preserve the raw pointer for varjson5_free().
    # (c_char_p would copy the bytes and lose the original address.)
    lib.varjson5_process.restype  = ctypes.c_void_p
    lib.varjson5_process.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]

    # void varjson5_free(char* ptr)
    lib.varjson5_free.restype  = None
    lib.varjson5_free.argtypes = [ctypes.c_void_p]

    # const char* varjson5_last_error(void)
    lib.varjson5_last_error.restype  = ctypes.c_char_p
    lib.varjson5_last_error.argtypes = []

    # varjson5_doc* varjson5_load(const char* input)
    lib.varjson5_load.restype  = ctypes.c_void_p
    lib.varjson5_load.argtypes = [ctypes.c_char_p]

    # char* varjson5_query(varjson5_doc* doc, const char* filter, int flags)
    lib.varjson5_query.restype  = ctypes.c_void_p
    lib.varjson5_query.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]

    # void varjson5_free_doc(varjson5_doc* doc)
    lib.varjson5_free_doc.restype  = None
    lib.varjson5_free_doc.argtypes = [ctypes.c_void_p]

    return lib


# ---------------------------------------------------------------------------
# High-level wrapper
# ---------------------------------------------------------------------------
class Varjson5Error(Exception):
    pass


class Varjson5:
    """High-level wrapper around libvarjson5."""

    def __init__(self, lib_path: str | None = None) -> None:
        self._lib = _load(lib_path)

    # ------------------------------------------------------------------
    def process(
        self,
        input: str,
        filter: str = ".",
        *,
        raw: bool = False,
        compact: bool = False,
    ) -> str:
        """
        Parse *input* as JSON5, apply {{vars}} substitution, then run *filter*.

        Parameters
        ----------
        input   : JSON5 string to process
        filter  : jq-style filter expression (default: ".")
        raw     : if True, strings are output without JSON encoding
        compact : if True, output is single-line (no indentation)

        Returns
        -------
        Processed output as a string (multiple results separated by newlines).

        Raises
        ------
        Varjson5Error on parse or filter errors.
        """
        flags = 0
        if raw:
            flags |= VARJSON5_RAW
        if compact:
            flags |= VARJSON5_COMPACT

        ptr = self._lib.varjson5_process(
            input.encode(),
            filter.encode(),
            flags,
        )
        if ptr is None:
            msg = self._lib.varjson5_last_error()
            raise Varjson5Error(msg.decode() if msg else "unknown error")

        text = ctypes.cast(ptr, ctypes.c_char_p).value.decode()
        self._lib.varjson5_free(ptr)
        return text.rstrip("\n")

    def process_to_dict(self, input: str, filter: str = ".") -> object:
        """
        Convenience method: process *input* and parse the first result as Python
        object via json.loads().  Useful when a single JSON value is expected.
        """
        text = self.process(input, filter, compact=True)
        first_line = text.splitlines()[0] if text else "null"
        return json.loads(first_line)

    def load(self, input: str) -> "Varjson5Doc":
        """
        Parse *input* as JSON5 and apply {{vars}} substitution once.
        Returns a :class:`Varjson5Doc` that can be queried multiple times
        without re-parsing.

        Use as a context manager to ensure the document is freed::

            with vj.load(json5_str) as doc:
                r1 = doc.query(".body")
                r2 = doc.query(".config", compact=True)
        """
        ptr = self._lib.varjson5_load(input.encode())
        if ptr is None:
            msg = self._lib.varjson5_last_error()
            raise Varjson5Error(msg.decode() if msg else "unknown error")
        return Varjson5Doc(self._lib, ptr)


# ---------------------------------------------------------------------------
# Varjson5Doc — handle returned by Varjson5.load()
# ---------------------------------------------------------------------------
class Varjson5Doc:
    """
    An opaque handle to a parsed and variable-substituted JSON5 document.
    Created by :meth:`Varjson5.load`; supports context-manager protocol.
    """

    def __init__(self, lib: ctypes.CDLL, ptr: int) -> None:
        self._lib = lib
        self._ptr = ptr  # c_void_p (raw integer address)

    def query(
        self,
        filter: str = ".",
        *,
        raw: bool = False,
        compact: bool = False,
    ) -> str:
        """
        Apply *filter* to the loaded document without re-parsing.

        Parameters
        ----------
        filter  : jq-style filter expression (default: ".")
        raw     : if True, strings are output without JSON encoding
        compact : if True, output is single-line (no indentation)

        Returns
        -------
        Processed output as a string (multiple results separated by newlines).
        """
        if self._ptr is None:
            raise Varjson5Error("document has already been freed")
        flags = 0
        if raw:
            flags |= VARJSON5_RAW
        if compact:
            flags |= VARJSON5_COMPACT
        ptr = self._lib.varjson5_query(self._ptr, filter.encode(), flags)
        if ptr is None:
            msg = self._lib.varjson5_last_error()
            raise Varjson5Error(msg.decode() if msg else "unknown error")
        text = ctypes.cast(ptr, ctypes.c_char_p).value.decode()
        self._lib.varjson5_free(ptr)
        return text.rstrip("\n")

    def query_to_dict(self, filter: str = ".") -> object:
        """Convenience: query and parse the first result via json.loads()."""
        text = self.query(filter, compact=True)
        first_line = text.splitlines()[0] if text else "null"
        return json.loads(first_line)

    def close(self) -> None:
        """Release the document handle. Idempotent."""
        if self._ptr is not None:
            self._lib.varjson5_free_doc(self._ptr)
            self._ptr = None

    def __enter__(self) -> "Varjson5Doc":
        return self

    def __exit__(self, *_) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()


# ---------------------------------------------------------------------------
# Demo (python examples/varjson5.py)
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    vj = Varjson5()

    # --- 1. Basic {{vars}} substitution ---
    src1 = """{
  "vars": { "env": "production", "ver": "1.2.3" },
  "app": {
    "name": "myapp-{{env}}",
    "version": "{{ver}}",
    "tag": "{{env}}-{{ver}}"
  }
}"""
    print("=== 1. vars substitution ===")
    print(vj.process(src1))

    # --- 2. jq-style filter ---
    print("\n=== 2. filter .app.tag ===")
    print(vj.process(src1, ".app.tag", raw=True))

    # --- 3. Compact output ---
    print("\n=== 3. compact output ===")
    print(vj.process(src1, ".app", compact=True))

    # --- 4. JSON5 features (comments, trailing comma, unquoted keys) ---
    src2 = """{
  // JSON5 comment
  vars: { host: "localhost", port: "5432" },
  dsn: "postgres://{{host}}:{{port}}/db",  // trailing comma OK
}"""
    print("\n=== 4. JSON5 features ===")
    print(vj.process(src2, ".dsn", raw=True))

    # --- 5. Array + map filter ---
    src3 = """{
  "vars": { "prefix": "item" },
  "list": ["{{prefix}}_a", "{{prefix}}_b", "{{prefix}}_c"]
}"""
    print("\n=== 5. array map ===")
    print(vj.process(src3, ".list[]", raw=True))

    # --- 6. process_to_dict ---
    print("\n=== 6. process_to_dict ===")
    obj = vj.process_to_dict('{"vars":{"n":42},"val":"{{n}}"}', ".val")
    print(f"type={type(obj).__name__}  value={obj!r}")

    # --- 7. Error handling ---
    print("\n=== 7. error handling ===")
    try:
        vj.process("{ invalid json !!!")
    except Varjson5Error as e:
        print(f"Caught Varjson5Error: {e}")

    # --- 8. load() + query(): parse once, query many times ---
    print("\n=== 8. load/query (parse once) ===")
    with vj.load(src1) as doc:
        print(doc.query(".app.name", raw=True))
        print(doc.query(".app.tag",  raw=True))
        print(doc.query(".app",      compact=True))
