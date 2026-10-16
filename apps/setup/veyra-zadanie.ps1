# Zadanie Harmonogramu "\Veyra\Veyra Update" (najwyższe uprawnienia; tworzy je instalator, za jedną zgodą UAC).
# Jedyna droga, którą Veyra robi rzeczy administratora bez okna UAC. Uruchamia je "VeyraSetup.exe /zadanie" (bez okna
# konsoli), który zapisuje ten skrypt do <Veyra>\zadanie (folder tylko dla administratorów) i wywołuje go z -Dir <Veyra>.
# Prośba: jedno słowo w %LOCALAPPDATA%\Exploder\zadanie\prosba.txt; wynik: wynik.txt ("OK <prośba>" / "BŁĄD <opis>").
# Tylko z góry określone akcje — nic z prośby nie trafia do poleceń, a pliki pochodzą wyłącznie z wydania na GitHubie
# (pobierane do <Veyra>\aktualizacja, też tylko dla administratorów), nie z folderów użytkownika.
param([string]$Dir)
$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'
$q = Join-Path $env:LOCALAPPDATA 'Exploder\zadanie'
$req = Join-Path $q 'prosba.txt'; $res = Join-Path $q 'wynik.txt'
$stage = Join-Path $Dir 'aktualizacja'
$ifeo = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options'
$uninst = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Veyra'
function Done($t) { New-Item -ItemType Directory -Force $q | Out-Null; Set-Content -Path $res -Value $t -Encoding UTF8 }
# Podmiana programu systemowego (IFEO Debugger); $target pusty = przywrócenie systemowego (tylko nasz wpis)
function Debugger($exe, $target) {
  $k = Join-Path $ifeo $exe
  if ($target) { New-Item -Force $k | Out-Null; Set-ItemProperty $k Debugger ('"' + (Join-Path $Dir $target) + '"') }
  elseif ((Test-Path $k) -and ([string](Get-ItemProperty $k -ErrorAction SilentlyContinue).Debugger) -like '*exploder-*') { Remove-ItemProperty $k Debugger }
}
$action = ''
if (Test-Path $req) { $action = ([string](Get-Content $req -TotalCount 1)).Trim(); Remove-Item $req -Force }
try {
  if (-not $Dir -or -not (Test-Path (Join-Path $Dir 'exploder.exe'))) { throw 'nie znaleziono folderu Veyry' }
  # Pozostałości po podmianie uruchomionych plików (np. VeyraSetup.exe tego zadania)
  Get-ChildItem $Dir -Filter '*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  switch -exact ($action) {
    'sprawdz' { }
    'pobierz' {
      [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
      $rel = Invoke-RestMethod -Uri 'https://api.github.com/repos/Promileczek/Veyra/releases/latest' -UseBasicParsing -Headers @{ 'User-Agent' = 'Veyra' }
      $asset = $rel.assets | Where-Object { $_.name -like '*.zip' } | Select-Object -First 1
      if (-not $asset) { throw 'w wydaniu brak pliku .zip' }
      if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
      New-Item -ItemType Directory -Force $stage | Out-Null
      $zip = Join-Path $stage 'veyra.zip'
      Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -UseBasicParsing
      $src = Join-Path $stage 'pliki'
      Expand-Archive $zip $src -Force
      # exploder.exe w głównym folderze archiwum albo w jedynym podfolderze
      if (-not (Test-Path (Join-Path $src 'exploder.exe'))) { $src = (Get-ChildItem $src -Directory | Select-Object -First 1).FullName }
      if (-not $src -or -not (Test-Path (Join-Path $src 'exploder.exe'))) { throw 'w pobranym archiwum brak exploder.exe' }
      Set-Content (Join-Path $stage 'zrodlo.txt') $src -Encoding UTF8
      Set-Content (Join-Path $stage 'wersja.txt') ([string]$rel.tag_name -replace '^[vV]', '') -Encoding UTF8
    }
    'zainstaluj' {
      # Veyrę zamyka wcześniej skrypt aktualizacji (konto użytkownika); tu tylko podmiana plików
      $srcFile = Join-Path $stage 'zrodlo.txt'
      if (-not (Test-Path $srcFile)) { throw 'brak pobranej wersji' }
      $src = ([string](Get-Content $srcFile -TotalCount 1)).Trim()
      $minimal = Test-Path (Join-Path $Dir 'veyra-minimal')
      $files = Get-ChildItem $src -Filter '*.exe' | Where-Object { -not $minimal -or $_.Name -in @('exploder.exe', 'exploder-settings.exe', 'VeyraSetup.exe') }
      foreach ($f in $files) {
        $to = Join-Path $Dir $f.Name
        for ($i = 0; ; $i++) {
          try { Copy-Item $f.FullName $to -Force; break }
          catch {
            if ($i -ge 20) { throw }
            # Uruchomionego pliku nie da się nadpisać, ale da się zmienić jego nazwę
            if (Test-Path $to) { Move-Item $to ($to + '.old') -Force -ErrorAction SilentlyContinue }
            Start-Sleep -Milliseconds 300
          }
        }
      }
      Get-ChildItem $src -Filter '*.pdb' | Copy-Item -Destination $Dir -Force -ErrorAction SilentlyContinue
      $wp = Join-Path $src 'wallpapers'
      if (Test-Path $wp) { New-Item -ItemType Directory -Force (Join-Path $Dir 'wallpapers') | Out-Null; Copy-Item (Join-Path $wp '*') (Join-Path $Dir 'wallpapers') -Force }
      $ver = ([string](Get-Content (Join-Path $stage 'wersja.txt') -TotalCount 1 -ErrorAction SilentlyContinue)).Trim()
      if ($ver -and (Test-Path $uninst)) { Set-ItemProperty $uninst DisplayVersion $ver }
      Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
    }
    'kalkulator-wlacz' { Debugger 'calc.exe' 'exploder-calc.exe' }
    'kalkulator-wylacz' { Debugger 'calc.exe' '' }
    'menedzer-wlacz' { Debugger 'taskmgr.exe' 'exploder-taskmgr.exe' }
    'menedzer-wylacz' { Debugger 'taskmgr.exe' '' }
    default { throw 'nieznana prośba' }
  }
  Done ('OK ' + $action)
} catch {
  Done ('BŁĄD ' + $_.Exception.Message)
}
