<?php
/**
 * VarJson5 PHP binding via FFI (PHP 7.4+).
 *
 * 動作要件:
 *   - PHP 7.4 以上 + ext-ffi 有効 (php.ini: extension=ffi)
 *   - php.ini: ffi.enable = true  (または "preload")
 *   - libvarjson5.so がビルド済みでアクセス可能であること
 *
 * 使い方:
 *   $vj = new VarJson5();                          // libvarjson5.so を自動検索
 *   $vj = new VarJson5('/path/to/libvarjson5.so'); // パス明示
 *
 *   $result = $vj->process('{"vars":{"k":"hi"},"body":{"t":"{{k}}"}}');
 *   $result = $vj->process($json5, '.body', raw: false, compact: false);
 *   $arr    = $vj->processToArray($json5, '.body');
 *
 *   // 一度パースして複数クエリする場合
 *   $doc = $vj->load($json5);
 *   echo $doc->query('.body');
 *   echo $doc->query('.config', compact: true);
 *   $doc->free();
 */

class VarJson5Exception extends RuntimeException {}

// ---------------------------------------------------------------------------
// VarJson5Doc — VarJson5::load() が返すドキュメントハンドル
// ---------------------------------------------------------------------------

/**
 * パース済み JSON5 ドキュメントのハンドル。
 * VarJson5::load() によって生成される。再パース不要で複数回クエリ可能。
 */
class VarJson5Doc
{
    private \FFI  $ffi;
    private mixed $ptr;   // varjson5_doc* への FFI ポインタ

    /** @internal VarJson5::load() からのみ呼び出す */
    public function __construct(\FFI $ffi, mixed $ptr)
    {
        $this->ffi = $ffi;
        $this->ptr = $ptr;
    }

    /**
     * パース済みドキュメントに jq スタイルフィルタを適用する。
     *
     * @param  string $filter  jq スタイルのフィルタ式 (デフォルト ".")
     * @param  bool   $raw     文字列を JSON エンコードせずに出力
     * @param  bool   $compact 1 行のコンパクト形式で出力
     * @return string          処理結果 (複数結果は "\n" 区切り)
     * @throws VarJson5Exception フィルタエラー時
     */
    public function query(
        string $filter = '.',
        bool $raw = false,
        bool $compact = false
    ): string {
        if ($this->ptr === null) {
            throw new VarJson5Exception('ドキュメントはすでに解放されています');
        }
        $flags = ($raw ? VarJson5::RAW : 0) | ($compact ? VarJson5::COMPACT : 0);
        $ptr = $this->ffi->varjson5_query($this->ptr, $filter, $flags);
        if ($ptr === null) {
            $err = $this->ffi->varjson5_last_error();
            throw new VarJson5Exception(\FFI::string($err));
        }
        $result = \FFI::string($ptr);
        $this->ffi->varjson5_free($ptr);
        return rtrim($result, "\n");
    }

    /**
     * クエリ結果の最初の行を json_decode() でデコードして返す。
     *
     * @return mixed デコードされた PHP 値
     * @throws VarJson5Exception フィルタエラー時
     * @throws \JsonException    JSON デコードエラー時
     */
    public function queryToArray(string $filter = '.'): mixed
    {
        $text = $this->query($filter, compact: true);
        $firstLine = explode("\n", $text)[0];
        return json_decode($firstLine, associative: true, flags: \JSON_THROW_ON_ERROR);
    }

    /** ドキュメントハンドルを解放する。冪等。 */
    public function free(): void
    {
        if ($this->ptr !== null) {
            $this->ffi->varjson5_free_doc($this->ptr);
            $this->ptr = null;
        }
    }

    public function __destruct()
    {
        $this->free();
    }
}

// ---------------------------------------------------------------------------
// VarJson5 — メインクラス
// ---------------------------------------------------------------------------

/**
 * libvarjson5 の PHP ラッパークラス。
 *
 * JSON5 のパース・{{vars}} 変数展開・jq スタイルフィルタを提供する。
 */
