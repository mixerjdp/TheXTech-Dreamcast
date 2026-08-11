# Dreamcast port — handoff

**Objetivo:** un CDI que permita **seleccionar campañas** y **jugarlas** en Dreamcast retail (16 MB) / Flycast.

**Última actualización:** 2026-08-11
**Repo:** `I:\sw\TheXTech-main` (remoto: `mixerjdp/TheXTech-Dreamcast`)
**Idioma de trabajo con el usuario:** español

---

## Estado actual

**Jugable, con imagen y sonido completos.** Arranca, el menú se lee, se elige
episodio (`cliche`), se carga el nivel y se juega a ~60 FPS ocupando los
640x480, con efectos de sonido y música de mapa/nivel (loop estable).

| Pieza | Estado | Cómo se verificó |
|---|---|---|
| Arranque del motor | OK | Corre >100 s sin excepción SH4 |
| Texturas (`.dctex`) | OK | 3183 texturas, 0 fallos |
| Texto / fuentes | OK | Menú legible en captura |
| Menú → episodio → nivel | OK | Confirmado por el usuario con mando |
| Viewport | OK | Llena 640x480, sin bordes |
| Efectos de sonido | OK | 104 cargados en AICA; saltos ya no matan la música |
| Música (mapa + nivel) | OK | Buffer RAM + `fmemopen`; pistas de episodio en CDI |
| Controles Maple | OK | Jugado con mando |
| Guardado persistente | **Falta** | `/ram` es volátil |
| Rendimiento | **Mejorable** | Baja de 60 en escenas cargadas |
| CI remoto | **Apagado** | Workflows multiplataforma borrados del fork |

Artefacto entregado: `dist/dreamcast/thextech_dc.cdi` (~180 MB con `cliche` + música).

---

## Cómo construir y probar

### 1. Assets (host: Python + Pillow + numpy + ffmpeg)

```bash
python utils/convertkit/gfx-convert-dc.py gamepack build-dreamcast/cdroot --worlds cliche
```

~35 s. Convierte gráficos, efectos y música. Flags: `--worlds` elige campañas
(sin él, todas), `--no-sfx`, `--no-music`, `--no-twiddle`, `--no-lists`.

Convierte **`music/` de la raíz y de cada episodio** (`worlds/cliche/music/`),
reescribe `music.ini` y los `MF:"….spc"` embebidos en `.lvlx`/`.wldx` a `.ogg`.
Sin eso, el mapa puede sonar y el nivel quedar en silencio.

ffmpeg debe traer el demuxer **libgme** (para los `.spc`) y **libvorbis**.
Bitrate de música: **24 kbps** mono 22 kHz (cabe en el buffer de staging).

### 2. Motor

Usar **ruta absoluta**: un `cd` dentro de `-c` es frágil en este shell. Si `make`
falla con `dofork: child died unexpectedly` es el fork de MSYS, no el código:
reintentar con `-j6`.

```bash
DREAMSDK_HOME='I:\sw\dc-dev\DreamSDK' /i/sw/dc-dev/DreamSDK/usr/bin/bash.exe -l -c \
  'cmake --build /i/sw/TheXTech-main/build-dc -j6'
```

Configurar desde cero (si no existe `build-dc/`): `script/dreamcast/build_engine.sh`.

### 3. CDI

```bash
DREAMSDK_HOME='I:\sw\dc-dev\DreamSDK' /i/sw/dc-dev/DreamSDK/usr/bin/bash.exe -l -c \
  'bash /i/sw/TheXTech-main/script/dreamcast/build_engine_image.sh'
```

### 4. Prueba en la VM (`juan@192.168.31.128`)

```bash
bash script/dreamcast/quick_cycle.sh                    # CDI mínimo, ~40 s/iteración
STOP=8 bash script/dreamcast/bisect_findworlds.sh       # bisección con puntos de parada
```

Para la imagen completa: `scp` + `vm_test_engine.sh` con `ROM=`.

