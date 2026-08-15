$path = 'f:\ZyyDocument\UE\MetalSlugGet\MetalSlug\MetalSlug01\Source\MetalSlug01\Public\UI\Activity\Pages\DailyUpgradeReward\DailyUpgradeRewardPage.h'
$utf8Bom = New-Object System.Text.UTF8Encoding($true)
$bytes = [System.IO.File]::ReadAllBytes($path)
$text = $utf8Bom.GetString($bytes)
$lines = $text -split "`r`n"

Write-Host 'Before delete - lines 315-327:'
for ($i = 315; $i -lt 327; $i++) {
    if ($i -lt $lines.Length) {
        Write-Host ('L' + ($i+1) + ': [' + $lines[$i] + ']')
    }
}

# After StrReplace, L322 (field) is gone, but L321 (comment) and L323 (blank) remain
# Target: L321 (comment) and L323 (blank) → keep just one blank between L319 and L324
# Current state expected:
# L318: /** 缓存的物品图标数据... */
# L319: 	TArray<TSoftObjectPtr<UTexture2D>> CachedItemIcons;
# L320: (empty)
# L321: 	/** 【Ensure 修复】FixedPrizeWidget ... */
# L322: (empty, was field line, now removed)
# L323: (empty, was blank line)
# L324: 	// ==========================================
#
# We want to keep only L320 as the single blank line between L319 and L324.
# Delete L321 and L322.

$linesToKeep = @()
for ($i = 0; $i -lt $lines.Length; $i++) {
    $lineNum = $i + 1  # 1-based

    # Skip the two lines we want to delete
    if ($lineNum -eq 321) {
        Write-Host ('SKIP line 321 (comment)')
        continue
    }
    if ($lineNum -eq 322 -and $lines[$i] -eq '') {
        Write-Host ('SKIP line 322 (extra blank)')
        continue
    }

    $linesToKeep += $lines[$i]
}

$newText = $linesToKeep -join "`r`n"
$newBytes = $utf8Bom.GetBytes($newText)
[System.IO.File]::WriteAllBytes($path, $newBytes)

Write-Host '----'
Write-Host 'After delete - lines 315-327:'
$newLines = $newText -split "`r`n"
for ($i = 315; $i -lt 327; $i++) {
    if ($i -lt $newLines.Length) {
        Write-Host ('L' + ($i+1) + ': [' + $newLines[$i] + ']')
    }
}
