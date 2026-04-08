# varjson5

jqライクなJSON5プロセッサ。`{{vars}}`形式のMustache風変数展開に対応しています。

## 特徴

- **JSON5対応** — コメント(`//`, `/* */`)、末尾カンマ、unquoted key、単引用符文字列、16進数リテラルなど
- **{{vars}}展開** — `"vars"`キーで定義した変数をドキュメント全体の`{{key}}`パターンに自動展開。vars内での変数間参照・連鎖展開にも対応
- **jqライクなフィルタ** — `.key`, `.[]`, パイプ `|`, `map()`, `select()` など
- **高速** — C++17実装、`-O3 -march=native` + LTO、依存ライブラリなし
- **FFI対応** — 共有ライブラリ(`libvarjson5.so`)としてPython・PHPなど他言語から利用可能

## インストール

### パッケージからインストール（推奨）

```bash
# Debian/Ubuntu
sudo dpkg -i varjson5_1.0.0_amd64.deb

# RHEL/Fedora/Rocky
sudo rpm -i varjson5-1.0.0.rpm
```

インストール先:

| ファイル | DEB (`/usr`) | RPM (`/usr/local`) |
|---------|-------------|-------------------|
| 実行ファイル | `/usr/bin/varjson5` | `/usr/local/bin/varjson5` |
| 共有ライブラリ | `/usr/lib/libvarjson5.so` | `/usr/local/lib/libvarjson5.so` |
| ヘッダ | `/usr/include/varjson5.h` | `/usr/local/include/varjson5.h` |
| Python バインディング | `/usr/lib/python3.x/site-packages/VarJson5.py` | `/usr/local/lib/python3.x/site-packages/VarJson5.py` |
| PHP バインディング | `/usr/share/php/VarJson5.php` | `/usr/local/share/php/VarJson5.php` |

### ソースからビルド

**必要なもの:**
- CMake 3.16以上
- C++17対応コンパイラ（GCC 8+ / Clang 7+）

```bash
git clone https://github.com/example/varjson5.git
cd varjson5

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
sudo cmake --install build   # デフォルトは /usr/local 以下
```

`--prefix` でインストール先を変更できます:

```bash
sudo cmake --install build --prefix /usr
```

インストール先（デフォルト `/usr/local`）:

| ファイル | パス |
|---------|------|
| 実行ファイル | `/usr/local/bin/varjson5` |
| 共有ライブラリ | `/usr/local/lib/libvarjson5.so` |
| ヘッダ | `/usr/local/include/varjson5.h` |
| Python バインディング | `/usr/local/lib/python3.x/site-packages/VarJson5.py` |
| PHP バインディング | `/usr/local/share/php/VarJson5.php` |

### パッケージ作成

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cd build

