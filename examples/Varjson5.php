<?php
/**
 * varjson5 PHP binding via FFI (PHP 7.4+).
 *
 * Requirements:
 *   - PHP 7.4+ with ext-ffi enabled (extension=ffi in php.ini)
 *   - ffi.enable = true  (or "preload") in php.ini
 *   - libvarjson5.so built and accessible
 *
 * Usage:
 *   $vj = new Varjson5();                          // auto-locate libvarjson5.so
 *   $vj = new Varjson5('/path/to/libvarjson5.so'); // explicit path
 *
 *   $result = $vj->process('{"vars":{"k":"hi"},"body":{"t":"{{k}}"}}');
 *   $result = $vj->process($json5, '.body', raw: false, compact: false);
 *   $obj    = $vj->processToArray($json5, '.body');
 */

class Varjson5Exception extends RuntimeException {}

class Varjson5
{
    // Flags (mirror varjson5.h)
    public const RAW     = 1;
    public const COMPACT = 2;

    private \FFI $ffi;

    public function __construct(?string $libPath = null)
    {
        if (!extension_loaded('ffi')) {
            throw new \RuntimeException('PHP FFI extension is not loaded. Enable extension=ffi in php.ini.');
        }

        $lib = $libPath ?? $this->findLib();

        $this->ffi = \FFI::cdef(
            '
            char*       varjson5_process    (const char* input, const char* filter, int flags);
            void        varjson5_free       (char* ptr);
            const char* varjson5_last_error (void);
            ',
            $lib
        );
    }

    // ------------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------------

    /**
     * Process JSON5 input with {{vars}} substitution and an optional filter.
     *
     * @param  string $input   JSON5 string
     * @param  string $filter  jq-style filter (default ".")
     * @param  bool   $raw     Output strings without JSON encoding
     * @param  bool   $compact Single-line output (no indentation)
     * @return string          Processed output (multiple results separated by "\n")
     * @throws Varjson5Exception on parse or filter errors
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
            throw new Varjson5Exception(\FFI::string($err));
        }

        // Copy C string into PHP string before freeing
        $result = \FFI::string($ptr);
        $this->ffi->varjson5_free($ptr);

        return rtrim($result, "\n");
    }

    /**
     * Process and decode the first result as a PHP value (via json_decode).
     *
     * @param  string $input   JSON5 string
     * @param  string $filter  jq-style filter (default ".")
     * @return mixed           Decoded PHP value
     * @throws Varjson5Exception on processing errors
     * @throws \JsonException   on JSON decode errors
     */
    public function processToArray(string $input, string $filter = '.'): mixed
    {
        $text = $this->process($input, $filter, compact: true);
        $firstLine = explode("\n", $text)[0];
        return json_decode($firstLine, associative: true, flags: \JSON_THROW_ON_ERROR);
    }

    // ------------------------------------------------------------------
    // Internal
    // ------------------------------------------------------------------

    private function findLib(): string
    {
        $here = dirname(__FILE__);

        // Build LD_LIBRARY_PATH entries into candidate list
        $ldPaths = [];
        foreach (explode(':', getenv('LD_LIBRARY_PATH') ?: '') as $p) {
            if ($p !== '') $ldPaths[] = rtrim($p, '/') . '/libvarjson5.so';
        }

        $candidates = [
            // Development build
            ...$ldPaths,
            $here . '/libvarjson5.so',
            $here . '/../build/libvarjson5.so',
            $here . '/../libvarjson5.so',
            // cmake --install (default prefix /usr/local)
            '/usr/local/lib/libvarjson5.so',
            // CPack package install (CPACK_PACKAGING_INSTALL_PREFIX = /usr)
            '/usr/lib/libvarjson5.so',
            // Multiarch (Debian/Ubuntu)
            '/usr/lib/x86_64-linux-gnu/libvarjson5.so',
            '/usr/lib/aarch64-linux-gnu/libvarjson5.so',
        ];

        foreach ($candidates as $path) {
            if (file_exists($path)) {
                return realpath($path);
            }
        }

        throw new \RuntimeException(
            'libvarjson5.so not found. ' .
            'Build the project first (cmake + make) or set LD_LIBRARY_PATH.'
        );
    }
}


// ---------------------------------------------------------------------------
// Demo  (php examples/Varjson5.php)
// ---------------------------------------------------------------------------
if (basename(__FILE__) === basename($_SERVER['SCRIPT_FILENAME'] ?? '')) {

    $vj = new Varjson5();

    // --- 1. Basic {{vars}} substitution ---
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

    echo "=== 1. vars substitution ===\n";
    echo $vj->process($src1) . "\n";

    // --- 2. jq-style filter ---
    echo "\n=== 2. filter .app.tag ===\n";
    echo $vj->process($src1, '.app.tag', raw: true) . "\n";

    // --- 3. Compact output ---
    echo "\n=== 3. compact output ===\n";
    echo $vj->process($src1, '.app', compact: true) . "\n";

    // --- 4. JSON5 features (comments, trailing comma, unquoted keys) ---
    $src2 = <<<'JSON5'
{
  // JSON5 comment
  vars: { host: "localhost", port: "5432" },
  dsn: "postgres://{{host}}:{{port}}/db",
}
JSON5;

    echo "\n=== 4. JSON5 features ===\n";
    echo $vj->process($src2, '.dsn', raw: true) . "\n";

    // --- 5. Array iteration ---
    $src3 = <<<'JSON5'
{
  "vars": { "prefix": "item" },
  "list": ["{{prefix}}_a", "{{prefix}}_b", "{{prefix}}_c"]
}
JSON5;

    echo "\n=== 5. array .list[] ===\n";
    echo $vj->process($src3, '.list[]', raw: true) . "\n";

    // --- 6. processToArray ---
    echo "\n=== 6. processToArray ===\n";
    $arr = $vj->processToArray('{"vars":{"n":42},"data":{"x":"{{n}}","y":2}}', '.data');
    var_dump($arr);

    // --- 7. Error handling ---
    echo "\n=== 7. error handling ===\n";
    try {
        $vj->process('{ invalid json !!!');
    } catch (Varjson5Exception $e) {
        echo 'Caught Varjson5Exception: ' . $e->getMessage() . "\n";
    }
}
