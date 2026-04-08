"""
VarJson5 Python binding via ctypes.

Usage:
    from VarJson5 import VarJson5

    vj = VarJson5()                          # libvarjson5.so を自動検索
    vj = VarJson5("/path/to/libvarjson5.so") # パス明示

    result = vj.process('{"vars":{"k":"hi"},"body":{"t":"{{k}}"}}')
    result = vj.process(json5_str, filter=".body", raw=False, compact=False)
    obj    = vj.process_to_dict(json5_str, ".body")

    # 一度パースして何度もクエリする場合
    with vj.load(json5_str) as doc:
        r1 = doc.query(".body")
        r2 = doc.query(".config", compact=True)
"""

import ctypes
import ctypes.util
import json
import os
from pathlib import Path


# ---------------------------------------------------------------------------
# Flags (varjson5.h のフラグと対応)
# ---------------------------------------------------------------------------
_RAW     = 1
_COMPACT = 2


# ---------------------------------------------------------------------------
# 内部: ライブラリの検索と読み込み
# ---------------------------------------------------------------------------
def _find_lib() -> str:
    """libvarjson5.so のパスを返す。見つからない場合は FileNotFoundError。"""
    here = Path(__file__).resolve().parent

    candidates = [
        here / "libvarjson5.so",
        here.parent / "build" / "libvarjson5.so",
        here.parent / "libvarjson5.so",
    ]

    # LD_LIBRARY_PATH も探索
    for p in os.environ.get("LD_LIBRARY_PATH", "").split(":"):
        if p:
            candidates.append(Path(p) / "libvarjson5.so")

    for c in candidates:
        if c.exists():
            return str(c)

    # システムパス (ldconfig 等)
    found = ctypes.util.find_library("varjson5")
    if found:
        return found

    raise FileNotFoundError(
        "libvarjson5.so が見つかりません。"
        " cmake + make でビルドするか LD_LIBRARY_PATH を設定してください。"
    )


def _load_lib(path: str | None = None) -> ctypes.CDLL:
    lib = ctypes.CDLL(path or _find_lib())

    lib.varjson5_process.restype  = ctypes.c_void_p
    lib.varjson5_process.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]

    lib.varjson5_free.restype  = None
    lib.varjson5_free.argtypes = [ctypes.c_void_p]

    lib.varjson5_last_error.restype  = ctypes.c_char_p
    lib.varjson5_last_error.argtypes = []

    lib.varjson5_load.restype  = ctypes.c_void_p
    lib.varjson5_load.argtypes = [ctypes.c_char_p]

    lib.varjson5_query.restype  = ctypes.c_void_p
    lib.varjson5_query.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]

    lib.varjson5_free_doc.restype  = None
    lib.varjson5_free_doc.argtypes = [ctypes.c_void_p]

    return lib


# ---------------------------------------------------------------------------
# 例外クラス
# ---------------------------------------------------------------------------
class VarJson5Error(Exception):
    """VarJson5 の処理中に発生したエラー。"""


# ---------------------------------------------------------------------------
# VarJson5Doc — VarJson5.load() が返すドキュメントハンドル
# ---------------------------------------------------------------------------
class VarJson5Doc:
    """
    一度パースされた JSON5 ドキュメントのハンドル。
    VarJson5.load() によって生成され、コンテキストマネージャとして使用可能。
    """

    def __init__(self, lib: ctypes.CDLL, ptr: int) -> None:
        self._lib = lib
        self._ptr = ptr

    def query(
        self,
        filter: str = ".",
        *,
        raw: bool = True,
        compact: bool = False,
    ) -> str:
        """
        パース済みドキュメントに jq スタイルのフィルタを適用する。

        Parameters
        ----------
        filter  : jq スタイルのフィルタ式 (デフォルト: ".")
        raw     : True の場合、文字列を JSON エンコードせずに出力
        compact : True の場合、1 行のコンパクト形式で出力

        Returns
        -------
        フィルタ結果の文字列 (複数結果は改行区切り)
        """
        if self._ptr is None:
            raise VarJson5Error("ドキュメントはすでに解放されています")
        flags = (_RAW if raw else 0) | (_COMPACT if compact else 0)
        ptr = self._lib.varjson5_query(self._ptr, filter.encode(), flags)
        if ptr is None:
            msg = self._lib.varjson5_last_error()
            raise VarJson5Error(msg.decode() if msg else "unknown error")
        text = ctypes.cast(ptr, ctypes.c_char_p).value.decode()
        self._lib.varjson5_free(ptr)
        return text.rstrip("\n")

    def query_to_dict(self, filter: str = ".") -> object:
        """クエリ結果の最初の行を json.loads() でデコードして返す。"""
        text = self.query(filter, compact=True)
        first = text.splitlines()[0] if text else "null"
        return json.loads(first)

    def close(self) -> None:
        """ドキュメントハンドルを解放する。冪等。"""
        if self._ptr is not None:
            self._lib.varjson5_free_doc(self._ptr)
            self._ptr = None

    def __enter__(self) -> "VarJson5Doc":
        return self

    def __exit__(self, *_) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()