Medir audio de verdad (la VM tiene tarjeta de sonido y PulseAudio):

```bash
MON="$(pactl get-default-sink).monitor"
ffmpeg -f pulse -i "$MON" -t 30 -y /tmp/cap.wav
ffmpeg -i /tmp/cap.wav -af volumedetect -f null /dev/null 2>&1 | grep volume
```

`-91 dB` = silencio digital. El juego sano da ~-16 dB de media.

---

## Dependencias del entorno

| Pieza | Dónde | Notas |
|---|---|---|
| DreamSDK R4 + KOS | `I:\sw\dc-dev\DreamSDK` | sh-elf-gcc 13.2.0 |
| kos-ports instalados | `libogg`, `libtremor` | `cd $KOS_PORTS/<port> && make install` |
| BIOS real (VM) | `~/.config/retroarch/system/dc/dc_boot.bin` | + `dc_flash.bin` |
| ffmpeg (host) | con libgme y libvorbis | conversión de audio |

### Configuración de Flycast (aprendida a base de golpes)

- **`reicast_enable_dsp` debe estar en `enabled`.** Con `disabled` **no sale
  ningún sonido**, ni efectos ni música. Fue la causa de un "no suena nada" que
  costó varias iteraciones.
- **`reicast_gdrom_fast_loading` debe estar en `disabled`.** Con `enabled`
  crashea al cargar nivel (`Fatal: SH4 exception when blocked`) en Windows y en
  la VM. No confundir con “más rápido = mejor”.
- `reicast_hle_bios = "disabled"` para usar el BIOS real.
- Las opciones del core viven en `config/Flycast/Flycast.opt`, **no** en
  `retroarch-core-options.cfg`.
- `system_directory` debe ser ruta absoluta: el core no expande `~` y cae
  silenciosamente a REIOS.
- En Windows, `audio_driver = "wasapi"` es una fuente habitual de silencio;
  **`xaudio`** es más fiable.
- Resto de ajustes recomendados: `alpha_sorting = per-strip` (el renderer ya
  envía en orden de pintor), `mipmapping`, `fog` y `anisotropic` desactivados
  (no los usamos), `internal_resolution` a 640x480 nativo.

---

## Arquitectura del port

### Texturas: formato `.dctex`

`utils/convertkit/gfx-convert-dc.py` convierte el gamepack a blobs listos para
PVR, siguiendo el modelo del port de Wii (`.tpl`) y 3DS (`.t3x`). `X_IMG_EXT`
pasó de `.png` a `.dctex`.

- **Media resolución**: el minport direcciona texels en espacio de medio píxel
  (`FLOORDIV2` en `render_minport_shared.hpp`); el sidecar `.size` reporta las
  dimensiones a resolución completa.
- **Excepción, las fuentes**: los atlas que declaran `texture-scale = 2` se
  guardan a **resolución completa** (`fonts_kept_at_full_res()`), igual que hace
  el Wii. Y `lazyLoadPicture()` **ignora `scaleFactor`**, porque la cabecera ya
  trae el tamaño final; aplicarlo otra vez duplicaba cada celda de glifo.
- **Formato por imagen**: RGB565 / ARGB1555 / ARGB4444 según el alfa real.
- **Twiddled**, replicando `pvr_txr_load_ex` de KOS (validado contra la
  implementación de referencia en 8 tamaños, incluidos rectangulares).
- Arte que excede los 1024 texels del PVR se reduce más; el runtime lo compensa
  con `u_scale`/`v_scale` de la cabecera.
- Genera `graphics.list` por directorio: evita miles de búsquedas en el GD-ROM.

Cabecera (24 bytes): magic `DCTX`, `tex_w/h` (POT), `span_w/h`, `img_w/h`,
formato, flags.

### Renderer — `src/core/dreamcast/render_dreamcast.cpp`

- Carga fichero → VRAM en trozos de 16 KB por store queues, sin buffer
  intermedio del tamaño de la textura.
