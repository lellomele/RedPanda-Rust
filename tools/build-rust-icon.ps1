$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$pngPath = Join-Path $root 'assets/rust/red-panda-rust.png'
$icoPath = Join-Path $root 'assets/rust/red-panda-rust.ico'
if (-not (Test-Path -LiteralPath $pngPath)) { throw "Icon source not found: $pngPath" }

$source = [System.Drawing.Bitmap]::FromFile($pngPath)
if ($source.Width -ne $source.Height) { $source.Dispose(); throw 'Icon source must be square.' }
if ($source.GetPixel(0, 0).A -ne 0) { $source.Dispose(); throw 'Icon source must have a transparent background outside its rounded badge.' }

$frames = [System.Collections.Generic.List[object]]::new()
foreach ($size in @(16, 24, 32, 48, 64, 128, 256)) {
    $bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.DrawImage($source, 0, 0, $size, $size)

        $xorLength = $size * $size * 4
        $maskStride = [int]([math]::Ceiling($size / 32) * 4)
        $maskLength = $maskStride * $size
        $frame = [System.IO.MemoryStream]::new()
        $writer = [System.IO.BinaryWriter]::new($frame)
        try {
            $writer.Write([UInt32]40)
            $writer.Write([Int32]$size)
            $writer.Write([Int32]($size * 2))
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]32)
            $writer.Write([UInt32]0)
            $writer.Write([UInt32]($xorLength + $maskLength))
            $writer.Write([Int32]0)
            $writer.Write([Int32]0)
            $writer.Write([UInt32]0)
            $writer.Write([UInt32]0)

            for ($y = $size - 1; $y -ge 0; $y--) {
                for ($x = 0; $x -lt $size; $x++) {
                    $pixel = $bitmap.GetPixel($x, $y)
                    $writer.Write([byte]$pixel.B)
                    $writer.Write([byte]$pixel.G)
                    $writer.Write([byte]$pixel.R)
                    $writer.Write([byte]$pixel.A)
                }
            }
            $writer.Write([byte[]]::new($maskLength))
            $writer.Flush()
            $frames.Add([pscustomobject]@{ Size = $size; Bytes = $frame.ToArray() })
        }
        finally {
            $writer.Dispose()
            $frame.Dispose()
        }
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}
$source.Dispose()

$file = [System.IO.MemoryStream]::new()
$writer = [System.IO.BinaryWriter]::new($file)
try {
    $writer.Write([UInt16]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]$frames.Count)
    $offset = 6 + 16 * $frames.Count
    foreach ($frame in $frames) {
        $dimension = if ($frame.Size -eq 256) { [byte]0 } else { [byte]$frame.Size }
        $writer.Write($dimension)
        $writer.Write($dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]$frame.Bytes.Length)
        $writer.Write([UInt32]$offset)
        $offset += $frame.Bytes.Length
    }
    foreach ($frame in $frames) { $writer.Write($frame.Bytes) }
    $writer.Flush()
    [System.IO.File]::WriteAllBytes($icoPath, $file.ToArray())
}
finally {
    $writer.Dispose()
    $file.Dispose()
}

$icon = [System.Drawing.Icon]::new($icoPath)
try {
    if ($icon.Size.Width -ne 32 -or $icon.Size.Height -ne 32) { throw 'The generated icon failed its Windows icon format check.' }
}
finally {
    $icon.Dispose()
}
Write-Output "Created transparent 16–256 px Windows icon: $icoPath"
Copy-Item -LiteralPath $icoPath -Destination (Join-Path $root 'RedPandaIDE/resources/appicon/red-panda-rust.ico') -Force
Copy-Item -LiteralPath $pngPath -Destination (Join-Path $root 'RedPandaIDE/resources/appicon/red-panda-rust.png') -Force

