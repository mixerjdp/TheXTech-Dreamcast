# Smoke-test thextech_dc.cdi on Windows RetroArch + Flycast.
# Compares stock Win Flycast.opt vs Linux-aligned opts; tries to enter gameplay.
param(
    [string]$RaRoot = 'I:\juegos\RetroArch-Win64',
    [string]$Rom = 'I:\sw\TheXTech-main\dist\dreamcast\thextech_dc.cdi',
    [string]$OutDir = 'I:\sw\TheXTech-main\build-dreamcast\smoke_win',
    [ValidateSet('stock', 'linuxlike', 'both')]
    [string]$Mode = 'both',
    [int]$BootWaitSec = 14,
    [int]$AfterInputSec = 18
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class NativeWin {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
}
"@

function Stop-Ra {
    Get-Process retroarch -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 800
}

function Write-LinuxLikeOpt([string]$path) {
    @"
reicast_allow_service_buttons = "disabled"
reicast_alpha_sorting = "per-triangle (normal)"
reicast_analog_stick_deadzone = "15%"
reicast_anisotropic_filtering = "4"
reicast_auto_skip_frame = "disabled"
reicast_broadcast = "NTSC"
reicast_cable_type = "TV (Composite)"
reicast_coin_limit = "0"
reicast_custom_textures = "disabled"
reicast_dc_32mb_mod = "disabled"
reicast_dcnet = "enabled"
reicast_delay_frame_swapping = "enabled"
reicast_detect_vsync_swap_interval = "disabled"
reicast_device_port1_slot1 = "VMU"
reicast_device_port1_slot2 = "Purupuru"
reicast_device_port2_slot1 = "VMU"
reicast_device_port2_slot2 = "Purupuru"
reicast_device_port3_slot1 = "VMU"
reicast_device_port3_slot2 = "Purupuru"
reicast_device_port4_slot1 = "VMU"
reicast_device_port4_slot2 = "Purupuru"
reicast_digital_triggers = "disabled"
reicast_dump_replaced_textures = "disabled"
reicast_dump_textures = "disabled"
reicast_emulate_bba = "disabled"
reicast_emulate_framebuffer = "disabled"
reicast_enable_dsp = "enabled"
reicast_enable_rttb = "disabled"
reicast_fix_upscale_bleeding_edge = "enabled"
reicast_fog = "enabled"
reicast_force_freeplay = "enabled"
reicast_frame_skipping = "disabled"
reicast_gdrom_fast_loading = "disabled"
reicast_hle_bios = "disabled"
reicast_internal_resolution = "640x480"
reicast_language = "English"
reicast_lightgun1_crosshair = "disabled"
reicast_lightgun2_crosshair = "disabled"
reicast_lightgun3_crosshair = "disabled"
reicast_lightgun4_crosshair = "disabled"
reicast_lightgun_crosshair_size_scaling = "100%"
reicast_linked_vmu_storage = "disabled"
reicast_mipmapping = "enabled"
reicast_native_depth_interpolation = "disabled"
reicast_network_output = "disabled"
reicast_oit_abuffer_size = "512MB"
reicast_oit_layers = "32"
reicast_per_content_vmus = "disabled"
reicast_preload_custom_textures = "disabled"
reicast_pvr2_filtering = "disabled"
reicast_region = "USA"
reicast_screen_rotation = "horizontal"
reicast_sh4clock = "200"
reicast_show_lightgun_settings = "disabled"
reicast_show_vmu_screen_settings = "disabled"
reicast_texture_filtering = "0"
reicast_texupscale = "1"
reicast_texupscale_max_filtered_texture_size = "256"
reicast_threaded_rendering = "enabled"
reicast_trigger_deadzone = "0%"
reicast_upnp = "enabled"
reicast_vmu_sound = "disabled"
reicast_volume_modifier_enable = "enabled"
reicast_widescreen_cheats = "disabled"
reicast_widescreen_hack = "disabled"
"@ | Set-Content -Path $path -Encoding ascii
}

function Send-GameConfirm {
    param([int]$Times = 3, [int]$GapMs = 900)
    $procs = Get-Process retroarch -ErrorAction SilentlyContinue
    foreach ($p in $procs) {
        if ($p.MainWindowHandle -ne [IntPtr]::Zero) {
            [NativeWin]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
            [NativeWin]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
        }
    }
    Start-Sleep -Milliseconds 400
    for ($i = 0; $i -lt $Times; $i++) {
        # A / Start / Enter — cover menu confirm bindings
        [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
        Start-Sleep -Milliseconds 200
        [System.Windows.Forms.SendKeys]::SendWait('x')  # often RetroPad A
        Start-Sleep -Milliseconds 200
        [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
        Start-Sleep -Milliseconds $GapMs
        Write-Host "  input burst $($i+1)/$Times"
    }
}

function Invoke-Smoke([string]$label, [string]$optSource) {
    $runDir = Join-Path $OutDir $label
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    $log = Join-Path $runDir 'retroarch.log'
    $cfg = Join-Path $runDir 'append.cfg'
    $shotDir = Join-Path $runDir 'shots'
    New-Item -ItemType Directory -Force -Path $shotDir | Out-Null

    $optPath = Join-Path $RaRoot 'config\Flycast\Flycast.opt'
    Copy-Item -Force $optSource $optPath

    @"
system_directory = "$($RaRoot -replace '\\','/')/system"
audio_driver = "xaudio"
video_driver = "gl"
video_fullscreen = "false"
video_windowed_fullscreen = "false"
video_window_width = "640"
video_window_height = "480"
video_font_enable = "true"
notification_show_autoconfig = "false"
quit_press_twice = "false"
log_verbosity = "true"
libretro_log_level = "1"
screenshot_directory = "$($shotDir -replace '\\','/')"
network_cmd_enable = "true"
network_cmd_port = "55355"
input_auto_game_focus = "1"
"@ | Set-Content -Path $cfg -Encoding ascii

    Stop-Ra
    Remove-Item -Force $log -ErrorAction SilentlyContinue

    $exe = Join-Path $RaRoot 'retroarch.exe'
    $core = Join-Path $RaRoot 'cores\flycast_libretro.dll'
    Write-Host "==> [$label] launching"
    Write-Host "    opt: $optSource"
    Write-Host "    rom: $Rom"

    $p = Start-Process -FilePath $exe -ArgumentList @(
        '-L', $core, $Rom,
        '--appendconfig', $cfg,
        '--verbose'
    ) -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $runDir 'stderr.log') -WorkingDirectory $RaRoot

    $t0 = Get-Date
    Start-Sleep -Seconds $BootWaitSec

    if ($p.HasExited) {
        Write-Host "!! [$label] exited early code=$($p.ExitCode) after $([int]((Get-Date)-$t0).TotalSeconds)s"
    } else {
        Write-Host "  alive after boot wait; sending confirms"
        Send-GameConfirm -Times 4 -GapMs 1100
        Start-Sleep -Seconds $AfterInputSec
    }

    $alive = -not $p.HasExited
    $ranSec = [int]((Get-Date) - $t0).TotalSeconds
    if (-not $p.HasExited) {
        try {
            $udp = New-Object System.Net.Sockets.UdpClient
            $bytes = [Text.Encoding]::ASCII.GetBytes("SCREENSHOT`n")
            $udp.Send($bytes, $bytes.Length, '127.0.0.1', 55355) | Out-Null
            $udp.Close()
            Start-Sleep -Seconds 2
        } catch {}
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 1
    }

    $text = ''
    if (Test-Path $log) { $text = Get-Content -Raw -Path $log -ErrorAction SilentlyContinue }
    $err = ''
    $errPath = Join-Path $runDir 'stderr.log'
    if (Test-Path $errPath) { $err = Get-Content -Raw -Path $errPath -ErrorAction SilentlyContinue }
    $all = "$text`n$err"

    $fatal = [regex]::Matches($all, 'Fatal:.*|SH4 exception|exception when blocked|Content ran for .+ seconds') |
        ForEach-Object { $_.Value } | Select-Object -Unique

    $summary = [pscustomobject]@{
        label = $label
        alive_after_inputs = $alive
        ran_sec = $ranSec
        exit_code = $p.ExitCode
        fatals = ($fatal -join ' | ')
        shots = @(Get-ChildItem $shotDir -Filter *.png -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
        opt_32mb = (Select-String -Path $optSource -Pattern 'reicast_dc_32mb_mod').Line
        opt_alpha = (Select-String -Path $optSource -Pattern 'reicast_alpha_sorting').Line
        opt_gdrom = (Select-String -Path $optSource -Pattern 'reicast_gdrom_fast_loading').Line
    }
    $summary | ConvertTo-Json | Set-Content (Join-Path $runDir 'summary.json')
    $summary | Format-List | Out-String | Write-Host
    return $summary
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$optDir = Join-Path $RaRoot 'config\Flycast'
$stockBak = Join-Path $OutDir 'Flycast.opt.stock.bak'
$linuxOpt = Join-Path $OutDir 'Flycast.opt.linuxlike'
Copy-Item -Force (Join-Path $optDir 'Flycast.opt') $stockBak
Write-LinuxLikeOpt $linuxOpt

$results = @()
try {
    if ($Mode -eq 'stock' -or $Mode -eq 'both') {
        $results += Invoke-Smoke 'stock_win' $stockBak
    }
    if ($Mode -eq 'linuxlike' -or $Mode -eq 'both') {
        $results += Invoke-Smoke 'linuxlike' $linuxOpt
    }
}
finally {
    Copy-Item -Force $stockBak (Join-Path $optDir 'Flycast.opt')
    Stop-Ra
    Write-Host "==> restored original Flycast.opt"
}

Write-Host "==== SUMMARY ===="
$results | Format-Table label, alive_after_inputs, ran_sec, fatals -AutoSize
$results | ConvertTo-Json | Set-Content (Join-Path $OutDir 'summary_all.json')
