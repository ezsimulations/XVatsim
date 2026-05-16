Add-Type -AssemblyName System.Drawing

function New-RoundedRectPath {
    param(
        [System.Drawing.RectangleF]$Rect,
        [float]$Radius
    )

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $diameter = $Radius * 2
    $path.AddArc($Rect.X, $Rect.Y, $diameter, $diameter, 180, 90)
    $path.AddArc($Rect.Right - $diameter, $Rect.Y, $diameter, $diameter, 270, 90)
    $path.AddArc($Rect.Right - $diameter, $Rect.Bottom - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($Rect.X, $Rect.Bottom - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$outputPath = Join-Path $projectRoot 'mockups\xvatsim-ui-mock-v3-glass.png'
$outputDir = Split-Path -Parent $outputPath
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

$width = 1600
$height = 900
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$graphics.Clear([System.Drawing.Color]::FromArgb(8, 14, 22))

$haze1 = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(22, 110, 178, 215))
$haze2 = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(12, 255, 255, 255))
$haze3 = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(18, 70, 220, 210))
$graphics.FillEllipse($haze1, 945, 82, 455, 180)
$graphics.FillEllipse($haze2, 1030, 565, 240, 88)
$graphics.FillEllipse($haze3, 115, 655, 330, 120)

$shadowRect = New-Object System.Drawing.RectangleF(66, 58, 462, 292)
$shadowPath = New-RoundedRectPath -Rect $shadowRect -Radius 18
$shadowBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(44, 0, 0, 0))
$graphics.FillPath($shadowBrush, $shadowPath)

$panelRect = New-Object System.Drawing.RectangleF(50, 42, 462, 292)
$panelPath = New-RoundedRectPath -Rect $panelRect -Radius 18
$panelBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    (New-Object System.Drawing.PointF($panelRect.Left, $panelRect.Top)),
    (New-Object System.Drawing.PointF($panelRect.Right, $panelRect.Bottom)),
    ([System.Drawing.Color]::FromArgb(178, 24, 34, 46)),
    ([System.Drawing.Color]::FromArgb(150, 12, 18, 27))
)
$panelBorder = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(70, 168, 195, 214)), 1
$graphics.FillPath($panelBrush, $panelPath)
$graphics.DrawPath($panelBorder, $panelPath)

$headerGlow = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    (New-Object System.Drawing.PointF($panelRect.Left, $panelRect.Top)),
    (New-Object System.Drawing.PointF($panelRect.Left, ($panelRect.Top + 42))),
    ([System.Drawing.Color]::FromArgb(34, 180, 220, 240)),
    ([System.Drawing.Color]::FromArgb(0, 180, 220, 240))
)
$graphics.FillRectangle($headerGlow, $panelRect.Left + 1, $panelRect.Top + 1, $panelRect.Width - 2, 50)

$dockRect = New-Object System.Drawing.RectangleF(182, 24, 162, 18)
$dockPath = New-RoundedRectPath -Rect $dockRect -Radius 8
$dockBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(92, 95, 118, 136))
$graphics.FillPath($dockBrush, $dockPath)

$dividerPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(54, 172, 190, 205)), 1

$fontBrand = New-Object System.Drawing.Font('Segoe UI Semibold', 18, [System.Drawing.FontStyle]::Regular)
$fontBadge = New-Object System.Drawing.Font('Segoe UI Semibold', 9, [System.Drawing.FontStyle]::Regular)
$fontMeta = New-Object System.Drawing.Font('Segoe UI', 11, [System.Drawing.FontStyle]::Regular)
$fontCallsign = New-Object System.Drawing.Font('Segoe UI Semibold', 18, [System.Drawing.FontStyle]::Regular)
$fontCount = New-Object System.Drawing.Font('Segoe UI Semibold', 16, [System.Drawing.FontStyle]::Regular)
$fontRow = New-Object System.Drawing.Font('Segoe UI Semibold', 16, [System.Drawing.FontStyle]::Regular)
$fontRowRight = New-Object System.Drawing.Font('Segoe UI', 13, [System.Drawing.FontStyle]::Regular)
$fontFooter = New-Object System.Drawing.Font('Segoe UI', 11, [System.Drawing.FontStyle]::Regular)

$whiteBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(236, 241, 246, 250))
$mutedBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(182, 189, 198, 208))
$cyanBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(186, 220, 244))
$greenBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(169, 242, 205))
$yellowBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(247, 212, 130))
$badgeFill = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(42, 72, 115, 116))
$greenDot = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(74, 199, 139))
$cyanDot = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(108, 164, 234, 244))
$yellowDot = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(118, 247, 212, 130))

$graphics.FillEllipse($greenDot, 70, 66, 12, 12)
$graphics.DrawString('XVatsim', $fontBrand, $whiteBrush, 96, 55)

$badgeRect = New-Object System.Drawing.RectangleF(190, 58, 108, 24)
$badgePath = New-RoundedRectPath -Rect $badgeRect -Radius 7
$graphics.FillPath($badgeFill, $badgePath)
$graphics.DrawString('CONNECTED', $fontBadge, $cyanBrush, 208, 64)
$graphics.DrawString('CRZ', $fontMeta, $mutedBrush, 428, 57)

$graphics.DrawString('UAL2457', $fontCallsign, $cyanBrush, 70, 97)
$graphics.DrawString('FL360', $fontMeta, $mutedBrush, 426, 100)
$graphics.DrawLine($dividerPen, 70, 130, 476, 130)

$graphics.FillEllipse($cyanDot, 70, 149, 6, 6)
$graphics.DrawString('39 ATC', $fontCount, $whiteBrush, 96, 138)
$graphics.DrawLine($dividerPen, 70, 176, 476, 176)

$graphics.DrawString('ZHU_CTR', $fontRow, $greenBrush, 96, 193)
$graphics.DrawString('132.775  ACTIVE', $fontRowRight, $greenBrush, 312, 197)
$graphics.DrawLine($dividerPen, 70, 226, 476, 226)

$graphics.DrawString('MMUN_CTR', $fontRow, $yellowBrush, 96, 242)
$graphics.DrawString('126.900  NEXT', $fontRowRight, $yellowBrush, 327, 246)
$graphics.DrawLine($dividerPen, 70, 275, 476, 275)

$graphics.FillEllipse($yellowDot, 70, 294, 6, 6)
$graphics.DrawString('KIAH -> MRHO', $fontRow, $cyanBrush, 96, 286)
$graphics.DrawString('954nm remaining', $fontFooter, $mutedBrush, 96, 314)

$bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)

$haze1.Dispose()
$haze2.Dispose()
$haze3.Dispose()
$shadowBrush.Dispose()
$shadowPath.Dispose()
$panelBrush.Dispose()
$panelBorder.Dispose()
$panelPath.Dispose()
$headerGlow.Dispose()
$dockBrush.Dispose()
$dockPath.Dispose()
$dividerPen.Dispose()
$whiteBrush.Dispose()
$mutedBrush.Dispose()
$cyanBrush.Dispose()
$greenBrush.Dispose()
$yellowBrush.Dispose()
$badgeFill.Dispose()
$greenDot.Dispose()
$cyanDot.Dispose()
$yellowDot.Dispose()
$badgePath.Dispose()
$fontBrand.Dispose()
$fontBadge.Dispose()
$fontMeta.Dispose()
$fontCallsign.Dispose()
$fontCount.Dispose()
$fontRow.Dispose()
$fontRowRight.Dispose()
$fontFooter.Dispose()
$graphics.Dispose()
$bitmap.Dispose()

Write-Output $outputPath
