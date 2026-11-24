<#
.SYNOPSIS
    Beschafft das DiagnoseTool fuer das WinPE-Image.

.DESCRIPTION
    Standard: laedt die fertige, self-contained Windows-x64-Exe aus dem
    neuesten GitHub-Release von kruemmel2704/DiagnoseTool herunter.

    Mit -FromSource wird stattdessen aus dem (lokal ausgecheckten oder frisch
    geklonten) DiagnoseTool-Quellcode gebaut - und zwar als ORDNER-Publish
    (kein Single-File). Das ist fuer WinPE robuster, weil die .NET-Laufzeit
    dann nicht erst in den begrenzten RAM-Scratch (X:) entpackt werden muss.

    Zusaetzlich wird FurMark (Resources/FurMark.zip) aus dem DiagnoseTool-Repo
    geholt und vor-entpackt, damit die App es im PE nicht erst auspacken muss.

.OUTPUTS
    Legt das Ergebnis unter .\app\DiagnoseTool\ ab:
      DiagnoseTool.exe            (+ ggf. begleitende DLLs bei -FromSource)
      FurMark\...                 (vor-entpacktes FurMark)
#>
[CmdletBinding()]
param(
    [string]$Version = 'latest',
    [switch]$FromSource,
    [string]$SourcePath,                           # Pfad zum DiagnoseTool-Repo (fuer -FromSource)
    [string]$Runtime = 'win-x64',
    [string]$OutDir                                # Zielordner (Default: <repo>\app\DiagnoseTool)
)

. "$PSScriptRoot\common.ps1"

$repo    = Get-RepoRoot
if (-not $OutDir) { $OutDir = Join-Path $repo 'app\DiagnoseTool' }
$dlDir   = Join-Path $repo 'work\download'
$ghOwner = 'kruemmel2704'
$ghRepo  = 'DiagnoseTool'

if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# ---------------------------------------------------------------------
#  FurMark aus dem DiagnoseTool-Repo holen und vor-entpacken
# ---------------------------------------------------------------------
function Get-FurMark {
    param([string]$Dest)
    Write-Step "FurMark beschaffen (vor-entpacken)"
    $furZip = Join-Path $dlDir 'FurMark.zip'
    $rawUrl = "https://raw.githubusercontent.com/$ghOwner/$ghRepo/main/Resources/FurMark.zip"
    if (-not (Test-Path $furZip)) {
        if (-not (Invoke-Download -Url $rawUrl -OutFile $furZip)) {
            Write-Warn2 "FurMark.zip konnte nicht geladen werden - App packt es dann zur Laufzeit selbst aus."
            return
        }
    }
    $furDir = Join-Path $Dest 'FurMark'
    New-Item -ItemType Directory -Force -Path $furDir | Out-Null
    Expand-Archive -Path $furZip -DestinationPath $furDir -Force
    Write-Ok "FurMark entpackt nach $furDir"
}

# ---------------------------------------------------------------------
#  Variante A: Release-Exe herunterladen
# ---------------------------------------------------------------------
function Get-ReleaseExe {
    Write-Step "DiagnoseTool-Release beschaffen ($Version)"
    $api = if ($Version -eq 'latest') {
        "https://api.github.com/repos/$ghOwner/$ghRepo/releases/latest"
    } else {
        "https://api.github.com/repos/$ghOwner/$ghRepo/releases/tags/$Version"
    }
    $headers = @{ 'User-Agent' = 'DiagnosePE-Builder'; 'Accept' = 'application/vnd.github+json' }
    $rel = Invoke-RestMethod -Uri $api -Headers $headers
    Write-Info "Release: $($rel.tag_name)"

    # bevorzugt das x64-Windows-Asset
    $asset = $rel.assets | Where-Object { $_.name -match '(?i)windows.*x64.*\.exe$' } | Select-Object -First 1
    if (-not $asset) { $asset = $rel.assets | Where-Object { $_.name -match '(?i)\.exe$' } | Select-Object -First 1 }
    if (-not $asset) { Fail "Kein .exe-Asset im Release $($rel.tag_name) gefunden." }

    $exe = Join-Path $OutDir 'DiagnoseTool.exe'
    if (-not (Invoke-Download -Url $asset.browser_download_url -OutFile $exe)) {
        Fail "Download des DiagnoseTool-Release-Assets fehlgeschlagen."
    }
    Write-Ok "DiagnoseTool.exe bereit: $exe"
}

# ---------------------------------------------------------------------
#  Variante B: Aus Quellcode bauen (Ordner-Publish, PE-robust)
# ---------------------------------------------------------------------
function Build-FromSource {
    Write-Step "DiagnoseTool aus Quellcode bauen (Ordner-Publish)"
    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    if (-not $dotnet) { Fail "'dotnet' nicht gefunden. .NET SDK installieren oder ohne -FromSource (Release-Exe) bauen." }

    $src = $SourcePath
    if (-not $src) {
        # neben dem DiagnosePE-Repo?
        $guess = Join-Path (Split-Path -Parent $repo) 'DiagnoseTool'
        if (Test-Path (Join-Path $guess 'DiagnoseTool.csproj')) { $src = $guess }
    }
    if (-not $src) {
        $src = Join-Path $repo 'work\DiagnoseTool-src'
        if (-not (Test-Path (Join-Path $src 'DiagnoseTool.csproj'))) {
            Write-Info "Klone DiagnoseTool-Quellcode..."
            git clone --depth 1 "https://github.com/$ghOwner/$ghRepo.git" $src
        }
    }
    $csproj = Join-Path $src 'DiagnoseTool.csproj'
    if (-not (Test-Path $csproj)) { Fail "DiagnoseTool.csproj nicht gefunden unter $src." }

    # WICHTIG: PublishSingleFile=false -> Ordner mit allen Dateien (kein RAM-Self-Extract in PE)
    & dotnet publish $csproj -c Release -r $Runtime --self-contained true `
        -p:PublishSingleFile=false -p:EnableCompressionInSingleFile=false `
        -o $OutDir
    if ($LASTEXITCODE -ne 0) { Fail "dotnet publish fehlgeschlagen (ExitCode $LASTEXITCODE)." }
    if (-not (Test-Path (Join-Path $OutDir 'DiagnoseTool.exe'))) { Fail "DiagnoseTool.exe nach Publish nicht vorhanden." }
    Write-Ok "Aus Quellcode gebaut nach $OutDir"
}

# ---------------------------------------------------------------------
if ($FromSource) { Build-FromSource } else { Get-ReleaseExe }
Get-FurMark -Dest $OutDir

Write-Host ""
Write-Ok "DiagnoseTool bereit unter: $OutDir"