- Si `Mix_DC_IsCdLocked()` (staging de música), el lazy-load **espera** antes
  de abrir otro fichero en `/cd` — evita abortar la copia de la Ogg.
- Todo se emite a la lista **TR** en orden de dibujo → algoritmo del pintor.
- `getWindowSize()` devuelve **2× el framebuffer real** (1280x960 para 640x480):
  es la convención del minport (el Wii hace `g_rmode_w * 2`).
- `s_update_transform()` **escala para llenar** la salida en vez de centrar. El
  factor es uniforme, así que un objetivo 4:3 llena exacto y cualquier otro haría
  letterbox en lugar de deformar.
- Offsets de viewport se **suman** (como SDL y 3DS; el Wii los resta, es
  peculiaridad suya).
- `RENDER_WANTS_UNSIGNED_DEPTH`: el PVR compara profundidades positivas.

Limitación conocida: **sin scissor de viewport**. El recorte del PVR es por
tiles de 32x32 y sólo haría falta para pantalla partida.

### Audio — `lib/sdl_proxy/dreamcast/mixer_dreamcast.cpp`

- **Efectos**: `snd_sfx` de KOS. El conversor pasa los `.ogg` a **WAV Yamaha
  ADPCM** (`-acodec adpcm_yamaha -ac 1 -ar 22050`), formato que `snd_sfx_load`
  entiende nativo: 104 efectos ocupan 1.2 MB de los 2 MB del AICA. Reescribe
  `sounds.ini` de `.ogg` a `.wav`.
- **Canales AICA**: los índices de canal de SDL Mixer (`single-channel=1` en
  `sounds.ini`, p. ej. `player-jump` → canal `0`) **no** se pasan a
  `snd_sfx_play_chn()`. Los canales AICA **0/1** los reserva el stream de
  música (`snd_sfx_chn_alloc`). Mapear SDL→AICA hacía que **cada salto matara
  la melodía**. Ahora siempre se elige un canal libre; si el SFX es
  single-channel se corta la instancia anterior del mismo chunk.
- **Música**: `sndoggvorbis` (kos-ports **libtremor**). Todas las pistas pasan
  a Ogg mono 22 kHz @ **24 kbps**, incluidas las `.spc`/`.nsf`/`.it` vía
  **libgme**. Chiptunes se cortan a 120 s en el conversor.
  - Al `Mix_OpenAudio` se reserva un buffer de **768 KB**.
  - Bajo un **CD lock** (`Mix_DC_IsCdLocked`; el lazy-load de texturas espera),
    se copia la pista entera con `fs_total` + `fs_read` y se valida `OggS`.
  - Se reproduce con **`fmemopen` + `sndoggvorbis_start_fd`**: el loop
    (`ov_raw_seek`) pega en RAM, nunca en el GD-ROM. Reopens reusan el mismo
    buffer; **no se vuelve a leer `/cd`** para la pista actual.
  - Si el stage falla al entrar al nivel, la pista queda *pending* y
    `Mix_DC_PumpMusic()` (cada frame desde `events_dreamcast`) reintenta cuando
    el bus está más quieto. **No** se hace stream de loop desde `/cd` (eso
    moría a los 2–8 s con duración variable).
  - Un pump agresivo que reabría el fichero cada frame al boot **crasheaba** el
    SH4; el pump actual solo actúa tras cooldown y con buffer ya validado.
- **Conversor / episodio**: convierte `music/` raíz **y** `worlds/*/music/`,
  reescribe `music.ini` y `MF:` en `.lvlx`/`.wldx`. Sin la carpeta de episodio
  en el CDI, el overworld puede sonar y Mushroom Pass quedar mudo.
- `Mix_Music` lleva un flag **`streamable`** (sólo `.ogg`). El motor también
  mete efectos largos por la API de música; sin el flag cada efecto llamaba a
  `sndoggvorbis_stop()` y mataba la BGM.
- **Paneo aceptado e ignorado**: todo suena centrado.

### Controles Maple — `src/control/input_dreamcast.cpp`

