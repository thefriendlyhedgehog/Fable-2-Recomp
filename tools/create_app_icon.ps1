param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$sourceImage = [Drawing.Image]::FromFile((Resolve-Path -LiteralPath $Source).Path)
try {
    $outputPath = [IO.Path]::GetFullPath($OutputDirectory)
    New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
    $iconImages = @()
    foreach ($iconSize in @(256,128,64,48,32,16)) {
        $bitmap = [Drawing.Bitmap]::new($iconSize,$iconSize)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.DrawImage($sourceImage,0,0,$iconSize,$iconSize)
            $stream = [IO.MemoryStream]::new()
            try {
                $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
                $iconImages += ,$stream.ToArray()
                if ($iconSize -eq 256) { [IO.File]::WriteAllBytes((Join-Path $outputPath 'app-icon.png'),$stream.ToArray()) }
            } finally { $stream.Dispose() }
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
    }
    $icoStream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($icoStream)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$iconImages.Count)
        $offset = 6 + 16 * $iconImages.Count
        $sizes = @(256,128,64,48,32,16)
        for ($i=0; $i -lt $iconImages.Count; $i++) {
            $dimension = [byte]($sizes[$i] % 256)
            $writer.Write($dimension); $writer.Write($dimension)
            $writer.Write([byte]0); $writer.Write([byte]0)
            $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$iconImages[$i].Length); $writer.Write([uint32]$offset)
            $offset += $iconImages[$i].Length
        }
        foreach ($iconImage in $iconImages) { $writer.Write([byte[]]$iconImage) }
        $writer.Flush()
        [IO.File]::WriteAllBytes((Join-Path $outputPath 'app-icon.ico'),$icoStream.ToArray())
    } finally { $writer.Dispose(); $icoStream.Dispose() }
} finally { $sourceImage.Dispose() }
