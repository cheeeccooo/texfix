# Installer di Texfix per Windows. Controlla che cosa c'è già (anche installato a mano, con MiKTeX invece di
# TeX Live), dice che cosa installerebbe e dove, e solo dopo la conferma installa ciò che manca, compila il
# programma e lancia le prove. Si può rilanciare quante volte si vuole.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$QtVersion = '6.12.0'
$QtArch = 'win64_msvc2022_64'
# aqtinstall al commit che conosce la disposizione dei file di Qt 6.11 e successivi su Windows (la 3.3.0 non la conosce)
$AqtSource = 'git+https://github.com/miurahr/aqtinstall.git@5a49f2b391ae8ee1d3c0dd87f8b33c1d49a4deb7'
$Tools = Join-Path $PSScriptRoot '.strumenti'

function Say($msg) { Write-Host "`n==> $msg" -ForegroundColor Yellow }
function Have($cmd) { [bool](Get-Command $cmd -ErrorAction SilentlyContinue) }
# Due giri sugli stessi passi: nel primo ($Plan) si descrivono, che cosa e dove; dopo la conferma si eseguono.
# Ciò che installa Texfix si annota in .installati ("tipo<TAB>argomenti"): uninstall.bat propone di togliere
# solo quello, mai ciò che c'era già.
$Plan = $true
$Steps = 0
$Manifest = Join-Path $PSScriptRoot '.installati'
function Record([string]$kind, [string]$args_) { Add-Content -Path $script:Manifest -Value "$kind`t$args_" -Encoding utf8 }
function Step([string]$what, [string]$where, [scriptblock]$action, [string]$record = '') {
    if ($script:Plan) {
        $script:Steps++
        Write-Host "  $($script:Steps). $what"
        Write-Host "     in: $where" -ForegroundColor DarkGray
    } else {
        Say $what
        $global:LASTEXITCODE = 0
        & $action
        if ($LASTEXITCODE) { Write-Host "Non riuscito: $what" -ForegroundColor Red; exit 1 }
        if ($record) { $kind, $rest = $record -split ' ', 2; Record $kind $rest }
    }
}