A=Salto, B=Salto alt., X=Correr, Y=Correr alt., L=Soltar, R=Correr (secundario),
Start=Start, cruceta + stick analógico. Hasta 4 jugadores, uno por puerto.

Los 10 controles de `PlayerControls` están mapeados; **los hotkeys no** (no
sobran botones en el pad). Tampoco se pueden reasignar desde el juego:
`PollPrimaryButton`/`PollSecondaryButton` devuelven `false`.

### Filesystem

KOS monta `/cd` (ISO9660, Joliet y Rock Ridge) automáticamente. `/ram` sólo se
monta si el binario declara `KOS_INIT_FLAGS` (ver trampa 3).

---

## Trampas resueltas — leer antes de tocar nada

Ninguna es evidente y todas costaron bisección.

### 1. Ningún binario C++ arrancaba

**Síntoma:** `Fatal: SH4 exception when blocked`, 0 s de ejecución, antes del
primer constructor estático.

**Causa:** en este toolchain, `__register_frame_info()` de libgcc
(`unwind-dw2-fde.c`) falla. `frame_dummy` de crtbegin la llama desde `_init()`,
o sea antes de cualquier constructor, así que **cualquier** binario que enlazase
un objeto de libstdc++ —incluso sólo `operator new`— moría al arrancar.

No es específico de TheXTech: **el ejemplo `cpp/concurrency` que trae KallistiOS
falla igual**, tanto con REIOS como con BIOS real.

**Solución:** `src/core/dreamcast/eh_frame_dreamcast.cpp` la sustituye por un
no-op, vía `-Wl,--wrap=__register_frame_info`.

Se intentó *aplazar* el registro (capturarlo en `_init()` y replicarlo con la
función real al entrar en `main()`). **No sirve**: revienta igual, sólo que más
tarde. No es cuestión de cuándo se llama.

### 2. Consecuencia: las excepciones C++ no desenrollan

Sin tablas de frames, un `throw` **termina el programa**. Verificado con un
`try { throw std::runtime_error(...) } catch(...)` dentro del propio motor.

Esto sigue siendo cierto. **Cualquier código nuevo que dependa de excepciones se
caerá.**

### 3. Los estáticos locales de función también fallaban

`__cxa_guard_acquire/release/abort` de libstdc++ pasan por gthreads y fallan
aquí. El motor moría en el primer `static` local con constructor dinámico
(`g_config_backup` en `UpdateConfig`). Implementados a mano en el mismo fichero,
vía `-Wl,--allow-multiple-definition`. El arranque es mono-hilo, así que un byte
"¿ya se ejecutó?" basta.

### 4. El ramdisk de KOS no tiene directorios

`fs_ramdisk.c` deja los handlers de `mkdir`/`rmdir` a NULL — sólo existe el
directorio raíz. Y sin declarar `KOS_INIT_FLAGS`, KOS sólo inicializa los
subsistemas que el binario referencia por símbolo débil: el motor nunca nombra
`fs_ramdisk_init`, así que **`/ram` ni siquiera se montaba**.

- `src/core/dreamcast/kos_init_dreamcast.cpp` declara `KOS_INIT_FLAGS(INIT_DEFAULT)`.
- `lib/AppPath/private/app_path_dreamcast.cpp` apunta *todas* las raíces
  escribibles a `/ram/` (plano, sin subcarpetas).

### 5. Crash al seleccionar episodio

El parser PGE-X moderno (MDX) usa una **excepción como control de flujo normal**:

```cpp
static bool s_load_head_only(void* _FileData, WorldHead& dest)
{
    s_load_head(_FileData, dest);
    throw PGE_FileFormats_misc::callback_interrupt();   // <-- camino feliz
}
```

Eso es lo que hace `FindWorlds()` al listar episodios, y el parser de **niveles
tiene el mismo patrón**. Con el desenrollado roto (trampa 2), mataba la máquina.

**Solución:** parser legacy en Dreamcast, al principio de `main()`:

```cpp
FileFormats::g_use_legacy_pgex_parser = true;
```

