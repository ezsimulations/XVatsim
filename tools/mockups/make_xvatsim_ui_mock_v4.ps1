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

function Draw-Case {
    param(
        [System.Drawing.Graphics]$Graphics,
        [float]$X,
        [float]$Y,
        [float]$Width = 170,
        [float]$Height = 18
    )

    $caseRect = New-Object System.Drawing.RectangleF -ArgumentList ([float]$X), ([float]$Y), ([float]$Width), ([float]$Height)
    $casePath = New-RoundedRectPath -Rect $caseRect -Radius 9
    $caseBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF($caseRect.Left, $caseRect.Top)),
        (New-Object System.Drawing.PointF($caseRect.Left, $caseRect.Bottom)),
        ([System.Drawing.Color]::FromArgb(210, 66, 78, 90)),
        ([System.Drawing.Color]::FromArgb(190, 28, 36, 45))
    )
    $caseBorder = New-Object System.Drawing.Pen -ArgumentList ([System.Drawing.Color]::FromArgb(90, 174, 196, 210)), 1
    $slotBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(150, 10, 14, 20))

    $Graphics.FillPath($caseBrush, $casePath)
    $Graphics.DrawPath($caseBorder, $casePath)
    $Graphics.FillRectangle($slotBrush, $X + 36, $Y + 7, $Width - 72, 3)

    $slotBrush.Dispose()
    $caseBorder.Dispose()
    $caseBrush.Dispose()
    $casePath.Dispose()
}

function Draw-Card {
    param(
        [System.Drawing.Graphics]$Graphics,
        [float]$X,
        [float]$Y,
        [float]$Width = 388,
        [float]$Height = 252
    )

    $shadowRect = New-Object System.Drawing.RectangleF -ArgumentList ([float]($X + 12)), ([float]($Y + 12)), ([float]$Width), ([float]$Height)
    $shadowPath = New-RoundedRectPath -Rect $shadowRect -Radius 18
    $shadowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(42, 0, 0, 0))
    $Graphics.FillPath($shadowBrush, $shadowPath)

    $panelRect = New-Object System.Drawing.RectangleF -ArgumentList ([float]$X), ([float]$Y), ([float]$Width), ([float]$Height)
    $panelPath = New-RoundedRectPath -Rect $panelRect -Radius 18
    $panelBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF($panelRect.Left, $panelRect.Top)),
        (New-Object System.Drawing.PointF($panelRect.Right, $panelRect.Bottom)),
        ([System.Drawing.Color]::FromArgb(178, 26, 36, 48)),
        ([System.Drawing.Color]::FromArgb(150, 10, 15, 23))
    )
    $panelBorder = New-Object System.Drawing.Pen -ArgumentList ([System.Drawing.Color]::FromArgb(82, 170, 194, 210)), 1
    $headerGlow = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF($panelRect.Left, $panelRect.Top)),
        (New-Object System.Drawing.PointF($panelRect.Left, ($panelRect.Top + 34))),
        ([System.Drawing.Color]::FromArgb(40, 187, 223, 242)),
        ([System.Drawing.Color]::FromArgb(0, 187, 223, 242))
    )
    $dividerPen = New-Object System.Drawing.Pen -ArgumentList ([System.Drawing.Color]::FromArgb(48, 172, 190, 205)), 1

    $whiteBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(238, 241, 246, 250))
    $mutedBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(176, 186, 196, 206))
    $cyanBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(186, 222, 244))
    $greenBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(170, 242, 205))
    $yellowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(247, 212, 130))
    $badgeFill = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(42, 75, 116, 118))
    $greenDot = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(74, 199, 139))
    $cyanDot = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(108, 164, 234, 244))
    $yellowDot = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(118, 247, 212, 130))

    $fontBrand = New-Object System.Drawing.Font('Segoe UI Semibold', 17, [System.Drawing.FontStyle]::Regular)
    $fontBadge = New-Object System.Drawing.Font('Segoe UI Semibold', 8.5, [System.Drawing.FontStyle]::Regular)
    $fontMeta = New-Object System.Drawing.Font('Segoe UI', 10.5, [System.Drawing.FontStyle]::Regular)
    $fontCallsign = New-Object System.Drawing.Font('Segoe UI Semibold', 17, [System.Drawing.FontStyle]::Regular)
    $fontCount = New-Object System.Drawing.Font('Segoe UI Semibold', 15, [System.Drawing.FontStyle]::Regular)
    $fontRow = New-Object System.Drawing.Font('Segoe UI Semibold', 15.5, [System.Drawing.FontStyle]::Regular)
    $fontRowRight = New-Object System.Drawing.Font('Segoe UI', 12.5, [System.Drawing.FontStyle]::Regular)
    $fontFooter = New-Object System.Drawing.Font('Segoe UI', 10.5, [System.Drawing.FontStyle]::Regular)

    $Graphics.FillPath($panelBrush, $panelPath)
    $Graphics.DrawPath($panelBorder, $panelPath)
    $Graphics.FillRectangle($headerGlow, $X + 1, $Y + 1, $Width - 2, 40)

    $Graphics.FillEllipse($greenDot, $X + 18, $Y + 19, 11, 11)
    $Graphics.DrawString('XVatsim', $fontBrand, $whiteBrush, $X + 46, $Y + 9)

    $badgeRect = New-Object System.Drawing.RectangleF -ArgumentList ([float]($X + 136)), ([float]($Y + 12)), 106.0, 23.0
    $badgePath = New-RoundedRectPath -Rect $badgeRect -Radius 7
    $Graphics.FillPath($badgeFill, $badgePath)
    $Graphics.DrawString('CONNECTED', $fontBadge, $cyanBrush, $X + 154, $Y + 18)
    $Graphics.DrawString('CRZ', $fontMeta, $mutedBrush, $X + 327, $Y + 13)

    $Graphics.DrawString('UAL2457', $fontCallsign, $cyanBrush, $X + 20, $Y + 48)
    $Graphics.DrawString('FL360', $fontMeta, $mutedBrush, $X + 318, $Y + 51)
    $Graphics.DrawLine($dividerPen, $X + 20, $Y + 81, $X + $Width - 20, $Y + 81)

    $Graphics.FillEllipse($cyanDot, $X + 20, $Y + 98, 6, 6)
    $Graphics.DrawString('39 ATC', $fontCount, $whiteBrush, $X + 46, $Y + 88)
    $Graphics.DrawLine($dividerPen, $X + 20, $Y + 126, $X + $Width - 20, $Y + 126)

    $Graphics.DrawString('ZHU_CTR', $fontRow, $greenBrush, $X + 46, $Y + 141)
    $Graphics.DrawString('132.775  ACTIVE', $fontRowRight, $greenBrush, $X + 222, $Y + 145)
    $Graphics.DrawLine($dividerPen, $X + 20, $Y + 174, $X + $Width - 20, $Y + 174)

    $Graphics.DrawString('MMUN_CTR', $fontRow, $yellowBrush, $X + 46, $Y + 189)
    $Graphics.DrawString('126.900  NEXT', $fontRowRight, $yellowBrush, $X + 236, $Y + 193)
    $Graphics.DrawLine($dividerPen, $X + 20, $Y + 221, $X + $Width - 20, $Y + 221)

    $Graphics.FillEllipse($yellowDot, $X + 20, $Y + 237, 6, 6)
    $Graphics.DrawString('KIAH -> MRHO', $fontRow, $cyanBrush, $X + 46, $Y + 230)
    $Graphics.DrawString('954nm remaining', $fontFooter, $mutedBrush, $X + 46, $Y + 256)

    $fontBrand.Dispose()
    $fontBadge.Dispose()
    $fontMeta.Dispose()
    $fontCallsign.Dispose()
    $fontCount.Dispose()
    $fontRow.Dispose()
    $fontRowRight.Dispose()
    $fontFooter.Dispose()
    $whiteBrush.Dispose()
    $mutedBrush.Dispose()
    $cyanBrush.Dispose()
    $greenBrush.Dispose()
    $yellowBrush.Dispose()
    $badgeFill.Dispose()
    $greenDot.Dispose()
    $cyanDot.Dispose()
    $yellowDot.Dispose()
    $dividerPen.Dispose()
    $headerGlow.Dispose()
    $badgePath.Dispose()
    $panelBrush.Dispose()
    $panelBorder.Dispose()
    $panelPath.Dispose()
    $shadowBrush.Dispose()
    $shadowPath.Dispose()
}

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$outputDir = Join-Path $projectRoot 'mockups'
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

