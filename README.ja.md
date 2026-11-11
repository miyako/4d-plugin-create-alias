# Create Alias

[English](README.md) | 日本語

macOS で本物の Finder エイリアスを、Windows で Shell Link
ショートカットを作成するネイティブ 4D プラグインです。コマンドは1つ、結果は構造化オブジェクト1つです。コンポーネント、補助
CLI、シェル呼び出し、シンボリックリンク、ハードリンク、ランチャー、ブックマーク BLOB API は使用しません。

## 要件

- 4D v20 以降の協調プロセス。
- macOS 11 以降（ユニバーサル arm64/x86_64）、または Windows x64。
- 参照先は既存の通常のファイルまたはフォルダーであること。

ホストテストプロジェクトでは、利用可能な **20.6 LTS** tool4d ランタイムを使用します。これより
前のすべての v20 パッチで実行済みであることを意味するものではありません。

## インストール

ソースからビルドするか、提供されている場合はメンテナーが公開した配布物を使用してください。
`create-alias.bundle` をアプリケーションまたはプロジェクトの **Plugins**
フォルダーにコピーし、4D を再起動します。統合配布物には両プラットフォームが含まれます。
このリポジトリに初期バージョンが設定されていることは、リリースの公開を意味しません。

## コマンド

```4d
var $status : Object
$status:=CREATE_ALIAS_EX($targetPath; $aliasPath {; $options})
```

波括弧は省略可能な引数を示すものであり、実際の 4D 構文には含めません。

| 引数 | 型 | 意味 |
|---|---|---|
| `$targetPath` | テキスト | 参照先となる既存のファイルまたはフォルダー。空の場合は `INVALID_TARGET`。 |
| `$aliasPath` | テキスト | 新しいエイリアス／ショートカットのパス。親フォルダーがすでに存在している必要があります。 |
| `$options` | オブジェクト | 後述の省略可能なオプション。省略または null の場合はオプションなし。変更されることはありません。 |
| `$status` | オブジェクト | ブールの `OK` とコレクションの `errors`。成功時にはパスと `kind` も含まれます。 |

```4d
var $status : Object
var $folder : 4D.Folder
$folder:=Folder(fk desktop folder)
$status:=CREATE_ALIAS_EX($folder.file("report.pdf").platformPath; $folder.file("report alias").platformPath)
If (Not($status.OK))
    LOG EVENT(Into system standard outputs; JSON Stringify($status.errors)+Char(Line feed); Error message)
End if
```

このコマンドは **`OK` システム変数を読み取りも設定もしません**。`OK` ではなく
`$status.OK` を確認してください。検証エラーとネイティブエラーはダイアログを
表示せずに返されます。宣言されたコマンドシグネチャの型に関するランタイムエラーは、
プラグインが実行される前に 4D エラーとなります。

### オプション

すべての名前と列挙値は大文字と小文字を区別します。スペルミスを含め、不明なキーはエラーです。
値の型は自動変換されません。プロパティ値の null はデフォルト値を意味しません。
ファイルシステムへの確定配置より前に、すべての検証を行います。エラーは最初に検出された時点で返されます（
コレクションは、無効なフィールドをすべて網羅した一覧ではありません）。

