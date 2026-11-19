# Aggiorna Texfix (Windows): controlla su GitHub se c'è una versione nuova, dice cosa cambia e, se confermi,
# la scarica e la installa con install.ps1 (che controlla anche i requisiti nuovi).
# I tuoi progetti e le tue preferenze non stanno in questa cartella: l'aggiornamento non li tocca.
# Gli errori di git si controllano con $LASTEXITCODE: Windows PowerShell 5.1 con 'Stop' si fermerebbe a ogni
# messaggio di git su stderr
$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot

function Say($msg) { Write-Host "`n==> $msg" -ForegroundColor Yellow }
function Ask($question) {
    Write-Host "`n$question [s/N] " -NoNewline
    $a = if ([Console]::IsInputRedirected) { [Console]::In.ReadLine() } else { Read-Host }
    return $a -match '^(s|si|sì|y|yes)$'
}
function Git-Out { $out = & git @args 2>$null; if ($LASTEXITCODE) { return $null }; return $out }

if (-not (Get-Command git -ErrorAction SilentlyContinue) -or -not (Test-Path .git)) {
    Write-Host "Questa copia di Texfix non è un clone git, quindi non posso aggiornarla da qui."
    Write-Host "Scarica di nuovo Texfix e lancia install.bat: i tuoi progetti e le preferenze stanno altrove e restano."
    exit 1
}
$branch = Git-Out rev-parse --abbrev-ref HEAD
if ($branch -eq 'HEAD' -or -not (Git-Out rev-parse --abbrev-ref '@{u}')) {
    Write-Host "Il branch attuale ($branch) non segue un branch su GitHub: aggiorna a mano con git."
    exit 1
}

Say "Controllo se c'è una versione nuova"
git fetch --quiet 2>$null
if ($LASTEXITCODE) {
    Write-Host "Non riesco a contattare GitHub: controlla la connessione e riprova."
    exit 1
}
$behind = [int](Git-Out rev-list --count 'HEAD..@{u}')
$ahead = [int](Git-Out rev-list --count '@{u}..HEAD')
$current = Git-Out log -1 '--format=%h del %ad' '--date=format:%d/%m/%Y'
if ($behind -eq 0) {
    Write-Host "Hai già l'ultima versione ($current)."
    exit 0
}
$latest = Git-Out log -1 '--format=%h del %ad' '--date=format:%d/%m/%Y' '@{u}'
Write-Host "C'è una versione nuova: hai la $current, l'ultima è la $latest ($behind modifiche)."
Write-Host "`nCosa cambia:"
Git-Out log -n 25 '--format=  - %s' 'HEAD..@{u}' | ForEach-Object { Write-Host $_ }
if ($behind -gt 25) { Write-Host "  ... e altre $($behind - 25)" }

if ($ahead -ne 0) {
    Write-Host "`nHai $ahead commit tuoi che su GitHub non ci sono: per non perderli non aggiorno da qui."
    Write-Host "Uniscili a mano con 'git pull' e poi lancia install.bat"
    exit 1
}
if (Get-Process Texfix -ErrorAction SilentlyContinue) {
    Write-Host "`nTexfix è aperto: chiudilo e rilancia update.bat"
    exit 1
}
$changed = Git-Out status --porcelain --untracked-files=no
if ($changed) {
    Write-Host "`nHai modificato questi file di Texfix:"
    $changed | ForEach-Object { Write-Host "  $_" }
    Write-Host "Prima di aggiornare li metto da parte con git stash: li ritrovi con 'git stash pop'."
}
Write-Host "`nPoi l'installer controlla i requisiti (la versione nuova può chiederne altri), ti dice cosa farebbe e"
Write-Host "ti chiede conferma prima di ricompilare."

if (-not (Ask 'Scarico la versione nuova?')) {
    Write-Host "Non ho toccato niente."
    exit 0
}
if ($changed) { git stash push --quiet -m "Texfix: modifiche locali prima dell'aggiornamento del $(Get-Date -Format 'dd/MM/yyyy')" }
git merge --ff-only --quiet '@{u}'
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Host "Aggiornato alla $latest."

Say 'Installo la versione nuova'
& (Join-Path $PSScriptRoot 'install.ps1')
exit $LASTEXITCODE
