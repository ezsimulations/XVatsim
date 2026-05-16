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

$sourcePath = 'C:\Users\DARRON\Downloads\XVatsim.png'
$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$outputPath = Join-Path $projectRoot 'mockups\xvatsim-ui-mock-v1.png'

$outputDir = Split-Path -Parent $outputPath
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

$image = [System.Drawing.Image]::FromFile($sourcePath)
$bitmap = New-Object System.Drawing.Bitmap $image.Width, $image.Height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$graphics.DrawImage($image, 0, 0, $image.Width, $image.Height)

$panelRect = New-Object System.Drawing.RectangleF(382, 135, 626, 510)
$panelBg = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    (New-Object System.Drawing.PointF($panelRect.Left, $panelRect.Top)),
    (New-Object System.Drawing.PointF($panelRect.Left, $panelRect.Bottom)),
    ([System.Drawing.Color]::FromArgb(232, 34, 39, 46)),
    ([System.Drawing.Color]::FromArgb(224, 22, 25, 30))
)
$graphics.FillRectangle($panelBg, $panelRect)

$mistBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(18, 255, 255, 255))
$graphics.FillEllipse($mistBrush, 720, 458, 205, 82)
$graphics.FillEllipse($mistBrush, 808, 410, 145, 58)
$graphics.FillEllipse($mistBrush, 655, 516, 110, 44)

$linePen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(90, 143, 150, 158)), 1
$dividerPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(64, 143, 150, 158)), 1

$fontTitle = New-Object System.Drawing.Font('Segoe UI Semibold', 24, [System.Drawing.FontStyle]::Regular)
$fontBadge = New-Object System.Drawing.Font('Segoe UI Semibold', 12, [System.Drawing.FontStyle]::Regular)
$fontCallsign = New-Object System.Drawing.Font('Segoe UI Semibold', 26, [System.Drawing.FontStyle]::Regular)
$fontLine = New-Object System.Drawing.Font('Segoe UI Semibold', 22, [System.Drawing.FontStyle]::Regular)
$fontSmall = New-Object System.Drawing.Font('Segoe UI', 18, [System.Drawing.FontStyle]::Regular)

$whiteBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(235, 237, 242, 245))
$mutedBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(190, 198, 205, 212))
$greenBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(178, 243, 203))
$greenAccent = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(70, 190, 137))
$yellowBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(247, 208, 133))
$cyanBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(178, 238, 244))
$badgeFill = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(44, 91, 125, 124))
$badgeStroke = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(128, 148, 210, 208)), 1

$graphics.FillEllipse($greenAccent, 404, 163, 16, 16)
$graphics.DrawString('XVatsim', $fontTitle, $whiteBrush, 442, 145)

$badgeRect = New-Object System.Drawing.RectangleF(555, 146, 134, 34)
$badgePath = New-RoundedRectPath -Rect $badgeRect -Radius 8
$graphics.FillPath($badgeFill, $badgePath)
$graphics.DrawPath($badgeStroke, $badgePath)
$graphics.DrawString('CONNECTED', $fontBadge, $cyanBrush, 572, 154)
$graphics.DrawString('...', $fontTitle, $mutedBrush, 947, 142)

$graphics.DrawLine($linePen, 400, 198, 985, 198)
$graphics.DrawString('UAL2457', $fontCallsign, $cyanBrush, 442, 226)
$graphics.DrawString('CRZ FL360', $fontSmall, $mutedBrush, 866, 234)

$graphics.DrawLine($dividerPen, 400, 288, 985, 288)
$graphics.DrawString('39 ATC', $fontLine, $whiteBrush, 475, 307)

$graphics.DrawLine($dividerPen, 400, 362, 985, 362)
$graphics.DrawString('ZHU_CTR', $fontLine, $greenBrush, 447, 382)
$graphics.DrawString('132.775  *ACTIVE*', $fontSmall, $greenBrush, 760, 388)

$graphics.DrawLine($dividerPen, 400, 434, 985, 434)
$graphics.DrawString('MMUN_CTR', $fontLine, $yellowBrush, 447, 454)
$graphics.DrawString('126.900  *NEXT*', $fontSmall, $yellowBrush, 792, 460)

$graphics.DrawLine($dividerPen, 400, 506, 985, 506)
$graphics.DrawString('MRHO_ATIS', $fontLine, $whiteBrush, 447, 526)
$graphics.DrawString('127.650', $fontSmall, $mutedBrush, 862, 532)

$graphics.DrawLine($dividerPen, 400, 578, 985, 578)
$graphics.DrawString('KIAH -> MRHO', $fontLine, $cyanBrush, 447, 596)
$graphics.DrawString('954nm remaining', $fontSmall, $mutedBrush, 447, 632)

$bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)

$badgeStroke.Dispose()
$dividerPen.Dispose()
$linePen.Dispose()
$badgeFill.Dispose()
$whiteBrush.Dispose()
$mutedBrush.Dispose()
$greenBrush.Dispose()
$greenAccent.Dispose()
$yellowBrush.Dispose()
$cyanBrush.Dispose()
$mistBrush.Dispose()
$fontTitle.Dispose()
$fontBadge.Dispose()
$fontCallsign.Dispose()
$fontLine.Dispose()
$fontSmall.Dispose()
$badgePath.Dispose()
$panelBg.Dispose()
$graphics.Dispose()
$bitmap.Dispose()
$image.Dispose()

Write-Output $outputPath