cpack           # DEB + RPM を両方生成
cpack -G DEB   # DEBのみ
cpack -G RPM   # RPMのみ
```

生成されるファイル（例: x86_64環境）:
```
build/varjson5_1.0.0_amd64.deb
build/varjson5-1.0.0-x86_64.rpm
```

## 使い方

```
varjson5 [オプション] [フィルタ] [ファイル...]
```

フィルタを省略した場合は `.`（全体出力）になります。
ファイルを省略した場合は標準入力から読み込みます。

### オプション

| オプション | 説明 |
|-----------|------|
| `-r`, `--raw-output` | 文字列をクォートなしで出力 |
| `-c`, `--compact-output` | 1行のコンパクト形式で出力 |
| `-n`, `--null-input` | ファイル/stdinを読まず `null` を入力とする |
| `-h`, `--help` | ヘルプを表示 |

## {{vars}} 変数展開

`"vars"`キーに変数を定義すると、フィルタ適用前に以下の2段階で展開されます。

1. **vars内の相互展開** — vars内の変数が他の変数を参照している場合、定義順序に関係なくすべて解決
2. **ドキュメント全体への展開** — 解決済みの変数をドキュメント全体の`{{key}}`パターンに適用

### 基本

**入力:**
```json5
{
  "vars": {
    "key01": "abcdefg"
  },
  "body": {
    "txt": "data is {{key01}}"
  }
}
```

**出力:**
```json
{
  "vars": {
    "key01": "abcdefg"
  },
  "body": {
    "txt": "data is abcdefg"
  }
}
```

### vars内での変数参照・連鎖展開

vars内の変数が別の変数を参照できます。定義の順序は問いません。

**入力:**
```json5
{
  "vars": {
    "full":     "{{greeting}}!!",        // greeting はこの後に定義
    "greeting": "{{base}} world",        // base はこの後に定義
    "base":     "hello",
    "url":      "https://{{env}}.example.com",
    "env":      "production",
  },
  "body": {
    "msg":      "{{full}}",
    "endpoint": "{{url}}",
  }
}
```

**出力:**
```json
{
  "vars": {
    "full":     "hello world!!",
    "greeting": "hello world",
    "base":     "hello",
    "url":      "https://production.example.com",
    "env":      "production"
  },
  "body": {
    "msg":      "hello world!!",
    "endpoint": "https://production.example.com"
  }
}
```

### 循環参照

vars同士が循環参照している場合、該当箇所は`{{key}}`のまま残ります（エラーにはなりません）。

```json5
{
  "vars": { "a": "{{b}}", "b": "{{a}}" },
  "body": { "msg": "{{a}}" }
}
// → body.msg は "{{a}}" のまま
```

## フィルタ

### 基本

| フィルタ | 説明 |
|---------|------|
| `.` | 入力をそのまま出力（identity） |
| `.key` | オブジェクトのキーにアクセス |
| `.key.sub` | ネストしたキーにアクセス |
| `.["key"]` | クォートキーでアクセス |
| `.[]` | 配列/オブジェクトの要素を展開（ストリーム出力） |
| `.[n]` | 配列のインデックスアクセス（負数で末尾から） |
| `.[2:5]` | スライス |
| `.a, .b` | カンマで複数フィルタを並べて複数出力 |
| `f1 \| f2` | パイプで結果を次のフィルタに渡す |

### 組み込み関数

| フィルタ | 説明 |
|---------|------|
| `keys` | オブジェクトのキー一覧（ソート済み） |
| `keys_unsorted` | オブジェクトのキー一覧（挿入順） |
| `values` | オブジェクトの値一覧 |
| `length` | 文字列/配列/オブジェクトの長さ |
| `type` | 型名を返す（`"null"` `"boolean"` `"number"` `"string"` `"array"` `"object"`） |
| `has("key")` | キーの存在確認 |
| `not` | 論理否定 |
| `empty` | 出力なし |
| `map(f)` | 配列の各要素にフィルタを適用 |
| `map_values(f)` | 配列/オブジェクトの各値にフィルタを適用 |
| `select(f)` | 条件が真の場合のみ通過 |
| `sort` | 配列をソート |
| `sort_by(f)` | フィルタ結果でソート |
| `group_by(f)` | フィルタ結果でグループ化 |
| `unique` | 重複を除去 |
| `unique_by(f)` | フィルタ結果で重複除去 |
| `reverse` | 配列/文字列を逆順にする |
| `flatten` | ネスト配列を平坦化 |
| `flatten(n)` | 深さ`n`まで平坦化 |
| `add` | 配列の要素を合計/結合 |
| `to_entries` | オブジェクトを`{key, value}`の配列に変換 |
| `from_entries` | `{key, value}`の配列をオブジェクトに変換 |
| `recurse` | 再帰的にすべての値を展開 |
| `any(f)` | いずれかの要素が条件を満たすか |
| `all(f)` | すべての要素が条件を満たすか |
| `tostring` | JSON文字列に変換 |
| `tonumber` | 数値に変換 |
| `tojson` | JSONエンコードされた文字列として出力 |
| `fromjson` | JSON文字列をパース |
| `ascii_downcase` | 小文字に変換 |
| `ascii_upcase` | 大文字に変換 |

## 使用例

```bash
# ファイル全体を出力
varjson5 '.' data.json5

