jqコマンドと同様な感じでjson5のパーサーを実装してください。  
mustachのように{{}}で囲まれた値を変数として扱得るようにしてください。
具体的には以下のように"vars": {}で指定された内部は変数として扱い、同じjson5ファイル内で展開して扱えるようにしてください

例:
入力
{
"vars": {
    "key01": "abcdefg"
},
"body": {
    "txt": "data is {{key01}}"
}
}

出力
{
"vars": { "key01": "abcdefg"},
"body": {
"txt": "data is abcdefg"
}
}

言語はc++