# ---------------------------------------------------------------------------
# VarJson5 — メインクラス
# ---------------------------------------------------------------------------
class VarJson5:
    """
    libvarjson5 の Python ラッパークラス。

    JSON5 のパース・{{vars}} 変数展開・jq スタイルフィルタを提供する。

    Examples
    --------
    >>> vj = VarJson5()
    >>> vj.process('{"vars":{"k":"hello"},"msg":"{{k}} world"}', ".msg", raw=True)
    'hello world'
    """

    def __init__(self, lib_path: str | None = None) -> None:
        """
        Parameters
        ----------
        lib_path : libvarjson5.so の絶対パス。省略時は自動検索。
        """
        self._lib = _load_lib(lib_path)

    def process(
        self,
        input: str,
        filter: str = ".",
        *,
        raw: bool = False,
        compact: bool = False,
    ) -> str:
        """
        JSON5 文字列をパースし、{{vars}} を展開してフィルタを適用する。

        Parameters
        ----------
        input   : 処理する JSON5 文字列
        filter  : jq スタイルのフィルタ式 (デフォルト: ".")
        raw     : True の場合、文字列を JSON エンコードせずに出力
        compact : True の場合、1 行のコンパクト形式で出力

        Returns
        -------
        処理結果の文字列 (複数結果は改行区切り)

        Raises
        ------
        VarJson5Error : パースエラーまたはフィルタエラー時
        """
        flags = (_RAW if raw else 0) | (_COMPACT if compact else 0)
        ptr = self._lib.varjson5_process(input.encode(), filter.encode(), flags)
        if ptr is None:
            msg = self._lib.varjson5_last_error()
            raise VarJson5Error(msg.decode() if msg else "unknown error")
        text = ctypes.cast(ptr, ctypes.c_char_p).value.decode()
        self._lib.varjson5_free(ptr)
        return text.rstrip("\n")

    def process_to_dict(self, input: str, filter: str = ".") -> object:
        """
        process() の結果を json.loads() でデコードして Python オブジェクトとして返す。

        Parameters
        ----------
        input  : 処理する JSON5 文字列
        filter : jq スタイルのフィルタ式 (デフォルト: ".")

        Returns
        -------
        デコードされた Python オブジェクト (dict / list / str / int など)
        """
        text = self.process(input, filter, compact=True)
        first = text.splitlines()[0] if text else "null"
        return json.loads(first)

    def load(self, input: str) -> VarJson5Doc:
        """
        JSON5 文字列を一度だけパースし、{{vars}} を展開した VarJson5Doc を返す。
        同じドキュメントに対して複数のクエリを実行する場合に効率的。

        Parameters
        ----------
        input : 処理する JSON5 文字列

        Returns
        -------
        VarJson5Doc : コンテキストマネージャとして使用可能なドキュメントハンドル

        Raises
        ------
        VarJson5Error : パースエラー時
        """
        ptr = self._lib.varjson5_load(input.encode())
        if ptr is None:
            msg = self._lib.varjson5_last_error()
            raise VarJson5Error(msg.decode() if msg else "unknown error")
        return VarJson5Doc(self._lib, ptr)


# ---------------------------------------------------------------------------
# デモ (python python/VarJson5.py)
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    vj = VarJson5()

    # --- 1. {{vars}} 変数展開 ---
    src1 = """{
  "vars": { "env": "production", "ver": "1.2.3" },
  "app": {
    "name": "myapp-{{env}}",
    "version": "{{ver}}",
    "tag": "{{env}}-{{ver}}"
  }
}"""
    print("=== 1. vars 展開 ===")
    print(vj.process(src1))

    # --- 2. jq スタイルフィルタ ---
    print("\n=== 2. フィルタ .app.tag ===")
    print(vj.process(src1, ".app.tag", raw=True))

    # --- 3. コンパクト出力 ---
    print("\n=== 3. コンパクト出力 ===")
    print(vj.process(src1, ".app", compact=True))

    # --- 4. JSON5 機能 (コメント、末尾カンマ、引用符なしキー) ---
    src2 = """{
  // JSON5 コメント
  vars: { host: "localhost", port: "5432" },
  dsn: "postgres://{{host}}:{{port}}/db",  // 末尾カンマ OK
}"""
    print("\n=== 4. JSON5 機能 ===")
    print(vj.process(src2, ".dsn", raw=True))

    # --- 5. 配列 + map フィルタ ---
    src3 = """{
  "vars": { "prefix": "item" },
  "list": ["{{prefix}}_a", "{{prefix}}_b", "{{prefix}}_c"]
}"""
    print("\n=== 5. 配列 .list[] ===")
    print(vj.process(src3, ".list[]", raw=True))

    # --- 6. process_to_dict ---
    print("\n=== 6. process_to_dict ===")
    obj = vj.process_to_dict('{"vars":{"n":42},"val":"{{n}}"}', ".val")
    print(f"type={type(obj).__name__}  value={obj!r}")

    # --- 7. エラーハンドリング ---
    print("\n=== 7. エラーハンドリング ===")
    try:
        vj.process("{ invalid json !!!")
    except VarJson5Error as e:
        print(f"VarJson5Error をキャッチ: {e}")

    # --- 8. load() + query(): 一度パースして複数クエリ ---
    print("\n=== 8. load/query (一度だけパース) ===")
    with vj.load(src1) as doc:
        print(doc.query(".app.name", raw=True))
        print(doc.query(".app.tag",  raw=True))
        print(doc.query(".app",      compact=True))
