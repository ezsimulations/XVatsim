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
$outputPath = Join-Path $projectRoot 'mockups\xvatsim-ui-mock-v2-flat.png'
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

$bgBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    (New-Object System.Drawing.PointF(0, 0)),
    (New-Object System.Drawing.PointF(0, $height)),
    ([System.Drawing.Color]::FromArgb(255, 19, 28, 38)),
    ([System.Drawing.Color]::FromArgb(255, 10, 14, 20))
)
$graphics.FillRectangle($bgBrush, 0, 0, $width, $height)

$haze1 = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(20, 140, 190, 220))
$haze2 = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(16, 255, 255, 255))
$graphics.FillEllipse($haze1, 920, 78, 520, 220)
$graphics.FillEllipse($haze2, 1080, 548, 310, 120)
$graphics.FillEllipse($haze2, 150, 650, 420, 150)

$shadowRect = New-Object System.Drawing.RectangleF(62, 62, 560, 424)
$shadowPath = New-RoundedRectPath -Rect $shadowRect -Radius 24
$shadowBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(54, 0, 0, 0))
$graphics.FillPath($shadowBrush, $shadowPath)

$panelRect = New-Object System.Drawing.RectangleF(46, 46, 560, 424)
$panelPath = New-RoundedRectPath -Rect $panelRect -Radius 24
$panelBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    (New-Object System.Drawing.PointF($panelRect.Left, $panelRect.Top)),
    (New-Object System.Drawing.PointF($panelRect.Left, $panelRect.Bottom)),
    ([System.Drawing.Color]::FromArgb(218, 26, 33, 40)),
    ([System.Drawing.Color]::FromArgb(210, 16, 21, 28))
)
$panelBorder = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(88, 154, 168, 181)), 1
$graphics.FillPath($panelBrush, $panelPath)
$graphics.DrawPath($panelBorder, $panelPath)

$dividerPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(55, 164, 176, 188)), 1

$fontBrand = New-Object System.Drawing.Font('Segoe UI Semibold', 20, [System.Drawing.FontStyle]::Regular)
$fontBadge = New-Object System.Drawing.Font('Segoe UI Semibold', 10, [System.Drawing.FontStyle]::Regular)
$fontCallsign = New-Object System.Drawing.Font('Segoe UI Semibold', 22, [System.Drawing.FontStyle]::Regular)
$fontMeta = New-Object System.Drawing.Font('Segoe UI', 12, [System.Drawing.FontStyle]::Regular)
$fontRow = New-Object System.Drawing.Font('Segoe UI Semibold', 18, [System.Drawing.FontStyle]::Regular)
$fontRowSmall = New-Object System.Drawing.Font('Segoe UI', 15, [System.Drawing.FontStyle]::Regular)
$fontFooter = New-Object System.Drawing.Font('Segoe UI', 13, [System.Drawing.FontStyle]::Regular)

$whiteBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(238, 241, 244, 247))
$mutedBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(190, 182, 191, 200))
$cyanBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(182, 230, 241))
$greenBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(170, 239, 203))
$yellowBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(246, 209, 126))
$badgeFill = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(40, 86, 122, 120))
$onlineDot = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(70, 198, 138))
$cyanDot = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(90, 157, 238, 244))
$yellowDot = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(90, 246, 209, 126))

$graphics.FillEllipse($onlineDot, 74, 76, 14, 14)
$graphics.DrawString('XVatsim', $fontBrand, $whiteBrush, 104, 65)

$badgeRect = New-Object System.Drawing.RectangleF(220, 67, 118, 28)
$badgePath = New-RoundedRectPath -Rect $badgeRect -Radius 7
$graphics.FillPath($badgeFill, $badgePath)
$graphics.DrawPath($panelBorder, $badgePath)
$graphics.DrawString('CONNECTED', $fontBadge, $cyanBrush, 238, 74)
$graphics.DrawString('UAL2457', $fontCallsign, $cyanBrush, 74, 116)
$graphics.DrawString('CRZ FL360', $fontMeta, $mutedBrush, 475, 123)
$graphics.DrawLine($dividerPen, 74, 158, 578, 158)

$graphics.FillEllipse($cyanDot, 75, 186, 8, 8)
$graphics.DrawString('39 ATC', $fontRow, $whiteBrush, 104, 176)
$graphics.DrawLine($dividerPen, 74, 224, 578, 224)

$graphics.DrawString('ZHU_CTR', $fontRow, $greenBrush, 104, 244)
$graphics.DrawString('132.775', $fontRowSmall, $greenBrush, 382, 247)
$graphics.DrawString('*ACTIVE*', $fontRowSmall, $greenBrush, 472, 247)
$graphics.DrawLine($dividerPen, 74, 290, 578, 290)

$graphics.DrawString('MMUN_CTR', $fontRow, $yellowBrush, 104, 309)
$graphics.DrawString('126.900', $fontRowSmall, $yellowBrush, 382, 312)
$graphics.DrawString('*NEXT*', $fontRowSmall, $yellowBrush, 479, 312)
$graphics.DrawLine($dividerPen, 74, 355, 578, 355)

$graphics.DrawString('MRHO_ATIS', $fontRow, $whiteBrush, 104, 374)
$graphics.DrawString('127.650', $fontRowSmall, $mutedBrush, 472, 377)
$graphics.DrawLine($dividerPen, 74, 420, 578, 420)

$graphics.FillEllipse($yellowDot, 75, 444, 8, 8)
$graphics.DrawString('KIAH -> MRHO', $fontRow, $cyanBrush, 104, 434)
$graphics.DrawString('954nm remaining', $fontFooter, $mutedBrush, 104, 467)

$bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)

$bgBrush.Dispose()
$haze1.Dispose()
$haze2.Dispose()
$shadowBrush.Dispose()
$shadowPath.Dispose()
$panelBrush.Dispose()
$panelBorder.Dispose()
$panelPath.Dispose()
$dividerPen.Dispose()
$whiteBrush.Dispose()
$mutedBrush.Dispose()
$cyanBrush.Dispose()
$greenBrush.Dispose()
$yellowBrush.Dispose()
$badgeFill.Dispose()
$badgePath.Dispose()
$onlineDot.Dispose()
$cyanDot.Dispose()
$yellowDot.Dispose()
$fontBrand.Dispose()
$fontBadge.Dispose()
$fontCallsign.Dispose()
$fontMeta.Dispose()
$fontRow.Dispose()
$fontRowSmall.Dispose()
$fontFooter.Dispose()
$graphics.Dispose()
$bitmap.Dispose()

Write-Output $outputPath
