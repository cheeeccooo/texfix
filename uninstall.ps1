# Disinstalla Texfix (Windows). Chiede pezzo per pezzo che cosa togliere e che cosa tenere, riepiloga e solo dopo
# la conferma esegue. Dei programmi esterni propone di togliere solo ciò che ha installato Texfix (annotato
# da install.ps1 in .installati), mai ciò che c'era già. I tuoi progetti non li tocca mai.
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
Set-Location $Root

function Say($msg) { Write-Host "`n==> $msg" -ForegroundColor Yellow }
function Ask($question) {
    Write-Host "$question [s/N] " -NoNewline
    $a = if ([Console]::IsInputRedirected) { [Console]::In.ReadLine() } else { Read-Host }
    return $a -match '^(s|si|sì|y|yes)$'
}
function Size([string[]]$paths) {
    $bytes = 0
    foreach ($p in $paths) {
        if (Test-Path $p) { $bytes += (Get-ChildItem $p -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum }
    }
    if ($bytes -ge 1GB) { return '{0:N1} GB' -f ($bytes / 1GB) }
    if ($bytes -ge 1MB) { return '{0:N0} MB' -f ($bytes / 1MB) }
    return '{0:N0} KB' -f ($bytes / 1KB)
}

if (Get-Process Texfix -ErrorAction SilentlyContinue) {
    Write-Host "Texfix è aperto: chiudilo e rilancia uninstall.bat"
    exit 1
}

# Le scelte: cosa togliere (descrizione, azione, riga di .installati da cancellare dopo) e cosa resta
$remove = New-Object System.Collections.ArrayList
$keep = New-Object System.Collections.ArrayList
function Plan-Remove([string]$what, [scriptblock]$action, [string]$line = '') {
    [void]$script:remove.Add([pscustomobject]@{ What = $what; Action = $action; Line = $line })
}
function Choose([string]$what, [string]$question, [scriptblock]$action, [string]$line = '') {
    if (Ask "  $what. $question") { Plan-Remove $what $action $line } else { [void]$script:keep.Add($what) }
}

# ---------------------------------------------------------------- programmi installati da Texfix
Say 'Programmi che Texfix ha installato'
$manifest = Join-Path $Root '.installati'
$lines = if (Test-Path $manifest) { @(Get-Content $manifest -Encoding utf8 | Where-Object { $_ }) } else { @() }
if ($lines.Count -eq 0) {
    Write-Host '  Nessuno: compilatore, CMake, Qt, TeX, Poppler e Claude Code c''erano già prima di Texfix (o li hai'
    Write-Host '  installati prima che l''installer tenesse il conto). Non li tocco.'
}
foreach ($line in $lines) {
    $kind, $arg = $line -split "`t", 2
    switch ($kind) {
        'winget' {
            $names = @{ 'Git.Git' = 'Git'; 'Microsoft.VisualStudio.2022.BuildTools' = 'Visual Studio Build Tools (compilatore C++)'
                'Kitware.CMake' = 'CMake'; 'oschwartz10612.Poppler' = 'Poppler'; 'Anthropic.ClaudeCode' = 'Claude Code'
                'Python.Python.3.12' = 'Python 3'; 'JohnMacFarlane.Pandoc' = 'pandoc' }
            $name = if ($names[$arg]) { $names[$arg] } else { $arg }
            $note = if ($arg -eq 'Anthropic.ClaudeCode') { 'Lo tolgo? (login e impostazioni in .claude restano)' } else { 'Lo tolgo? (attenzione se lo usi anche per altro)' }
            $id = $arg
            Choose "$name, installato con winget" $note { winget uninstall --id $id -e --silent }.GetNewClosure() $line
        }
        'texlive' {
            $dir = $arg
            Choose "TeX Live ($dir), con tutti i pacchetti LaTeX" 'Lo tolgo?' { tlmgr uninstall --all --force; Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue }.GetNewClosure() $line
        }
        'tlmgr' {
            $pkgs = $arg -split ' '
            Choose "Pacchetti LaTeX aggiunti a TeX Live: $arg" 'Li tolgo? (servono solo a Texfix se il TeX lo usi per altro)' { tlmgr remove @pkgs }.GetNewClosure() $line
        }
        'miktex' {
            $pkgs = $arg -split ' '
            Choose "Pacchetti LaTeX aggiunti a MiKTeX: $arg" 'Li tolgo? (servono solo a Texfix se il TeX lo usi per altro)' {
                foreach ($p in $pkgs) { if (Get-Command miktex -ErrorAction SilentlyContinue) { miktex packages remove $p } else { mpm --uninstall=$p } }
            }.GetNewClosure() $line
        }
        'cartella' {
            $dir = $arg
            Choose "Qt e aqtinstall scaricati per Texfix ($dir, $(Size $dir))" 'Li tolgo?' { Remove-Item -Recurse -Force $dir }.GetNewClosure() $line
        }
        'path' {
            $dir = $arg
            Choose "Poppler nel PATH del tuo utente ($dir)" 'Lo tolgo dal PATH?' {
                $parts = [Environment]::GetEnvironmentVariable('Path', 'User') -split ';' | Where-Object { $_ -and ($_.TrimEnd('\') -ne $dir.TrimEnd('\')) }
                [Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')
            }.GetNewClosure() $line
        }
    }
}

# ---------------------------------------------------------------- le preferenze di Texfix
Say 'Preferenze, regole, stili e profili di partenza'
$dataDir = Join-Path $env:APPDATA 'Texfix\Texfix (nuovo)'
$regKey = 'HKCU:\Software\Texfix\Texfix (nuovo)'
if ((Test-Path $dataDir) -or (Test-Path $regKey)) {
    Choose "Le preferenze di Texfix, le regole cambiate, gli stili e i profili salvati ($(Size $dataDir))" 'Li cancello? (non si recuperano)' {
        Remove-Item -Recurse -Force $dataDir -ErrorAction SilentlyContinue
        Remove-Item -Recurse -Force $regKey -ErrorAction SilentlyContinue
    }.GetNewClosure()
} else {
    Write-Host '  Nessuna: Texfix non è mai stato aperto, o non ha salvato niente.'
}
Write-Host '  I tuoi progetti (le cartelle che hai aperto con Texfix) restano dove sono.'

# ---------------------------------------------------------------- Texfix
Say 'Texfix'
if (Ask "  Tutta la cartella di Texfix ($Root, $(Size $Root)): codice e programma compilato. La cancello?") {
    # Fuori dalla cartella: Windows non cancella una cartella in uso
    Plan-Remove "Tutta la cartella di Texfix ($Root)" { Set-Location $env:TEMP; Remove-Item -Recurse -Force $Root }.GetNewClosure()
} else {
    [void]$keep.Add("La cartella di Texfix con il codice ($Root)")
    if (Test-Path (Join-Path $Root 'build')) {
        Choose "Il programma compilato (build, $(Size (Join-Path $Root 'build'))): si rifà con install.bat" 'Lo cancello?' { Remove-Item -Recurse -Force (Join-Path $Root 'build') }.GetNewClosure()
    }
}

# ---------------------------------------------------------------- riepilogo e conferma
Say 'Riepilogo'
if ($remove.Count -eq 0) {
    Write-Host '  Non hai scelto niente da togliere: non faccio nulla.'
    exit 0
}
Write-Host '  Tolgo:'
$remove | ForEach-Object { Write-Host "    - $($_.What)" }
if ($keep.Count -gt 0) {
    Write-Host '  Tengo:'
    $keep | ForEach-Object { Write-Host "    - $_" }
}
Write-Host ''
if (-not (Ask 'Procedo?')) {
    Write-Host 'Non ho toccato niente.'
    exit 0
}

foreach ($r in $remove) {
    Say $r.What
    try {
        $global:LASTEXITCODE = 0
        & $r.Action
        if ($LASTEXITCODE) { throw "codice $LASTEXITCODE" }
        # Tolto: non si ripropone la prossima volta
        if ($r.Line -and (Test-Path $manifest)) {
            $rest = @(Get-Content $manifest -Encoding utf8 | Where-Object { $_ -and $_ -ne $r.Line })
            Set-Content $manifest $rest -Encoding utf8
        }
    } catch {
        Write-Host "  Non riuscito ($_): lo puoi togliere a mano." -ForegroundColor Red
    }
}
Say 'Fatto'