# 特定キーを取得
varjson5 '.body.txt' data.json5

# 文字列をクォートなしで出力
varjson5 -r '.body.txt' data.json5

# コンパクト出力
varjson5 -c '.' data.json5

# 配列の要素をストリーム展開
varjson5 '.items[]' data.json5

# パイプで複数フィルタを組み合わせ
varjson5 '.users[] | .name' data.json5

# 条件でフィルタリング
varjson5 '.users[] | select(.age > 30)' data.json5

# 配列をマップ
varjson5 '[.users[] | .name]' data.json5

# キー一覧を取得
varjson5 'keys' data.json5

# stdinから読み込み
echo '{ name: "world" }' | varjson5 '.name'

# 複数ファイルを処理
varjson5 '.name' a.json5 b.json5
```

### JSON5形式の入力例

```json5
{
  // ラインコメント
  /* ブロックコメント */
  vars: {
    env: "production",   // unquoted key
    version: "1.2.3",
  },                     // 末尾カンマOK
  config: {
    mode: '{{env}}',     // 単引用符OK
    debug: false,
    timeout: 0xFF,       // 16進数リテラル
  },
}
```

## ライブラリとして使う（FFI）

`libvarjson5.so` を他言語からFFI経由で利用できます。

### C API

```c
#include <varjson5.h>

/* flags */
#define VARJSON5_RAW     1   /* 文字列をクォートなしで出力 */
#define VARJSON5_COMPACT 2   /* コンパクト出力 */

/* 1回処理 API — パース・展開・フィルタを一括実行。失敗時はNULL。
   戻り値は varjson5_free() で解放すること。スレッドセーフ。 */
char*       varjson5_process    (const char* input, const char* filter, int flags);
void        varjson5_free       (char* ptr);
const char* varjson5_last_error (void);   /* 直前のエラーメッセージ（スレッドローカル） */

/* Document API — 同一ドキュメントに複数クエリを実行する場合に効率的。
   varjson5_load() で一度だけパース・展開し、varjson5_query() を繰り返し呼び出す。 */
typedef struct varjson5_doc varjson5_doc;
varjson5_doc* varjson5_load     (const char* input);
char*         varjson5_query    (varjson5_doc* doc, const char* filter, int flags);
void          varjson5_free_doc (varjson5_doc* doc);
```

複数の結果はjqと同様に改行区切りで返されます。

### Python

Python 3.x + 標準ライブラリ `ctypes` のみで動作します。

```python
from VarJson5 import VarJson5

vj = VarJson5()                          # libvarjson5.so を自動検出
vj = VarJson5("/path/to/libvarjson5.so") # パスを直接指定

# 基本
result = vj.process('{"vars":{"k":"hi"},"body":{"t":"{{k}}"}}')

# フィルタ・オプション指定
result = vj.process(json5_str, filter=".body", raw=False, compact=False)

# 結果をPythonオブジェクトとして取得
obj = vj.process_to_dict(json5_str, ".body")

# 同一ドキュメントに複数クエリ（Document API）
with vj.load(json5_str) as doc:
    r1 = doc.query(".body")
    r2 = doc.query(".config", compact=True)
    obj = doc.query_to_dict(".body")
```

**`raw` / `compact` オプション:**

| オプション | 型 | デフォルト | 説明 |
|-----------|-----|-----------|------|
| オプション | 型 | `process()` デフォルト | `query()` デフォルト | 説明 |
|-----------|-----|----------------------|---------------------|------|
| `raw` | `bool` | `False` | `True` | `True` にすると、文字列値を JSON のダブルクォートで囲まずそのまま出力する。CLI の `-r` / `--raw-output` に相当 |
| `compact` | `bool` | `False` | `False` | `True` にすると、インデントや改行を省いた 1 行のコンパクト形式で出力する。CLI の `-c` / `--compact-output` に相当 |

```python
vj.process(src, ".body.name", raw=True)    # → hello  （クォートなし）
vj.process(src, ".body.name", raw=False)   # → "hello"（クォートあり、デフォルト）

