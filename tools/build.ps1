param(
    [string]$Preset = "Debug",
    [string]$Target = "LCD_Test_Appli",
    [int]$Jobs = 4
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")
Set-Location $repoRoot

function Ensure-TouchGfxWildcardCharacters {
    param(
        [string]$RepoRootPath
    )

    $touchgfxRoot = Join-Path $RepoRootPath "Appli\TouchGFX"
    $textsXml = Join-Path $touchgfxRoot "assets\texts\texts.xml"
    if (-not (Test-Path $textsXml)) {
        return
    }

    $xmlRaw = [System.IO.File]::ReadAllText($textsXml)
        $pattern = '(<Typography\s+Id="Default"[^>]*WidgetWildcardCharacters=")([^"]*)(")'
        $missingPattern = '(<Typography\s+Id="Default"(?![^>]*WidgetWildcardCharacters=)[^>]*?)(\s*/>)'
    $expected = '0123456789.,-'
    $script:changed = $false

    $updated = [System.Text.RegularExpressions.Regex]::Replace(
        $xmlRaw,
        $pattern,
        {
            param($m)
            if ($m.Groups[2].Value -ne $expected) {
                $script:changed = $true
                return ($m.Groups[1].Value + $expected + $m.Groups[3].Value)
            }
            return $m.Value
        }
    )

    if (-not $script:changed) {
        $updated = [System.Text.RegularExpressions.Regex]::Replace(
            $updated,
            $missingPattern,
            {
                param($m)
                $script:changed = $true
                return ($m.Groups[1].Value + ' WidgetWildcardCharacters="' + $expected + '"' + $m.Groups[2].Value)
            }
        )
    }

    $needRegen = $script:changed

    if ($script:changed) {
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($textsXml, $updated, $utf8NoBom)
        Write-Host "[touchgfx] fixed Default.WidgetWildcardCharacters -> $expected"
    }

    if (-not $needRegen) {
        $tablePath = Join-Path $touchgfxRoot "generated\fonts\src\Table_verdana_20_4bpp.cpp"
        $requiredGlyphs = @(
            "0x002C", "0x002D", "0x0030", "0x0031", "0x0032",
            "0x0033", "0x0034", "0x0035", "0x0036", "0x0037",
            "0x0038", "0x0039"
        )

        if (-not (Test-Path $tablePath)) {
            $needRegen = $true
            Write-Host "[touchgfx] glyph table missing, forcing assets regeneration"
        }
        else {
            $tableRaw = [System.IO.File]::ReadAllText($tablePath)
            foreach ($glyph in $requiredGlyphs) {
                if ($tableRaw.IndexOf($glyph, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
                    $needRegen = $true
                    Write-Host "[touchgfx] missing glyph $glyph in generated table, forcing assets regeneration"
                    break
                }
            }
        }
    }

    if (-not $needRegen) {
        return
    }

    $makeExe = "D:\TouchGFX\4.26.0\env\MinGW\bin\mingw32-make.exe"
    $rubyDir = "D:\TouchGFX\4.26.0\env\MinGW\msys\1.0\Ruby30-x64\bin"
    $makeDir = "D:\TouchGFX\4.26.0\env\MinGW\bin"
    if (-not (Test-Path $makeExe)) {
        throw "TouchGFX make tool not found: $makeExe"
    }
    if (-not (Test-Path $rubyDir)) {
        throw "TouchGFX Ruby not found: $rubyDir"
    }

    Write-Host "[touchgfx] regenerating assets (fonts/texts)..."
    Push-Location $touchgfxRoot
    try {
        $env:PATH = "$rubyDir;$makeDir;$env:PATH"
        & $makeExe -f simulator/gcc/Makefile assets -j8
        if ($LASTEXITCODE -ne 0) {
            throw "TouchGFX asset generation failed."
        }
    }
    finally {
        Pop-Location
    }
}

function Assert-TouchGfxOverlayBindings {
    param(
        [string]$RepoRootPath
    )

    $touchgfxRoot = Join-Path $RepoRootPath "Appli\TouchGFX"
    $viewRoot = Join-Path $touchgfxRoot "gui\src"
    if (-not (Test-Path $viewRoot)) {
        return
    }

    $viewCppFiles = Get-ChildItem $viewRoot -Recurse -File -Filter "*View.cpp"
    $issues = New-Object System.Collections.Generic.List[string]
    $overlayPattern = 'configureNumericOverlay\s*\(\s*[^,]+,\s*([A-Za-z_][A-Za-z0-9_]*)\s*,'

    foreach ($viewCpp in $viewCppFiles) {
        $viewRaw = [System.IO.File]::ReadAllText($viewCpp.FullName)
        $matches = [System.Text.RegularExpressions.Regex]::Matches($viewRaw, $overlayPattern)
        if ($matches.Count -eq 0) {
            continue
        }

        $screenFolder = Split-Path -Leaf (Split-Path -Parent $viewCpp.FullName)
        $viewName = [System.IO.Path]::GetFileNameWithoutExtension($viewCpp.Name)
        $baseHeader = Join-Path $touchgfxRoot ("generated\gui_generated\include\gui_generated\{0}\{1}Base.hpp" -f $screenFolder, $viewName)

        if (-not (Test-Path $baseHeader)) {
            $issues.Add(("{0}: generated base header not found ({1})" -f $viewCpp.FullName, $baseHeader))
            continue
        }

        $headerRaw = [System.IO.File]::ReadAllText($baseHeader)
        $widgetNames = New-Object System.Collections.Generic.HashSet[string]
        foreach ($match in $matches) {
            $null = $widgetNames.Add($match.Groups[1].Value)
        }

        foreach ($widgetName in $widgetNames) {
            $memberPattern = ('\b{0}\b\s*;' -f [System.Text.RegularExpressions.Regex]::Escape($widgetName))
            if (-not [System.Text.RegularExpressions.Regex]::IsMatch($headerRaw, $memberPattern)) {
                $issues.Add(("{0}: missing widget '{1}' in {2}" -f $viewCpp.FullName, $widgetName, $baseHeader))
            }
        }
    }

    if ($issues.Count -gt 0) {
        Write-Host "[touchgfx] stale widget binding(s) detected:" -ForegroundColor Yellow
        foreach ($issue in $issues) {
            Write-Host ("  - {0}" -f $issue) -ForegroundColor Yellow
        }
        throw "TouchGFX widget binding check failed. Update custom View code or Designer widget names before build."
    }
}

Ensure-TouchGfxWildcardCharacters -RepoRootPath $repoRoot
Assert-TouchGfxOverlayBindings -RepoRootPath $repoRoot

$toolBin = "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin"
if (Test-Path $toolBin) {
    $env:PATH = "$toolBin;$env:PATH"
}

$buildDir = Join-Path $repoRoot "build\$Preset"
Write-Host "[build] configure preset: $Preset"
cmake --preset $Preset
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

Write-Host "[build] target: $Target (jobs=$Jobs)"
if ($Jobs -gt 0) {
    cmake --build $buildDir --target $Target --parallel $Jobs
} else {
    cmake --build $buildDir --target $Target
}
if ($LASTEXITCODE -ne 0) {
    throw "Build failed."
}

$targetSubdir = if ($Target -like "*Boot*") { "Boot" } else { "Appli" }
$binPath = Join-Path $buildDir (Join-Path $targetSubdir "$Target.bin")
if (Test-Path $binPath) {
    $bin = Get-Item $binPath
    Write-Host "[build] bin: $($bin.FullName) ($([Math]::Round($bin.Length / 1MB, 2)) MB)"
} else {
    Write-Host "[build] warning: bin not found at $binPath"
}

Write-Host "[build] done"