class VarJson5
{
    /** 文字列を JSON エンコードせずに出力するフラグ */
    public const RAW     = 1;
    /** 1 行のコンパクト形式で出力するフラグ */
    public const COMPACT = 2;

    private \FFI $ffi;

    /**
     * @param string|null $libPath libvarjson5.so の絶対パス。省略時は自動検索。
     * @throws \RuntimeException FFI 拡張未ロード時、またはライブラリが見つからない場合
     */
    public function __construct(?string $libPath = null)
    {
        if (!extension_loaded('ffi')) {
            throw new \RuntimeException(
                'PHP FFI 拡張がロードされていません。php.ini で extension=ffi を有効にしてください。'
            );
        }

        $lib = $libPath ?? $this->findLib();

        $this->ffi = \FFI::cdef(
            '
            char*       varjson5_process    (const char* input, const char* filter, int flags);
            void        varjson5_free       (char* ptr);
            const char* varjson5_last_error (void);

            typedef struct varjson5_doc varjson5_doc;
            varjson5_doc* varjson5_load     (const char* input);
            char*         varjson5_query    (varjson5_doc* doc, const char* filter, int flags);
            void          varjson5_free_doc (varjson5_doc* doc);
            ',
            $lib
        );
    }

    // ------------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------------

    /**
     * JSON5 文字列をパースし、{{vars}} を展開してフィルタを適用する。
     *
     * @param  string $input   処理する JSON5 文字列
     * @param  string $filter  jq スタイルのフィルタ式 (デフォルト ".")
     * @param  bool   $raw     文字列を JSON エンコードせずに出力
     * @param  bool   $compact 1 行のコンパクト形式で出力
     * @return string          処理結果 (複数結果は "\n" 区切り)
     * @throws VarJson5Exception パースエラーまたはフィルタエラー時
     */
    public function process(
        string $input,
        string $filter = '.',
        bool $raw = false,
        bool $compact = false
    ): string {
        $flags = ($raw ? self::RAW : 0) | ($compact ? self::COMPACT : 0);

        $ptr = $this->ffi->varjson5_process($input, $filter, $flags);
        if ($ptr === null) {
            $err = $this->ffi->varjson5_last_error();
            throw new VarJson5Exception(\FFI::string($err));
        }

        $result = \FFI::string($ptr);
        $this->ffi->varjson5_free($ptr);
        return rtrim($result, "\n");
    }

    /**
     * process() の結果を json_decode() でデコードして PHP 値として返す。
     *
     * @param  string $input   処理する JSON5 文字列
     * @param  string $filter  jq スタイルのフィルタ式 (デフォルト ".")
     * @return mixed           デコードされた PHP 値 (配列/文字列/数値など)
     * @throws VarJson5Exception 処理エラー時
     * @throws \JsonException    JSON デコードエラー時
     */
    public function processToArray(string $input, string $filter = '.'): mixed
    {
        $text = $this->process($input, $filter, compact: true);
        $firstLine = explode("\n", $text)[0];
        return json_decode($firstLine, associative: true, flags: \JSON_THROW_ON_ERROR);
    }

    /**
     * JSON5 文字列を一度だけパースし、{{vars}} を展開した VarJson5Doc を返す。
     * 同じドキュメントに複数クエリを実行する場合に効率的。
     *
     * @param  string $input  処理する JSON5 文字列
     * @return VarJson5Doc    クエリ可能なドキュメントハンドル
     * @throws VarJson5Exception パースエラー時
     */
    public function load(string $input): VarJson5Doc
    {
        $ptr = $this->ffi->varjson5_load($input);
        if ($ptr === null) {
            $err = $this->ffi->varjson5_last_error();
            throw new VarJson5Exception(\FFI::string($err));
        }
        return new VarJson5Doc($this->ffi, $ptr);
    }

    // ------------------------------------------------------------------
    // 内部: ライブラリ検索
    // ------------------------------------------------------------------