# Open state: transparent canvas with compact case + pull-down card
$openPath = Join-Path $outputDir 'xvatsim-ui-mock-v4-open.png'
$openBitmap = New-Object System.Drawing.Bitmap 470, 320
$openGraphics = [System.Drawing.Graphics]::FromImage($openBitmap)
$openGraphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$openGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$openGraphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$openGraphics.Clear([System.Drawing.Color]::Transparent)
Draw-Case -Graphics $openGraphics -X 146 -Y 6
$tetherBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(120, 104, 122, 136))
$openGraphics.FillRectangle($tetherBrush, 225, 22, 12, 22)
Draw-Card -Graphics $openGraphics -X 18 -Y 28
$openBitmap.Save($openPath, [System.Drawing.Imaging.ImageFormat]::Png)
$tetherBrush.Dispose()
$openGraphics.Dispose()
$openBitmap.Dispose()

# Sleep state: case only, ready to unroll
$sleepPath = Join-Path $outputDir 'xvatsim-ui-mock-v4-sleep.png'
$sleepBitmap = New-Object System.Drawing.Bitmap 240, 62
$sleepGraphics = [System.Drawing.Graphics]::FromImage($sleepBitmap)
$sleepGraphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$sleepGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$sleepGraphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$sleepGraphics.Clear([System.Drawing.Color]::Transparent)
Draw-Case -Graphics $sleepGraphics -X 34 -Y 10 -Width 170 -Height 18
$sleepSlotBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(110, 118, 136, 150))
$sleepGraphics.FillRectangle($sleepSlotBrush, 111, 28, 16, 3)
$sleepBitmap.Save($sleepPath, [System.Drawing.Imaging.ImageFormat]::Png)
$sleepSlotBrush.Dispose()
$sleepGraphics.Dispose()
$sleepBitmap.Dispose()

Write-Output $openPath
Write-Output $sleepPath