# query() は raw=True がデフォルト
doc.query(".body.name")             # → hello  （クォートなし）
doc.query(".body.name", raw=False)  # → "hello"（クォートあり）

vj.process(src, ".body", compact=True)     # → {"name":"hello","age":30}
vj.process(src, ".body", compact=False)    # → 整形された複数行 JSON
```

**ライブラリ検索順序:**
1. スクリプト周辺 (`./`, `../build/`, `../`)
2. `LD_LIBRARY_PATH` 内の各パス
3. `ctypes.util.find_library("varjson5")` — `ldconfig` キャッシュ・システムパスを検索

ソース: [`python/VarJson5.py`](python/VarJson5.py)

### PHP

PHP 7.4以上 + `ext-ffi` が必要です。`php.ini` で以下を有効化してください:

```ini
extension=ffi
ffi.enable=true
```

```php
require_once 'VarJson5.php';

$vj = new VarJson5();                          // libvarjson5.so を自動検出
$vj = new VarJson5('/path/to/libvarjson5.so'); // パスを直接指定

// 基本
$result = $vj->process('{"vars":{"k":"hi"},"body":{"t":"{{k}}"}}');

// フィルタ・オプション指定
$result = $vj->process($json5, '.body', raw: false, compact: false);

// 結果をPHP配列として取得
$arr = $vj->processToArray($json5, '.body');

// 同一ドキュメントに複数クエリ（Document API）
$doc = $vj->load($json5);
$r1  = $doc->query('.body');
$r2  = $doc->query('.config', compact: true);
$arr = $doc->queryToArray('.body');
$doc->free();
```

**`raw` / `compact` オプション:**

| オプション | 型 | デフォルト | 説明 |
|-----------|-----|-----------|------|
| オプション | 型 | `process()` デフォルト | `query()` デフォルト | 説明 |
|-----------|-----|----------------------|---------------------|------|
| `raw` | `bool` | `false` | `true` | `true` にすると、文字列値を JSON のダブルクォートで囲まずそのまま出力する。CLI の `-r` / `--raw-output` に相当 |
| `compact` | `bool` | `false` | `false` | `true` にすると、インデントや改行を省いた 1 行のコンパクト形式で出力する。CLI の `-c` / `--compact-output` に相当 |

```php
$vj->process($src, '.body.name', raw: true);    // → hello  （クォートなし）
$vj->process($src, '.body.name', raw: false);   // → "hello"（クォートあり、デフォルト）

// query() は raw: true がデフォルト
$doc->query('.body.name');              // → hello  （クォートなし）
$doc->query('.body.name', raw: false); // → "hello"（クォートあり）

$vj->process($src, '.body', compact: true);     // → {"name":"hello","age":30}
$vj->process($src, '.body', compact: false);    // → 整形された複数行 JSON
```

**ライブラリ検索順序:**
1. `LD_LIBRARY_PATH` 内の各パス
2. スクリプト周辺 (`./`, `../build/`, `../`)
3. `/usr/local/lib/libvarjson5.so` — `cmake --install` デフォルト
4. `/usr/lib/libvarjson5.so` — CPack パッケージインストール
5. `/usr/lib/x86_64-linux-gnu/libvarjson5.so`, `/usr/lib/aarch64-linux-gnu/libvarjson5.so` — Debian/Ubuntu マルチアーチ

ソース: [`php/VarJson5.php`](php/VarJson5.php)

## ライセンス

MIT License — 詳細は [LICENSE](LICENSE) を参照してください。
