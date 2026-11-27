param(
    [string] $SdkPath = $env:ANDROID_HOME,
    [string] $KeyStore = $env:LAWCLIENT_ANDROID_KEYSTORE,
    [string] $KeyAlias = $env:LAWCLIENT_ANDROID_KEY_ALIAS
)

$ErrorActionPreference = 'Stop'
if (!$SdkPath -or !$env:JAVA_HOME) { throw 'Set ANDROID_HOME and JAVA_HOME before building.' }
if (!$KeyStore -or !$KeyAlias -or !$env:LAWCLIENT_ANDROID_STORE_PASSWORD) {
    throw 'Provide the release keystore, alias, and LAWCLIENT_ANDROID_STORE_PASSWORD.'
}

$env:ANDROID_HOME = (Resolve-Path -LiteralPath $SdkPath).Path
$KeyStore = (Resolve-Path -LiteralPath $KeyStore).Path
$root = Split-Path -Parent $PSScriptRoot
$gradle = Join-Path $PSScriptRoot $(if ($IsWindows) { 'gradlew.bat' } else { 'gradlew' })
& $gradle -p $PSScriptRoot :app:assembleRelease
if ($LASTEXITCODE -ne 0) { throw 'Android installer assembly failed.' }

$apk = Join-Path $PSScriptRoot 'app/build/outputs/apk/release/app-release-unsigned.apk'
$signer = Join-Path $env:ANDROID_HOME $(if ($IsWindows) { 'build-tools/35.0.0/apksigner.bat' } else { 'build-tools/35.0.0/apksigner' })
$out = Join-Path $root 'out'
New-Item -ItemType Directory -Force $out | Out-Null
$target = Join-Path $out 'LawClientInstaller.apk'
$keyPassword = if ($env:LAWCLIENT_ANDROID_KEY_PASSWORD) { 'env:LAWCLIENT_ANDROID_KEY_PASSWORD' } else { 'env:LAWCLIENT_ANDROID_STORE_PASSWORD' }
& $signer sign --ks $KeyStore --ks-key-alias $KeyAlias --ks-pass env:LAWCLIENT_ANDROID_STORE_PASSWORD --key-pass $keyPassword --v4-signing-enabled false --out "$target.part" $apk
if ($LASTEXITCODE -ne 0) { throw 'Android installer signing failed.' }
& $signer verify --verbose "$target.part"
if ($LASTEXITCODE -ne 0) { throw 'Android installer signature verification failed.' }
Move-Item -LiteralPath "$target.part" -Destination $target -Force
$hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$target.sha256" -Value $hash -Encoding ascii -NoNewline
Get-Item -LiteralPath $target