Lee los mismos ficheros y **no lanza** en el camino de éxito. Cubre mundos y
niveles de una vez.

### 6. Texto ilegible

Dos causas encadenadas: las fuentes deben quedarse a resolución completa, y
`scaleFactor` no debe aplicarse a blobs pre-cocinados. Ver "Texturas" arriba.

### 7. Viewport con bordes verdes

`g_screen_phys_*` centra el juego a escala 1:1 — con un objetivo de 800x600 eso
es una isla de 400x300 en medio de los 640x480, y alrededor quedaba framebuffer
sin tocar. Ahora se escala para llenar. Ver "Renderer".

### 8. Silencio total pese a que el audio estaba bien

`reicast_enable_dsp = "disabled"` en Flycast. Ver "Configuración de Flycast".

Antes de eso hubo otra: `InitMixerX()` está entero dentro de
`#ifndef THEXTECH_NO_SDL_BUILD`, así que en Dreamcast **nunca se llamaba a
`Mix_OpenAudio`** y `g_mixerLoaded` se quedaba en `false`, con lo que
`InitSound()` retornaba sin cargar un solo efecto aunque el backend fuera
perfecto. `src/sound.cpp` tiene ahora una rama `__DREAMCAST__`.

### 9. El preload de niveles reventaba

`THEXTECH_PRELOAD_LEVELS` hace que `loadingThread` llame a `FindWorlds()` +
`FindLevels()` al arrancar. Desactivado para DC: los mundos se descubren bajo
demanda desde el menú, que además es lo correcto con 16 MB.

Nota: la causa era la trampa 5, ya resuelta. Se podría reactivar, pero
precargarlo todo sigue sin caber cómodamente.

### 10. `cdi4dc` y el flag `-d`

`-d` es para imágenes MSINFO 0. La nuestra usa `-C 0,11702` (MSINFO 11702,
audio/data) y va **sin flag**. Con `-d` el CDI sale malformado y Flycast revienta
con `std::length_error` en REIOS.

### 11. Música que “dura 2–8 s” y se corta (variable)

**Síntoma:** la pista del nivel suena unos segundos y muere; a veces 2 s, a
veces 6–8 s. El fichero en el CDI dura ~87 s.

**Causas encadenadas (todas reales en algún momento):**

1. Stream desde `/cd` mientras el lazy-load de `.dctex` aborta el stream ISO.
2. Copia a `/ram` truncada: `fread` devolvía 0 antes del EOF real; o un sidecar
   `.ogg.size` leído a medias daba un tamaño falso (“270682” → “2”) y el mixer
   aceptaba un stub.
3. Pump que reabría desde `/cd` tras el corte → otra ráfaga corta.

**Solución actual:** buffer de 768 KB + `fs_total`/`fs_read` bajo CD lock +
`fmemopen`/`start_fd`. Las texturas esperan con `Mix_DC_IsCdLocked()`. No
reproducir loops desde `/cd`.

### 12. Saltar mata la música

**Síntoma:** la BGM suena hasta que Mario salta (u otro SFX `single-channel`).

**Causa:** `Mix_PlayChannelVol(0, …)` pasaba el índice SDL al AICA. El canal
hardware 0 es del stream de `sndoggvorbis`.

**Solución:** nunca mapear canales SDL → AICA; `snd_sfx_play()` elige canal
libre. Ver sección Audio.

### 13. Nivel en silencio pese a que el mapa suena

**Causa:** el conversor solo trataba `music/` de la raíz; `worlds/cliche/music/`
faltaba en el CDI y los `MF:"music/….spc"` del `.lvlx` no se reescribían a
`.ogg`.

**Solución:** `gfx-convert-dc.py` convierte cualquier `…/music/` y reescribe
`MF:` / `music.ini`.

### 14. Spam de mails “DSi CI / 3DS CI / …” en el fork

Los workflows de CI multiplataforma del upstream se disparaban en cada push y
fallaban. En este fork se **eliminaron** de `.github/workflows/` y se
desactivaron en GitHub Actions. No re-añadirlos sin un runner que los soporte.