function Poppler-Bin {
    Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\oschwartz10612.Poppler_*\*\Library\bin" -Directory -ErrorAction SilentlyContinue |
        Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
}
function VS-Path {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
# Qt scaricato in .strumenti\qt, o uno indicato a mano con la variabile TEXFIX_QT
function Qt-Dir {
    if ($env:TEXFIX_QT -and (Test-Path "$env:TEXFIX_QT\lib\cmake\Qt6Pdf")) { return $env:TEXFIX_QT }
    Get-ChildItem "$Tools\qt\*\msvc*_64" -Directory -ErrorAction SilentlyContinue | Where-Object { Test-Path "$($_.FullName)\lib\cmake\Qt6Pdf" } |
        Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
}
function Qt-Ok { [bool](Qt-Dir) }
function Python-Ok {
    if (-not (Have python)) { return $false }
    $v = cmd /c "python --version 2>&1" | Out-String      # (quello finto di Windows apre lo Store e non dice la versione)
    return $v -match 'Python 3\.(\d+)' -and [int]$Matches[1] -ge 9
}
# Il PATH aggiornato, più gli strumenti installati ma fuori dal PATH di questa sessione
function Refresh-Path {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
    $extra = @("$env:USERPROFILE\.local\bin", "$env:LOCALAPPDATA\Microsoft\WinGet\Links", "$env:APPDATA\npm",
               "$env:ProgramFiles\MiKTeX\miktex\bin\x64", "$env:LOCALAPPDATA\Programs\MiKTeX\miktex\bin\x64",
               "$env:ProgramFiles\Git\cmd", "$env:ProgramFiles\Pandoc")
    $tl = Get-ChildItem 'C:\texlive\*\bin\windows', 'C:\texlive\*\bin\win64' -Directory -ErrorAction SilentlyContinue |
        Sort-Object FullName | Select-Object -Last 1
    if ($tl) { $extra += $tl.FullName }
    $py = Get-ChildItem "$env:LOCALAPPDATA\Programs\Python\Python3*" -Directory -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1
    if ($py) { $extra += $py.FullName }
    # La CMake che arriva con Visual Studio
    $vs = VS-Path
    if ($vs) { $extra += "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin" }
    foreach ($d in $extra) { if ((Test-Path $d) -and ($env:Path -notlike "*$d*")) { $env:Path = "$env:Path;$d" } }
    # Poppler davanti a TeX Live, che ha un suo pdftoppm più povero
    $pop = Poppler-Bin
    if ($pop) { $env:Path = "$pop;$env:Path" }
}

function Pdftoppm-Ok { (Have pdftoppm) -and (Have pdfunite) -and (Have pdfseparate) }
function Cmake-Ok {
    if (-not (Have cmake)) { return $false }
    $v = (cmake --version | Select-Object -First 1) -replace '[^0-9.]', ''
    try { return [version]($v.Split('.')[0..1] -join '.') -ge [version]'3.21' } catch { return $false }
}
function Cpp-Ok { [bool](VS-Path) }
# File LaTeX che usano Texfix e lo stile «styles», ciascuno con il pacchetto (TeX Live e MiKTeX) che lo contiene
$TexFiles = [ordered]@{ 'amsthm.sty' = 'amscls'; 'amssymb.sty' = 'amsfonts'; 'mathtools.sty' = 'mathtools'; 'cleveref.sty' = 'cleveref'
    'hyperref.sty' = 'hyperref'; 'italian.ldf' = 'babel-italian'; 'tikz.sty' = 'pgf'; 'pgfplots.sty' = 'pgfplots'
    'varwidth.sty' = 'varwidth'; 'preview.sty' = 'preview'; 'tcolorbox.sty' = 'tcolorbox'; 'environ.sty' = 'environ'
    'trimspaces.sty' = 'trimspaces'; 'tikzfill.image.sty' = 'tikzfill'; 'pdfcol.sty' = 'pdfcol'; 'enumitem.sty' = 'enumitem'
    'titlesec.sty' = 'titlesec'; 'fancyhdr.sty' = 'fancyhdr'; 'stackengine.sty' = 'stackengine'; 'listofitems.sty' = 'listofitems'
    'cancel.sty' = 'cancel'; 'contour.sty' = 'contour'; 'truncate.sty' = 'truncate'; 'microtype.sty' = 'microtype'
    'caption.sty' = 'caption'; 'float.sty' = 'float'; 'bm.sty' = 'tools'; 'raleway.sty' = 'raleway'; 'plex-serif.sty' = 'plex'
    'fontaxes.sty' = 'fontaxes'; 'mweights.sty' = 'mweights'; 'bookmark.sty' = 'bookmark'; 'geometry.sty' = 'geometry'
    'ecrm1000.tfm' = 'ec' }
function Tex-Missing {
    $out = @()
    foreach ($f in $TexFiles.Keys) {
        $found = if (Have kpsewhich) { cmd /c "kpsewhich $f 2>nul" } else { $null }
        if (-not $found) { $out += $TexFiles[$f] }
    }
    if (-not (Have latexmk)) { $out += 'latexmk' }
    if (-not (Have synctex)) { $out += 'synctex' }
    return $out
}
function Is-MiKTeX { (Have initexmf) -or ((Get-Command pdflatex -ErrorAction SilentlyContinue).Source -like '*MiKTeX*') }
function Tex-Root {
    $pdflatex = (Get-Command pdflatex -ErrorAction SilentlyContinue).Source
    if ($pdflatex) { return Split-Path (Split-Path (Split-Path $pdflatex)) }
    return 'C:\texlive\<anno>'
}
function Add-TeX-Packages([string[]]$pkgs) {
    if (-not $pkgs) { return }
    $what = "Pacchetti LaTeX che mancano: $($pkgs -join ' ')"
    if (Is-MiKTeX) {
        Step $what "$(Tex-Root) (con MiKTeX)" {
            foreach ($p in $pkgs) {
                if (Have miktex) { miktex packages install $p } else { mpm --install=$p }
            }
        } "miktex $($pkgs -join ' ')"
    } else {
        Step $what "$(Tex-Root)\texmf-dist (con tlmgr)" { tlmgr install @pkgs } "tlmgr $($pkgs -join ' ')"
    }
}

Refresh-Path

Say "Controllo che cosa c'è già"
function Status($label, $ok) {
    if ($ok) { Write-Host "  c'è     $label" -ForegroundColor Green } else { Write-Host "  manca   $label" -ForegroundColor Red }
}
function Show-Status {
    Status 'Git' (Have git)
    Status 'Compilatore C++ (Visual Studio)' (Cpp-Ok)
    Status 'CMake 3.21+' (Cmake-Ok)
    Status "Qt $QtVersion con QtPdf e Qt Quick" (Qt-Ok)
    Status 'TeX (pdflatex: TeX Live o MiKTeX)' (Have pdflatex)
    Status 'latexmk, synctex e pacchetti LaTeX (amsthm, tcolorbox, TikZ, ...)' ((Have pdflatex) -and -not (Tex-Missing))
    Status 'Poppler (pdftoppm, pdfunite, pdfseparate)' (Pdftoppm-Ok)
    Status 'pandoc (legge i .docx, .odt, .rtf)' (Have pandoc)
    Status 'Claude Code' (Have claude)
}
Show-Status

# winget serve solo se c'è qualcosa da installare con lui
$NeedsWinget = $false
function Winget-Install([string]$what, [string]$where, [string]$id, [string[]]$extra = @()) {
    $script:NeedsWinget = $true
    Step $what $where { winget install --id $id -e --silent --accept-source-agreements --accept-package-agreements @extra } "winget $id"
}

function Install-All {
    if (-not (Have git)) { Winget-Install 'Git' "$env:ProgramFiles\Git" 'Git.Git' }
    $vsComing = -not (Cpp-Ok)
    if ($vsComing) {
        Winget-Install 'Visual Studio Build Tools con il compilatore C++ e CMake (alcuni GB)' `
            "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools" 'Microsoft.VisualStudio.2022.BuildTools' @('--override',
            '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
        if (-not $script:Plan) { Refresh-Path } # porta anche la sua CMake
    }
    # Visual Studio porta con sé CMake: a parte serve solo se manca anche dopo
    if (-not (Cmake-Ok) -and -not ($script:Plan -and $vsComing)) { Winget-Install 'CMake' "$env:ProgramFiles\CMake" 'Kitware.CMake' }
    if (-not $script:Plan) { Refresh-Path }

    # Qt non è in winget: lo scarica aqtinstall (Python) dal server di Qt, tutto dentro .strumenti
    if (-not (Qt-Ok)) {
        if (-not (Python-Ok)) {
            Winget-Install 'Python 3 (serve solo per scaricare Qt)' "$env:LOCALAPPDATA\Programs\Python" 'Python.Python.3.12'
            if (-not $script:Plan) { Refresh-Path }
        }
        Step "Qt $QtVersion con QtPdf (circa 1,5 GB), scaricato con aqtinstall" "$Tools\qt" {
            python -m venv "$Tools\aqt"
            & "$Tools\aqt\Scripts\pip.exe" install --quiet $AqtSource
            if ($LASTEXITCODE) { return }
            & "$Tools\aqt\Scripts\aqt.exe" install-qt windows desktop $QtVersion $QtArch -m qtpdf -O "$Tools\qt"
        } "cartella $Tools"
    }

    if (-not (Have pdflatex)) {
        Step 'TeX Live, schema di base (poi solo i pacchetti che servono)' 'C:\texlive\<anno>' {
            $tmp = Join-Path $env:TEMP 'texfix-texlive'
            New-Item -ItemType Directory -Force $tmp | Out-Null
            Invoke-WebRequest 'https://mirror.ctan.org/systems/texlive/tlnet/install-tl.zip' -OutFile "$tmp\install-tl.zip"
            Expand-Archive "$tmp\install-tl.zip" -DestinationPath $tmp -Force
            $dir = Get-ChildItem $tmp -Directory -Filter 'install-tl-*' | Select-Object -First 1
            & "$($dir.FullName)\install-tl-windows.bat" -no-gui -no-interaction -scheme scheme-basic
            Refresh-Path
            Record 'texlive' (Tex-Root)
        }
    }
    # latexmk ha bisogno di Perl: TeX Live lo porta con sé, MiKTeX no
    Add-TeX-Packages @(Tex-Missing)

    if (-not (Pdftoppm-Ok)) {
        if (-not (Poppler-Bin)) {
            Winget-Install 'Poppler (pagine dei PDF in immagini, PDF uniti e divisi)' "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" 'oschwartz10612.Poppler'
        }
        Step "Poppler nel PATH, prima di TeX Live" 'variabile PATH del tuo utente' {
            $pop = Poppler-Bin
            if ($pop) {
                $parts = [Environment]::GetEnvironmentVariable('Path', 'User') -split ';' |
                    Where-Object { $_ -and ($_.TrimEnd('\') -ne $pop.TrimEnd('\')) }
                [Environment]::SetEnvironmentVariable('Path', ((@($pop) + $parts) -join ';'), 'User')
                Record 'path' $pop
            }
        }
        if (-not $script:Plan) { Refresh-Path }
    }
    if (-not (Have pandoc)) { Winget-Install 'pandoc (legge i .docx, .odt, .rtf)' "$env:ProgramFiles\Pandoc" 'JohnMacFarlane.Pandoc' }

    if (-not (Have claude)) {
        Winget-Install 'Claude Code' "$env:LOCALAPPDATA\Microsoft\WinGet\Packages, comando in ...\WinGet\Links" 'Anthropic.ClaudeCode'
        if (-not $script:Plan) { Refresh-Path }
    }
}

Say 'Che cosa farei'
Install-All
if ($Steps -eq 0) { Write-Host '  Non manca niente: non installo nulla.' }
Write-Host "  $($Steps + 1). Compilo Texfix e lancio le prove del motore (qualche minuto)"
Write-Host "     in: $PSScriptRoot\build" -ForegroundColor DarkGray
$noWinget = $NeedsWinget -and -not (Have winget)
if ($noWinget) { Write-Host "  ! Per installare serve winget: installa 'App Installer' dal Microsoft Store." -ForegroundColor Red }

Write-Host "`nProcedo? [s/N] " -NoNewline
$answer = if ([Console]::IsInputRedirected) { [Console]::In.ReadLine() } else { Read-Host }
if ($answer -notmatch '^(s|si|sì|y|yes)$') {
    Write-Host "Non ho toccato niente."
    exit 0
}
if ($noWinget) {
    Write-Host "Senza winget non posso installare ciò che manca: installa 'App Installer' dal Microsoft Store, poi rilancia install.bat" -ForegroundColor Red
    exit 1
}

$Plan = $false
Install-All

Say 'Compilo Texfix'
$qt = Qt-Dir
if (-not $qt) { Write-Host "Qt non c'è: non posso compilare." -ForegroundColor Red; exit 1 }
$env:Path = "$qt\bin;$env:Path"
cmake -S . -B build "-DCMAKE_PREFIX_PATH=$qt"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build build --config Release --parallel 4
if ($LASTEXITCODE) { exit $LASTEXITCODE }
# Le librerie di Qt accanto al programma: così si apre anche con un doppio clic
& "$qt\bin\windeployqt.exe" --release --qmldir qml --no-translations build\Release\Texfix.exe | Out-Null

Say 'Le prove del motore'
& build\Release\texfix_tests.exe -silent
if ($LASTEXITCODE) { Write-Host "Qualche prova non è passata: il programma si apre lo stesso, ma dimmelo." -ForegroundColor Red }

Say 'Riepilogo'
Show-Status
Write-Host "`nPer aprire Texfix: doppio clic su avvia.bat"
Write-Host "Se non l'hai mai fatto, apri un terminale e lancia 'claude' una volta per fare il login."
