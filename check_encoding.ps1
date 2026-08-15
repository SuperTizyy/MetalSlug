$path = 'f:\ZyyDocument\UE\MetalSlugGet\MetalSlug\MetalSlug01\Source\MetalSlug01\Public\UI\Activity\Pages\DailyUpgradeReward\DailyUpgradeRewardPage.h'
$utf8Bom = New-Object System.Text.UTF8Encoding($true)
$bytes = [System.IO.File]::ReadAllBytes($path)
$text = $utf8Bom.GetString($bytes)
$lines = $text -split "`r`n"

# Show hex of line 322 (the bIsFixedPrizeWidgetEventBound line)
$line322 = $lines[321]
Write-Host ('L322 raw: [' + $line322 + ']')
$enc = [System.Text.Encoding]::UTF8
$lineBytes = $enc.GetBytes($line322)
Write-Host 'L322 bytes (hex):'
for ($i = 0; $i -lt $lineBytes.Length; $i++) {
    Write-Host ('  [' + $i + '] 0x' + $lineBytes[$i].ToString('X2'))
}

# Show hex of line 321 (the comment)
$line321 = $lines[320]
Write-Host ('L321 raw: [' + $line321 + ']')
$lineBytes321 = $enc.GetBytes($line321)
Write-Host 'L321 bytes (hex):'
for ($i = 0; $i -lt $lineBytes321.Length; $i++) {
    Write-Host ('  [' + $i + '] 0x' + $lineBytes321[$i].ToString('X2'))
}

# Find byte offset of 'bIsFixedPrizeWidgetEventBound' marker
$marker = 'bIsFixedPrizeWidgetEventBound = false;'
$idx = $text.IndexOf($marker)
Write-Host ('Marker found at char index: ' + $idx)

# Find line range we need to delete: L321 comment + L322 field line + L323 blank
$startLineIdx = 320  # 0-based: L321
$endLineIdx = 322    # 0-based: L323 (inclusive)
Write-Host ('Will delete lines: ' + ($startLineIdx+1) + ' to ' + ($endLineIdx+1))
