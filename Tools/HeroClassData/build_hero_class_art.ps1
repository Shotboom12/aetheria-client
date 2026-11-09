param(
    [Parameter(Mandatory = $true)]
    [string]$SourcePng,
    [Parameter(Mandatory = $true)]
    [string]$OutputBlp
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $SourcePng -PathType Leaf)) {
    throw "Source image not found: $SourcePng"
}
if (Test-Path -LiteralPath $OutputBlp) {
    throw "Refusing to overwrite: $OutputBlp"
}

Add-Type -AssemblyName System.Drawing.Common
$source = [System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $SourcePng).Path)
$mips = [System.Collections.Generic.List[byte[]]]::new()
try {
    for ($size = 128; $size -ge 1; $size = [Math]::Max(1, [int]($size / 2))) {
        $bitmap = [System.Drawing.Bitmap]::new(
            $size,
            $size,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
        )
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.DrawImage($source, 0, 0, $size, $size)
            }
            finally {
                $graphics.Dispose()
            }

            $rect = [System.Drawing.Rectangle]::new(0, 0, $size, $size)
            $bits = $bitmap.LockBits(
                $rect,
                [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
            )
            try {
                $pixels = [byte[]]::new($size * $size * 4)
                for ($row = 0; $row -lt $size; $row++) {
                    $sourceRow = [IntPtr]::Add($bits.Scan0, $row * $bits.Stride)
                    [System.Runtime.InteropServices.Marshal]::Copy(
                        $sourceRow,
                        $pixels,
                        $row * $size * 4,
                        $size * 4
                    )
                }
                $mips.Add($pixels)
            }
            finally {
                $bitmap.UnlockBits($bits)
            }
        }
        finally {
            $bitmap.Dispose()
        }
        if ($size -eq 1) { break }
    }
}
finally {
    $source.Dispose()
}

$parent = Split-Path -Parent $OutputBlp
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent | Out-Null
}

# BLP2 raw BGRA32, with a complete mip chain, matching the existing Aetheria class-card assets.
$headerSize = 148
$paletteSize = 1024
$offsets = [uint32[]]::new(16)
$sizes = [uint32[]]::new(16)
$offset = [uint32]($headerSize + $paletteSize)
for ($i = 0; $i -lt $mips.Count; $i++) {
    $offsets[$i] = $offset
    $sizes[$i] = [uint32]$mips[$i].Length
    $offset += $sizes[$i]
}

$stream = [System.IO.File]::Open(
    $OutputBlp,
    [System.IO.FileMode]::CreateNew,
    [System.IO.FileAccess]::Write,
    [System.IO.FileShare]::None
)
try {
    $writer = [System.IO.BinaryWriter]::new($stream)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('BLP2'))
    $writer.Write([uint32]1)
    $writer.Write([byte]3) # uncompressed BGRA32
    $writer.Write([byte]8)
    $writer.Write([byte]8)
    $writer.Write([byte]1)
    $writer.Write([uint32]128)
    $writer.Write([uint32]128)
    foreach ($value in $offsets) { $writer.Write($value) }
    foreach ($value in $sizes) { $writer.Write($value) }
    $writer.Write([byte[]]::new($paletteSize))
    foreach ($mip in $mips) { $writer.Write($mip) }
    $writer.Flush()
}
finally {
    $stream.Dispose()
}

Get-FileHash -LiteralPath $OutputBlp -Algorithm SHA256