| オプション | 型／デフォルト | 仕様 |
|---|---|---|
| `overwrite` | ブール／`false` | 既存の本物のエイリアス／ショートカットのみ置き換えを許可します。無関係なファイル、ディレクトリ、シンボリックリンク／再解析ポイントは置き換えません。 |
| `pathFormat` | テキスト／`"system"` | 従来の 4D プラットフォームパス構文。`"posix"` は macOS の POSIX パスを明示的に選択します。Windows では非対応です。 |
| `arguments` | テキストのコレクション／指定なし | Windows のみ。順序付きの引数を Windows CRT 形式の引用規則でエンコードします。空のコレクションは引数なしを意味します。 |
| `argumentsText` | テキスト／指定なし | Windows のみ。加工せずに渡す Windows 引数文字列。どちらかが空でも `arguments` とは同時に指定できません。 |
| `workingDirectory` | テキスト／指定なし | Windows のみ。既存のディレクトリ。参照先と同じパス規則を適用します。 |
| `description` | テキスト／指定なし | Windows のみ。ショートカットの説明。 |
| `iconPath` | テキスト／指定なし | Windows のみ。既存のアイコンファイル／リソースのパス。同じパス規則を適用します。 |
| `iconIndex` | 整数値／`iconPath` 指定時は `0` | Windows のみ。符号付きリソースインデックス。`iconPath` が必要です。小数値や範囲外の値はエラーです。 |
| `showCommand` | 整数値／`1` | Windows のみ。`1` = 通常、`3` = 最大化、`7` = アクティブ化せずに最小化。それ以外の値はエラーです。 |

Windows 専用のフィールドは、macOS ではすべて `UNSUPPORTED_OPTION` となります。**空の
値やゼロであっても同様です**。Finder エイリアスに起動引数を設定することはできません。`.app`
ランチャーが暗黙に生成されることもありません。

#### Windows の引数

```4d
var $options; $status : Object
$options:=New object("arguments"; New collection("a b"; "日本語"; ""; "quoted \"text\""; "C:\\ending\\"))
$status:=CREATE_ALIAS_EX("C:\\Tools\\viewer.exe"; "C:\\Users\\Public\\Desktop\\viewer"; $options)
```

`arguments` は各要素を1つの argv 要素として扱います。引数を二重引用符で囲み、
引用符の直前のバックスラッシュを二重にし、引用符をエスケープします。
引用符で囲んだ引数の末尾のバックスラッシュも二重にします。空の要素は
`""` になります。これは Microsoft C/C++ ランタイムの規則に従うものであり、`cmd.exe`、
PowerShell、シェルのコマンドライン、あるいはすべてのアプリケーション独自のパーサーに対応するものではありません。
引数は**パス変換、展開、シェル構文としての解釈を一切行いません**。
`argumentsText` はシリアライズを行わず、引用符の扱いは呼び出し元の責任となります。
argv の解析と実行時の動作は参照先が制御します。

Windows Shell Link のフィールドには、対応範囲として保守的な上限を設けています。UTF-16
コード単位で、パスと説明は 259、シリアライズ済み／生の引数文字列は
1,023（引用符の処理分を含む）です。これらは OS の絶対的な最大値を
示すものではありません。上限を超える値はエラーとなり、暗黙に切り詰められることはありません。バックエンドは、
準備済みのショートカットを新しい COM オブジェクトに再読み込みし、確定配置の前に引数、
説明、参照先、ワーキングディレクトリ、アイコンのパス／インデックス、表示状態を
検証します。メモリ内の setter の成功だけでは受け入れ条件を満たしません。
説明の上限は、引数の解釈も保護します。Windows の
受け入れテストでは、上限を超える長い説明はバイナリファイル内ではそのまま保持されていましたが、
プラットフォームの再読み込み後の getter は説明を切り詰め、隣接する引数を誤って読み取りました。
したがって、シリアライズされたバイト列が有効であるだけでは、受け入れ条件を満たしません。

### パス

**デフォルトの `"system"`** は、`Macintosh HD:Users:me:report.pdf` のような
従来の macOS 絶対パス、および `C:\Reports\report.pdf` や `\\server\share\report.pdf` のような
Windows のドライブ／UNC パスを受け付けます。
`/Users/me/report.pdf` のような macOS POSIX パスを使用するには、
`New object("pathFormat"; "posix")` が必要です。

相対パスは、**呼び出しごとに1回取得した、実際の 4D ホストプロセスの
カレントワーキングディレクトリのスナップショット**を基準とします。プラグインがその
ディレクトリを変更することはありません。必ずしもプロジェクト／データベースフォルダーではなく、
プラグインフォルダーでもありません。取得に失敗した場合は `CWD_ERROR` となり、代替の基準は使用しません。

