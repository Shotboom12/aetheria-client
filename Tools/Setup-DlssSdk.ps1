# Downloads the NVIDIA DLSS SDK from NVIDIA's own public repository (github.com/NVIDIA/DLSS) into ThirdParty/DLSS (headers + nvsdk_ngx_d.lib).
# The SDK is NOT redistributed by this repository (NVIDIA RTX SDK license); this script only fetches the official copy. The tag below is the version
# the code was written against. Run from anywhere: pwsh Tools/Setup-DlssSdk.ps1 [-Tag v310.9.16]
param([string]$Tag = 'v310.9.16')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dest = Join-Path $root 'ThirdParty\DLSS'
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('dlss-sdk-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    $zip = Join-Path $tmp 'dlss.zip'
    Invoke-WebRequest -Uri "https://github.com/NVIDIA/DLSS/archive/refs/tags/$Tag.zip" -OutFile $zip
    Expand-Archive -LiteralPath $zip -DestinationPath $tmp
    $src = Get-ChildItem $tmp -Directory | Where-Object { $_.Name -like 'DLSS-*' } | Select-Object -First 1
    if (-not $src) { throw 'The archive did not contain the DLSS repository.' }
    foreach ($need in 'include\nvsdk_ngx_vk.h', 'lib\Windows_x86_64\x64\nvsdk_ngx_d.lib') {
        if (-not (Test-Path (Join-Path $src.FullName $need))) { throw "Missing in the NVIDIA archive: $need" }
    }
    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item (Join-Path $src.FullName 'include') $dest -Recurse -Force
    New-Item -ItemType Directory -Force (Join-Path $dest 'lib\Windows_x86_64\x64') | Out-Null
    Copy-Item (Join-Path $src.FullName 'lib\Windows_x86_64\x64\nvsdk_ngx_d.lib') (Join-Path $dest 'lib\Windows_x86_64\x64') -Force
    if (Test-Path (Join-Path $src.FullName 'LICENSE.txt')) { Copy-Item (Join-Path $src.FullName 'LICENSE.txt') $dest -Force }
    Write-Output "DLSS SDK $Tag installed into $dest. Configure with -DAETHERIA_NATIVE_FSR=ON -DAETHERIA_NATIVE_DLSS=ON."
} finally { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
