# Dreamcast port (KallistiOS)

## Objetivo

CDI que permite **seleccionar campañas** y **jugarlas** en Dreamcast 16 MB / Flycast.

Documento completo para continuar el trabajo (trampas resueltas, hipótesis
descartadas, pendiente priorizado):

→ **[HANDOFF.md](HANDOFF.md)**

## Estado

**Jugable, con imagen y sonido completos.** Arranca, se navega el menú, se elige
episodio, se carga el nivel y se juega a ~60 FPS a pantalla completa, con
efectos y música.

| Pieza | Estado |
|-------|--------|
| Toolchain DreamSDK + KOS | OK |
| Motor `thextech.elf` | OK |
| Texturas reales (`.dctex`) | OK — conversor + carga en runtime |
| Assets en `/cd` + `graphics.list` | OK |
| Texto / fuentes | OK — menú legible |
| Selección de episodio y gameplay | OK — confirmado con mando |
| Viewport | OK — llena los 640x480, sin bordes |
| Efectos de sonido | OK — AICA vía `snd_sfx` (ADPCM Yamaha); canales SDL ≠ AICA |
| Música | OK — Ogg en buffer RAM + `fmemopen`/`sndoggvorbis` (loop sin `/cd`) |
| Música de episodio | OK — `worlds/*/music/` + `MF:` en `.lvlx` convertidos a `.ogg` |
| Controles Maple | OK (hotkeys sin mapear: no sobran botones en el pad) |
| Guardado persistente | Pendiente — `/ram` es volátil, haría falta VMU |
| Rendimiento | Mejorable — todo va a la lista TR del PVR |
| CI GitHub Actions | Desactivado en el fork (workflows multiplataforma eliminados) |

Imagen lista para usar: `dist/dreamcast/thextech_dc.cdi` (~180 MB con `cliche` + música)

## Build

```bash
# 1) Assets (host: Python + Pillow + numpy + ffmpeg con libgme)
python utils/convertkit/gfx-convert-dc.py gamepack build-dreamcast/cdroot --worlds cliche

# 2) Motor (shell DreamSDK, ruta absoluta)
DREAMSDK_HOME='I:\sw\dc-dev\DreamSDK' /i/sw/dc-dev/DreamSDK/usr/bin/bash.exe -l -c \
  'cmake --build /i/sw/TheXTech-main/build-dc -j6'

# 3) CDI
DREAMSDK_HOME='I:\sw\dc-dev\DreamSDK' /i/sw/dc-dev/DreamSDK/usr/bin/bash.exe -l -c \
  'bash /i/sw/TheXTech-main/script/dreamcast/build_engine_image.sh'
```

Requiere los kos-ports `libogg` y `libtremor` instalados
(`cd $KOS_PORTS/<port> && make install`).

Configurar desde cero (si no existe `build-dc/`): `script/dreamcast/build_engine.sh`.

## Prueba en la VM

```bash
bash script/dreamcast/quick_cycle.sh
```

Requiere BIOS real en `~/.config/retroarch/system/dc/dc_boot.bin` en la VM, y
`reicast_hle_bios = "disabled"` en `~/.config/retroarch/config/Flycast/Flycast.opt`.

⚠️ **`reicast_enable_dsp` debe estar en `enabled`** o no sale ningún sonido.

⚠️ **`reicast_gdrom_fast_loading` debe estar en `disabled`** o crashea al
cargar nivel (`Fatal: SH4 exception when blocked`).

⚠️ En RetroArch Windows, `audio_driver = "xaudio"` suele funcionar mejor que
`wasapi` (silencio total con wasapi es habitual).

## Backend

- `src/core/dreamcast/` — render PVR, window, events, msgbox, power, init de KOS,
  sustitutos del runtime C++, sonda de arranque y reportero de crash
- `src/control/input_dreamcast.*` — mando Maple
- `lib/sdl_proxy/dreamcast/` — timers y mixer (AICA + Ogg en RAM vía tremor)
- `lib/AppPath/private/app_path_dreamcast.cpp` — `/cd/` + `/ram/` plano
- `utils/convertkit/gfx-convert-dc.py` — conversor de gráficos, efectos y música
  (incluye `music/` de episodio y reescritura de `MF:` en niveles)
- CMake: `-DDREAMCAST=ON` + `cmake/dreamcast.toolchain.cmake`

Detalle operativo (trampas, audio, pendiente): **[HANDOFF.md](HANDOFF.md)**.