| モード | 相対パス構文 |
|---|---|
| macOS system | `:folder:file`、`::sibling:file`、またはファイル名のみ。`volume:folder:file` は絶対パスです。 |
| macOS POSIX | `folder/file`、`../sibling/file`。先頭が `/` の場合は絶対パスです。 |
| Windows system | `folder\file`、`..\sibling\file`。ドライブ相対の `C:file` とルート相対の `\file` は、曖昧なため拒否されます。 |

アダプターは、4D のパス変換コマンドを呼び出す**前に**、従来の macOS 相対パス構文の
基準を確定します。これは、相対パスに
[`Convert path system to POSIX`](https://developer.4d.com/docs/commands/convert-path-system-to-posix)
を直接呼び出す場合とは意図的に異なります。その場合の基準は**データベースフォルダー**です。
絶対パスの変換には、そのコマンドと
[`Convert path POSIX to system`](https://developer.4d.com/docs/commands/convert-path-posix-to-system)
を URL エンコードなしで使用し、文字列の置換は行いません。
`/PACKAGE` のような 4D ファイルシステムエイリアスは、ネイティブの POSIX パスではありません。

これらの規則は、参照先、配置先、`workingDirectory`、`iconPath`、および
返されるパスに適用されます。**引数や説明には適用されません**。NUL および不正な Unicode
は拒否されます。返されるパスは選択した形式の絶対パスであり、字句的に正規化されますが、
正規の物理パスであることは保証しません。
Windows では、`.lnk` が付いていなければ（大文字と小文字を区別せずに判定して）追加します。返される `aliasPath`
にはこの拡張子が含まれます。macOS では拡張子を追加しません。

### 結果とエラー

成功時：

```json
{
  "OK": true,
  "targetPath": "<absolute path in selected format>",
  "aliasPath": "<actual absolute destination>",
  "kind": "alias",
  "errors": []
}
```

Windows では `"kind": "shortcut"` を返します。失敗時：

```json
{
  "OK": false,
  "errors": [
    {"code": "UNSUPPORTED_OPTION", "message": "Launch options are Windows-only", "option": "arguments"}
  ]
}
```

すべてのエラーには、安定した `code` と人間が読める `message` があります。`option` は、
特定できる場合に関連フィールドを示します（`targetPath`／`aliasPath` も含みます）。
ネイティブ／ホストのエラーには、さらに `nativeDomain` と数値の
`nativeCode` が含まれます。メッセージとネイティブ値はプラットフォームに依存します。失敗時には、
成功時専用のパス／kind フィールドが含まれることは保証しません。

アダプターのコードには、`INVALID_TARGET`、`INVALID_PATH`、`INVALID_PATH_FORMAT`、
`INVALID_TEXT`、`UNKNOWN_OPTION`、`INVALID_OPTION_TYPE`、`INVALID_OPTION`、
`CONFLICTING_OPTIONS`、`UNSUPPORTED_OPTION`、`CWD_ERROR`、
`PATH_CONVERSION_FAILED`、`HOST_ERROR`、`INTERNAL_ERROR` があります。
ネイティブの失敗では、参照先の検査、配置先の保護、
エイリアス／ショートカットの構築、確定配置、クリーンアップのエラーも識別します。

| ネイティブコード | 意味 |
|---|---|
| `INVALID_PATH`, `INVALID_OPTION` | Shell Link の長さ制限を含む、無効なネイティブパス／フィールド。 |
| `TARGET_NOT_FOUND` | 参照先が存在しません。 |
| `UNSUPPORTED_TARGET` | 最終的な参照先がシンボリックリンク／再解析ポイントまたは特殊ファイルであり、通常のファイル／フォルダーではありません。 |
| `SELF_DESTINATION` | macOS の参照先と配置先が同じエントリーを指しています。 |
| `UNSUPPORTED_OPTION` | macOS で Windows の起動オプションが指定されました。 |
| `DESTINATION_EXISTS` | 上書き禁止の確定配置で既存のエントリーが見つかりました。 |
| `UNSAFE_OVERWRITE` | 既存の配置先が、安全に置き換えられる本物のエイリアス／ショートカットではありません。 |
| `CONCURRENT_MODIFICATION` | macOS で、置き換え中にエントリー／バージョンの変更を検出しました。 |
| `IO_ERROR` | ネイティブの検査、準備、COM、シリアライズ、または確定配置に失敗しました。 |
| `CLEANUP_FAILED` | 準備用ファイル／バックアップのクリーンアップに失敗しました。メッセージと、保持された復旧用パスがあれば確認してください。 |

通常のエラー仕様は、4D のアロケーター／API が機能していることを前提とします。
ホストが結果オブジェクト自体を確保できない場合、プラグインは明示的な
診断を stderr に出力し、オブジェクトを保証できません。成功を装うことはありません。

### 確定配置と上書き

作成処理は、配置先と同じファイルシステム上の隣接する場所で準備した後、
書き込み途中のエイリアスが現れないように確定配置します。デフォルトの上書き禁止の配置では、
壊れたシンボリックリンクを含め、既存の配置先を置き換えません。
`overwrite:true` は、**本物のエイリアス／ショートカット**の置き換えを許可するものであり、
汎用的なファイル上書きオプションではありません。参照先と配置先が同じ場合はエラーです。
ネイティブのシリアライズまたは確定配置に失敗した場合、既存の配置先は保持されます。
同時に行われる変更を検査し、ネイティブ API が許す範囲で安全側に倒して失敗させます。
ただし、親ディレクトリを書き換えられる悪意のあるプロセスに対するトランザクションではありません。
共有の書き込み可能なディレクトリをセキュリティ境界として使用しないでください。
親フォルダーは作成しません。
上位ディレクトリのリンクは OS の通常の規則で解決されます。最終的な参照先と
配置先にシンボリックリンク／再解析ポイントは使用できません。ロールバックの安全性を
確認できない場合、退避された元のファイルは削除せず、エラーメッセージで報告する
復旧用パスに保持します。**確定配置の後**にクリーンアップエラーが報告される
場合があります。再試行する前に実際の配置先を確認してください。これらの復旧に関する
状況は、確定前のシリアライズ／配置の失敗とは異なります。

## ソースからのビルド

CMake 3.20 以降と、macOS では Xcode、Windows では C++ 対応の Visual Studio が必要です。
リンクするのは OS のライブラリのみです。SDK は特定のコミットに固定したサブモジュールです。

```sh
git clone --recurse-submodules https://github.com/miyako/4d-plugin-create-alias.git
cd 4d-plugin-create-alias
cmake -S create-alias -B create-alias/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build create-alias/build --config Release
ctest --test-dir create-alias/build -C Release --output-on-failure
```

Windows では、`-DCMAKE_OSX_ARCHITECTURES=...` の代わりに `-A x64` を使用してください。
Windows のプラグインとテストは静的 CRT を使用します。ビルド出力は、
テストプロジェクトの Git 管理対象外の `Plugins` フォルダーに配置されます。macOS では `APPL` ではなく `BNDL` を作成します。

## 検証とホストのサンプル

macOS のネイティブテストは実際の Foundation ブックマークファイルを使用し、Finder エイリアスの
メタデータを確認して、元のファイル／フォルダーに解決されることを検証します。Windows のテストは実際の COM
プロパティの読み戻しを行い、**生成した `.lnk` を通じて無害な argv 記録用実行ファイルを
起動**して、空白、Unicode、空の引数、埋め込まれた引用符、
末尾のバックスラッシュを比較します。ネイティブテストは SDK の代替実装ではなく、ホスト API の
動作を証明するものでもありません。

実際の 4D ホストプロジェクトは
`create-alias/create-alias-test/Project/create-alias.4DProject` です。
`sample_create_alias` は、v20 互換の呼び出し可能なサンプルです。
ホストテストでは、構造化オブジェクト、オプションの省略、オプションの不変性、
絶対／相対パス、POSIX の明示的な選択、エラー、上書き、`OK` の監視用値を検証します。
条件を制御した相対パスのテスト用フィクスチャを使用するため、リポジトリのルートから起動してください。

```sh
/path/to/tool4d --dataless --startup-method=test_all \
  --project="$PWD/create-alias/create-alias-test/Project/create-alias.4DProject" \
  > host-test.log 2>&1
result=$?
cat host-test.log
test "$result" = 0 && grep -qx PASS host-test.log
```

フィクスチャは、プラグインとは独立してテストプロジェクトの
場所から期待する起動ディレクトリを導き出し、そこに一意の名前のフィクスチャを
作成します。生成された CI も同様にリポジトリのルートから起動します。
完全一致の `PASS` 行**と**終了ステータスゼロの両方が必要です。アサーションが失敗しても、
終了ステータスがゼロになることがあります。

手書きのテストランナーは、組み込みトークン `Application info:C1599` を使用します。
この v20.6 フィクスチャでは、トークンなしの `Application info` は未定義と評価されました。
この観測結果は、v20 の構文解析や IDE で作成したプロジェクトに関する
一般的な主張ではありません。公開された `CREATE_ALIAS_EX` の呼び出しには引き続きトークンを付けません。

## CI/CD

個別に手書きしたワークフローではなく、追跡対象の正規
[パイプライン](docs/plugin-pipeline.md)から生成します。

| ワークフロー | トリガー | 目的 |
|---|---|---|
| **Bump version** | `main` で手動実行 | patch/minor/major の `VERSION` 変更をコミット。タグは作成しません。 |
| **Build and Test** | プッシュ／PR／手動／再利用可能 | ユニバーサル macOS と Windows x64 のビルド、ネイティブおよび実際のホストでのテスト、パッケージの受け入れ検証。 |
| **Build and Release** | `main` で明示的に手動実行、入力なし | 同一リビジョンのテスト、Developer ID 署名／公証、検証済みの `vVERSION` 公開。 |

```sh
python3 scripts/generate_plugin_pipeline.py --config .github/plugin-pipeline.json --output "$PWD" --check
```

通常のテストには署名用のシークレットは不要です。リリースの署名には、
`release-signing` 環境の `APPLE_DEVELOPER_ID_CERTIFICATE`、`APPLE_DEVELOPER_ID_CERTIFICATE_PASSWORD`、
`KEYCHAIN_PASSWORD`、`NOTARYTOOL_APPLE_ID`、`NOTARYTOOL_TEAM_ID`、
`NOTARYTOOL_PASSWORD` を使用します。
[ユーザーが実行する認証情報の手順](.github/instructions/plugin-dev.instructions.md#required-github-secrets-for-release)に従ってください。
エージェントは認証情報の値を調査／アップロードしてはいけません。Windows の配布物は
明示的に未署名です。このパイプラインの生成は、**リリースの承認ではありません**。

## 参考資料

読み取り専用の先行実装：
[`4d-plugin-alias-manager`](https://github.com/miyako/4d-plugin-alias-manager)
および [`4d-plugin-bookmark-manager`](https://github.com/miyako/4d-plugin-bookmark-manager)。
いずれも依存関係ではありません。このコマンドは、それらの API を維持するものではなく、
Finder エイリアスの範囲をシンボリックリンク／ハードリンクやランチャーに広げるものでもありません。

## ライセンス

[MIT](LICENSE)。SDK のライセンス情報は、固定された
[`4D-Plugin-SDK`](4D-Plugin-SDK) サブモジュールと、生成されたパイプラインのライセンス通知に含まれます。