---

## Hipótesis descartadas (no reabrir sin datos nuevos)

- **Tamaño del binario o del `.bss`** — demos en C con footprint mayor que el
  motor (4 MB texto + 4.3 MB bss) arrancan sin problema.
- **`--gc-sections`** — se quitó de la build DC; `text`/`bss` quedaron byte a
  byte idénticos, no recolectaba nada.
- **Tamaño del CDI / árbol de assets** — un CDI mínimo con sólo `1ST_READ.BIN`
  fallaba igual.
- **`scramble` con binarios grandes** — demos de 3.9 MB pasan por el mismo
  scramble y arrancan.
- **RAM insuficiente** — fallaba igual con `reicast_dc_32mb_mod`.
- **TLS** — ningún binario tiene segmento `PT_TLS` ni secciones `.tbss`/`.tdata`.
- **Mismatch de ABI de coma flotante** — `libkallisti.a` y la `libstdc++.a` del
  multilib `m4-single-only` son **ambas sh4a** (flags `0xc`); coinciden.
  ⚠️ **No usar `-m4-single`**: selecciona el multilib por defecto, cuya
  `libstdc++.a` es **sh2e** (flags `0xb`), arquitectura equivocada.
- **REIOS vs BIOS real** — comportamiento idéntico. El BIOS real sirvió para
  descartar el emulador como causa.
- **Desajuste de ABI en gthreads** — `mutex_t` son 12 bytes con inicializador
  todo ceros y `kthread_once_t` es un `volatile int`. La causa real de que
  `__register_frame_info` falle **sigue sin identificarse**.

### Ruido del entorno que conviene conocer

`environ_dreamcast.sh` comprueba `-m4-single` compilando con `-o /dev/null`, lo
que da **falso negativo en MSYS/Windows**, de ahí la advertencia constante sobre
el ABI. El fallback (`-m4-single-only`) es el **correcto** aquí.

También aparece `The KallistiOS Ports library referential file was not found:
kos-libraries.conf`. Tampoco impide compilar.

---

## Herramientas de diagnóstico

Todas compilan a nada por defecto.

| Script | Para qué sirve |
|---|---|
| `script/dreamcast/quick_cycle.sh` | Empaqueta, sube y ejecuta un CDI mínimo (~40 s) |
| `script/dreamcast/vm_test_engine.sh` | Lanza RetroArch en la VM y toma capturas escalonadas |
| `script/dreamcast/bisect_findworlds.sh` | Un ciclo de bisección con `STOP=<n>` |
| `script/dreamcast/probe_cpp.sh` | Reproductor C++ con footprint configurable |
| `script/dreamcast/probe_level.sh` | Bisección por features de C++ (`LEVEL=0..7`) |

| Opción CMake | Efecto |
|---|---|
| `THEXTECH_DC_BOOT_PROBE` | Marcadores de color en el framebuffer |
| `THEXTECH_DC_STOP_AT=<n>` | Congela en el punto `DC_STEP(n)` de `FindWorlds()` |
| `THEXTECH_DC_TEST_FINDWORLDS` | Llama a `FindWorlds()` durante la carga (sin mando) |
| `THEXTECH_DC_AUDIO_TEST` | Reservada para pruebas de audio |
| `THEXTECH_DC_EH_BISECT` | Bisecciona `__register_frame_info` paso a paso |

`dc_boot_probe(color)` pinta y sigue; **`dc_boot_halt(color)` apaga el PVR,
pinta y se queda ahí** — imprescindible una vez el PVR arranca, porque redibuja
cada frame y borraría un marcador normal. Para verlos hay que activar
`reicast_emulate_framebuffer` en Flycast (son escrituras directas a VRAM).

### Lección de método

Las primeras bisecciones se hicieron **sobre binarios distintos** (cambiando
código entre medias), así que los resultados no eran comparables y apuntaban a
sitios contradictorios, haciendo perder horas. **Un solo binario, punto de
parada elegido por `-D`.** Ese es el motivo de existir de
`bisect_findworlds.sh`.

