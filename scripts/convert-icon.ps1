# Convert a PNG to a multi-size PNG-in-ICO (letterboxed, aspect-preserved).
#   default       : art\S2_icon.png  -> packaging\app.ico   (the app / shield icon)
#   .sentinel file: -Src art\<file-art>.png -Dst packaging\file.ico
param(
    [string]$Src = "G:\SentinelIDE\art\S2_icon.png",
    [string]$Dst = "G:\SentinelIDE\packaging\app.ico"
)
Add-Type -AssemblyName System.Drawing
$src = $Src
$dst = $Dst
$img = [System.Drawing.Image]::FromFile($src)
$sw = $img.Width; $sh = $img.Height
$sizes = @(256, 48, 32, 16)
$blobs = @()
foreach ($s in $sizes) {
    # Icons must be square; preserve the source aspect ratio by scaling-to-fit and
    # centering on a transparent square canvas (letterbox) rather than stretching.
    $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $scale = [Math]::Min($s / $sw, $s / $sh)
    $dw = [int][Math]::Round($sw * $scale); $dh = [int][Math]::Round($sh * $scale)
    $dx = [int](($s - $dw) / 2); $dy = [int](($s - $dh) / 2)
    $g.DrawImage($img, $dx, $dy, $dw, $dh)
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $blobs += , ($ms.ToArray())
    $bmp.Dispose(); $ms.Dispose()
}
$img.Dispose()
$fs = [System.IO.File]::Create($dst)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $len = $blobs[$i].Length
    $dim = if ($s -ge 256) { 0 } else { $s }
    $bw.Write([Byte]$dim); $bw.Write([Byte]$dim); $bw.Write([Byte]0); $bw.Write([Byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32); $bw.Write([UInt32]$len); $bw.Write([UInt32]$offset)
    $offset += $len
}
foreach ($b in $blobs) { $bw.Write($b) }
$bw.Flush(); $bw.Close(); $fs.Close()
Write-Output "wrote $dst ($((Get-Item $dst).Length) bytes, sizes: $($sizes -join ','))"