    private function findLib(): string
    {
        $here = dirname(__FILE__);

        // LD_LIBRARY_PATH のエントリを候補に追加
        $ldPaths = [];
        foreach (explode(':', getenv('LD_LIBRARY_PATH') ?: '') as $p) {
            if ($p !== '') {
                $ldPaths[] = rtrim($p, '/') . '/libvarjson5.so';
            }
        }

        $candidates = [
            ...$ldPaths,
            $here . '/libvarjson5.so',
            $here . '/../build/libvarjson5.so',
            $here . '/../libvarjson5.so',
            '/usr/local/lib/libvarjson5.so',
            '/usr/lib/libvarjson5.so',
            '/usr/lib/x86_64-linux-gnu/libvarjson5.so',
            '/usr/lib/aarch64-linux-gnu/libvarjson5.so',
        ];

        foreach ($candidates as $path) {
            if (file_exists($path)) {
                return realpath($path);
            }
        }

        throw new \RuntimeException(
            'libvarjson5.so が見つかりません。' .
            ' cmake + make でビルドするか LD_LIBRARY_PATH を設定してください。'
        );
    }
}


// ---------------------------------------------------------------------------
// デモ  (php php/VarJson5.php)
// ---------------------------------------------------------------------------
if (basename(__FILE__) === basename($_SERVER['SCRIPT_FILENAME'] ?? '')) {

    $vj = new VarJson5();

    // --- 1. {{vars}} 変数展開 ---
    $src1 = <<<'JSON5'
{
  "vars": { "env": "production", "ver": "1.2.3" },
  "app": {
    "name":    "myapp-{{env}}",
    "version": "{{ver}}",
    "tag":     "{{env}}-{{ver}}"
  }
}
JSON5;

    echo "=== 1. vars 展開 ===\n";
    echo $vj->process($src1) . "\n";

    // --- 2. jq スタイルフィルタ ---
    echo "\n=== 2. フィルタ .app.tag ===\n";
    echo $vj->process($src1, '.app.tag', raw: true) . "\n";

    // --- 3. コンパクト出力 ---
    echo "\n=== 3. コンパクト出力 ===\n";
    echo $vj->process($src1, '.app', compact: true) . "\n";

    // --- 4. JSON5 機能 (コメント、末尾カンマ、引用符なしキー) ---
    $src2 = <<<'JSON5'
{
  // JSON5 コメント
  vars: { host: "localhost", port: "5432" },
  dsn: "postgres://{{host}}:{{port}}/db",
}
JSON5;

    echo "\n=== 4. JSON5 機能 ===\n";
    echo $vj->process($src2, '.dsn', raw: true) . "\n";

    // --- 5. 配列イテレーション ---
    $src3 = <<<'JSON5'
{
  "vars": { "prefix": "item" },
  "list": ["{{prefix}}_a", "{{prefix}}_b", "{{prefix}}_c"]
}
JSON5;

    echo "\n=== 5. 配列 .list[] ===\n";
    echo $vj->process($src3, '.list[]', raw: true) . "\n";

    // --- 6. processToArray ---
    echo "\n=== 6. processToArray ===\n";
    $arr = $vj->processToArray('{"vars":{"n":42},"data":{"x":"{{n}}","y":2}}', '.data');
    var_dump($arr);

    // --- 7. エラーハンドリング ---
    echo "\n=== 7. エラーハンドリング ===\n";
    try {
        $vj->process('{ invalid json !!!');
    } catch (VarJson5Exception $e) {
        echo 'VarJson5Exception をキャッチ: ' . $e->getMessage() . "\n";
    }

    // --- 8. load() + query(): 一度パースして複数クエリ ---
    echo "\n=== 8. load/query (一度だけパース) ===\n";
    $doc = $vj->load($src1);
    echo $doc->query('.app.name', raw: true) . "\n";
    echo $doc->query('.app.tag',  raw: true) . "\n";
    echo $doc->query('.app',      compact: true) . "\n";
    $doc->free();
}