---

## Pendiente

### P0 — Rendimiento

Baja de 60 FPS en escenas cargadas. **Causa conocida**: el renderer manda
*todo* a la lista translúcida (TR) del PVR para garantizar el orden de pintor, y
ésa es la ruta lenta del hardware.

**Arreglo**: mandar los sprites de alfa binaria (que son la mayoría — el
conversor ya los marca como `ARGB1555`) a la lista **punch-through (PT)**, que
el PVR resuelve con test de alfa y ordenación por Z. Habría que:

1. Guardar en `StdPictureData` si la textura es PT (fmt 1) u opaca (fmt 2).
2. Abrir las listas OP/PT/TR en `setTargetTexture()` y enviar cada quad a la que
   toque, cuidando que el orden entre listas no rompa el layering.
3. Medir antes y después con el contador de FPS de RetroArch.

### P1 — Guardado persistente (VMU)

`/ram` se pierde al apagar. Hay que escribir en VMU con `vmufs` de KOS. Ojo:
128 KB por bloque de memoria, así que habrá que recortar o comprimir las
partidas guardadas.

### P1 — Memoria

`text 3.9 MB + bss 4.1 MB ≈ 8 MB` de 16 MB; quedan ~8 MB de heap. Vigilar al
cargar niveles grandes. La VRAM son 8 MB aparte, con expulsión LRU.

### P2 — Más campañas

Sólo se empaqueta `cliche` (15 niveles). `the invasion 2` (73 niveles) es el
siguiente; el conversor acepta `--worlds`. Vigilar el tamaño de la imagen y la
memoria al cargar niveles más grandes.

### P2 — Paneo de audio

Aceptado e ignorado; todo suena centrado. `snd_sfx_play` acepta un parámetro
`pan` (0..255, 128 = centro): bastaría con guardar el paneo por canal en
`Mix_SetPanning` y aplicarlo al reproducir.

### P2 — Hotkeys sin mapear

`VanillaCam`, `DebugInfo`, `EnterCheats`, `ToggleHUD`, `LegacyPause` no tienen
botón: el pad de Dreamcast no da para más. Harían falta combinaciones
(p. ej. Start+X).

### P3 — Arreglo de fondo del toolchain

Si algún día se identifica por qué falla `__register_frame_info`, se podrían
recuperar las excepciones y quitar el parser legacy. No bloquea nada hoy.
`dc-chain` **no está instalado**; reconstruir implicaría bajar GCC 13.2 +
binutils + newlib y compilar horas en MSYS.

---

## No hacer / evitar

- No quitar `eh_frame_dreamcast.cpp` ni los flags `--wrap` /
  `--allow-multiple-definition` sin arreglar antes el toolchain.
- No escribir código nuevo que dependa de excepciones C++.
- No quitar `g_use_legacy_pgex_parser = true`.
- No poner `reicast_enable_dsp = disabled` (silencia todo el audio).
- No poner `reicast_gdrom_fast_loading = enabled` (crash al cargar nivel).
- No pasar `-d` a `cdi4dc` con imágenes `-C 0,11702`.
- No usar `-m4-single` (su libstdc++ es sh2e).
- No asumir que `/ram` admite subdirectorios.
- No reintroducir `__16M__` en el define de Dreamcast.
- No `#include <kos.h>` desde headers que entren por `globals.h`.
- No forzar `THEXTECH_ENABLE_EDITOR=OFF` sin stubs de `EditorNPCFrame`.
- No bisecar sobre binarios distintos: un solo build, punto de parada por `-D`.
- No streamear música en loop desde `/cd` ni reabrir Ogg del GD-ROM cada frame
  (stubs de 2–8 s / crash SH4).
- No pasar índices de canal SDL Mixer a `snd_sfx_play_chn` (pisan el stream).
- No volver a meter los workflows CI multiplataforma en este fork sin necesidad.
